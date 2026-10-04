# 项目上下文

本文记录项目定位、正式架构、安全边界、技术选型和长期有效的工程原则。

## 目录

- [1. 项目定位](#1-项目定位)
- [2. 架构决策](#2-架构决策)
- [3. 项目目标](#3-项目目标)
- [4. 组件职责与安全边界](#4-组件职责与安全边界)
- [5. 关键数据流](#5-关键数据流)
- [6. 技术选型](#6-技术选型)
- [7. 工程原则](#7-工程原则)
- [8. 文档权威顺序](#8-文档权威顺序)

## 1. 项目定位

这是一个项目驱动的嵌入式技术提升工程，目标是把FreeRTOS、Classic CAN、W5500网络通信、MQTT、FTP和Bootloader升级串成一条可运行、可测试、可展示的完整链路。Windows侧使用现有CAN工具和MQTTX调试，不开发WPF。2026-10-04用户将终点调整为代码更新、Boot/Application检查及开发归档，最后定稿`docs/项目简历.md`；完整升级尚未提供的实机结果保留为未验收，结项口径见[结项记录](PROJECT_CLOSEOUT.md)。

项目以制冷设备或小型工业控制器为背景，但当前使用开发板和模拟数据完成验证，不接入真实危险负载。它不是简单的传感器 Demo，也不复刻公司的完整产品，只选择业务心跳和升级流程作为工程训练对象。

## 2. 架构决策

### 2.1 当前正式架构

```text
STM32F103ZET6 + FreeRTOS
  ├─ CAN1 ⇄ USB-CAN ⇄ Windows CAN调试/仿真端
  ├─ W5500 / MQTT ⇄ EMQX Broker（Windows侧独立服务）
  │                              ⇅
  │                    MQTTX（Windows业务调试端）
  └─ W5500 / FTP ⇄ FTP Server（Windows独立服务）
```

STM32同时承担CAN节点、MQTT Client和FTP Client。CAN与MQTT是两条独立通信链路，不设置CAN-MQTT转换网关。MQTTX发布`Server2Client/*`命令并订阅`Client2Server/*`设备上行，是连接EMQX的另一个MQTT Client。EMQX只负责连接、订阅和路由。EMQX、FTP Server和MQTTX可以部署在同一台Windows PC，但必须保持为三个职责和生命周期独立的组件。

STM32网络侧采用协议任务分离：`AppNetworkTask`只监督W5500芯片初始化、PHY和全芯片复位；`AppMqttTask`独占MQTT Socket、TCP Client和`MQTTContext_t`；`AppFtpTask`独占FTP控制/数据Socket；P3的`AppStorageTask`独占W25Q64。各协议Task通过一个带优先级继承的FreeRTOS Mutex串行访问共享W5500，但不能共享Socket。详细契约见[W5500多协议任务架构](W5500_MULTITASK_ARCHITECTURE.md)。

W25Q64通过独立SPI1连接，不使用W5500 Mutex。其分区固定为两个独立4 KiB元数据扇区、一个1 MiB候选固件槽和扩展保留区；元数据A/B轮流提交以保证掉电原子性，但不保留旧固件副本。FTP Server原样下发应用工程生成的`application.bin`，文件前512字节是描述头，只保存硬件型号、软件型号和完整文件长度，其余字节全0，不保存MD5或其他字段；候选槽从首地址开始保存完整文件，不依赖独立PC打包工具。STM32内部Flash采用64 KiB裸机Bootloader加448 KiB单应用区，不在内部Flash再做A/B双应用区。W25Q64从一开始就由正式`AppStorageTask`独占，不增加开机擦写自检。

Application镜像只采用一套构建配置和一份应用区链接脚本，不维护“可直烧默认固件/Application”双模式。P3由开发板上此前已烧录的`0x08000000`旧固件完成FTP主流程验收，`firmware/build/`生成的`application.bin`是下载候选文件；Bootloader完成前，新构建镜像不能直接硬复位运行。P4裸机工程固定放在根目录`bootloader/`，拥有自己的CMake、工具链文件、64 KiB链接脚本、启动文件、芯片库、W25Q64 BSP、MD5库及启动/安装代码，产物位于`bootloader/build/`。两个工程不共享源码、头文件、库目录或工程配置，不存在跨工程包含、编译或链接依赖；Boot的元数据编解码和描述头校验在Boot内独立实现。唯一的交接依据是`docs/protocol.md`冻结的数据字节格式、数值和Flash布局，两边各自定义本工程常量并做兼容性验证，不新增跨工程公共契约头。用户已决定首版不做软件版本防降级，安装和新应用未确认启动各最多尝试3次。

当前实现进度只包含W5500启动初始化和运行期PHY轮询，尚未实现运行期全芯片复位与协议Task静默协调。因此当前网络状态不保存上电后不会继续变化的芯片代次；未来实现完整复位流程时，再把复位代次作为真实变化的失效令牌整体接入。

应用首次启动确认采用用户已同意的本地条件：FreeRTOS调度器运行、Storage初始化和元数据读取成功、ADC已有有效采样。确认仅推进有效`BOOT_PENDING`为`CONFIRMED`，保持该记录中的文件长度、MD5和安装/启动次数，完整A/B提交成功后解锁。不依赖MQTT联网，不新增自检任务或重复确认标志；Storage Task继续独占元数据写入，只通过ADC已有快照接口零等待读取样本。2026-10-03用户已录入该增量并通过逻辑静态检查，2026-10-04随MQTT升级增量完成Debug构建；未操作板卡，首次确认仍随正常升级实机验收，不能提前认定P4完成。

MQTT升级命令与结果上报已于2026-10-04按用户明确授权接入Application源码；最终审查修正Startup无条件失败返回及FTP终态进度回落后，Debug重新构建通过；接口与逐文件代码保留在[实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)供对照，正常升级仍待实机验收。Storage保留现有长度1状态Queue作为唯一最新快照，FTP和业务Task都用单次零等待`xQueuePeek`读取；持久状态及代次来自已选中的有效A/B记录，升级请求携带观察代次与本机关联号。新增的长度1发送回执Queue由MQTT Task生产、业务Task消费，最多一笔需回执消息在途；普通消息的请求编号为0。MQTT Task复用原TCP的`xTxInFlight`确认末段SENDOK，不新增发送状态机，也不执行MCU复位。

正常升级由Storage完整提交`UPGRADE_REQUESTED`，再由业务Task依次发送受理ACK与`upgrade=3, rate=0`；每条回执都须匹配本机请求编号及原MQTT会话，并为`SENT`，最后由业务Task检查当前持久状态和本地输出后放行复位。发送或排空失败不自动复位，已提交请求继续锁定下载，可重发开始命令或在放行复位之前取消；手工复位仍会让Boot执行已持久请求。取消须由Storage完整提交回`READY_TO_UPGRADE`后才回复成功。新应用本地提交`CONFIRMED`后，在每个MQTT READY会话补报一次成功；有效`FAILED`且错误为安装前候选拒绝、旧应用仍运行时可以补报失败。该方案不提供QoS 0的最终显示保证或跨重启恰好一次送达，正常升级仍待实机验收。

### 2.2 决策原因

- 现有创芯科技CAN分析仪只支持Windows，因此CAN调试端自然保留在Windows。
- 霸道 V2 已板载 W5500，可以让 STM32 直接建立 TCP、MQTT 和 FTP 连接。
- 板载 W25Q64 提供 8 MB 外部 Flash，适合暂存待升级固件。
- Windows侧已经具备EMQX，并可承载FTP Server，不需要为了服务承载额外保留Linux主机。
- 使用MQTTX即可完成当前业务协议与正常升级验收；用户已取消WPF，不为界面开发延长项目范围。
- 新架构仍能覆盖CAN、RTOS、TCP、MQTT、FTP、Flash和Bootloader，同时保持每个阶段可单独验证。

### 2.3 W25Q64单候选固件与双元数据决策

候选固件和元数据解决的是两个不同问题，副本数量不需要相同。本项目只在W25Q64中保留一份
候选固件，原因如下：

- 新下载命令具有候选包替换权：只要尚未进入Bootloader升级锁定阶段，无论当前候选包是否已经
  升级过，新的合法PREPARE都会先原子提交更高代次的PREPARING，使旧READY失效，再覆盖唯一
  候选槽；A/B只保存元数据，不保存两份固件包。
- 固件完整下载后必须通过文件长度、前512字节描述头和整个`application.bin`的MD5校验，只有全部通过才会
  提交`READY_TO_UPGRADE`，残缺文件不会进入安装流程。
- 下载期间，当前旧应用仍完整保存在STM32内部Flash中。下载失败只需要重新下载，不需要在
  W25Q64中再保存一份旧应用。
- 安装开始后，W25Q64候选包保持不变。若写内部Flash时掉电，裸机Bootloader重新校验该候选包，
  并从应用区起始地址重新安装，因此不需要第二个候选文件来处理安装中断。
- 本项目明确接受不自动回滚的限制：如果新固件虽然完整但存在逻辑错误，Bootloader停留在安全
  故障状态并由ST-Link人工恢复。因此第二份固件不属于当前需求。

元数据仍然保留A/B两份，每份位于独立的4 KiB擦除扇区。原因是W25Q64更新元数据前必须先擦除
整个扇区；如果只有一份记录，擦除或重新写入期间掉电会同时失去旧状态和新状态，Bootloader将
无法可靠判断候选包处于下载中、已经可升级，还是正在安装。A/B更新规则固定为：

1. 保留当前有效副本不动；
2. 擦除另一个元数据扇区；
3. 写入代次更高的新记录及其CRC32；
4. 最后写入`CMIT`提交标记；
5. 启动时从CRC32和提交标记都有效的记录中选择代次较新者。

这样即使在任一步骤掉电，至少仍有一份完整记录可以恢复。两份元数据保护的是状态更新的
原子性，不是为了保存两份固件。

固件完整性只保留一份摘要定义：MD5覆盖完整的`application.bin`，包括前512字节描述头。MQTT `ftpMD5`是下载期间
唯一的期望值，必须与W25Q64实际固件字节的计算结果一致；校验通过后同一个值写入A/B元数据，
供复位后的Bootloader安装前复验。RAM中的命令参数与持久元数据分别服务于下载期间和复位后的
不同生命周期，不是两个相互竞争的权威来源。描述头不保存MD5，也不单独保存CRC32；它作为
完整`.bin`的一部分由同一个MD5覆盖。
MD5只用于受控实验网络的传输错误检测，不提供
固件真实性认证；安全升级仍需后续数字签名方案。

### 2.4 历史架构

`docs/STM32_Linux_MQTT_System_Architecture.docx`记录最早的“STM32 CAN → Linux SocketCAN网关 → MQTT”方案；此前由Linux仅承载MQTT Broker、FTP和站控模拟器的中间方案也已被当前Windows服务架构取代。历史资料只保留作背景参考，不再作为实现依据。

## 3. 项目目标

### 3.1 通信与业务基础目标

- STM32 与 Windows CAN 分析仪双向收发 Classic CAN 报文。
- STM32通过W5500接入局域网并与Windows侧EMQX Broker建立MQTT 3.1.1连接。
- 实现 MQTT Keep Alive、断线重连以及10秒周期业务心跳上报。
- 使用MQTTX验证心跳、温度、电压和LED/蜂鸣器控制协议，不再增加自研界面。

### 3.2 升级目标

- P3先使用MQTTX通过MQTT下发Windows FTP文件下载命令。
- STM32从Windows FTP Server下载固件，流式写入W25Q64，并进行长度与MD5校验。
- 下载成功后，P4先使用MQTTX通过MQTT下发升级命令。
- 本项目不开发FTP Server程序，P3配置FileZilla Server免费标准版Windows 64位最新稳定版，并固定使用`EPSV`被动数据连接；下载和升级命令统一由MQTTX下发。
- Bootloader 将已验证镜像写入 STM32 内部 Flash，并具备掉电保护、镜像有效性检查和失败恢复策略。
- 下载和升级过程按定义上报进度、结果与异常。

术语和阶段边界固定如下：

- **下载**：STM32应用中的FTP Client从Windows FTP Server读取固件字节，流式写入W25Q64暂存区并完成长度与MD5校验。下载阶段以W25Q64中的镜像变为`READY_TO_UPGRADE`为终点，不擦除、不写入MCU内部Flash。
- **升级**：Bootloader从W25Q64读取已经校验通过的镜像，擦除、写入并读回校验STM32内部Flash应用区。升级阶段不连接FTP Server，也不重新下载固件。

### 3.3 项目终点与收尾

- MQTT升级请求、Boot候选安装、内部Flash读回校验、本地首次确认及跨重启上报已实现；代码检查和Application Debug构建作为本次开发收尾依据。
- 按用户2026-10-04决定，不再推进实机验收或WPF等其他业务；普通跳转、正常升级、首次确认及再次复位的未提供结果继续保留为未验收。
- 故障测试按用户决定跳过，保护代码继续保留；不把MD5当作签名，不宣称自动回滚或工业级安全OTA。
- 定稿[项目简历](项目简历.md)，维护[结项记录](PROJECT_CLOSEOUT.md)，由用户建立项目根Git仓库并推送新远端；旧Application Git历史保留，不改写或覆盖原远端。

## 4. 组件职责与安全边界

### 4.1 STM32 负责

- 确定性传感器采集、输出控制、状态机和本地参数校验。
- 急停、联锁、限温、超压、看门狗等安全保护。
- CAN 初始化、编解码、收发队列、超时与错误恢复。
- W5500 网络初始化、Socket 管理和链路状态检测。
- MQTT 连接、认证、Keep Alive、发布、订阅、业务 ACK 和断线重连。
- FTP 下载、W25Q64 写入、MD5 校验和升级状态机。
- Bootloader 镜像校验、内部 Flash 更新和失败恢复。

CAN、MQTT、FTP 和 Flash 操作必须由独立任务或明确状态机隔离；中断与 MQTT 回调只投递事件，不执行长时间网络或 Flash 操作。

### 4.2 Windows CAN 端负责

- 使用创芯科技厂商工具收发和记录 CAN 帧。
- 模拟另一个 CAN 节点，验证 STM32 的收发、超时和异常处理。
- 当前不承担 MQTT 转发，也不是产品运行时必需组件。

### 4.3 Windows基础服务负责

- EMQX作为独立Broker进程，负责MQTT消息路由、认证、连接和会话管理。
- FTP Server作为独立文件服务，提供测试固件文件和受控虚拟目录。
- Broker和FTP Server独立于MQTTX；调试端关闭或重启时，两项基础服务仍可独立运行。
- Windows防火墙、监听端口、FTP被动端口范围和运行日志属于部署配置；真实凭据不得提交仓库。

### 4.4 MQTTX业务调试端负责

- 作为MQTT Client连接EMQX，订阅心跳、温度、电压、命令ACK及下载/升级状态。
- 发布`Server2Client/*`控制、下载和升级命令；心跳只观察，不回复ACK。
- 使用协议规定的`messageId`关联命令与ACK，不把发布成功或受理ACK误认为下载/升级完成。
- 等下载校验成功、候选镜像进入`READY_TO_UPGRADE`后，再下发升级命令。
- 不承担Broker、FTP Server或硬实时控制职责，不开发替代MQTTX的WPF程序。

### 4.5 明确不做

- 不开发WPF Dashboard；MQTT、FTP、Windows基础服务或MQTTX不进入毫秒级实时控制闭环。
- 危险保护不依赖网络、Broker、FTP 或上位机在线。
- P0 不创建具体业务代码；P1 不提前实现 MQTT 或 OTA。
- 当前不引入 TLS、Web、InfluxDB/Grafana、Modbus、RS485 或 CAN FD。
- 不在仓库中保存真实用户名、密码、证书私钥或设备唯一凭据。

## 5. 关键数据流

### 5.1 CAN 联调

```text
Windows 厂商工具 ↔ USB-CAN 分析仪 ↔ CANH/CANL ↔ STM32 bxCAN
```

CAN仅用于本地设备通信、调试和后续业务扩展；CAN与MQTT保持独立，不设置协议转换层。

### 5.2 业务心跳

```text
STM32 → W5500/TCP → Windows侧EMQX → MQTTX
```

MQTT Keep Alive属于传输配置，业务`heartbeat`是STM32每10秒发送的上行状态。用户已经取消心跳ACK解析和180秒业务离线判定；MQTTX只订阅观察，STM32不订阅`Server2Client/heartbeat`。

### 5.3 固件下载与升级

```text
MQTTX --MQTT filedownloadcommand--> STM32应用
Windows FTP Server --FTP/TCP固件字节--> STM32 FTP Client
STM32 FTP Client --流式写入并校验--> W25Q64单候选槽
STM32应用 --MQTT下载结果/进度--> MQTTX
MQTTX --MQTT fileupgradecommand--> STM32应用
STM32应用 --设置升级标志并重启--> Bootloader
W25Q64候选槽 --分块读取--> Bootloader --擦除/写入/读回校验--> STM32内部Flash应用区
升级中断 --重新校验同一候选包--> Bootloader从头重试安装
新应用未确认 --停留Bootloader安全故障状态--> ST-Link人工恢复
```

固件二进制不放入 MQTT Payload。MQTT 只承载下载参数、命令、ACK、进度、结果和异常。
下载状态机和升级状态机必须分离，二者只通过W25Q64中的已校验镜像及`READY_TO_UPGRADE`持久化状态衔接。
单候选槽方案不提供旧版本自动回滚。下载中断时内部旧应用仍然运行；安装中断时裸机Bootloader
依靠A/B元数据中的`INSTALLING`状态重新校验并安装同一候选包。已通过完整性校验但存在
未发现逻辑错误的新程序不能自动恢复旧版本，超过有限启动/安装尝试后停留在Bootloader并由
ST-Link人工恢复；这是本项目明确接受的简化边界。

## 6. 技术选型

| 范围 | 选型 | 说明 |
| --- | --- | --- |
| 开发板 | 野火 STM32F103 霸道 V2 | 已有硬件 |
| MCU | STM32F103ZET6 | bxCAN、512 KB Flash、64 KB SRAM |
| RTOS | FreeRTOS V11.3.0 | 当前使用Task、有界Queue、Task Notification、Mutex和heap_4；未据此宣称已接入看门狗 |
| CAN | Classic CAN + SIT1042AQT/3 | 500 kbit/s，29位扩展数据帧，PB8/PB9，CAN1重映射 |
| CAN 调试 | 创芯科技 CAN 分析仪至尊版 | Windows厂商驱动与工具 |
| 网络控制器 | W5500 | SPI2 硬件 TCP/IP Socket |
| 外部 Flash | W25Q64，8 MB | 固件暂存、校验和恢复数据 |
| Broker | EMQX | 作为Windows侧独立服务运行，不依赖MQTTX；现有版本和运行方式待P2记录 |
| 文件服务 | FileZilla Server 1.12.6，Windows | 版本以用户日志为据；控制端口21，`EPSV`被动端口`50000-50009` |
| PC业务调试端 | MQTTX | 作为MQTT Client观察设备上报并下发命令，不开发WPF |
| 应用协议 | MQTT 3.1.1 + JSON | 先实现心跳与升级子集 |
| 构建 | ARM GCC 14.3.1 + CMake + GNU Make | 不使用Keil；裸机LED、FreeRTOS/USART/Console/诊断、ADC、P1 CAN以及W5500 SPI/静态IP/PHY/Ping基线已通过；分层TCP的双Session收发、超过64 KiB回绕、FIN/EOF、持续退避、PHY恢复、拔线恢复和资源回归均已验收，并形成固件提交`13f1b6b`及标签`freertos-w5500-tcp-v0.1` |

## 7. 工程原则

- 先定义协议和验收条件，再写实现。
- 已知后续消费者时，先冻结可长期复用的公共接口，再按阶段启用最小功能子集；不为一次验收设计随后必然废弃的基础接口。
- MQTT与FTP由独立协议Task运行，每个Task独占自己的Socket和协议上下文；W5500由统一访问门面和同一个Mutex串行化，芯片初始化、PHY与全芯片复位由`AppNetworkTask`集中监督。
- BSP、W5500访问门面、TCP连接管理、Transport和协议状态机分别拥有硬件原语、跨Task互斥、单连接生命周期、字节流适配和协议会话状态，不得互相吞并职责。
- 临时Echo、固定数据量和故障注入必须与长期网络基础设施隔离；测试次数不能成为设备永久停止重连的产品策略。
- 先完成CAN和网络基本能力，再实现W25Q64与FTP下载，然后实现Bootloader升级；按用户最新决定完成代码审查、开发结项和文档归档，不再开发WPF；保留未完成实机验收的事实。
- MQTT回调运行在MQTT Task上下文，只做有界校验、必要复制和Queue投递；业务处理放在其他业务Task中。
- FTP 必须流式下载，不能把整个固件放入 64 KB SRAM。
- 下载镜像必须先写入外部 Flash 并通过长度、MD5 和镜像头校验，禁止边下载边覆盖正在运行的程序。
- Bootloader 必须先验证镜像再擦写内部 Flash；失败恢复策略未评审前，不宣称支持可靠 OTA。
- 所有状态机必须定义超时、有限重试、重复命令处理和断电恢复行为。
- 所有阶段都保留可复现的测试步骤和结果。

## 8. 文档权威顺序

发生冲突时依次遵循：

1. 用户当前明确要求。
2. 根目录 `AGENTS.md`。
3. 本文件的当前架构边界。
4. `docs/NEXT_STEPS.md` 的阶段范围和验收条件。
5. `docs/protocol.md` 的协议定义。
6. 历史架构 DOCX 和 `resources/` 中的参考资料。

架构或协议发生变化时，必须同步更新相关 Markdown、测试计划和受影响端。
