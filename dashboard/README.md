# Windows WPF Dashboard（已取消，历史方案）

用户已取消本项目的WPF开发，本目录仅保留历史设计，不作为后续实施任务。Windows业务调试和最终升级验收使用MQTTX；技术工作止于正常升级验证，最后定稿[项目简历](../docs/项目简历.md)。以下技术边界、分层目标和业务职责均为已取消的历史方案，不代表已实现功能。

## 目录

- [当前状态](#当前状态)
- [技术边界](#技术边界)
- [分层目标](#分层目标)
- [业务职责](#业务职责)
- [明确不做](#明确不做)

## 当前状态

P1、P2和P3正常下载路径已验收，P4正常升级闭环仍待完成。原P5 Dashboard阶段取消，不创建WPF解决方案或引入NuGet依赖，保留本文和现有目录，不继续实现历史方案。

## 技术边界

- 语言与UI：C#、WPF。
- 架构模式：MVVM。
- MQTT Client：MQTTnet；必须显式使用MQTT 3.1.1以匹配STM32。
- Broker：Windows侧独立EMQX，不嵌入Dashboard进程。
- FTP：Windows独立FTP Server，不嵌入Dashboard进程。

## 分层目标

```text
View（XAML）
    ⇅ Data Binding / Command
ViewModel
    ↓
Application Service
    ↓
MQTT Service / Protocol Codec / Device State
```

View只负责界面和绑定；ViewModel维护可观察UI状态和命令；应用服务编排连接与业务流程；MQTT和协议基础设施负责连接、Topic、JSON与ACK关联。MQTT回调不得直接堆叠界面和长业务逻辑。

## 业务职责

- 订阅`Client2Server/heartbeat`并刷新设备业务在线状态；心跳不回复业务ACK。
- 订阅`Client2Server/temperature`，显示当前温度、电压以及固定时间窗、有界点数的变化曲线。
- 展示MQTT连接、业务心跳、设备在线状态、命令ACK、下载/升级进度和日志。
- 通过`Server2Client/*`下发控制、下载和升级命令，并使用协议规定的`messageId`关联ACK。
- 选择待升级文件、计算MD5并编排“发布下载命令→等待下载完成→发布升级命令”；文件字节仍由独立FTP Server提供。
- 提供“下载到设备”和“开始升级”两个独立按钮；只有W25Q64镜像校验成功后才启用升级按钮。
- 根据设备上报的准备、下载、校验、擦写、重启和结果状态驱动进度条与升级动画，不能仅根据按钮点击启动成功动画。
- 区分“MQTT消息发布成功”“Broker收到消息”和“STM32执行成功”。
- 对断线、重连、超时和重复命令提供可观察状态。

## 明确不做

- 不承担急停、联锁、限温、超压、PID或其他硬实时安全控制。
- 不把EMQX或FTP Server嵌入WPF进程。
- 不通过CAN-MQTT转换层转发原始CAN帧；CAN调试继续由Windows厂商工具独立完成。
- 在协议未评审前，不自行新增Topic、JSON字段、错误码或OTA语义。
