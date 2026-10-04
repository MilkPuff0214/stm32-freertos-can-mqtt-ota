# W5500 多协议任务架构

本文冻结STM32侧W5500并发架构：MQTT与FTP分别由独立FreeRTOS Task拥有，不能继续把协议状态机堆入同一个Network Task；W5500作为共享硬件，由一个带优先级继承的互斥量串行访问。当前固件已经接入独立MQTT/FTP Task、业务Task、Storage Task和统一W5500访问门面，P3正常下载已验收；运行期全芯片复位协调尚未实现。MQTT升级发送回执和共用Storage快照已于2026-10-04按明确授权接入Application源码并通过Debug构建，最终审查已修正Startup与FTP终态进度，Application Debug重新构建通过；用户决定开发结项，未完成的升级实机项保留记录，入口为[结项与归档](PROJECT_CLOSEOUT.md)。

## 目录

- [1. 架构结论](#1-架构结论)
- [2. Task与模块职责](#2-task与模块职责)
- [3. 固定Socket所有权](#3-固定socket所有权)
- [4. W5500互斥访问规则](#4-w5500互斥访问规则)
- [5. 全芯片状态与复位协调](#5-全芯片状态与复位协调)
- [6. Task间数据流](#6-task间数据流)
- [7. 目标目录和依赖方向](#7-目标目录和依赖方向)
- [8. 调度与资源规则](#8-调度与资源规则)
- [9. 当前基线与后续扩展](#9-当前基线与后续扩展)
- [10. 验收条件](#10-验收条件)
- [11. 官方依据](#11-官方依据)

## 1. 架构结论

最终执行模型采用“协议Task分离、Socket独占、W5500总线互斥、芯片生命周期集中管理”：

```text
                         ┌────────────────────────┐
                         │ AppNetworkTask         │
                         │ 初始化/PHY/复位/代次   │
                         └───────────┬────────────┘
                                     │ 网络状态快照
                   ┌─────────────────┴─────────────────┐
                   ▼                                   ▼
          ┌─────────────────┐                 ┌─────────────────┐
          │ AppMqttTask     │                 │ AppFtpTask      │
          │ Socket 0        │                 │ Socket 1、2     │
          │ TCP + coreMQTT  │                 │ FTP状态机       │
          └────────┬────────┘                 └────────┬────────┘
                   │                                   │
                   └──────────┬────────────────────────┘
                              ▼
                 ┌─────────────────────────┐
                 │ RtosW5500Access Mutex   │
                 │ 整个BSP操作期间持有     │
                 └────────────┬────────────┘
                              ▼
                 bsp_w5500 → WIZnet驱动 → SPI2/W5500
```

Task是并发和调度边界，模块是代码分层边界。采用多个Task后仍需把每个协议拆成“Task入口、协议客户端、Transport/TCP、BSP”，不能把整个MQTT实现直接写进`app_mqtt_task.c`，也不能让FTP Task承担W25Q64写入。

## 2. Task与模块职责

| Task | 唯一拥有的状态 | 允许做什么 | 禁止做什么 |
| --- | --- | --- | --- |
| `AppNetworkTask` | W5500初始化状态、PHY状态、芯片代次、复位流程 | 初始化芯片、配置网络参数、监测PHY、发布状态快照、协调全芯片复位 | 不运行MQTT/FTP协议，不拥有业务Socket，不收发业务Payload |
| `AppMqttTask` | Socket 0、`AppTcpClient_t`、`MQTTContext_t`、MQTT订阅/保活/重连状态 | 连接EMQX、运行coreMQTT、处理MQTT回调、向业务Task投递有界消息 | 不调用FTP状态机，不写W25Q64，不复位W5500 |
| `AppMqttBusinessTask` | JSON业务校验、上报时序、升级业务执行状态 | 通过Queue提交FTP/Storage请求及MQTT出站消息，核对发送回执后决策应用复位 | 不直接调用coreMQTT、不拥有Socket、不直接写W25Q64 |
| `AppFtpTask` | Socket 1控制连接、Socket 2数据连接、FTP事务状态 | 登录FTP、执行下载命令、分块接收文件、上报进度 | 不调用coreMQTT，不直接写W25Q64，不复位W5500 |
| `AppStorageTask`（P3新增） | W25Q64、下载槽、镜像元数据和写入状态 | 擦除、分块写入、读回、MD5输入和持久化状态 | 不访问W5500，不解析MQTT或FTP控制报文 |

`AppNetworkTask`是网络监督Task，不是协议容器。新增网络协议时应新增独立协议模块；只有确有独立阻塞周期、优先级或资源所有权时才新增Task。

## 3. 固定Socket所有权

当前项目不需要动态Socket分配器，使用编译期固定表更直观：

| W5500 Socket | 所有者 | 生命周期 |
| --- | --- | --- |
| 0 | `AppMqttTask` | 常驻MQTT TCP连接，失败后持续退避重连 |
| 1 | `AppFtpTask` | 单次FTP控制会话 |
| 2 | `AppFtpTask` | 单次FTP数据连接 |
| 3～7 | 保留 | 未分配前任何Task不得使用 |

固定值后续放入`User/app/network/app_network_config.h`，并用编译期断言保证三个Socket号互不相同且均小于8。一个Socket只能有一个Task和一个连接状态机所有者；互斥量不能用来允许两个Task轮流操作同一Socket。

## 4. W5500互斥访问规则

### 4.1 互斥量放在哪里

FreeRTOS适配放在：

```text
firmware/User/rtos/w5500/rtos_w5500_access.c
firmware/User/rtos/w5500/rtos_w5500_access.h
```

它只拥有一个W5500访问Mutex，不包含MQTT、FTP、Socket状态或业务逻辑。`bsp_w5500`继续只依赖FWlib、CMSIS、硬件和WIZnet驱动，不反向包含FreeRTOS头文件。

当前`FreeRTOSConfig.h`的`configUSE_MUTEXES`已经为1；工程编译既有`queue.c`，不引入另一份内核实现。

### 4.2 锁的边界

锁必须覆盖一个完整的`BspW5500_*()`公开操作，包括该操作内部的全部寄存器读写以及`s_xSpiTransferFailed`的清零、置位和最终检查：

```text
Take W5500 Mutex
    调用一次完整BspW5500_*操作
Give W5500 Mutex
```

锁失败表示共享设备当前忙，上层状态机下一周期重试；不能把普通锁竞争误判成TCP断链。后续实现应在`AppTcpClient`和FTP连接适配层集中完成这种映射，不能让每个业务调用点自行判断。

WIZnet官方`reg_wizchip_cris_cbfunc()`只围绕单次底层I/O事务，不能单独保护由多次寄存器访问组成的BSP公开操作，也保护不了当前BSP函数级的全局SPI错误锁存。因此本项目使用外层“完整BSP操作”Mutex；不得再把同一个非递归Mutex注册到内部CRIS回调，避免嵌套获取导致死锁。

### 4.3 持锁期间禁止的操作

持有W5500 Mutex时禁止：

- `vTaskDelay()`或任何主动等待调度周期的操作；
- 阻塞式Queue发送/接收、等待Task Notification；
- Console格式化和日志发送；
- coreMQTT事件回调和JSON业务处理；
- W25Q64擦除、写入和MD5计算；
- 获取第二把Mutex；
- 从ISR调用Mutex或W5500驱动。

这样互斥区只包含有界SPI和寄存器操作。FreeRTOS Mutex的优先级继承用于缩短优先级反转，但不能代替短临界区和有限工作量。

## 5. 全芯片状态与复位协调

Socket相互独立不代表芯片复位相互独立。硬件复位、软件复位、网络参数和TX/RX缓冲区重分配会同时破坏所有Socket，只能由`AppNetworkTask`执行。

以下`ulGeneration`属于**运行期全芯片复位的目标契约**。当前源码尚未实现完整复位协调，只有启动初始化和PHY轮询，因此当前状态快照只保留`xChipReady`与`xPhyLinkUp`，不保留一个上电后不再变化的伪代次。实现完下面六步完整流程时，才应把代次字段、递增生产者和各协议Task的失效处理同时加入。

每个网络状态快照至少包含：

```c
typedef struct
{
    uint32_t ulGeneration;
    bool xChipReady;
    bool xPhyLinkUp;
} AppNetworkStatus_t;
```

`AppNetworkTask`通过分别属于MQTT和FTP的长度1 Queue发送最新快照，使用覆盖语义，消费者永远读取最新状态而不是积压历史链路事件。

全芯片复位顺序固定为：

1. Supervisor发布`xChipReady=false`的新快照。
2. MQTT Task和FTP Task停止发起新操作，使各自状态机进入QUIESCED并回复确认。
3. Supervisor确认所有已启用协议Task静默后获取W5500 Mutex。
4. 执行复位、缓冲区配置和网络参数写入/读回。
5. `ulGeneration++`，发布新的芯片/PHY状态快照。
6. 各协议Task发现代次变化，丢弃旧Socket会话状态并从关闭状态重新开始。

任何协议Task都只能“请求复位”，不能直接拉RST或改公共网络配置。

## 6. Task间数据流

### 6.1 MQTT命令触发FTP

```text
coreMQTT回调
  → 有界校验并复制命令
  → MqttInboundQueue
  → AppMqttBusinessTask校验JSON和业务状态
  → FtpCommandQueue
  → AppFtpTask执行下载
```

MQTT回调不得调用FTP函数、等待FTP完成或写Flash。

### 6.2 FTP写入W25Q64

```text
AppFtpTask收到固定大小数据块
  → 释放W5500 Mutex
  → StorageRequestQueue复制事务ID、偏移、长度和最多512字节数据
  → AppFtpTask本地接收缓冲区立即可以复用
  → AppStorageTask写W25Q64
  → StorageStatusQueue覆盖发布最新状态和已写入长度
```

FTP Task持有W5500 Mutex时不能投递可能阻塞的Flash Queue。数据离开W5500 RX缓冲区并释放W5500 Mutex后，才允许把内联数据复制到Storage Request Queue。请求消息不携带外部缓冲区指针，因此不需要额外的缓冲池、所有权归还Queue或跨Task生命周期协议；Queue满时由FTP Task暂停读取数据连接，形成明确的背压。

### 6.3 FTP状态反馈MQTT

```text
AppFtpTask
  → FtpStatusQueue
  → AppMqttBusinessTask编码进度或ACK
  → MqttOutboundQueue
  → AppMqttTask按协议发布
```

Queue必须有固定容量和满队列策略。命令、进度、数据块和最终结果使用不同契约，不传递指向临时MQTT接收缓冲区或任务栈的指针。

### 6.4 共用Storage最新快照与升级发送回执

以下增量已于2026-10-04接入源码并通过Debug构建，具体接口及代码见[实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)：

- Storage仍唯一拥有W25Q64及元数据写入，现有长度1`StorageStatusQueue`改为非消费的最新快照。
  FTP和业务Task各自单次零等待`xQueuePeek`，不循环排空，不新增第二状态Queue或转发层。
  持久状态与代次来自已选中有效A/B记录，升级请求通过代次与本机关联号校验结果。
- Startup新建并拥有长度1`PublishReceiptQueue`，MQTT Task生产、业务Task消费；同一时刻
  最多一笔需回执请求。出站编号0表示普通消息，不写回执；非零编号和原MQTT会话共同关联结果。
- MQTT Task复用现有TCP会话编号与`xTxInFlight`，Publish成功后继续Service，原会话末段SENDOK
  完成才回`SENT`；错误、超时或会话变化回`FAILED`。等待期间不取下一outbound，不持Mutex等待。
  每轮先TCP Service、再检查出站排空、最后MQTT Service，避免Keep Alive新SEND遮盖排空边界。
- 业务Task在Storage完整提交`UPGRADE_REQUESTED`之后串行发送受理ACK和`upgrade=3, rate=0`，
  核对两条匹配`SENT`回执后才检查输出并放行应用复位。MQTT Task不复位MCU或W5500。
  失败不自动复位，保留持久请求和下载锁定；取消在放行复位前由Storage完整提交回READY。
  手工复位仍会让Boot执行已持久升级请求，新应用CONFIRMED后的联网结果由业务Task补报。

回执表达网络发送完成，共用快照表达最新存储事实，两者职责不同；不以回执重建第二份持久状态。
该增量没有新增协议Task、Socket分配或第二套发送状态机，正常升级实机验收仍待完成。

## 7. 目标目录和依赖方向

```text
firmware/User/
├── app/
│   ├── network/
│   │   ├── app_network_task.c/.h       芯片监督、PHY、复位协调
│   │   ├── app_network_config.h        固定Socket分配和网络参数
│   │   └── app_tcp_client.c/.h         可实例化TCP连接状态机
│   ├── mqtt/
│   │   ├── app_mqtt_task.c/.h          MQTT Task入口和Queue编排
│   │   ├── app_mqtt_client.c/.h        coreMQTT会话状态机
│   │   ├── app_mqtt_transport_w5500.c/.h
│   │   └── core_mqtt_config.h
│   ├── ftp/
│   │   ├── app_ftp_task.c/.h           FTP Task入口和事务Queue
│   │   └── app_ftp_client.c/.h         FTP控制/数据状态机
│   └── storage/
│       └── app_storage_task.c/.h       P3新增，唯一W25Q64所有者
├── rtos/
│   └── w5500/
│       └── rtos_w5500_access.c/.h      唯一W5500 Mutex
└── bsp/
    └── w5500/
        └── bsp_w5500.c/.h              硬件和有界Socket原语
```

依赖方向：

```text
AppMqttTask → AppMqttClient → coreMQTT Transport → AppTcpClient
AppFtpTask  → AppFtpClient  → FTP连接/Socket原语
AppNetworkTask、AppTcpClient、AppFtpClient
              → RtosW5500Access → BspW5500 → WIZnet驱动 → SPI2
AppFtpTask → Queue → AppStorageTask → W25Q64 BSP
AppMqttBusinessTask → Queue → AppMqttTask/AppFtpTask/AppStorageTask
```

协议Task之间不互相包含头文件并直接调用内部函数；跨协议协作只通过放在明确契约头文件中的有界Queue消息。

## 8. 调度与资源规则

- MQTT Task优先级高于FTP Task，保证MQTT Keep Alive、业务心跳和命令接收不会被大文件下载饿死。
- Supervisor只执行短周期PHY检查和少量芯片管理，不承担持续数据搬运。
- FTP Task每次只推进一个有界状态或数据块，释放W5500 Mutex后再处理Queue。
- 每个Task拥有独立栈并通过高水位测量确定大小，不直接照搬示例数值。
- 同一Task不得同时持有W5500 Mutex和W25Q64/日志等其他Mutex，避免嵌套Mutex和锁顺序问题。
- coreMQTT的同一个`MQTTContext_t`只允许MQTT Task调用；其他Task只能向MQTT命令Queue提交发布请求。
- 每个Socket只能有一个重连状态机。MQTT TCP退避属于MQTT Task拥有的`AppTcpClient_t`；FTP重试属于FTP事务状态机，不能互相调用。

## 9. 当前基线与后续扩展

当前已经完成W5500 Mutex访问门面、独立MQTT Task、Socket 0所有权迁移、coreMQTT连接、
Keep Alive、订阅和有界业务Queue，以及温度/电压、LED/蜂鸣器控制和P3正常FTP下载。
FTP独占Socket 1/2，Storage独占W25Q64。阶段Echo已经退出构建，`AppNetworkTask`只保留芯片监督。
Boot源码和构建已经完成；应用首次确认及MQTT升级受理/发送回执/结果上报均已接入，2026-10-04的Application Debug构建通过，正常升级仍未实机验收。

项目已按用户2026-10-04决定转为代码开发结项：共享快照、升级受理、发送回执和跨重启上报源码已接入，最终修正后Debug构建通过。当前只整理简历与完整Git归档，不再安排新业务或实机步骤；[保留的正常升级步骤](MQTT_UPGRADE_IMPLEMENTATION_STEP.md#7-正常升级实机验收)不代表已经执行。完整升级、首次确认、再次复位及人为故障路径未实测的项目继续标注未验收，不开发WPF。

运行期全芯片复位仍是第5节规定的未来契约，不加入本次项目剩余路线。若未来启用，必须先完整接入协议Task静默确认和芯片代次失效，不能由某个协议Task绕过该边界。

## 10. 验收条件

多Task网络基础至少满足：

1. 任意时刻不同Task不会同时拉低W5500 CS或交错SPI字节。
2. `s_xSpiTransferFailed`在一次完整BSP操作内不会被另一Task清除或覆盖。
3. MQTT和FTP使用不同Socket，代码中不存在Socket所有权漂移。
4. FTP持续下载时，MQTT Keep Alive和10秒业务心跳仍按期运行。
5. PHY断开使各自连接失效，恢复时重新连接；未来启用全芯片复位后，各协议Task按第5节同一`ulGeneration`废弃旧会话。当前不将未实现的运行期芯片复位协调写为已验收。
6. 任一Task都不在持有W5500 Mutex时等待Queue、打印日志或写Flash。
7. Task栈、FreeRTOS Heap、Mutex等待时间和每轮数据量均有可观察值和实机余量。
8. CAN实时控制和安全逻辑不依赖任何网络Task或Mutex。

## 11. 官方依据

- [FreeRTOS Mutex说明](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/04-Mutexes)：Mutex用于共享资源互斥并提供基础优先级继承；ISR不能获取Mutex，优先级继承不能代替短临界区设计。
- [WIZnet ioLibrary临界区接口](https://github.com/Wiznet/ioLibrary_Driver/blob/master/Ethernet/wizchip_conf.h)：`reg_wizchip_cris_cbfunc()`用于保护单次WIZCHIP I/O；本项目在其外层继续保护完整BSP操作。
- [WIZnet W5500驱动](https://github.com/Wiznet/ioLibrary_Driver/blob/master/Ethernet/W5500/w5500.c)：底层寄存器读写使用`WIZCHIP_CRITICAL_ENTER/EXIT`边界。
