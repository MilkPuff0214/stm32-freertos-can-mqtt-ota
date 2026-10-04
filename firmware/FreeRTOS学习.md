# FreeRTOS 学习与移植笔记

本文记录 STM32F103ZET6 + GNU ARM GCC + CMake + GNU Make 环境下，从裸机工程开始学习并移植 FreeRTOS Kernel V11.3.0 的完整过程。

这不是只追求“编译通过”的操作清单，而是一份持续维护的学习笔记。每一步都要回答：这个功能解决什么问题、内核如何实现、对应哪些源码、STM32 Cortex-M3 如何参与、怎样通过构建和调试证明理解正确。

## 目录

- [1. 学习目标与边界](#1-学习目标与边界)
- [2. 当前工程和版本基线](#2-当前工程和版本基线)
- [3. FreeRTOS 是什么](#3-freertos-是什么)
- [4. 为什么从裸机循环转向任务](#4-为什么从裸机循环转向任务)
- [5. 一个 FreeRTOS Task 由什么组成](#5-一个-freertos-task-由什么组成)
- [6. 任务的四种主要状态](#6-任务的四种主要状态)
- [7. 调度器如何选择任务](#7-调度器如何选择任务)
- [8. `vTaskDelay()` 为什么不浪费 CPU](#8-vtaskdelay-为什么不浪费-cpu)
- [9. Tick：FreeRTOS 的时间基础](#9-tickfreertos-的时间基础)
- [10. Cortex-M3 的 SVC、PendSV 和 SysTick](#10-cortex-m3-的-svcpendsv-和-systick)
- [11. FreeRTOS 源码分层](#11-freertos-源码分层)
- [12. `FreeRTOS.h` 与 `FreeRTOSConfig.h`](#12-freertosh-与-freertosconfigh)
- [13. `heap_4.c` 与 STM32 SRAM](#13-heap_4c-与-stm32-sram)
- [14. 第一阶段源码关系总图](#14-第一阶段源码关系总图)
- [15. 学习和移植进度](#15-学习和移植进度)
- [16. 当前应掌握的核心结论](#16-当前应掌握的核心结论)
- [17. 自测题](#17-自测题)
- [18. 建议学习顺序](#18-建议学习顺序)
- [19. 第4课：任务优先级、中断优先级与 `BASEPRI`](#19-第4课任务优先级中断优先级与-basepri)
- [20. 第5课前置：启动文件与链接脚本](#20-第5课前置启动文件与链接脚本)
- [21. 第5课：设计 `FreeRTOSConfig.h`](#21-第5课设计-freertosconfigh)
- [22. 第6课：编写第一个 LED Task](#22-第6课编写第一个-led-task)
- [23. 项目自编 C 代码风格](#23-项目自编-c-代码风格)
- [24. 第7课：创建Task并启动调度器](#24-第7课创建task并启动调度器)
- [25. 第二阶段：按键、Queue、Semaphore 与 Mutex](#25-第二阶段按键queuesemaphore-与-mutex)
- [26. USART1 TX DMA 与 Task Notification](#26-usart1-tx-dma-与-task-notification)
- [27. USART1 RX循环DMA、IDLE与Task Notification](#27-usart1-rx循环dmaidle与task-notification)
- [28. 最小Console行协议与Serial RX Task接入](#28-最小console行协议与serial-rx-task接入)
- [29. FreeRTOS任务、运行时间与Heap诊断](#29-freertos任务运行时间与heap诊断)
- [30. ADC1双通道低频采样与串口发送复用](#30-adc1双通道低频采样与串口发送复用)

## 1. 学习目标与边界

### 1.1 总体目标

完成学习后，应能够独立解释并验证：

- FreeRTOS 是什么，和裸机程序、完整操作系统有什么区别。
- Task、TCB、任务栈、任务状态和任务优先级分别是什么。
- 调度器如何从 Ready 任务中选择下一个运行任务。
- 抢占、时间片和阻塞的区别。
- `vTaskDelay()` 为什么不会让整个 CPU 停止。
- SysTick、PendSV 和 SVC 在 Cortex-M3 调度中的职责。
- FreeRTOS 通用内核、处理器端口和应用配置之间的边界。
- `FreeRTOS.h` 与 `FreeRTOSConfig.h` 为什么缺一不可。
- `heap_4.c`、RTOS Heap、TCB、任务栈和 STM32 SRAM 的关系。
- 中断优先级与任务优先级为什么方向相反。
- 中断服务函数何时可以调用 FreeRTOS `FromISR` API。
- 如何通过 ELF、MAP、GDB 断点和实机现象验证移植结果。

### 1.2 第一阶段最小 Demo 边界（历史基线）

第一阶段只实现：

```text
FreeRTOS Kernel V11.3.0
GNU GCC Cortex-M3 端口
heap_4.c
一个 LED Task
vTaskDelay()
调度器正常启动
```

暂不加入：

- CAN。
- W5500、TCP 或 MQTT。
- FTP 或 W25Q64。
- Bootloader 或 OTA。
- 队列、互斥量、事件组和软件定时器的业务使用。

这些功能将在最小调度闭环稳定后逐步学习，不能一次性全部加入。

### 1.3 学习与修改分工

- 用户亲自创建和修改 C、头文件、CMake、启动文件及配置。
- Codex 负责讲解原理、检查用户提供的结果、协助定位问题和维护 Markdown 文档。
- 文档中的“已完成”必须对应真实操作或实机验证，不能把计划写成结果。
- 学习文档只沉淀可复用的FreeRTOS原理、源码关系和最终验证结论，不记录逐次断点操作、临时工具警告、截图或时间戳流水。

## 2. 当前工程和版本基线

### 2.1 硬件与工具链

| 项目 | 当前基线 |
| --- | --- |
| MCU | STM32F103ZET6，Cortex-M3 |
| 主频 | 72 MHz |
| SRAM | 64 KiB |
| Flash | 512 KiB |
| 编译器 | GNU Tools for STM32 / `arm-none-eabi-gcc` 14.3.1 |
| 构建配置 | CMake 4.3.1 |
| 构建执行 | GNU Make 4.4.1 |
| 下载与调试 | CMSIS-DAP + OpenOCD + `arm-none-eabi-gdb` |
| RTOS | FreeRTOS Kernel V11.3.0 |

### 2.2 已完成的裸机基线

真实硬件已经完成：

- CMake 配置成功。
- GNU Make 构建成功。
- ARM GCC 编译和链接成功。
- OpenOCD + CMSIS-DAP 烧录成功。
- PB5 红色 LED 约每 500 ms 翻转。
- GDB 连接和断点调试成功。

该基线非常重要。它说明启动文件、链接脚本、72 MHz 系统时钟、GPIO、下载器和调试器已经能够正常工作。后续如果 FreeRTOS Demo 失败，应优先检查新加入的内核配置、端口和异常入口，而不是重新怀疑所有裸机基础。

### 2.3 FreeRTOS 源码来源

完整官方快照保留于：

```text
resources/开发板资料/FreeRTOS-Kernel-11.3.0/
```

已核验：

```text
tskKERNEL_VERSION_NUMBER = "V11.3.0"
tskKERNEL_VERSION_MAJOR  = 11
tskKERNEL_VERSION_MINOR  = 3
tskKERNEL_VERSION_BUILD  = 0
```

固件工程只选择最小依赖：

```text
firmware/Libraries/FreeRTOS-Kernel/
├── include/
├── portable/GCC/ARM_CM3/
│   ├── port.c
│   └── portmacro.h
├── portable/MemMang/
│   └── heap_4.c
├── tasks.c
├── list.c
├── queue.c
├── LICENSE.md
├── README.md
└── History.txt
```

第二阶段为按键事件Queue加入了 `queue.c`。`timers.c`、`event_groups.c`、`stream_buffer.c`和 `croutine.c`仍未加入当前构建。

## 3. FreeRTOS 是什么

### 3.1 FreeRTOS 是实时操作系统内核

FreeRTOS 主要提供：

- 任务管理。
- 任务调度。
- 时间管理。
- 任务间通信。
- 任务同步。
- 中断与任务协作。
- 内存管理。

它不是包含驱动、图形界面、文件系统和网络服务的完整桌面操作系统。STM32 GPIO、RCC、CAN、SPI、W5500、MQTT 和业务逻辑仍由 CMSIS、STM32 标准外设库、第三方组件和应用代码实现。

### 3.2 “实时”不等于“运行得最快”

实时系统强调的是：

> 重要事件能否在已知、可分析的时间范围内得到响应。

一个程序平均速度很快，但偶尔因为不可控阻塞而晚响应几秒，不属于良好的实时行为。实时设计更关注：

- 最坏响应时间。
- 调度延迟。
- 中断延迟。
- 任务执行时间上限。
- 资源是否有界。
- 超时和失败路径是否明确。

### 3.3 FreeRTOS 不会让单核 CPU 真正并行

STM32F103 只有一个 Cortex-M3 CPU 核心。在任意时刻，CPU 只能执行一个指令流，因此最多只有一个 Task 处于 Running 状态。

FreeRTOS 提供的是并发：调度器在多个任务之间切换，使多个任务在一段时间内都得到运行机会。

```text
并行：多个 CPU 核心在同一时刻执行多个任务
并发：一个 CPU 在多个任务之间切换推进
```

本项目属于单核并发。

## 4. 为什么从裸机循环转向任务

### 4.1 裸机超级循环

典型裸机结构：

```c
int main(void)
{
    hardware_init();

    for (;;)
    {
        led_process();
        key_process();
        can_process();
        network_process();
    }
}
```

这种结构不是错误。对于功能少、时序简单的系统，它可能是最合适的设计。

困难在于：循环中的每个函数都必须快速返回。如果某个函数忙等 500 ms、等待网络数据或执行较长 Flash 操作，排在后面的模块都无法及时得到 CPU。

### 4.2 使用 FreeRTOS 后

可以按职责拆分：

```text
LED Task
Control Task
CAN Task
Network Task
```

每个任务可以拥有自己的循环和阻塞点：

```c
void led_task(void *argument)
{
    for (;;)
    {
        LED1_TOGGLE;
        vTaskDelay(...);
    }
}
```

关键不是“把所有函数都改成任务”，而是让具有不同周期、优先级和等待条件的职责能够清晰隔离。任务数量过多也会增加栈占用、调度复杂度和共享资源问题。

## 5. 一个 FreeRTOS Task 由什么组成

### 5.1 任务函数

任务函数描述任务要执行的逻辑。常见结构为无限循环：

```c
void task_function(void *argument)
{
    for (;;)
    {
        /* 完成一次工作。 */
        /* 阻塞等待下一周期或事件。 */
    }
}
```

任务通常不能直接从函数末尾返回。若任务确实需要结束，应按 FreeRTOS 规定删除任务，而不是像普通函数一样返回未知地址。

### 5.2 任务栈

每个任务有独立栈，用于保存：

- 局部变量。
- 函数参数。
- 函数返回地址。
- 嵌套调用现场。
- 上下文切换时保存的寄存器。

任务能够暂停在函数内部、以后从同一位置继续执行，关键基础就是独立任务栈。

Cortex-M3 的 `StackType_t` 为 32 位，因此栈深度参数的单位通常是 4 字节的栈元素，而不是字节：

```text
栈深度 128 × 4 字节 = 512 字节
```

### 5.3 TCB

TCB 是 Task Control Block，任务控制块。它是内核保存任务管理信息的数据结构，典型内容包括：

- 当前任务栈顶指针。
- 任务优先级。
- 任务状态链表节点。
- 事件等待链表节点。
- 任务名称。
- 任务通知值等可选信息。

可以把任务理解为：

```text
Task = 任务函数 + 任务栈 + TCB + 当前状态 + 优先级
```

### 5.4 任务优先级

FreeRTOS Task 优先级数值越大，优先级越高：

```text
Task Priority 3 高于 Task Priority 2
Task Priority 2 高于 Task Priority 1
Task Priority 1 高于 Idle Priority 0
```

这与 Cortex-M NVIC 中断优先级的数值方向相反。两套优先级属于不同系统，不能混用。

## 6. 任务的四种主要状态

### 6.1 Running

当前正在 CPU 上执行的任务。

单核 STM32F103 在任意时刻最多只有一个 Running Task。

### 6.2 Ready

任务已经具备运行条件，但正在等待 CPU。

可能有多个 Ready Task。调度器通常选择其中优先级最高者运行。

### 6.3 Blocked

任务正在等待时间或事件，例如：

- `vTaskDelay()` 等待时间到期。
- 等待队列消息。
- 等待信号量。
- 等待任务通知。

Blocked Task 不参与 CPU 竞争。阻塞是 FreeRTOS 高效并发的核心机制，不等于异常。

### 6.4 Suspended

任务被显式挂起。仅靠时间流逝或普通事件不会自动恢复，需要其他代码显式恢复。

### 6.5 状态关系

```text
                    被调度
             Ready ────────→ Running
               ↑                │
               │                │ 时间片结束、被高优先级任务抢占
               └────────────────┘

Running ──等待时间/事件──→ Blocked
  ↑                         │
  └────时间到或事件发生─────┘

Running/Ready/Blocked ──显式挂起──→ Suspended
Suspended ──显式恢复──→ Ready
```

状态变化的本质通常是：内核将任务对应的链表节点从一个链表移动到另一个链表。

## 7. 调度器如何选择任务

### 7.1 基本规则

可以先把调度器理解为：

> 从所有 Ready Task 中选择优先级最高者运行。

如果高优先级任务处于 Blocked，它就不参与选择，低优先级任务可以运行。

### 7.2 抢占

抢占式调度下，高优先级任务一旦从 Blocked 变为 Ready，可以要求调度器切换任务。

示例：

```text
LED Task，优先级 1
Idle Task，优先级 0
```

流程：

```text
LED Task Running
    ↓ 调用 vTaskDelay()
LED Task Blocked
    ↓
Idle Task Running
    ↓ LED 延时到期
LED Task Ready
    ↓ 优先级高于 Idle
LED Task 抢占 Idle Task
```

### 7.3 时间片

如果多个同优先级任务同时 Ready，启用时间片后，它们可以在 Tick 到达时轮流运行。

时间片主要处理“同优先级、同时 Ready”的任务；它与高优先级抢占低优先级不是同一个概念。

### 7.4 Idle Task

调度器启动时，内核会创建 Idle Task。它的优先级为 0，职责包括：

- 保证任何时刻至少有一个可运行任务。
- 清理已删除任务的部分资源。
- 在启用 Idle Hook 时调用应用回调。

Idle Task 能运行并不代表系统无用，而是说明当前没有更高优先级的 Ready Task。

### 7.5 为什么调度器需要链表

调度器需要频繁完成：

- 把新任务加入 Ready 状态。
- 把当前任务从 Ready 移到 Blocked。
- 把到期任务从 Blocked 移回 Ready。
- 在同优先级任务之间轮转。
- 删除或挂起任务。

这些操作如果反复移动整个 TCB，会增加复制成本。FreeRTOS 的做法是在每个 TCB 中嵌入链表节点，通过移动节点表示任务状态变化。

`include/list.h` 中的 `List_t` 主要包含：

```text
uxNumberOfItems  当前链表节点数量
pxIndex          遍历位置，同优先级轮转时会使用
xListEnd         链表尾标记，同时形成循环结构
```

`ListItem_t` 主要包含：

```text
xItemValue       排序值，例如任务唤醒Tick
pxNext           后一个节点
pxPrevious       前一个节点
pvOwner          节点属于谁，任务链表中通常指向TCB
pxContainer      当前节点属于哪个链表
```

因此可以从链表节点通过 `pvOwner` 找回对应 TCB，也可以通过 `pxContainer` 判断任务当前位于哪个内核链表。

### 7.6 一个 TCB 为什么有两个链表节点

V11.3.0 的 TCB 中包含：

```c
ListItem_t xStateListItem;
ListItem_t xEventListItem;
```

两者用途不同：

- `xStateListItem`：表示任务的调度状态，用于 Ready、Delayed 或 Suspended 等状态链表。
- `xEventListItem`：表示任务正在等待某个事件，例如以后等待队列、信号量或事件对象。

单纯调用 `vTaskDelay()` 时，主要移动 `xStateListItem`。以后任务等待队列消息并带有超时时间时，它可能同时：

```text
xStateListItem 位于延时链表，表示超时时间
xEventListItem 位于队列事件链表，表示等待队列事件
```

消息先到或超时先到，内核都会把任务从另一条相关链表中移除，保证任务只因一个最终条件进入 Ready。

### 7.7 Ready List 为什么是数组

`tasks.c` 中定义：

```c
static List_t pxReadyTasksLists[ configMAX_PRIORITIES ];
```

它不是一个就绪链表，而是“每个优先级一个就绪链表”。如果 `configMAX_PRIORITIES=5`，可以理解为：

```text
pxReadyTasksLists[0]  优先级0的Ready任务
pxReadyTasksLists[1]  优先级1的Ready任务
pxReadyTasksLists[2]  优先级2的Ready任务
pxReadyTasksLists[3]  优先级3的Ready任务
pxReadyTasksLists[4]  优先级4的Ready任务
```

选择任务时，内核先找到最高的非空优先级链表，再从该链表取任务。V11.3.0 的通用选择宏 `taskSELECT_HIGHEST_PRIORITY_TASK()` 会：

1. 从当前记录的最高 Ready 优先级向下寻找非空链表。
2. 使用 `listGET_OWNER_OF_NEXT_ENTRY()` 移动链表的 `pxIndex`。
3. 通过节点的 `pvOwner` 得到新的 `pxCurrentTCB`。

`pxIndex` 每次移动，使同优先级任务可以轮流被选中，这是同优先级时间片的链表基础。

### 7.8 Delayed List 为什么有两条

`tasks.c` 中实际维护：

```text
xDelayedTaskList1
xDelayedTaskList2
pxDelayedTaskList
pxOverflowDelayedTaskList
```

任务进入延时状态时，内核计算：

```text
xTimeToWake = 当前Tick + 等待Tick数
```

并把 `xTimeToWake` 写入任务的 `xStateListItem.xItemValue`。延时链表按照唤醒时间从小到大排序，因此链表头就是最近需要唤醒的任务。

使用两条链表是为了处理 Tick 计数器回绕。例如32位 Tick 接近最大值：

```text
当前Tick：0xFFFFFFF0
延时：    0x00000020
唤醒Tick：0x00000010
```

唤醒值数值上小于当前值，是因为加法越过了32位最大值。此任务会进入 Overflow Delayed List。Tick 从 `0xFFFFFFFF` 回到0时，内核交换两条延时链表：

```text
pxDelayedTaskList ↔ pxOverflowDelayedTaskList
```

这样链表内部仍可以使用简单、稳定的排序规则，不必让每次延时比较都处理复杂的回绕关系。

### 7.9 `vTaskDelay()` 的真实源码过程

V11.3.0 的 `vTaskDelay()` 位于 `tasks.c`。其关键过程可以按以下顺序理解：

```text
vTaskDelay(xTicksToDelay)
        │
        ├─ 如果延时为0：不进入Delayed，只请求重新调度
        │
        ├─ 暂停调度器的任务切换处理
        │
        ├─ prvAddCurrentTaskToDelayedList()
        │      ├─ 从当前优先级Ready List移除xStateListItem
        │      ├─ 计算xTimeToWake
        │      ├─ 把xTimeToWake写入xStateListItem
        │      └─ 插入当前Delayed或Overflow Delayed List
        │
        ├─ 恢复调度器
        └─ 请求Yield，因为当前任务已经不再Ready
```

这里“暂停调度器”不等于把当前任务放入 Suspended 状态，也不等于永久关闭全部中断。它是内核为了原子地修改调度数据结构而使用的短暂控制过程。

当前任务被移出 Ready List 后，不能继续长期执行，否则会出现“正在运行的任务在调度器看来却不可运行”的矛盾。因此 `vTaskDelay()` 最后必须请求重新调度。

### 7.10 Tick 如何把任务从 Blocked 唤醒

每次 SysTick 到达，ARM_CM3 端口调用 `xTaskIncrementTick()`。核心过程为：

1. `xTickCount` 增加1。
2. 如果 Tick 回绕到0，交换普通和 Overflow 延时链表。
3. 使用 `xNextTaskUnblockTime` 判断当前是否可能有任务到期。
4. 查看 Delayed List 头部任务的 `xItemValue`。
5. 如果唤醒时间尚未到，因为链表已排序，后续任务也不会到期，可以立即停止检查。
6. 如果到期，从 Delayed List 移除该任务。
7. 如果任务还在等待事件链表，也从事件链表移除。
8. 将任务加入它对应优先级的 Ready List。
9. 若抢占开启且被唤醒任务优先级高于当前任务，返回“需要切换”。

`xNextTaskUnblockTime` 是一个优化：延时链表头未到期之前，不必每个 Tick 都完整遍历延时链表。

### 7.11 Tick、任务选择和 PendSV 的分工

ARM_CM3 的 `xPortSysTickHandler()` 做的关键工作是：

```text
调用xTaskIncrementTick()
        ↓
如果返回需要切换
        ↓
设置PendSV挂起位
```

SysTick 不直接完成全部寄存器切换。真正上下文切换发生在 `xPortPendSVHandler()`：

1. 从 PSP 取得当前任务栈顶。
2. 将 R4-R11 压入当前任务栈。
3. 把新的栈顶保存到当前 TCB 的第一个成员。
4. 调用 `vTaskSwitchContext()`。
5. `vTaskSwitchContext()` 通过 Ready List 选择新的 `pxCurrentTCB`。
6. 从新 TCB 取出任务栈顶。
7. 恢复新任务的 R4-R11。
8. 更新 PSP，并从异常返回到新任务。

Cortex-M3 进入异常时，硬件已经自动保存 R0-R3、R12、LR、PC 和 xPSR；PendSV 软件只需补充保存没有被硬件自动保存的 R4-R11。

所以三个层次不能混淆：

```text
xTaskIncrementTick()   更新时间和任务状态，判断是否需要切换
vTaskSwitchContext()   选择新的pxCurrentTCB
xPortPendSVHandler()   真正保存/恢复寄存器和切换任务栈
```

### 7.12 LED Task 的完整时间线示例

假设：

```text
当前Tick = 100
LED Task优先级 = 1
Idle Task优先级 = 0
LED Task延时 = 500 Tick
```

过程如下：

```text
Tick 100：LED Task Running，翻转LED
    ↓
调用vTaskDelay(500)
    ↓
计算唤醒时间100+500=600
    ↓
LED Task从Ready[1]移到Delayed，xItemValue=600
    ↓
请求PendSV，切换到Idle Task
    ↓
Tick 101...599：LED Task仍在Delayed
    ↓
Tick 600：xTaskIncrementTick发现LED Task到期
    ↓
LED Task从Delayed移到Ready[1]
    ↓
LED优先级1高于当前Idle优先级0
    ↓
SysTick挂起PendSV
    ↓
PendSV保存Idle上下文，选择LED TCB，恢复LED上下文
    ↓
LED Task从vTaskDelay()之后继续运行
```

任务并不是从函数开头重新开始，而是依靠任务栈中保存的调用现场，从原先阻塞点之后继续。

### 7.13 哪些情况会触发重新调度

常见调度时机包括：

| 情况 | 为什么可能切换 |
| --- | --- |
| 当前任务调用 `vTaskDelay()` | 当前任务进入 Blocked，必须选择其他 Ready Task |
| 当前任务等待队列或信号量 | 当前任务等待事件，不再是 Ready |
| 当前任务主动 Yield | 主动让调度器重新选择任务 |
| Tick 使高优先级任务到期 | 新就绪任务可以抢占当前任务 |
| Tick 到达且有多个同优先级 Ready Task | 开启时间片时轮转 |
| 中断唤醒高优先级任务 | `FromISR` API 可以请求退出中断后切换 |

并非每次 SysTick 都必然切换任务。如果没有任务到期、没有同优先级时间片需要轮转、也没有挂起的 Yield 请求，当前任务可以继续运行。

### 7.14 本阶段常见误区

1. Ready 不等于 Running。Ready 只表示具备运行条件。
2. Blocked 不表示任务正在循环等待；它已经退出 CPU 竞争。
3. `vTaskDelay()` 返回后不是重新从任务函数开头执行，而是从保存的调用现场继续。
4. 每个优先级都有独立 Ready List，不是所有就绪任务混在一条链表。
5. `vTaskSwitchContext()` 主要选择任务，不负责完整汇编级寄存器切换。
6. SysTick 负责时间基础，但上下文保存/恢复由 PendSV 完成。
7. 每个 Tick 不一定发生上下文切换。
8. 调度器暂时挂起与 Task 的 Suspended 状态不是同一概念。

### 7.15 本课自测

1. 为什么 TCB 中需要 `xStateListItem` 和 `xEventListItem` 两个节点？
2. `pxReadyTasksLists[3]` 表示什么？
3. 为什么 Delayed List 按唤醒 Tick 排序？
4. 为什么 Tick 回绕需要两条 Delayed List？
5. `vTaskDelay(0)` 会不会让任务进入延时链表？
6. Tick 600 唤醒优先级1的 LED Task 时，当前优先级0的 Idle Task 为什么会被抢占？
7. `xTaskIncrementTick()`、`vTaskSwitchContext()` 和 PendSV 各做什么？
8. 为什么不是每次 SysTick 都一定切换任务？

### 7.16 自测答案解析

#### 第1题：`vTaskDelay()` 后 TCB 和任务栈在哪里

用户最初理解：TCB 不变并仍在自己的任务栈中，真正移动的是 TCB 内的链表节点。

其中“真正移动的是 TCB 内的链表节点”正确，但“TCB 在任务栈中”不正确。

动态创建任务时，FreeRTOS 通常分别分配：

```text
一块TCB内存
一块任务栈内存
```

V11.3.0 的 `tasks.c` 中可以看到两类独立分配：

```text
pvPortMalloc(sizeof(TCB_t))
pvPortMallocStack(uxStackDepth * sizeof(StackType_t))
```

二者关系为：

```text
TCB
 ├─ pxStack       指向任务栈起始区域
 ├─ pxTopOfStack  记录任务当前栈顶
 ├─ xStateListItem
 └─ xEventListItem

任务栈
 └─ 保存局部变量、函数调用现场和任务上下文
```

调用 `vTaskDelay()` 后：

- TCB 的内存地址通常不变。
- 任务栈的内存地址不变，栈中保存阻塞点的调用现场。
- TCB 内嵌的 `xStateListItem` 从 Ready List 移到 Delayed List。
- 链表节点的 `pxNext`、`pxPrevious` 和 `pxContainer` 等字段会改变。
- 调度器随后把 `pxCurrentTCB` 改为另一个被选中任务的 TCB。

准确结论：

> 不是移动整个 TCB，也不是移动任务栈，而是移动 TCB 内嵌的状态链表节点；TCB 和任务栈是互相关联但彼此独立的内存对象。

#### 第2题：Tick 到期后谁把 Blocked Task 移回 Ready

用户最初理解：Tick 到期触发 PendSV，在 PendSV 中把 Blocked Task 放入 Ready List。

这个过程发生位置需要纠正。Blocked→Ready 的状态迁移发生在 SysTick 调用的 `xTaskIncrementTick()` 中，不是在 PendSV 中。

实际顺序：

```text
SysTick异常进入xPortSysTickHandler()
        ↓
调用xTaskIncrementTick()
        ↓
xTickCount加1
        ↓
检查Delayed List头部
        ↓
到期任务执行listREMOVE_ITEM()
        ↓
执行prvAddTaskToReadyList()
        ↓
任务已经从Blocked变为Ready
        ↓
如果需要抢占，xTaskIncrementTick()返回pdTRUE
        ↓
xPortSysTickHandler()设置PendSV挂起位
        ↓
PendSV稍后执行真正的上下文切换
```

因此 PendSV 执行前，到期任务已经在 Ready List 中。PendSV 不负责判断延时是否到期，也不负责把 Delayed 节点移回 Ready。

#### 第3题：三层职责的精确划分

用户对总体方向理解正确，但需要把“置位 PendSV”的执行者说得更精确：

```text
xTaskIncrementTick()
  ├─ Tick加1
  ├─ 更新Delayed/Ready链表
  ├─ 处理同优先级时间片条件
  └─ 返回是否需要切换

xPortSysTickHandler()
  ├─ 调用xTaskIncrementTick()
  └─ 返回pdTRUE时设置PendSV挂起位

xPortPendSVHandler()
  ├─ 保存当前任务寄存器和PSP
  ├─ 调用vTaskSwitchContext()
  ├─ 从Ready List选择新的pxCurrentTCB
  └─ 恢复新任务寄存器和PSP
```

不能简单说“`xTaskIncrementTick()` 置位 PendSV”。它只返回判断结果，真正写 NVIC PendSV Set 位的是 ARM_CM3 端口的 `xPortSysTickHandler()`。

#### 本次答题后的正确链路

```text
状态更新：xTaskIncrementTick()
切换请求：xPortSysTickHandler()挂起PendSV
任务选择：vTaskSwitchContext()
现场切换：xPortPendSVHandler()
```

## 8. `vTaskDelay()` 为什么不浪费 CPU

裸机忙等：

```text
CPU 反复检查计数标志
任务逻辑无法做其他工作
```

FreeRTOS 延时：

```text
当前任务进入 Blocked
调度器运行其他 Ready Task
Tick 到期后任务重新 Ready
```

以 LED Task 为例：

```text
LED Task 翻转 LED
        ↓
vTaskDelay(500 Tick)
        ↓
LED Task 从就绪链表移出
        ↓
加入延时链表
        ↓
其他任务或 Idle Task 运行
        ↓
500 Tick 到期
        ↓
LED Task 移回就绪链表
```

所以 `vTaskDelay()` 阻塞的是当前任务，不是 CPU，也不是整个调度器。

## 9. Tick：FreeRTOS 的时间基础

### 9.1 当前硬件事实

CMSIS 当前维护：

```text
SystemCoreClock = 72,000,000 Hz
```

STM32F103 实现 4 个 NVIC 优先级位。

### 9.2 Tick 频率

如果配置为 1000 Hz：

```text
每秒 1000 Tick
每个 Tick 1 ms
```

ARM_CM3 端口的 SysTick 重装关系为：

```text
SysTick LOAD = SysTick时钟 / Tick频率 - 1
```

假设 SysTick 使用 72 MHz：

```text
72,000,000 / 1000 - 1 = 71,999
```

SysTick 从 71,999 计数到 0，共经过 72,000 个时钟周期，产生一次 1 ms Tick。

### 9.3 毫秒与 Tick

应使用：

```c
pdMS_TO_TICKS(500U)
```

而不是在应用中假定一个 Tick 永远等于 1 ms。这样以后改变 Tick 频率时，代码语义仍然是“500 ms”。

### 9.4 Tick 频率的权衡

Tick 频率越高：

- 时间分辨率越细。
- Tick 中断次数越多。
- 调度判断开销越大。

Tick 频率越低：

- 中断开销更低。
- 延时和超时分辨率更粗。

1 kHz 是当前最小 Demo 的候选值，不应把它当作所有 FreeRTOS 项目的固定答案。

## 10. Cortex-M3 的 SVC、PendSV 和 SysTick

### 10.1 SysTick

SysTick 提供周期性 RTOS Tick。每次 Tick 到达，内核更新系统时间、检查延时任务是否到期，并判断是否需要调度。

### 10.2 PendSV

PendSV 用于真正执行任务上下文切换。它通常设置为最低异常优先级，使重要外设中断先完成，再切换任务。

上下文切换需要：

```text
保存当前任务寄存器到当前任务栈
把当前栈顶写入当前任务TCB
选择下一个任务TCB
取出下一个任务栈顶
恢复下一个任务寄存器
返回到下一个任务
```

### 10.3 SVC

SVC 是 Supervisor Call。FreeRTOS Cortex-M3 端口使用它启动第一个任务，使 CPU 从内核启动环境进入第一个任务保存的上下文。

### 10.4 三者分工

```text
SysTick：时间到了，判断是否需要调度
PendSV：真正保存和恢复任务上下文
SVC：启动第一个任务
```

当前只建立概念，后续会结合 `port.c` 和启动文件逐行确认三个入口如何连接。

### 10.5 Cortex-M3 的两种执行模式

Cortex-M3 有两个与本次学习直接相关的执行模式：

```text
Thread Mode   普通程序和FreeRTOS Task运行的模式
Handler Mode  SVC、PendSV、SysTick和外设中断运行的模式
```

复位完成后进入 `main()` 时，CPU 处于 Thread Mode。异常或中断到来时，CPU 自动进入 Handler Mode；异常返回后，再回到 Thread Mode。

模式与任务优先级不是同一概念。Handler Mode 表示 CPU 正在执行异常处理，Thread Mode 表示 CPU 正在执行普通代码或任务。

### 10.6 MSP 与 PSP

Cortex-M3 提供两个栈指针：

```text
MSP  Main Stack Pointer
PSP  Process Stack Pointer
```

基本规则：

| 执行场景 | 使用的栈指针 |
| --- | --- |
| 复位后的启动代码 | MSP |
| 调度器启动前的 `main()` | MSP |
| FreeRTOS Task | PSP |
| SVC、PendSV、SysTick、外设中断 | MSP |

Handler Mode 固定使用 MSP。Thread Mode 可以选择 MSP 或 PSP：复位后默认使用 MSP，FreeRTOS 启动第一个任务时通过异常返回机制让 Thread Mode 改用 PSP。

这样形成隔离：

```text
MSP：所有异常/中断共享的系统栈
PSP：当前Task自己的任务栈
```

切换任务时不是把整个任务栈复制到别处，而是让 PSP 指向另一个任务已经保存好的栈位置。

### 10.7 复位到 `main()`：为什么开始时使用 MSP

当前启动文件的中断向量表第一个32位值是 `_estack`：

```asm
.word _estack
.word Reset_Handler
```

链接脚本定义：

```text
RAM起始：0x20000000
RAM长度：64 KiB
_estack：0x20000000 + 64 KiB = 0x20010000
```

复位时，Cortex-M3 硬件自动完成：

1. 从向量表第0项读取 `0x20010000` 写入 MSP。
2. 从向量表第1项读取 `Reset_Handler` 地址写入 PC。
3. 从 `Reset_Handler` 开始执行。

当前 `Reset_Handler` 随后：

```text
调用SystemInit()
复制.data到RAM
清零.bss
调用main()
```

这些代码都发生在调度器启动之前，因此使用 MSP。

### 10.8 新任务还没有运行，为什么已经有上下文

创建新任务时，它以前从未真正执行过，理论上没有可以“恢复”的旧现场。FreeRTOS 通过 `pxPortInitialiseStack()` 在任务栈中人工构造一份初始现场，使它看起来像一个刚被异常打断、即将通过异常返回开始运行的任务。

V11.3.0 ARM_CM3 端口预先放入：

```text
xPSR  设置Thumb状态位
PC    任务函数入口pxCode
LR    任务意外返回时的错误入口
R12
R3
R2
R1
R0    任务参数pvParameters
R11-R4
```

概念布局如下。Cortex-M 栈向低地址增长：

```text
高地址：任务栈起始顶部
        xPSR
        PC = 任务函数入口
        LR = 任务返回错误处理
        R12
        R3
        R2
        R1
        R0 = pvParameters
        R11
        R10
        R9
        R8
        R7
        R6
        R5
低地址：R4  ← pxTopOfStack保存到TCB
```

这份人工栈帧与后续 PendSV 保存的任务上下文格式保持一致，因此第一次启动任务和以后恢复任务可以复用同一种恢复思路。

### 10.9 为什么任务函数不能直接 `return`

普通函数通过 LR 中的返回地址回到调用者。但任务函数不是由普通业务函数调用后等待返回的，它是调度器恢复出来的执行上下文，没有正常调用者。

`pxPortInitialiseStack()` 把初始 LR 设置为任务返回错误入口。若任务函数意外返回，端口会触发断言并停住，帮助发现错误。

因此任务函数通常：

```c
void task_function(void *argument)
{
    for (;;)
    {
        /* 任务工作。 */
    }
}
```

需要结束任务时，应使用规定的任务删除机制，而不是直接 `return`。

### 10.10 第一个任务如何启动

`vTaskStartScheduler()` 在通用内核中建立 Idle Task、选择首个任务并调用端口层 `xPortStartScheduler()`。ARM_CM3 端口随后：

1. 检查 SVC 和 PendSV Handler 是否正确安装到向量表。
2. 设置 PendSV 和 SysTick 为最低异常优先级。
3. 设置 SVC 为最高异常优先级。
4. 配置产生 RTOS Tick 的 SysTick。
5. 调用 `prvPortStartFirstTask()`。

`prvPortStartFirstTask()` 的关键过程：

```text
从VTOR找到当前向量表
读取向量表第0项的初始栈顶
重新写入MSP
使能中断
执行svc 0
```

重新设置 MSP 的意义是：启动和 C 初始化阶段曾使用过 MSP。调度器正式启动前，将 MSP 恢复到向量表记录的初始顶部，使后续中断拥有干净的 Handler 栈起点。调度器启动后，原先 `main()` 的普通调用链不会再正常返回。

执行 `svc 0` 后：

1. CPU 从 Thread Mode 进入 Handler Mode。
2. 进入 `vPortSVCHandler()`，Handler 使用 MSP。
3. 从 `pxCurrentTCB` 取得第一个任务保存的栈顶。
4. 从任务栈恢复 R4-R11。
5. 把恢复后的地址写入 PSP。
6. 构造异常返回状态，使异常返回到 Thread Mode 并使用 PSP。
7. CPU 自动从 PSP 恢复 R0-R3、R12、LR、PC、xPSR。
8. PC 恢复为任务函数入口，R0 恢复为任务参数，第一个任务开始执行。

所以第一个任务不是由普通 C 语句直接调用，而是通过 SVC 的异常返回机制“恢复”出来的。

### 10.11 异常到来时硬件自动保存什么

当正在运行的 Task 使用 PSP，SysTick 或 PendSV 等异常到来时，Cortex-M3 硬件自动将以下寄存器压入当时活动的任务栈 PSP：

```text
R0
R1
R2
R3
R12
LR
PC
xPSR
```

这8个32位寄存器组成基本异常栈帧，共32字节。

自动压栈完成后：

- CPU 进入 Handler Mode。
- Handler 本身改用 MSP。
- 被中断任务的硬件现场留在它自己的 PSP 栈中。

这样中断函数使用 MSP 时，不会继续消耗当前任务 PSP 上的普通 Handler 调用栈；但硬件自动保存的基本异常帧仍属于被中断任务的 PSP 上下文。

### 10.12 为什么 PendSV 只手动保存 R4-R11

Cortex-M3 异常入口已经自动保存 R0-R3、R12、LR、PC 和 xPSR，但没有保存 R4-R11。

根据 ARM 调用约定，R4-R11 属于需要跨函数调用保留的寄存器。若任务切换时不保存它们，新任务会破坏旧任务的数据。

因此 PendSV 补充：

```asm
mrs   r0, psp
stmdb r0!, {r4-r11}
```

完整任务上下文可以看成：

```text
硬件自动保存：R0-R3、R12、LR、PC、xPSR
PendSV软件保存：R4-R11
TCB保存：任务栈顶pxTopOfStack
```

基础上下文共有16个32位寄存器槽，至少64字节。实际任务栈还必须容纳任务函数调用、局部变量和中断到来时的栈帧，因此不能只按64字节配置任务栈。

### 10.13 后续任务切换的完整过程

任务A运行时：

```text
Thread Mode
PSP指向任务A栈
MSP保留给异常
```

PendSV 到来：

```text
1. 硬件把A的R0-R3、R12、LR、PC、xPSR压入A的PSP栈
2. CPU进入Handler Mode，Handler使用MSP
3. PendSV读取PSP
4. PendSV把A的R4-R11压入A的PSP栈
5. 把A的新栈顶写入A的TCB
6. vTaskSwitchContext()把pxCurrentTCB改为任务B的TCB
7. 从B的TCB读取B的栈顶
8. 从B的PSP栈恢复R4-R11
9. 将恢复后的地址写入PSP
10. 异常返回时，硬件从B的PSP栈恢复其余寄存器
11. CPU回到Thread Mode，从B原先的PC继续执行
```

整个过程没有复制任务A或B的整块任务栈。内核只保存各自的栈顶指针，并在切换时恢复对应寄存器。

### 10.14 MSP/PSP 内存关系图

```text
STM32F103 SRAM
高地址 0x20010000
┌────────────────────────────┐
│ MSP初始栈顶                 │ ← 向量表第0项_estack
│ Handler使用的主栈           │
│            ↓ 向低地址增长   │
├────────────────────────────┤
│ 其他.data/.bss/预留区域     │
├────────────────────────────┤
│ FreeRTOS heap_4管理区域     │
│  ├─ Task A TCB              │
│  ├─ Task A Stack            │ ← Task A运行时PSP
│  ├─ Task B TCB              │
│  └─ Task B Stack            │ ← Task B运行时PSP
├────────────────────────────┤
│ 其他全局/静态数据           │
└────────────────────────────┘
低地址 0x20000000
```

该图是概念关系，不表示链接器最终严格按此顺序排列。实际地址必须通过 MAP 文件和 GDB 查看。

### 10.15 为什么分离 MSP 和 PSP

主要收益：

- 每个任务拥有独立 PSP，任务上下文自然隔离。
- 所有异常统一使用 MSP，不需要为每个 Handler 维护独立栈指针。
- 调度器只需在 TCB 中保存任务 PSP 栈顶即可切换任务。
- 任务栈溢出和 Handler 栈占用可以作为不同问题分析。

但这种分离不等于完整内存保护。普通 Cortex-M3 非 MPU FreeRTOS Task 仍可能错误访问其他任务内存；独立栈主要提供执行现场隔离，而不是硬件访问权限隔离。

### 10.16 本阶段常见误区

1. MSP 不是“main函数专用栈”。它是 Main Stack Pointer，调度前 Thread Mode 使用它，所有 Handler Mode 也使用它。
2. PSP 不是一个固定任务的栈。它始终指向当前运行任务的栈，切换任务时会更新。
3. TCB 不在任务栈中；TCB 保存指向任务栈和当前栈顶的指针。
4. PendSV 不是复制整个栈，只保存寄存器并切换栈顶指针。
5. 硬件自动压栈发生在进入异常时，PendSV 汇编只补充保存 R4-R11。
6. 第一个任务不是从 `main()` 普通调用，而是通过 SVC 异常返回启动。
7. Handler Mode 始终使用 MSP，即使被中断 Task 在 Thread Mode 使用 PSP。
8. 独立任务栈不等于 MPU 内存保护。

### 10.17 本课自测

1. 复位时 MSP 的初始值从哪里取得？当前工程中是多少？
2. 调度器启动前的 `main()` 使用 MSP 还是 PSP？
3. FreeRTOS Task 正常运行时使用 MSP 还是 PSP？
4. SysTick 和 PendSV Handler 使用 MSP 还是 PSP？
5. 新任务从未运行过，FreeRTOS 如何让它拥有可恢复的初始上下文？
6. 异常入口硬件自动保存哪8个寄存器？
7. PendSV 为什么还要保存 R4-R11？
8. `vTaskSwitchContext()` 是否复制任务栈？它实际改变什么？
9. 第一个任务为什么通过 SVC 启动，而不是在 `main()` 中直接调用任务函数？
10. Task A 切换到 Task B 时，MSP 和 PSP 分别承担什么工作？

### 10.18 MSP/PSP 自测答案解析

#### 第1题：异常自动压栈与 Handler 使用的栈

用户回答：Task 使用 PSP 运行时，硬件自动现场压入 PSP；进入 Handler Mode 后使用 MSP。

回答正确。需要保持这个时间顺序：

```text
异常发生前：Thread Mode，Task使用PSP
异常进入时：硬件把基本异常帧压入当时活动的PSP
异常处理时：CPU进入Handler Mode，Handler使用MSP
```

“异常帧保存在哪里”与“Handler代码自身使用哪个栈”是两个不同问题。

#### 第2题：从未运行的新任务为何可以异常返回

用户回答方向正确，但首次启动时并不存在真正的旧任务现场。准确过程是：

1. `pxPortInitialiseStack()` 提前在新任务栈中人工写入 xPSR、PC、LR、R0-R12 等初始值。
2. PC 被设置为任务函数入口，R0 被设置为任务参数。
3. SVC Handler 从 TCB 取得这份人工任务栈，先恢复 R4-R11。
4. SVC Handler 设置 PSP 指向剩余的硬件异常帧。
5. 异常返回时，Cortex-M3 从 PSP 自动弹出 R0-R3、R12、LR、PC 和 xPSR。
6. PC 取得任务函数入口，所以 CPU 开始执行任务函数。

因此不是“先实际运行并保存任务”，而是“先伪造一份符合异常返回格式的初始上下文”。

#### 第3题：PendSV 为什么只手动保存 R4-R11

用户列出了 R0-R3、R12、LR、PC 和 xPSR，方向正确。完整结论是：

> Cortex-M3 进入异常时已经由硬件自动保存 R0-R3、R12、LR、PC 和 xPSR，因此 PendSV 软件只需要补充保存硬件没有自动保存的 R4-R11。

恢复时也按相反顺序协作：PendSV 软件恢复 R4-R11，异常返回硬件恢复其余基本异常帧。

#### 第4题：Task A 切换到 Task B

完整过程为：

```text
硬件把A的基本异常帧压入A的PSP栈
        ↓
CPU进入Handler Mode，Handler使用MSP
        ↓
PendSV把A的R4-R11压入A的任务栈
        ↓
A的新PSP栈顶写入A的TCB
        ↓
vTaskSwitchContext()选择B的TCB
        ↓
从B的TCB读取栈顶
        ↓
恢复B的R4-R11并更新PSP
        ↓
异常返回硬件恢复B的基本异常帧
        ↓
从B保存的PC继续执行
```

没有复制 A、B 的整块任务栈。真正切换的是：

```text
当前TCB
当前任务保存的PSP栈顶
由栈顶所定位的寄存器现场
```

一个更细的注意点：PendSV Handler 自身在 Handler Mode 使用 MSP，但汇编先通过 `mrs r0, psp` 得到 Task A 的 PSP 数值，再通过 `stmdb r0!, {r4-r11}` 把寄存器写入 Task A 的任务栈。也就是说，“Handler 使用 MSP”并不妨碍 Handler 显式读写任务的 PSP 栈。

在 Markdown 学习记录中，这类流程图代码块应标记为 `text`，而不是 `css`；这只影响文档语义和高亮，不影响 FreeRTOS 原理。

## 11. FreeRTOS 源码分层

### 11.1 `include/`

包含公共 API 头文件和内核共享定义：

| 文件 | 主要职责 |
| --- | --- |
| `FreeRTOS.h` | 内核总入口、基础类型、公共宏和配置检查 |
| `task.h` | 任务 API |
| `queue.h` | 队列 API |
| `semphr.h` | 信号量和互斥量 API |
| `timers.h` | 软件定时器 API |
| `event_groups.h` | 事件组 API |
| `stream_buffer.h` | 流缓冲和消息缓冲 API |
| `list.h` | 内核链表声明，主要供内核内部使用 |
| `portable.h` | 通用内核与端口层接口 |

不是 `include/` 中的每个头文件都要由应用直接包含。

### 11.2 通用内核源文件

| 文件 | 主要职责 |
| --- | --- |
| `tasks.c` | 任务创建、延时、状态变化和调度器核心 |
| `list.c` | 内核内部链表 |
| `queue.c` | 队列，以及信号量和互斥量的底层基础 |
| `timers.c` | 软件定时器和 Timer Service Task |
| `event_groups.c` | 事件组 |
| `stream_buffer.c` | 流缓冲和消息缓冲 |
| `croutine.c` | 旧式协程，本项目不使用 |

### 11.3 `portable/`

`portable/` 主要适配“编译器 + CPU 架构”，不是直接适配具体开发板外设。

当前选择：

```text
编译器：GNU GCC
CPU：Cortex-M3
端口：portable/GCC/ARM_CM3
```

其中：

- `port.c`：调度器启动、SysTick 和上下文切换底层逻辑。
- `portmacro.h`：栈类型、Tick 类型、临界区和任务切换宏。
- `heap_4.c`：独立的动态内存管理策略。

ARM_CM3 端口不负责 STM32 GPIO、RCC、CAN、SPI 等外设初始化。

## 12. `FreeRTOS.h` 与 `FreeRTOSConfig.h`

### 12.1 两者不是二选一

```text
FreeRTOS.h        内核提供，说明内核的公共基础
FreeRTOSConfig.h  应用提供，决定本工程如何使用内核
```

V11.3.0 的 `FreeRTOS.h` 第 58 行直接包含：

```c
#include "FreeRTOSConfig.h"
```

随后检查应用是否定义必要宏。缺少最小栈、最大优先级数量、抢占设置等必要配置时，内核会用 `#error` 停止编译。

### 12.2 典型包含关系

```text
main.c
  ├─ FreeRTOS.h
  │    ├─ FreeRTOSConfig.h
  │    ├─ portable.h
  │    └─ portmacro.h
  └─ task.h
```

应用通常先包含：

```c
#include "FreeRTOS.h"
#include "task.h"
```

`FreeRTOS.h` 不是所有 API 函数声明的集合。任务 API 在 `task.h`，队列 API 在 `queue.h`，信号量 API 在 `semphr.h`。

### 12.3 为什么配置必须独立

同一份 FreeRTOS Kernel 可以运行于不同 CPU 频率、内存容量和业务需求的工程。如果把具体参数修改进官方 `FreeRTOS.h`：

- 会污染上游内核源码。
- 升级内核时容易丢失配置。
- 无法清晰区分通用内核和项目决策。
- 多个项目难以复用同一版本内核。

因此项目应在 `User/FreeRTOSConfig.h` 中维护自己的配置。

## 13. `heap_4.c` 与 STM32 SRAM

### 13.1 `heap_4.c` 做什么

`heap_4.c` 实现 FreeRTOS 的 `pvPortMalloc()` 和 `vPortFree()`，支持释放并合并相邻空闲块。

动态创建任务时，内核通常从 RTOS Heap 分配：

```text
TCB
任务栈
```

以后动态创建队列、信号量等对象，也会消耗 RTOS Heap。

### 13.2 RTOS Heap 不是全部 SRAM

STM32F103ZET6 的 64 KiB SRAM 还需要容纳：

- `.data`。
- `.bss`。
- FreeRTOS Heap 数组。
- 主栈 MSP。
- 全局和静态变量。
- 链接脚本预留的 C Heap/Stack 区域。

因此不能把 `configTOTAL_HEAP_SIZE` 直接设置成 64 KiB。必须在链接后结合 MAP 和内存使用输出检查。

### 13.3 五种官方 Heap 的基本区别

| 实现 | 特点 |
| --- | --- |
| `heap_1.c` | 只分配、不释放，最简单 |
| `heap_2.c` | 可释放，但不合并相邻空闲块 |
| `heap_3.c` | 包装 C 库 `malloc/free` |
| `heap_4.c` | 可释放并合并相邻空闲块，适合单一连续 RAM 区域 |
| `heap_5.c` | 支持多个不连续内存区域 |

本项目选择 `heap_4.c`，但选择理由和运行结果仍需通过最小 Demo 验证。

## 14. 第一阶段源码关系总图

```text
应用 main.c / LED Task
        │
        ├─ FreeRTOS.h ──→ FreeRTOSConfig.h
        └─ task.h
              │
              ▼
          tasks.c
       任务和调度器核心
              │
              ├─ list.c
              │  就绪/延时链表
              │
              ├─ heap_4.c
              │  TCB和任务栈内存
              │
              └─ ARM_CM3/port.c
                 SVC/PendSV/SysTick
                 保存和恢复上下文
```

## 15. 学习和移植进度

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 0 | 裸机 LED、工具链、烧录和 GDB 基线 | 已完成实机验证 |
| 1 | 下载并核验 FreeRTOS Kernel V11.3.0 | 已完成 |
| 2 | 选择最小通用内核、ARM_CM3 端口和 `heap_4.c` | 已完成文件准备 |
| 3 | 理解 FreeRTOS、Task、TCB、任务栈、状态和调度器 | 已完成概念、源码路径、调度器运行及阻塞/唤醒实机验证 |
| 4 | 理解 Tick、SVC、PendSV 和 Cortex-M3 上下文切换 | 已完成MSP/PSP、异常自动压栈、首任务启动和后续切换学习，调度器已在硬件运行 |
| 4A | 理解Task/IRQ优先级、NVIC分组、`BASEPRI`与`FromISR`边界 | 已完成讲解、自测，并通过逻辑优先级6的DMA ISR调用FromISR API完成实机验证 |
| 4B | 理解 GNU 启动文件、链接脚本和 MCU 上电流程 | 已结合当前工程讲解，待后续通过 MAP、反汇编和 GDB 验证 |
| 5 | 理解并编写 `FreeRTOSConfig.h` | 已由用户完成配置与Hook；动态分配、Queue和Task Notification均已进入运行路径 |
| 6 | 将最小内核加入 CMake | 已完成首次链接；`-Wl,-z,noexecstack`已在正式ELF中验证生效，`GNU_STACK`为`RW` |
| 6A | 规范固件目录分层 | LED、Key、USART、TIM6 BSP以及对应应用、Console和诊断模块均已按功能域落位并完成构建/实机回归 |
| 7 | 用户编写 LED Task 并启动调度器 | `main.c`已调用`xTaskCreate()`和`vTaskStartScheduler()`并成功生成ELF |
| 8 | ELF、MAP、异常符号和内存检查 | 符号、Heap、异常向量、内存边界、GNU_STACK以及TIM6运行统计符号复核通过 |
| 9 | 烧录、GDB 和连续运行验收 | 调度器、Queue Demo及USART1 TX/RX DMA → ISR → Task Notification闭环已完成真实硬件验证 |
| 10 | 建立有界Serial TX Queue | 私有Queue、消息复制、FIFO发送和单一TX所有权已完成构建与真实硬件验证 |
| 11 | 建立USART1 RX循环DMA闭环 | Channel 5循环接收、IDLE通知、CNDTR位置计算、原始字节回显和缓冲区回绕已完成构建与真实硬件验证 |
| 12 | 建立最小Console行协议 | 有界行缓冲、CR/LF/CRLF、退格、超长整行丢弃、只读命令分发和边界恢复已完成构建与真实硬件验证 |
| 13 | 建立任务、运行时间和Heap诊断 | Trace/Stats、TIM6 10 kHz、`task`/`heap`以及栈、CPU、Heap实机验证已完成 |
| 14 | 建立ADC1双通道低频采样闭环 | PC1/ADC1_IN11电位器与ADC1_IN16内部温度通道已由单个ADC Task顺序采样，并通过现有Serial TX Queue完成实机报告 |

## 16. 当前应掌握的核心结论

1. FreeRTOS 是实时内核，不是包含全部驱动和服务的完整操作系统。
2. 单核 STM32 上多个任务是并发推进，不是真正同时并行。
3. Task 由任务函数、任务栈、TCB、状态和优先级共同构成。
4. 单核任意时刻最多一个 Running Task，但可以有多个 Ready 或 Blocked Task。
5. 调度器从 Ready Task 中选择最高优先级任务。
6. `vTaskDelay()` 阻塞当前任务，不阻塞整个 CPU。
7. 高优先级任务阻塞后，低优先级任务仍可运行。
8. `list.c` 提供调度数据结构，`tasks.c` 在其上实现任务和调度。
9. `port.c` 执行 Cortex-M3 相关的底层上下文切换。
10. 每个任务有独立栈，TCB 保存其栈顶及管理信息。
11. `FreeRTOS.h` 会包含项目自己的 `FreeRTOSConfig.h`。
12. RTOS Heap 只是 64 KiB SRAM 的一部分。

## 17. 自测题

在继续编写配置前，应能回答：

1. STM32F103 只有一个 CPU 核心，为什么多个 Task 看起来能同时工作？
2. `vTaskDelay()` 与裸机忙等延时的本质区别是什么？
3. 高优先级任务进入 Blocked 后，低优先级任务能否运行？为什么？
4. Ready 与 Running 有什么区别？
5. TCB 和任务栈分别保存什么？
6. 为什么 `list.c` 是调度器的重要基础？
7. SysTick、PendSV 和 SVC 各自负责什么？
8. 为什么不能直接修改官方 `FreeRTOS.h` 填写项目配置？
9. 栈深度 128 在 Cortex-M3 上为什么通常不是 128 字节？
10. `configTOTAL_HEAP_SIZE` 为什么不能等于 MCU 的全部 SRAM？

## 18. 建议学习顺序

学习配置和移植时，建议按以下顺序建立知识关系，而不是直接抄写完整配置文件：

1. 任务状态链表与调度时机。
2. Cortex-M3 异常、MSP/PSP 和任务上下文。
3. 任务优先级与 NVIC 中断优先级的区别。
4. `FreeRTOSConfig.h` 的时钟与 Tick 配置。
5. 调度、Heap、栈和调试 Hook 配置。
6. 最小 CMake 接入及第一次编译错误的含义。

每完成一个知识点和实际操作，都继续在本文中记录原理、操作、现象和结论。

## 19. 第4课：任务优先级、中断优先级与 `BASEPRI`

### 19.1 先区分两套完全不同的优先级

FreeRTOS Task 优先级由软件调度器使用，决定多个 Ready Task 中谁先运行：

```text
Task优先级：数值越大，优先级越高
```

Cortex-M NVIC 中断优先级由硬件使用，决定异常之间的抢占关系：

```text
IRQ优先级：数值越小，紧急程度越高
```

示例：

```text
Task Priority 4 高于 Task Priority 1
IRQ Priority 1 高于 IRQ Priority 4
```

两套优先级不能直接比较。Task Priority 4 不会阻止一个最低紧急程度的外设中断 Priority 15：只要该中断已使能且没有被屏蔽，Handler Mode 仍可抢占 Thread Mode 中的任何 Task。

因此：

```text
任务优先级解决：Ready任务之间谁运行
中断优先级解决：异常之间谁能抢占谁，以及谁会被BASEPRI屏蔽
```

### 19.2 STM32F103 为什么只有0～15共16级

NVIC 的每个优先级寄存器字段占8位，但具体芯片可以只实现高若干位。当前 `stm32f10x.h` 明确定义：

```c
#define __NVIC_PRIO_BITS 4
```

所以 STM32F103 只实现高4位，逻辑优先级范围为：

```text
0～15，共16级
```

写入8位硬件寄存器时，4位逻辑值放在高4位，低4位没有实现：

| 逻辑优先级 | 二进制逻辑值 | NVIC 8位寄存器值 | 紧急程度 |
| --- | --- | --- | --- |
| 0 | `0000` | `0x00` | 最高 |
| 1 | `0001` | `0x10` | 很高 |
| 4 | `0100` | `0x40` | 高 |
| 5 | `0101` | `0x50` | 候选FreeRTOS API边界 |
| 15 | `1111` | `0xF0` | 最低 |

换算公式：

```text
硬件寄存器值 = 逻辑优先级 << (8 - __NVIC_PRIO_BITS)
             = 逻辑优先级 << 4
```

### 19.3 为什么同一个优先级会看到5和`0x50`两种写法

STM32标准外设库和 CMSIS 的中断设置接口通常接收未左移的逻辑值。例如概念上设置一个外设中断为逻辑优先级5时，应向库接口传5，库内部再左移为`0x50`写入 NVIC 寄存器。

而 V11.3.0 ARM_CM3 端口把 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 直接写入 `BASEPRI`，因此该宏必须使用已经左移的硬件格式。

概念区分：

```text
传给STM32/CMSIS优先级API：5
FreeRTOS写入BASEPRI的值：  0x50
```

若把`0x50`错误传给只接受0～15逻辑值的库接口，会超出该接口范围；若把未左移的5直接交给 `BASEPRI` 配置，STM32F103 未实现的低位不会形成期望屏蔽边界。

后续配置时可以定义一个便于阅读的逻辑边界宏，再明确左移得到内核实际使用值：

```text
逻辑边界：5
有效位数：4
硬件边界：5 << (8 - 4) = 0x50
```

当前只是理解计算，尚未把这些宏写入 `FreeRTOSConfig.h`。

### 19.4 抢占优先级与子优先级

STM32 NVIC 可以把4个有效位拆成：

```text
抢占优先级位
子优先级位
```

抢占优先级决定一个中断能否打断另一个中断；子优先级主要决定两个已挂起、且抢占级相同的中断谁先响应，不会产生彼此抢占。

当前 STM32 标准外设库定义：

```text
NVIC_PriorityGroup_4：4位抢占优先级，0位子优先级
```

FreeRTOS ARM_CM3 端口要求用于中断优先级的有效位全部作为抢占优先级，并在启用 `configASSERT` 时检查分组。这样 `BASEPRI` 边界、IRQ抢占关系和 FreeRTOS API 可调用范围可以使用同一个清晰数值表达。

本项目后续应使用与当前标准外设库匹配的：

```text
NVIC_PriorityGroup_4
```

优先级分组应在启动调度器前设置一次，不能由各个外设驱动随意改写。

### 19.5 `PRIMASK` 与 `BASEPRI` 的区别

Cortex-M3 提供不同的中断屏蔽机制。初学阶段需要区分：

```text
PRIMASK：屏蔽几乎所有可配置中断
BASEPRI：只屏蔽处于某个紧急程度阈值及以下的中断
```

FreeRTOS ARM_CM3 端口在内核临界区主要使用 `BASEPRI`，目的是：

- 防止能够访问 FreeRTOS 内核数据结构的中断在链表更新中途进入。
- 仍允许比 FreeRTOS API 边界更紧急的中断及时响应。

NMI 和 HardFault 等不可屏蔽或特殊异常不受普通 `BASEPRI` 阈值控制。

### 19.6 `BASEPRI=0x50` 到底屏蔽什么

假设后续选择逻辑边界5：

```text
configMAX_SYSCALL_INTERRUPT_PRIORITY = 0x50
```

当 FreeRTOS 进入临界区并写入：

```text
BASEPRI = 0x50
```

屏蔽关系为：

| 逻辑IRQ优先级 | 硬件值 | 临界区内是否被BASEPRI屏蔽 | 是否允许调用FreeRTOS API |
| --- | --- | --- | --- |
| 0～4 | `0x00`～`0x40` | 不屏蔽，仍可抢占 | 禁止调用任何FreeRTOS API，包括`FromISR` |
| 5～15 | `0x50`～`0xF0` | 屏蔽，等待临界区结束 | 可以调用对应的`FromISR` API |

这里的逻辑是：

```text
0～4非常紧急，FreeRTOS不延迟它们
        ↓
它们可能在内核链表修改到一半时进入
        ↓
因此它们绝不能访问FreeRTOS内核对象
```

```text
5～15允许访问FreeRTOS内核
        ↓
内核临界区会暂时屏蔽它们
        ↓
保证任务、队列等数据结构修改的原子性
```

                FreeRTOS TaskB
                 Thread Mode
                     │
                USART IRQ 到来
                     ↓
               Handler Mode
              USART_IRQHandler
                     │
                     │ xQueueSendFromISR()
                     ↓
               TaskA 被唤醒
            Blocked → Ready
                     │
                     │
       TaskA不能抢占USART ISR！
                     │
                     ↓
        xHigherPriorityTaskWoken
                = pdTRUE
                     │
                     ↓
          portYIELD_FROM_ISR()
                     │
                     ↓
             PendSV = Pending
                     │
                     ↓
           USART_IRQHandler结束
                     │
                     ↓
             PendSV_Handler
                     │
                任务切换
                     │
              TaskB → TaskA
                     │
                     ↓
                 TaskA
              Thread Mode

当 `BASEPRI=0` 时，表示没有通过 BASEPRI 屏蔽任何中断，不是“屏蔽最高优先级”。因此 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 不能配置为0；V11.3.0 端口启动时会通过断言检查这一点。

### 19.7 为什么高紧急程度中断不能调用`FromISR`（FromISR 的意思就是“从中断服务程序中调用”。其中 ISR = Interrupt Service Routine，即“中断服务程序”。所以 FreeRTOS 里函数名带 FromISR，基本就是在告诉你：这是专门给中断上下文使用的 API 版本）

假设内核正在把任务节点从 Delayed List 移到 Ready List，操作尚未完成。逻辑优先级2的中断不受 `BASEPRI=0x50` 屏蔽，可以立即进入。

如果这个中断又调用 FreeRTOS API 修改同一组内核链表，可能看到中间状态或破坏链表。

因此高紧急程度中断只能执行不依赖 FreeRTOS 内核的数据处理，例如：

- 清除硬件中断标志。
- 读取或写入必要寄存器。
- 更新经过严格设计的普通原子标志或无锁结构。
- 通过后续较低优先级机制把复杂处理延迟到任务。

不能因为函数名包含 `FromISR` 就从任意中断优先级调用它。`FromISR` 只表示该 API 的实现适合中断上下文，仍必须满足中断优先级边界。

### 19.8 普通API与`FromISR` API

Task Context 使用普通 API，例如以后会学习：

```text
xQueueSend()
xSemaphoreGive()
vTaskNotifyGive()
```

Handler Context 只能使用明确以 `FromISR` 结尾或文档声明为中断安全的 API，例如：

```text
xQueueSendFromISR()
xSemaphoreGiveFromISR()
vTaskNotifyGiveFromISR()
```

ISR 不允许像任务一样阻塞等待，因此 `FromISR` API 的参数和返回行为通常与普通 API 不同。它们常通过一个“是否唤醒了更高优先级任务”的输出参数通知端口：退出 ISR 前是否需要请求 PendSV。

典型概念流程：

```text
外设IRQ进入
    ↓
FromISR API使高优先级任务Ready
    ↓
设置HigherPriorityTaskWoken
    ↓
portYIELD_FROM_ISR()
    ↓
退出中断后PendSV切换任务
```

当前最小 Demo 尚未复制 `queue.c`，也不会在本阶段实现具体中断通信代码；这里先建立规则。

### 19.9 默认优先级0为什么危险

许多 Cortex-M 外设中断在没有配置时默认优先级为0，即最高紧急程度。

优先级0：

- 不会被非零 `BASEPRI` 屏蔽。
- 高于候选 FreeRTOS API 边界5。
- 不能调用 FreeRTOS `FromISR` API。

因此以后任何需要调用 FreeRTOS API 的外设中断，都必须显式配置到允许区间，不能依赖上电默认值。

V11.3.0 端口的 `vPortValidateInterruptPriority()` 会在启用 `configASSERT` 时读取当前中断优先级，并检查其硬件值是否大于等于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。如果一个优先级0 ISR 调用 FreeRTOS ISR API，断言应及时暴露错误。

### 19.10 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 名称为什么容易误解

名称中的“MAX”描述的是“允许调用内核 API 的最高紧急程度边界”，不是数值最大值。

若逻辑边界为5：

```text
逻辑优先级5：允许调用FromISR的最高紧急程度
逻辑优先级6～15：也允许调用FromISR，但紧急程度更低
逻辑优先级0～4：紧急程度更高，禁止调用FreeRTOS API
```

FreeRTOS 官方源码注释常说“priority above”，指的是逻辑紧急程度更高，也就是数值更小。阅读文档时最好同时写出数值范围，避免“高/低优先级”语言歧义。

### 19.11 当前 V11.3.0 ARM_CM3 端口的版本特性

当前选中的 `portable/GCC/ARM_CM3/port.c`：

- 使用常量255写 PendSV 和 SysTick 的优先级字段；STM32F103 只实现高4位，所以实际得到最低逻辑优先级15，即硬件值`0xF0`。
- 把 SVC 优先级寄存器设置为0，即最高紧急程度。
- 直接使用 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 设置 `BASEPRI`。
- 当前端口源码没有使用 `configKERNEL_INTERRUPT_PRIORITY` 来设置 PendSV/SysTick；该宏虽然出现在通用模板和历史资料中，但不能据此机械复制到本端口配置。
- 启用 `configASSERT` 后，会检查 SVC/PendSV Handler 安装、有效优先级位、API调用边界和优先级分组。

所以后续编写配置应以“当前 V11.3.0 ARM_CM3 端口实际引用了哪些宏”为依据，而不是照搬其他 FreeRTOS 版本、其他 Cortex-M 端口或通用模板的全部字段。

### 19.12 三层数值对照

候选边界5可以用三层心智模型记忆：

```text
人阅读的逻辑值：5
        ↓ 左移4位
硬件/BASEPRI值：0x50
        ↓ 形成边界
API规则：0～4禁止，5～15允许FromISR
```

PendSV和SysTick：

```text
逻辑值15
硬件值0xF0
紧急程度最低
```

SVC：

```text
逻辑值0
硬件值0x00
紧急程度最高
```

### 19.13 本课常见误区

1. Task优先级与IRQ优先级属于两套系统，不能直接比较。
2. Task优先级数值越大越高；IRQ优先级数值越小越紧急。
3. CMSIS/SPL接口通常使用未左移逻辑值；`BASEPRI` 使用硬件左移值。
4. 优先级0不是最低，而是最高紧急程度。
5. `FromISR` API不是任何ISR都能调用，ISR还必须位于允许优先级范围。
6. `BASEPRI=0`表示不屏蔽，不是最强屏蔽。
7. 高紧急程度ISR不受FreeRTOS临界区屏蔽，因此禁止访问内核对象。
8. FreeRTOS临界区不会关闭所有中断，逻辑优先级0～4仍可响应候选边界5。
9. 中断默认优先级0不能直接调用FreeRTOS API。
10. 当前 V11.3.0 ARM_CM3 端口不能机械套用旧版本的`configKERNEL_INTERRUPT_PRIORITY`示例。

### 19.14 本课自测

1. FreeRTOS Task Priority 4 和 Task Priority 1 谁更高？
2. Cortex-M IRQ Priority 4 和 IRQ Priority 1 谁更紧急？
3. 为什么再高的 Task Priority 也不能阻止一个已使能的普通IRQ抢占？
4. STM32F103 实现几位 NVIC 优先级？逻辑范围是多少？
5. 逻辑优先级5写入8位NVIC寄存器后为什么是`0x50`？
6. 若`BASEPRI=0x50`，逻辑优先级2和6分别是否会被屏蔽？
7. 为什么逻辑优先级0～4的ISR不能调用`FromISR` API？
8. 为什么逻辑优先级5～15的ISR只能调用`FromISR`版本，而不能调用可能阻塞的普通任务API？
9. `xTaskIncrementTick()`唤醒任务与外设ISR通过`FromISR` API唤醒任务，最终如何共同请求PendSV？
10. 为什么外设中断保持默认优先级0时调用FreeRTOS API很危险？
11. `NVIC_PriorityGroup_4`在当前标准外设库中表示什么？
12. 当前 V11.3.0 ARM_CM3 端口如何设置PendSV和SysTick的优先级？

### 19.15 第4课自测答案解析

#### 第1题：两套优先级的方向

用户回答：Task Priority 3 高于 Task Priority 1；IRQ Priority 1 比 IRQ Priority 3 更紧急。

回答正确：

```text
Task：数值越大，优先级越高
IRQ： 数值越小，紧急程度越高
```

#### 第2题：`BASEPRI=0x50`的屏蔽范围

用户回答：逻辑优先级2不被屏蔽，逻辑优先级6被屏蔽。

回答正确：

```text
IRQ 2 → 硬件值0x20 → 高于屏蔽边界，不屏蔽
IRQ 6 → 硬件值0x60 → 处于屏蔽范围，被屏蔽
```

#### 第3题：为什么IRQ 2不能调用`FromISR` API

用户回答：IRQ 2属于高紧急程度中断，不受 `BASEPRI` 屏蔽；若内核正在操作链表时它进入并调用 `FromISR` API，会造成链表异常。

回答正确。更广义地说，风险不限于 Ready/Delayed 链表，也包括队列、信号量、任务通知等受 FreeRTOS 临界区保护的内核对象。

V11.3.0 ARM_CM3 端口在启用 `configASSERT` 后，会在 ISR API 路径中通过 `vPortValidateInterruptPriority()` 检查当前外设中断优先级，帮助在调试阶段发现这种非法调用。

#### 第4题：IRQ 6应调用哪个队列API

用户回答：逻辑优先级6应调用 `xQueueSendFromISR()`。

回答正确。不能在 ISR 中调用可能具有阻塞语义的普通 `xQueueSend()`。完整的中断唤醒链路还包括：

```text
ISR调用xQueueSendFromISR()
        ↓
API通过xHigherPriorityTaskWoken告知是否唤醒了更高优先级任务
        ↓
ISR结束前调用portYIELD_FROM_ISR()
        ↓
需要切换时挂起PendSV
        ↓
退出当前中断后切换到被唤醒任务
```

`FromISR` API 自身不会像任务API那样阻塞等待空间或资源。

#### 第5题：默认优先级0为什么危险

用户回答与第3题原理相同。

回答正确，并补充默认配置这一层：外设IRQ若没有显式设置，通常保持逻辑优先级0，也就是最高紧急程度。它不受非零 `BASEPRI` 屏蔽，因此禁止调用 FreeRTOS API。需要使用 ISR API 的中断必须先显式配置到允许范围，不能依赖复位默认值。

#### 第4课学习结论

用户已能够正确区分：

- Task 与 IRQ 优先级的数值方向。
- 逻辑优先级与左移后的硬件值。
- `BASEPRI=0x50` 的屏蔽范围。
- 高紧急程度 ISR 禁止访问 FreeRTOS 内核的原因。
- 普通任务 API 与 `FromISR` API 的使用上下文。

后续进入 `FreeRTOSConfig.h` 时，仍需亲自完成逻辑边界到硬件值的计算，并通过 `configASSERT` 和实机中断验证规则是否正确。

## 20. 第5课前置：启动文件与链接脚本

### 20.1 先纠正一个容易产生的误解

不是“使用 Keil 就不需要启动文件和链接文件，使用 GCC 才需要”。任何裸机 MCU 程序都必须解决下面两个问题：

1. MCU 复位后从哪里取得初始栈顶和第一条指令？
2. 编译出来的代码、常量、全局变量、栈等分别放到 Flash 和 RAM 的什么位置？

Keil 工程同样需要这些信息，只是 Keil IDE、ARM 运行库、启动模板和链接器把一部分工作隐藏或代办了。当前工程不依赖 Keil IDE，使用 CMake、GNU Make 和 `arm-none-eabi-gcc`，所以把这些责任明确写在工程里：

- `Libraries/CMSIS/startup/gcc/startup_stm32f103xe.S`：GNU 汇编器使用的启动文件；
- `linker/STM32F103ZETx_FLASH.ld`：GNU `ld` 使用的链接脚本。

这里的重点不是从空白开始发明一套启动文件和链接脚本。正常做法是从芯片厂商、CMSIS、STM32Cube 或经过验证的官方工程取得与芯片和工具链匹配的模板，再读懂、核对和按需要调整。

### 20.2 从源文件到可烧录文件：它们分别在哪一步参与

当前工程的构建过程可以简化为：

```text
C 源文件（.c）
    │ 编译
    ▼
目标文件（.o） ───────────────┐
                              │
启动文件（.S）                │
    │ 预处理 + 汇编            │
    ▼                         │
启动目标文件（.o）─────────────┤
                              │
库文件（.a）──────────────────┤
                              ▼
                    GNU 链接器 arm-none-eabi-ld
                              ▲
                              │ 读取布局规则
                       链接脚本（.ld）
                              │
                              ▼
                       firmware.elf
                              │ objcopy
                              ├────────► firmware.hex
                              └────────► firmware.bin
```

#### 启动文件 `.S` 参与编译/汇编

启动文件本身是程序的一部分。它先被预处理和汇编，产生目标文件，然后与 C 源码产生的目标文件一起链接进最终 ELF。

扩展名大小写在 GNU 工具链中有意义：

- `.S`：先经过 C 预处理器，再交给汇编器，因此可以使用 `#include`、`#if`、宏等；
- `.s`：通常直接交给汇编器，不先运行 C 预处理器。

这不是说所有平台都必须如此，而是 GNU 工具链的常见约定。

#### 链接脚本 `.ld` 只在链接阶段被读取

`.ld` 不是 MCU 将来逐条执行的程序，也不会像 `.c` 一样被编译。链接器读取它，用它决定每一个输入段的最终地址，解析启动文件和 C 代码引用的符号，并生成 ELF。

因此可记成一句话：

```text
启动文件提供“上电后要执行什么”，链接脚本决定“这些内容最终放在哪里”。
```

### 20.3 MCU 运行时，谁先工作

构建顺序和 MCU 的运行顺序不能混为一谈。在构建时，启动文件先变成 `.o`，链接脚本最后参与链接；但在芯片运行时，`.ld` 不执行，启动文件里的向量表和 `Reset_Handler` 最先发挥作用。

STM32F103 从 Flash 启动时，可以把复位过程简化为：

```text
芯片复位
   │
   ├─ 读取向量表第 0 项 ──► 装入 MSP（主栈指针）
   │
   ├─ 读取向量表第 1 项 ──► 装入 PC（Reset_Handler 地址）
   │
   ▼
Reset_Handler
   │
   ├─ 调用 SystemInit()：配置最基础的芯片系统环境/时钟
   ├─ 把 .data 的初值从 Flash 复制到 RAM
   ├─ 把 .bss 对应的 RAM 清零
   └─ 调用 main()
```

以上顺序是当前 `startup_stm32f103xe.S` 的实际实现。`main()` 不是硬件认识的复位入口；是启动代码完成 C 语言运行环境的基本准备后主动调用它。

### 20.4 启动文件具体负责什么

当前启动文件主要包含四类内容。

#### 1. 中断向量表

向量表 `g_pfnVectors` 保存：

- 初始 MSP 值；
- `Reset_Handler` 地址；
- NMI、HardFault 等内核异常入口；
- 各个 STM32 外设中断入口；
- FreeRTOS 后面会用到的 SVC、PendSV、SysTick 入口。

向量表不是“中断处理函数本体”，而是一张入口地址表。发生异常时，Cortex-M3 根据异常编号到表中取出相应函数地址。

#### 2. `Reset_Handler`

`Reset_Handler` 是复位后的第一段软件入口。它负责调用 `SystemInit()`、初始化 `.data` 和 `.bss`，最后进入 `main()`。

#### 3. 默认中断处理函数

如果某个中断没有由用户提供真正的处理函数，它会通过弱别名落到 `Default_Handler`。默认处理通常停在死循环中，便于调试时暴露“发生了未实现中断”的错误。

#### 4. 弱符号

启动文件把多数中断入口声明为弱符号。用户代码或库只要提供同名的强符号，链接器就会选用强符号覆盖默认实现。

这与 FreeRTOS 直接相关：ARM_CM3 端口最终要接管 SVC、PendSV 和 SysTick。启动文件中的弱入口允许 FreeRTOS 端口提供的强实现替换默认处理函数，而不需要去删除向量表中的条目。

### 20.5 链接脚本具体负责什么

链接脚本描述 MCU 的内存地图以及各段的摆放规则。当前工程中的核心信息包括：

```text
FLASH：起始地址 0x08000000，长度 512 KiB
RAM：  起始地址 0x20000000，长度  64 KiB
初始栈顶 _estack：0x20010000
```

这些参数必须与具体芯片匹配。STM32F103ZET6 的 Flash/RAM 容量与另一个封装或容量等级的 STM32F103 不一定相同，不能只看“都是 F103”就照搬。

链接脚本还安排常见的输出段：

| 段 | 主要内容 | 通常运行位置 |
| --- | --- | --- |
| `.isr_vector` | 初始栈顶和异常/中断入口表 | Flash 起始位置 |
| `.text` | 程序机器指令 | Flash |
| `.rodata` | 字符串常量、只读常量 | Flash |
| `.data` | 有非零初值的全局/静态变量 | 运行在 RAM，初值存于 Flash |
| `.bss` | 未显式初始化或初值为 0 的全局/静态变量 | RAM，上电时清零 |
| 栈保留区 | 函数调用、局部变量、异常现场等 | RAM |

其中 `.data` 最值得理解。假设有：

```c
uint32_t counter = 123;
```

程序掉电后 RAM 不保存 `123`，所以这个初值必须存进 Flash；复位时启动代码再把它复制到链接器指定的 RAM 地址。于是 `.data` 同时涉及两个地址：

```text
装载地址 LMA：初值在 Flash 中的位置
运行地址 VMA：变量运行时在 RAM 中的位置
```

`.bss` 不需要在 Flash 中逐字节保存一大片 0，链接器只为它分配 RAM 范围，启动代码在复位时清零即可。因此当前脚本把它标成 `NOLOAD`。

### 20.6 启动文件与链接脚本是一份接口契约

当前启动文件引用了下列符号：

| 符号 | 由谁定义 | 启动代码如何使用 |
| --- | --- | --- |
| `_estack` | `.ld` | 作为向量表第 0 项，即初始 MSP |
| `_sidata` | `.ld` | `.data` 初值在 Flash 中的起点 |
| `_sdata` | `.ld` | `.data` 在 RAM 中的起点 |
| `_edata` | `.ld` | `.data` 在 RAM 中的终点 |
| `_sbss` | `.ld` | `.bss` 在 RAM 中的起点 |
| `_ebss` | `.ld` | `.bss` 在 RAM 中的终点 |

启动代码并不知道“RAM 到底有多大”或“.data 应放哪里”，它只使用这些符号；链接脚本并不亲自复制内存，它只计算并导出地址。两者共同完成 C 运行环境初始化。

因此更换其中一个模板时，不能只看文件能否通过编译，还要核对符号名称和含义是否一致。例如新链接脚本定义的是 `__data_start__`，而旧启动代码仍寻找 `_sdata`，链接就会报未定义符号；更危险的是名字存在但语义不一致，程序可能链接成功却在复位阶段破坏内存。

### 20.7 为什么 GNU `.ld` 看起来比 Keil 的链接配置多

首先要区分“功能更多”和“写得更显式”。当前 GNU 工程主要是后者。

Keil/ARM 工具链常见的分工是：

```text
Keil Target 设置或 scatter 文件（.sct）描述内存区域
启动文件调用 SystemInit()
启动文件跳转到 ARM 运行库的 __main
__main 的 scatter-loading 代码复制数据段、清零 ZI 段并进入用户 main()
```

GNU 裸机工程的常见分工则是：

```text
.ld 显式描述内存和 ELF 段
Reset_Handler 显式复制 .data、清零 .bss
Reset_Handler 直接调用 main()
```

当前链接选项还使用了 `-nostartfiles`，表示不采用工具链默认的启动文件，所以工程必须自己明确提供复位入口和最基本的运行时初始化。

当前 `.ld` 中一些看起来陌生的内容可这样理解：

- `ENTRY(Reset_Handler)`：告诉链接器程序入口符号；
- `MEMORY`：声明 Flash 和 RAM 的起点、长度及属性；
- `KEEP(*(.isr_vector))`：即使启用了 `--gc-sections`，也不允许把向量表当成“未引用内容”删除；
- `.ARM.extab`、`.ARM.exidx`：ARM 异常展开/回溯相关元数据；
- `.preinit_array`、`.init_array`、`.fini_array`：C/C++ 初始化和结束函数表，尤其与 C++ 全局对象构造有关；
- `AT> FLASH`：说明某段运行在 RAM，但其初始镜像装载于 Flash；
- `NOLOAD`：该段只占运行内存，不要求在固件镜像中保存逐字节初值；
- `._user_heap_stack`：为基础堆和 MSP 栈预留空间，并帮助链接阶段发现明显的 RAM 溢出；
- `/DISCARD/`：丢弃当前裸机目标不需要的输入段。

所以不要用文件行数判断复杂程度。不同工具链采用不同语法、运行库和责任划分，同样的工作可能在 Keil IDE、scatter 文件和 `__main` 运行库中被分散完成，而在 GNU 工程中集中展示出来。

### 20.8 当前工程需要知道的两个边界

#### C++ 或完整运行库初始化

当前启动代码初始化 `.data`、`.bss` 后直接调用 `main()`，没有看到对 `__libc_init_array()` 的调用。对当前以 C 为主的最小裸机/FreeRTOS 学习工程，这不妨碍现阶段目标；但如果以后引入依赖全局构造函数的 C++ 代码或某些完整 C 运行库功能，就必须重新核对启动流程，而不能假设现有模板自动完成全部初始化。

#### FreeRTOS 堆不等同于链接脚本预留的 C 堆

当前 `.ld` 中的 `_Min_Heap_Size` 是链接脚本为传统 C 运行库堆预留/检查的空间；选择 `heap_4.c` 后，FreeRTOS 的动态内存来自它自己的 `ucHeap[configTOTAL_HEAP_SIZE]` 数组。两者用途和管理者不同。

后续应通过 MAP 文件核对 RAM 占用，避免既给 C 堆预留过多空间，又给 FreeRTOS 堆设置过大，最终挤压 MSP、任务栈、全局变量和中断栈。当前阶段只理解边界，不修改这些数值。

### 20.9 为什么启动目录里有很多文件

`Libraries/CMSIS/startup/arm/` 下的多个文件并不是要全部加入工程，而是供应商为不同 STM32F1 产品线和 ARM/Keil 汇编器保留的候选模板。例如常见后缀表示：

| 后缀 | 大致含义 |
| --- | --- |
| `ld` | Low-density，低容量产品 |
| `md` | Medium-density，中容量产品 |
| `hd` | High-density，高容量产品 |
| `xl` | XL-density，超大容量产品 |
| `cl` | Connectivity line，互联型产品 |
| `vl` | Value line，基本型产品 |

不同产品线的外设数量和中断向量表不同，因此启动文件不能完全共用。工具链语法也不同：

- `startup/arm/*.s` 通常是 ARMASM/Keil 语法；
- `startup/gcc/*.S` 是 GNU assembler 使用的语法。

当前 CMake 只加入：

```text
Libraries/CMSIS/startup/gcc/startup_stm32f103xe.S
```

`STM32F103ZET6` 属于当前选择的高容量/xE 目标，工程只应编译一个与芯片及 GNU 工具链匹配的启动文件。若同时编译多个启动文件，通常会出现多个向量表、多个 `Reset_Handler` 等重复定义问题。

启动文件末尾还可能含有针对特定 STM32 系列启动方式的保留向量或签名字，不能因为暂时看不懂就随意删除。先确认其来源和数据手册含义，再决定是否调整。

### 20.10 需要学到什么程度

第一次移植 FreeRTOS 时，不需要背下全部 GNU linker script 语法，也不需要立即做到手写完整启动文件。目标应分三层。

#### 必须理解

1. MCU 复位时先从向量表装载 MSP 和 `Reset_Handler`。
2. `Reset_Handler` 为什么要准备 `.data`、`.bss` 后才能进入 `main()`。
3. Flash/RAM 起始地址和容量必须与芯片一致。
4. `.text`、`.rodata`、`.data`、`.bss` 各是什么。
5. `.data` 为什么“初值在 Flash、运行在 RAM”。
6. 启动文件使用的地址符号由链接脚本提供。
7. 工程中只能选择一个与 MCU、工具链匹配的启动文件。
8. 栈从高地址向低地址增长，错误的栈顶或 RAM 容量会直接造成故障。
9. FreeRTOS 如何通过强符号接管弱定义的 SVC、PendSV 和 SysTick。
10. 会看 ELF/MAP 的段地址、大小和 RAM/Flash 是否溢出。

#### 以后需要能够修改和验证

- 更换同系列不同容量芯片时调整 Flash/RAM 长度；
- 根据实际中断嵌套深度调整 MSP 预留，并根据任务需求配置 FreeRTOS 任务栈；
- 引入 Bootloader 时调整应用 Flash 起点、镜像地址和向量表位置；
- 更换芯片内核时同步选择新的 CMSIS 启动文件、编译参数和 FreeRTOS portable 端口；
- 使用 MAP、反汇编和 GDB 验证修改，而不是只凭“能链接”判断正确。

#### 不需要死记

- STM32F103 的所有中断名字和编号；
- `.ld` 的每一个语法细节；
- 启动汇编的每一条指令编码；
- 所有 ELF 元数据段的内部格式；
- FreeRTOS PendSV 汇编的全部细节。

正确的能力不是脱离资料默写，而是知道关键接口、知道去哪里查，并能检查模板是否适合当前目标。

### 20.11 以后换芯片或换工具链怎么办

不要在旧文件上盲目搜索替换芯片名。更稳妥的步骤是：

1. 从新芯片厂商/CMSIS 软件包取得与目标工具链匹配的启动文件和链接脚本模板。
2. 核对完整料号、内核类型、Flash、RAM、向量表和系统时钟文件。
3. 更新编译器 CPU 参数，例如 Cortex-M3、Cortex-M4，以及是否存在 FPU 和对应浮点 ABI。
4. 更新设备宏、CMSIS 设备头文件和 `system_*.c`。
5. 核对启动文件与链接脚本共享的符号名称和含义。
6. 若 CPU 内核改变，重新选择 FreeRTOS `portable` 端口；不能把 ARM_CM3 端口直接当成所有 Cortex-M 的通用端口。
7. 先恢复最小裸机基线：编译、烧录、LED、GDB 和异常入口。
8. 再接入 FreeRTOS，验证 SVC、PendSV、SysTick、任务栈和内存占用。

可按变化范围判断哪些文件可能保持不变：

```text
只换开发板、MCU型号不变
    └─ 启动文件和.ld通常可保持；引脚、时钟、电路可能变化

同一内核、只换Flash/RAM容量
    └─ FreeRTOS端口通常可保持；.ld和可能的向量表型号需要核对

Cortex-M3换成Cortex-M4F
    └─ CPU/FPU参数、CMSIS、启动文件、.ld、FreeRTOS端口都要重新核对

以后加入Bootloader
    └─ 应用Flash起点、烧录地址、向量表/VTOR、镜像大小边界都要调整
```

### 20.12 本课对当前项目的结论

当前工程已经通过真实硬件完成裸机 LED、烧录和 GDB 基线验证，说明现有启动文件、链接脚本、CMake 和芯片基础配置在这个裸机用例上能够协同工作。它不能自动证明未来所有 FreeRTOS 堆栈大小、中断优先级和内存配置都正确，但它为下一步提供了可信基线。

接下来编写 `FreeRTOSConfig.h` 时，不需要改写启动文件或链接脚本。先保持这条已验证链路不动，只让最小 FreeRTOS 内核加入构建；待生成 ELF/MAP 后，再观察内核对象、FreeRTOS 堆、任务栈以及 SVC/PendSV/SysTick 符号是否符合预期。

### 20.13 本课自测

1. `.S` 和 `.ld` 分别在哪一个构建阶段参与？它们都会变成 MCU 执行的机器指令吗？
2. 芯片复位时，向量表前两个 32 位值分别是什么？
3. 为什么有初值的全局变量需要同时涉及 Flash 地址和 RAM 地址？
4. `.bss` 为什么不需要在固件中保存一大片 0？
5. `_sidata`、`_sdata` 和 `_edata` 是谁定义、谁使用的？
6. 为什么 Keil 工程看起来可以少写一些数据段初始化代码？
7. 为什么当前工程不能把 `startup/arm/` 下的所有 `.s` 都加入 CMake？
8. FreeRTOS 为什么可以接管启动向量表中的 SVC、PendSV 和 SysTick？
9. `heap_4.c` 的 FreeRTOS 堆与 `.ld` 中的 `_Min_Heap_Size` 是同一个堆吗？
10. 如果以后加入 Bootloader，至少需要重新核对哪三类地址或配置？

## 21. 第5课：设计 `FreeRTOSConfig.h`

### 21.1 本课目标和第一阶段边界

这一课的目标不是从网上复制一份能够编译的配置文件，而是建立下面这条推导链：

```text
芯片和工程事实
    ↓
选择调度、时间、内存和中断策略
    ↓
把策略写成FreeRTOSConfig.h中的编译期宏
    ↓
FreeRTOS.h读取并检查这些宏
    ↓
tasks.c、heap_4.c和ARM_CM3端口按配置参与编译
    ↓
用编译、ELF/MAP、GDB和实机现象验证
```

本章记录最小LED Demo设计时的历史阶段。本章中的“当前”和“首版”均指当时；仓库后续完成情况见第15章以及第26～29章。

本课当时只完成“理解和设计”。`User/FreeRTOSConfig.h` 仍为0字节，尚未由用户填写；FreeRTOS源码也还没有加入CMake，因此本章出现的数值都必须区分为：

- 已确认的硬件事实；
- 依据事实推导出的首版候选配置；
- 仍需构建、MAP、GDB 或实机验证的参数。

Codex 不修改配置文件。用户完成本课自测后，再亲自按分组逐段写入。

### 21.2 `FreeRTOSConfig.h` 不是运行时配置表

`FreeRTOSConfig.h` 主要由 C 预处理宏组成。它不是程序启动后读取的一张参数表，而是在编译阶段改变内核源码的内容。

典型包含链为：

```text
tasks.c / list.c / heap_4.c / port.c
                 │
                 └─ include "FreeRTOS.h"
                               │
                               ├─ include "FreeRTOSConfig.h"
                               ├─ 检查必需宏
                               └─ include "portable.h"
                                             │
                                             └─ ARM_CM3/portmacro.h
```

例如：

```c
#define INCLUDE_vTaskDelay 1
```

会使 `tasks.c` 中 `#if ( INCLUDE_vTaskDelay == 1 )` 包围的 `vTaskDelay()` 实现被编译。若写成0，该函数实现不会进入目标文件，应用再调用它就会在链接阶段出现未定义引用。

再例如：

```c
#define configMAX_PRIORITIES 5
```

不只是保存数字5。`tasks.c` 会据此创建5个 Ready List，并用它检查任务优先级范围。所以修改配置后必须重新编译内核，不能在运行中改变这类结构。

### 21.3 为什么不能直接复制官方通用模板

完整上游快照提供：

```text
examples/template_configuration/FreeRTOSConfig.h
```

它是一份覆盖许多 CPU、MPU、SMP、TrustZone、静态/动态分配和内核模块的说明模板，不是针对 STM32F103ZET6 的完成品。当前源码中至少能看到以下不匹配：

| 通用模板示例 | 当前 ARM_CM3 工程事实 | 结论 |
| --- | --- | --- |
| CPU 时钟示例为20 MHz | 当前 `SystemCoreClock` 为72 MHz | 必须按当前系统时钟配置 |
| Tick 示例为100 Hz | 本项目首版候选为1000 Hz | 必须根据时间分辨率和开销选择 |
| Tick 类型示例为64位 | ARM_CM3 `portmacro.h` 只接受16或32位 | 当前必须选择32位 |
| 最大系统调用中断优先级示例为0 | `BASEPRI=0` 表示不屏蔽任何中断，当前端口断言要求非0 | 当前候选边界是逻辑5，即硬件值 `0x50` |
| 软件定时器、事件组、流缓冲等可能启用 | 相应 `.c` 尚未复制/加入最小内核 | 最小 Demo 必须关闭或不使用 |
| 静态和动态分配都可能启用 | 当前选择 `heap_4.c` 和动态创建任务 | 首版只启用动态分配 |

因此正确用法是：把模板当作宏的解释手册，再根据当前端口源码和工程目标选取最小子集。

### 21.4 配置文件的基本外壳

配置头文件也需要 include guard，防止一个编译单元通过不同包含链重复处理：

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* 配置宏按功能分组写在这里。 */

#endif /* FREERTOS_CONFIG_H */
```

宏按“硬件与时间、调度、内存、中断、调试、模块/API裁剪”分组，比照着官方模板从上到下复制更容易检查。

### 21.5 第一组：CPU 时钟与 SysTick

#### 21.5.1 `configCPU_CLOCK_HZ`

当前工程已经确认：

```text
SystemInit()把系统配置到72 MHz
system_stm32f10x.c维护SystemCoreClock
裸机LED和GDB基线已经通过实机验证
```

首版优先让 `configCPU_CLOCK_HZ` 引用 CMSIS 的 `SystemCoreClock`，而不是在多个文件分别维护 `72000000`：

```c
#include "system_stm32f10x.h"

#define configCPU_CLOCK_HZ    ( SystemCoreClock )
```

这样做要求 CMake 后续为 FreeRTOS 源文件提供 CMSIS 头文件搜索路径。当前工程本来就有 `Libraries/CMSIS`，但 FreeRTOS 尚未加入 CMake，后续接入时仍需检查实际编译命令。

`SystemCoreClock` 是一个变量，不意味着 FreeRTOS 会持续自动跟踪运行中的时钟变化。ARM_CM3 端口在启动调度器时根据它设置 SysTick；若以后运行中改变主频，必须同时更新 `SystemCoreClock` 并重新配置 Tick 时基。

#### 21.5.2 `configTICK_RATE_HZ`

首版候选：

```text
configTICK_RATE_HZ = 1000 Hz
```

含义是每秒产生1000次 Tick：

```text
1 Tick = 1 / 1000 s = 1 ms
```

当前端口在未单独定义 `configSYSTICK_CLOCK_HZ` 时自动令：

```text
configSYSTICK_CLOCK_HZ = configCPU_CLOCK_HZ = 72,000,000 Hz
```

SysTick 重装值为：

```text
LOAD = 72,000,000 / 1,000 - 1
     = 71,999
```

SysTick 是24位向下计数器，最大重装值为 `0xFFFFFF`，即16,777,215。71,999远小于上限，因此合法。

1 kHz 并非唯一正确答案。它便于学习时把 Tick 与毫秒直接对应，但每秒会进入1000次 Tick 中断。产品后期若不需要1 ms分辨率，可评估100 Hz等更低频率；当前阶段先追求清晰、可验证的最小闭环。

#### 21.5.3 当前裸机 SysTick 冲突

当前 `User/main.c` 的 `delay_ms()` 直接写入：

```text
SysTick->LOAD
SysTick->VAL
SysTick->CTRL
```

并通过轮询 `COUNTFLAG` 完成裸机延时。FreeRTOS 调度器启动时也会配置同一个 SysTick，并打开其中断。因此调度器启动后不能继续调用这个裸机 `delay_ms()`：

```text
裸机阶段：delay_ms()拥有SysTick
RTOS阶段：FreeRTOS端口拥有SysTick，任务使用vTaskDelay()
```

后续改入口程序时由用户删除或停止使用裸机延时；本课不修改 `main.c`。

### 21.6 第二组：Tick 类型宽度

V11.3.0 要求下面两个宏恰好定义一个：

```text
新式：configTICK_TYPE_WIDTH_IN_BITS
旧式兼容：configUSE_16_BIT_TICKS
```

两者都不定义会触发 `#error`，两者同时定义也会触发 `#error`。新工程使用当前接口：

```c
#define configTICK_TYPE_WIDTH_IN_BITS    TICK_TYPE_WIDTH_32_BITS
```

ARM_CM3 端口的 `portmacro.h` 只实现：

- `TICK_TYPE_WIDTH_16_BITS` → `uint16_t TickType_t`；
- `TICK_TYPE_WIDTH_32_BITS` → `uint32_t TickType_t`。

通用模板里出现的64位选项在当前端口会触发编译错误，不能因为“位数越大越好”而照搬。

在1 kHz Tick下：

```text
16位Tick回绕周期 = 65,536 / 1,000 ≈ 65.536秒
32位Tick回绕周期 = 4,294,967,296 / 1,000
                 ≈ 49.71天
```

Tick 回绕本身不是系统只能运行49.71天。FreeRTOS 使用溢出延时链表和无符号数规则处理回绕；它说明 Tick 计数器会重新从0开始，不是调度器停止运行。

### 21.7 第三组：调度策略

首版候选为：

| 宏 | 候选值 | 含义 |
| --- | ---: | --- |
| `configUSE_PREEMPTION` | 1 | 高优先级任务就绪时允许抢占低优先级任务 |
| `configUSE_TIME_SLICING` | 1 | 同优先级 Ready 任务可以随 Tick 轮转 |
| `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 1 | Cortex-M3 使用 `CLZ` 指令和位图寻找最高就绪优先级 |
| `configMAX_PRIORITIES` | 5 | 可用任务优先级为0、1、2、3、4 |
| `configIDLE_SHOULD_YIELD` | 1 | Idle Task 可让出 CPU 给同为优先级0的应用任务 |
| `configUSE_TICKLESS_IDLE` | 0 | 首版保持周期 Tick，不引入低功耗 Tickless 逻辑 |

ARM_CM3 端口默认启用优化任务选择。启用后 `configMAX_PRIORITIES` 不能超过32，因为32位位图的每一位表示一个优先级。当前选择5级远低于上限。

这5级不是“系统只能创建5个任务”。它表示有5种优先级，同一个优先级可以有多个任务。

### 21.8 第四组：动态分配、RTOS Heap 与栈

#### 21.8.1 动态和静态分配

当前最小 Demo 计划使用：

```text
xTaskCreate() + heap_4.c
```

所以首版候选：

```c
#define configSUPPORT_DYNAMIC_ALLOCATION    1
#define configSUPPORT_STATIC_ALLOCATION     0
```

两者不能同时为0，否则内核没有任何对象创建方式并触发配置错误。它们可以同时为1，但那会扩大第一版要学习和验证的范围。

#### 21.8.2 `configTOTAL_HEAP_SIZE`

`heap_4.c` 内部默认定义：

```c
static uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
```

因此该宏单位是字节，并会使 `.bss` 增加相应大小。首版可先采用8 KiB作为候选：

```text
configTOTAL_HEAP_SIZE = 8 × 1024字节
```

选择8 KiB的目的不是宣称它是最终最优值，而是为一个 LED Task、Idle Task、两个TCB和分配器元数据提供容易观察的余量。最终必须检查：

- 链接器输出的总 RAM；
- MAP 中 `ucHeap` 的大小和地址；
- `xPortGetFreeHeapSize()` 与最小剩余堆；
- 任务栈高水位；
- 后续每增加任务/队列后的变化。

8 KiB只占64 KiB SRAM的一部分。它不能与链接脚本中的 `_Min_Heap_Size=0x200` 混为一谈：前者由 `heap_4.c` 管理，后者是链接脚本为传统 C 运行库堆预留的区域。

#### 21.8.3 `configMINIMAL_STACK_SIZE`

首版候选：

```c
#define configMINIMAL_STACK_SIZE    128U
```

ARM_CM3 的 `StackType_t` 是32位，所以单位是“字”而不是字节：

```text
128 words × 4 bytes/word = 512 bytes
```

这个宏主要决定 Idle Task 的栈深度，并不自动规定 LED Task 的栈。以后调用 `xTaskCreate()` 时还要给 LED Task 单独传入栈深度。

512字节是首版候选，不是未经验证的永久结论。后续启用栈溢出检测，并用高水位 API/调试器观察实际余量后再调整。

### 21.9 第五组：Cortex-M3 中断边界

#### 21.9.1 逻辑优先级到硬件值

STM32F103 实现4个 NVIC 优先级位：

```text
__NVIC_PRIO_BITS = 4
```

上一课选用逻辑边界5时：

```text
左移位数 = 8 - 4 = 4
硬件值   = 5 << 4 = 0x50
```

所以当前端口真正需要的候选宏是：

```c
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x50U
```

其含义仍然是：

```text
逻辑IRQ 0～4：不受FreeRTOS临界区BASEPRI屏蔽，禁止调用FreeRTOS API
逻辑IRQ 5～15：可以调用FromISR API，并会受内核临界区屏蔽
```

不能写0。`BASEPRI=0` 表示不屏蔽任何中断；当前 V11.3.0 ARM_CM3 端口启用断言后还会确认最大系统调用优先级在硬件有效位掩码后非0。

#### 21.9.2 为什么本端口不照抄 `configKERNEL_INTERRUPT_PRIORITY`

当前 `portable/GCC/ARM_CM3/port.c` 没有读取 `configKERNEL_INTERRUPT_PRIORITY`，而是直接：

- 把 PendSV 和 SysTick 的8位优先级字段写成255，STM32F103实际保留高4位后得到 `0xF0`，即逻辑15；
- 把 SVC 优先级设置为0。

所以不能看到旧教程或通用模板中的该宏就认为当前端口必然使用它。配置依据应是本项目固定版本的源码。

#### 21.9.3 NVIC 优先级分组的潜在冲突

FreeRTOS Cortex-M 规则要求优先级位全部用于抢占优先级，不能让子优先级破坏 `BASEPRI` 边界解释。使用当前标准外设库时对应：

```text
NVIC_PriorityGroup_4：4位抢占优先级，0位子优先级
```

工作区中的 `User/exti/bsp_exti.c` 目前写有 `NVIC_PriorityGroup_1`。该文件尚未加入当前 CMake，因此不影响裸机基线；但以后启用 EXTI 或其他外设中断时必须先统一分组，不能让各个驱动重复改写 AIRCR 分组。本课只记录风险，不修改该文件。

### 21.10 第六组：把端口处理函数接到向量表

启动文件中的向量名称是：

```text
SVC_Handler
PendSV_Handler
SysTick_Handler
```

FreeRTOS ARM_CM3 端口源码中的函数名称是：

```text
vPortSVCHandler
xPortPendSVHandler
xPortSysTickHandler
```

首版采用直接路由，通过预处理宏把端口函数名映射成启动文件需要的强符号：

```c
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler
```

以 SVC 为例，预处理后 `port.c` 中：

```c
void vPortSVCHandler(void)
```

相当于变成：

```c
void SVC_Handler(void)
```

它会覆盖启动文件中的弱 `SVC_Handler`。PendSV 和 SysTick 同理。

再显式启用：

```c
#define configCHECK_HANDLER_INSTALLATION    1
```

调度器启动时会读取 VTOR 指向的向量表，检查 SVC 和 PendSV 表项是否确实指向 FreeRTOS 端口函数。该检查依赖有效的 `configASSERT()`。

### 21.11 第七组：断言、Hook 与故障可观察性

#### 21.11.1 `configASSERT`

学习移植阶段必须启用断言。没有断言时，一些非法中断优先级、错误向量安装和内核参数问题可能表现为难以定位的随机异常。

首版断言行为应做到：

```text
条件成立：继续运行
条件失败：停止调度相关中断并停在固定位置，便于GDB查看调用栈
```

可以采用官方模板的 `taskDISABLE_INTERRUPTS()` 加死循环形式。定义宏时要使用 `do { ... } while (0)` 或等价安全结构，避免多语句宏与 `if/else` 组合时产生语法歧义。

#### 21.11.2 栈溢出检测

首版候选：

```c
#define configCHECK_FOR_STACK_OVERFLOW    2
```

模式2会检查任务栈边界处预填充的模式是否被覆盖，比只检查栈指针越界的模式1更容易发现常见问题，但仍不能保证捕获所有瞬时越界或任意内存破坏。

启用后，应用必须在后续入口改造阶段提供：

```c
vApplicationStackOverflowHook(...)
```

否则链接时会缺少函数实现。

#### 21.11.3 动态分配失败 Hook

首版候选：

```c
#define configUSE_MALLOC_FAILED_HOOK    1
```

使用 `xTaskCreate()` 时，如果 `heap_4.c` 无法为 TCB 或任务栈分配内存，Hook 能让错误停在可调试位置。启用后应用必须提供：

```c
vApplicationMallocFailedHook(void)
```

#### 21.11.4 暂时关闭的 Hook

最小 Demo 不需要在每个 Tick 或 Idle 循环中执行应用回调：

```c
#define configUSE_IDLE_HOOK    0
#define configUSE_TICK_HOOK    0
```

以后有明确用途再开启；开启任意 Hook 都意味着应用必须提供对应函数，并遵守其上下文限制。

### 21.12 第八组：只保留最小 Demo 需要的功能

当前只准备了 `tasks.c`、`list.c`、ARM_CM3 端口和 `heap_4.c`，所以首版配置范围应保持一致：

| 功能 | 首版候选 | 原因 |
| --- | ---: | --- |
| 软件定时器 | 0 | 未加入 `timers.c`，最小 LED Task 不需要 |
| 队列/互斥量/信号量 | 0或不使用 | 未加入 `queue.c` |
| 事件组 | 0 | 未加入 `event_groups.c` |
| 流/消息缓冲区 | 0 | 未加入 `stream_buffer.c` |
| Co-routine | 0 | 未加入 `croutine.c`，项目使用普通 Task |
| Task Notification | 首版可关闭 | LED Task 不需要，后续有具体同步场景再启用 |
| Tickless Idle | 0 | 暂不引入低功耗时基补偿 |
| 运行时间统计 | 0 | 尚未提供独立统计计时器 |
| Trace/格式化统计 | 0 | 先控制依赖和内存占用 |
| Newlib 每任务重入 | 0 | 暂不为每个任务分配 `_reent` 结构 |

API 裁剪中必须开启：

```c
#define INCLUDE_vTaskDelay    1
```

因为最小 LED Task 要用 `vTaskDelay()` 进入 Blocked 状态。其他 `INCLUDE_*` API 当前可以保持0或使用 V11.3.0 默认值，等真实需求出现时再启用。

### 21.13 首版配置决策表

下面是本课得出的“待用户填写、待构建验证”的设计表，不代表文件已经完成：

| 类别 | 宏 | 首版候选 | 依据/待验证项 |
| --- | --- | ---: | --- |
| 时钟 | `configCPU_CLOCK_HZ` | `SystemCoreClock` | 已确认72 MHz；需保证声明和头文件路径可见 |
| Tick | `configTICK_RATE_HZ` | 1000 | 1 ms/Tick；实机检查 Tick 周期 |
| Tick类型 | `configTICK_TYPE_WIDTH_IN_BITS` | 32位 | ARM_CM3支持，1 kHz约49.71天回绕 |
| 调度 | `configUSE_PREEMPTION` | 1 | 使用抢占式调度 |
| 调度 | `configUSE_TIME_SLICING` | 1 | 同优先级任务允许轮转 |
| 调度 | `configUSE_PORT_OPTIMISED_TASK_SELECTION` | 1 | Cortex-M3 `CLZ` 优化 |
| 优先级 | `configMAX_PRIORITIES` | 5 | 任务优先级0～4；不等于任务数量 |
| Idle栈 | `configMINIMAL_STACK_SIZE` | 128 words | 512字节；待高水位验证 |
| 分配 | 动态/静态 | 1/0 | `xTaskCreate()` + `heap_4.c` |
| RTOS堆 | `configTOTAL_HEAP_SIZE` | 8 KiB | 候选值；待MAP和剩余堆验证 |
| 中断边界 | `configMAX_SYSCALL_INTERRUPT_PRIORITY` | `0x50` | 逻辑5左移4位 |
| 向量 | 三个 Handler 映射 | 直接路由 | 与当前启动文件弱符号匹配 |
| 向量检查 | `configCHECK_HANDLER_INSTALLATION` | 1 | 启动调度器时检查SVC/PendSV |
| 断言 | `configASSERT` | 启用 | GDB中保留故障现场 |
| 栈检查 | `configCHECK_FOR_STACK_OVERFLOW` | 2 | 需后续实现Stack Overflow Hook |
| 堆失败 | `configUSE_MALLOC_FAILED_HOOK` | 1 | 需后续实现Malloc Failed Hook |
| Task延时 | `INCLUDE_vTaskDelay` | 1 | LED Task必需 |
| 其他模块 | timers/queue/events/stream等 | 0/不使用 | 不超出最小内核范围 |

### 21.14 配置完成后要怎样证明正确

“头文件没有红线”不是验收。后续由用户完成配置和 CMake 接入后，至少依次检查：

1. 预处理/编译没有缺失宏或不兼容 Tick 类型错误。
2. 链接没有缺少 Hook、Handler 或内核函数。
3. `firmware.map` 中出现 `ucHeap`，大小与 `configTOTAL_HEAP_SIZE` 一致。
4. ELF 中只有一个 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` 强实现，并来自 FreeRTOS 端口。
5. SysTick `LOAD` 实际为71,999，Tick 周期约1 ms。
6. PendSV/SysTick 逻辑优先级为15，SVC为0。
7. `configMAX_SYSCALL_INTERRUPT_PRIORITY` 进入 PendSV/临界区路径的立即数为 `0x50`。
8. `vTaskStartScheduler()` 不返回，LED Task 与 Idle Task 按预期运行。
9. `vTaskDelay()` 期间 LED Task 位于 Blocked 链表，Tick 到期后重新就绪。
10. 故意构造的非法优先级、堆不足或小栈测试能进入对应断言/Hook；这些故障注入要在正常 Demo 稳定后单独执行。

### 21.15 本课常见误区

1. `FreeRTOSConfig.h` 不是官方内核自带的固定答案，而是应用与内核/端口的编译期契约。
2. 官方通用模板中的数值是说明性占位或跨平台示例，不能整份复制。
3. `configMAX_PRIORITIES=5` 表示5种优先级，不是最多5个任务。
4. `configMINIMAL_STACK_SIZE=128` 在 Cortex-M3 上是512字节，不是128字节。
5. `configTOTAL_HEAP_SIZE` 的单位是字节，并且不是 MCU 全部 SRAM。
6. 32位 Tick 约49.71天回绕不表示系统运行49.71天就停止。
7. `configMAX_SYSCALL_INTERRUPT_PRIORITY` 要写硬件左移值 `0x50`，不能直接写逻辑值5。
8. 最大系统调用优先级不能写0；`BASEPRI=0` 表示取消屏蔽。
9. Handler 名字不一致时，即使三个端口函数成功编译，向量表也可能仍指向默认死循环。
10. 启用 Hook 但不提供函数实现，会在链接阶段失败。
11. FreeRTOS 接管 SysTick 后，裸机 `delay_ms()` 不能再改写 SysTick 寄存器。
12. 各外设驱动不能各自重复修改 NVIC 优先级分组。

### 21.16 本课自测

1. 为什么说 `FreeRTOSConfig.h` 是编译期契约，而不是运行时配置表？请用 `INCLUDE_vTaskDelay` 举例。
2. 当前主频72 MHz、Tick为1000 Hz时，SysTick `LOAD` 应是多少？一个 Tick 是多少毫秒？
3. 为什么当前 ARM_CM3 端口不能照抄通用模板中的64位 Tick？在1 kHz下32位 Tick 大约多久回绕？回绕是否表示系统停止？
4. `configMAX_PRIORITIES=5` 时有哪些合法任务优先级？它是否限制任务总数为5？
5. `configMINIMAL_STACK_SIZE=128` 在当前 MCU 上是多少字节？它主要给哪个任务使用？
6. 为什么 `configTOTAL_HEAP_SIZE=8 KiB` 只能称为候选值？至少要用哪三类信息验证它？
7. 逻辑中断优先级5为什么在 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 中要写成 `0x50`？逻辑优先级2和6谁可以调用 `FromISR` API？
8. 三个 Handler 映射宏做了什么？如果不映射，启动文件中的弱入口可能发生什么？
9. 启用 `configCHECK_FOR_STACK_OVERFLOW=2` 和 `configUSE_MALLOC_FAILED_HOOK=1` 后，应用还必须补充哪两个 Hook？
10. 当前 `delay_ms()` 与 FreeRTOS 为什么会争用 SysTick？进入 RTOS 后任务应改用什么方式延时？
11. 当前未加入 CMake 的 EXTI 文件使用 `NVIC_PriorityGroup_1`，以后启用它前为什么必须统一改为满足 FreeRTOS 的分组？
12. 为什么当前版本不能机械照抄旧教程中的 `configKERNEL_INTERRUPT_PRIORITY`，而应先检查所选 `port.c` 是否引用它？

### 21.17 实操第1步：文件外壳、CPU时钟和Tick

配置文件先建立下面这组时钟与Tick基础内容：

```c
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "system_stm32f10x.h"

#define configCPU_CLOCK_HZ                ( SystemCoreClock )
#define configTICK_RATE_HZ                 1000U
#define configTICK_TYPE_WIDTH_IN_BITS      TICK_TYPE_WIDTH_32_BITS

#endif /* FREERTOS_CONFIG_H */
```

逐行含义：

| 内容 | 作用 |
| --- | --- |
| `FREERTOS_CONFIG_H` include guard | 防止同一编译单元重复处理配置头文件 |
| `<stdint.h>` | 提供 `uint32_t` 等固定宽度整数类型，使后面的CMSIS系统头文件具备完整类型依赖 |
| `system_stm32f10x.h` | 声明由 `system_stm32f10x.c` 定义的 `SystemCoreClock` |
| `configCPU_CLOCK_HZ` | 让FreeRTOS端口取得调度器启动时的内核时钟频率，当前实际值应为72 MHz |
| `configTICK_RATE_HZ` | 请求每秒1000个RTOS Tick，即1 ms/Tick |
| `configTICK_TYPE_WIDTH_IN_BITS` | 选择ARM_CM3端口支持的32位 `TickType_t` |

`SystemCoreClock` 的声明与定义关系为：

```text
system_stm32f10x.h：extern uint32_t SystemCoreClock;
        │ 只声明“变量存在于别处”
        ▼
system_stm32f10x.c：uint32_t SystemCoreClock = ...;
        │ 真正分配存储并由系统时钟配置维护
        ▼
FreeRTOSConfig.h：configCPU_CLOCK_HZ使用该变量
```

本步骤暂时不运行完整构建。原因不是这三行错误，而是 V11.3.0 还强制要求 `configMINIMAL_STACK_SIZE`、`configMAX_PRIORITIES`、`configUSE_PREEMPTION`、`configUSE_IDLE_HOOK`、`configUSE_TICK_HOOK` 等后续分组；同时 FreeRTOS 源文件尚未加入 CMake。

用户填写后先进行人工检查：

1. include guard 首尾名称完全相同。
2. 使用半角英文双引号、括号和空格，没有中文标点。
3. 只定义 `configTICK_TYPE_WIDTH_IN_BITS`，没有同时定义旧式 `configUSE_16_BIT_TICKS`。
4. Tick频率写的是1000 Hz，而不是把重装值71,999误写成 Tick频率。
5. `FreeRTOSConfig.h` 只引用 `SystemCoreClock`，不定义第二个同名变量。
6. 文件放在 `User/`，后续通过CMake的 `User` include路径被内核找到。

当前工程最终采用上述时钟与Tick配置，并通过完整构建验证。末尾使用普通 `#endif` 在语法上正确，也可以补充 `/* FREERTOS_CONFIG_H */` 注释提高可读性。

### 21.18 实操第2步：抢占、时间片和任务优先级

本步骤由用户在三个 Tick 配置宏之后、文件末尾 `#endif` 之前追加：

```c
/* Scheduler configuration. */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configMAX_PRIORITIES                      5U
#define configMAX_TASK_NAME_LEN                   16U
#define configIDLE_SHOULD_YIELD                   1
#define configUSE_TICKLESS_IDLE                   0
```

#### `configUSE_PREEMPTION`

设为1表示使用抢占式调度。若低优先级任务正在运行，而高优先级任务因为Tick、队列、中断通知等原因变为Ready，调度器可以请求PendSV并切换到高优先级任务。

设为0则是协作式调度。此时高优先级任务就绪并不一定马上运行，当前任务需要主动调用会引发调度的API。当前学习目标是掌握FreeRTOS常用的抢占式模型，因此选择1。

#### `configUSE_TIME_SLICING`

设为1表示多个同优先级任务同时Ready时，可以在Tick到来时轮流运行。它不能让Blocked任务运行，也不会让低优先级任务越过高优先级任务：

```text
先选择当前最高的Ready优先级
        ↓
若该优先级有多个Ready任务，再在它们之间时间片轮转
```

首版只有一个LED Task和Idle Task，通常不会立刻观察到同优先级轮转，但先保持常用行为；以后可以创建两个同优先级实验任务验证。

#### `configUSE_PORT_OPTIMISED_TASK_SELECTION`

设为1后，ARM_CM3端口使用32位Ready优先级位图和Cortex-M3的 `CLZ`（Count Leading Zeros，统计前导零）指令快速找到最高Ready优先级。

概念示例：

```text
Ready优先级：0、2、4
位图：        bit0、bit2、bit4为1
最高置位：    bit4
下一任务：    从优先级4的Ready List中选择
```

该优化要求 `configMAX_PRIORITIES <= 32`。当前值5满足要求。即使不显式定义，本 ARM_CM3 `portmacro.h` 也默认选择1；这里显式写出是为了让当前工程的调度策略可见，避免只依赖隐藏默认值。

#### `configMAX_PRIORITIES`

设为5表示创建5条Ready List，对应合法任务优先级：

```text
0、1、2、3、4
```

它限制的是优先级种类，不是任务数量。可以创建多个同优先级任务。Idle Task固定为优先级0；后续LED Task候选使用 `tskIDLE_PRIORITY + 1`，即优先级1。

优先级数量不是越多越好。过多等级会增加设计复杂度，也容易用优先级掩盖同步关系。当前最小Demo使用5级，为后续基础任务留出空间，但每个任务的实际优先级仍要根据阻塞关系和响应要求设计。

#### `configMAX_TASK_NAME_LEN`

设为16表示每个TCB中的任务名数组最多16个字符，并且包含字符串结尾的 `\0`。因此真正可见的最长任务名通常是15个字符。

任务名主要用于GDB、内核感知调试器、Trace和诊断，不决定调度行为。这个数组存在于每个TCB中，名字长度增加会让每个TCB都相应增大。

#### `configIDLE_SHOULD_YIELD`

设为1时，如果应用创建了优先级0的任务，Idle Task会让出处理器，帮助同优先级应用任务获得运行机会。它只影响与Idle同为优先级0的任务；不会影响优先级1及以上任务。

当前LED Task计划使用优先级1，所以其主要让出CPU的方式是 `vTaskDelay()` 进入Blocked，而不是依靠Idle Task主动yield。

#### `configUSE_TICKLESS_IDLE`

设为0表示即使系统空闲，SysTick仍按1 kHz持续运行。Tickless Idle会在预计长时间空闲时停止周期Tick、进入低功耗，并在唤醒后补偿经过的Tick；这涉及睡眠指令、唤醒源和时钟误差，不属于最小调度Demo。

#### 本步骤人工检查

1. 所有宏都位于最终 `#endif` 之前。
2. `configMAX_PRIORITIES` 是5，合法任务优先级是0～4。
3. 优化任务选择为1时，优先级数量不超过32。
4. 不把“抢占”与“同优先级时间片”理解为同一件事。
5. 不把 `configIDLE_SHOULD_YIELD` 误认为所有低优先级任务都会无条件让出CPU。
6. 当前保持 `configUSE_TICKLESS_IDLE=0`，不提前加入低功耗路径。
7. 编辑完成后建议统一以UTF-8保存文件。

当前工程最终采用上述7个调度宏，并通过完整构建验证。`configMAX_TASK_NAME_LEN=16U`包含结尾 `\0`，因此使用ASCII任务名时最多保存15个可见字符。

### 21.19 补充理解：优先级数量、优化选择与Tickless Idle

用户在填写调度配置前提出三个问题。三个宏分别回答不同层面的问题：

```text
configMAX_PRIORITIES
    决定有多少个FreeRTOS任务优先级档位

configUSE_PORT_OPTIMISED_TASK_SELECTION
    决定调度器用什么方法找到最高的Ready优先级

configUSE_TICKLESS_IDLE
    决定系统预计长时间空闲时是否暂停周期Tick以降低功耗
```

#### `configMAX_PRIORITIES=5U` 限制的不是任务数量

这里指的是FreeRTOS Task的优先级数量。值为5时，合法任务优先级为：

```text
0、1、2、3、4
```

数字越大，任务优先级越高。Idle Task固定使用优先级0。

它不会把任务总数限制为5，也不要求每个任务使用不同优先级。例如只要RAM和RTOS Heap足够，可以创建：

```text
Idle Task       优先级0
LED Task        优先级1
Log Task        优先级1
Protocol Task   优先级2
Control Task    优先级3
Alarm Task      优先级3
```

这些任务只使用0、1、2、3四个档位，其中优先级1和3分别有多个任务。任务总数是6个，但优先级档位仍没有超过5。

内核中的结构可抽象为：

```text
pxReadyTasksLists[5]
    ├─ [0]：Idle Task、其他优先级0的Ready任务……
    ├─ [1]：LED Task、Log Task……
    ├─ [2]：Protocol Task……
    ├─ [3]：Control Task、Alarm Task……
    └─ [4]：当前可能为空
```

每个数组元素是一条链表，一条链表可以挂多个TCB。因此“5条Ready List”不等于“只能保存5个TCB”。

后缀 `U` 只表示C语言的无符号整数常量：

```text
5U = unsigned int类型的5
```

它不是优先级单位，也不会改变可用范围0～4。

若任务创建时错误地传入优先级5，它已经超出合法范围。启用断言时应尽早暴露这个错误；不能把越界后的内核保护行为当成正常设计。

#### 优化任务选择解决什么问题

任务调度时，内核首先要回答：

```text
当前所有非空Ready List中，哪一条优先级最高？
```

通用方法可以从高优先级向低优先级逐级检查；Cortex-M3端口的优化方法则用一个32位位图记录哪些优先级存在Ready任务。

假设当前Ready优先级为0、1、3，则位图可以简化为：

```text
bit4 bit3 bit2 bit1 bit0
  0    1    0    1    1

二进制：01011
最高置位：bit3
结论：优先级3是当前最高Ready优先级
```

Cortex-M3具有 `CLZ`（Count Leading Zeros）指令。结合位图，它可以很快计算最高置位的位置，而不必逐条扫描Ready List。

该宏只改变“寻找最高Ready优先级的实现方法”，不会改变调度规则：

- 仍然是最高优先级Ready任务先运行；
- 同优先级多个任务仍由链表和时间片规则选择；
- Blocked任务不会因为启用优化就变成Ready；
- 它不限制每个优先级能挂多少任务。

因为位图是32位，所以启用该优化时最多表示32个优先级档位：

```text
configMAX_PRIORITIES <= 32
```

当前选择5满足条件。ARM_CM3端口本身默认启用该优化，配置文件显式写1是为了让工程策略清楚可见。

#### `configUSE_TICKLESS_IDLE=0` 不等于没有Idle Task

Tickless Idle可译为“无周期Tick的空闲模式”或“空闲时抑制Tick”。它是低功耗机制，与Idle Task是否存在是两件事。

设为0时：

```text
LED Task调用vTaskDelay(500 ms)
        ↓
LED Task进入Blocked
        ↓
没有其他普通Ready任务时运行Idle Task
        ↓
SysTick仍每1 ms中断一次
        ↓
500次Tick后LED Task到期并重新Ready
```

即使CPU大部分时间只运行Idle Task，1 kHz SysTick仍持续唤醒处理器。这最容易观察和调试，因此适合第一版Demo。

设为1时，FreeRTOS会在预计所有普通任务都将阻塞较长时间时尝试：

```text
计算最近一个任务还要多少Tick到期
        ↓
暂时停止周期SysTick
        ↓
配置一次较长的等待并让CPU进入低功耗/空闲
        ↓
由定时到期或其他中断唤醒
        ↓
根据实际经过时间补偿内核Tick计数
```

Tickless不是永久取消Tick，也不是让 `vTaskDelay()` 失效，而是把许多没有必要逐个发生的空闲Tick合并处理。

它需要额外验证：

- 睡眠前后时钟是否稳定；
- 哪些中断能够唤醒CPU；
- 补偿的Tick数量是否准确；
- 调试器、外设和低功耗模式是否相互影响；
- 长延时和临界边界是否产生漂移。

当前目标只是证明调度器、SysTick、PendSV和LED Task正常工作，所以选择0。以后有明确低功耗需求时再单独开启并测试。

#### 三个宏串起来看

```text
某个Tick或外设事件使任务Ready
        ↓
Ready任务被放入0～4中的某条Ready List
        ↓
优化任务选择通过位图+CLZ找到最高非空优先级
        ↓
从该优先级链表中选择任务运行
        ↓
若最后只剩Idle Task可运行
        ├─ Tickless=0：SysTick继续每1 ms发生
        └─ Tickless=1：满足条件时抑制一段空闲Tick
```

### 21.20 实操第3步：动态分配、FreeRTOS Heap与任务栈

本步骤由用户在调度配置之后、文件末尾 `#endif` 之前追加：

```c
/* Memory allocation configuration. */
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configSUPPORT_STATIC_ALLOCATION            0
#define configTOTAL_HEAP_SIZE                      ( 8U * 1024U )
#define configMINIMAL_STACK_SIZE                   128U
#define configAPPLICATION_ALLOCATED_HEAP           0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP  0
#define configHEAP_CLEAR_MEMORY_ON_FREE            0
```

#### 三种容易混淆的单位

```text
configTOTAL_HEAP_SIZE
    单位：字节

configMINIMAL_STACK_SIZE
    单位：StackType_t个数，即“栈深度”

xTaskCreate()的栈深度参数
    单位：StackType_t个数，即“栈深度”
```

当前 ARM_CM3 端口定义：

```c
typedef uint32_t StackType_t;
```

所以一个 `StackType_t` 为4字节：

```text
configMINIMAL_STACK_SIZE = 128
Idle Task栈字节数        = 128 × 4
                        = 512字节
```

不能把128误解成128字节，也不能给 `configTOTAL_HEAP_SIZE` 再乘4。

#### 动态分配与静态分配分别控制什么

```c
#define configSUPPORT_DYNAMIC_ALLOCATION  1
#define configSUPPORT_STATIC_ALLOCATION   0
```

动态分配设为1后，可使用：

```text
xTaskCreate()
```

内核会通过 `pvPortMalloc()`，也就是当前选择的 `heap_4.c`，为任务分配TCB和任务栈。

静态分配设为0后，不编译 `xTaskCreateStatic()` 等由应用提供对象内存的创建路径。这不表示C语言不能使用全局变量或 `static` 变量；它只表示当前不启用FreeRTOS对象的静态创建API。

FreeRTOS要求动态分配和静态分配不能同时为0。当前只启用动态分配，是为了让最小Demo的内存来源保持单一：

```text
LED Task的TCB和栈  ─┐
Idle Task的TCB和栈 ─┼─► heap_4.c管理的ucHeap[]
后续动态内核对象   ─┘
```

#### `configTOTAL_HEAP_SIZE`

```c
#define configTOTAL_HEAP_SIZE    ( 8U * 1024U )
```

单位是字节，因此当前候选为：

```text
8 × 1024 = 8192字节
```

`heap_4.c` 默认在内部定义：

```c
static uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
```

链接时它会作为约8 KiB的数组进入 `.bss`。无论运行时已经分配多少对象，这8 KiB地址空间都已经从64 KiB SRAM中预留给FreeRTOS Heap。

任务创建时，Heap内部可抽象为：

```text
ucHeap[8192]
├─ 分配器管理和对齐开销
├─ Idle Task TCB
├─ Idle Task栈
├─ LED Task TCB
├─ LED Task栈
└─ 尚未分配的空闲块
```

TCB和任务栈都位于 `ucHeap[]` 里面，不能在估算RAM时把同一块任务栈既算入Heap又在Heap外重复计算。

8 KiB是首版候选而不是最终答案，因为实际消耗还受以下因素影响：

- `TCB_t` 在当前配置和编译器下的真实大小；
- 每个任务传给 `xTaskCreate()` 的栈深度；
- Heap块头、8字节对齐和碎片；
- 后续是否增加任务、队列、信号量等动态对象；
- 64 KiB SRAM中 `.data`、其他 `.bss`、MSP和链接脚本预留区的占用。

后续至少用三种证据验证：

1. 链接器和MAP确认 `ucHeap` 为8192字节，总RAM未溢出。
2. 运行时读取 `xPortGetFreeHeapSize()` 和 `xPortGetMinimumEverFreeHeapSize()`。
3. 分别检查每个任务的栈高水位，避免用“Heap还有剩余”代替栈安全验证。

#### `configMINIMAL_STACK_SIZE`

```c
#define configMINIMAL_STACK_SIZE    128U
```

它主要是内核创建Idle Task时使用的栈深度。它不是：

- 所有任务的统一栈大小；
- MSP大小；
- FreeRTOS Heap大小；
- 字节数。

后续创建LED Task时仍需单独提供栈深度，例如同样选择128 words作为首版候选，然后分别验证Idle和LED任务的高水位。

#### `configAPPLICATION_ALLOCATED_HEAP`

```c
#define configAPPLICATION_ALLOCATED_HEAP    0
```

设为0时，由 `heap_4.c` 自己定义静态的 `ucHeap[]`，链接器按普通 `.bss` 对象为它安排RAM地址。

若设为1，则 `heap_4.c` 只声明：

```c
extern uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
```

应用必须在其他源文件真正定义该数组，常用于把Heap放进自定义链接段或特定RAM区域。当前STM32F103只有一块普通SRAM，最小Demo不需要额外控制Heap位置，因此选择0。

#### `configSTACK_ALLOCATION_FROM_SEPARATE_HEAP`

```c
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP    0
```

设为0表示动态任务的TCB和任务栈都从同一个FreeRTOS Heap分配。若设为1，应用还要实现专门的任务栈分配/释放函数，并为栈准备另一块内存来源。

当前没有多块速度不同的RAM，也没有把任务栈放入特殊区域的需求，所以保持0。

#### `configHEAP_CLEAR_MEMORY_ON_FREE`

```c
#define configHEAP_CLEAR_MEMORY_ON_FREE    0
```

设为0表示 `vPortFree()` 回收内存块时不把整块内容清零，这也是 `heap_4.c` 的默认行为。设为1可以减少旧数据残留，但会增加释放时的执行时间；它也不能代替内存越界检查。

最小Demo不会主动删除LED Task，当前先保持0。以后处理敏感数据或明确要求清理时再评估。

#### 与链接脚本中C Heap和MSP的关系

当前RAM中可以粗略理解为：

```text
64 KiB SRAM
├─ .data / 普通.bss
├─ ucHeap[8192]                 FreeRTOS Heap
│   ├─ 动态TCB
│   └─ 动态任务栈
├─ 链接脚本_Min_Heap_Size区域   C运行库Heap预留
└─ 链接脚本_Min_Stack_Size区域  MSP预留
```

FreeRTOS Heap不等于C运行库Heap，任务PSP栈也不等于异常和启动过程使用的MSP。它们最终都消耗同一块64 KiB SRAM，所以必须通过链接结果统一核算。

#### 本步骤人工检查

1. 7个宏全部位于最终 `#endif` 之前。
2. 动态分配为1、静态分配为0。
3. `configTOTAL_HEAP_SIZE` 明确为8192字节，而不是8192 words。
4. `configMINIMAL_STACK_SIZE=128U` 理解为512字节的Idle Task栈候选。
5. `configAPPLICATION_ALLOCATED_HEAP=0` 时不在应用中重复定义 `ucHeap`。
6. 任务栈从FreeRTOS Heap内部划分，RAM估算时不重复统计。
7. 不把8 KiB候选写成已经通过MAP或实机验证的最终值。

最小Demo当时最终采用上述7个内存配置宏，8 KiB Heap已经进入ELF并完成静态内存边界检查。后续诊断实测该配置只剩768字节，接入ADC后曾调整为24 KiB；P3加入Storage和FTP双Socket Task及其Queue后，当前配置为32 KiB。各任务栈高水位仍需按每个阶段的最终任务集合重新测量，详见第29章。

### 21.21 补充理解：为什么释放Heap内存时默认不清零

“释放内存”和“把内存内容改成0”是两个不同操作。

`vPortFree()` 的核心语义是：

```text
这块内存原来属于某个任务或内核对象
        ↓
把对应块重新挂入Free List
        ↓
必要时与相邻空闲块合并
        ↓
以后可以把这段地址重新分配给其他对象
```

释放以后，旧字节可能仍暂时保留在SRAM中，但原来的调用者已经失去访问权。继续读取或写入该指针属于 use-after-free（释放后继续使用）错误；不能因为旧内容“看起来还在”就继续使用。

#### 默认不清零的主要原因

第一，分配器正确工作并不要求用户数据区全为0。它只需要维护块大小、分配标记和空闲链表等管理信息。

第二，清零需要额外时间，而且时间与内存块大小相关：

```text
释放32字节  → 最多额外写32字节
释放4 KiB   → 最多额外写4 KiB
释放更大块  → 写入时间继续增加
```

嵌入式实时系统通常希望释放路径尽量短，并避免仅为覆盖旧数据产生大批RAM写操作。`heap_4.c` 的空闲链表插入与相邻块合并本身也有开销；清零会再增加一个按块大小增长的操作。

第三，下一次获得这块内存的代码本来就应当主动初始化它。FreeRTOS的：

```c
pvPortMalloc()
```

语义类似C库 `malloc()`，不是 `calloc()`；不能假设新分配内存天然为0。即使启用了“释放时清零”，应用仍不应依赖这个副作用来代替初始化。

#### 不清零不表示永远都不需要清理

如果内存中保存过以下敏感内容，释放前清理可能有价值：

- 密码、密钥或认证Token；
- 设备唯一凭据；
- 协议中的敏感Payload；
- 不希望被后续对象读到的历史数据。

可以按系统策略选择：

```c
#define configHEAP_CLEAR_MEMORY_ON_FREE    1
```

也可以只在敏感对象释放前对它的有效数据区进行专门的安全清理。普通 `memset()` 在某些优化条件下可能被编译器认为结果不再使用而删除；真正的密钥清理还需要使用不会被优化掉的安全清零方法，并结合具体编译器验证。

#### 清零不能解决哪些错误

释放时清零不能证明内存安全，也不能替代：

- 防止重复释放；
- 防止释放后继续访问；
- 防止越界写；
- Heap块头保护；
- 任务栈溢出检测；
- 新对象创建后的显式初始化。

如果程序错误地使用已经释放的指针，读到0只是改变错误表现，错误本身仍然存在。

#### 当前最小Demo为什么选择0

当前只计划动态创建Idle Task和LED Task，LED Task正常情况下不会删除，也不保存密钥或密码。因此该配置在最小Demo中几乎不会产生可观察差异：

```text
configHEAP_CLEAR_MEMORY_ON_FREE=0
```

选择0可以保持默认行为、减少当前变量，并避免在学习调度器时额外引入释放时清零开销。以后项目开始处理MQTT/FTP凭据，或者需要动态删除持有敏感数据的对象时，应重新评估；当前值不是永久的安全策略。

### 21.22 实操第4步：中断边界与三个异常Handler映射

本步骤由用户在内存配置之后、文件末尾 `#endif` 之前追加：

```c
/* Cortex-M3 interrupt configuration. */
#define configPRIO_BITS                                  4U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5U
#define configMAX_SYSCALL_INTERRUPT_PRIORITY             \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8U - configPRIO_BITS ) )

/* Direct exception handler routing. */
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler
```

#### 三个优先级宏中谁真正被内核使用

```c
#define configPRIO_BITS                              4U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
```

这两个是当前应用为了可读性定义的辅助宏：

- `configPRIO_BITS=4U` 记录STM32F103只实现NVIC 8位优先级字段中的高4位；该事实也可在CMSIS设备头的 `__NVIC_PRIO_BITS` 中交叉检查。
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5U` 使用适合人阅读和传给标准外设库/CMSIS接口的逻辑优先级形式。

ARM_CM3端口真正读取的是：

```c
configMAX_SYSCALL_INTERRUPT_PRIORITY
```

它必须采用已经左移到NVIC寄存器高位的硬件值。展开计算：

```text
8 - configPRIO_BITS = 8 - 4 = 4

configMAX_SYSCALL_INTERRUPT_PRIORITY
    = 5 << 4
    = 0x50
```

因此这三行同时保留了：

```text
硬件事实：4个有效位
人类策略：逻辑边界5
端口输入：硬件值0x50
```

若以后换成实现3个优先级位的MCU，左移位数将变为5；不能继续机械使用当前 `0x50`。

#### 这个边界不会自动配置所有外设IRQ

`configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50` 的作用是：

- FreeRTOS临界区通过BASEPRI屏蔽逻辑优先级5～15；
- 端口断言检查调用 `FromISR` API的当前ISR是否位于允许范围；
- PendSV切换期间使用同一个边界保护调度器数据。

它不会自动把USART、CAN、EXTI等外设中断设置成逻辑优先级5。以后每个外设初始化仍需根据用途显式调用NVIC配置接口。

当前规则保持：

```text
逻辑IRQ 0～4
    ├─ 紧急程度高
    ├─ 不受FreeRTOS BASEPRI临界区屏蔽
    └─ 禁止调用任何FreeRTOS API

逻辑IRQ 5～15
    ├─ 可被FreeRTOS临界区暂时屏蔽
    └─ 允许调用名称以FromISR结尾的API
```

普通任务API在任何ISR中都不能调用；进入允许范围也只是获得调用 `FromISR` API的资格。

#### 为什么不加入 `configKERNEL_INTERRUPT_PRIORITY`

许多旧教程和其他Cortex-M端口会定义：

```text
configKERNEL_INTERRUPT_PRIORITY
```

但当前固定的FreeRTOS Kernel V11.3.0 `portable/GCC/ARM_CM3/port.c` 没有引用该宏。它在启动调度器时直接执行：

```text
PendSV优先级字段 ← 255 → STM32实际逻辑优先级15
SysTick优先级字段 ← 255 → STM32实际逻辑优先级15
SVC优先级字段 ← 0       → STM32实际逻辑优先级0
```

所以首版配置不为了“看起来像旧模板”而添加一个当前端口不使用的宏。以后升级内核或更换端口时必须重新搜索端口源码。

#### Handler映射为什么属于配置文件

当前启动文件向量表引用：

```text
SVC_Handler
PendSV_Handler
SysTick_Handler
```

当前FreeRTOS ARM_CM3端口源代码定义：

```text
vPortSVCHandler
xPortPendSVHandler
xPortSysTickHandler
```

直接映射宏让预处理器在编译 `port.c` 时替换名字：

```text
port.c原函数名          预处理后生成的ELF符号
vPortSVCHandler      →  SVC_Handler
xPortPendSVHandler   →  PendSV_Handler
xPortSysTickHandler  →  SysTick_Handler
```

启动文件中的三个符号是弱定义；FreeRTOS端口生成同名强定义后，链接器会让向量表指向FreeRTOS实现。

三者作用：

| 映射后的符号 | FreeRTOS职责 |
| --- | --- |
| `SVC_Handler` | 从MSP/启动上下文恢复第一个任务并开始使用PSP |
| `PendSV_Handler` | 保存当前任务R4～R11、切换TCB、恢复下一个任务上下文 |
| `SysTick_Handler` | 增加Tick、处理延时到期，并在需要时挂起PendSV |

若不做映射，`port.c` 中的三个函数即使成功编译，也可能没有被向量表引用；发生异常时仍会进入启动文件的弱默认死循环。

#### 为什么本步骤暂时不添加Handler安装检查

V11.3.0支持：

```c
#define configCHECK_HANDLER_INSTALLATION 1
```

调度器启动时可检查向量表中的SVC和PendSV是否安装正确。但显式启用该检查要求同时定义有效的 `configASSERT()`。断言、Handler检查、栈溢出Hook和分配失败Hook将在下一组一起配置，避免留下没有故障处理出口的半套调试配置。

#### 本步骤人工检查

1. 三个辅助/结果宏展开后，`configMAX_SYSCALL_INTERRUPT_PRIORITY` 等于 `0x50`。
2. 没有把逻辑值5直接交给使用硬件值的ARM_CM3端口。
3. 没有定义值为0的最大系统调用优先级。
4. 三个Handler映射的左右名称和大小写与当前启动文件、`port.c` 完全一致。
5. 不添加当前端口未引用的 `configKERNEL_INTERRUPT_PRIORITY`。
6. 明白该宏不会替应用自动配置外设IRQ优先级。
7. 当前未添加 `configCHECK_HANDLER_INSTALLATION`，下一组将与 `configASSERT` 同时加入。

当前工程的中断边界宏展开为 `0x50`，三个Handler名称与启动文件及端口一致，ELF向量和强符号已经通过静态检查。准确表述是：“逻辑5是允许调用FromISR API的最高紧急程度，逻辑5～15允许、0～4禁止”。

### 21.23 补充理解：具体中断优先级到底在哪里配置

用户指出得正确：

```c
#define configPRIO_BITS                               4U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5U
#define configMAX_SYSCALL_INTERRUPT_PRIORITY          \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8U - configPRIO_BITS ) )
```

这三行没有选择USART、CAN或EXTI，也没有向某个外设的NVIC优先级寄存器写值。它们定义的是FreeRTOS中断规则及 `BASEPRI` 屏蔽边界，不是具体IRQ的优先级分配表。

#### 三层配置必须分开

```text
第一层：硬件能力
configPRIO_BITS=4
说明STM32F103实现4个NVIC优先级位

第二层：FreeRTOS规则边界
configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5
configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50
说明逻辑5～15允许调用FromISR API，0～4禁止

第三层：具体IRQ分配
在CAN、USART、EXTI等外设初始化函数中调用NVIC配置接口
决定“这个IRQ实际是2、6还是其他逻辑优先级”
```

前两层定义规则，第三层把具体中断放到规则的一侧。

#### 当前标准外设库如何给具体IRQ赋优先级

当前工程使用STM32标准外设库。典型过程为：

```c
NVIC_InitTypeDef nvic_init;

nvic_init.NVIC_IRQChannel = EXTI0_IRQn;
nvic_init.NVIC_IRQChannelPreemptionPriority = 6U;
nvic_init.NVIC_IRQChannelSubPriority = 0U;
nvic_init.NVIC_IRQChannelCmd = ENABLE;
NVIC_Init( &nvic_init );
```

这里的 `NVIC_IRQChannelPreemptionPriority=6U` 才是给一个具体IRQ设置逻辑优先级6。标准外设库的 `NVIC_Init()` 会根据当前优先级分组完成编码和左移，再写入对应的NVIC `IP[]` 寄存器。

若该ISR需要调用例如：

```text
xQueueSendFromISR()
vTaskNotifyGiveFromISR()
xSemaphoreGiveFromISR()
```

则逻辑优先级必须在5～15范围。示例中的6满足边界。

如果一个非常紧急的ISR选择逻辑优先级2，它可以抢占FreeRTOS临界区，但禁止调用任何FreeRTOS API。优先级2不是错误，只有“优先级2又调用FreeRTOS API”才违反规则。

#### 使用CMSIS接口时为什么也写逻辑值

另一种常见写法是CMSIS接口：

```c
NVIC_SetPriority( EXTI0_IRQn, 6U );
NVIC_EnableIRQ( EXTI0_IRQn );
```

传给 `NVIC_SetPriority()` 的通常也是未左移的逻辑优先级6；CMSIS内部会根据 `__NVIC_PRIO_BITS` 左移后写寄存器。

这与FreeRTOS宏不同：

```text
NVIC_Init()/NVIC_SetPriority()参数
    使用逻辑值，例如6

configMAX_SYSCALL_INTERRUPT_PRIORITY
    当前ARM_CM3端口直接写BASEPRI，使用硬件值，例如0x50
```

不能因为边界宏写 `0x50`，就把 `NVIC_IRQChannelPreemptionPriority` 也写成 `0x50`。

#### 优先级分组必须先统一

当前STM32F103有4个有效优先级位。FreeRTOS希望这些位全部用于抢占优先级：

```c
NVIC_PriorityGroupConfig( NVIC_PriorityGroup_4 );
```

在STM32标准外设库命名中：

```text
NVIC_PriorityGroup_4
    = 4位抢占优先级
    = 0位子优先级
    = 逻辑抢占优先级0～15
```

采用该分组后，具体IRQ一般写：

```text
NVIC_IRQChannelPreemptionPriority = 0～15中的设计值
NVIC_IRQChannelSubPriority        = 0
```

优先级分组是全局设置，不属于某一个外设。工程后续应在统一的系统/中断初始化位置设置一次，驱动不应各自反复修改分组。

#### 当前 `bsp_exti.c` 是什么状态

现有旧例程中可以看到：

```c
NVIC_PriorityGroupConfig( NVIC_PriorityGroup_1 );
NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
```

`NVIC_PriorityGroup_1` 在标准外设库中表示1位抢占优先级、3位子优先级，不符合本项目后续FreeRTOS的统一规则。该文件当前没有加入CMake，也没有参与最小LED构建，因此现在不会影响调度器；以后真正启用EXTI前，应由用户重新设计和修改，而不是直接沿用旧例程。

若EXTI ISR以后要调用 `FromISR` API，可以选择逻辑6等5～15范围值，子优先级为0；若它必须作为不访问内核的高紧急ISR，则可选择0～4，但必须保证ISR不调用任何FreeRTOS API。具体数值应根据业务响应要求确定，不能统一机械设置为6。

#### SVC、PendSV和SysTick在哪里设置

这三个是Cortex-M系统异常，不是普通外设IRQ。当前V11.3.0 ARM_CM3 `port.c` 在 `xPortStartScheduler()` 中直接写系统处理优先级寄存器：

```text
PendSV  → 逻辑优先级15，最低紧急程度
SysTick → 逻辑优先级15，最低紧急程度
SVC     → 逻辑优先级0，最高紧急程度
```

因此它们不通过外设驱动中的 `NVIC_Init()` 设置。三个Handler映射宏只解决“向量表跳到哪个函数”，而 `port.c` 的寄存器写入解决“这些系统异常使用什么优先级”。

#### 完整关系图

```text
FreeRTOSConfig.h
├─ configPRIO_BITS=4
│      说明硬件有多少有效位
├─ configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50
│      设置BASEPRI边界和FromISR合法范围
└─ Handler映射
       让向量表进入FreeRTOS的SVC/PendSV/SysTick函数

port.c启动调度器
└─ 设置SVC/PendSV/SysTick系统异常优先级

各外设驱动初始化
├─ CAN NVIC配置   → 具体CAN IRQ优先级
├─ USART NVIC配置 → 具体USART IRQ优先级
└─ EXTI NVIC配置  → 具体EXTI IRQ优先级
```

#### 判断一个外设IRQ配置是否合法

以后检查任何中断时依次问：

1. 全局是否使用 `NVIC_PriorityGroup_4`？
2. 该IRQ的逻辑抢占优先级到底是多少？
3. 该ISR是否调用FreeRTOS API？
4. 若调用，是否只使用 `FromISR` 版本？
5. 若调用，逻辑优先级是否位于5～15？
6. 若位于0～4，是否完全不访问任何FreeRTOS内核对象？

### 21.24 实操第5步：断言、Handler检查和故障Hook

本步骤由用户在中断与Handler配置之后、文件末尾 `#endif` 之前追加：

```c
/* Assertion and hook configuration. */
#define configUSE_IDLE_HOOK                   0
#define configUSE_TICK_HOOK                   0
#define configUSE_MALLOC_FAILED_HOOK          1
#define configCHECK_FOR_STACK_OVERFLOW        2
#define configCHECK_HANDLER_INSTALLATION      1

#define configASSERT( condition )                         \
    do                                                    \
    {                                                     \
        if( ( condition ) == 0 )                          \
        {                                                 \
            taskDISABLE_INTERRUPTS();                     \
            for( ; ; )                                    \
            {                                             \
            }                                             \
        }                                                 \
    } while( 0 )
```

多行宏中，每个需要继续到下一物理行的反斜杠 `\` 必须是该行最后一个有效字符，后面不能追加 `//` 注释。

#### `configASSERT` 解决什么问题

断言表示“内核认为这个条件必须成立”。例如当前ARM_CM3端口会检查：

- 最大系统调用优先级经硬件有效位掩码后不能为0；
- `configMAX_SYSCALL_INTERRUPT_PRIORITY` 不能包含硬件未实现的低位；
- 调用 `FromISR` API的当前外设IRQ优先级必须合法；
- NVIC优先级分组必须允许FreeRTOS正确解释抢占边界；
- SVC和PendSV是否安装到当前VTOR指向的向量表；
- 普通任务API的临界区入口不能来自ISR上下文；
- 任务优先级、链表和Heap操作中的内部前置条件。

宏的执行逻辑是：

```text
condition != 0
    └─ 条件成立，继续运行

condition == 0
    ├─ taskDISABLE_INTERRUPTS()
    └─ 停在无限循环，等待GDB查看
```

`taskDISABLE_INTERRUPTS()` 在ARM_CM3端口中通过BASEPRI屏蔽FreeRTOS可管理范围内的中断，不是把所有物理中断永久清零。程序停在循环后，可用GDB观察：

- 当前调用栈；
- 触发断言的源码行；
- 当前中断号和优先级寄存器；
- `BASEPRI`、`IPSR`、`VTOR`；
- 当前TCB和任务名。

当前简单宏没有主动打印文件名和行号，也没有复位设备。学习阶段先保留现场；以后可改为调用项目自己的 `vAssertCalled(__FILE__, __LINE__)`，但这会要求应用增加函数实现。

#### 为什么使用 `do { ... } while (0)`

它让包含多条语句的宏在C语法中表现得像一条普通语句。例如：

```c
if( ready )
{
    configASSERT( pointer != NULL );
}
else
{
    /* ... */
}
```

调用者可以正常在宏后写分号，不会因宏内部有多条语句而破坏外层 `if/else` 结构。`while(0)` 只用于语法包装，不会形成运行时循环；只有条件失败后的 `for(;;)` 才是真正的停机循环。

#### `configCHECK_HANDLER_INSTALLATION`

```c
#define configCHECK_HANDLER_INSTALLATION    1
```

当前端口在 `xPortStartScheduler()` 中读取VTOR指向的向量表并检查：

```text
向量表SVC项    == vPortSVCHandler（映射后的SVC_Handler）
向量表PendSV项 == xPortPendSVHandler（映射后的PendSV_Handler）
```

如果Handler映射写错、启动文件不匹配或VTOR指向错误，调度器会在真正开始任务前触发 `configASSERT`。

端口没有在这里检查SysTick，因为应用可以覆盖弱 `vPortSetupTimerInterrupt()`，改用其他硬件定时器产生RTOS Tick。但当前项目仍使用SysTick，所以后续必须通过ELF符号和实机Tick断点验证 `SysTick_Handler`。

显式设置安装检查为1时，V11.3.0要求定义 `configASSERT()`；因此这两个配置在同一步加入。

#### `configCHECK_FOR_STACK_OVERFLOW=2`

FreeRTOS创建任务栈时会用已知模式填充未使用区域。模式2在任务切换等检查点观察栈边界处的模式是否被覆盖：

```text
边界模式仍完整 → 没有检测到溢出
边界模式被破坏 → 调用vApplicationStackOverflowHook()
```

启用后，应用必须在后续入口/故障处理代码中提供：

```c
void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName );
```

两个参数帮助判断哪个任务发生问题。Hook应尽快停机并保留现场，不能在栈已经可疑时执行复杂格式化、动态分配或阻塞操作。

模式2比模式1多检查栈末端填充值，但仍不能捕获所有内存破坏：一次越界写可能跳过检测区域，其他指针错误也可能破坏别的任务。它是调试防线，不是数学证明。

#### `configUSE_MALLOC_FAILED_HOOK=1`

当 `pvPortMalloc()` 不能从 `heap_4.c` 获得所需内存时，内核调用：

```c
void vApplicationMallocFailedHook( void );
```

例如动态创建任务通常需要为TCB和任务栈分配内存，其中任一步失败都可能触发Hook，并使 `xTaskCreate()` 返回失败。

启用Hook不代表应用可以忽略API返回值。后续创建LED Task时仍必须检查：

```text
xTaskCreate()是否返回pdPASS
```

Hook用于提供统一故障入口，返回值用于让调用位置知道本次操作是否成功，两者职责不同。

#### 为什么Idle Hook和Tick Hook先关闭

```c
#define configUSE_IDLE_HOOK    0
#define configUSE_TICK_HOOK    0
```

设为1时，应用必须分别提供：

```text
vApplicationIdleHook()
vApplicationTickHook()
```

Idle Hook在Idle Task上下文反复执行；不能阻塞，并且必须及时返回。Tick Hook在Tick中断路径执行；必须非常短，不能调用会阻塞的任务API。

当前最小Demo不需要这两个回调，关闭可以避免额外函数和上下文规则。关闭Hook不会删除Idle Task，也不会关闭SysTick。

#### 此步骤完成后为什么仍可能链接失败

配置已经要求两个应用Hook存在：

```text
vApplicationStackOverflowHook()
vApplicationMallocFailedHook()
```

但用户尚未在C源码中实现它们。若此时把内核加入CMake并链接，出现这两个未定义符号属于预期结果，不应通过关闭检查来掩盖。后续改造入口程序时由用户亲自实现最小停机Hook。

#### 本步骤人工检查

1. 多行 `configASSERT` 的反斜杠位于每个续行的末尾。
2. 调用宏时写 `configASSERT(expression);`，宏定义本体使用 `do...while(0)`。
3. Handler安装检查与 `configASSERT` 同时启用。
4. 栈溢出模式为2，并记住后续必须实现Stack Overflow Hook。
5. Malloc Failed Hook启用，并记住仍要检查 `xTaskCreate()` 返回值。
6. Idle Hook和Tick Hook保持0，不误认为Idle Task或SysTick被关闭。
7. 当前不构建，不把预期的Hook未实现状态误判为已完成接入。

当前工程已经启用上述断言和Hook配置，并提供两个应用Hook，完整构建通过。准确表述是：“断言失败时屏蔽逻辑优先级5～15，并停在循环中保留GDB现场”。故障注入验证尚未完成。

### 21.25 补充理解：任务栈溢出与FreeRTOS Heap分配失败

这两个配置处理的是两种不同阶段、不同原因的内存故障：

```text
configUSE_MALLOC_FAILED_HOOK=1
    对象创建/申请内存阶段：FreeRTOS Heap拿不出所需内存

configCHECK_FOR_STACK_OVERFLOW=2
    任务成功创建并运行以后：某个任务把分给自己的栈空间用穿
```

可以用仓库和工作台类比：

```text
FreeRTOS Heap = 存放可分配内存的仓库
任务栈        = 从仓库中分给某个任务的一张固定大小工作台

Malloc Failed
    = 仓库已经拿不出足够空间制作新的工作台/TCB

Stack Overflow
    = 工作台已经分配成功，但任务运行时摆放的调用现场和局部变量超过工作台边界
```

#### 一个任务为什么需要自己的栈

每个FreeRTOS Task都有独立任务栈，用于保存：

- 函数调用的返回地址；
- 局部自动变量；
- 函数参数和编译器临时值；
- 异常自动压入的R0～R3、R12、LR、PC、xPSR；
- PendSV保存的R4～R11；
- 调用链上的其他栈帧。

动态创建任务时，任务栈来自 `heap_4.c` 管理的 `ucHeap[]`，但创建成功后它已经成为这个任务的固定栈区域。Heap中还有空闲空间，不会让一个即将溢出的任务栈自动长大。

例如：

```text
FreeRTOS Heap总大小：8192字节
LED Task栈：          512字节
Heap剩余：            仍有6000多字节
```

即使Heap还剩很多，只要LED Task实际栈使用超过自己的512字节，它仍会发生Stack Overflow。Heap剩余和单任务栈余量必须分别观察。

#### 模式2如何使用 `0xA5` 检查任务栈

当前V11.3.0 `tasks.c` 定义：

```c
#define tskSTACK_FILL_BYTE    0xa5U
```

创建任务时，在构造初始上下文之前，内核先把整个任务栈填充为 `0xA5`：

```text
任务刚分配到一块512字节栈

低地址                                             高地址
+------------------------------------------------------+
| A5 A5 A5 A5 A5 A5 A5 A5 ... A5 A5 A5 A5 A5 A5 A5  |
+------------------------------------------------------+
```

ARM_CM3端口的栈从高地址向低地址增长。建立初始任务上下文、调用更多函数和创建局部变量时，高地址一侧开始被真实栈数据覆盖，并逐渐向低地址发展：

```text
低地址                                             高地址
+------------------------------------------------------+
| A5 A5 A5 A5 A5 A5 | 已使用的任务栈数据、寄存器现场   |
+------------------------------------------------------+
                       ← 栈使用方向
```

只要低地址边界仍留有 `0xA5`，说明至少这片保护区域尚未被使用。当前Cortex-M3、向低地址增长的模式2检查：

1. TCB中保存的 `pxTopOfStack` 是否已经到达/越过栈起点。
2. 栈起点处前4个32位字是否仍等于 `0xA5A5A5A5`。

也就是检查低地址端16字节保护模式。若任意一项失败，就调用：

```c
vApplicationStackOverflowHook( ( TaskHandle_t ) pxCurrentTCB,
                               pxCurrentTCB->pcTaskName );
```

Hook参数提供疑似溢出的任务句柄和任务名。

#### 检查在什么时候发生

当前内核在调度/任务切换路径中调用 `taskCHECK_FOR_STACK_OVERFLOW()`，重点检查即将被切出的当前任务。

它不是每条机器指令后都检查。因此可能出现：

```text
任务先发生越界写
    ↓
越界数据已经破坏邻近内存
    ↓
到下一个任务切换检查点
    ↓
检测到边界或0xA5模式被破坏
```

所以模式2能发现很多常见栈溢出，但不能保证在第一次非法写之前拦截，也不能保证发现所有跳跃式越界或其他指针造成的内存破坏。

#### 哪些代码容易导致任务栈不足

- 在任务函数中声明很大的局部数组；
- 深层函数调用链；
- 递归；
- `printf`、浮点格式化等栈开销较大的库函数；
- ISR嵌套和异常现场；
- 编译优化等级改变导致的栈布局变化；
- 把本应放在静态区或流式处理的数据整块放到局部变量中。

例如下面的局部数组直接消耗约400字节任务栈：

```c
void ExampleTask( void * argument )
{
    uint8_t buffer[ 400 ];
    /* ... */
}
```

如果任务总栈只有512字节，还要容纳函数现场和中断上下文，就很危险。

#### Stack Overflow Hook应该做什么

检测到栈溢出时，当前任务栈已经不可信，Hook应尽量简单：

```text
记录最少量的任务句柄/任务名或错误码
关闭或屏蔽会继续改变系统状态的中断
停在循环中供GDB查看，或进入经过设计的安全复位路径
```

不适合在Hook中：

- 再申请动态内存；
- 调用复杂 `printf`；
- 执行长调用链；
- 阻塞等待队列或信号量；
- 继续让发生溢出的任务正常运行。

#### Malloc Failed是什么意思

`heap_4.c` 的 `pvPortMalloc(size)` 会从 `ucHeap[]` 的空闲链表中寻找足够大的连续空闲块，并考虑：

- 用户请求大小；
- Heap块头管理开销；
- 8字节对齐；
- 当前剩余空闲块的分布。

若找不到足够大的块，它返回：

```c
NULL
```

启用：

```c
#define configUSE_MALLOC_FAILED_HOOK    1
```

后，当前 `heap_4.c` 在检测到返回值为 `NULL` 时调用：

```c
vApplicationMallocFailedHook();
```

它不会自动增加Heap、删除其他任务、重试或复位，只是把失败事件交给应用处理。

#### 哪些操作可能触发Malloc Failed

当前最小Demo中，`xTaskCreate()` 通常需要动态获得：

```text
一块TCB内存
一块任务栈内存
```

Idle Task也需要TCB和栈。以后启用相应模块后，动态创建以下对象也会使用FreeRTOS Heap：

- Queue；
- Semaphore；
- Mutex；
- Event Group；
- Software Timer相关对象；
- Stream/Message Buffer。

即使总空闲字节看起来不少，如果没有足够大的连续块满足本次请求，也可能失败。`heap_4.c` 会合并相邻空闲块以减少碎片，但不能保证任意分配/释放序列永不产生不可用碎片。

#### Malloc Failed Hook不能替代返回值检查

后续用户创建LED Task时仍需检查：

```c
BaseType_t result = xTaskCreate( /* ... */ );

if( result != pdPASS )
{
    /* 创建失败处理。 */
}
```

原因是：

- Hook提供系统统一的诊断入口；
- `xTaskCreate()` 返回值告诉当前调用者这次任务创建是否成功；
- Hook理论上可以返回，调用者不能假设调用Hook后程序必然永久停止。

#### Malloc Failed Hook应该做什么

由于Heap已经不足，Hook中禁止再次依赖动态分配。适合：

```text
读取剩余Heap统计
记录固定大小的错误码/计数器
关闭危险输出
停机供GDB检查或进入既定安全恢复策略
```

不适合：

- 再调用 `pvPortMalloc()`；
- 动态创建日志消息或任务；
- 假设打印函数完全不使用Heap；
- 无限重试同一个必然失败的分配。

#### 两种故障对照

| 对比项 | Stack Overflow | Malloc Failed |
| --- | --- | --- |
| 发生阶段 | 任务创建成功后的运行阶段 | 创建任务/对象或主动申请内存时 |
| 检查对象 | 某一个任务自己的固定栈 | `heap_4.c` 管理的整个FreeRTOS Heap |
| 常见原因 | 局部变量大、调用过深、递归、栈配置过小 | Heap总量不足、请求过大、连续块不足 |
| 当前配置 | `configCHECK_FOR_STACK_OVERFLOW=2` | `configUSE_MALLOC_FAILED_HOOK=1` |
| 回调 | `vApplicationStackOverflowHook()` | `vApplicationMallocFailedHook()` |
| 是否自动修复 | 否 | 否 |
| 后续验证 | 栈高水位、故意使用小栈 | 剩余Heap、故意请求过大内存 |

#### 必须记住的结论

```text
Heap还有空间 ≠ 每个任务栈都安全
任务栈很大   ≠ FreeRTOS Heap一定够用
启用Hook     ≠ 内核自动修复故障
没有触发Hook ≠ 已经证明内存永远安全
```

### 21.26 实操第6步：内核功能与API裁剪

本步骤由用户在断言/Hook配置之后、文件末尾 `#endif` 之前追加：

```c
/* Optional kernel feature configuration. */
#define configUSE_TASK_NOTIFICATIONS            0  /* 首版不用任务通知，暂不增加每个TCB的通知字段。 */
#define configUSE_MUTEXES                       0  /* 未加入queue.c，暂不编译互斥量和优先级继承支持。 */
#define configUSE_RECURSIVE_MUTEXES             0  /* 未启用普通Mutex，递归Mutex也关闭。 */
#define configUSE_COUNTING_SEMAPHORES            0  /* 未加入queue.c，暂不使用计数信号量。 */
#define configUSE_QUEUE_SETS                     0  /* 未加入queue.c，暂不使用Queue Set。 */
#define configUSE_TIMERS                         0  /* 未加入timers.c，不创建Timer Task和Timer Queue。 */
#define configUSE_EVENT_GROUPS                   0  /* 未加入event_groups.c，暂不使用事件组。 */
#define configUSE_STREAM_BUFFERS                 0  /* 未加入stream_buffer.c，暂不使用流/消息缓冲区。 */
#define configUSE_CO_ROUTINES                    0  /* 未加入croutine.c，本项目使用普通Task。 */
#define configUSE_NEWLIB_REENTRANT               0  /* 不为每个TCB分配Newlib重入结构，减少RAM占用。 */
#define configUSE_TRACE_FACILITY                 0  /* 暂不增加Trace所需的TCB字段和相关功能。 */
#define configUSE_STATS_FORMATTING_FUNCTIONS     0  /* 暂不编译依赖字符串格式化的旧式任务统计函数。 */
#define configGENERATE_RUN_TIME_STATS            0  /* 暂不统计任务CPU时间，也未提供独立统计计时器。 */
#define configQUEUE_REGISTRY_SIZE                0  /* 当前没有Queue/Semaphore需要注册给调试器。 */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  0  /* 不在每个TCB中预留应用线程局部指针。 */
#define configUSE_APPLICATION_TASK_TAG           0  /* 不为任务增加应用Tag/Hook字段。 */
#define configUSE_POSIX_ERRNO                    0  /* 不在每个TCB中保存任务级FreeRTOS_errno。 */

/* API inclusion configuration. */
#define INCLUDE_vTaskDelay                       1  /* 编译vTaskDelay()，供LED Task阻塞延时。 */
#define INCLUDE_uxTaskGetStackHighWaterMark      1  /* 启用历史最小剩余任务栈查询，单位为words。 */
#define INCLUDE_uxTaskGetStackHighWaterMark2     0  /* 不重复启用返回configSTACK_DEPTH_TYPE的第二套API。 */
```

#### 模块可用需要满足三层条件

以Queue为例，不能只看一个宏：

```text
第1层：源文件
    queue.c必须参与编译

第2层：相关配置
    如果使用Mutex/递归Mutex/计数信号量，还要启用对应configUSE_*宏

第3层：应用调用
    包含queue.h或semphr.h，并调用对应API
```

当前没有把 `queue.c` 复制进最小固件依赖，也没有加入CMake，因此普通Queue API现在不可用。FreeRTOS没有一个统一的 `configUSE_QUEUES` 宏；普通Queue功能主要由是否编译 `queue.c` 决定，Mutex和Semaphore的扩展能力再由相关配置控制。

类似关系：

| 功能 | 需要的主要源码 | 当前处理 |
| --- | --- | --- |
| Task、Delay、调度 | `tasks.c`、`list.c` | 已准备，后续加入CMake |
| 动态内存 | `heap_4.c` | 已准备，后续加入CMake |
| Queue/Semaphore/Mutex | `queue.c` | 未复制、未加入 |
| Software Timer | `timers.c`，并依赖Queue机制 | 未复制、未加入 |
| Event Group | `event_groups.c` | 未复制、未加入 |
| Stream/Message Buffer | `stream_buffer.c` | 未复制、未加入 |
| Co-routine | `croutine.c` | 未复制、未加入 |
| Task Notification | 实现在 `tasks.c`/TCB中 | 首版主动关闭 |

#### 为什么Task Notification也先关闭

Task Notification不需要单独的 `.c` 文件，是内置在任务控制块和 `tasks.c` 中的轻量同步机制。启用后每个任务的TCB会保存通知值和状态，任务可以用它完成计数、事件位或直接唤醒。

它非常适合后续ISR到任务的通知，但当前LED Task没有任务间通信。先设为0可以让第一版只观察：

```text
Task创建
Ready/Running/Blocked状态
Tick延时
PendSV切换
```

以后真正设计CAN接收或外设事件唤醒任务时，可专门比较Task Notification与Queue/Semaphore，再决定启用方式，而不是提前打开后不验证。

#### Queue、Mutex、Semaphore为何一起讨论

FreeRTOS中的Mutex、递归Mutex和Semaphore复用了Queue底层对象与实现。因此当前没有 `queue.c` 时，下面这些全部关闭：

```c
configUSE_MUTEXES            0
configUSE_RECURSIVE_MUTEXES  0
configUSE_COUNTING_SEMAPHORES 0
configUSE_QUEUE_SETS         0
```

普通二值信号量也依赖 `queue.c`。配置宏为0不是以后永远不用，而是与“最小LED任务不加入queue.c”的源码范围一致。

#### 为什么Software Timer关闭

启用：

```c
configUSE_TIMERS=1
```

不仅需要编译 `timers.c`，还会创建Timer Service/Daemon Task和Timer Command Queue，并要求继续配置：

```text
configTIMER_TASK_PRIORITY
configTIMER_QUEUE_LENGTH
configTIMER_TASK_STACK_DEPTH
```

这会增加一个任务、一个队列、额外Heap和更多调度行为。当前LED Task直接使用 `vTaskDelay()`，不需要软件定时器，所以保持0。

#### Event Group和Stream Buffer为什么显式关闭

V11.3.0 `FreeRTOS.h` 对 `configUSE_EVENT_GROUPS` 和 `configUSE_STREAM_BUFFERS` 的默认值可能为1，但默认启用配置不等于对应源文件已经参与构建。

当前显式写0，可以使配置文件与实际最小源码集合一致，并防止读者误以为这些模块已经接入。后续增加模块时应同时更新：

```text
依赖源码
CMake源文件
配置宏
应用用法
README/学习记录
构建和实机测试
```

#### Newlib重入为什么关闭

```c
#define configUSE_NEWLIB_REENTRANT    0
```

设为1会在每个TCB中增加Newlib重入状态，以便某些C库函数维护每任务上下文，但会显著增加每个任务的RAM占用，并要求确认所用Newlib及系统调用实现。

当前最小Demo不依赖每任务 `errno`、复杂标准IO或Newlib线程本地状态，所以关闭。关闭不代表所有C库函数自动线程安全；以后使用 `printf`、`malloc`、浮点格式化等功能时必须重新评估。

`configUSE_POSIX_ERRNO=0` 同样表示不在每个TCB中保存FreeRTOS POSIX风格的任务级错误号。

#### 首版为什么关闭Trace、统计和Queue Registry

```text
configUSE_TRACE_FACILITY=0
    不向TCB加入Trace所需字段

configUSE_STATS_FORMATTING_FUNCTIONS=0
    不加入依赖字符串格式化的旧统计输出函数

configGENERATE_RUN_TIME_STATS=0
    不统计每个任务的CPU运行时间；当时也没有提供独立高频计时器

configQUEUE_REGISTRY_SIZE=0
    当前没有Queue/Semaphore对象供内核感知调试器注册
```

这些诊断功能有价值，但会引入额外字段、函数、计时源或C库依赖。第一版只保留GDB断点、断言、Heap统计和栈高水位。

#### 为什么必须启用 `vTaskDelay`

```c
#define INCLUDE_vTaskDelay    1
```

它使 `tasks.c` 编译 `vTaskDelay()` 实现。LED Task后续调用它后：

```text
LED翻转
    ↓
vTaskDelay(500 Tick)
    ↓
从Ready List移到Delayed List
    ↓
Idle Task运行
    ↓
Tick到期后LED Task重新Ready
```

若设为0，最小Demo无法使用这条阻塞延时路径。

#### 栈高水位是什么

本步骤启用：

```c
#define INCLUDE_uxTaskGetStackHighWaterMark    1
```

对应API：

```c
UBaseType_t uxTaskGetStackHighWaterMark( TaskHandle_t xTask );
```

“高水位”在这里返回的是任务运行历史中从未使用过的最小栈余量，单位仍是 `StackType_t`，不是字节。

例如LED Task栈深度为128 words，经过代表性负载后返回40：

```text
历史最小未使用栈 = 40 words
                  = 40 × 4
                  = 160字节
```

数值越小表示最坏时刻越接近栈边界；返回0表示没有观察到安全余量，必须立即调查。它不是“当前瞬间空闲多少”，而是自任务创建以来最差情况留下多少。

该API通过扫描栈中仍为 `0xA5` 的区域计算余量。当前栈溢出模式2已经要求创建任务时填充 `0xA5`，因此两者可以共享同一基础机制。

`xTask=NULL` 时查询调用者自己的任务；传入其他有效Task Handle时查询指定任务。不要在1 kHz Tick Hook等高频路径反复扫描栈，应在诊断点或低频监测中使用。

`uxTaskGetStackHighWaterMark2()` 与旧API主要区别是返回类型使用 `configSTACK_DEPTH_TYPE`。当前32位Cortex-M3中两种返回类型都足够表示实际栈深度；首版只启用一个API，避免重复接口。

#### 其他辅助配置

```text
configNUM_THREAD_LOCAL_STORAGE_POINTERS=0
    不在每个TCB中预留应用线程局部指针

configUSE_APPLICATION_TASK_TAG=0
    不给任务增加应用Tag/Hook字段
```

都属于“出现明确使用场景再开启”的TCB扩展。

#### 本步骤人工检查

1. 未加入相应 `.c` 的Timer、Event Group、Stream Buffer和Co-routine均为0。
2. Queue相关源码未加入，因此Mutex、Semaphore扩展和Queue Set均关闭。
3. Task Notification虽然不需要独立源码，首版仍主动关闭。
4. `INCLUDE_vTaskDelay=1`，保证LED Task阻塞延时实现被编译。
5. 只启用一个栈高水位API，并记住返回单位是words。
6. Trace、运行时间统计和格式化统计保持0，不引入额外计时器/字符串依赖。
7. 所有宏位于最终 `#endif` 之前。
8. 功能裁剪完成后，应结合V11.3.0必需宏、默认宏和完整构建检查配置一致性。

当前工程已经采用上述裁剪配置并完成编译、链接和静态尺寸检查；任务栈高水位仍需在运行阶段测量。

### 21.27 配置裁剪宏逐项详解

上一节先给出了适合最小LED Demo的配置。本节不再只解释“当前填0还是1”，而是逐项回答以下问题：

1. 这个宏控制FreeRTOS的什么能力？
2. 打开后会增加什么源码、TCB字段、内核对象或后台任务？
3. 它适合解决什么问题，又不适合解决什么问题？
4. 当前最小Demo为什么暂时关闭或开启？

#### 先区分 `configUSE_*` 与 `INCLUDE_*`

这两类名字的观察角度不同：

```text
configUSE_xxx
    主要控制一种内核机制、内核对象、TCB扩展或诊断能力

INCLUDE_xxx
    主要控制某一个可裁剪API的实现是否编译进tasks.c
```

例如：

```text
configUSE_TASK_NOTIFICATIONS=1
    让每个任务具备任务通知状态和值，并编译任务通知机制

INCLUDE_vTaskDelay=1
    让tasks.c保留vTaskDelay()这个具体API
```

因此，不能把所有这些宏都简单理解为“是否添加一个 `.c` 文件”。有些功能需要独立源文件，有些直接实现在 `tasks.c` 中，有些会扩展每一个TCB，还有些只控制诊断API。

#### `configUSE_TASK_NOTIFICATIONS`：任务通知

任务通知是FreeRTOS直接放在TCB里的轻量通信机制。启用后，每个任务都会拥有通知值和通知状态；默认通常至少有一个通知槽，也可以通过其他配置扩展成通知数组。

常见API包括：

```c
xTaskNotify();
xTaskNotifyFromISR();
xTaskNotifyWait();
ulTaskNotifyTake();
vTaskNotifyGiveFromISR();
```

一个通知值可以按不同方式使用：

```text
当作二值信号量：通知任务“有事件发生”
当作计数信号量：累计发生了多少次事件
当作事件位：一个32位值中的不同bit表示不同事件
直接覆盖或更新一个整数值
```

它的优势是发送目标直接就是某个Task，不需要额外创建Queue或Semaphore对象，通常速度更快、RAM更少。它的限制也来自这种“一对一”关系：通知属于接收任务自身，不适合保存一串带内容的消息，也不适合让多个接收任务竞争同一个通知对象。

典型场景是中断唤醒一个固定处理任务，例如：

```text
CAN RX中断
    ↓ vTaskNotifyGiveFromISR()
CAN处理Task被唤醒
    ↓
Task读取驱动缓冲区并处理数据
```

当前最小LED Demo没有任务间通信和ISR到任务的唤醒需求，因此设为0，让第一轮只学习任务创建、阻塞、Tick和上下文切换。以后启用时，要把它和Semaphore、Queue的用途做对比，而不是因为它“轻量”就替代所有通信对象。

#### `configUSE_MUTEXES`：互斥量

Mutex用于保护“同一时刻只能由一个任务访问”的共享资源，例如共享I2C总线、共享外设、非线程安全的软件模块。

Mutex与普通二值信号量最重要的区别是：

```text
Mutex有所有者
    谁成功Take，原则上就应由谁Give

Mutex支持优先级继承
    高优先级任务等待低优先级任务持有的Mutex时，
    FreeRTOS可临时提升持有者优先级，缓解优先级反转

二值信号量没有所有者
    更适合表达事件，不用于表达资源所有权
```

Mutex底层复用Queue实现，所以需要 `queue.c`。普通Mutex不能在ISR中获取或释放，因为中断不是一个能够拥有Mutex的Task；ISR到Task的事件通知应使用 `FromISR` 版本的Queue、Semaphore或Task Notification API。

当前没有加入 `queue.c`，LED也没有共享资源竞争，因此设为0。

#### `configUSE_RECURSIVE_MUTEXES`：递归互斥量

递归Mutex允许同一个任务连续获取同一个Mutex多次，而不会在第二次获取时把自己阻塞。内核会记录递归获取次数，任务必须按相同次数释放，Mutex才真正变为可用。

```text
Task A第一次Take     递归计数=1
Task A第二次Take     递归计数=2
Task A第一次Give     递归计数=1，其他任务仍不能获取
Task A第二次Give     递归计数=0，Mutex真正释放
```

它适合存在嵌套调用的代码：外层函数和内层函数都可能保护同一个资源。代价是额外的递归计数与更复杂的所有权管理，而且容易掩盖锁设计过深的问题。

它依赖普通Mutex能力和 `queue.c`。当前 `configUSE_MUTEXES=0`，递归Mutex自然也保持0。

#### `configUSE_COUNTING_SEMAPHORES`：计数信号量

计数信号量保存一个从0到设定最大值的计数：

```text
Give一次：计数加1，但不超过最大值
Take一次：计数减1；计数为0时调用者可以阻塞等待
```

典型用途有两类：

1. 统计事件次数，例如ISR连续收到5次事件，处理Task稍后逐次取走。
2. 管理相同资源的可用数量，例如3个相同缓冲块对应初始计数3。

它与Mutex不同：计数信号量没有“所有者”，也没有Mutex的优先级继承语义。它与Queue也不同：它只保存数量，不保存每次事件携带的数据。如果每次事件还要传递CAN帧、指针或结构体，应考虑Queue或“通知负责唤醒、环形缓冲区负责数据”。

计数信号量底层也依赖 `queue.c`。当前最小Demo不需要，所以设为0。

#### `configUSE_QUEUE_SETS`：Queue Set

Queue Set让一个Task能够在同一个阻塞点等待多个Queue或Semaphore中的任意一个变为可用，作用有点像：

```text
等待：命令Queue、数据Queue、停止Semaphore
其中任何一个就绪，都唤醒同一个Task
```

Task醒来后先通过Queue Set确定是哪个成员就绪，再对那个Queue/Semaphore执行真正的接收或获取。

Queue Set适合确实需要“等待多个独立内核对象”的场景，但它会增加额外对象和存储，并让数据流更难追踪。很多嵌入式设计可以通过一个统一的事件Queue、Task Notification事件位或更清晰的任务划分解决，因此不应一开始就默认使用。

它依赖 `queue.c`。当前没有任何Queue或Semaphore，因此设为0。

#### `configUSE_TIMERS`：FreeRTOS软件定时器

这里的Timer不是STM32硬件定时器。FreeRTOS软件定时器由Tick时间和Timer Service Task管理，可以配置为单次或周期触发：

```text
硬件SysTick产生系统Tick
    ↓
FreeRTOS维护软件定时器到期时间
    ↓
到期命令由Timer Service Task处理
    ↓
在Timer Service Task上下文执行回调函数
```

因此，软件定时器回调不是在SysTick ISR里执行，但所有软件定时器回调通常共享同一个Timer Service Task。一个回调长时间阻塞，会推迟其他定时器回调和Timer命令，所以回调应短小，不能执行无限等待。

开启它通常还需要：

```text
timers.c
queue.c
configTIMER_TASK_PRIORITY
configTIMER_QUEUE_LENGTH
configTIMER_TASK_STACK_DEPTH
```

它会创建Timer Service Task和Timer Command Queue，并消耗任务栈、TCB、Queue及Heap。当前LED Task可以直接 `vTaskDelay()`，无需软件定时器，所以设为0。

#### `configUSE_EVENT_GROUPS`：事件组

Event Group是一个由多个bit组成的同步对象，每个bit代表一个布尔事件。例如：

```text
bit0：网络已连接
bit1：参数已加载
bit2：时间已同步
```

任务可以等待：

```text
任意一个bit满足
所有指定bit都满足
醒来时自动清除这些bit，或保留它们
```

它特别适合表达“状态条件的组合”，但不保存事件发生了多少次，也不携带每次事件的数据。若同一个bit在任务处理前被重复设置多次，最终看到的仍然只是“该bit为1”，不是累计次数。

Event Group需要 `event_groups.c`。另外，从ISR操作事件组时要特别检查对应API的延后处理方式以及Timer Service Task相关配置，不能把普通任务API直接放进ISR。

当前最小Demo没有组合状态同步，也未加入 `event_groups.c`，所以显式设为0。

#### `configUSE_STREAM_BUFFERS`：Stream Buffer与Message Buffer

Stream Buffer用于传递连续字节流；Message Buffer构建在同一套实现上，但会额外保留每条消息的边界。

```text
Stream Buffer：发送ABC，再发送DEF，接收端可把它看成连续的ABCDEF字节流
Message Buffer：发送ABC和DEF，接收端仍按两条消息取出
```

默认设计重点是“一个写入者、一个读取者”。如果存在多个写入者或多个读取者，需要由应用额外串行化访问，不能直接把它当成天然的多生产者、多消费者Queue。

它适合UART字节流、网络字节流或变长消息，但固定结构体消息、多个发送任务或对象所有权清晰的场景通常更适合Queue。

该功能需要 `stream_buffer.c`。当前最小Demo没有数据流，所以设为0。

#### `configUSE_CO_ROUTINES`：协程

FreeRTOS Co-routine是为RAM极少的旧式小型处理器准备的轻量协作式机制。多个Co-routine可以共享更少的栈资源，但编程模型、可调用API和调度方式都比普通Task受限，应用还需要安排 `vCoRoutineSchedule()` 的执行。

它和现代语言中的C++20协程也不是同一个概念。对当前Cortex-M3项目，普通Task更直观，也更符合后续CAN、网络和控制任务的工程结构。

开启还需要 `croutine.c` 及Co-routine优先级等配置。当前不使用，所以设为0。

#### `configUSE_NEWLIB_REENTRANT`：每任务Newlib重入状态

GNU Arm工具链常使用Newlib提供部分C标准库。某些C库函数需要保存 `errno`、格式化、区域或其他库内部状态；如果多个Task共用一份状态，可能发生互相干扰。

启用该宏后，FreeRTOS会为每个Task准备一份Newlib重入结构，并在任务切换时让Newlib使用当前Task对应的状态。

要注意三个边界：

1. 它会明显增大每个TCB，具体大小取决于所用Newlib版本和工具链。
2. 它不保证所有第三方函数或所有底层驱动自动线程安全。
3. `printf()` 的串口输出、底层 `_write()` 以及动态内存实现仍可能需要单独的互斥与系统调用适配。

当前最小LED Demo不依赖复杂C库和每任务Newlib状态，因此设为0。以后真正引入标准IO、Newlib动态分配或依赖 `errno` 的库时再重新评估。

#### `configUSE_TRACE_FACILITY`：任务状态快照与Trace基础设施

启用Trace Facility后，内核会增加用于追踪或识别对象的字段和代码，并开放获取系统任务状态快照的能力，例如 `uxTaskGetSystemState()`。快照可包含任务句柄、名称、状态、优先级、栈信息以及在启用运行时间统计时的累计运行计数。

可以把它理解为“为观察内核状态打基础”，但不能理解为：

```text
设为1 ≠ 自动从串口打印任务列表
设为1 ≠ 自动保存完整上下文切换时间线
设为1 ≠ 自动出现图形化Trace界面
```

若要完整事件时间线，通常还要配置Trace宏、记录器、缓冲区以及主机工具。任务快照本身还可能需要遍历任务链表；旧式接口甚至会在收集期间暂停调度，因此不适合在硬实时高频路径中反复调用。

代价包括每个TCB的附加字段、额外代码和采集开销。当前最小Demo已经可以用GDB、断言和栈高水位观察关键行为，所以先设为0。

#### `configUSE_STATS_FORMATTING_FUNCTIONS`：把统计结果格式化成文字

该宏控制的是“文字格式化函数”，不是统计数据本身。相关函数会把任务状态或运行时间数据整理成人类可读的表格，例如任务名、状态、优先级、栈余量、运行计数或百分比。

V11.3.0中可以看到任务列表和运行时间统计的文本接口，包括新接口及兼容旧接口。它们的共同特点是：

```text
先获取原始TaskStatus_t数组或运行时间计数
    ↓
再调用字符串格式化函数
    ↓
写入调用者提供的字符缓冲区
```

这个宏不会自己产生运行时间计数。若想输出CPU占用，还需要 `configGENERATE_RUN_TIME_STATS=1`；若想输出完整任务状态，通常还需要Trace Facility。当前V11.3.0还会检查这些依赖关系：只开格式化而Trace和运行时间统计都关闭，会触发配置错误。

格式化函数会带来字符串处理代码、输出缓冲区、栈占用以及采集期间的调度影响。当前版本的有关实现还要求支持动态内存分配。FreeRTOS源码注释也更建议正式产品读取原始状态数据后，由应用按需输出，避免在内核统计API里进行大段格式化。

最小LED Demo当时既不需要任务表格，也未启用Trace和运行时间计数，所以设为0。当前诊断阶段已把这三个宏设为1，详见第29章。

#### `configGENERATE_RUN_TIME_STATS`：统计各任务实际占用CPU的时间

该宏记录的是每个Task累计运行了多少“高分辨率计数”，不是：

```text
任务从创建到现在经过了多久
任务阻塞了多久
任务用了多少栈
Heap还剩多少
```

工作思路可以简化为：

```text
Task A开始运行时记下计数器
    ↓
切换离开Task A时再次读取计数器
    ↓
两次计数之差累计到Task A
    ↓
所有任务累计值可换算为CPU占用比例
```

例如一次观察窗口中：

```text
Control Task  60000 counts  ≈ 60%
Idle Task     39000 counts  ≈ 39%
LED Task       1000 counts  ≈  1%
```

开启后，应用必须提供一个持续递增的运行时间计数源，V11.3.0会检查类似以下端口宏：

```text
portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
portGET_RUN_TIME_COUNTER_VALUE()
```

这个计数源通常独立于1 kHz系统Tick，并应比Tick具有更高分辨率。还要考虑计数器位宽、回绕、时钟频率以及低功耗时是否继续计数。它会增加每任务计数字段和上下文切换附近的读取开销。

最小LED Demo当时尚未选择和验证独立统计计时器，因此设为0。当前已经用TIM6建立并实机验证10 kHz运行时间计数，详见第29章；仍然不能为了看到一个“CPU百分比”就随便复用会停止、清零或频繁改频的计数器。

#### `configQUEUE_REGISTRY_SIZE`：内核对象名称注册表容量

Queue Registry把Queue、Semaphore或Mutex的句柄与一个便于人阅读的名字关联起来，主要供内核感知调试器或Trace工具显示：

```c
vQueueAddToRegistry( xCanRxQueue, "CAN_RX" );
```

这样调试器可以显示 `CAN_RX`，而不是只显示一个类似 `0x20001234` 的地址。

该宏的数值表示“最多可以注册多少个对象”，不是：

```text
不是Queue能够保存多少条消息
不是系统最多允许创建多少个Queue
不是Queue中每条消息的大小
```

例如系统创建了10个Queue，但只想让调试器观察其中3个，可以设置为3并只注册那3个。值为0时，注册功能被裁掉，`vQueueAddToRegistry()`会成为空操作宏。

注册表本身需要保存对象句柄指针和名称指针；在32位MCU上，每个注册槽通常至少涉及两个指针量级的RAM，最终以当前编译器和Map文件为准。名字字符串的存储还要另外计算。

当前既没有 `queue.c`，也没有Queue/Semaphore/Mutex对象，所以设为0。

#### `configNUM_THREAD_LOCAL_STORAGE_POINTERS`：每任务私有指针槽

该宏决定每个TCB中预留多少个 `void *` 指针槽。相同索引在不同Task中保存不同的值：

```text
Task A：TLS[0] → A自己的设备上下文
Task B：TLS[0] → B自己的设备上下文
```

应用可通过类似以下API设置和读取：

```c
vTaskSetThreadLocalStoragePointer();
pvTaskGetThreadLocalStoragePointer();
```

典型用途是保存每任务日志上下文、库实例、错误上下文或任务私有数据指针，避免建立一个“Task Handle到上下文”的全局查找表。

它只保存指针，不会自动完成以下操作：

```text
不会替你分配指针所指向的对象
不会替你释放对象
不会检查对象生命周期
不等同于C11的_thread_local或编译器TLS
不等同于configUSE_NEWLIB_REENTRANT
```

在Cortex-M3上一个指针通常为4字节，所以设置为N会让每个TCB增加大约 `4 × N` 字节，不包含指针指向的数据。例如6个Task、每个2个槽，仅指针数组合计约增加：

```text
6 × 2 × 4 = 48字节
```

实际布局还可能有对齐影响。团队还必须统一每个索引的含义，否则不同模块会争用同一个槽。当前LED Task没有私有库上下文，所以设为0。

#### `configUSE_APPLICATION_TASK_TAG`：每任务应用Tag/Hook

启用后，每个TCB可以保存一个由应用设置的Task Hook函数指针。应用可给不同Task设置不同Hook，并在需要时通过FreeRTOS提供的接口调用当前或指定任务的Hook。

它可用于很轻量的任务标记、测试注入或特定追踪钩子，但它不是通用的任务参数存储区，也不是每次上下文切换都会自动完成全部业务逻辑的机制。若只是创建任务时传入一个上下文，应优先理解 `xTaskCreate()` 的 `pvParameters`；若要保存多个任务私有指针，再评估TLS Pointer。

打开会给每个TCB增加相应字段。当前没有Task Hook设计，所以设为0。

#### `configUSE_POSIX_ERRNO`：每任务FreeRTOS错误号

启用后，每个TCB会保存一份任务级 `FreeRTOS_errno`，避免不同Task共用同一个FreeRTOS POSIX风格错误号时互相覆盖。

它不等于打开完整POSIX兼容层，也不应和Newlib自己的 `errno` 重入机制混为一谈：

```text
configUSE_POSIX_ERRNO
    主要控制FreeRTOS任务级FreeRTOS_errno字段

configUSE_NEWLIB_REENTRANT
    主要控制每任务Newlib重入结构及其中的C库状态
```

当前最小Demo不使用FreeRTOS POSIX错误码，所以设为0。

#### `INCLUDE_vTaskDelay`：是否编译相对延时API

设为1后，`tasks.c` 保留：

```c
void vTaskDelay( const TickType_t xTicksToDelay );
```

它让当前Task相对“调用这一刻”阻塞指定Tick，并把CPU让给其他Ready Task。它不会忙等，也不保证每次循环的起始时刻绝对等间隔；若任务自身每轮执行时间会变化，周期也会随之漂移。

```text
vTaskDelay()       适合简单阻塞延时
vTaskDelayUntil()  更适合固定周期任务，但由另一个INCLUDE宏控制
```

当前LED Task要用阻塞延时观察调度器，因此必须设为1。

#### `INCLUDE_uxTaskGetStackHighWaterMark`：旧返回类型的栈高水位API

设为1后，可调用：

```c
UBaseType_t uxTaskGetStackHighWaterMark( TaskHandle_t xTask );
```

它扫描任务栈中仍保持初始化填充值的区域，返回该任务运行历史上的“最小未使用栈深度”，单位是 `StackType_t` 个数，在当前Cortex-M3上也就是words，不是字节。

它属于栈容量诊断，与 `configUSE_TRACE_FACILITY` 相互独立。也就是说，即使Trace Facility为0，只要该 `INCLUDE` 宏为1，仍然可以使用栈高水位API。

当前启用它，是为了在最小Demo跑起来后由用户在GDB中低频查询栈余量。

#### `INCLUDE_uxTaskGetStackHighWaterMark2`：使用栈深度类型的新版返回接口

设为1后，可调用：

```c
configSTACK_DEPTH_TYPE uxTaskGetStackHighWaterMark2( TaskHandle_t xTask );
```

它与前一个API检查的是同一个概念，主要区别是返回类型改成 `configSTACK_DEPTH_TYPE`。这在某些平台上可以避免 `UBaseType_t` 与栈深度类型宽度不一致。

当前Cortex-M3最小Demo只保留一个高水位API以减少接口重复，所以旧接口设为1、`...HighWaterMark2` 设为0。以后若工程统一采用新版返回类型，可以切换，不需要两个都开。

#### 把整组宏放回同一张图理解

```text
任务通信与同步
├─ Task Notification        configUSE_TASK_NOTIFICATIONS
├─ Mutex                    configUSE_MUTEXES
├─ Recursive Mutex          configUSE_RECURSIVE_MUTEXES
├─ Counting Semaphore       configUSE_COUNTING_SEMAPHORES
├─ Queue Set                configUSE_QUEUE_SETS
├─ Event Group              configUSE_EVENT_GROUPS
└─ Stream/Message Buffer    configUSE_STREAM_BUFFERS

内核服务与旧式机制
├─ Software Timer           configUSE_TIMERS
└─ Co-routine               configUSE_CO_ROUTINES

每任务TCB扩展
├─ Newlib状态               configUSE_NEWLIB_REENTRANT
├─ TLS指针槽                configNUM_THREAD_LOCAL_STORAGE_POINTERS
├─ Application Task Tag     configUSE_APPLICATION_TASK_TAG
└─ FreeRTOS errno           configUSE_POSIX_ERRNO

观察与统计
├─ 原始任务状态/Trace基础   configUSE_TRACE_FACILITY
├─ CPU运行计数              configGENERATE_RUN_TIME_STATS
├─ 把数据排成文本           configUSE_STATS_FORMATTING_FUNCTIONS
└─ Queue对象显示名称        configQUEUE_REGISTRY_SIZE

按需编译的任务API
├─ 相对阻塞延时             INCLUDE_vTaskDelay
├─ 栈高水位旧返回类型       INCLUDE_uxTaskGetStackHighWaterMark
└─ 栈高水位栈深度返回类型   INCLUDE_uxTaskGetStackHighWaterMark2
```

最小Demo的裁剪原则不是“所有功能都不要”，而是：

```text
只打开当前Demo会实际调用、能够构建、能够观察并能够验证的功能；
每增加一个宏，就同时确认源码、依赖配置、RAM/Flash代价、API用法和实机测试。
```

这些宏都不是调度器能够启动的全部必要条件。当前最小Demo最关键的是Task、List、Cortex-M3 Port、Heap、Tick/PendSV/SVC映射、必要Hook，以及应用确实调用的 `vTaskDelay()`；本节其余大部分功能都可以在调度器运行后按学习进度逐个加入。

### 21.28 Trace和运行统计是否需要串口或Shell

结论先说：

```text
这三个功能可以用于本项目，也很适合后续学习和调试；
最小调度阶段设为0只是为了控制变量，不是因为它们不能用；
当前已经按第29章的方案启用并完成实机验证。
```

它们不强制要求串口，也不会自动提供Shell。FreeRTOS Kernel负责生成数据，至于在哪里查看，由应用选择。

#### 三个宏各自只完成流水线中的一段

```text
configUSE_TRACE_FACILITY
    允许内核提供任务状态等原始数据
    典型入口：uxTaskGetSystemState()
                    │
                    ▼
configGENERATE_RUN_TIME_STATS
    为每个任务累计实际Running时间计数
    还需要应用提供独立的自由运行计数器
                    │
                    ▼
configUSE_STATS_FORMATTING_FUNCTIONS
    把原始数据转换成ASCII文字表格
    典型入口：vTaskListTasks()、vTaskGetRunTimeStatistics()
                    │
                    ▼
应用决定如何输出
    GDB查看内存 / UART / SWO / RTT / USB / 网络
```

所以，即使三个宏都设为1，FreeRTOS也只会在应用调用对应API时，把结果写进应用提供的字符缓冲区。它不知道项目用的是USART1还是USART2，也不知道波特率、USB虚拟串口、终端窗口或命令名称，更不会自行调用STM32串口驱动。

#### 不使用串口也能先观察

最简单的学习方法可以完全不做Shell：

```text
诊断Task或临时测试代码
    ↓
调用vTaskListTasks()把任务表写入字符数组
    ↓
在该调用之后打GDB断点
    ↓
通过GDB查看字符数组内容
```

这条路径不需要串口接收、命令解析和终端程序，可以先验证：

1. 能否看到LED Task和Idle Task。
2. Task状态、优先级和栈高水位是否合理。
3. 输出缓冲区是否足够。

之后也可以让一个低优先级诊断Task每隔几秒生成一次报告，再通过UART主动输出。这仍然不是Shell，因为PC端只能被动接收，不能发送命令选择报告。

#### 做成Shell后确实类似RT-Thread命令窗口

如果加入命令行层，使用体验可以做得类似RT-Thread `msh`：

```text
PC串口终端输入：task
        ↓
UART接收中断只接收字符并通知CLI Task
        ↓
CLI Task解析出task命令
        ↓
调用vTaskListTasks()
        ↓
把生成的字符缓冲区通过UART发回终端
```

再例如：

```text
task      查看任务状态、优先级、栈高水位
runtime   查看各任务累计运行计数和CPU占用比例
heap      查看FreeRTOS Heap剩余量和历史最小剩余量
```

但这个Shell不是上述三个宏提供的。它还需要应用自己解决：

```text
UART初始化
UART RX中断或DMA
接收环形缓冲区
ISR到CLI Task的通知
命令行编辑和结束符处理
命令解析与分发
并发输出保护
发送超时和输出缓冲区大小
```

RT-Thread把FinSH/msh作为一套现成组件提供，所以用户容易把“任务信息命令”和“内核统计能力”看成一个整体。FreeRTOS Kernel的设计更精简，它本身不集成这种交互Shell。FreeRTOS生态中存在独立的CLI组件，但它仍然需要接入具体传输接口和命令，当前移植阶段没有必要提前加入。

#### 为什么最小调度阶段不立即全部打开

原因是学习阶段需要控制变量。如果调度器、任务、运行时间计时器、`snprintf()`、大字符缓冲区和UART Shell一次全部加入，出现故障时很难判断问题属于哪一层：

```text
调度器没有启动？
PendSV/SysTick映射错误？
统计计数器没有运行或回绕？
字符缓冲区太小？
格式化消耗的Task栈太大？
pvPortMalloc()失败？
UART发送阻塞？
CLI接收丢字符？
```

而且V11.3.0本地源码明确体现了以下依赖：

1. `uxTaskGetSystemState()` 需要 `configUSE_TRACE_FACILITY=1`。
2. `vTaskListTasks()` 需要Trace Facility和Stats Formatting同时开启。
3. `vTaskGetRunTimeStatistics()` 还需要Run Time Stats同时开启。
4. 格式化接口依赖 `snprintf()`，可能增加Flash和调用任务的栈消耗。
5. 当前格式化实现会为 `TaskStatus_t` 数组申请动态内存，因此还依赖动态内存支持，并存在申请失败的可能。
6. 获取完整系统任务状态会遍历任务列表并影响调度时序，因此只适合作为低频诊断功能，不能在硬实时控制路径中频繁调用。

这不是说它们危险或不值得用，而是应该把它们当成最小调度Demo之后的一个独立学习实验。

#### 推荐的学习顺序

```text
第1步：最小调度Demo
    LED Task + Idle Task正常运行
    vTaskDelay()、SysTick、PendSV已经实机验证

第2步：只加入任务状态观察
    configUSE_TRACE_FACILITY=1
    学习TaskStatus_t和uxTaskGetSystemState()
    先通过GDB查看原始数组

第3步：加入文字任务表
    configUSE_STATS_FORMATTING_FUNCTIONS=1
    学习vTaskListTasks()、输出缓冲区、snprintf和栈/Heap代价
    先用GDB看字符串，再考虑UART输出

第4步：加入CPU运行时间统计
    选择并验证一个独立自由运行计数器
    configGENERATE_RUN_TIME_STATS=1
    学习运行计数和CPU百分比的含义

第5步：如果确有交互需要，再做UART CLI/Shell
    task、runtime、heap等命令
```

这个顺序既能让学习者真正用上FreeRTOS的观察能力，又能保证每次只新增一个故障来源。

当前项目已经按这条路径完成：Console提供 `task`和 `heap`入口，诊断模块生成任务状态、运行时间和Heap报告，TIM6提供独立计数源；最终结构与验收结论见第29章。

#### 统计功能与实时性的边界

任务列表和CPU占用适合回答：

```text
有哪些Task？
它们大致处于什么状态？
优先级是多少？
历史最小栈余量是多少？
一段时间内各Task大约占用了多少CPU？
```

它们不适合证明：

```text
某个控制Task每一次都在截止时间前完成
某个中断的最坏响应时间一定是多少
系统不存在任何短时抖动
栈和Heap在所有未来路径上绝不会耗尽
```

CPU占用是累计统计，可能掩盖很短但很严重的延迟尖峰。硬实时路径仍需要GPIO翻转配合示波器、DWT周期测量、Trace时间线或明确的最坏执行时间分析。

### 21.29 Queue调试名称、任务私有指针和Task Hook到底是什么

这三个配置都不是调度器的基本功能。要真正理解它们，不能只记名称，必须看到它们分别把什么东西放进RAM：

```text
configQUEUE_REGISTRY_SIZE
    在queue.c中创建一个全局“对象句柄→名字”数组

configNUM_THREAD_LOCAL_STORAGE_POINTERS
    在每一个Task的TCB中创建一个void *数组

configUSE_APPLICATION_TASK_TAG
    在每一个Task的TCB中创建一个函数指针
```

#### Queue本来为什么没有方便阅读的名称

创建Queue后，应用拿到的是 `QueueHandle_t`。它在调试时通常表现为一个地址：

```text
xCanRxQueue = 0x20001240
xLogQueue   = 0x20001318
```

这些地址对CPU足够，因为它可以根据地址找到Queue控制结构；但人看到 `0x20001240` 并不知道这是CAN接收Queue、日志Queue还是其他对象。

Task情况不同。调用 `xTaskCreate()` 时本来就会传入Task名称，因此调试器通常能够从TCB中看到 `LED_Task`、`IDLE` 等名字。Queue、Semaphore和Mutex底层共用Queue结构，却没有同样的固有应用名称字段。

“Queue调试名称”就是应用额外给句柄贴的一张人类可读标签：

```text
0x20001240 → "CAN_RX_QUEUE"
0x20001318 → "LOG_QUEUE"
0x20001400 → "SPI_MUTEX"
```

名字不会改变对象行为：

```text
有没有名字都不影响Queue收发
名字不参与任务调度
名字不参与Queue容量计算
名字主要供调试器、Trace工具或诊断代码显示
```

#### `configQUEUE_REGISTRY_SIZE=5`在RAM里实际创建了什么

V11.3.0的 `queue.c` 中，每个注册项实际保存两个指针：

```c
typedef struct
{
    const char * pcQueueName;
    QueueHandle_t xHandle;
} QueueRegistryItem_t;
```

当配置为5时，内核创建的概念结构是：

```c
QueueRegistryItem_t xQueueRegistry[ 5 ];
```

假设应用注册了三个对象，RAM中的逻辑内容类似：

```text
xQueueRegistry[0]
├─ pcQueueName → "CAN_RX_QUEUE"
└─ xHandle     → 0x20001240

xQueueRegistry[1]
├─ pcQueueName → "LOG_QUEUE"
└─ xHandle     → 0x20001318

xQueueRegistry[2]
├─ pcQueueName → "SPI_MUTEX"
└─ xHandle     → 0x20001400

xQueueRegistry[3]：空
xQueueRegistry[4]：空
```

所以数值5的准确含义是：这个名字查询表有5个槽。系统仍然可以创建第6、第7个Queue，只是它们不能同时都出现在这个5槽注册表中。

在32位Cortex-M3上，每个注册项包含两个4字节指针，因此5个槽的数组通常约为：

```text
5 × (4 + 4) = 40字节
```

这是注册表本身的近似RAM，不包含Queue对象，也不包含名称字符串实际存放的空间，最终仍以编译器布局和Map文件为准。

#### 调试名称由谁注册、谁读取

教学示例：

```c
QueueHandle_t xCanRxQueue;

xCanRxQueue = xQueueCreate( 8, sizeof( CanFrame_t ) );

if( xCanRxQueue != NULL )
{
    vQueueAddToRegistry( xCanRxQueue, "CAN_RX_QUEUE" );
}
```

执行 `vQueueAddToRegistry()` 后，内核只保存：

```text
Queue Handle
名称字符串的地址
```

它不会复制名称字符串内容。因此使用字符串字面量或其他具有足够长生命周期的存储最安全。下面这种局部数组思路有风险：函数返回后，数组生命周期结束，注册表留下的名称指针可能失效。

调试器或应用可以根据Handle查名称：

```c
const char * pcName = pcQueueGetName( xCanRxQueue );
```

内核感知调试器则会直接识别 `xQueueRegistry`，把对象地址显示成 `CAN_RX_QUEUE`。这与编程时给C变量起名不同：C变量名在优化或作用域变化后未必方便调试器追踪，而注册名称作为运行期数据明确保留在目标RAM/Flash指针关系中。

如果注册表5个槽已经全部占满，又注册第6个不同对象，V11.3.0的 `vQueueAddToRegistry()` 没有返回值；找不到空槽时，该对象不会被加入注册表。它的Queue功能仍正常，只是没有这个调试标签。

同一个Handle再次注册时，V11.3.0会更新它对应的名称。删除Queue时，内核在启用注册表的配置下会执行注销；也可理解 `vQueueUnregisterQueue()` 和手动诊断场景，但不要在对象已经无效后继续保留悬空注册关系。

#### 为什么Queue、Semaphore、Mutex都能注册在同一张表

FreeRTOS的Semaphore和Mutex底层复用了Queue实现，因此它们的Handle都能交给：

```c
vQueueAddToRegistry();
```

例如：

```text
数据Queue       → "CAN_RX_QUEUE"
二值Semaphore   → "ADC_DONE_SEM"
Mutex           → "SPI_BUS_MUTEX"
```

这里的名称只帮助人判断“这个内核对象是什么”，不会让FreeRTOS自动判断对象用途是否正确。

#### 什么叫每个Task的“私有指针槽”

当：

```c
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  2
```

V11.3.0会在每一个TCB中加入：

```c
void * pvThreadLocalStoragePointers[ 2 ];
```

重点是“每一个TCB都有自己的一份数组”，不是所有Task共享一个两元素数组：

```text
LED Task的TCB
├─ TLS[0] → LED任务自己的LogContext
└─ TLS[1] → LED设备上下文

CAN Task的TCB
├─ TLS[0] → CAN任务自己的LogContext
└─ TLS[1] → CAN设备上下文

MQTT Task的TCB
├─ TLS[0] → MQTT任务自己的LogContext
└─ TLS[1] → MQTT连接上下文
```

虽然大家都使用索引0，但每个Task读取到的地址可以不同，这就是“Task Local”的含义。

“槽”只是一个能放指针数值的位置。槽里通常保存某个对象的地址，而不是把整个对象复制进TCB：

```text
TLS槽本身：0x20002000       一个地址
                 │
                 ▼
真正的LogContext对象
├─ moduleName
├─ logLevel
└─ errorCount
```

#### 私有指针槽解决了什么问题

假设有一个所有Task都会调用的日志函数：

```c
void LogMessage( const char * pcMessage );
```

这个函数只收到了消息文字，没有收到“当前是谁在打印”“它使用哪个模块名”“它累计了多少错误”。常见解决办法有三种：

```text
办法1：每次调用都传入Context指针
LogMessage( &xCanContext, "timeout" );

办法2：建立全局Task Handle到Context的查找表
先取得当前Task Handle，再遍历全局映射

办法3：把Context指针放进当前Task自己的TLS槽
公共日志函数根据当前TCB直接取TLS[0]
```

TLS教学示例：

```c
enum
{
    TLS_INDEX_LOG_CONTEXT = 0,
    TLS_INDEX_DRIVER_CONTEXT = 1
};

/* 在某Task初始化时，把该Task自己的上下文地址放入槽0。 */
vTaskSetThreadLocalStoragePointer(
    NULL,
    TLS_INDEX_LOG_CONTEXT,
    &xMyLogContext );

/* 公共函数取得“当前正在运行Task”的槽0。 */
LogContext_t * pxContext =
    pvTaskGetThreadLocalStoragePointer(
        NULL,
        TLS_INDEX_LOG_CONTEXT );
```

这里的 `NULL` 表示当前调用Task。也可以传入其他有效的Task Handle，设置或查询指定Task的槽。

它适合第三方库上下文、日志上下文、协议实例、每任务统计对象等“公共代码需要根据当前Task找到自己的数据”的场景。

#### TLS槽不负责什么

FreeRTOS只保存一个地址，不会自动管理地址指向的对象：

```text
不会调用pvPortMalloc()创建对象
不会复制结构体内容
不会判断指针是否已经失效
不会在普通配置下替应用释放对象
不会阻止不同模块错误地占用同一个索引
```

因此应用需要自己保证：

1. 指针指向的对象在使用期间仍然存在。
2. Task删除前后正确处理对象生命周期。
3. 全工程统一索引含义，例如索引0始终是Log Context。
4. 不访问大于或等于 `configNUM_THREAD_LOCAL_STORAGE_POINTERS` 的索引。

#### TLS槽与局部变量、`pvParameters`有什么区别

```text
Task局部变量
    存在于Task栈中
    任务函数和它调用的下层函数可以正常访问
    最简单，能用局部变量时优先使用

xTaskCreate()的pvParameters
    创建Task时传入一个void *启动参数
    Task入口函数收到它后可以长期保存和使用
    适合把一个主要上下文交给Task

Thread Local Storage Pointer
    指针存放在TCB中
    公共库代码可根据当前Task查询，不必沿每一层函数参数传递
    一个Task可以拥有多个约定索引的指针
```

如果LED Task只需要一个GPIO信息，通过 `pvParameters` 或普通局部状态就足够，不需要TLS。TLS并不是“写FreeRTOS Task就必须使用”的功能。

#### TLS配置值如何影响RAM

在Cortex-M3上一个 `void *` 通常是4字节。若：

```text
configNUM_THREAD_LOCAL_STORAGE_POINTERS = 3
Task数量 = 6
```

仅这些指针数组大约增加：

```text
3槽 × 4字节 × 6个Task = 72字节
```

这还不包含指针指向的真正对象。即使某个Task一个槽也不用，只要宏是3，它的TCB仍会预留3个槽。

#### 什么叫给每个Task保存Application Task Hook函数指针

普通数据指针指向数据；函数指针指向一段可以执行的代码。

V11.3.0定义的Task Hook函数类型是：

```c
typedef BaseType_t ( * TaskHookFunction_t )( void * arg );
```

也就是说，这类函数：

```text
接收一个void *参数
返回一个BaseType_t结果
```

启用：

```c
#define configUSE_APPLICATION_TASK_TAG  1
```

会在每个TCB中增加：

```c
TaskHookFunction_t pxTaskTag;
```

假设有两个Hook函数：

```c
static BaseType_t LedDiagnosticHook( void * pvArgument );
static BaseType_t CanDiagnosticHook( void * pvArgument );
```

内存关系可以理解为：

```text
LED Task的TCB
└─ pxTaskTag → LedDiagnosticHook()的代码地址

CAN Task的TCB
└─ pxTaskTag → CanDiagnosticHook()的代码地址
```

因此，同一套调用API可以根据目标Task，转而调用不同的应用函数。

#### Task Hook由谁设置、由谁调用

先设置函数指针：

```c
vTaskSetApplicationTaskTag(
    xLedTaskHandle,
    LedDiagnosticHook );
```

此时只完成了：

```text
xLedTaskHandle对应TCB的pxTaskTag
    = LedDiagnosticHook函数地址
```

函数并没有执行。要调用它，应用需要明确调用：

```c
BaseType_t xResult;

xResult = xTaskCallApplicationTaskHook(
    xLedTaskHandle,
    &xDiagnosticRequest );
```

FreeRTOS内部会执行类似：

```c
xResult = pxTCB->pxTaskTag( pvParameter );
```

如果对应Task没有设置Hook，V11.3.0返回 `pdFAIL`。

还可以使用：

```c
xTaskGetApplicationTaskTag();
xTaskGetApplicationTaskTagFromISR();
```

读取某个TCB里保存的函数指针。注意，存在 `FromISR` Getter不代表可以随意在ISR里执行复杂Hook；中断可调用哪些代码仍然必须遵守ISR规则。

#### 最容易误解的一点：Task Hook不会自动执行

只把函数地址保存进TCB，不会导致它在以下时刻自动运行：

```text
Task创建时不会自动运行
Task每次被调度时不会自动运行
Task进入Blocked时不会自动运行
Task删除时不会自动运行
Tick到来时不会自动运行
```

除非应用或某个明确配置的Port/Trace机制主动读取或调用它，否则它只是TCB中的一个函数地址。

而且：

```text
Task A调用xTaskCallApplicationTaskHook(Task B, ...)
```

并不会切换到Task B执行。Hook函数仍然立即运行在调用者Task A的上下文中，只是FreeRTOS从Task B的TCB里取出了要调用的函数地址。

因此不要把它误认为“给Task B发消息”或“要求Task B稍后处理事件”。如果希望Task B在自己的上下文中处理事件，应使用Task Notification、Queue、Semaphore等同步通信机制。

#### Application Task Tag与其他Hook、参数、TLS的区别

```text
Application Task Tag
    每个Task一个函数指针
    由应用明确调用时执行
    适合轻量测试、诊断或特定Trace扩展

pvParameters
    Task创建时传入的一个数据指针
    不是函数指针

Thread Local Storage Pointer
    每个Task可有多个数据指针槽
    读取后由应用决定如何使用数据

Idle Hook / Tick Hook / Malloc Failed Hook
    由FreeRTOS在明确的全局内核事件发生时调用
    不是每个Task各保存一个不同函数

Task Notification / Queue
    用来通知Task并让Task在自己的上下文中处理事件
    不是直接调用另一个Task TCB里的函数地址
```

`configUSE_APPLICATION_TASK_TAG=1` 通常会让每个TCB增加一个函数指针，在当前32位Cortex-M3上通常约4字节。它只有一个槽，不是一个函数列表，也不能携带持久数据；调用时的 `void *` 参数由调用者临时提供。

#### 当前最小Demo为什么三个都关闭

```text
configQUEUE_REGISTRY_SIZE=0
    当前没有加入queue.c，也没有Queue/Semaphore/Mutex可命名

configNUM_THREAD_LOCAL_STORAGE_POINTERS=0
    LED Task没有公共库上下文，不需要在TCB中查找私有数据

configUSE_APPLICATION_TASK_TAG=0
    没有设计任何需要按Task选择的诊断回调
```

它们并不是“不好”或“永远不用”，而是只有出现具体问题时才有意义：

```text
调试器里Queue地址太难辨认
    → 开Queue Registry并注册关键对象

公共库函数需要取得当前Task自己的上下文
    → 评估Thread Local Storage Pointer

确实需要为不同Task挂接不同的可调用诊断函数
    → 评估Application Task Tag
```

仅仅因为FreeRTOS提供了这些宏就全部开启，会让TCB和全局RAM增加，却没有产生可验证的收益。

### 21.30 实操第7步：完整配置静态审查与应用Hook

当前 `FreeRTOSConfig.h` 已经覆盖最小Demo所需的主要配置组：

```text
时钟与Tick
调度策略
动态内存
Cortex-M3中断阈值
SVC/PendSV/SysTick直接路由
断言与故障Hook
可选内核功能裁剪
按需编译的Task API
```

本步骤先进行只读静态审查，再由用户亲自补充应用层Hook，不修改FreeRTOS内核源码。

#### 当前配置静态审查结果

已使用当前GNU Arm工具链，对下面四个最小内核源文件执行不生成 `.o` 文件的 `-fsyntax-only` 检查：

```text
tasks.c   通过
list.c    通过
heap_4.c  通过
port.c    通过
```

这证明当前配置头满足这四个源文件的预处理和C语法要求，包括：

1. `configTICK_TYPE_WIDTH_IN_BITS` 已选择32位Tick。
2. `configMINIMAL_STACK_SIZE`、`configMAX_PRIORITIES` 等必需宏已定义。
3. 动态分配为1、静态分配为0，不会出现两种分配方式同时关闭。
4. 递归Mutex与普通Mutex均为0，不存在依赖冲突。
5. Stats Formatting、Trace和Run Time Stats均为0，不存在统计功能依赖冲突。
6. Run Time Stats关闭，因此当前无需提供运行时间计数器宏。
7. Software Timer关闭，因此当前无需提供Timer Task的优先级、Queue长度和栈深度。
8. `configCHECK_HANDLER_INSTALLATION=1` 时已经同时定义 `configASSERT()`。

#### 为什么当前V11.3.0没有写 `configKERNEL_INTERRUPT_PRIORITY`

很多旧教程的Cortex-M3配置会出现：

```c
#define configKERNEL_INTERRUPT_PRIORITY ...
```

但当前移植使用的V11.3.0 `portable/GCC/ARM_CM3/port.c` 已经在端口层定义最低中断优先级值，并在启动调度器时直接把：

```text
PendSV 设为最低优先级
SysTick设为最低优先级
SVCall设为最高优先级
```

因此不要从旧教程机械复制已经不被当前端口读取的配置宏。判断一个宏是否需要，应检查当前版本端口源码，而不是只比较其他工程的 `FreeRTOSConfig.h` 长短。

当前真正需要应用配置的是：

```c
configMAX_SYSCALL_INTERRUPT_PRIORITY
```

它决定使用FreeRTOS临界区和 `FromISR` API时的BASEPRI边界，与PendSV/SysTick被端口设置到最低优先级不是同一个问题。

#### 语法检查通过为什么还不能说明最终能链接

C工程要经过不同阶段：

```text
预处理
    展开#include和#define
        ↓
编译
    检查每个.c的语法，生成.o
        ↓
链接
    把所有.o中的函数引用与函数定义对应起来
```

`-fsyntax-only` 只检查到编译阶段。只要编译器已经看到函数声明，下面这样的调用就能通过语法检查：

```c
vApplicationMallocFailedHook();
```

但最终链接时，链接器还要找到这个函数的真实函数体。如果整个工程只有声明和调用，没有定义，就会报告类似：

```text
undefined reference to `vApplicationMallocFailedHook'
```

当前工程正处在这个状态：

```text
configUSE_MALLOC_FAILED_HOOK=1
    heap_4.c会调用vApplicationMallocFailedHook()

configCHECK_FOR_STACK_OVERFLOW=2
    tasks.c会调用vApplicationStackOverflowHook()

工程中的应用.c
    目前没有这两个函数的定义
```

所以现在不是把两个宏改回0，而是由应用层提供这两个已经主动启用的安全诊断入口。

#### Hook为什么必须由应用提供

FreeRTOS不知道产品遇到内存故障后应该：

```text
停机等待GDB
点亮故障LED
保存故障记录
触发看门狗复位
进入安全状态
```

这些都是产品策略，不应该由通用内核替项目决定。因此内核只规定函数名称和参数，在故障发生时调用；函数体由应用编写。

这和C标准库回调的思想相似：

```text
FreeRTOS内核定义“什么时候调用”
应用定义“调用后做什么”
```

#### 建议用户在RTOS集成层创建独立Hook源文件

为了不把故障处理塞进 `main.c` 或堆在 `User/` 根目录，建议用户亲自在RTOS集成层目录创建：

```text
firmware/User/rtos/freertos_hooks.c
```

首版最小实现可由用户写成：

```c
#include "FreeRTOS.h"
#include "task.h"

void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}

void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;

    taskDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
```

这是给用户操作的教学代码，Codex不直接创建或修改该文件。

#### 为什么函数不能写成 `static`

内核的调用来自另外的 `.c` 文件：

```text
heap_4.c → vApplicationMallocFailedHook()
tasks.c  → vApplicationStackOverflowHook()
```

如果应用定义成：

```c
static void vApplicationMallocFailedHook( void )
```

`static` 会把函数限制在当前源文件内部，链接器无法让 `heap_4.c` 找到它。因此两个Hook必须保持外部链接，不能加 `static`，名称和参数类型也必须与头文件声明一致。

#### 两个Hook分别何时进入

`vApplicationMallocFailedHook()` 的路径是：

```text
xTaskCreate()等对象创建函数
    ↓
pvPortMalloc()向heap_4申请内存
    ↓
找不到足够大的连续空闲块
    ↓
返回NULL前调用vApplicationMallocFailedHook()
```

它不仅可能由应用直接分配触发，也可能在创建Task、Queue或其他动态对象时触发，因为这些对象内部也可能调用 `pvPortMalloc()`。

`vApplicationStackOverflowHook()` 的路径是：

```text
发生任务切换
    ↓
FreeRTOS执行栈溢出模式2检查
    ↓
发现栈指针越界或栈末端填充值被破坏
    ↓
调用vApplicationStackOverflowHook(xTask, pcTaskName)
```

参数含义：

```text
xTask
    被检测为栈异常的Task Handle

pcTaskName
    被检测Task的名称指针
```

首版虽然用 `(void)` 避免未使用参数警告，但进入Hook后仍可在GDB中观察参数和调用栈。编译优化可能影响变量显示，因此Debug构建的 `-Og -g3` 更适合这一阶段。

#### 为什么Hook中只关中断并死循环

进入这两个Hook时，系统已经处于异常资源状态：

```text
Malloc Failed
    FreeRTOS Heap已经无法满足本次申请

Stack Overflow
    当前或相关Task的栈可能已经破坏数据
```

因此Hook里不适合继续执行：

```text
printf()复杂格式化
再次pvPortMalloc()
创建新Task或Queue
调用可能阻塞的FreeRTOS API
执行依赖大栈的复杂函数
```

首版死循环的目的不是恢复系统，而是保留现场：

```text
故障发生
    ↓
程序停在具有明确名称的Hook
    ↓
GDB连接或断点命中
    ↓
检查调用栈、Task名称、Heap和TCB
```

`taskDISABLE_INTERRUPTS()` 在当前Cortex-M3端口通过BASEPRI屏蔽受FreeRTOS管理的中断优先级范围。按照当前配置，它不会屏蔽逻辑优先级0～4的最高紧急中断，因此它不等同于PRIMASK意义上的“全局关闭所有可屏蔽中断”。首版没有这类高优先级业务ISR，使用FreeRTOS通用写法即可；以后产品故障冻结策略需要单独设计。

#### 为什么暂时不在Hook里操作LED

故障LED在产品中很有价值，但第一版Hook应先保持依赖最少：

1. LED驱动本身可能依赖尚未确认的初始化状态。
2. 闪灯延时不能再依赖已经异常的调度器。
3. 添加GPIO调用会让Hook验证同时依赖外设驱动。

等两个Hook能通过GDB断点验证后，再学习如何设计不依赖调度器的故障灯或复位策略。

#### Hook接入顺序

1. 亲自创建 `User/rtos/` 目录和 `User/rtos/freertos_hooks.c`。
2. 按上述最小版本实现两个Hook，不修改FreeRTOS内核源码。
3. 暂时不要修改CMake；下一步会单独学习如何把内核源文件、Port、Heap、Hook和头文件目录加入现有CMake target。
4. 完成后核对函数签名、依赖方向和构建结果。

本步骤暂时不会进行最终链接，因为现有CMake尚未纳入FreeRTOS源文件。这正好保留清晰顺序：

```text
配置头完整
    ↓
应用Hook完整
    ↓
CMake加入FreeRTOS
    ↓
解决编译/链接问题
    ↓
创建第一个Task
```

当前工程把Hook放在 `User/rtos/freertos_hooks.c`，函数签名正确，并已随FreeRTOS最小内核完成最终链接。

### 21.31 固件分层：目录必须同时表达层级和功能域

直接创建 `User/freertos_hooks.c` 在编译技术上没有问题，但目录结构表达不出它的职责。随着项目增加CAN、W5500、MQTT、FTP和升级状态机，所有 `.c` 都放在 `User/` 根目录会逐渐产生：

```text
文件属于哪一层不清楚
模块依赖方向不清楚
业务代码容易进入ISR和Hook
第三方内核、驱动和应用修改混在一起
单元测试和后续替换RTOS变得困难
```

因此从第一个FreeRTOS应用侧源文件开始就建立分层规则。

#### 本项目确定的目标分层

```text
firmware/
├─ Libraries/FreeRTOS-Kernel/   第三方RTOS内核
├─ Libraries/CMSIS/             ARM/芯片核心支持
├─ Libraries/FWlib/             STM32标准外设库
└─ User/
   ├─ main.c                    顶层启动入口
   ├─ FreeRTOSConfig.h          全工程FreeRTOS编译配置
   ├─ rtos/                     应用到FreeRTOS的集成层
   │  └─ freertos_hooks.c       应用实现的内核回调
   ├─ app/                      Task入口和业务编排，按功能域继续分层
   │  └─ led/                   LED应用任务，后续创建
   └─ bsp/                      板级支持层，按外设或器件继续分层
      ├─ led/                   LED板级驱动
      ├─ key/                   按键板级驱动，当前未接入
      ├─ exti/                  外部中断板级驱动，当前未接入
      └─ usart/                 串口板级驱动，当前未接入
```

裸机基线最初把LED驱动放在 `User/led/`。当前工程已将 `bsp_led.c/.h`迁移到 `User/bsp/led/`并完成构建回归，同时没有把Task逻辑混入该次分层调整。其他尚未接入构建的历史目录不批量搬迁，真正使用时再按同一规则逐个整理。

#### BSP与通用Driver有什么区别

`BSP` 是 Board Support Package，强调“这块具体开发板怎样连接和初始化硬件”。当前 `bsp_led.c/.h` 固定使用霸道V2上的PB5、PB0、PB1以及STM32F10x标准外设库，因此它与具体板卡绑定，放在 `User/bsp/led/` 最准确。

通用Driver强调“某个外设或器件本身怎样工作”，应尽量不写死具体板卡引脚。例如未来一个可复用的W5500协议驱动可以放在独立Driver层，而“本板使用SPI2、PG9作为CS”属于BSP适配。当前最小工程还没有建立通用Driver层，不为尚未出现的模块提前创建空目录。

因此不能仅凭文件操作了硬件就一律称为Driver，也不能仅凭目录叫 `bsp` 就认为已经分层；关键要看代码是否绑定具体板卡，以及依赖是否从上层指向下层。

#### 各层的职责和允许依赖

```text
应用层 User/app
    │ 调用
    ├──────────────► RTOS集成层 User/rtos
    │
    └──────────────► BSP层 User/bsp/<外设或器件>
                          │ 调用
                          ▼
                 FWlib / CMSIS / 硬件

RTOS集成层
    │ 调用或实现回调
    ▼
FreeRTOS Kernel
```

依赖应主要从上层指向下层：

```text
允许：app调用LED BSP
允许：app调用FreeRTOS Task API
允许：rtos层实现FreeRTOS要求的Hook

禁止：FreeRTOS-Kernel包含User/app头文件
禁止：LED BSP调用具体MQTT或控制业务
禁止：Hook直接编排CAN、MQTT或升级流程
禁止：ISR执行长时间业务处理
```

#### `freertos_hooks.c`为什么属于 `User/rtos/`

两个方向共同决定它的位置：

```text
向下
    它依赖FreeRTOS类型、宏和Hook约定

向上
    它由当前产品决定故障时停机、记录还是复位
```

所以它既不能放入第三方 `Libraries/FreeRTOS-Kernel/`，否则会污染上游源码；也不属于LED、USART等BSP；更不是某一个业务Task。它正是应用与RTOS之间的“胶水代码”，放在 `User/rtos/` 最准确。

#### 为什么 `FreeRTOSConfig.h` 暂时可以留在 `User/` 根目录

`FreeRTOSConfig.h` 是FreeRTOS编译期间全工程都要直接找到的配置入口，不是一个带业务行为的实现 `.c` 文件。当前已经通过 `User/` 包含路径完成静态检查，暂时保留可以减少无意义的路径变更。

以后如果RTOS集成文件明显增多，也可以在一次独立、可验证的目录重构中把它移动到 `User/rtos/`，同时更新CMake包含路径和全部文档。但不应在接入CMake的同一步骤里顺手移动，以免增加排错变量。

#### `main.c`、Hook和Task入口分别应该做什么

```text
main.c
    初始化时钟和必要BSP
    创建顶层Task
    启动调度器
    不承载持续业务循环

User/rtos/freertos_hooks.c
    实现Malloc Failed、Stack Overflow等RTOS回调
    只做最小、安全、可诊断的故障处理
    不承载正常业务流程

User/app/led/app_led_task.c（后续）
    实现LED Task入口
    使用vTaskDelay等RTOS机制组织任务行为
    通过LED BSP操作硬件，不直接重写GPIO底层

User/bsp/led/bsp_led.c
    初始化LED GPIO并提供亮灭、翻转等板级接口
    不知道LED Task是否存在，也不调用应用层
```

#### 分层不是“文件夹越多越好”

分层的判断标准是职责和依赖，不是目录数量。当前LED确实同时具有“硬件驱动”和“应用Task”两种不同职责，因此使用 `User/bsp/led/` 与 `User/app/led/` 两个模块目录是有语义的，并不是为了外观机械增加层级：

```text
一个模块只有明确职责时才创建
上层知道下层，下层不反向知道具体业务
第三方源码保持可替换和可升级
中断、Hook和驱动只做所在层应该做的工作
```

本项目从此遵循：新增实现源文件不得仅为省事直接堆入 `User/` 根目录，也不得使用 `User/led/`、`User/can/` 这种只表达功能、不表达层级的新增路径；必须先判断它属于RTOS集成、应用、BSP或其他明确层次，再按功能域建立下一层目录。该规则已经写入根目录 `AGENTS.md`，供后续Codex和自动化代理强制执行。

### 21.32 实操第8步：把最小FreeRTOS依赖接入CMake

当前已经分别证明：

```text
裸机工程可以构建、烧录和调试
FreeRTOSConfig.h可以通过最小内核语法检查
freertos_hooks.c可以单独通过语法检查
```

但现有 `CMakeLists.txt` 仍然只编译裸机点灯文件，因此现在要把最小FreeRTOS文件真正加入同一个 `firmware` target。本步骤仍不修改 `main.c`，目的是只验证构建系统集成，避免同时引入Task创建问题。

#### CMake在这里解决两个不同问题

CMake必须同时知道：

```text
哪些.c需要编译
    → add_executable()或target_sources()的源文件列表

#include到哪里查找.h
    → target_include_directories()的头文件搜索目录
```

两者不能互相替代：

```text
把tasks.c所在目录加入include路径
    不会自动编译tasks.c

把tasks.c加入源文件列表
    也不会自动让编译器找到FreeRTOS.h和portmacro.h
```

#### 当前最小Demo需要加入的五个源文件

用户应在现有 `add_executable(firmware ...)` 中加入：

```cmake
    # FreeRTOS Kernel V11.3.0 minimal sources.
    Libraries/FreeRTOS-Kernel/tasks.c
    Libraries/FreeRTOS-Kernel/list.c
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c
    Libraries/FreeRTOS-Kernel/portable/MemMang/heap_4.c

    # Application-side FreeRTOS integration.
    User/rtos/freertos_hooks.c
```

加入后的结构应类似：

```cmake
add_executable(firmware
    Libraries/CMSIS/startup/gcc/startup_stm32f103xe.S
    Libraries/CMSIS/system_stm32f10x.c
    Libraries/FWlib/src/stm32f10x_gpio.c
    Libraries/FWlib/src/stm32f10x_rcc.c

    # FreeRTOS Kernel V11.3.0 minimal sources.
    Libraries/FreeRTOS-Kernel/tasks.c
    Libraries/FreeRTOS-Kernel/list.c
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c
    Libraries/FreeRTOS-Kernel/portable/MemMang/heap_4.c

    # Application sources.
    User/bsp/led/bsp_led.c
    User/rtos/freertos_hooks.c
    User/main.c
)
```

源文件在列表中的先后顺序通常不会改变最终链接语义，但按第三方内核、Port/Heap、应用集成、应用源码分组，可以让评审者直接看出分层。

#### 五个源文件为什么一个都不能少

```text
tasks.c
    xTaskCreate()、vTaskDelay()、调度器和Task状态转换

list.c
    Ready、Delayed等内核链表的底层操作

portable/GCC/ARM_CM3/port.c
    启动第一个Task、PendSV上下文切换、SysTick和临界区端口实现

portable/MemMang/heap_4.c
    为动态创建的TCB和Task栈提供pvPortMalloc()/vPortFree()

User/rtos/freertos_hooks.c
    提供当前配置已经要求的Malloc Failed和Stack Overflow Hook定义
```

只加入 `tasks.c` 和 `list.c` 会在链接阶段缺少Port或内存分配实现；只加入内核文件却漏掉Hook，则在相关内核函数保留后可能出现未定义引用。

#### 当前不要加入哪些源文件

不要因为头文件已经复制就把所有模块源文件一起加入：

```text
queue.c
timers.c
event_groups.c
stream_buffer.c
croutine.c
```

当前配置已经关闭这些功能，最小LED Task也不调用它们。CMake源文件集合应与 `FreeRTOSConfig.h` 和当前Demo需求一致。

下面这些也不是C源文件，不能放进编译源列表：

```text
Libraries/FreeRTOS-Kernel/include/CMakeLists.txt
Libraries/FreeRTOS-Kernel/list.c和tasks.c.txt
LICENSE.md
README.md
History.txt
```

`list.c和tasks.c.txt` 是用户学习笔记，不是编译单元；扩展名已经表达了这一点。

#### 需要增加的两个头文件搜索目录

用户应在现有 `target_include_directories(firmware PRIVATE ...)` 中加入：

```cmake
    Libraries/FreeRTOS-Kernel/include
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3
```

形成类似：

```cmake
target_include_directories(firmware PRIVATE
    Libraries/CMSIS
    Libraries/FWlib/inc
    Libraries/FreeRTOS-Kernel/include
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3
    User
    User/bsp/led
)
```

两个目录分别解决不同的包含链：

```text
Libraries/FreeRTOS-Kernel/include
    让编译器找到FreeRTOS.h、task.h、list.h、portable.h等

Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3
    让portable.h继续找到当前端口的portmacro.h
```

`FreeRTOSConfig.h` 当前仍位于 `User/`，而 `User` 已经是包含目录，所以不需要再复制配置头，也不需要修改上游 `FreeRTOS.h`。

`freertos_hooks.c` 目前没有需要被其他模块包含的私有头文件，因此仅把 `.c` 加入源列表即可，暂时不必为了目录对称而添加 `User/rtos` include路径。以后该目录真正出现公共头文件时再添加。

#### 编译器如何沿包含链找到正确端口

以 `tasks.c` 为例：

```text
tasks.c
  ↓ #include "FreeRTOS.h"
FreeRTOS-Kernel/include/FreeRTOS.h
  ↓ #include "FreeRTOSConfig.h"
User/FreeRTOSConfig.h
  ↓ FreeRTOS.h继续包含portable.h
FreeRTOS-Kernel/include/portable.h
  ↓ #include "portmacro.h"
FreeRTOS-Kernel/portable/GCC/ARM_CM3/portmacro.h
```

如果漏掉第一个目录，通常报：

```text
fatal error: FreeRTOS.h: No such file or directory
```

如果漏掉ARM_CM3端口目录，通常报：

```text
fatal error: portmacro.h: No such file or directory
```

如果漏掉 `User`，通常报：

```text
fatal error: FreeRTOSConfig.h: No such file or directory
```

通过报错文件名反推缺失的包含链，比随机添加大量include目录更可靠。

#### 为什么现在仍保留裸机 `main.c`

当前 `main.c` 仍直接配置SysTick进行忙等点灯。把FreeRTOS加入构建后，V11.3.0的 `port.c` 会提供强符号：

```text
SVC_Handler
PendSV_Handler
SysTick_Handler
```

它们会覆盖启动文件中相同名称的弱默认处理函数。当前裸机 `delay_ms()` 只开启SysTick计数器，没有开启SysTick中断，因此在尚未启动调度器时不会主动进入FreeRTOS SysTick Handler。

本步骤保留裸机 `main.c` 的目的不是继续长期使用这种设计，而是先验证：

```text
内核源文件能编译
Port汇编能编译
Hook能进入链接
异常强符号能正确覆盖启动文件弱符号
现有链接脚本仍能容纳新增代码和RTOS Heap
```

这些通过后，下一步再单独改造 `main.c` 和创建 `User/app/led/app_led_task.c`。

#### 接入后执行的构建命令

在 `firmware` 目录中执行一次Fresh配置，确保CMake重新生成GNU Make构建规则：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
```

这不是烧录步骤。本轮只观察：

1. CMake配置是否成功。
2. 五个新 `.c` 是否出现在编译输出或 `build/compile_commands.json`。
3. 是否存在warning或undefined reference。
4. Flash和RAM使用量相较裸机基线增加多少。
5. `firmware.map` 中SVC、PendSV、SysTick最终来自哪里。

#### CMake接入清单

1. 亲自修改 `firmware/CMakeLists.txt` 的源文件列表，加入五个最小源文件。
2. 亲自增加两个FreeRTOS include目录。
3. 不加入Queue、Timer、Event Group等未启用模块。
4. 不修改 `main.c`。
5. 执行Fresh配置和GNU Make构建，把完整错误或成功输出反馈回来。

当前工程的五个最小源文件均已参与编译并完成链接；Newlib GNU-stack链接警告已通过明确的 `noexecstack`链接策略解决。

### 21.33 首次链接结果：编译进入不等于最终保留

用户执行：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
```

CMake配置和GNU Make构建均成功。构建输出明确出现：

```text
tasks.c.obj
list.c.obj
portable/GCC/ARM_CM3/port.c.obj
portable/MemMang/heap_4.c.obj
User/rtos/freertos_hooks.c.obj
```

这证明源文件和include目录接入正确；最终生成 `firmware.elf`、`.hex`、`.bin` 和 `.map`。

#### 当前内存结果与裸机基线对比

```text
                         裸机基线       接入内核但未创建Task       增量
FLASH                    1268 B         2072 B                    +804 B
RAM                      2568 B         2704 B                    +136 B
```

当前构建详细段大小为：

```text
.isr_vector          484 B
.text               1584 B
.data                  4 B
.bss                 140 B
._user_heap_stack   2560 B
```

GNU `size` 的Berkeley汇总把 `.bss` 和链接脚本预留的 `._user_heap_stack` 等未初始化RAM一起汇总为输出中的2700 B `bss`，所以：

```text
data 4 B + 汇总bss 2700 B = RAM 2704 B
```

#### 为什么配置了8 KiB RTOS Heap，RAM却只增加136 B

`heap_4.c` 中的 `ucHeap[]` 只有在 `pvPortMalloc()` 路径被最终固件引用时才需要保留。当前 `main.c` 仍是裸机循环，没有调用：

```text
xTaskCreate()
vTaskStartScheduler()
pvPortMalloc()
```

而编译选项包含：

```text
-ffunction-sections
-fdata-sections
```

链接选项包含：

```text
-Wl,--gc-sections
```

它们组合起来后，每个函数和数据可被放在更细的Section中，链接器会删除从入口和其他已保留符号不可达的Section。

因此发生的是：

```text
heap_4.c已经编译成heap_4.c.obj
        │
        ├─ 证明源码与配置可以编译
        │
        ▼
最终链接发现没有保留路径调用pvPortMalloc()
        │
        ▼
pvPortMalloc()、ucHeap[]和Malloc Failed Hook被垃圾回收
```

所以必须区分三句话：

```text
“文件出现在CMake源列表”
    只说明它被安排参与构建

“文件生成了.obj”
    只说明它已经成功编译

“符号出现在最终ELF/MAP”
    才说明对应代码或数据真正进入固件映像
```

等下一步应用调用 `xTaskCreate()` 后，动态任务创建路径会引用 `pvPortMalloc()`，8 KiB `ucHeap[]` 才会进入最终RAM映像。TCB和任务栈会在运行时从这块已经预留的FreeRTOS Heap内部切分，不是在8 KiB之外再重复静态预留同样大小。

#### 当前哪些FreeRTOS内容已经被保留

ELF符号检查得到：

```text
08000440  xTaskIncrementTick
080005b8  vTaskSwitchContext
08000694  SVC_Handler
080006b8  PendSV_Handler
080006fc  SysTick_Handler
08000728  vApplicationStackOverflowHook
2000001c  xTickCount
2000008c  pxCurrentTCB
```

为什么应用还没启动调度器，这些内容仍被保留？因为启动文件的中断向量表引用 `SVC_Handler`、`PendSV_Handler` 和 `SysTick_Handler`。FreeRTOS `port.c` 提供同名强定义后，从向量表形成了可达路径：

```text
中断向量表
    ├─ SVC_Handler
    ├─ PendSV_Handler ──► vTaskSwitchContext()、pxCurrentTCB
    └─ SysTick_Handler ─► xTaskIncrementTick()、xTickCount
```

这些符号因此不会被 `--gc-sections` 删除。栈溢出检查代码又使 `vApplicationStackOverflowHook()` 被保留。

当前没有出现在最终ELF中的代表性符号包括：

```text
xTaskCreate()
vTaskDelay()
vTaskStartScheduler()
pvPortMalloc()
ucHeap[]
vApplicationMallocFailedHook()
```

它们会在创建任务和启动调度器后沿调用关系被保留。

#### 如何确认异常入口已经覆盖弱默认处理函数

最终ELF中的三个符号类型为全局Text符号：

```text
T SVC_Handler
T PendSV_Handler
T SysTick_Handler
```

这里的 `T` 表示符号位于最终可执行代码Section中并作为全局定义存在。启动文件原来提供的是同名弱默认定义；链接器在存在FreeRTOS强定义时选择强定义，因此当前向量表已经路由到FreeRTOS端口。

但要区分：

```text
ELF符号正确
    证明链接路由正确

实机进入SVC/PendSV/SysTick
    还需要启动调度器并用GDB断点验证
```

当前不应把静态符号检查提前写成“调度器已经运行”。

#### 链接警告来自哪里

唯一警告是：

```text
libc_a-memset.o: missing .note.GNU-stack section implies executable stack
```

`.note.GNU-stack` 是目标文件用于声明“本目标文件是否要求可执行栈”的ELF元数据。当前工具链预编译Newlib库中的 `memset` 目标文件缺少该Section。GNU ld为了兼容旧目标文件，发出警告并把最终ELF的 `GNU_STACK` 程序头标成：

```text
RWE = Read + Write + Execute
```

这条警告不是：

```text
任务栈已经溢出
FreeRTOS Heap配置错误
链接脚本RAM地址错误
freertos_hooks.c写错
```

它是工具链库目标文件的栈权限元数据问题。

#### 为什么已有 `-Wa,--noexecstack` 仍然出现警告

当前编译选项已有：

```text
-Wa,--noexecstack
```

`-Wa` 表示把后面的选项传给汇编器。它会影响当前工程重新汇编的 `.S` 或编译器产生的汇编，但不会重新生成工具链中已经打包好的：

```text
libc_a-memset.o
```

因此项目自己的启动汇编可以带正确标记，预编译Newlib对象仍可能缺少标记。

#### 已验证的处理方式

在最终链接中明确加入：

```text
-Wl,-z,noexecstack
```

含义是：

```text
-Wl,
    把后续参数传给链接器ld

-z noexecstack
    明确最终ELF不要求可执行栈
```

使用当前同一批 `.obj` 临时重链接的验证结果是：

```text
链接退出码：0
原警告：消失
GNU_STACK：由RWE变为RW
```

这比：

```text
-Wl,--no-warn-execstack
```

更合适。后者只是不显示警告，并没有明确改变最终栈权限标记；`-z noexecstack` 则表达了项目真实意图。

当前使用的是普通Cortex-M3端口，不能把ELF的 `GNU_STACK` 标志理解为已经建立了任务级硬件内存隔离。它首先是ELF元数据和链接卫生；真正的任务内存执行权限保护需要MPU端口、内存区域配置和硬件支持。

#### 工程采用的链接处理方式

在 `target_link_options(firmware PRIVATE ...)` 中加入：

```cmake
    -Wl,-z,noexecstack  # 明确最终ELF栈不需要执行权限，并消除预编译Newlib对象的GNU-stack警告
```

建议放在：

```cmake
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-z,noexecstack
```

然后重新执行：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
arm-none-eabi-readelf -W -l build/firmware.elf | Select-String GNU_STACK
```

期望看到：

```text
构建警告：0
GNU_STACK ... RW ...
```

这一步通过后再创建 `User/app/led/app_led_task.c` 并改造 `main.c`，保持“一次只增加一个故障来源”。

最终状态：FreeRTOS最小源文件已进入CMake并成功链接，异常强符号已经确认；`-Wl,-z,noexecstack`已经生效，后续Task创建路径也已进入最终ELF。

### 21.34 深入理解 `-Wl,-z,noexecstack`

这条选项不属于FreeRTOS，也不是C语言宏，而是GCC驱动程序转交给GNU链接器的参数：

```text
-Wl,-z,noexecstack
```

逐段拆开：

```text
-Wl,
    告诉arm-none-eabi-gcc：
    后面的内容不要交给C编译器，转交给链接器ld

-z
    GNU ld的一组ELF链接策略选项入口

noexecstack
    声明最终ELF不要求栈具有代码执行权限
```

逗号是GCC参数转发时的分隔符，因此：

```text
arm-none-eabi-gcc ... -Wl,-z,noexecstack
```

概念上等价于GCC最终调用链接器时传入：

```text
arm-none-eabi-ld ... -z noexecstack
```

#### “栈可执行”到底是什么意思

RAM中的栈正常用途是保存数据：

```text
局部变量
函数参数
返回地址
保存的寄存器
FreeRTOS任务上下文
```

正常嵌入式程序的机器指令主要放在Flash中的 `.text`：

```text
Flash .text
    放函数机器指令，需要执行权限

RAM Stack
    放临时数据，需要读写权限，不需要执行权限
```

“可执行栈”表示系统认为栈内存除了读写外，还可能被当作机器指令来源执行。Linux等带内存权限管理的系统会根据ELF权限决定是否允许CPU从栈中取指；普通嵌入式固件通常根本不需要在栈上生成并执行代码。

最终ELF用 `PT_GNU_STACK` 程序头描述这一意图。`readelf`显示：

```text
GNU_STACK ... RWE ...
```

表示：

```text
R  Read     可读
W  Write    可写
E  Execute  可执行
```

加入 `-z noexecstack` 后变为：

```text
GNU_STACK ... RW ...
```

缺少 `E` 表示最终ELF声明栈不需要执行权限。

#### `.note.GNU-stack` 与 `GNU_STACK`不是同一个东西

容易混淆的两个名称是：

```text
.note.GNU-stack
    每个输入目标文件.o中的Section
    告诉链接器“这个目标文件是否要求可执行栈”

PT_GNU_STACK / readelf中的GNU_STACK
    链接器综合所有输入.o后写入最终ELF的程序头
    表达整个程序对栈权限的最终要求
```

流程是：

```text
startup.S.obj的.note.GNU-stack
tasks.c.obj的.note.GNU-stack
其他项目.obj的.note.GNU-stack
Newlib libc_a-memset.o：缺少该标记
                │
                ▼
GNU ld综合判断最终PT_GNU_STACK
                │
                ▼
因为有旧对象缺少声明而警告，并按兼容规则得到RWE
```

当前警告点名的是工具链中已经预编译好的：

```text
libc_a-memset.o
```

不是用户刚写的Hook，也不是FreeRTOS任务栈溢出。

#### 为什么一个目标文件不写声明，链接器会保守地认为需要执行栈

旧式工具链和某些历史代码可能真的在栈上生成短小代码片段，再跳到栈中执行。为了不破坏这种旧程序，GNU ld过去把“没有 `.note.GNU-stack` 声明”保守解释成“可能需要可执行栈”。

新版本链接器认为这种隐式行为不安全且即将被淘汰，所以提示：

```text
missing .note.GNU-stack section implies executable stack
This behaviour is deprecated
```

这句话不是说链接失败，而是在说：

```text
当前仍按旧兼容规则给了执行权限；
未来版本不希望继续根据缺失信息做这种推断；
项目应该明确声明最终意图。
```

#### 为什么本项目可以明确声明 `noexecstack`

当前固件是常规C和ARM汇编程序：

```text
代码固定链接到Flash .text
Task栈只保存数据和上下文
没有JIT即时编译
没有在栈上生成机器指令
不依赖GCC嵌套函数的栈上trampoline
```

因此没有合理需求从任务栈或MSP栈执行动态生成的代码，声明 `noexecstack` 与程序设计一致。

如果未来某段代码真的依赖栈上执行代码，强制 `noexecstack` 可能使它在有权限强制机制的平台上失败。但这种设计不适合当前安全导向的STM32固件，应从代码设计上消除，而不是重新打开栈执行权限。

#### 它会不会禁止FreeRTOS使用任务栈

不会。

```text
noexecstack
    禁止或声明不需要“把栈里的字节当指令执行”

FreeRTOS任务栈
    正常读写局部变量、保存寄存器和异常上下文
```

Task A切换到Task B时，PendSV仍然可以：

```text
向A的PSP栈写入R4～R11
从B的PSP栈读取R4～R11
异常返回时从B栈读取硬件保存帧
```

这些操作只需要读写权限，与执行权限无关。

它也不会：

```text
改变configTOTAL_HEAP_SIZE
改变任务栈深度
清空栈内容
禁止PSP/MSP
改变栈地址
修改vTaskDelay()
影响PendSV上下文保存格式
```

#### 它在STM32F103上是否真的形成硬件保护

不能仅凭这个链接选项就得出“硬件已经阻止从RAM栈执行”的结论。

ELF中的 `GNU_STACK` 首先是程序元数据。Linux这类操作系统的加载器可以读取它并设置页权限；裸机STM32启动流程不会像Linux进程加载器那样为每个ELF段建立虚拟内存页权限。

当前使用的是普通 `portable/GCC/ARM_CM3` 端口，不是FreeRTOS MPU端口。因此这里的直接价值主要是：

```text
明确程序不依赖可执行栈
消除新GNU ld的兼容性警告
让ELF元数据符合安全意图
避免未来工具链把缺失声明继续解释为RWE
```

真正的硬件内存执行保护需要芯片的MPU能力、MPU端口、内存区域属性及正确配置，不能由一个链接参数替代。

#### 它会增加Flash或RAM吗

通常不会增加实际 `.bin` 中的业务机器指令，也不会增加Task栈或FreeRTOS Heap。主要变化发生在ELF程序头的权限标志：

```text
修改前：GNU_STACK RWE
修改后：GNU_STACK RW
```

`.hex`/`.bin` 中烧录的Flash代码通常不会因为这一权限标志产生有意义的大小变化。仍应以构建后的 `size` 输出进行最终确认。

#### 与 `-Wa,--noexecstack` 的区别

当前编译选项已经有：

```text
-Wa,--noexecstack
```

拆开后：

```text
-Wa,
    把选项交给assembler汇编器

--noexecstack
    让当前重新汇编的目标文件声明不需要可执行栈
```

它作用于“生成每一个 `.o`”的汇编阶段；`-Wl,-z,noexecstack` 作用于“把所有 `.o` 合成ELF”的最终链接阶段：

```text
-Wa,--noexecstack
    管当前工程新生成的单个目标文件

-Wl,-z,noexecstack
    管最终ELF的整体栈权限意图
```

预编译Newlib对象在工具链安装时就已经生成，项目现在传入的 `-Wa` 无法回头修改它，所以仍需要最终链接策略。

#### 与 `--no-warn-execstack` 的区别

```text
-Wl,--no-warn-execstack
    不显示相关警告
    重点是“不要告诉我”

-Wl,-z,noexecstack
    明确最终ELF不要求可执行栈
    重点是“设置正确的最终意图”
```

当前临时验证已经证明后者同时做到：

```text
链接成功
警告消失
GNU_STACK由RWE变为RW
```

因此本项目选择 `-Wl,-z,noexecstack`，不是单纯为了让构建输出看起来干净。

#### 最终验证结论

当前工程已经把以下选项加入 `target_link_options(firmware PRIVATE ...)`：

```cmake
-Wl,-z,noexecstack
```

修改后应从两类证据判断选项是否生效：

```text
构建日志证据
    不再出现missing .note.GNU-stack警告

ELF程序头证据
    GNU_STACK只有RW，不再是RWE
```

推荐先执行干净配置和构建，避免把旧ELF误认为新结果：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
```

然后检查最终ELF程序头。若 `arm-none-eabi-readelf.exe` 已在当前终端的 `PATH` 中，可以执行：

```powershell
arm-none-eabi-readelf.exe -W -l build/firmware.elf | Select-String "GNU_STACK"
```

期望看到的关键权限是：

```text
GNU_STACK ... RW ...
```

如果命令找不到工具，应使用当前STM32CubeCLT中实际的完整路径调用 `arm-none-eabi-readelf.exe`，不要因此修改工程代码。

最终ELF检查结果为：

```text
GNU_STACK权限：RW
text：2068 B
data：4 B
bss：2700 B
```

因此该选项已经进入正式ELF，且没有改变当时可烧录代码和静态RAM数据的尺寸。

## 22. 第6课：编写第一个 LED Task

### 22.1 本课先分层，再增加Task行为

当前内核、Cortex-M3端口、Heap和Hook已经进入工程，但应用还没有创建Task。本课拆成两个独立变更，避免把目录迁移和Task行为混在一次构建里：

```text
第一个变更：只整理分层
    User/led/bsp_led.c/.h → User/bsp/led/bsp_led.c/.h
    同步CMake路径
    重新构建并确认行为、尺寸和链接结果没有异常

第二个变更：再增加Task
    创建User/app/led目录
    编写app_led_task.h/.c
    理解Task函数的入口、参数、无限循环和阻塞延时

当前暂不完成
    不修改main.c
    不调用xTaskCreate()
    不调用vTaskStartScheduler()
    不烧录验证
```

这样可以把两个概念分开：

```text
Task函数
    定义这个任务得到CPU后具体执行什么

xTaskCreate()
    为Task创建TCB和栈，并把Task加入Ready链表
```

仅仅写出Task函数并不会让它自动运行。它必须在后续由 `xTaskCreate()` 注册给内核，并在调度器启动后才可能获得CPU。

### 22.2 为什么放在 `User/app/led/` 而不是 `main.c`

LED闪烁是应用行为，不是FreeRTOS内核实现，也不是RTOS Hook，因此放在：

```text
User/app/
└── led/
    ├── app_led_task.h
    └── app_led_task.c
```

各层职责为：

```text
User/main.c
    板级初始化、创建任务、启动调度器

User/app/led/app_led_task.c
    LED Task的循环、延时和应用行为

User/bsp/led/bsp_led.c
    GPIO初始化和LED底层操作

User/rtos/freertos_hooks.c
    FreeRTOS调用的应用Hook

Libraries/FreeRTOS-Kernel/
    官方内核、Cortex-M3端口和Heap实现
```

依赖方向是：

```text
LED Task → FreeRTOS API
LED Task → LED BSP
```

LED BSP不应该反过来包含或调用LED Task。否则底层驱动会依赖上层业务，后续复用和测试都会变困难。

### 22.3 先迁移LED BSP并回归裸机基线

在创建Task源文件前，用户先亲自完成一次纯目录迁移：

```text
原路径
User/led/bsp_led.c
User/led/bsp_led.h

目标路径
User/bsp/led/bsp_led.c
User/bsp/led/bsp_led.h
```

这一步只改变文件位置，不修改 `bsp_led.c/.h` 的函数、宏、引脚或行为。随后由用户在CMake中同步两处路径：

```cmake
# 源文件路径
User/bsp/led/bsp_led.c

# 头文件搜索路径
User/bsp/led
```

不要同时创建LED Task或改造 `main.c`。Fresh构建通过后，应核对：

```text
bsp_led.c从新路径参与编译
编译错误和链接错误均为0
GNU_STACK仍为RW
Flash/RAM尺寸没有不合理变化
有条件时重新烧录，PB5裸机闪烁行为不变
```

目录迁移本身不应改变机器代码；若尺寸明显变化，说明CMake可能遗漏、重复加入了源文件，或构建配置并非同一个，需要先排查再继续。完成这项无行为变化的重构后，才开始创建应用Task。

本项目的实际回归结果为：

```text
目标文件路径：User/bsp/led/bsp_led.c/.h
CMake源文件路径：User/bsp/led/bsp_led.c
CMake包含路径：User/bsp/led
text：2068 B
data：4 B
bss：2700 B
GNU_STACK：RW
```

目录迁移后的目标文件路径、链接结果和固件尺寸均符合预期；该结果只证明无行为变化的构建回归，不替代硬件复验。

### 22.4 创建任务头文件

由用户创建 `User/app/led/app_led_task.h`：

```c
#ifndef APP_LED_TASK_H
#define APP_LED_TASK_H

void vLedTask(void *pvParameters);

#endif /* APP_LED_TASK_H */
```

这个头文件只公布Task入口函数，让后续的 `main.c` 可以把它的地址传给 `xTaskCreate()`。

命名中的 `v` 是FreeRTOS传统匈牙利命名习惯，表示函数返回类型为 `void`；它不是C语言语法要求。`pvParameters` 中的 `p` 表示pointer，`v`表示void，即“指向任意类型数据的指针”。

### 22.5 创建任务源文件

由用户创建 `User/app/led/app_led_task.c`：

```c
#include "app_led_task.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_led.h"

void vLedTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        LED1_TOGGLE;
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
}
```

先包含自己的头文件，可以让编译器检查头文件声明和源文件定义是否一致。`FreeRTOS.h` 应在 `task.h` 前包含，因为它先引入本工程配置、FreeRTOS基础类型和端口定义；`task.h` 再提供 `vTaskDelay()` 等任务API声明。

### 22.6 Task函数为什么必须是这种形式

FreeRTOS要求普通Task入口可以作为 `TaskFunction_t` 使用，其核心函数类型是：

```c
void TaskFunction(void *pvParameters);
```

三个部分分别表示：

```text
void返回值
    Task不能通过return把结果交回调用者

void *参数
    同一种Task入口可以接收不同实例的配置或对象地址

函数地址
    xTaskCreate()把它保存为新Task第一次运行的位置
```

当前LED Task不需要外部参数，所以后续创建时会传入 `NULL`。函数内部使用：

```c
(void)pvParameters;
```

明确表示“本版本有意不使用该参数”，从而避免 `-Wextra` 产生未使用参数警告。它不会释放、清零或改变参数。

### 22.7 为什么Task不能执行到函数末尾

普通C函数由调用者调用，结束后可以返回调用点；FreeRTOS Task不是由另一个普通C函数持续调用的。调度器只把处理器上下文切换到它的入口，Task没有可以正常返回的业务调用者。

因此Task通常写成：

```c
for (;;)
{
    /* 周期工作 */
}
```

如果Task确实要结束，应显式调用 `vTaskDelete(NULL)`；但当前最小配置没有启用删除API，LED Task也应永久存在，所以必须保持无限循环，不能写 `return`。

这里的无限循环不会天然霸占CPU。是否霸占CPU取决于循环中有没有进入阻塞态、挂起态或主动让出CPU。当前循环每次都会调用 `vTaskDelay()`，因此绝大多数时间处于Blocked状态。

### 22.8 `vTaskDelay()`执行后发生什么

假设后续LED Task的优先级为1，Idle Task优先级为0，Tick频率为1000 Hz：

```text
LED Task第一次得到CPU
    ↓
翻转PB5
    ↓
vTaskDelay(pdMS_TO_TICKS(500U))
    ↓
500 ms换算为500 Tick
    ↓
LED Task的TCB状态链表节点从Ready链表移入延时链表
    ↓
LED Task进入Blocked，Idle Task成为最高优先级Ready任务
    ↓
CPU运行Idle Task，Tick仍每1 ms递增
    ↓
第500个Tick到期，LED Task的链表节点移回Ready链表
    ↓
LED Task优先级高于Idle Task，请求PendSV切换
    ↓
LED Task继续循环，再次翻转PB5
```

TCB本身不会搬到任务栈中；变化的是TCB内部的链表节点属于哪个内核链表。Task的PSP栈保留其函数调用现场，因此再次运行时会从 `vTaskDelay()` 返回后的下一步继续。

### 22.9 为什么不再调用裸机 `delay_ms()`

当前裸机 `delay_ms()` 直接配置并轮询SysTick：

```text
main()配置SysTick
CPU反复读取COUNTFLAG
等待期间不能执行其他普通任务逻辑
```

FreeRTOS启动后，SysTick归Cortex-M3端口管理，用来产生整个内核的时间基准。应用如果再次写 `SysTick->LOAD`、`SysTick->CTRL` 或清空计数器，就会破坏RTOS Tick频率和任务延时。

因此后续改造 `main.c` 时必须整体删除裸机 `delay_ms()`，不能把它保留给某个Task继续使用。LED Task只调用 `vTaskDelay()`，不直接操作SysTick寄存器。

### 22.10 Task源文件创建后的阶段边界

工程新增两个文件：

```text
User/app/led/app_led_task.h
User/app/led/app_led_task.c
```

只创建文件但尚未加入CMake、也未被 `main.c`引用时，不能宣称LED Task已经运行。正确的验证顺序是：

1. 把 `User/app/led/app_led_task.c` 和 `User/app/led` 包含路径加入CMake。
2. 重新构建，先证明Task源文件可以独立编译。
3. 再改造 `main.c`，学习 `xTaskCreate()` 六个参数和 `vTaskStartScheduler()` 的启动路径。

### 22.11 把LED Task加入CMake做隔离编译

`User/app/led/app_led_task.c/.h`应满足：

```text
Task入口签名正确
未使用参数已显式转换为void
无限循环中使用vTaskDelay()
没有直接操作SysTick
通过bsp_led.h调用BSP层
符合项目自编C代码格式
```

先只让这个源文件进入编译，不在同一步修改 `main.c`。CMake需要增加两类信息：

```cmake
# add_executable(firmware ...)中的应用Task源文件
User/app/led/app_led_task.c

# target_include_directories(firmware PRIVATE ...)中的头文件目录
User/app/led
```

源文件路径告诉CMake“需要为谁生成 `.o`”；包含目录告诉编译器以及后续的 `main.c`“从哪里找到 `app_led_task.h`”。头文件目录搜索不是递归的：加入 `User/app` 不会自动搜索 `User/app/led`，所以应明确加入真正保存公共头文件的模块目录。

推荐按职责组织源文件列表：

```cmake
    # Application-side FreeRTOS integration.
    User/rtos/freertos_hooks.c

    # Application tasks.
    User/app/led/app_led_task.c

    # Board support package.
    User/bsp/led/bsp_led.c

    User/main.c
```

修改后执行Fresh构建：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
```

本轮必须在构建输出中看到类似：

```text
Building C object ... User/app/led/app_led_task.c.obj
```

但最终ELF中可能仍然找不到 `vLedTask`，Flash/RAM尺寸也可能保持不变。这不是失败：当前 `main.c` 没有引用 `vLedTask`，而链接选项 `--gc-sections` 会把这个未引用函数对应的Section从最终ELF裁掉。此次验收只证明Task文件、头文件包含关系和API调用能够通过编译；下一步由 `xTaskCreate()` 引用Task入口后，它才会保留在ELF中并获得TCB与任务栈。

当前工程的隔离编译结论为：

```text
app_led_task.c.obj已生成
生成的C_INCLUDES包含User/app/led
生成的C_INCLUDES包含User/bsp/led
最终ELF中没有vLedTask符号
text/data/bss仍为2068/4/2700 B
GNU_STACK仍为RW
```

这说明隔离编译和链接垃圾回收行为符合预期：源文件已经成功编译，但任务入口在尚未被应用引用时不会保留在最终ELF中。

## 23. 项目自编 C 代码风格

### 23.1 适用范围

本节是项目自编C代码的统一格式，适用于 `firmware/User/`、后续项目自建模块以及Codex给出的项目代码示例。它的目的不是判断FreeRTOS上游风格对错，而是让当前工程保持一致、便于阅读和评审。

以下内容不做批量格式化：

```text
Libraries/FreeRTOS-Kernel/
Libraries/CMSIS/
Libraries/FWlib/
其他原样引入的第三方源码
```

阅读或引用FreeRTOS源码时可以保留上游格式；一旦编写本项目自己的应用代码，则使用本节格式。

### 23.2 函数调用和函数定义

圆括号内部不留额外空格：

```c
foo(a, b);

void foo(uint32_t a);
```

不采用：

```c
foo( a, b );
void foo( uint32_t a );
```

这个规则同样适用于嵌套调用：

```c
vTaskDelay(pdMS_TO_TICKS(500U));
```

### 23.3 强制类型转换

类型括号内部以及转换表达式前不额外留空格：

```c
(void)pvParameters;
(uint32_t)value;
```

其中 `(void)pvParameters` 表示有意忽略参数，不改变变量内容。

### 23.4 控制语句和无限循环

`if`、`while` 和 `for` 是关键字，不是函数名，因此关键字与左圆括号之间保留一个空格；圆括号内部不在首尾添加空格：

```c
if (a > 0U)
{
}

while (1)
{
}

for (i = 0U; i < 10U; i++)
{
}

for (;;)
{
}
```

### 23.5 指针星号位置

指针星号靠变量名：

```c
uint8_t *pData;
void *pvParameters;
char *pcTaskName;
```

这里表达的是变量 `pData`、`pvParameters` 和 `pcTaskName` 为指针。以后声明多个变量时，应避免在同一条声明里混合指针和非指针，以减少误读。

### 23.6 Allman大括号

函数和控制语句的左大括号独占下一行：

```c
void foo(uint32_t a)
{
    if (a > 0U)
    {
        while (1)
        {
        }
    }
}
```

### 23.7 对当前工程的影响

从本规则确定后，新写的 `app_led_task.c/.h` 必须直接使用该格式。此前用户自编的 `freertos_hooks.c` 中仍有FreeRTOS上游式空格，例如 `( void ) xTask`、`for( ; ; )` 和 `char * pcTaskName`；它们不影响功能，但后续应由用户在一次仅格式变更中调整为 `(void)xTask`、`for (;;)` 和 `char *pcTaskName`。

格式调整只能改变空白和排版，不能顺便修改Hook逻辑、函数签名或错误处理行为。Codex仍只负责指出和记录，不直接修改用户源码。

## 24. 第7课：创建Task并启动调度器

### 24.1 本课目标和修改边界

上一课已经证明 `app_led_task.c` 可以独立编译，但当前最终ELF仍然没有：

```text
vLedTask
xTaskCreate
vTaskStartScheduler
pvPortMalloc
ucHeap
```

原因是 `main.c` 仍执行裸机循环，没有引用任何任务创建或调度器启动路径。本课只改造程序入口，目标是形成第一条真正的FreeRTOS运行链：

```text
main()
    ↓
初始化LED BSP
    ↓
xTaskCreate()创建LED Task
    ↓
vTaskStartScheduler()创建Idle Task并启动端口
    ↓
SVC恢复第一个Task上下文
    ↓
vLedTask()运行、阻塞、唤醒
```

本课仍不加入CAN、W5500、MQTT、FTP、OTA、Queue、Timer或其他Task。

### 24.2 `xTaskCreate()`的函数签名

FreeRTOS V11.3.0在 `task.h` 中定义了六个核心参数。为了先理解调用关系，可以整理为：

```c
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char *pcName,
    configSTACK_DEPTH_TYPE uxStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pxCreatedTask);
```

上面是按项目格式整理的教学简化表示；不要求也不允许修改FreeRTOS上游 `task.h`。真实声明还使用了不影响调用方式的顶层 `const` 限定，并保留上游自身排版。

返回值含义：

```text
pdPASS
    TCB和任务栈分配成功，新Task已经加入Ready链表

errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
    FreeRTOS Heap无法完成创建所需的动态分配
```

### 24.3 六个参数逐项理解

本项目计划使用：

```c
xTaskCreate(
    vLedTask,
    "LED",
    configMINIMAL_STACK_SIZE,
    NULL,
    tskIDLE_PRIORITY + 1U,
    NULL);
```

#### 参数1：`vLedTask`

这是Task入口函数地址。内核不会在调用 `xTaskCreate()` 时直接执行它，而是把该地址写入新Task的初始栈帧，使其成为Task第一次恢复上下文后的PC。

它必须符合：

```c
void vLedTask(void *pvParameters);
```

Task不能正常返回，因此当前实现使用 `for (;;)`。

#### 参数2：`"LED"`

这是任务调试名称，不是C函数名，也不决定任务行为。内核把它复制进TCB中的任务名数组，供GDB、RTOS感知调试器或任务列表识别。

当前：

```text
configMAX_TASK_NAME_LEN = 16
```

该长度包含结尾的 `\0`，因此最多保存15个有效字符。`"LED"`需要4个字节，完全足够。

#### 参数3：`configMINIMAL_STACK_SIZE`

这是LED Task自己的栈深度，不是字节数。当前ARM_CM3端口中：

```text
StackType_t = uint32_t = 4 B
configMINIMAL_STACK_SIZE = 128
LED Task栈空间 = 128 × 4 B = 512 B
```

该宏主要用于定义Idle Task的最小栈深度。本次LED Task逻辑简单，可以暂时复用128 words作为初始测量值，但不能把它理解为所有Task永远都应使用512 B。后续要用栈高水位和压力路径验证，而不是凭感觉长期固定。

#### 参数4：`NULL`

这是传给Task入口的参数。Cortex-M3端口会把它放进伪造的初始上下文R0位置，因此Task第一次运行时：

```c
void vLedTask(void *pvParameters)
```

收到的 `pvParameters` 就是这里传入的值。当前LED Task不需要实例配置，所以传 `NULL`，并在任务中写 `(void)pvParameters;`。

以后如果传入结构体地址，该对象的生命周期必须覆盖Task使用它的整个时间，不能随便传一个即将离开作用域的局部变量地址。

#### 参数5：`tskIDLE_PRIORITY + 1U`

这是FreeRTOS Task优先级：

```text
tskIDLE_PRIORITY = 0
LED Task优先级 = 1
configMAX_PRIORITIES = 5
合法范围 = 0～4
```

LED Task优先级1高于Idle Task优先级0。LED Task处于Ready时会优先运行；调用 `vTaskDelay()`进入Blocked后，Idle Task才获得CPU。

它与NVIC中断优先级无关。Task优先级数值越大越高，Cortex-M中断优先级数值越小越紧急。

#### 参数6：`NULL`

最后一个参数是Task句柄的输出地址。如果传入 `TaskHandle_t *`，内核会把新Task句柄写给调用者；后续可以用句柄挂起、恢复、通知或查询指定Task。

本Demo不需要从其他位置操作LED Task，因此传 `NULL`，表示不保存句柄。这不等于不创建TCB：TCB仍然存在，内核仍通过内部指针和链表管理Task，只是应用没有额外保存一个引用。

### 24.4 `xTaskCreate()`在内核中做了什么

根据当前V11.3.0 `tasks.c`，主流程可以概括为：

```text
xTaskCreate(...)
    ↓
prvCreateTask(...)
    ↓
通过pvPortMalloc()从heap_4分配TCB和任务栈
    ↓
初始化TCB、任务名、优先级和链表节点
    ↓
pxPortInitialiseStack()构造第一次运行所需的初始栈帧
    ↓
prvAddNewTaskToReadyList()
    ↓
返回pdPASS
```

创建成功后，LED Task已经处于Ready状态，但调度器尚未启动，所以它还不会执行。`xTaskCreate()`只是把运行所需的内存、上下文和调度数据准备好。

### 24.5 初始任务栈为什么能让函数第一次运行

普通的任务切换是保存旧Task上下文、恢复新Task上下文；新创建的Task以前从未运行过，没有真实的旧现场可恢复。因此 `pxPortInitialiseStack()`预先在其栈中构造一份“看起来像曾被中断过”的上下文：

```text
xPSR     设置Thumb状态
PC       vLedTask入口地址
LR       Task意外return时的错误入口
R0       pvParameters，本次为NULL
R1～R3   预留
R12      预留
R4～R11  预留给软件恢复
```

启动第一个Task时，SVC Handler恢复R4～R11和PSP，再通过异常返回让硬件恢复R0～R3、R12、LR、PC、xPSR。由于恢复出来的PC就是 `vLedTask`，CPU会从Task入口开始执行；R0中的 `NULL` 成为它的参数。

这不是由 `main.c`普通调用 `vLedTask()`，而是通过构造并恢复处理器上下文进入它。

### 24.6 `vTaskStartScheduler()`做了什么

当前配置启用了动态分配、关闭软件定时器，所以启动过程主要是：

```text
vTaskStartScheduler()
    ↓
动态创建Idle Task的TCB和栈
    ↓
关闭中断，建立调度器初始状态和Tick计数
    ↓
xPortStartScheduler()
    ↓
检查SVC和PendSV是否正确接入向量表
    ↓
检查中断优先级位和MAX_SYSCALL边界
    ↓
把PendSV和SysTick设为最低优先级，SVC设为最高优先级
    ↓
配置SysTick产生1 ms RTOS Tick
    ↓
执行SVC 0，恢复第一个Ready Task
```

软件定时器已关闭，所以此时不会创建Timer Service Task。系统中只有：

```text
LED Task：应用创建，优先级1
Idle Task：内核创建，优先级0
```

### 24.7 为什么必须删除裸机 `delay_ms()`

原来的 `delay_ms()`会直接写：

```text
SysTick->LOAD
SysTick->VAL
SysTick->CTRL
```

FreeRTOS启动后，SysTick是整个内核的时间基准。如果保留应用对这些寄存器的控制，可能改变Tick周期、停止Tick或破坏任务唤醒。因此这次改造必须删除整个 `delay_ms()`函数以及裸机闪烁循环，不能只是在旧代码下面追加调度器启动。

删除 `delay_ms()` 后，`main.c`也不再直接使用 `uint32_t`，所以原有的：

```c
#include <stdint.h>
```

可以一并删除，避免无意义依赖。

### 24.8 `main.c`的目标版本

由用户亲自把 `main.c`改造成：

```c
#include "FreeRTOS.h"
#include "task.h"

#include "app_led_task.h"
#include "bsp_led.h"

int main(void)
{
    BaseType_t xTaskCreateResult;

    LED_GPIO_Config();

    xTaskCreateResult = xTaskCreate(
        vLedTask,
        "LED",
        configMINIMAL_STACK_SIZE,
        NULL,
        tskIDLE_PRIORITY + 1U,
        NULL);

    if (xTaskCreateResult != pdPASS)
    {
        for (;;)
        {
        }
    }

    vTaskStartScheduler();

    for (;;)
    {
    }
}
```

这个版本遵守项目代码风格：函数括号内部不留空格、控制关键字后留一个空格、指针星号靠变量名、无限循环写 `for (;;)`、大括号采用Allman格式。

### 24.9 为什么要检查创建返回值

`xTaskCreate()`返回 `BaseType_t`。只有 `pdPASS` 才表示任务已经成功加入Ready链表。不能忽略返回值后直接启动调度器，否则当Heap不足时，系统可能只剩Idle Task运行，看起来像“调度器启动了但LED不亮”，掩盖真正的分配失败。

当前已经启用Malloc Failed Hook，而且Hook会停在死循环中，所以真实分配失败通常先停在Hook；保留返回值检查仍然有意义，因为它明确表达调用契约，也能适应以后Hook策略变化。

### 24.10 为什么调度器后面仍有无限循环

正常情况下，Cortex-M3端口启动第一个Task后，`vTaskStartScheduler()`不会返回。函数后面的无限循环是故障兜底：

```text
正常路径
    vTaskStartScheduler() → SVC → LED Task，不返回main

异常路径
    调度器未能启动或异常返回 → 落入main末尾死循环
```

它不是正常业务循环，也不负责闪灯。

### 24.11 构建后预期发生的变化

改造 `main.c` 后，`--gc-sections`不能再裁掉任务和调度器路径。预计ELF中开始出现：

```text
vLedTask
xTaskCreate
vTaskStartScheduler
pvPortMalloc
ucHeap
```

`ucHeap[8192]`会进入 `.bss`，所以链接器报告的RAM占用预计明显增加约8 KiB。LED Task和Idle Task运行时的TCB与栈是在这块数组内部划分，不会再各自在 `.bss` 中重复增加512 B数组。

Flash也会明显增加，因为任务创建、Heap分配、调度器启动和延时链表路径终于被保留。具体数值必须以用户实际Fresh构建输出为准，不能提前猜成验收结果。

### 24.12 首个Task创建后的静态构建结论

`main.c`引用Task入口并启动调度器后，最终ELF尺寸为：

```text
text：5016 B
data：8 B
bss：11008 B
FLASH实际装载：5016 + 8 = 5024 B
RAM静态占用：8 + 11008 = 11016 B
```

相较Task尚未被 `main.c`引用时：

```text
FLASH增加：5024 - 2072 = 2952 B
RAM增加：11016 - 2704 = 8312 B
```

RAM增加的主体是：

```text
ucHeap：8192 B
其余增加：约120 B的调度器、Heap管理和任务相关静态状态
```

LED Task和Idle Task的TCB及各512 B任务栈将在运行时从 `ucHeap`内部划分，所以不会在链接报告中再各自出现独立的512 B `.bss`数组。`ucHeap`整块数组一旦被链接保留，RAM报告就先计入全部8192 B，不等于启动前已经把整块Heap分配完。

关键符号及地址包括：

```text
xTaskCreate                     0x08000714
vTaskStartScheduler             0x080007A4
SVC_Handler                     0x08000CC8
PendSV_Handler                  0x08000D0C
SysTick_Handler                 0x08000DE4
pvPortMalloc                    0x08001054
vApplicationMallocFailedHook    0x0800129C
vApplicationStackOverflowHook   0x080012AE
vLedTask                        0x080012C0
main                            0x08001348
xTickCount                      0x2000002C
pxCurrentTCB                    0x200000DC
ucHeap                          0x20000104，长度0x2000
```

向量表保存Thumb函数地址时最低位必须为1，因此实际表项为：

```text
SVC向量：    0x08000CC9 → 函数符号0x08000CC8
PendSV向量： 0x08000D0D → 函数符号0x08000D0C
SysTick向量：0x08000DE5 → 函数符号0x08000DE4
```

最低位1不是函数错位，而是告诉Cortex-M按Thumb指令状态进入Handler。链接脚本中 `_estack=0x20010000`，当前静态数据、C运行库Heap和MSP预留最高到 `0x20002B08`，未超出64 KiB SRAM边界。

至此烧录前静态验收通过。

### 24.13 `tasks.c`中六项学习注释的内容审查

用户明确要求保留自行选择的注释位置，Codex只检查技术内容，不再因为它位于第三方源码中而要求移动或删除。当前六项注释的结论是：

| 参数 | 当前注释结论 | 需要补充的精度 |
| --- | --- | --- |
| `pxTaskCode` | “Task入口”方向正确 | 更准确是Task入口函数地址 |
| `pcName` | 保存到TCB、供调试识别，正确 | 内核会复制名称，长度受`configMAX_TASK_NAME_LEN`限制 |
| `uxStackDepth` | “任务栈深度”基本正确 | 必须注明单位为`StackType_t`，不是字节 |
| `pvParameters` | Task参数、初始上下文R0，在当前ARM_CM3端口下正确 | 先说明它是传给Task入口的参数，R0是端口实现方式 |
| `uxPriority` | “任务优先级”正确 | FreeRTOS中数值越大优先级越高 |
| `pxCreatedTask` | “返回创建的任务句柄”容易误解 | 它是句柄输出地址，可为`NULL`；函数返回值是状态码 |

建议用户把注释内容改为：

```c
/**
 * @param pxTaskCode Task入口函数地址。
 * @param pcName 复制到TCB中的任务名称，主要用于调试识别。
 * @param uxStackDepth 任务栈深度，单位为StackType_t，不是字节。
 * @param pvParameters 传给Task入口的参数；ARM_CM3初始上下文中放入R0。
 * @param uxPriority Task优先级，数值越大优先级越高。
 * @param pxCreatedTask Task句柄输出地址；不需要保存句柄时可为NULL。
 */
```

注释不改变可烧录机器指令；若需要按源码行调试，修改注释后仍应重新构建，使DWARF行号与当前源码同步。

## 25. 第二阶段：按键、Queue、Semaphore 与 Mutex

### 25.1 为什么不能一次全部加入

按键、Queue、Semaphore和Mutex分别位于不同层面：

| 对象 | 解决的问题 | 是否携带业务数据 | 是否有所有者 |
| --- | --- | --- | --- |
| 按键BSP | 读取具体GPIO电平、配置按键硬件 | 返回硬件状态 | 无 |
| Queue | 在Task之间或ISR到Task之间传递有类型的数据 | 有 | 无 |
| Binary Semaphore | 通知“某件事至少发生过一次” | 无 | 无 |
| Counting Semaphore | 记录事件次数或可用资源数量 | 只有计数，没有事件内容 | 无 |
| Mutex | 保证同一时刻只有一个Task访问共享资源 | 无 | 有，必须由持有者释放 |

如果在同一个实验里同时加入这些对象，LED行为异常时无法快速判断是按键电平、消抖、队列、ISR优先级还是互斥逻辑的问题。学习时应保持“一次只验证一个新机制”。

推荐顺序：

```text
阶段A：非阻塞按键BSP + 按键轮询Task
阶段B：Key Task通过Queue向LED Task发送按键事件
阶段C：按键EXTI通过Binary Semaphore唤醒Task
阶段D：两个Task通过Mutex保护一个真实共享资源
```

阶段A和阶段B已经完成。阶段C和阶段D是通信对象的知识储备，不是必须紧接着完成的功能清单；当前没有需要Binary Semaphore表达的EXTI事件，也没有需要Mutex保护的真实共享资源，因此二者延期。后续应在真实需求出现时再启用，而不是为了覆盖API强行增加Task或同步对象。

### 25.2 当前历史按键代码的审查结论

工程中已有尚未加入当前CMake目标的历史示例：

```text
User/Key/bsp_key.c/.h
User/exti/bsp_exti.c/.h
```

历史定义包含：

```text
KEY1：PA0
KEY2：PC13
```

但不能直接把这两组文件加入FreeRTOS工程，原因包括：

1. 目录不符合当前 `User/bsp/<功能域>/`分层规则，应先整理为 `User/bsp/key/`。
2. `Key_Scan()`在检测到按下后使用 `while`持续等待释放，是阻塞CPU的轮询，不适合作为RTOS Task的BSP接口。
3. 2024版开发板原理图确认KEY1和KEY2均由4.7 kΩ下拉到GND，按下时接通3.3 V，因此两者都是高电平按下。历史EXTI示例却把KEY2配置成下降沿，与“按下事件”不一致，不能照抄。
4. 历史EXTI代码调用 `NVIC_PriorityGroup_1`，与当前FreeRTOS要求的统一抢占优先级模型不一致，禁止直接接入。
5. 历史EXTI优先级不能直接用于调用FreeRTOS `FromISR` API；具体逻辑优先级必须位于当前允许范围5～15。

因此这些文件只能作为引脚和外设初始化参考，不能原样作为新的RTOS模块。

### 25.3 阶段A：非阻塞按键BSP和轮询Task

第一步不加入Queue，也不启用EXTI。先建立最小按键闭环：

```text
Key Task每10 ms读取一次按键
        ↓
应用层完成消抖和边沿识别
        ↓
检测到一次稳定按下事件
        ↓
暂时直接调用LED应用动作
```

分层目标：

```text
User/bsp/key/
├── bsp_key.h    定义按键编号、按下/释放状态和BSP接口
└── bsp_key.c    GPIO初始化和一次非阻塞电平读取

User/app/key/
├── app_key_task.h
└── app_key_task.c    周期扫描、消抖、边沿识别
```

BSP读取函数必须“读取一次立即返回”，不能等待释放，也不能调用FreeRTOS API。消抖属于带有时间和状态变化的应用策略，放在Key Task中更清晰。

推荐的消抖思路是连续采样：

```text
每10 ms采样一次
连续3次得到相同电平
        ↓
确认稳定状态持续约30 ms
        ↓
仅在Released → Pressed时产生一次按下事件
```

不能简单理解为“检测到按下后 `vTaskDelay(20)` 就一定消抖完成”。真正的消抖需要区分原始采样状态、稳定状态和连续一致次数。

### 25.4 阶段B：用Queue传递按键事件

按键轮询稳定后，再让Key Task和LED Task解耦：

```text
Key Task（生产者）
    │  KeyEvent_t
    ▼
Key Event Queue
    │
    ▼
LED Task（消费者）
```

Queue中保存的是事件值的副本，不是发送方局部变量的地址。事件至少应区分：

```text
KEY1_PRESSED
KEY2_PRESSED
```

如果需要区分按下、释放或记录发生时间，可以把Queue元素扩展为结构体；不应依赖多个无含义的整数常量。

LED Task使用阻塞式 `xQueueReceive()`等待事件后，不再需要周期性查询按键：

```text
Queue为空
    ↓
LED Task进入Blocked，不占CPU
    ↓
Key Task发送事件
    ↓
LED Task变为Ready并处理LED动作
```

这一阶段需要：

- 把 `queue.c`加入CMake源文件。
- 应用包含 `queue.h`。
- 在启动调度器前创建Queue并检查返回值。
- Queue长度必须有界；满队列时要定义丢弃、覆盖还是有限等待策略。
- `configUSE_QUEUE_SETS`仍保持0，普通Queue不需要Queue Set。

Queue Handle可以在任务创建时作为 `pvParameters`的值传给Key Task和LED Task。传递的是Queue Handle本身，不是保存Handle的局部变量地址；Queue控制块和存储区位于FreeRTOS Heap中，生命周期不依赖 `main()`的局部变量。

### 25.5 阶段C：用Binary Semaphore学习ISR到Task同步

Queue实验稳定后，再选择一个按键改成EXTI。Binary Semaphore只表达：

> 中断事件已经发生，请Task处理。

典型关系：

```text
按键EXTI ISR
    ├─ 清除硬件中断标志
    ├─ xSemaphoreGiveFromISR()
    └─ portYIELD_FROM_ISR()
             ↓
Key Processing Task
    └─ xSemaphoreTake()成功后完成消抖和业务处理
```

必须遵守：

- ISR只做必要工作，不在ISR中等待按键释放。
- ISR只能调用带 `FromISR`的API。
- 调用FreeRTOS API的EXTI中断逻辑优先级必须在5～15范围内。
- NVIC统一使用全部有效位作为抢占优先级的分组，不能沿用历史 `NVIC_PriorityGroup_1`。
- `xHigherPriorityTaskWoken`必须在每次ISR进入时初始化为 `pdFALSE`。
- ISR结束前使用 `portYIELD_FROM_ISR()`请求必要的PendSV切换。
- Binary Semaphore可以合并多次尚未处理的事件，不能保存“哪个按键”或完整事件顺序。

如果ISR必须传递按键编号或事件内容，应使用 `xQueueSendFromISR()`，而不是额外建立一组全局变量补数据。

为了保持分层，按键GPIO和EXTI寄存器操作属于 `User/bsp/key/`；需要同时依赖BSP与FreeRTOS的IRQ桥接代码属于 `User/rtos/`。BSP层不能反向包含应用层或FreeRTOS业务逻辑。

### 25.6 阶段D：Mutex必须保护真实共享资源

Mutex不是“更高级的Semaphore”，它用于保护共享资源，并提供所有权和优先级继承：

```text
Task A成功Take Mutex
        ↓
Task A成为Mutex持有者
        ↓
其他Task只能等待
        ↓
Task A完成资源访问并Give Mutex
```

Mutex不适合：

- 在ISR中Take或Give。
- 代替Queue传递按键事件。
- 代替Binary Semaphore通知事件发生。
- 为了演示而让多个Task随意修改同一个LED状态。

本项目应等出现真实共享资源后再做Mutex实验，例如两个Task共享同一个UART输出接口。若以后日志量变大，更好的架构通常是“各Task发日志Queue，由单独日志Task独占UART”，而不是所有Task长时间持有UART Mutex。

Mutex实验需要：

```c
#define configUSE_MUTEXES 1
```

普通Mutex不需要开启递归Mutex：

```c
#define configUSE_RECURSIVE_MUTEXES 0
```

### 25.7 Queue、Semaphore和Mutex的底层关系

FreeRTOS的Queue、Binary Semaphore、Counting Semaphore和Mutex主要共用 `queue.c`中的底层对象结构和阻塞链表，但它们的使用契约不同：

```text
Queue               保存固定大小的数据项
Binary Semaphore    Queue长度类似1，元素大小为0，用于事件同步
Counting Semaphore  用计数表示累计事件或资源数量
Mutex               增加持有者和优先级继承语义
```

“底层实现相近”不表示它们可以随意互换。选择对象时先问：

1. 是否需要传递数据？需要则优先Queue。
2. 是否只需要通知事件？可以考虑Binary Semaphore或Task Notification。
3. 是否需要累计多个同类事件或表示多个资源？考虑Counting Semaphore。
4. 是否要保护只能被一个Task访问的共享资源？使用Mutex。

### 25.8 分阶段验收标准

| 阶段 | 最小验收 |
| --- | --- |
| 按键BSP | 每次读取立即返回；KEY1/KEY2有效电平分别确认；没有等待释放的忙循环 |
| Key Task | 周期扫描不会阻塞其他Task；一次物理按下只产生一次稳定事件 |
| Queue | Key Task只发送事件，LED Task阻塞接收；队列满策略和返回值处理明确 |
| Binary Semaphore | EXTI ISR优先级合法；ISR只Give，Task负责消抖和处理；无中断风暴 |
| Mutex | 只在Task上下文使用；持有范围短；所有成功Take路径最终都Give；共享资源无交叉输出 |

表中标准用于相应机制真正进入工程时验收。当前Binary Semaphore和Mutex实验已延期，不阻塞ADC闭环或后续CAN准备；CAN帧包含ID、DLC和数据字节，接收方向更适合使用能够携带数据的有界Queue。

## 26. USART1 TX DMA 与 Task Notification

### 26.1 本阶段解决什么问题

轮询发送串口时，CPU需要反复等待USART发送数据寄存器空闲，再逐字节写入数据。DMA发送把“从内存读取字节并写入USART数据寄存器”的重复动作交给DMA控制器：

```text
Serial TX Task
    │ 配置缓冲区地址和字节数
    ▼
DMA1 Channel 4
    │ USART1_TX请求驱动逐字节搬运
    ▼
USART1->DR
    │
    ▼
PA9 → 板载CH340G → 电脑串口工具
```

DMA只解决数据搬运问题，不负责告诉Task“本次发送已经结束”。因此本阶段还需要建立反向同步路径：

```text
DMA传输完成
    ▼
DMA1_Channel4_IRQHandler
    ▼
vTaskNotifyGiveFromISR()
    ▼
Serial TX Task从Blocked变为Ready
```

最终验证不是只收到一条固定字符串，而是Serial TX Task能够等待每次DMA完成、延时约1 s并再次发送。连续周期输出证明发送和完成通知两个方向都已经闭环。

### 26.2 STM32F103的固定DMA映射

STM32F103的DMA通道与外设请求之间存在芯片规定的固定映射，本项目使用：

```text
USART1_TX → DMA1 Channel 4
USART1_RX → DMA1 Channel 5
```

Channel 4不是应用随意挑选的编号。当前TX配置的关键含义是：

| 配置 | 当前选择 | 原因 |
| --- | --- | --- |
| 方向 | Memory → Peripheral | 字符串位于内存，目标是USART数据寄存器 |
| 外设地址递增 | 禁止 | 每个字节始终写入同一个 `USART1->DR` |
| 内存地址递增 | 允许 | DMA需要依次读取缓冲区中的每个字节 |
| 数据宽度 | Byte | USART按字节发送8位数据 |
| 模式 | Normal | 一个消息发送完后停止，等待Task提供下一条消息 |

TX选择Normal模式，是因为每条日志或命令响应都有明确边界。Circular模式更适合连续采样或连续接收固定缓冲区；如果TX使用Circular模式，同一段字符串会在没有Task控制的情况下不断重复发送。

### 26.3 DMA发送缓冲区的生命周期

启动DMA是异步操作：`BspUsart1_TxDmaStart()`返回时，DMA可能还没有读取完缓冲区。因此在传输完成前必须满足：

- 缓冲区仍然存在。
- 缓冲区内容不能被生产者改写。
- 不能释放或复用该内存。
- 不能让另一个Task同时重配同一个DMA通道。

当前固定消息使用 `static const`，其存储期覆盖整个程序运行过程，DMA读取期间不会因函数返回而失效。后续发送动态日志时，将由Serial TX Task独占一个持久发送缓冲区；其他Task只向有界日志Queue提交消息，不直接持有USART或DMA。

这种设计建立了明确所有权：

```text
其他Task             只生产待发送消息
Serial TX Task       唯一拥有TX工作缓冲区和启动DMA的权限
DMA1 Channel 4       传输期间读取该缓冲区
完成中断             只报告缓冲区已经可以再次使用
```

### 26.4 BSP与应用层如何分工

USART BSP只依赖FWlib、CMSIS和硬件，负责：

- USART1 GPIO、波特率和收发模式配置。
- DMA1 Channel 4固定参数配置。
- 装载本次内存地址和传输计数。
- 检查、清除DMA完成标志并关闭普通模式通道。

应用Serial模块负责：

- 创建和保存Serial TX Task Handle。
- 决定发送什么以及发送周期。
- 在中断桥接中调用FreeRTOS `FromISR` API。
- 在Task上下文等待完成事件并决定下一步行为。

因此依赖方向保持为：

```text
app_serial_task.c → bsp_usart.c + FreeRTOS API
bsp_usart.c       → FWlib/CMSIS/硬件
```

BSP不会包含应用头文件，也不会保存具体业务Task Handle。DMA ISR不格式化字符串、不解析命令、不等待硬件，只清状态、发送通知并按需请求调度。

### 26.5 为什么中断使用逻辑优先级6

当前配置规定：

```text
configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
```

STM32F103使用4个有效NVIC优先级位，逻辑优先级范围为0～15。DMA完成ISR调用了：

```c
vTaskNotifyGiveFromISR();
```

所以它必须位于允许调用FreeRTOS API的逻辑优先级5～15。本项目为DMA1 Channel 4选择逻辑优先级6：

```text
0～4   紧迫度高，但禁止调用FreeRTOS FromISR API
5～15  紧迫度较低，允许调用当前受支持的FromISR API
```

这里的“6”不是FreeRTOS Task优先级。Task优先级数值越大越高；Cortex-M IRQ优先级数值越小越紧迫，这两套方向不能混用。

### 26.6 Task Notification在TCB中保存什么

启用：

```c
#define configUSE_TASK_NOTIFICATIONS 1
```

后，每个TCB会保存任务通知值和通知状态。默认通知数组只有一个槽位，可以把当前Serial TX Task的相关字段概念化为：

```text
TCB
├── ulNotifiedValue[0]   当前累计通知值
└── ucNotifyState[0]     未等待、正在等待或已收到通知
```

`vTaskNotifyGiveFromISR()`采用“Give”语义，把目标任务的通知值加1。如果目标任务正在等待通知，内核还会把它从Blocked状态移到Ready状态。Task Notification不是独立分配的Queue对象；发送方必须已经知道唯一目标Task Handle。

这使它特别适合当前关系：

```text
一个固定DMA完成ISR → 一个固定Serial TX Task
```

### 26.7 `ulTaskNotifyTake()`的三个关键参数行为

当前任务调用：

```c
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

如果进入函数时通知值为0，任务会被加入阻塞/延时管理结构，`ulTaskNotifyTake()`暂时不能返回。DMA硬件仍然独立工作，CPU可以运行Key Task、LED Task或Idle Task。

第一个参数控制函数返回时如何消费通知值：

| 当前通知值 | 参数 | 函数返回值 | 返回后的通知值 |
| ---: | --- | ---: | ---: |
| 1 | `pdTRUE` | 1 | 0 |
| 3 | `pdTRUE` | 3 | 0 |
| 3 | `pdFALSE` | 3 | 2 |

因此：

- `pdTRUE`表示成功取得通知后把累计通知值清零，适合只关心“至少完成过一次”。
- `pdFALSE`表示只减1，适合按次数逐个消费累计事件。

返回值是清零或减1之前的通知值。当前代码使用 `(void)`忽略返回值，是因为使用最大等待时间且只把它当作一次同步点；以后改成有限超时时，应保存返回值，`0`表示没有在限定时间内收到通知。

第二个参数 `portMAX_DELAY`是Tick类型能够表达的最大等待值。当前配置没有显式启用 `INCLUDE_vTaskSuspend`，因此从严格语义看它是最大有限等待，而不是数学意义上的永远：32位Tick、1 kHz条件下约为49.7天。对当前秒级DMA实验可视为实际无限等待；若工程要求FreeRTOS提供真正不进入超时链表的无限阻塞语义，需要同时评审并启用 `INCLUDE_vTaskSuspend=1`。

### 26.8 为什么“中断先发生”也不会丢通知

存在这种合法时序：

```text
Task启动DMA
    ↓
DMA很快完成并进入ISR
    ↓
通知值从0增加到1
    ↓
Task才调用ulTaskNotifyTake()
```

这时 `ulTaskNotifyTake()`看到TCB中的通知值已经非0，会直接消费通知并返回，不进入Blocked状态。

另一种时序是：

```text
Task进入ulTaskNotifyTake()
    ↓
内核在临界区中确认通知值仍为0，并标记正在等待
    ↓
Task进入Blocked
    ↓
ISR Give通知，将Task移回Ready
```

FreeRTOS在检查通知值、设置等待状态的关键位置使用临界区，避免ISR刚好夹在“检查为0”和“标记等待”之间造成丢事件。这比应用自己用一个未经保护的全局布尔变量可靠。

### 26.9 当前Serial TX Task的完整状态时间线

```text
Running
  │ 启动DMA1 Channel 4
  │ 调用ulTaskNotifyTake()
  ▼
Blocked：等待DMA完成通知
  │ DMA完成ISR执行Give
  ▼
Ready
  │ 调度器选中该Task
  ▼
Running：ulTaskNotifyTake()返回并清零通知值
  │ 调用vTaskDelay(1000 ms)
  ▼
Blocked：等待固定发送周期
  │ Tick到期
  ▼
Ready → Running → 启动下一次DMA
```

如果DMA完成中断不发送通知，任务会一直停留在第一次等待DMA的Blocked阶段，后面的 `vTaskDelay()`不会执行，因此电脑端通常只能看到第一条消息。持续每秒输出说明通知等待和延时等待两个Blocked阶段都能正确结束。

### 26.10 为什么当前使用Task Notification

可选同步方式的区别是：

| 机制 | 是否传递数据 | 是否需要独立内核对象 | 当前场景 |
| --- | --- | --- | --- |
| Queue | 是 | 是 | 后续传递具体日志消息 |
| Binary Semaphore | 否 | 是 | 可用于一般ISR到Task事件同步 |
| Task Notification | 可作计数、位或值使用 | 否，状态直接位于TCB | 最适合固定ISR通知固定Task |
| Mutex | 否 | 是，带所有权 | 用于保护共享资源，不能由ISR释放 |

当前DMA完成事件不需要携带字符串内容，也只有一个固定接收Task，所以Task Notification开销小、关系明确。后续日志系统仍需要Queue，因为多个Task要把不同文本或结构化消息交给Serial TX Task；Queue负责“传什么”，Notification负责“DMA什么时候完成”，两者职责不同。

### 26.11 TX阶段验证结论与RX衔接

当前已经通过构建、ELF符号检查和真实硬件现象确认：

- USART1 TX使用DMA1 Channel 4普通模式工作。
- 静态发送缓冲区生命周期满足DMA异步读取要求。
- `DMA1_Channel4_IRQHandler`覆盖启动文件中的弱默认入口。
- 逻辑优先级6的ISR能够合法调用 `vTaskNotifyGiveFromISR()`。
- Serial TX Task能够在Notification到达前阻塞，收到通知后恢复，并通过 `vTaskDelay()`形成约1 s周期输出。
- Serial TX Task已经改为阻塞等待私有有界TX Queue，不再依靠固定周期主动产生消息。
- 两条启动消息能够按提交顺序完整输出，证明Queue FIFO、第一条DMA完成通知和第二条DMA启动形成连续闭环。
- BSP只处理硬件，应用层拥有Task Handle与发送策略，没有提前引入Console、ADC、CAN或网络业务。

有界TX Queue阶段已经完成。其他Task只提交待发送消息，Serial TX Task独占USART1 TX DMA；Queue负责“要发送什么”，Task Notification负责“本次DMA搬运何时结束”。

随后完成的RX硬件通路为：

```text
USART1_RX → DMA1 Channel 5循环模式
USART IDLE中断
DMA写位置与软件读位置计算
ISR通知Serial RX Task处理新增字节
```

该RX硬件通路已经完成，具体原理和实机验收见第27章；随后加入的行缓冲、回车换行、退格和 `help`/`version`命令见第28章，任务、CPU与Heap诊断见第29章。

### 26.12 有界Serial TX Queue的所有权与FIFO验证

当前Serial模块内部创建长度为4的私有Queue，每个元素保存一条最长512字节的消息及其实际长度。Queue对外只暴露 `AppSerial_Write()`，不把Queue Handle、USART寄存器或DMA通道暴露给生产者。早期原始回显实验曾使用128字节上限；接入完整任务诊断报告后扩大为512字节。

```text
main或其他Task
    │ AppSerial_Write()复制消息
    ▼
私有有界TX Queue（FIFO）
    │ xQueueReceive()取出一条完整副本
    ▼
Serial TX Task的局部工作消息
    │ 启动DMA后阻塞等待Notification
    ▼
DMA1 Channel 4 → USART1_TX
```

这里有三个不同的内存所有权阶段：

1. 调用 `AppSerial_Write()`之前，源缓冲区属于调用者。
2. `xQueueSend()`成功返回后，消息内容已经被复制进Queue存储区；调用者可以立即修改或释放自己的源缓冲区。
3. Serial TX Task通过 `xQueueReceive()`把一条消息复制到自己的局部工作消息中。DMA传输完成通知到来之前，该Task一直阻塞在当前函数栈帧中，局部工作消息仍然存在且不会被下一条消息覆盖。

因此，DMA读取的不是生产者可能随时修改的缓冲区，而是Serial TX Task当前独占的消息副本。只有收到DMA完成通知后，Task才返回Queue顶部取下一条消息。

Queue长度为4表示系统最多缓存4条尚未被消费者取走的消息，并不表示可以同时进行4次DMA传输。USART1 TX和DMA1 Channel 4在任一时刻仍只处理一条消息。Queue满时，当前非阻塞写接口立即报告失败，让上层明确选择丢弃、计数或稍后重试，避免低优先级日志无限消耗RAM或长期阻塞关键Task。

Queue由FreeRTOS在运行时从 `ucHeap`中分配控制块和元素存储区。因此它会减少FreeRTOS Heap的剩余空间，但Queue元素容量不会以同样大小直接增加ELF的 `bss`；`ucHeap`数组本身已经整体计入 `bss`。最小Demo初始使用8 KiB，接入512字节TX消息和诊断后调整为24 KiB，P3加入Storage与FTP任务和Queue后调整为32 KiB。

本阶段向Queue连续提交两条启动消息，电脑端按相同顺序完整收到两条消息。该现象至少验证了：

- 两次提交都成功复制进入Queue。
- Queue按FIFO顺序交付消息。
- 第一条发送完成后，DMA中断成功通知Serial TX Task。
- Task在收到通知后才安全复用工作消息并启动第二条DMA。
- Serial TX Task是唯一发送所有者，没有两个生产者直接争用DMA通道。

TX Queue本身不是日志或Console系统。RX硬件通路在第27章完成，Console行协议在第28章接入，任务格式化和运行统计在第29章完成；消息级别日志仍应作为独立功能评估。

## 27. USART1 RX循环DMA、IDLE与Task Notification

### 27.1 本阶段建立的最小闭环

本阶段把USART1接收从“CPU逐字节查询”改为“DMA持续搬运、IDLE事件唤醒Task”：

```text
PA10 / USART1_RX
    ↓ USART1_RX DMA请求
DMA1 Channel 5循环模式
    ↓
256字节RX DMA缓冲区
    │
    ├─ CNDTR表示本轮尚未搬运的数据单元数
    │
USART检测到IDLE
    ↓
USART1_IRQHandler
    ↓ vTaskNotifyGiveFromISR()
Serial RX Task：Blocked → Ready → Running
    ↓
计算新增字节区间和回绕
    ↓ AppSerial_Write()复制提交
私有TX Queue → Serial TX Task → DMA1 Channel 4回显
```

DMA负责搬运，IDLE只负责产生“现在值得检查一次缓冲区”的事件，Task Notification只负责把该事件交给固定的Serial RX Task。通知值不是接收字节数，实际位置仍由CNDTR决定。

本章记录的是接入Console之前用于隔离验证RX硬件通路的原始字节回显基线。第28章复用同一DMA缓冲区、IDLE通知和软件读位置算法，只把“原样回显”替换为“逐字节交给Console状态机”。

### 27.2 Circular模式与CNDTR写位置

RX使用Circular模式后，DMA从缓冲区位置0开始写入，写到最后一个字节后自动回到位置0。通道不需要在每次IDLE时停止、重装计数器或重新启动；这样才能避免接收空窗。

当前Channel 5关键配置为：

| 配置 | 当前选择 | 原因 |
| --- | --- | --- |
| 方向 | Peripheral → Memory | 数据从 `USART1->DR`进入RAM |
| 外设地址递增 | 禁止 | 每个字节都从同一个DR读取 |
| 内存地址递增 | 允许 | 依次写入RX缓冲区 |
| 数据宽度 | Byte | USART当前按8位数据接收 |
| 模式 | Circular | 写满后自动回到缓冲区头部 |
| DMA优先级 | High | RX不能像TX一样等待软件稍后重试 |

缓冲区长度为 `N` 时，DMA下一次写入位置为：

```text
writePosition = (N - CNDTR) % N
```

CNDTR表示本轮循环还剩多少个数据单元没有搬运，不是累计接收字节数。例如 `N=256`、`CNDTR=246`，说明本轮已经写入10字节，下一写位置为10。DMA完成一整圈时，CNDTR重新装载为256，因此写位置重新等价于0。

### 27.3 软件读位置、回绕与发送分块

Serial RX Task独占 `readPosition`，其含义是下一个尚未处理的字节位置。Task取得一次 `writePosition`快照后按以下规则处理：

| 条件 | 新增数据区间 |
| --- | --- |
| `writePosition > readPosition` | `[readPosition, writePosition)` |
| `writePosition < readPosition` | 先处理 `[readPosition, N)`，再处理 `[0, writePosition)` |
| `writePosition == readPosition` | 在当前“不允许追满一圈”的前提下视为没有新增数据 |

在第27章的原始回显实现中，每次交给 `AppSerial_Write()`的长度还要受单条TX消息128字节上限约束。因此一个连续区间可能继续拆成多条Queue消息，但任一消息都不能跨越RX数组末尾。第28章改为逐字节交给Console后不再按128字节拆分原始输入，但“连续块不能跨越数组末尾”以及读位置推进、回绕规则保持不变。

当前分块算法始终保持：

```text
0 <= readPosition < N
0 < chunkLength <= N - readPosition
```

因此本次访问范围 `[readPosition, readPosition + chunkLength)`最多到达 `buffer[N - 1]`。发送后 `readPosition += chunkLength`可能刚好得到 `N`，但代码会在下一次获取数组地址之前把它归零，所以“发送后再判断回绕”不会造成数组越界。

### 27.4 IDLE标志为什么必须读SR再读DR

STM32F103清除USART IDLE标志要求固定硬件序列：

```text
读取USART_SR
    ↓
读取USART_DR
```

当前BSP先使用 `USART_GetITStatus()`检查IDLE，该函数会读取SR；确认事件有效后再调用并丢弃 `USART_ReceiveData()`的返回值，从而完成DR读取。`USART_ClearITPendingBit()`不适用于IDLE；`NVIC_ClearPendingIRQ()`也只能清NVIC挂起状态，不能代替外设的SR→DR清除序列。

IDLE表示接收线路持续一个字符帧时间没有新数据。当前115200、8-N-1配置下，一个字符帧为10 bit，约86.8微秒。它适合表示一次输入暂停，但不是完整Console行或业务消息的天然边界。

### 27.5 为什么RX中断在调度器启动后才激活

`BspUsart1_Init()`在 `vTaskStartScheduler()`之前完成GPIO、波特率和基础外设配置，但RX DMA、IDLE中断源和USART1 NVIC只由Serial RX Task第一次运行时启动。

这样可以保证：

- Serial RX Task及其Handle已经创建。
- FreeRTOS调度器和Cortex-M3端口已经完成启动。
- USART1 ISR调用 `vTaskNotifyGiveFromISR()`时，中断优先级和调度上下文均有效。
- 即使IDLE事件发生在Task调用 `ulTaskNotifyTake()`之前，通知值也会先保存在TCB中，不会丢失。

当前USART1 ISR使用逻辑优先级6，位于项目允许调用FreeRTOS `FromISR` API的逻辑优先级5～15范围内。DMA1 Channel 5没有启用HT、TC或TE中断，因此不需要应用实现 `DMA1_Channel5_IRQHandler`。

### 27.6 Serial RX Task的状态时间线

```text
Running：启动RX循环DMA和IDLE中断
    ↓ ulTaskNotifyTake(pdTRUE, portMAX_DELAY)
Blocked：等待IDLE通知，DMA仍持续接收
    ↓ USART1 ISR执行Give
Ready
    ↓ 调度器选中
Running：读取CNDTR并处理新增区间（第27章提交回显，第28章逐字节解析Console）
    ↓ 再次等待
Blocked
```

`pdTRUE`会在 `ulTaskNotifyTake()`成功返回时把累计通知值清零。即使多个IDLE事件合并，Task仍会读取当前写位置快照并处理尚未读取的数据；通知次数不用于计算字节数量。

### 27.7 分层、缓冲区所有权与限制

USART BSP拥有RX DMA静态缓冲区并负责DMA、CNDTR和IDLE硬件状态；应用Serial模块拥有RX Task Handle、软件读位置和FreeRTOS ISR桥接。BSP不调用FreeRTOS，ISR不复制长数据或执行命令。

在原始回显基线中，`AppSerial_Write()`把RX数据复制进私有TX Queue，所以函数返回后RX DMA可以继续覆盖循环缓冲区，而不会影响已经排队的消息。接入Console后，RX原始数据不再整块回显；只有Console形成完整响应时才调用同一复制接口。Queue满时接口仍立即失败，RX Task记录丢弃并继续推进读位置，避免串口输出反向无限阻塞接收任务。

当前最小实现存在明确容量边界：仅凭 `readPosition`和 `writePosition`无法区分“缓冲区为空”和“DMA恰好追满一整圈”。因此一次连续无IDLE输入以及Task未及时处理的累计数据必须严格小于256字节。后续若要支持持续数据流，需要增加DMA半传输/传输完成事件或单调生产计数，而不是简单扩大数组后宣称问题消失。

第27章原始RX闭环曾为Serial RX Task分配192 words。接入使用FreeRTOS格式化接口的诊断命令后，当前栈深度调整为512 words，实测Stack High Water Mark剩余348 words；栈大小以最终调用链实测为依据，不能仅凭“没有触发栈溢出Hook”定稿。

### 27.8 构建与真实硬件验收结论

加入RX闭环后的Debug ELF静态尺寸为：

```text
text：10432 B
data：8 B
bss：11288 B
Flash装载量：10440 B
RAM静态占用：11296 B
```

ELF确认 `USART1_IRQHandler`为全局Text强符号，覆盖启动文件弱默认入口；`DMA1_Channel5_IRQHandler`继续保持弱默认符号，符合本阶段只使用USART IDLE事件的设计。

真实硬件已经完成以下验收：

- USART1 RX字节由DMA1 Channel 5写入256字节循环缓冲区。
- IDLE ISR能够清除硬件状态并通过Task Notification唤醒Serial RX Task。
- 原始字节能够经 `AppSerial_Write()`、TX Queue和DMA1 Channel 4完整回显。
- 先输入200字节并完整回显，验证128+72字节的TX消息分块。
- 随后输入100字节并完整回显，验证从位置200开始的尾部56字节加头部44字节回绕处理。

这些结果共同证明RX循环DMA、CNDTR写位置、软件读位置、回绕、IDLE清除、ISR到Task同步和TX Queue复制已经形成最小闭环。

### 27.9 与最小Console的衔接

后续接入最小Console时没有改变DMA、IDLE ISR或TX所有权，只在Serial RX Task取得字节后增加行协议：

```text
原始RX字节
    ↓
Console行缓冲
    ↓ 处理CR/LF和退格
完整命令行
    ↓
第28章初版只读命令：help、version
    ↓
AppSerial_Write()输出响应
```

第一版Console只学习行缓冲、边界检查和命令分发，没有提前加入ADC、CAN、W5500、MQTT、FTP或升级命令。实现原理和验收结论见第28章。

## 28. 最小Console行协议与Serial RX Task接入

### 28.1 模块边界与数据流

最小Console复用第27章已经验证的RX硬件通路，没有新增Console Task、Queue或中断：

```text
USART1_RX → DMA1 Channel 5循环缓冲区
    ↓ USART IDLE
USART1_IRQHandler：只清状态并通知
    ↓
Serial RX Task：计算CNDTR写位置、软件读位置和连续数据块
    ↓ 逐字节调用AppConsole_ProcessByte()
纯C Console状态机
    ↓ 完整命令产生静态响应视图
AppSerial_Write()复制响应
    ↓
私有TX Queue → Serial TX Task → DMA1 Channel 4
```

`User/app/console/`只负责有界行缓冲和命令分发，不包含FreeRTOS、BSP或Serial模块头文件，也不读取USART寄存器。它通过 `AppConsoleOutput_t`返回响应地址和长度，由Serial RX Task决定如何发送，因此不会形成Console反向驱动USART或DMA的依赖。

### 28.2 IDLE事件不等于命令行结束

USART IDLE只表示线路在一个字符帧时间内没有新数据。人工输入时，每两个按键之间都可能产生IDLE，所以一条 `help`命令可能经历多次以下状态转换：

```text
Serial RX Task：Blocked
    ↓ IDLE ISR发送Notification
Ready → Running：取出一个或若干新字节，更新Console状态
    ↓ 再次调用ulTaskNotifyTake()
Blocked
```

只有接收到CR（`0x0D`）、LF（`0x0A`）或CRLF组合时，Console才认为一行结束。命令行状态保存在Console模块的静态变量中，因此能够跨多次IDLE通知继续累计；Notification次数仍不表示命令长度。

### 28.3 有界行状态机

Console使用64字节静态数组，其中最多保存63个可打印字符，最后一个字节始终留给C字符串结束符 `\0`。核心状态为：

| 状态 | 含义 |
| --- | --- |
| 当前行长度 | 已经保存的有效字符数，范围为0～63 |
| 超长丢弃标志 | 当前行是否已经超过容量，需要丢弃到行结束 |
| 前一字节是CR | 用于把CRLF识别为一次行结束，而不是两次命令 |

`AppConsole_Init()`在Serial RX Task启动RX DMA前调用一次，重置上述状态。`AppConsole_ProcessByte()`每次只消费一个字节：普通可打印ASCII进入行缓冲，退格修改当前长度，CR或LF触发行完成处理，其他控制字符忽略。`prvCompleteLine()`只在行结束时补上 `\0`并执行精确命令匹配。

始终保持以下边界：

```text
0 <= lineLength <= 63
lineBuffer[lineLength] == '\0'
```

因此写入普通字符、退格后重新放置结束符，以及行结束时调用 `strcmp()`都不会访问数组之外。

### 28.4 CR/LF、退格与超长恢复

单独CR和单独LF都可以完成一行。收到CR后，Console先执行当前命令并记录“前一字节是CR”；如果下一字节恰好是LF，就只清除该记录而不再次执行，从而保证CRLF只产生一次响应。

Backspace（`0x08`）和Delete（`0x7F`）在内部都按删除一个字符处理，但只有当前长度大于0时才递减，避免无符号长度从0下溢。当前固件不逐字回显输入，终端上的输入显示和光标擦除效果取决于串口工具的本地回显设置；这不影响STM32内部行缓冲内容。

当63个有效字符已经占满缓冲区后，第64个可打印字符不会写入数组，而是使整行进入丢弃状态。此后包括退格在内的非行结束字节全部忽略，直到CR或LF到达，再返回一次 `ERROR: line too long`并重置状态。整行作废而不是执行被截断的前63字符，可以避免未来把长输入的有效前缀误当作控制命令。

### 28.5 响应所有权与背压

Console响应是模块内部的 `static const`数组。`AppConsole_ProcessByte()`返回 `true`时，输出对象只提供只读地址和长度；Serial RX Task立即调用 `AppSerial_Write()`，把响应复制进现有私有TX Queue。因此Console随后重置命令行状态，不会影响已经排队的响应，DMA也不会直接读取Console的可变行缓冲区。

`AppSerial_Write()`仍使用0 Tick非阻塞提交。TX Queue满时，Serial RX Task累计Console响应丢弃次数并继续推进DMA软件读位置，不在接收路径中等待串口发送完成。这样优先保护RX循环缓冲区不被未处理数据追上，但也意味着当前长度为4的TX Queue不承诺吸收无限数量的同批命令响应。

### 28.6 当前命令和明确限制

第一版只实现：

| 输入 | 行结束后的行为 |
| --- | --- |
| `help` | 返回当前命令列表 |
| `version` | 返回Console版本 |
| 空行 | 不响应 |
| 其他非空行 | 返回 `ERROR: unknown command` |
| 超长行 | 返回 `ERROR: line too long`，然后恢复接收下一行 |

命令按小写字符串精确比较，不裁剪前后空格，不做大小写归一。第28章初版没有参数解析、动态命令注册、命令历史、方向键编辑、设备端逐字符回显或运行统计命令；第29章在不改变行协议的前提下增加 `task`和 `heap`。当前单条TX消息上限为512字节。

### 28.7 构建与真实硬件验收结论

Console进入运行路径后的Debug ELF静态尺寸为：

```text
text：11056 B
data：8 B
bss：11352 B
Flash装载量：11064 B，约占512 KiB的2.11%
RAM静态占用：11360 B，约占64 KiB的17.33%
```

ELF中 `AppConsole_Init()`、`AppConsole_ProcessByte()`、`AppSerialRxTask_Create()`、`USART1_IRQHandler`和 `DMA1_Channel4_IRQHandler`均已进入有效代码路径。`DMA1_Channel5_IRQHandler`继续保持弱默认符号，符合RX循环DMA不启用Channel 5中断的设计。

真实硬件已经完成以下功能与边界验收：

- `help`和 `version`返回预期固定响应，未知命令返回错误，空行不响应。
- 手动输入能够跨多次IDLE通知累计为一条完整命令。
- 单独CR、单独LF和CRLF均可结束命令，CRLF只执行一次。
- 退格能够修改当前行，空行退格不会造成长度下溢。
- 同一批数据中的 `help\r\nversion\r\n`按顺序各响应一次。
- 63个可打印字符作为合法长度进入未知命令分支；第64个字符触发一次超长错误。
- 超长行结束后，下一条 `help`能够正常执行，证明丢弃状态已经恢复。

这些结果证明DMA/IDLE事件边界、Console行边界和TX响应边界已经被分开：IDLE负责唤醒，CR/LF负责完成命令，TX Queue负责复制和排队响应。

### 28.8 下一边界

最小Console闭环完成后，任务运行统计、栈高水位和Heap诊断作为第29章的独立功能完成；更复杂的命令参数仍不在当前范围。随后完成的ADC1低频双通道采样记录在第30章：它采用单ADC Task直接复用现有Serial TX Queue，没有增加ADC原始数据Queue、Binary Semaphore或Mutex。

## 29. FreeRTOS任务、运行时间与Heap诊断

### 29.1 目标、分层与数据流

本阶段在现有Console上增加 `task`和 `heap`，但不让Console直接依赖FreeRTOS。Console仍只负责把输入行识别为一种输出请求；Serial RX Task在Task上下文调用诊断模块，报告最后通过已有 `AppSerial_Write()`复制进入私有TX Queue：

```text
Console：识别task或heap
    ↓ 输出请求类型
Serial RX Task
    ↓
app_rtos_diagnostics：调用FreeRTOS诊断API并生成文本
    ↓
AppSerial_Write()复制报告
    ↓
Serial TX Queue → Serial TX Task → DMA1 Channel 4
```

`User/app/diagnostics/`只依赖FreeRTOS API，不访问USART或TIM寄存器；`User/bsp/timer/`只处理TIM6、RCC、DBGMCU和中断状态，不依赖应用层。诊断没有新增Task或Queue，也没有改变USART ISR、DMA所有权和Console行协议。

### 29.2 三个配置开关与官方格式化接口

当前启用：

```c
configUSE_TRACE_FACILITY = 1
configUSE_STATS_FORMATTING_FUNCTIONS = 1
configGENERATE_RUN_TIME_STATS = 1
```

三者职责不同：

| 配置 | 作用 |
| --- | --- |
| `configUSE_TRACE_FACILITY` | 让TCB和 `TaskStatus_t`具备任务编号、基础优先级、运行计数等快照所需信息 |
| `configUSE_STATS_FORMATTING_FUNCTIONS` | 编译 `vTaskListTasks()`和 `vTaskGetRunTimeStatistics()`等便捷文本格式化函数 |
| `configGENERATE_RUN_TIME_STATS` | 在任务切换时读取独立计数器，把各任务处于Running状态的时间累计到TCB |

`task`命令依次调用两个官方格式化接口：第一张表显示任务状态、优先级、栈高水位和任务编号，第二张表显示累计运行计数和CPU百分比。两次调用取得的是相邻时刻的两个快照，不是严格同一瞬间；当前低频人工诊断允许这点微小时间差，避免为追求完全一致引入自定义快照和格式化复杂度。

这两个便捷接口内部都会临时申请 `TaskStatus_t`数组。加入ADC Task后当前有6个任务，`sizeof(TaskStatus_t)=40`字节，因此单次正文快照需要240字节，再加 `heap_4`块管理开销；函数结束后会释放。它们适合低频调试，不应放入硬实时周期路径。

### 29.3 TIM6的10 kHz运行时间计数

FreeRTOS Tick为1 kHz，只能提供1 ms粒度，不适合统计很短的任务运行片段。本阶段单独使用TIM6产生10 kHz自由运行计数：

```text
APB1 PCLK1 = 36 MHz
APB1分频不为1，TIM6输入时钟 = 72 MHz
PSC = 7199
计数频率 = 72 MHz / 7200 = 10 kHz
每个计数 = 100 us
ARR = 0xFFFF
低16位每6.5536 s回绕一次
```

TIM6更新ISR只把32位软件高位加1并清除更新标志，不调用FreeRTOS API，因此使用逻辑优先级4。低16位硬件计数与32位软件高位组合后有48个有效累计位，通过 `uint64_t`接口返回；在10 kHz下约892年才会回绕。调试器暂停CPU时，DBGMCU同时冻结TIM6，避免断点停留时间被计入某个任务。

FreeRTOS在启动调度器时通过 `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()`初始化TIM6，并在任务切换路径通过 `portGET_RUN_TIME_COUNTER_VALUE()`读取累计值。当前读取函数使用很短的全局中断屏蔽保证高低位一致；该实现只服务于当前FreeRTOS调用路径，如果未来把它作为通用BSP时间源复用，需要重新评审调用上下文和中断状态恢复方式。

### 29.4 `task`状态表与栈余量

状态字符含义：

| 字符 | Task状态 | 当前常见示例 |
| --- | --- | --- |
| `X` | Running | 生成报告时的Serial RX Task |
| `R` | Ready | 等待CPU的Idle Task |
| `B` | Blocked | 等待Queue、通知或延时到期的LED、Key、Serial TX和ADC Task |
| `S` | Suspended | 被显式挂起或使用真正无限等待语义的任务 |
| `D` | Deleted | 已删除但资源尚待Idle Task回收的任务 |

`StackFree`是任务启动以来从未使用过的最小栈空间，单位为 `StackType_t`个数，不是字节。当前Cortex-M3的 `StackType_t`为4字节。ADC加入前的诊断阶段快照为：Serial RX 348、Idle 118、Key 104、LED 96、Serial TX 96 words。加入ADC后共有6个任务；ADC Task初始分配256 words，并在真实硬件验收中确认栈余量满足本阶段需求，但没有在文档中虚构未记录的具体数值。栈高水位用于证明余量，不能只看“没有触发栈溢出Hook”。

### 29.5 `RunCount`与CPU百分比

`RunCount(100us)`表示任务从调度器启动以来累计处于Running状态的计数。空闲系统中Idle Task承担绝大部分时间，因此实测占用99%～100%；Key、LED和Serial任务每次运行时间很短，可能显示 `<1%`，甚至因为100 us分辨率而暂时显示0个计数。CPU百分比是累计整数结果并向下取整，各行显示值不保证精确相加等于100%。

实测累计值从614788继续增加到3212612，跨越多次TIM6低16位回绕后仍保持单调，证明硬件低位、软件高位和TIM6更新ISR已经闭环。

当前V11.3.0默认格式化分支在打印绝对 `RunCount`时会转换为32位 `unsigned int`；内部累计类型和CPU百分比计算仍使用 `uint64_t`。因此设备连续运行约4.97天后，表中的绝对计数只显示累计值低32位，而CPU百分比仍可继续计算。当前学习诊断接受这一显示限制；若以后要求长期显示完整绝对时间，应直接使用 `uxTaskGetSystemState()`取得原始64位字段并自行格式化，而不是修改FreeRTOS上游源码。

### 29.6 `heap`命令与三项数值

`heap`使用 `heap_4.c`官方接口：

| 字段 | 含义 |
| --- | --- |
| `Total` | `configTOTAL_HEAP_SIZE`配置的FreeRTOS Heap总字节数 |
| `Free` | 当前所有空闲块的总字节数 |
| `MinEverFree` | 调度器启动以来出现过的最低空闲总量 |

`Free`不等于“最大一次可分配块”，也不能单独证明没有碎片；`MinEverFree`是历史低水位，临时分配释放后不会自动升高。执行 `task`时临时申请任务快照数组，结束后 `Free`应恢复，而 `MinEverFree`可能下降一次。重复执行 `task`和 `heap`后，`Free`保持稳定才说明没有持续泄漏。

初始8 KiB Heap在原有任务、512字节×4的TX Queue和任务栈创建完成后只剩768字节，不足以安全扩展ADC Task，因此当时调整为24 KiB。P3加入Storage、FTP双TCP Task及其有界Queue后，当前进一步调整为32 KiB；它仍位于STM32内部64 KiB SRAM中，已经整体计入ELF的 `bss`，与链接脚本的C Heap、MSP和板载外部RAM都不是同一个区域。最终余量仍以全量链接结果、`heap`命令和各Task栈高水位实测为准。

### 29.7 缓冲区、构建与实机结论

任务报告使用512字节静态缓冲区，Heap报告使用128字节静态缓冲区；只有Serial RX Task触发生成，随后 `AppSerial_Write()`立即复制到TX Queue。静态报告缓冲区不从FreeRTOS Heap反复分配，也不会在DMA发送期间被诊断模块直接复用。

ADC加入前的诊断阶段Debug构建结果：

```text
text：17408 B
data：88 B
bss：28424 B
Flash：17496 B / 512 KiB，3.34%
RAM：28512 B / 64 KiB，43.51%
```

真实硬件已经确认：两个任务表头和Heap表头完整；任务状态、优先级、栈余量和任务编号合理；TIM6跨多次6.5536 s回绕后计数连续；空闲系统Idle占用符合预期；重复执行任务和Heap诊断后当前空闲量稳定。诊断闭环完成后，ADC阶段继续复用这些接口验收新增Task和Heap余量，具体设计与结论见第30章。

## 30. ADC1双通道低频采样与串口发送复用

### 30.1 目标、分层与数据流

本阶段建立的最小闭环为：

```text
ADC Task：每隔约1 s开始一次采样
    ↓
BspAdc_ReadRaw()
    ├─ PC1 / ADC1_IN11：板载电位器
    └─ ADC1_IN16：内部温度传感器
    ↓
ADC Task：生成有界文本报告
    ↓
AppSerial_Write()复制数据
    ↓
私有Serial TX Queue
    ↓
Serial TX Task → DMA1 Channel 4 → USART1
```

`User/bsp/adc/`只负责ADC1、GPIO、通道选择、校准、转换和硬件超时，不依赖FreeRTOS或应用层。`User/app/adc/`负责采样周期、失败计数和报告策略，不直接访问ADC、USART或DMA寄存器。

ADC1只有ADC Task一个所有者，Serial TX Task继续独占USART1发送DMA。两个硬件资源的所有权都保持单一、明确。

### 30.2 软件触发单次转换

ADC1使用独立模式，关闭扫描和连续转换。每次先把一个通道配置为规则组Rank 1，再由软件启动一次转换。电位器和内部温度传感器不是同时转换，也不是ADC1与ADC2双ADC模式，而是由ADC1依次完成两次单通道转换。

当前PCLK2为72 MHz，ADC时钟使用六分频得到12 MHz：

```text
电位器：55.5采样周期 + 12.5转换周期 ≈ 5.67 us
内部温度：239.5采样周期 + 12.5转换周期 = 21 us
两路转换合计约26.7 us
```

内部温度传感器使用更长采样时间，是为了给内部高阻信号源足够的采样建立时间。当前只输出12位原始值，不直接换算摄氏度；内部温度传感器的典型参数、芯片个体差异和校准误差需要单独处理，原始值更适合先验证采样链路。

`BspAdc_ReadRaw()`先把两路结果保存到局部变量，只有两次转换都成功才更新调用者提供的输出对象，避免失败时返回一半新数据、一半旧数据。

### 30.3 Task轮询EOC、超时和任务状态

EOC是End Of Conversion，表示本次ADC转换完成。当前转换流程为：

```text
清除旧EOC
    ↓
软件启动一次转换
    ↓
Task循环读取EOC标志
    ↓
EOC置位后读取ADC_DR
```

轮询EOC期间ADC Task仍处于Running状态，不会像等待Queue或调用`vTaskDelay()`那样进入Blocked。但两路转换只占约27 us，而且每秒只执行一次，因此当前忙等时间很短，适合本阶段的低频状态监测。

`BSP_ADC_TIMEOUT_COUNT`是轮询次数上限，不是毫秒值。它受CPU频率、编译优化和函数调用开销影响，只用于防止ADC或校准硬件异常时永久卡死，不能当作精确时间基准。初始化校准超时会阻止调度器启动；运行中的转换超时则使本次读取失败，由ADC Task记录失败计数后等待下一周期。

ADC Task的典型状态时间线为：

```text
Running：调用vTaskDelay(1000 ms)
    ↓
Blocked：等待周期到期，不占用CPU
    ↓ 延时到期
Ready
    ↓ 被调度
Running：完成两路转换、文本格式化和Queue提交
    ↓
再次调用vTaskDelay()
Blocked
```

因此`task`命令中ADC通常显示为`B`，偶尔可能在尚未获得CPU时显示`R`。当前使用相对延时，所以实际相邻报告间隔约为“一秒延时加本次处理时间”；对于低频监测足够。如果未来要求严格固定采样相位，可以评估`vTaskDelayUntil()`；若要求持续高速采样，则应重新评估定时器触发、ADC扫描和DMA。

### 30.4 为什么当前不使用ADC中断或DMA

ADC中断适合转换期间Task还要执行其他工作，或由不规律事件触发单次采样的场景；定时器触发加DMA更适合固定频率、连续多通道采样和批量处理。

当前需求只有每秒两次顺序转换，全部转换时间约27 us。为此增加ADC ISR、`FromISR`同步、DMA缓冲区及其回绕管理，获得的收益很小，却会引入更多状态和所有权。因此本阶段保留软件触发和Task轮询EOC，只有在采样率和实时性需求发生变化时再升级方案。

### 30.5 为什么没有ADC Queue和第二个Task

当前数据只有一个生产者和一种直接处理策略：

```text
ADC Task读取原始值
    ↓
ADC Task生成低频报告
```

没有另一个需要独立调度的原始数据消费者。现有`AppSerial_Write()`之后已经有私有Serial TX Queue，它把“生成报告”和“等待DMA发送”解耦。此时再增加ADC原始值Queue和ADC Process Task，会重复增加一次中转，同时增加Task栈、Queue内存和失败路径，却没有解决新的并发问题。

只有出现下列需求时，才应重新考虑ADC数据Queue或第二个处理Task：

- 采样频率明显高于处理或上报频率。
- 必须暂存并保留每一个样本。
- 滤波、控制和记录需要不同优先级或周期。
- 多个模块需要消费同一批采样数据。
- ADC采样时序不能被文本格式化影响。

同理，当前没有ADC ISR，所以不需要Binary Semaphore；ADC1只有一个Task访问，也没有需要Mutex保护的并发共享资源。

### 30.6 串口发送所有权与背压

ADC Task使用96字节局部缓冲区生成不含浮点数的固定格式报告。`AppSerial_Write()`在返回前把内容复制进有界Serial TX Queue，因此函数返回后ADC Task可以安全复用自己的局部缓冲区，DMA不会继续引用ADC Task栈上的数据。

ADC Task不等待DMA完成，也不直接修改USART或DMA状态。Serial TX Task仍是唯一发送所有者，因此ADC周期报告、Console响应和其他消息不会由多个Task同时操作硬件。

当TX Queue已满时，`AppSerial_Write()`立即返回`false`。ADC Task累计报告丢弃计数并进入下一周期，不让串口输出反向阻塞ADC采样。这个策略适合允许丢失个别状态报告的低频监测；若未来数据具有不可丢失语义，需要重新定义缓存容量、超时和背压策略。

### 30.7 构建与真实硬件验收结论

最终Debug构建结果：

```text
text：18400 B
data：88 B
bss：28432 B
Flash：18488 B / 512 KiB，约3.53%
RAM：28520 B / 64 KiB，约43.52%
```

ELF中已经保留`BspAdc_Init()`、`BspAdc_ReadRaw()`、`AppAdcTask_Create()`和ADC Task入口。真实硬件已经确认：

- ADC1能够周期读取PC1/ADC1_IN11板载电位器和ADC1_IN16内部温度传感器。
- 电位器原始值随旋钮位置明显变化，并保持在12位ADC范围内。
- 内部温度原始值能够稳定输出；本阶段没有把未经校准的原始值宣称为精确摄氏温度。
- 串口约每秒输出一条完整报告，Console和原有串口功能仍可正常使用。
- ADC Task大部分时间处于Blocked，CPU占用符合低频短时采样预期。
- Task栈和FreeRTOS Heap验收通过，连续运行未出现持续内存下降、乱码或异常复位。
- ADC读取失败计数和报告丢弃计数提供了明确的失败可观察入口。

这证明软件触发、EOC轮询、周期Task、现有Serial TX Queue和DMA发送已经形成最小ADC闭环。Binary Semaphore和Mutex因当前没有真实使用场景而暂缓。P1的CAN波特率已经确定为500 kbit/s，PCLK1=36 MHz时采用Prescaler=4、BS1=15 TQ、BS2=2 TQ和SJW=1 TQ；下一步继续确认CAN供电、终端电阻、Windows分析仪配置和P1测试帧评审，再进入bxCAN实现。

进入CAN阶段后，ADC1仍由ADC Task独占。CAN Task不得为了发送遥测而再次直接调用`BspAdc_ReadRaw()`；ADC Task应把一组完整样本作为不可变快照，通过明确、非阻塞的数据所有权路径交给CAN侧。若需要保留每一组样本，可使用有界Queue复制传递；若周期遥测只关心最新状态，则可评审由单一写者发布、CAN Task读取的最新值快照及其一致性保护。无论采用哪种方式，CAN接收帧都包含ID、IDE、RTR、DLC和数据字节，接收方向应使用能够携带完整帧快照的有界Queue，而不是只能表达“发生过事件”的Binary Semaphore。
