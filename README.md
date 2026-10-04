# STM32 FreeRTOS CAN MQTT OTA 实践项目

一个面向学习、作品展示与工程实践的嵌入式通信项目。系统以野火 STM32F103 霸道 V2 为核心：通过CAN与Windows调试端通信，通过板载W5500直接连接Windows侧的EMQX MQTT Broker和FTP Server，使用MQTTX下发业务命令并观察设备ACK、遥测和升级结果。用户已决定在代码更新和检查后结束开发，归档应用、Boot及项目文档，并输出可粘贴到简历的项目经历；不开发WPF Dashboard。完整升级实机验收状态单独记录，不以开发结项代替实测。

项目强调完整、可解释、可测试的端到端工程链路，而不是单一外设演示。

## 目录

- [当前状态](#当前状态)
- [已确认硬件](#已确认硬件)
- [总体架构](#总体架构)
- [目标协议子集](#目标协议子集)
- [目录职责](#目录职责)
- [实施路线](#实施路线)

## 当前状态

2026-10-04按用户决定完成代码开发收尾，不再增加功能。Bootloader、Application及二者交接已做静态检查；修正Application启动创建函数遗留的两个无条件失败返回块，以及FTP完成后内部进度从100%回落到95%的问题，Application Debug重新构建通过。当前代码审查未再发现阻塞项，不代表穷尽全部缺陷或实机升级已经通过。

- P1 CAN与P2 W5500/TCP/MQTT业务已有实机验收：心跳、CPU温度/电压、输出控制与ACK、重连及CAN错误恢复。
- P3 FTP正常下载已有用户成功及100%结果；采用单候选固件槽、A/B元数据和完整文件MD5，用户已决定跳过故障矩阵。
- P4独立裸机Boot已完成源码及Debug/Release构建，历史双镜像烧录读回成功；Application升级请求、发送完成回执、取消、本地首次确认及跨重启上报已实现。
- 普通Boot跳转、完整新版本安装、首次`CONFIRMED`及再次复位尚无最终运行反馈，保留为未验收，不再作为当前开发待办。
- 当前Application链接占用FLASH 87476字节/448 KiB、RAM 47112字节/64 KiB；现有newlib/nosys占位警告保留，新增功能的运行期栈/Heap余量未实测。

应用只采用一套构建配置，`firmware/build/`输出`application.elf/.bin/.hex`，描述头512字节、向量表地址`0x08010200`。Boot工程独立位于`bootloader/`，不引用、编译或链接Application文件；两边只通过协议字节格式交接。

归档按用户选择采用新的项目根Git仓库及新远端；用户已将旧应用历史保留为`firmware/.git-legacy-backup`，初始化根仓库`main`并关联新远端，首次暂存、提交和推送仍待完成。手动归档和标签步骤见结项记录。Codex未执行Git写操作。

维护入口：

- [结项记录与完整归档步骤](docs/PROJECT_CLOSEOUT.md)
- [项目简历（可直接复制正文）](docs/项目简历.md)
- [Application维护说明](firmware/README.md)
- [Bootloader维护说明](bootloader/README.md)
- [项目上下文](docs/PROJECT_CONTEXT.md)
- [阶段记录与验收边界](docs/NEXT_STEPS.md)
- [MQTT升级实现对照及保留的实机步骤](docs/MQTT_UPGRADE_IMPLEMENTATION_STEP.md)
- [W5500任务架构](docs/W5500_MULTITASK_ARCHITECTURE.md)
- [协议与镜像格式](docs/protocol.md)
- [Windows服务说明](infrastructure/README.md)
- [FTP学习资料（本机保留，不进入公开档案）](resources/FTP/FTP_DOWNLOAD_GUIDE.md)
- [已取消的WPF历史说明](dashboard/README.md)
- [协作规则](AGENTS.md)

## 已确认硬件

| 范围 | 已确认选型 | 作用 |
| --- | --- | --- |
| 开发板 | 野火 STM32F103 霸道 V2 | 主控制器与实验载体 |
| MCU | STM32F103ZET6，Cortex-M3，72 MHz，512 KB Flash，64 KB SRAM | FreeRTOS、CAN、网络业务和升级状态机 |
| CAN 收发器 | 板载 SIT1042AQT/3 | 连接 CANH/CANL |
| CAN 调试端 | 创芯科技 CAN 分析仪至尊版，仅支持 Windows | CAN 报文收发、仿真和验收 |
| 以太网 | 板载 W5500，SPI2 接口 | TCP Socket、MQTT 和 FTP 数据通道 |
| 固件暂存 | 板载 W25Q64，8 MB | 保存下载完成、校验通过的待升级固件 |

CAN 使用 PB8/PB9（CAN1 重映射）和500 kbit/s；W5500 使用 PB13/PB14/PB15，并使用 PG9/PG8/PG15 作为 CS/INT/RST。CAN 供电需按原理图配置 J40 的 1-2 跳帽，PB8/PB9 与摄像头接口复用，因此本项目不同时使用摄像头。

## 总体架构

```text
STM32F103ZET6 + FreeRTOS
  ├─ CAN1 ⇄ USB-CAN ⇄ Windows厂商CAN调试工具
  │
  ├─ W5500 / MQTT ⇄ EMQX Broker（Windows侧独立服务）
  │                              ⇅ MQTT
  │                    MQTTX（Windows业务调试端）
  │
  └─ W5500 / FTP ⇄ FTP Server（Windows独立服务）
         │
         └─ W25Q64暂存 → 校验 → Bootloader升级
```

CAN调试、MQTT消息路由、FTP文件服务和MQTT业务调试是四个逻辑上独立的职责。MQTTX发布`Server2Client/*`业务命令并订阅`Client2Server/*`设备上行，技术上是MQTT Client。EMQX只负责连接、订阅和路由。EMQX和FTP Server即使与MQTTX运行在同一台Windows PC，也保持为独立进程；关闭MQTTX不得停止Broker或FTP Server。CAN与MQTT不设置转换网关，STM32自己生成和解析两套协议的数据。

架构的核心安全边界不变：急停、联锁、限温、超压保护、实时 PID 等安全或毫秒级控制必须在 STM32 本地完成。网络、Broker、FTP 或上位机中断不得影响设备安全运行。

## 目标协议子集

本项目借鉴 `resources/mqtt/CP4Z热管理系统MQTT通讯协议-V1.7-20260624.docx`，第一批只实现：

- MQTT Keep Alive 和断线重连。
- 设备周期上报业务心跳 `heartbeat`，不要求业务ACK。
- 周期上报CPU温度和电位器电压，并接收红色LED1/蜂鸣器控制命令及其ACK。
- 文件下载命令 `filedownloadcommand` 及其 ACK。
- FTP 固件下载、下载进度和 MD5 校验。
- 升级命令 `fileupgradecommand` 及其 ACK。
- 升级进度 `filedownloadprocess`、结果和异常上报。

需要明确区分：MQTT只下发FTP地址、凭据、路径、文件名和MD5；固件内容由STM32 FTP Client通过独立FTP/TCP连接从FileZilla Server标准版下载，不放入MQTT Payload。本项目不开发FTP Server程序；P3固定使用`EPSV`和TCP `50000-50009`被动端口范围。

本项目固定术语如下：“下载”是STM32作为FTP Client从Windows FTP Server读取固件，并将固件流式写入W25Q64暂存区；该阶段不得擦写MCU内部Flash。“升级”是Bootloader读取W25Q64中已经校验通过的镜像，擦除、写入并校验STM32内部Flash应用区；该阶段不再进行FTP传输。

## 目录职责

```text
STM32_FreeRTOS_CAN_Linux_Gateway/
├── firmware/   独立STM32应用工程、FreeRTOS、CAN、W5500、MQTT、FTP及自己的驱动/库
├── bootloader/ 独立裸机Boot工程、内部Flash安装与应用启动
├── infrastructure/ Windows侧EMQX、FTP Server的部署说明与脱敏示例配置
├── dashboard/  已取消的WPF历史方案，不继续实现
├── docs/       架构、协议、阶段计划与测试记录
├── tools/      本机联调工具与服务安装包，不进入公开归档
└── resources/  本机厂商、客户参考及学习资料，不进入公开归档
```

当前仓库根目录名中的`Linux_Gateway`以及历史空`gateway/`目录来自旧方案，不再表示正式运行架构。本轮不重命名仓库根目录，避免影响本机工作区和历史路径；本次完整归档也保留现有目录名称。

## 实施路线

- P0：硬件、网络、协议与升级方案设计。
- P1：STM32 与 Windows CAN 最小闭环。
- P2：W5500、TCP、Windows侧EMQX、MQTT以及心跳、温度和输出控制业务。
- P3：W25Q64存储基础、Windows FTP下载、镜像暂存和MD5校验。
- P4：Bootloader、内部Flash升级、应用首次启动确认及正常升级实机验证；保留失败保护代码，故障测试按用户要求跳过。
- 收尾：按用户2026-10-04决定，代码检查和Application构建通过后开发结项，定稿`docs/项目简历.md`并完整归档；正常升级和再次复位缺少的实机证据如实保留，不再进入WPF开发。

当前任务和验收标准以 [docs/NEXT_STEPS.md](docs/NEXT_STEPS.md) 为准；协议实现以 [docs/protocol.md](docs/protocol.md) 为准。
