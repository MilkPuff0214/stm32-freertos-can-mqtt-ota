# CAN / MQTT / 固件升级协议草案

本文定义各阶段实现前必须统一的CAN、MQTT、文件下载和升级协议边界；标记为“待确认”的参数不得直接硬编码为正式值。

## 目录

- [1. 文档状态](#1-文档状态)
- [2. 当前实现范围](#2-当前实现范围)
- [3. 节点与角色](#3-节点与角色)
- [4. 硬件接口基线](#4-硬件接口基线)
- [5. CAN 协议](#5-can-协议)
- [6. MQTT 连接参数](#6-mqtt-连接参数)
- [7. Topic 方向](#7-topic-方向)
- [8. JSON 公共结构](#8-json-公共结构)
- [9. MQTT Keep Alive 与业务心跳](#9-mqtt-keep-alive-与业务心跳)
- [10. 文件下载协议](#10-文件下载协议)
- [11. 升级协议](#11-升级协议)
- [12. ACK、重复消息与失败语义](#12-ack重复消息与失败语义)
- [13. 安全与资源限制](#13-安全与资源限制)
- [14. 待确认清单](#14-待确认清单)
- [15. 参考资料](#15-参考资料)

## 1. 文档状态

- 版本：v0.9-code-baseline
- 阶段：P1/P2及P3正常下载已有实机证据；P4源码完成，2026-10-04按用户决定开发结项，完整升级实机未验收
- 实现状态：P1 CAN、P2 MQTT业务和P3 FTP正常下载路径已通过实机验证；用户观察到下载成功及100%结果。P3错误MD5、断网、被动端口阻断等失败矩阵尚未逐项验收，用户决定当前跳过错误测试；Bootloader源码及完整Debug/Release构建已完成，两份ELF烧录读回已有成功日志。2026-10-03应用首次启动确认代码已由用户录入并通过逻辑静态检查，2026-10-04随升级增量完成Debug构建，尚未实机确认；应用既有系统调用占位警告仍保留记录。MQTT升级受理的Storage快照、发送完成回执、失败策略和跨重启上报方案已获用户批准，手工改码见[实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)；2026-10-04用户明确授权Codex接入Application源码并完成Debug构建，未修改Boot或烧录；普通启动及完整升级实机结果未补齐，不认定P4完成
- 适用架构：STM32通过CAN与Windows调试端通信，并通过W5500连接Windows侧EMQX Broker和Windows FTP Server；MQTTX作为Windows业务调试Client，不开发WPF

开发结项与Git归档入口为[结项记录](PROJECT_CLOSEOUT.md)。本次最终审查修正Application启动创建失败返回及FTP终态进度，并重新完成Debug构建；协议数值和Boot字节格式不变。本文件保留为代码维护依据。P1的ADC遥测、输出控制和应用层ACK已经统一为29位Classic CAN扩展数据帧并完成实机验收。P2已经完成分层TCP、MQTT CONNECT/Keep Alive/重连、入站Payload复制、心跳与温度上报，以及LED/蜂鸣器控制和ACK。P3的W25Q64、Storage和FTP正常下载路径已经接通；P4正常启动和升级实机步骤保留在[阶段记录](NEXT_STEPS.md)中，但按用户结项决定不再继续；Boot维护入口为[独立工程说明](../bootloader/README.md)。当前跳过人为故障测试，不删除程序中的状态、长度、型号、MD5校验或失败保护，也不将未执行项写成实测通过。

用户最新决定不增加RTC或网络校时，将Word中的GMT+8日期字符串改为发送端进程启动后的运行秒数。这是破坏性协议调整，因此业务版本从`0.0.1`升级为`0.0.2`；Word仍是其余字段和Topic的主要依据，但时间戳类型、心跳无ACK及当前控制语义以本文件v0.7和用户最新决定为准。

## 2. 当前实现范围

### 2.1 P1：CAN 最小闭环

- STM32 每约1秒上报一次电位器和内部温度传感器的ADC原始值。
- Windows CAN 分析仪下发红灯和蜂鸣器的目标状态。
- STM32 校验并执行控制命令，再返回包含执行结果和已应用状态的应用层 ACK。
- 验证错误计数、Bus-Off 和恢复。

### 2.2 P2：MQTT 业务

- MQTT 3.1.1 CONNECT/CONNACK、订阅、发布和断线重连。
- MQTT Keep Alive。
- 每10秒上报业务心跳`heartbeat`和`temperature`；心跳不要求业务ACK。
- 接收`control`命令，设置红色LED1或蜂鸣器并返回业务ACK。

### 2.3 P3：W25Q64与FTP下载

- W25Q64分区、最大固件长度、A/B元数据和原子提交规则已经冻结；FTP原样下载应用工程生成的`application.bin`，其前512字节为不含MD5的应用描述头；SPI1基础驱动、器件识别和Storage Task唯一所有权已经完成，当前扩展下载事务、读写擦除与掉电恢复。
- 实现MQTT文件下载命令与ACK、FTP流式下载、真实进度、长度与MD5校验。
- P3使用MQTTX发布下载命令；测试工具不改变正式Topic、JSON或ACK语义。

### 2.4 P4：Bootloader升级

- 实现MQTT升级命令与ACK、升级意图持久化和应用到Bootloader的受控切换。
- Bootloader只使用W25Q64中的已校验镜像完成内部Flash更新、启动校验和失败恢复。
- 首版不按软件版本号防降级；安装尝试最多3次，新应用未确认时启动尝试最多3次，耗尽后等待ST-Link人工恢复。
- P4仍可使用MQTTX发布升级命令，升级状态和结果必须遵守本文协议。

### 2.5 项目收尾

- 原P5 WPF阶段取消；MQTTX作为最终业务调试端，以MQTT 3.1.1连接Windows侧EMQX。
- 正常升级、首次启动确认、成功结果上报及再次复位运行验证通过后，定稿[项目简历](项目简历.md)并结束项目。
- 简历收尾不新增协议字段、Topic或业务功能；本次范围变更不改变业务协议版本`0.0.2`。

术语严格固定：“下载”是STM32作为FTP Client从Windows FTP Server读取固件并写入W25Q64，终点是W25Q64中的已校验镜像；“升级”是Bootloader从W25Q64读取该镜像并写入STM32内部Flash应用区。下载阶段不得写内部Flash，升级阶段不再进行FTP传输。

Word已经定义MQTT温度/电位器上报、心跳、LED/蜂鸣器控制、下载和升级报文；`temperature`和`control`已经随P2业务增量通过实机验收，心跳只周期上报。P3实现下载报文和数据链路，P4实现升级流程；正常升级验收后结束，不再接入WPF。

## 3. 节点与角色

| 角色 | 连接 | 职责 |
| --- | --- | --- |
| STM32 设备端 | CAN + W5500 Ethernet | CAN 节点、MQTT Client、FTP Client、固件暂存和升级 |
| Windows CAN 端 | 创芯科技 USB-CAN | CAN 测试、仿真、抓帧和人工验收 |
| Windows侧EMQX Broker | Ethernet | 独立服务；负责MQTT消息路由、认证和连接管理 |
| MQTTX | MQTT | 发布`Server2Client/*`命令并订阅`Client2Server/*`设备上行；用于控制、下载/升级及ACK、遥测和进度结果联调 |
| Windows FTP Server | FTP/TCP | 独立服务；提供固件文件，不参与MQTT路由 |

STM32 的 `deviceId`、MQTT Client ID 和 CAN 节点地址是三个不同概念，不得混用。

EMQX、FTP Server和MQTTX可以运行在同一台Windows PC，但逻辑上是三个独立组件。EMQX只负责MQTT连接、订阅和消息路由，不生成业务消息或执行升级；关闭或重启MQTTX不得连带停止Broker或FTP Server。

## 4. 硬件接口基线

| 功能 | 已确认配置 |
| --- | --- |
| MCU | STM32F103ZET6，72 MHz，512 KB 内部 Flash，64 KB SRAM |
| CAN | CAN1 重映射，PB8=CAN_RX，PB9=CAN_TX |
| CAN 收发器 | SIT1042AQT/3，CANH/CANL，板载 R40=120 Ω |
| CAN 供电 | J40 的 1-2 跳帽控制 CAN/485_3V3；原理图标注默认不连接，使用前必须检查实物已短接 |
| W5500 SPI | PB13=SCK，PB14=MISO，PB15=MOSI |
| W5500 控制 | PG9=CS，PG8=INT，PG15=RST |
| 外部 Flash | W25Q64，8 MB，独立于 W5500 的 SPI 总线 |

PB8/PB9 与摄像头 D0/D1 复用，本项目不同时启用摄像头。

## 5. CAN 协议

### 5.1 总线参数

| 参数 | 当前值 |
| --- | --- |
| 物理层 | Classic CAN，CANH/CANL |
| 帧格式 | 29位Classic CAN扩展数据帧；不混用11位标准帧，不使用远程帧或CAN FD |
| bxCAN内核时钟 | PCLK1=36 MHz；当前HCLK=72 MHz且APB1二分频，CAN不使用APB定时器的倍频规则 |
| 波特率 | 500 kbit/s |
| Prescaler | 4；单个时间量子为4 / 36 MHz = 111.111 ns |
| 位段 | Sync Segment=1 TQ，BS1=15 TQ，BS2=2 TQ，总计18 TQ |
| SJW | 1 TQ |
| 采样点 | (1 + 15) / 18 = 88.89% |
| 实际波特率与误差 | 36 MHz / (4 × 18) = 500 kbit/s，误差0% |
| 终端电阻 | 开发板固定 120 Ω；分析仪端终端状态待确认 |

18 TQ方案在精确得到500 kbit/s的同时，保留了比8 TQ方案更细的位段调整粒度；88.89%采样点适合当前短距离实验总线。SJW先使用常见且保守的1 TQ，BS2仍保留2 TQ。该位时序只适用于当前PCLK1=36 MHz；后续若修改系统时钟或APB1分频，必须重新计算，不能继续沿用Prescaler=4。

总线断电后测量 CANH 与 CANL 约为 60 Ω，才可判定两端各有一个 120 Ω 终端电阻。不得在每个节点重复接入终端电阻。

### 5.2 P1 扩展ID布局与学习帧总表

P1使用一个项目自定义的29位扩展ID布局。它借鉴工业CAN协议常见的“优先级、消息类型、源地址、目标地址”分区思路，但不是J1939，也不实现PGN或动态地址声明。

```text
Bit 28        Bit 26 Bit 25             Bit 16 Bit 15       Bit 8 Bit 7        Bit 0
+------------------+--------------------------+------------------+------------------+
| Priority（3 bit）| MessageType（10 bit）   | Source（8 bit）   | Destination（8）  |
+------------------+--------------------------+------------------+------------------+
```

扩展ID按下式组合：

```text
ExtId = (Priority << 26) | (MessageType << 16) | (Source << 8) | Destination
```

| 字段 | 位范围 | 掩码 | 语义 |
| --- | --- | --- | --- |
| `Priority` | bit28～bit26 | `0x1C000000` | `0～7`；数值越小，CAN仲裁优先级越高；`0`为未来真正的紧急或安全报文保留 |
| `MessageType` | bit25～bit16 | `0x03FF0000` | 固定表示报文语义，范围`0x000～0x3FF` |
| `Source` | bit15～bit8 | `0x0000FF00` | 发送节点的协议逻辑地址 |
| `Destination` | bit7～bit0 | `0x000000FF` | 目标节点的协议逻辑地址；不会改变CAN物理层所有节点都能看到报文的广播特性 |

方向不再占用单独的bit，而由`Source → Destination`唯一确定，避免方向字段与地址字段产生矛盾。P1控制命令不携带事务序号，Windows端必须采用“同一时间最多只有一个待确认命令”的串行方式等待ACK；应用层重试继续使用完全相同的ID、DLC和Payload。

P1逻辑地址分配：

| 地址 | 节点 | 规则 |
| --- | --- | --- |
| `0x01` | 当前STM32节点 | 当前唯一正式设备节点 |
| `0xF0` | Windows USB-CAN测试端 | 只用于CAN调试、仿真和验收 |
| `0xFF` | 广播目标地址 | 只允许出现在`Destination`；不得作为发送节点的`Source` |

P1只定义三种扩展数据帧。控制与ACK使用Priority 1，周期遥测使用Priority 4；同为Priority 1时，`MessageType=0x001`的控制命令先于`MessageType=0x002`的ACK参与后续仲裁位比较。

| 扩展CAN ID | Priority | MessageType | Source→Destination | 作用 | DLC | 应答规则 |
| --- | ---: | ---: | --- | --- | ---: | --- |
| `0x0401F001` | 1 | `0x001` | Windows `0xF0` → STM32 `0x01` | 设置红灯和蜂鸣器目标状态 | 1 | STM32使用`0x040201F0`返回应用层ACK |
| `0x040201F0` | 1 | `0x002` | STM32 `0x01` → Windows `0xF0` | 控制命令应用层ACK和已应用状态 | 2 | 不再应答ACK帧 |
| `0x110001FF` | 4 | `0x100` | STM32 `0x01` → 广播 `0xFF` | 周期上报两路ADC原始值 | 6 | 不使用应用层ACK；后续周期数据覆盖旧数据 |

此前v0.3草案中的`0x100`、`0x101`和`0x180`为尚未进入固件的11位标准帧ID，v0.4起直接废止，不保留标准帧与扩展帧双格式兼容。STM32接收路径必须同时校验29位ID、`IDE=扩展帧`、`RTR=数据帧`和DLC；标准帧、远程帧及未定义扩展ID均不解释为业务命令。v0.5取消了控制命令序号，控制帧和ACK的DLC分别缩减为1和2。

所有多字节整数使用小端序，即低有效字节在前。编码和解码必须逐字段完成，不得把C结构体连同潜在的填充字节直接复制为CAN Payload。

### 5.3 ADC 周期上报 `0x110001FF`

当前ADC1由ADC Task独占，每约1秒依次采集PC1/ADC1_IN11板载电位器和ADC1_IN16内部温度传感器。两个字段都是12位ADC原始码，单位为`ADC count`；内部温度字段不是摄氏温度。

| 字节 | 字段 | 类型 | 范围与语义 |
| --- | --- | --- | --- |
| Byte 0 | `sequence` | `uint8_t` | 上报序号，`0～255`循环；设备复位后从0开始 |
| Byte 1 | `valid_flags` | 位图 | bit0=电位器有效，bit1=温度ADC有效；bit2～7固定为0 |
| Byte 2～3 | `potentiometer_adc_raw` | `uint16_t`，小端 | 有效值`0～4095`，无效值`0xFFFF` |
| Byte 4～5 | `mcu_temperature_adc_raw` | `uint16_t`，小端 | 有效值`0～4095`，无效值`0xFFFF`；不得解释为摄氏度 |

上报周期基线为1000 ms。每生成一次新的采样报告，`sequence`增加1；即使本次报告因发送请求Queue已满而未能提交，也继续增加，使接收端能够从后续序号跳变观察到丢失。序号从255回绕到0属于正常行为。

ADC周期遥测不做应用层重试和ACK。它表示当前时刻附近的状态，旧样本失去实时价值；丢失一帧时等待下一周期即可。CAN控制器仍使用协议自身的ACK和自动重发机制处理总线级发送。

### 5.4 输出控制命令 `0x0401F001`

第一版只控制当前未被按键Task使用的红灯和板载蜂鸣器。协议只描述与硬件有效电平无关的逻辑开关状态；红灯的低电平有效和蜂鸣器的高电平有效由各自BSP处理。

| 字节 | 字段 | 语义 |
| --- | --- | --- |
| Byte 0 | `target_state` | bit0=红灯，bit1=蜂鸣器；对应bit为0表示关闭，为1表示开启；bit2～7必须为0 |

命令使用绝对状态`SET OFF/ON`，不使用`TOGGLE`。因此完全相同的命令因ACK丢失而重发时，不会把输出再次翻转，命令天然具备幂等基础。

### 5.5 控制命令 ACK `0x040201F0`

CAN帧中的硬件ACK只说明至少一个其他节点在总线层正确接收了帧；`0x040201F0`才表示STM32应用已经校验并处理控制命令。

| 字节 | 字段 | 语义 |
| --- | --- | --- |
| Byte 0 | `result` | 执行结果，见下表 |
| Byte 1 | `applied_state` | bit0=红灯、bit1=蜂鸣器的已应用逻辑状态；bit2～7固定为0 |

| `result` | 名称 | 语义 |
| --- | --- | --- |
| `0x00` | `OK` | 命令合法，目标状态已经应用 |
| `0x01` | `INVALID_DLC` | DLC不是1 |
| `0x02` | `INVALID_STATE` | `target_state`的bit2～7存在非0值 |

参数错误时不改变当前输出，`applied_state`返回命令处理完成后的逻辑状态。Windows只有在收到符合当前请求预期的`result`和`applied_state`后，才把本次命令判定为完成；CAN硬件ACK不能替代该应用层判断。

### 5.6 超时、重试、重复命令与初始状态

- Windows发送`0x0401F001`后等待`0x040201F0`，应用层ACK超时为100 ms。
- Windows同一时间最多发送一个尚未完成的控制命令；收到ACK或完成全部超时重试前，不得流水线下发下一条命令。
- 超时后最多重试2次；重试必须使用完全相同的扩展ID、DLC和Payload。
- 命令使用绝对目标状态而不是`TOGGLE`，所以重复执行相同Payload仍得到相同输出；STM32不保存最近命令序号或重复命令缓存，每次合法重试都可以重新设置相同状态并返回ACK。
- P1的ACK没有事务序号，只适用于单Windows控制端、单个在途命令的最小闭环；未来若需要并发命令、多个控制端或严格识别迟到ACK，必须重新引入事务ID并升级协议版本。
- STM32复位后，红灯和蜂鸣器的协议初始状态均为关闭。
- 未定义CAN ID直接忽略，不返回通用ACK；接收过滤器和软件校验不得把未知ID解释成控制命令。
- Bus-Off期间不能保证100 ms内返回ACK；Windows在有限重试结束后报告失败，不无限重发。STM32的Bus-Off有限恢复规则在位时序和错误处理步骤中另行确定。

## 6. MQTT 连接参数

业务协议以`docs/协议制定.docx`和用户后续确认结论为准。Clean Session、持久会话、QoS、Retain、Keep Alive和LWT不属于业务Topic或JSON字段，不得据此增加业务状态、字段或报文；但MQTT 3.1.1实际报文仍要求底层为相应标志和参数提供明确取值，具体传输配置在MQTT接入步骤中单独冻结，不能依赖库的隐式默认值。

| 参数 | P2基线 |
| --- | --- |
| 协议版本 | MQTT 3.1.1 |
| 传输 | 实验阶段 MQTT over TCP |
| Broker地址 | 当前Windows主机`192.168.101.11` |
| 端口 | 1883；实机验收时核对EMQX监听和防火墙 |
| TLS | 本项目不实现；不把8883配置为无TLS端口 |
| Client ID | `stm32f103-0253544d3201`；由当前W5500静态MAC `02:53:54:4D:32:01`生成，不与业务`deviceId`混用 |
| Username/Password | 使用Word协议指定的实验账号，由仓库外的本机私有配置注入，不在项目Markdown、源码、日志或Git中保存明文密码 |
| Clean Session | `true`；每次连接都建立新会话，不恢复Broker中的旧订阅或未完成消息 |
| Keep Alive | `30 s`；只负责Client与Broker之间的MQTT协议保活 |
| LWT | 当前不使用；CONNECT不携带Will Topic或Will Payload，后续不得在没有协议变更时自行增加 |
| QoS | 所有PUBLISH和SUBSCRIBE均使用QoS 0 |
| Retain | 所有业务消息均为`false`，不在Broker保存保留消息 |
| 业务协议版本 | 固定字符串`0.0.2` |

W5500只提供TCP Socket；MQTT报文编解码和重连由STM32 MQTT库及其网络适配层实现。由于Clean Session为`true`，每次MQTT重连成功后都必须重新订阅当前阶段需要的全部下行Topic，不能依赖Broker恢复旧订阅。QoS 0不产生MQTT PUBACK；`control`、`filedownloadcommand`和`fileupgradecommand`仍使用业务ACK，`heartbeat`按用户最新决定不使用业务ACK。MQTTX作为独立Client，使用MQTT 3.1.1以匹配STM32；EMQX独立运行，不依赖调试端。

## 7. Topic 方向

| 方向 | Topic 格式 | 发布者 | 订阅者 |
| --- | --- | --- | --- |
| 上行 | `Client2Server/{method}` | STM32 | MQTTX（经EMQX） |
| 下行 | `Server2Client/{method}` | MQTTX | STM32（经EMQX） |

EMQX只负责中转和权限控制，不是业务消息的发布者或最终处理者。Topic大小写敏感，`Client2Server`与后面的`/`之间、`Server2Client`与后面的`/`之间均不得出现空格；`method`必须使用下表给出的全小写字符串。

当前子集：

| Topic | 用途 | 固定`messageId` | 业务ACK |
| --- | --- | --- | --- |
| `Client2Server/temperature` | STM32每10秒上报ADC采集值 | `0` | 无 |
| `Client2Server/heartbeat` | STM32每10秒发送业务心跳 | `1` | 无 |
| `Server2Client/control` | 下发LED或蜂鸣器控制 | `4` | `Client2Server/control`使用同一`messageId`回复 |
| `Server2Client/filedownloadcommand` | 下发FTP下载参数 | `6` | `Client2Server/filedownloadcommand`使用同一`messageId`回复 |
| `Client2Server/filedownloadresult` | 下载进度和最终结果 | `2` | 无 |
| `Server2Client/fileupgradecommand` | 取消升级或开始升级 | `8` | `Client2Server/fileupgradecommand`使用同一`messageId`回复 |
| `Client2Server/filedownloadprocess` | 升级进度和结果 | `3` | 无 |

Word中ACK表格出现的`5`、`7`和`9`不再使用；根据用户确认，ACK必须沿用对应请求的固定`messageId`。这些编号是报文类型标识，不是逐报文递增的事务ID。

## 8. JSON 公共结构

业务Payload使用JSON对象，并固定分为`header`和`body`：

```json
{
  "header": {
    "method": "heartbeat",
    "messageId": "1",
    "version": "0.0.2",
    "timestamp": 456,
    "deviceId": "192.168.101.50"
  },
  "body": {
    "result": true
  }
}
```

公共字段语义固定为：

- `method`：必须与Topic末段一致，并使用Topic表中的全小写字符串。
- `messageId`：使用Topic表中固定的字符串编号；ACK与对应请求保持一致。
- `version`：本项目业务协议版本，固定为字符串`0.0.2`，不是固件版本。
- `timestamp`：非负JSON整数，单位为秒，表示消息发送进程启动后的单调运行时间。STM32由FreeRTOS Tick累计得到；PC下发报文按同一规则填写，MQTTX不被假定会自动生成该值。任一端重启后从0重新开始。它不是UTC或本地日期时间。
- `deviceId`：固定使用设备当前配置的IPv4地址字符串；当前实机为`192.168.101.50`。除明确位于`body`中的业务字段外，所有报文及ACK都统一使用该字段，不使用`ipClient`替代。

JSON键名不得包含Word示例排版产生的首尾空格。`CPUTemp`、`PotentiometerVoltage`和其他数值字段必须作为JSON Number发送，`result`必须作为JSON布尔值`false`或`true`发送，均不得加引号；`CPUTemp`与`PotentiometerVoltage`直接使用ADC Task发布的物理量快照，不在MQTT模块内二次采样。

STM32不得把`xTaskGetTickCount()`的原始毫秒Tick直接当成秒。业务Task通过每轮Tick差值累计64位运行Tick，再除以`configTICK_RATE_HZ`生成`timestamp`，从而处理32位Tick回绕。设备运行秒数只用于诊断和识别复位，MQTTX收到消息的本地时间与它不是同一时钟。

`Client2Server/temperature`的`body`固定为：

| 字段 | JSON类型 | 来源 |
| --- | --- | --- |
| `CPUTemp` | Number | MCU内部温度传感器估算温度，单位°C；按数据手册典型值换算，不作为精密环境温度 |
| `PotentiometerVoltage` | Number | 板载电位器采样电压，单位V；当前按`VDDA=3.3 V`换算 |

`Server2Client/control`的`body`固定为`controlType`和`control`两个JSON整数：`controlType=1`表示红色LED1，`controlType=2`表示板载蜂鸣器；`control=1`表示开，`control=2`表示关。当前不实现LED闪烁状态机。`Client2Server/control` ACK原样返回这两个字段，并增加JSON布尔字段`result`表示执行结果。命令使用绝对状态，不使用`TOGGLE`；CAN和MQTT都通过唯一输出控制服务写硬件，最后一个合法命令生效。

命令ACK固定使用公共`header`。下载命令ACK还按Word保留`ftpServer`字段，并把Word中的`ipClient`统一改为`deviceId`；下载命令ACK与升级命令ACK的`body`均只使用JSON布尔字段`result`。心跳不使用业务ACK。

解析器必须限制 JSON 总长度、字符串长度和数组元素数量；缺少必填字段、类型错误或超限时必须拒绝命令，不能使用未初始化默认值继续执行。

## 9. MQTT Keep Alive 与业务心跳

### 9.1 两种机制

- MQTT Keep Alive是MQTT传输层参数，不进入Word业务协议；当前固定为30秒。
- 业务心跳严格使用`heartbeat` JSON，由STM32周期上报，不要求PC调试端回复ACK。

MQTT连接状态由MQTT Client状态机维护。STM32不根据业务心跳判定PC调试端在线；MQTTX只订阅观察，不反向影响STM32控制和安全逻辑。

### 9.2 固定时序

| 参数 | 固定值 |
| --- | --- |
| 业务心跳周期 | 10秒 |
| 心跳Topic | `Client2Server/heartbeat` |
| 心跳ACK | 不使用 |

状态机：

1. MQTT连接和`Server2Client/control`订阅完成后，STM32立即发布一条`Client2Server/heartbeat`，之后每10秒发布一次，`messageId`固定为字符串`1`。
2. MQTTX订阅心跳用于观察，不发布`Server2Client/heartbeat`。
3. MQTT连接断开时进入有上限间隔的持续退避重连，不积压历史心跳；PHY恢复时立即开始新一轮连接。
4. 新MQTT Session进入READY后立即生成新的当前心跳。
5. 心跳中的`timestamp`只记录STM32进程运行秒数，不用于校时；设备重启后从0重新计数。

## 10. 文件下载协议

### 10.1 下载命令

MQTTX发布`Server2Client/filedownloadcommand`，Body包含：

```json
{
  "ftpServer": "<server-ip>",
  "ftpPort": 21,
  "ftpUser": "<runtime-user>",
  "ftpPassword": "<runtime-password>",
  "ftpPath": "<remote-path>",
  "ftpFileName": "<firmware-file>",
  "ftpMD5": "<32-hex-md5>"
}
```

固件内容不经过 MQTT。STM32 收到命令后先校验字段和当前状态，再通过`Client2Server/filedownloadcommand`返回“命令已接受/拒绝”的ACK；ACK的`messageId`与请求同为字符串`6`，该ACK不表示文件已经下载成功。请求中的`ftpPort`必须是JSON整数，所有报文和ACK的设备字段统一为`deviceId`。

`ftpMD5`固定表示完整`application.bin`的MD5，包括文件前512字节描述头，使用32个十六进制字符，也是下载事务的唯一
期望MD5。Storage对写入W25Q64并读回一致的全部固件字节计算MD5并与它比较，成功后把同一个值
写入A/B元数据，供复位后的Bootloader复验。描述头只保存硬件型号、软件型号和完整文件长度，
不保存MD5、CRC32或其他字段；描述头本身包含在完整文件MD5范围内。

`ftpServer`填写Windows FTP Server可被STM32访问的局域网地址。`ftpPath`是FTP虚拟根目录下的远程逻辑路径，不得下发或记录Windows本机绝对路径；具体FTP软件、虚拟根目录映射和防火墙规则属于部署配置，不进入业务Payload。

### 10.2 下载状态机

下载目标固定为W25Q64单候选固件槽。`DOWNLOAD_SUCCESS`或`READY_TO_UPGRADE`只表示W25Q64中的候选镜像已经完成长度与MD5校验，不表示MCU内部Flash已经发生改变。新下载会先用A/B元数据使旧候选失效，再擦除候选槽需要的扇区。

```text
IDLE
  → VALIDATE_COMMAND
  → FTP_CONNECT
  → FTP_LOGIN_TYPE_AND_SIZE
  → VALIDATE_REMOTE_SIZE
  → ERASE_TARGET_SLOT
  → FTP_DOWNLOAD_AND_WRITE
  → VERIFY_LENGTH_AND_MD5
  → READY_TO_UPGRADE
```

任一步失败进入 `DOWNLOAD_FAILED`，关闭 FTP Socket、保留错误码并上报最终失败。新命令只有在状态机允许时才能开始。

### 10.3 下载规则

- FTP数据连接固定使用`EPSV`：STM32只发送`EPSV`并解析`229`端口，不实现`PASV/227`回退；数据连接始终复用控制连接对端IP。
- Windows侧固定选择FileZilla Server免费标准版Windows 64位最新稳定版；安装后记录准确版本号，不使用Pro Enterprise、Beta或Nightly。
- FTP控制监听固定为`192.168.101.11:21`，被动数据端口固定为TCP `50000-50009`；FileZilla Server与Windows防火墙配置必须一致。
- MQTT 长连接占用一个 W5500 Socket；FTP 控制和数据连接还需要两个 Socket。
- 固件必须小块流式接收和写入，禁止将整个文件缓存到 64 KB SRAM。
- 只有`SIZE`非零且不超过固件包和候选槽上限后，才能使旧候选失效并按扇区擦除候选槽。下载期间在RAM更新已完成字节数和流式MD5，不为每个数据块擦写持久化元数据。
- 下载进度每跨越 5% 上报一次，不按每个 TCP 分片上报。
- 参考协议规定下载超过 10 分钟判定失败；本项目还需定义连接、读数据和整体三个独立超时。
- 只有长度和 MD5 都正确时才能设置 `READY_TO_UPGRADE`。
- FTP 凭据只保存在运行时受控内存中，不写入日志、源码或普通参数区。

### 10.4 下载结果

`Client2Server/filedownloadresult` Body：

| 字段 | 类型 | 语义 |
| --- | --- | --- |
| `fileDownloadResult` | Int | 1=成功，2=失败，3=下载中 |
| `fileDownloadRate` | Int | 0-100 的下载进度 |

错误原因字段在参考协议中未定义。本项目需要在正式实现前决定是增加兼容扩展字段，还是通过独立日志/诊断 Topic 表达。

MQTTX发布下载命令后，受理ACK只表示命令已受理；`fileDownloadResult=3`及`fileDownloadRate`表示实际下载进度。只有最终成功且候选镜像进入`READY_TO_UPGRADE`后，才下发开始升级命令，不以发布成功替代设备结果。

## 11. 升级协议

升级源固定为W25Q64中的`READY_TO_UPGRADE`镜像，目标固定为STM32内部Flash应用区。该阶段由Bootloader执行，不使用FTP Client，也不从Windows FTP Server重新获取文件。

### 11.1 升级命令

MQTTX发布`Server2Client/fileupgradecommand`：

| `upgrade` | 语义 |
| --- | --- |
| 1 | 取消升级 |
| 2 | 开始升级 |

STM32通过`Client2Server/fileupgradecommand`返回受理ACK；ACK的`messageId`与请求同为字符串`8`。开始升级前必须满足：

- 存在 `READY_TO_UPGRADE` 镜像。
- 描述头硬件型号与本机型号完全一致，完整文件长度和MD5再次校验通过。
- 设备处于允许升级的安全状态。
- 供电条件满足升级要求。

此ACK只表示升级意图已经持久受理，不是安装成功证明。应用在受理时检查`READY_TO_UPGRADE`和本地安全状态；Bootloader在擦除内部Flash之前重新校验型号、描述头、长度和完整文件MD5。当前仅在受控稳定实验供电下验证，尚未实现自动低电压阻止安装能力，不应把该供电条件写成已验收功能。

MQTT升级受理已按批准方案接入源码并于2026-10-04通过Debug构建，正常升级尚待实机验收。Storage完整提交`UPGRADE_REQUESTED`后才发送
`result=true`的受理ACK，ACK的本机请求编号和原MQTT会话必须得到匹配的`SENT`回执，随后发送
`upgrade=3, rate=0`并等待它的匹配`SENT`回执。`SENT`要求Publish成功、原TCP会话仍有效且末段
W5500 SENDOK已确认；仅业务Task在全部条件成立且本地输出关闭后放行复位。
本机请求编号与回执是内部Queue契约，不加入MQTT JSON，不改变固定`messageId=8`或业务版本`0.0.2`。

Queue满、发送失败、会话变化或发送完成等待超时均不自动复位，已提交的`UPGRADE_REQUESTED`
继续保留并禁止新下载；可重发开始命令，或在业务Task放行复位之前请求取消。手工复位仍会
让Boot执行已持久请求。取消必须由Storage完整提交回`READY_TO_UPGRADE`后才回复成功，
进入Boot安装后不能通过MQTT取消；提交结果不确定时保持锁定，复位后重新选择有效A/B记录。

同一候选已持久处于`UPGRADE_REQUESTED`且运行期无未结束请求时，重发开始可幂等受理，
不再次擦写该意图。原开始仍在提交时重复命令等待原最终ACK；已受理且发送/复位放行尚在处理时，
只回复当前受理事实，不建立第二笔升级。取消处理期间另一开始请求明确拒绝；重复取消等待原取消ACK。
超时但完成未知时先保留原关联号收尾，不能立即插入另一请求覆盖其回执。

### 11.2 W25Q64 镜像元数据

W25Q64分区和元数据格式已经冻结，以下字节协议是Application与Bootloader之间的交接依据。
应用侧的独立实现位于`firmware/User/app/storage/`，Boot侧在`bootloader/`内独立实现；
两者不共用源码、头文件、类型或构建配置，只要求读写的实际字节兼容。
应用与Bootloader必须遵守以下共同约束：

- `0x000000～0x001FFF`是4 KiB元数据A/B扇区；每份记录固定256字节。
- `0x010000～0x10FFFF`是1 MiB候选固件槽。
- `0x110000～0x7FFFFF`是扩展保留区，当前应用不得临时占用。
- 候选固件槽只保存当前待安装包；新下载允许覆盖旧候选，但必须先使旧`READY_TO_UPGRADE`失效。
- FTP文件就是应用工程生成的`application.bin`，最大`0x70000`字节；前512字节为描述头，候选槽首字节就是描述头首字节。
- 元数据新记录先写主体并读回校验，最后单独写`CMIT`提交标记；旧记录在新记录完整提交前始终保留。
- 上电只接受magic、版本、长度、地址范围、CRC32和提交标记全部正确的记录，并在A/B中选择更新的`generation`。
- 只有`READY_TO_UPGRADE`允许进入升级；`PREPARING`、`DOWNLOADING`或`VERIFYING`复位后必须从头下载。

元数据版本固定为 **3**。A扇区起点`0x000000`，B扇区起点`0x001000`，各自起始的256字节为记录。
以下偏移相对于每份记录首字节；所有多字节整数为小端，MD5为16字节原始摘要，不是32字符文本：

| 偏移 | 字节数 | 字段与固定约束 |
| --- | ---: | --- |
| `0x000` | 8 | magic：`53 54 47 57 4D 44 00 00`，即`STGWMD\0\0` |
| `0x008` | 2 | 格式版本：`3` |
| `0x00A` | 2 | 记录长度：`256` |
| `0x00C` | 4 | `generation`：提交代次，按32位无符号整数递增 |
| `0x010` | 4 | 持久状态数值，见下表 |
| `0x014` | 4 | `lastError`：原始错误数值，P4值见11.5节 |
| `0x018` | 4 | `firmwareLength`：完整`.bin`文件长度，包含512字节描述头 |
| `0x01C` | 16 | 完整文件MD5摘要；Boot安装前与候选实际摘要比较 |
| `0x02C` | 4 | `installAttempts`：已持久记录的安装尝试次数 |
| `0x030` | 4 | `bootAttempts`：新应用未确认时的启动尝试次数 |
| `0x034` | 196 | 保留区：全部为`0` |
| `0x0F8` | 4 | CRC32：只覆盖前248字节，即`0x000～0x0F7` |
| `0x0FC` | 4 | 提交标记：`43 4D 49 54`，即`CMIT`，必须最后单独编程 |

CRC算法与现有应用一致：初值`0xFFFFFFFF`，按字节低位优先、逐位右移，反射多项式
`0xEDB88320`，最终异或`0xFFFFFFFF`。CRC字段和CMIT不计入CRC覆盖范围。
先编码并写入前252字节（主体含CRC），确认芯片Ready且读回一致，再写最后4字节CMIT；
整个256字节再次读回并解码通过后才认为提交成功。不得将C结构体的内存布局直接作为记录格式。

| 数值 | 持久状态 | 含义 |
| ---: | --- | --- |
| 0 | `EMPTY` | 无有效候选；该状态的文件长度必须为0 |
| 1 | `PREPARING` | 新下载已使旧候选失效，准备候选区 |
| 2 | `DOWNLOADING` | 正在写入候选文件 |
| 3 | `VERIFYING` | 正在校验完整文件 |
| 4 | `READY_TO_UPGRADE` | 候选有效，尚未持久受理升级请求 |
| 5 | `UPGRADE_REQUESTED` | 应用已持久受理升级请求 |
| 6 | `INSTALLING` | Boot已开始安装事务；复位后须从头重装 |
| 7 | `BOOT_PENDING` | 新应用已安装，等待首次启动确认 |
| 8 | `CONFIRMED` | 新应用已确认运行 |
| 9 | `FAILED` | 事务失败，原因保存在错误字段 |

除`EMPTY`外，有效记录的文件长度必须`>512`且`<=0x70000`，同时不超过候选槽容量；
未知状态值、错误magic/版本/长度、非零保留字节、错误CRC或缺少CMIT均使记录无效。
A/B选择规则固定为：

1. 两份都无效时，表示没有有效记录，逻辑状态为`EMPTY`；不得猜测存在升级请求。
2. 只有一份有效时选择该份。
3. 两份有效且代次不同，按`(int32_t)(generationA - generationB) > 0`判断A更新，否则选择B，兼容32位代次回绕；正常交替提交的两份记录只相差一代。
4. 代次相同时，256字节完全一致则选择A；内容不一致属于冲突，Boot停留故障态，不擅自覆盖。

### 11.3 Bootloader 基线

STM32F103ZET6内部Flash共512 KiB，分区已经前置冻结为：

- Bootloader：`0x08000000～0x0800FFFF`，64 KiB。
- Application镜像区：`0x08010000～0x0807FFFF`，448 KiB；其中`0x08010000～0x080101FF`
  是512字节描述头，仅前三个字段有意义，其余必须全0；应用向量表和可执行内容从
  `0x08010200`开始。

描述头是应用链接生成的完整文件的一部分，固定512字节，两边各自实现写入/解析，不共享头文件：

| 文件内偏移 | 字节数 | 内容 |
| --- | ---: | --- |
| `0x00` | 16 | 硬件型号，例如`FIRE_F103_V2`；ASCII字符串、以0结束、剩余字节全0 |
| `0x10` | 16 | 软件型号，例如`APP.V1.0.0`；ASCII字符串、以0结束、剩余字节全0 |
| `0x20` | 4 | 完整文件长度，小端`uint32_t`，包含本描述头 |
| `0x24` | 476 | 保留区，全部为0，不保存MD5或额外字段 |

两个型号字段均须非空、在各自16字节内遇到结束0，非零字符范围为`0x21～0x7E`；
文件长度须与元数据声明一致并满足`512 < length <= 0x70000`。Boot将硬件型号与自己的本地型号
完整比较；首版不做软件版本防降级。完整文件MD5包含这512字节，期望值保存在元数据，不在描述头。

当前Application已迁移链接地址，固定输出“512字节描述头 + 向量表和应用代码”的
`application.bin`。P4的Bootloader把完整文件写到
`0x08010000`，并从`0x08010200`校验向量表和启动应用。内部Flash不做双应用区，W25Q64也不保留
旧应用副本。Bootloader
不使用FreeRTOS，不包含W5500、TCP、MQTT或FTP，只使用必要的时钟、SPI1/W25Q64、内部Flash、
校验、看门狗和跳转逻辑。

Bootloader 至少执行：

1. 检查升级标志、候选固件槽和镜像头。
2. 重新校验目标型号、长度、地址范围、完整文件MD5和元数据CRC。
3. 记录升级阶段后再擦除应用区。
4. 分块从候选固件槽读取并写入内部Flash。
5. 读回校验已写入内容。
6. 内部镜像读回及完整 MD5 校验通过后，一次原子提交`BOOT_PENDING`和首次启动次数`1`，清理 Boot 使用的中断、时基和外设后直接跳转应用 Reset_Handler；不额外软件复位，也不在同一次启动内重复计算内部 MD5。
7. 应用在FreeRTOS调度器已运行、Storage初始化及元数据读取成功、ADC已发布第一份有效样本后，仅对有效`BOOT_PENDING`提交`CONFIRMED`；不要求MQTT联网，不新增自检Task。提交沿用记录中的完整文件长度、MD5及安装/启动次数，只更新代次、状态和错误值；完整A/B提交与最终读回成功后才解除下载锁定。确认失败不按下载失败提交`FAILED`，旧有效记录保留、运行期禁止新下载；结果不确定时复位重新选择实际有效的A/B记录。已`CONFIRMED`的普通复位不重复确认，`READY_TO_UPGRADE`候选也不能被当作当前运行应用确认。
8. `INSTALLING`期间复位时，重新校验并从头安装同一候选包。
9. 后续复位若仍为`BOOT_PENDING`，重新核对内部应用头、型号、长度、向量和完整文件 MD5；未达 3 次时先持久递增启动次数再跳转，达到上限仍未确认则记录失败并停留，等待 ST-Link 恢复。已提交`CONFIRMED`后的正常复位不再消耗启动次数。

`bootAttempts`是已持久预登记的启动尝试次数；提交后到实际跳转之间掉电也会消耗一次，
但提交失败绝不跳转。应用发起升级请求后进入 Boot 的复位仍然保留；仅去掉 Boot 安装成功后的额外复位。

本方案只宣称候选镜像完整性保护和安装中断重试，不宣称旧版本自动回滚或无条件防变砖。

### 11.4 升级进度

`Client2Server/filedownloadprocess` Body：

| 字段 | 类型 | 语义 |
| --- | --- | --- |
| `upgrade` | Int | 1=升级成功，2=升级失败，3=升级中 |
| `rate` | Int | 0-100 的升级进度 |
| `upgradeabnormal` | 可选 | 0=默认，1=升级异常 |

参考协议要求升级进度每增加5%上报一次、超过5分钟判定失败。但本项目的Bootloader阶段不运行MQTT，因此MQTTX不能获得实时擦写百分比：应用重启前上报升级受理ACK和“升级中”，`rate=0`只表示等待安装，不代表实际擦写进度。新应用完成持久确认并重新连接MQTT后补报成功，`rate=100`。失败只有在应用仍可运行并联网时才能补报；停留Boot故障态时无法通过MQTT上报。不得用本地计时伪造0–100%的实际擦写进度。

已批准方案在每个MQTT READY会话补报一次最新有效终态：只有有效`CONFIRMED`且保留了真实
安装/启动记录时上报`upgrade=1, rate=100`；有效`FAILED`且错误为本节定义的
`CANDIDATE_REJECTED_BEFORE_INSTALL`、旧应用仍运行时上报`upgrade=2, rate=0`。
新下载替换元数据后不沿用旧成功状态。QoS 0和本机SENDOK回执不保证MQTTX最终显示，
也不宣称跨重启恰好一次送达。源码及Debug构建已完成，正常升级实机验收仍待完成，见[实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)。

### 11.5 P4 元数据内部错误码

以下数值写入元数据`ulLastError`，不改变现有 MQTT Body，也不与 P3 的`APP_STORAGE_ERROR_*`编号混用。Bootloader 与 Application 在各自工程内独立定义/解释这些数值，不包含对方的头文件，不要求C枚举名称相同；必须保证持久字节的数值及含义一致。元数据提交本身失败时只在 RAM 留诊断码，不能假称已持久化。

| 数值 | 名称后缀 | 处理 |
| --- | --- | --- |
| `0x1001` | `CANDIDATE_REJECTED_BEFORE_INSTALL` | 首次安装前拒绝候选；旧应用有效时可继续启动 |
| `0x1002` | `CANDIDATE_LOST_DURING_INSTALL` | 已进入安装事务后候选失效；不得跳旧应用 |
| `0x1003` | `METADATA_COMMIT` | 元数据提交故障；RAM 诊断并停留故障态 |
| `0x1004` | `FLASH_ERASE` | 内部应用区擦除失败；停留故障态 |
| `0x1005` | `FLASH_PROGRAM` | 内部 Flash 半字编程失败；停留故障态 |
| `0x1006` | `FLASH_READBACK` | 内部 Flash 读回不一致；停留故障态 |
| `0x1007` | `INTERNAL_MD5` | 安装后的内部 Flash 全文件 MD5 不一致；停留故障态 |
| `0x1008` | `INSTALL_ATTEMPTS_EXHAUSTED` | 安装次数达到 3；停留故障态 |
| `0x1009` | `BOOT_ATTEMPTS_EXHAUSTED` | 新应用 3 次启动仍未确认；停留故障态 |

## 12. ACK、重复消息与失败语义

- 业务层不使用MQTT PUBACK替代业务ACK；MQTT传输配置不改变Word定义的ACK Topic和JSON。
- `control`、`filedownloadcommand`和`fileupgradecommand`必须有应用层ACK；ACK沿用请求的固定`messageId`。
- `heartbeat`、`temperature`、`filedownloadresult`和`filedownloadprocess`不要求业务ACK。
- 固定`messageId`只能标识报文类型，不能区分同一类型的两次独立事务。因此每种命令同一时刻只允许一笔在途；重复控制命令按目标状态幂等执行，下载或升级进行中收到同类命令时不得重复擦除、重复下载或重复启动升级，而应返回当前受理状态。
- 同一时刻只允许一个下载/升级事务；并发命令应明确拒绝。
- 单条业务命令及一次下载/升级事务的重试次数必须有限，离线期间不无限缓存控制和升级命令。TCP/MQTT连接为恢复长期可用性可以持续低频探测，但退避间隔必须有上限，且不能形成忙循环。
- STM32 复位后应从持久化事务状态恢复，而不是盲目重新执行最后一条命令。

P4 元数据内部错误码已经在 11.5 节定义；MQTT 对外是否增加错误原因字段及其编码仍待确认。P3 的参数错误、状态冲突、网络错误、FTP 认证失败、文件不存在、超时、外部 Flash 错误、空间不足和 MD5 错误继续使用既有状态/结果语义，不得把 P4 的`0x1000`段直接当作 MQTT Body 数值。

## 13. 安全与资源限制

- 实验阶段可在受控Windows局域网环境使用1883和FTP，但不得用于不受信任网络。
- 不在仓库中保存参考协议里的真实账号密码；示例全部使用占位符。
- 所有外部长度、地址、文件名和 JSON 字符串必须做上限检查。
- W5500 Socket、接收缓冲、JSON 缓冲、消息队列和 Flash 请求队列必须有固定上限。
- 网络中断、下载失败或 Broker 离线时，STM32 的本地安全控制继续运行。
- 升级期间必须定义允许的设备状态；不能在危险执行器未安全停机时升级。

## 14. 待确认清单

- [x] CAN波特率500 kbit/s；Prescaler=4、SJW=1 TQ、BS1=15 TQ、BS2=2 TQ、总计18 TQ、采样点88.89%、误差0%。
- [ ] 创芯科技CAN分析仪对29位扩展数据帧的发送、显示、过滤和正常模式CAN硬件ACK配置。
- [x] Clean Session固定为`true`，所有发布和订阅使用QoS 0，所有业务消息Retain固定为`false`；这些取值不扩展业务Topic或JSON。
- [x] MQTT Broker固定为`192.168.101.11:1883`，Client ID为`stm32f103-0253544d3201`，Keep Alive为30秒，当前不使用LWT。
- [x] 业务心跳周期10秒，只由STM32上报；不使用心跳ACK，不实现180秒业务离线判定。
- [x] `messageId`使用Word规定的固定字符串编号，ACK与请求一致；`timestamp`为发送端进程运行秒数，`deviceId`为设备IPv4地址，业务协议版本为`0.0.2`。
- [x] Topic固定为`Client2Server/{method}`和`Server2Client/{method}`且斜杠前无空格；ACK映射按本文执行。
- [x] 不开发FTP Server；用户日志记录FileZilla Server 1.12.6，`/firmware`、`EPSV`及TCP `50000-50009`已用于正常下载；异常恢复测试跳过。
- [x] 取消WPF开发，保留MQTTX调试；按用户新决定输出`项目简历.md`并开发结项，不把未完成的升级实机项记为通过。
- [x] FTP原样下载应用工程生成的`application.bin`；完整文件最大448 KiB，前512字节为不含MD5的描述头，不依赖独立PC打包工具。
- [x] 首版不做软件版本防降级；软件型号保留用于识别，硬件型号必须完全一致。
- [x] W25Q64分区、单候选固件槽、A/B元数据和提交标记最后写入的原子更新方式已经冻结。
- [x] 内部Flash分区、安装中断从头重试、有限尝试/安全停留及首次确认代码已实现并静态核对；异常路径未实机验收。
- [x] 错误码、日志字段、发送完成语义和升级结果跨重启上报已定义并接入；全链路升级结果未实机验收。

## 15. 参考资料

- `resources/开发板资料/原理图/野火STM32F103-V2霸道开发板原理图_2024.pdf`
- `resources/开发板资料/[野火]STM32F103霸道_V1&V2_开发板规格书.pdf`
- `resources/mqtt/CP4Z热管理系统MQTT通讯协议-V1.7-20260624.docx`

参考资料用于理解硬件和客户协议；本文件经过评审后的内容才是本项目实现规范。
