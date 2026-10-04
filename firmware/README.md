# STM32F103ZET6 固件工程

本目录承载野火 STM32F103 霸道 V2 的固件代码。工程已经先用不依赖 FreeRTOS 的裸机 LED 冒烟测试验证 ARM GCC 工具链、CMake + GNU Make 构建流程、GCC 启动文件、链接脚本、系统时钟和 GPIO；随后由用户亲自完成 FreeRTOS Kernel V11.3.0 最小调度闭环、按键 Queue Demo、USART1 TX DMA + Task Notification、USART1 RX循环DMA + IDLE回显闭环、Console行协议与命令分发、任务状态/栈/CPU/Heap诊断、ADC1双通道低频采样、bxCAN最小闭环，以及W5500分层TCP收发与持续重连闭环。

FreeRTOS 的原理、术语、源码关系、自测题和逐步移植学习记录见 [FreeRTOS 学习与移植笔记](FreeRTOS学习.md)。本 README 继续作为固件工程状态、构建方法和验收结果的入口。

Application采用单一`application.*`输出，不建立默认/Application双模式。完整镜像位于`0x08010000`，前512字节为描述头，向量表位于`0x08010200`，整个应用区上限为448 KiB。P4裸机Boot工程位于相邻的`../bootloader/`，独立配置和构建，产物输出到`../bootloader/build/`；两边各自保存源码、头文件、芯片库、W25Q64 BSP及构建配置，只按[协议文档](../docs/protocol.md)冻结的字节格式和地址交接。应用CMake目标仍名为`firmware`，文件产物为`build/application.*`；Boot使用方法见[Bootloader工程说明](../bootloader/README.md)，当前交付状态见[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。下文`firmware.*`及`0x08000000`记录保留为历史基线。

裸机程序、FreeRTOS调度、按键Queue、USART1 TX/RX DMA、Console、运行诊断、ADC1双通道采样和CAN1最小闭环均已完成构建与实机验证。W5500使用SPI2和官方ioLibrary Driver v3.2.0核心驱动，已经通过硬件复位、静态网络、PHY、Ping和分层TCP验收。coreMQTT v2.3.1、独立MQTT Task和业务Task已完成CONNECT、Keep Alive、重连、10秒心跳与温度/电压上报、红色LED1/蜂鸣器控制和业务ACK，P2业务闭环已实机验收。P3 FTP正常下载已看到成功及100%结果；失败矩阵按用户决定跳过。P4 Boot、应用首次确认和MQTT升级受理已实现，完整升级及再次复位尚无实机反馈；当前按用户要求完成开发结项审查与归档，不新增业务。WPF已取消，Windows业务端使用MQTTX。

## 目录

- [当前交付与验证边界](#当前交付与验证边界)
- [当前技术栈](#当前技术栈)
- [固件目录与分层目标](#固件目录与分层目标)
- [项目自编 C 代码风格](#项目自编-c-代码风格)
- [硬件基线](#硬件基线)
- [构建环境检查](#构建环境检查)
- [配置与构建](#配置与构建)
- [构建输出](#构建输出)
- [下载与实机验收](#下载与实机验收)
- [bxCAN错误状态与Bus-off管理](#bxcan错误状态与bus-off管理)
- [固件技术路线](#固件技术路线)
- [FreeRTOS V11.3.0 学习型移植记录](#freertos-v1130-学习型移植记录)
- [当前范围与限制](#当前范围与限制)
- [README 维护规则](#readme-维护规则)

## 当前交付与验证边界

2026-10-04结项审查覆盖独立Boot和Application的启动交接、元数据兼容及升级流程。应用通过Storage完整提交`UPGRADE_REQUESTED`后发送受理ACK，再发送`upgrade=3, rate=0`；两条消息均须取得匹配本机请求号、原MQTT会话和末段SENDOK的发送完成回执，最后由业务Task关闭输出并放行复位。取消只在Storage完整提交回`READY_TO_UPGRADE`后确认成功。首次确认复用Storage Task，以调度器运行、Storage初始化成功及ADC有效样本为条件，不依赖MQTT联网。

本轮已修正Startup更新时残留的两个无条件失败返回块，并修正FTP重复读取Storage快照把完成状态100%改回95%的问题，修复后的Debug构建通过。完整FTP和业务路径现已保留在ELF中；详细构建与检查结果见下文及[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。这些修复没有增加协议、任务或业务范围。

已有实机证据覆盖P1、P2和P3正常下载。完整正常升级、首次`CONFIRMED`、升级成功上报、再次复位运行及最终升级增量的运行栈/Heap余量仍未收到实机结果；烧录读回通过、源码审查和构建均不替代这些验收。故障注入按用户决定跳过，不列为已通过。当前结束开发并准备完整根目录归档到新远端，原`firmware/`Git历史保留；提交、打标签和推送继续由用户执行，归档步骤见[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。

## 当前技术栈

| 范围 | 当前选型 | 状态 |
| --- | --- | --- |
| 开发板 | 野火 STM32F103 霸道 V2 | 已确认 |
| MCU | STM32F103ZET6，Cortex-M3 | 已确认 |
| 板级库 | CMSIS + STM32F10x Standard Peripheral Library V3.5.0 | 已接入 |
| RTOS | FreeRTOS Kernel V11.3.0，`heap_4`当前32 KiB | 调度器、Queue、Task Notification和运行统计已实机验证；32 KiB配置及最终升级增量的运行余量以实际验收为准 |
| 串口基础 | USART1 PA9/PA10 + 板载CH340G；TX使用DMA1 Channel 4，RX使用DMA1 Channel 5 | TX普通DMA、RX循环DMA、IDLE通知、CNDTR位置计算和回绕均已实机验证 |
| 串口Console | 固定有界行缓冲；CR/LF/CRLF与退格处理；`help`、`version`、`task`、`heap`、`data_query`只读命令 | 全部命令已经实机验证；`data_query`按需显示当前CPU温度和电位器电压 |
| 运行诊断 | `vTaskListTasks()`、`vTaskGetRunTimeStatistics()`、TIM6 10 kHz计数和`heap_4`余量查询 | 状态、优先级、栈高水位、CPU累计占用及Heap当前/历史余量已验证 |
| ADC采样 | ADC1；PC1/ADC1_IN11板载电位器和ADC1_IN16内部温度传感器；软件触发单次转换、Task轮询EOC | 单个ADC Task每1 s发布最新快照；电位器换算为V、内部温度换算为°C，支持`data_query`按需读取和MQTT每10秒上报；已经实机验收 |
| CAN最小闭环 | CAN1，PB8/PB9重映射，29位Classic CAN扩展数据帧，500 kbit/s | 基础收发、真实ADC上报、控制、应用层ACK、错误状态、自动Bus-off恢复和2小时稳定性均已完成实机验收 |
| W5500与TCP | SPI2：PB13/PB14/PB15；PG9/PG8/PG15：CS/INT/RST；WIZnet ioLibrary Driver v3.2.0核心驱动 | 复位、寄存器、静态IP、PHY、Ping、分层Socket原语、TCP收发、FIN关闭和持续重连均已完成构建与实机验收；工作区已接入W5500统一Mutex访问门面 |
| MQTT连接 | coreMQTT v2.3.1；独立MQTT Task，独占Socket 0与MQTTContext | CONNECT、Keep Alive、重连、control订阅、心跳与温度JSON上报、输出控制ACK均已实机验证 |
| FTP与候选存储 | FTP Task独占Socket 1/2；Storage Task独占SPI1/W25Q64；Mbed TLS 3.6.7 MD5子集 | 正常下载、长度/描述头/MD5校验及100%成功结果已实机验收；失败矩阵跳过 |
| 升级交接 | 独立裸机Boot；元数据v3与A/B原子提交；应用首次确认、MQTT受理/取消和结果补报 | 源码已接入；完整升级、首次确认及再次复位实机结果未反馈 |
| 交叉编译器 | GNU Tools for STM32 14.3.1，`arm-none-eabi-gcc` | 已验证 |
| 构建配置 | CMake 4.3.1 | 已验证 |
| 构建执行 | GNU Make 4.4.1 | 已验证 |
| 工具集合 | STM32CubeCLT 1.22.0 | 已安装 |
| 调试下载器 | CMSIS-DAP，SWD 接口 | 已完成实机下载验证 |
| 调试服务器 | OpenOCD | 已完成烧录和调试联调 |
| 源码调试器 | `arm-none-eabi-gdb` | 已完成断点调试验证 |

本工程不使用 Keil，也不使用 Ninja。CMake 不是编译器，它负责根据 `CMakeLists.txt` 生成 Makefile；GNU Make 执行 Makefile，并调用 `arm-none-eabi-gcc` 完成编译、汇编和链接。

```text
C/C/ASM 源文件
      ↓
CMakeLists.txt
      ↓ CMake 配置
build/Makefile
      ↓ GNU Make 执行
arm-none-eabi-gcc
      ↓
application.elf / application.hex / application.bin
```

## 固件目录与分层目标

用户已经把LED BSP从 `User/led/` 迁移到 `User/bsp/led/`，并同步更新CMake源文件和包含路径。迁移后的正式ELF尺寸与迁移前一致，`GNU_STACK`仍为 `RW`，说明这次纯目录重构没有改变当前固件内容。下面是当前已经采用并供后续模块遵循的分层结构：

```text
firmware/
├── CMakeLists.txt
├── README.md
├── cmake/
│   └── arm-none-eabi-gcc.cmake       ARM GCC 交叉编译工具链文件
├── linker/
│   └── STM32F103ZETx_FLASH.ld        448 KiB应用区 / 64 KiB SRAM 链接脚本
├── Libraries/
│   ├── CMSIS/
│   │   └── startup/gcc/
│   │       └── startup_stm32f103xe.S GCC 启动文件和中断向量表
│   ├── FreeRTOS-Kernel/               FreeRTOS V11.3.0 最小内核依赖
│   │   ├── include/                   内核公共头文件
│   │   ├── portable/GCC/ARM_CM3/      GNU GCC Cortex-M3 端口
│   │   ├── portable/MemMang/heap_4.c  动态内存管理实现
│   │   ├── tasks.c                    任务和调度器核心
│   │   ├── list.c                     内核链表实现
│   │   └── queue.c                    Queue、Semaphore和Mutex的底层实现
│   ├── FWlib/                         STM32F10x 标准外设库
│   ├── ioLibrary_Driver-3.2.0/        WIZnet官方驱动快照、版本说明与许可证
│   │   └── Ethernet/                  当前编译wizchip_conf.c和W5500/w5500.c
│   ├── coreMQTT-2.3.1/                官方MQTT 3.1.1库、版本说明与许可证
│   └── MbedTLS-3.6.7/                 固定版本MD5子集及许可证
├── User/
│   ├── main.c                         程序入口；只调用板级初始化、应用创建并启动调度器
│   ├── FreeRTOSConfig.h               当前工程的FreeRTOS编译配置
│   ├── rtos/                          应用与FreeRTOS内核之间的集成层
│   │   ├── freertos_hooks.c           Malloc/栈溢出Hook
│   │   └── w5500/                     唯一W5500 Mutex访问门面
│   ├── app/                           应用Task和业务编排，按功能域继续分子目录
│   │   ├── adc/                       ADC周期采样、物理量换算和最新快照发布
│   │   ├── can/                       CAN应用协议、周期上报、命令与ACK
│   │   ├── console/                   有界行缓冲、行结束处理和只读命令分发
│   │   ├── diagnostics/               FreeRTOS任务、运行时间和Heap诊断报告
│   │   ├── event/                     Key Task与LED Task共享的按键事件契约
│   │   ├── ftp/                       FTP控制/数据状态机、流式下载和存储背压
│   │   ├── key/                       按键周期扫描、软件消抖和Queue生产者
│   │   ├── led/                       阻塞接收Queue事件并控制RGB LED
│   │   ├── mqtt/                      MQTT Task、MQTT Client、配置与Transport
│   │   ├── mqtt_business/             JSON业务、升级受理/取消、发送回执和结果补报
│   │   ├── network/                   芯片监督Task、永久TCP Client与网络状态契约
│   │   ├── output/                    CAN/MQTT共享的唯一输出控制服务
│   │   ├── serial/                    USART1 TX/RX Task、TX Queue、DMA与IDLE通知桥接
│   │   ├── startup/                   按依赖顺序创建Queue和应用Task
│   │   └── storage/                   候选槽、A/B元数据、MD5、升级意图和首次确认
│   └── bsp/                           板级支持层，按外设或器件继续分子目录
│       ├── adc/                       ADC1、PC1模拟输入、内部温度通道、校准和轮询转换
│       ├── board/                     调度器前的板级硬件初始化编排
│       ├── buzzer/                    板载蜂鸣器基础控制
│       ├── can/                       bxCAN初始化、收发、中断与错误快照
│       ├── key/                       PA0、PC13按键非阻塞读取BSP
│       ├── led/                       霸道 V2 RGB LED BSP：bsp_led.c/.h
│       ├── timer/                     TIM6运行时间统计计数器和回绕扩展
│       ├── usart/                     USART1 GPIO、TX/RX DMA、CNDTR和硬件中断状态处理
│       ├── w5500/                     SPI2、硬件复位、网络配置和有界Socket原语
│       └── w25q64/                    SPI1、外部Flash读写和有界器件操作
└── build/                             构建输出，不纳入 Git
```

TCP阶段提交`13f1b6b`曾使用单NET任务和阶段Echo测试夹具；该历史基线不再代表当前实现。当前本地提交`72e0a5a`已经接入独立MQTT Task、W5500 Mutex门面和coreMQTT CONNECT，Echo运行路径已移除；`main.c`仍只执行`BspBoard_Init()`、`AppStartup_Create()`和启动调度器。

当前正式架构中，`AppNetworkTask`监督W5500初始化和PHY；`AppMqttTask`独占Socket 0、TCP Client和MQTT上下文，`AppFtpTask`独占控制/数据Socket 1/2，`AppStorageTask`独占W25Q64。所有任务级W5500访问通过唯一Mutex门面进入BSP，FTP释放W5500 Mutex后才通过按值Queue移交数据给Storage；Storage状态是长度1的最新快照，由FTP和业务Task零等待`Peek`读取。完整所有权契约见[W5500多协议任务架构](../docs/W5500_MULTITASK_ARCHITECTURE.md)；运行期全芯片复位及协议Task静默协调尚未实现。

新增固件代码按职责落位，不能仅为省事堆在 `User/` 根目录：`Libraries/` 保存第三方内核和厂商库；`User/rtos/` 保存FreeRTOS应用侧Hook与适配；`User/app/<功能域>/` 保存Task入口和业务编排；`User/bsp/<外设或器件>/` 保存板级驱动。`main.c` 和全工程配置头可保留为根级入口，但不得逐步演变成业务实现集合。依赖方向应保持为：`main` 只调用必要的BSP初始化、应用模块注册和调度器启动，`app → bsp + FreeRTOS API`，`bsp → FWlib/CMSIS/硬件`；底层不得反向依赖应用层。

## 项目自编 C 代码风格

`firmware/User/` 及后续项目自编C代码统一采用：函数圆括号内部不留空格、控制关键字后留一个空格、强制转换写作 `(void)x`、无限循环写作 `for (;;)`、指针星号靠变量名，以及Allman大括号。Codex后续提供的项目代码示例也必须遵守该格式。

本规则不用于批量改写 `Libraries/` 中的FreeRTOS、CMSIS和FWlib第三方源码；引用上游源码讲解时可以保留其原始格式。完整强制规则见根目录 `AGENTS.md`，学习说明见 `FreeRTOS学习.md`。

## 硬件基线

- MCU：STM32F103ZET6，512 KiB 内部 Flash，64 KiB SRAM。
- 外部高速晶振：8 MHz；`SystemInit()` 将系统时钟配置为 72 MHz。
- 板载 RGB LED：共阳极，GPIO 输出低电平时点亮。
- 红灯：PB5；绿灯：PB0；蓝灯：PB1。
- 裸机基线程序每500 ms翻转一次红灯；首个FreeRTOS LED Task曾使用2000 ms阻塞延时完成调度验证。当前Queue Demo已改为事件驱动：KEY1稳定按下事件翻转绿灯，KEY2稳定按下事件翻转蓝灯，LED Task在无事件时阻塞等待Queue。
- 上电测试前需要确认 RGB LED 供电跳帽 J73 已接通。
- 调试下载使用 CMSIS-DAP，通过开发板 SWD 接口连接。
- 板载CH340G连接USART1：PA9为TX，PA10为RX，当前串口参数为115200、8-N-1、无流控。
- STM32F103固定DMA映射中，USART1_TX使用DMA1 Channel 4普通模式，USART1_RX使用DMA1 Channel 5循环模式。
- CAN1使用PB8/CAN_RX和PB9/CAN_TX，启用`GPIO_Remap1_CAN1`；bxCAN时钟来自PCLK1=36 MHz，500 kbit/s位时序使用Prescaler=4、BS1=15 TQ、BS2=2 TQ和SJW=1 TQ。
- 板载W5500独占SPI2：PB13/SCK、PB14/MISO、PB15/MOSI，PG9/CS、PG8/INT、PG15/RST；SPI Mode 0、MSB first，当前使用PCLK1 36 MHz的16分频，即2.25 MHz。
- 当前静态网络参数：MAC `02:53:54:4D:32:01`，STM32 `192.168.101.50`，掩码`255.255.255.0`，网关`192.168.101.1`；TCP验收服务地址为Windows `192.168.101.11:5000`。

## 构建环境检查

在 PowerShell 中确认以下命令均可执行：

```powershell
cmake --version
make --version
arm-none-eabi-gcc --version
arm-none-eabi-gdb --version
```

当前已验证的主要版本为：

```text
CMake                         4.3.1
GNU Make                      4.4.1
GNU Tools for STM32 / GCC    14.3.1
```

如果 PowerShell 提示无法识别 `make` 或 `arm-none-eabi-gcc`，应先检查 STM32CubeCLT 对应的 `bin` 目录是否已加入用户 `Path`，不要在 `CMakeLists.txt` 中写死本机绝对安装路径。

## 配置与构建

在 `firmware` 目录打开 PowerShell，第一次构建时执行：

```powershell
cmake -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
cmake --build build -- -j8
```

PowerShell 中需要把两个 `-D...` 参数整体放在引号内，避免参数中的 `.cmake` 被错误拆分。

日常修改代码后，只需要增量构建：

```powershell
cmake --build build -- -j8
```

清理构建结果：

```powershell
cmake --build build --target clean
```

如果更换了交叉编译器或生成器，可以重新配置 CMake 缓存：

```powershell
cmake --fresh -S . -B build -G "Unix Makefiles" "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake" "-DCMAKE_BUILD_TYPE=Debug"
```

## 构建输出

### 当前Application产物与地址

当前CMake目标`firmware`构建成功后生成：

```text
build/application.elf    带地址、符号和调试信息，用于Boot/Application联合下载和GDB
build/application.hex    Intel HEX格式，保留应用地址信息
build/application.bin    包含512字节描述头的完整FTP候选文件
build/application.map    链接映射，用于检查符号和内存占用
```

Application区为`0x08010000～0x0807FFFF`，上限448 KiB；描述头为
`0x08010000～0x080101FF`，向量表和执行体从`0x08010200`开始。描述头保存硬件型号、软件型号和
完整文件长度，保留字节全0；完整文件MD5包含描述头。`application.bin`通过FTP原样下载，
Boot安装前再校验并写入应用区。当前应用依赖独立Boot启动，不按下文历史`firmware.bin`的
`0x08000000`地址烧录。

2026-10-04本轮审查修复后的Debug构建通过，静态占用为：

```text
FLASH：87476 B / 448 KiB
RAM：47112 B / 64 KiB
text：87292 B
data：184 B
bss：46928 B
FreeRTOS heap_4：32768 B（已计入RAM静态占用）
```

链接保留了原有Newlib/libnosys系统调用未实现警告，不能写作“构建零警告”。Startup无条件
失败返回修正后，FTP和业务Task的可达路径恢复，因此不能沿用修复前被垃圾回收裁掉这些路径的
较小尺寸。上述数值是静态构建结果，不是完整升级实机验收或运行期Heap/栈余量证明。

### 历史构建尺寸与静态验收

以下裸机、FreeRTOS及外设增量记录属于迁移应用地址前的历史验收。保留其中的原产物名、
Flash容量和实测尺寸用于学习与版本追溯；它们不表示当前Application的地址或内存占用。

裸机基线 Debug 版本的历史构建结果：

```text
FLASH：1268 B / 512 KiB
RAM：2568 B / 64 KiB
编译和链接警告：0
```

ELF 静态检查已经确认：

- ELF 类型为 32 位 ARM、little-endian、soft-float ABI。
- 中断向量表位于 `0x08000000`。
- 程序入口为 `Reset_Handler`。
- 主栈初始地址为 `0x20010000`，即 64 KiB SRAM 末端。
- Flash 段和 RAM 段的加载地址、大小和权限正确。

### FreeRTOS最小源码接入构建结果

用户把五个最小FreeRTOS相关源文件和两个头文件目录加入CMake后，于2026-08-08使用同一GNU Make构建流程完成首次完整链接：

```text
FLASH：2072 B / 512 KiB
RAM：2704 B / 64 KiB
text：2068 B
data：4 B
bss：2700 B
编译错误：0
链接错误：0
链接警告：1（Newlib libc_a-memset.o缺少.note.GNU-stack标记）
```

相较裸机基线，当前增加：

```text
FLASH：2072 - 1268 = 804 B
RAM：  2704 - 2568 = 136 B
```

当前增量还不是最终FreeRTOS Demo占用。因为应用尚未调用 `xTaskCreate()`、`vTaskStartScheduler()` 或 `pvPortMalloc()`，`--gc-sections` 已把未引用的任务创建、Heap和调度器启动路径裁掉，8 KiB `ucHeap[]` 尚未进入最终ELF。当前保留的关键符号包括 `xTaskIncrementTick()`、`vTaskSwitchContext()`、`pxCurrentTCB`、`xTickCount`、栈溢出Hook以及FreeRTOS提供的三个异常入口。

ELF符号检查确认：

```text
SVC_Handler       全局Text强符号，来自FreeRTOS ARM_CM3 port.c
PendSV_Handler    全局Text强符号，来自FreeRTOS ARM_CM3 port.c
SysTick_Handler   全局Text强符号，来自FreeRTOS ARM_CM3 port.c
```

它们已经覆盖启动文件中的同名弱默认处理函数。当前尚未启动调度器，因此这一结果只证明链接路由正确，不代表Tick和任务切换已经完成实机验证。

首次链接时，链接器因预编译Newlib `memset` 对象缺少 `.note.GNU-stack` 而把最终 `GNU_STACK` 标记为 `RWE`。用户随后把 `-Wl,-z,noexecstack` 加入CMake链接选项并重新生成固件。只读检查确认当前 `firmware.elf` 晚于CMake修改时间，`GNU_STACK` 已变为 `RW`，且固件尺寸仍为 `text=2068 B`、`data=4 B`、`bss=2700 B`。该链接策略已经生效；本工程不采用只隐藏警告的 `--no-warn-execstack`。

### LED Task隔离编译结果

用户已把 `User/app/led/app_led_task.c` 和 `User/app/led` 加入CMake，并完成Fresh构建。只读检查确认Task对象文件和最终ELF均晚于CMake配置：

```text
app_led_task.c.obj：已生成
最终ELF中的vLedTask：无
最终ELF中的xTaskCreate：无
最终ELF中的vTaskStartScheduler：无
text/data/bss：2068/4/2700 B
GNU_STACK：RW
```

源文件已经通过编译，但 `main.c` 尚未引用Task入口和调度器API，所以 `--gc-sections` 把相关函数从最终ELF裁掉。这一结果只验收Task源文件和包含路径，不代表Task已经创建或运行。

### 首个Task创建后的构建结果

用户改造 `main.c`，调用 `xTaskCreate()`和 `vTaskStartScheduler()`后完成构建。只读检查得到：

```text
FLASH：5024 B / 512 KiB，约0.96%
RAM：11016 B / 64 KiB，约16.81%
text：5016 B
data：8 B
bss：11008 B
GNU_STACK：RW
```

相较Task尚未被引用时：

```text
FLASH：5024 - 2072 = 增加2952 B
RAM：11016 - 2704 = 增加8312 B
```

其中 `ucHeap`位于 `0x20000104`，长度为 `0x2000`，即8192 B，结束地址为 `0x20002104`。链接脚本随后继续预留C运行库Heap和MSP主栈，RAM静态预留最高到 `0x20002B08`，没有超出 `0x20010000`。

最终ELF已经保留：

```text
xTaskCreate
vTaskStartScheduler
pvPortMalloc
vLedTask
vApplicationMallocFailedHook
vApplicationStackOverflowHook
pxCurrentTCB
xTickCount
ucHeap
```

向量表中的SVC、PendSV和SysTick入口分别指向FreeRTOS端口提供的 `SVC_Handler`、`PendSV_Handler`和 `SysTick_Handler`。向量表中函数地址最低位为1，表示Cortex-M Thumb状态，不是地址错误。构建和静态链接验收通过。当前 `app_led_task.c` 使用 `pdMS_TO_TICKS(2 * 1000)`，在1 kHz Tick下得到2000 Tick，即每2 s翻转一次LED。由于 `main.c` 后续增加的学习注释晚于当前ELF，虽然注释不改变机器指令，仍需在GDB验证前再构建一次，使DWARF源码行号与当前文件同步。

### Queue与USART1 TX DMA闭环构建结果

按键Queue Demo完成后，工程继续加入FWlib USART/DMA驱动、`User/bsp/usart/`、`User/app/serial/`和私有有界TX Queue。当前Debug ELF的静态尺寸为：

```text
text：9760 B
data：8 B
bss：11024 B
Flash装载量：约9768 B / 512 KiB，约1.86%
RAM静态占用：约11032 B / 64 KiB，约16.83%
```

ELF中已经保留以下关键符号：

```text
xQueueGenericCreate
xQueueGenericSend
xQueueReceive
AppSerialTxTask_Create
AppSerial_Write
BspUsart1_TxDmaStart
BspUsart1_TxDmaHandleInterrupt
DMA1_Channel4_IRQHandler
ulTaskGenericNotifyTake
```

`DMA1_Channel4_IRQHandler`为全局Text强符号，已覆盖启动文件中的同名弱默认处理函数。BSP只处理USART/DMA寄存器与中断标志，不调用FreeRTOS；应用Serial模块保存Task Handle，并在ISR桥接中使用 `vTaskNotifyGiveFromISR()`唤醒Serial TX Task。私有TX Queue在运行时从当时的8 KiB FreeRTOS Heap分配，其他模块通过 `AppSerial_Write()`复制提交消息，不直接访问USART或DMA；诊断阶段已根据实测余量把Heap调整为24 KiB。该依赖方向保持为 `app → bsp + FreeRTOS API`。

### USART1 RX循环DMA闭环构建结果

加入DMA1 Channel 5循环接收、USART IDLE中断桥接和Serial RX Task后，当前Debug ELF的静态尺寸为：

```text
text：10432 B
data：8 B
bss：11288 B
Flash装载量：约10440 B / 512 KiB，约1.99%
RAM静态占用：约11296 B / 64 KiB，约17.24%
```

ELF中已经保留以下RX关键符号：

```text
AppSerialRxTask_Create
BspUsart1_RxDmaStart
BspUsart1_RxDmaGetWritePosition
BspUsart1_RxIdleHandleInterrupt
USART1_IRQHandler
```

`USART1_IRQHandler`为全局Text强符号，已覆盖启动文件中的弱默认入口。`DMA1_Channel5_IRQHandler`仍为弱默认符号，这是当前设计的预期结果：Channel 5只负责循环搬运，RX Task由USART IDLE事件唤醒，本阶段没有启用DMA半传输、传输完成或错误中断。

### 最小Console闭环构建结果

在不改变USART BSP、DMA配置、IDLE ISR和Serial TX所有权的前提下，工程新增纯应用模块 `User/app/console/`。Serial RX Task把DMA新增数据逐字节交给Console状态机，并在产生响应时继续通过 `AppSerial_Write()`复制提交。当前Debug ELF的静态尺寸为：

```text
text：11056 B
data：8 B
bss：11352 B
Flash装载量：11064 B / 512 KiB，约2.11%
RAM静态占用：11360 B / 64 KiB，约17.33%
```

ELF中已经保留以下关键符号：

```text
AppConsole_Init
AppConsole_ProcessByte
AppSerialRxTask_Create
AppSerial_Write
USART1_IRQHandler
DMA1_Channel4_IRQHandler
```

`USART1_IRQHandler`和 `DMA1_Channel4_IRQHandler`均为全局Text强符号；`DMA1_Channel5_IRQHandler`继续保持弱默认符号，符合循环DMA只借助CNDTR获取写位置、由USART IDLE通知RX Task的设计。Console没有新建Task或Queue，而是使用一个64字节静态行缓冲区和少量状态变量；缓冲区最多保存63个有效字符，最后一个字节保留给C字符串结束符 `\0`。当前Serial TX Queue单条消息上限已经扩展为512字节，可以复制承载完整任务诊断报告。

### FreeRTOS任务与Heap诊断构建结果

加入诊断模块、TIM6运行时间计数器并把FreeRTOS Heap调整为24 KiB后，当前Debug ELF静态尺寸为：

```text
text：17408 B
data：88 B
bss：28424 B
Flash装载量：17496 B / 512 KiB，约3.34%
RAM静态占用：28512 B / 64 KiB，约43.51%
```

ELF中已经保留 `AppRtosDiagnostics_BuildTaskList()`、`AppRtosDiagnostics_BuildHeapReport()`、`vTaskListTasks()`、`vTaskGetRunTimeStatistics()`、`BspRunTimeCounter_Init()`、`BspRunTimeCounter_GetValue()`和 `TIM6_IRQHandler`。TIM6以10 kHz计数，每个计数代表100 us；16位硬件计数器回绕由32位软件高位扩展，形成以 `uint64_t`返回的48位有效累计计数。TIM6中断使用逻辑优先级4且不调用FreeRTOS API，调试暂停时由DBGMCU冻结。

### ADC周期采样闭环构建结果

加入FWlib ADC驱动、`User/bsp/adc/`和`User/app/adc/`并创建ADC Task后，当前Debug ELF静态尺寸为：

```text
text：18400 B
data：88 B
bss：28432 B
Flash装载量：18488 B / 512 KiB，约3.53%
RAM静态占用：28520 B / 64 KiB，约43.52%
```

ELF中已经保留 `ADC_Init()`、`ADC_GetFlagStatus()`、`BspAdc_Init()`、`BspAdc_ReadRaw()`、`AppAdcTask_Create()`和私有任务入口 `prvAdcTask()`。ADC时钟由PCLK2六分频得到12 MHz；ADC1采用独立模式、单次转换和软件触发，PC1/ADC1_IN11使用55.5周期采样时间，内部温度通道ADC1_IN16使用239.5周期采样时间。ADC Task轮询EOC且带有有界超时，本阶段没有启用ADC中断或DMA。

### W5500与分层TCP构建结果

用户完成本阶段代码构建后，当前Debug ELF静态尺寸为：

```text
text：32472 B
data：132 B
bss：28928 B
Flash装载量：32604 B / 512 KiB，约6.22%
RAM静态占用：29060 B / 64 KiB，约44.34%
```

只读ELF检查确认`BspW5500_*`、`AppTcpClient_*`、`AppTcpEchoTest_*`、`AppNetworkTask_Create()`、`BspBoard_Init()`和`AppStartup_Create()`均已链接为全局Text强符号。构建产物时间晚于本阶段相关源码与CMake修改时间；本次TCP里程碑收尾没有重新运行会改写构建产物的构建命令。

## 下载与实机验收

### 当前Application与Bootloader交付

当前下载对象为独立Boot的`bootloader.elf`和应用的`application.elf`。此前两份镜像已由用户
烧录并完成读回校验；本轮修复后的程序尚未收到实机回归结果。Boot使用方法见
[Bootloader工程说明](../bootloader/README.md)，正常升级与再次复位的结果记录见
[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。P3已验收的下载只是把完整候选包写入W25Q64，
不能据此宣称Boot安装、首次确认及升级成功上报已实机通过。

### 历史裸机LED烧录验收

下列命令及验收是原`0x08000000`裸机LED基线，不作为当前Application单独复位启动步骤。
裸机 LED 基线已经由用户在真实硬件上完成以下验收：

- CMake + GNU Make + `arm-none-eabi-gcc` 配置、编译和链接成功。
- CMSIS-DAP 通过 SWD 与开发板正常连接。
- OpenOCD 成功下载并校验 `build/firmware.elf`。
- 复位后 PB5 红灯约每 500 ms 翻转一次。
- `arm-none-eabi-gdb` 可以连接目标并完成断点调试。

已验证的 OpenOCD 命令形式为：

```
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32f1x.cfg -c "adapter speed 1000" -c "program build/firmware.elf verify reset exit"
```

以上结果说明启动文件、链接脚本、系统时钟、GPIO、构建、下载和基础调试链路已经形成最小闭环，可以把后续 FreeRTOS 问题与裸机板级问题分开定位。

### FreeRTOS、USART1 TX/RX DMA与Console实机验收

用户已在真实硬件上完成以下稳定功能验收：

- FreeRTOS调度器正常启动，应用Task与Idle Task能够运行，`vTaskDelay()`和阻塞式等待不会阻塞整个CPU。
- Key Task周期扫描KEY1/KEY2并完成软件消抖，通过有界Queue发送事件；LED Task阻塞接收并分别翻转绿灯或蓝灯。
- USART1使用PA9/PA10和板载CH340G，115200、8-N-1、无流控配置能够被电脑串口工具正确接收。
- DMA1 Channel 4能够从静态发送缓冲区向USART1发送固定字符串。
- DMA完成中断能够清除硬件状态，并通过Direct-to-Task Notification唤醒Serial TX Task。
- Serial TX Task能够在 `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`返回后延时约1 s，再启动下一次发送；电脑端持续收到周期消息，证明DMA、ISR、Notification和任务状态转换闭环正常。
- DMA1 Channel 5能够把USART1_RX字节持续写入256字节循环缓冲区；USART IDLE中断只清除硬件状态并通知Serial RX Task。
- 在接入Console前的原始回显基线中，Serial RX Task能够根据CNDTR计算DMA下一写位置、维护软件读位置，并通过现有 `AppSerial_Write()`复制提交原始字节。
- 原始回显基线曾在两批之间等待IDLE并完成前一批回显：先输入200字节并完整回显，验证单条128字节上限下的128+72分块；再输入100字节并完整回显，验证从缓冲区位置200回绕后的尾部56字节加头部44字节处理。
- USART1_RX循环DMA在IDLE处理期间保持运行；ISR仍只清除硬件状态并通知Task，不复制或解析接收数据，也不生成Console响应。
- Serial RX Task逐字节调用Console状态机；手动输入能够跨多次IDLE通知累计，证明IDLE事件没有被误当作命令行结束。
- `help`、`version`、`task`、`heap`、`data_query`、未知命令和空行行为符合定义，均已完成实机验证。
- 单独CR、单独LF和CRLF均可结束命令，CRLF不会造成重复执行；退格能够安全修改当前行，行首退格不会导致长度下溢。
- 同一批接收数据中的 `help\r\nversion\r\n`能够按顺序各响应一次。
- 63个可打印字符仍作为合法长度进入未知命令分支；第64个可打印字符触发一次 `ERROR: line too long`，结束该行后下一条 `help`能够正常执行，证明超长丢弃状态可以恢复。
- `task`能够分别输出带表头的任务状态/栈表和运行时间/CPU表；`SERIAL_RX`在生成报告时为Running，Idle为Ready，其余等待型任务通常为Blocked。
- 实测Serial RX、Idle、Key、LED和Serial TX的栈高水位余量分别为348、118、104、96和96 words，当前均保留非零余量。
- `RunCount`单位为100 us，实测从614788继续增加到3212612，跨越多次TIM6 16位回绕后保持单调；空闲系统中Idle累计CPU占用为99%～100%。
- `heap`能够输出24 KiB `heap_4`的总量、当前空闲量和历史最小空闲量；重复执行任务与Heap诊断后，当前空闲量保持稳定，历史最小值只记录低水位。

有界Serial TX Queue已经通过两条启动消息的FIFO实机验证；RX循环DMA、IDLE通知、早期128字节发送分块和缓冲区回绕也已通过真实硬件验收。当前Console行协议、只读命令分发、任务/栈/CPU诊断和Heap诊断均已完成。参数解析、命令历史和更完整的交互编辑仍未实现。逐次断点、临时连接故障和工具输出不写入学习文档。

### ADC周期采样实机验收

用户已在真实硬件上确认：

- ADC1能够依次读取PC1/ADC1_IN11板载电位器和ADC1_IN16内部温度传感器的12位原始值。
- 转动板载电位器时，电位器原始值能够随旋钮位置变化；内部温度通道能够持续返回稳定有效的原始值。
- ADC Task每1 s完成一次两通道采样和串口报告，连续输出正常。
- ADC Task直接调用`AppSerial_Write()`，由现有Serial TX Queue和Serial TX Task完成复制、排队与DMA发送；ADC模块没有新增Queue或第二个Task。
- ADC Task大部分时间阻塞在周期延时中；任务状态、CPU、栈余量和24 KiB FreeRTOS Heap验收通过，未观察到持续内存下降、乱码或异常复位。
- 本阶段只验收原始值，不把内部温度传感器典型参数换算结果宣称为精确摄氏温度。

### bxCAN最小闭环实机验收

用户已在真实硬件上确认29位Classic CAN扩展数据帧的周期上报、Windows控制请求和应用层ACK均与`docs/protocol.md`一致；ADC帧使用ADC Task发布的真实最新样本，CAN Task未直接访问ADC BSP。无ACK/Error Passive、错误状态观测、通信恢复、发送超时与Queue异常可观察性已经完成回归；`CAN_ABOM`启用后恢复行为符合设计。连续双向收发2小时期间未观察到资源泄漏、异常复位或不可恢复通信中断，P1据此关闭。

### W5500与分层TCP实机验收

用户已在真实硬件上完成以下验收：

- W5500硬件复位、连续8次`VERSIONR=0x04`、静态MAC/IP/掩码/网关写入与读回、PHY DOWN/UP变化和PC Ping均正常。
- Socket 0能够连接Windows `192.168.101.11:5000`；TCP状态能够从连接、已建立、对端FIN、关闭进入下一轮连接或退避。
- 双Session自动验收逐字节核对ASCII和二进制数据，累计收发131700字节；超过64 KiB的W5500环形指针回绕、FIN尾部数据、正常EOF和首次自动重连均通过。
- Windows服务停止后，客户端按1、2、4、8、16秒增长并在30秒封顶，之后持续低频探测；不会因固定测试次数耗尽而永久停机。
- PHY恢复会跳过旧退避并立即开始Socket清理与连接重建；传输中拔线后可以恢复。
- 空连接收到FIN后能够正常关闭，但不会把零数据Session误判为健康会话并清除连续失败历史。
- 同步回归CAN周期帧、Console、NET任务栈和FreeRTOS Heap，没有观察到持续内存下降、异常复位或不可恢复通信中断。

长期保留的TCP基础为`bsp_w5500`的有界Socket原语及`app_tcp_client`的单连接生命周期、字节接口、FIN/SENDOK、持续退避和PHY恢复能力；`app_tcp_echo_test`仅是TCP阶段测试夹具。TCP阶段NET任务的单一所有权属于已验收历史基线，后续按多协议任务架构迁移：MQTT/FTP各自拥有Socket，`AppNetworkTask`只保留芯片监督职责，Echo在coreMQTT接管MQTT Task运行路径后退出构建。

## bxCAN错误状态与Bus-off管理

bxCAN使用发送错误计数器TEC和接收错误计数器REC实施CAN协议的故障限制。错误原因由`CAN_ESR.LEC`表示，节点当前严重程度由`EWGF`、`EPVF`和`BOFF`表示；“最近发生了哪种错误”和“节点现在处于什么状态”是两个不同维度。

```text
正常通信 / Error Active
        ↓ TEC或REC达到96
Error Warning（仍属于Error Active，只是警告标志置位）
        ↓ TEC或REC超过127
Error Passive
        ↓ 仅发送错误继续恶化，TEC超过255
Bus-off（控制器退出总线）
```

| 状态或标志 | 硬件条件 | 含义 |
| --- | --- | --- |
| Error Active | TEC和REC均未超过127 | 节点能够正常参与通信，并可发送主动错误标志 |
| Error Warning / `EWGF` | TEC或REC大于等于96 | 错误计数已经接近被动门限，但节点仍是Error Active |
| Error Passive / `EPVF` | TEC或REC大于127 | 节点仍可通信，但错误处理行为受到限制，避免故障节点持续干扰总线 |
| Bus-off / `BOFF` | 九位TEC超过255 | 仅由发送错误触发；控制器退出总线，不再发送数据或硬件ACK |

Error Passive中的“Passive”描述的是错误报告方式，不是禁止发送普通报文。节点仍可参与仲裁、发送和接收数据帧，也会对正确接收的帧发送硬件ACK；区别是它检测到错误时只能发送由6个隐性位组成的Passive Error Flag，不能像Error Active节点那样用显性位主动破坏当前错误帧。Error Passive节点在一次发送后还必须经过额外的Suspend Transmission等待，才能主动发起下一帧。真正完全退出总线的是Bus-off。

CAN故障限制还有一个重要例外：当发送节点已经处于Error Passive，仅因为没有检测到显性ACK而产生Acknowledgment Error，并且发送Passive Error Flag期间也没有检测到显性位时，TEC不再因这次错误增加。因此单节点运行或唯一的分析仪处于Listen Only时，节点可能停留在Error Passive而不会仅凭无ACK进入Bus-off；无ACK测试可以验证ACK错误和Error Passive，但不能作为必然触发Bus-off的测试方法。

SPL的`CAN_GetLSBTransmitErrorCounter()`只能读取九位TEC的低八位，因此TEC溢出后的Bus-off判断必须以`BOFF`标志为准，不能只看该函数返回值。成功收发会按CAN规则降低对应错误计数；计数变化不是简单的“每次错误固定加1”。

`CAN_InitTypeDef.CAN_ABOM`控制Automatic Bus-Off Management：

- `CAN_ABOM = DISABLE`：硬件不会自动退出Bus-off，需要软件执行恢复流程。
- `CAN_ABOM = ENABLE`：bxCAN在Bus-off后监测总线，检测到128次“连续11个隐性位”后按协议自动恢复。

P1错误管理的目标选择是：

```c
xCanInit.CAN_ABOM = ENABLE;
```

自动恢复由bxCAN硬件完成；CAN Task只负责读取错误快照、记录状态转换并在Bus-off期间停止提交新报文，不在CAN错误ISR中重新初始化外设。当前`bsp_can.c`已经设置为`ENABLE`，并已通过集中实机测试确认异常解除后能够恢复通信。

## 固件技术路线

```text
P0  CMake + GNU Make + ARM GCC裸机LED冒烟测试（已完成）
 ↓
P0  FreeRTOS V11.3.0最小调度闭环（已完成）
 ↓
P0  Key Task → Queue → LED Task（已完成并形成Git基线）
 ↓
P0  USART1 TX DMA → ISR → Task Notification（已完成实机验证）
 ↓
P0  有界Serial TX Queue与单一发送所有者（已完成实机验证）
 ↓
P0  USART1 RX循环DMA + IDLE回显（已完成实机验证）
 ↓
P0  最小Console行协议与命令分发（已完成实机验证）
 ↓
P0  Task状态/栈/CPU与Heap诊断（已完成实机验证）
 ↓
P0  ADC1双通道软件触发采样 → Task轮询EOC → Serial TX Queue（已完成实机验证）
 ↓
P1  bxCAN + Windows CAN分析仪最小双向闭环（已完成实机验证）
 ↓
P1  ADC最新样本、错误状态与Bus-off恢复（已完成实机验收）
 ↓
P1  2小时稳定性测试与阶段基线（已完成）
 ↓
P2  W5500 SPI2 + 复位 + 官方驱动适配 + VERSIONR读回（已完成实机验证）
 ↓
P2  静态网络参数 + PHY + Ping（已完成实机验证）
 ↓
P2  分层单Socket TCP + 双向字节流 + FIN + 持续重连（已完成实机验收）
 ↓
P2  W5500 Mutex访问门面 + Network Supervisor + MQTT Task（已完成实机验收）
 ↓
P2  EMQX + MQTT 3.1.1 + 心跳/温度上报 + 输出控制ACK（已完成实机验收）
 ↓
P3  W25Q64存储基础 + Windows FTP流式下载 + MD5校验（正常路径已实机验收）
 ↓
P4  独立Boot + 内部Flash安装 + 应用首次确认 + MQTT升级（源码已实现，实机未反馈）
 ↓
收尾 开发结项审查 + 文档与简历定稿 + 完整根目录Git归档（WPF取消）
```

P1、P2和P3正常路径已完成实机验证，P4源码已实现。本轮完成两项应用审查修复和开发交付文档，
随后按用户决定结束开发并归档；完整升级与再次复位保留为未验证事项，故障测试已跳过。
Windows业务调试和最终观察使用MQTTX，不开发C#、WPF、MVVM或MQTTnet Dashboard。

## FreeRTOS V11.3.0 学习型移植记录

本节完整保留早期学习过程和当时的阶段状态、文件名、容量及测试结论；其中“当前”“下一阶段”
均对应相应历史增量，不替代上文的当前交付状态。

本次移植由用户亲自完成，目标不仅是让 Demo 运行，还要理解每个内核文件、处理器端口、配置项和异常入口在系统中的作用。Codex 只负责讲解、协助分析用户提供的现象，以及根据真实进度维护本文档，不直接修改固件代码或构建文件。

### 1. 移植边界和完成标准

第一版只建立如下最小闭环：

```text
复位进入 main()
    ↓
初始化 LED GPIO
    ↓
创建一个 LED Task
    ↓
vTaskStartScheduler() 启动调度器
    ↓
SysTick 产生 RTOS Tick
    ↓
LED Task 使用 vTaskDelay() 周期阻塞和唤醒
    ↓
PB5 红灯约每 2000 ms 翻转一次
```

本阶段不加入 CAN、W5500、MQTT、FTP、W25Q64 或 OTA 代码。最小 Demo 必须同时满足：构建和链接无警告、调度器不返回、LED 任务持续运行、错误 Hook 不触发、GDB 能观察任务以及 SysTick/PendSV/SVC 相关入口。

### 2. 总体步骤与当前状态

| 步骤 | 学习目标 | 当前状态 |
| --- | --- | --- |
| 0. 固化裸机基线 | 排除工具链、启动、链接、时钟、GPIO、下载器和 GDB 问题 | 已完成实机验证 |
| 1. 下载内核 | 从官方仓库取得并核验 FreeRTOS Kernel V11.3.0 | 已下载到 `resources/` 并完成版本核验 |
| 2. 认识内核组成 | 理解通用内核、处理器端口、内存管理器和应用配置的边界 | 已完成第一轮目录学习 |
| 3. 选择 Cortex-M3 端口 | 从完整上游快照中选择 `portable/GCC/ARM_CM3`，理解 SysTick、PendSV 和 SVC | 端口已接入构建，三个异常入口的ELF强符号和调度器实机运行均已确认 |
| 4. 选择堆实现 | 选择 `heap_4.c`，理解任务创建所需的动态内存 | 初始8 KiB基线已验证；诊断阶段根据实测余量调整为24 KiB |
| 5. 创建应用配置 | 编写工程自己的 `FreeRTOSConfig.h` | 已由用户创建；最小内核与应用Hook的只读语法检查通过 |
| 6. 接入 CMake | 只把最小 Demo 所需源文件和包含路径加入目标 | 已由用户完成并首次链接成功；`noexecstack`已验证生效，正式ELF的`GNU_STACK`为`RW` |
| 6A. 规范目录分层 | 把板级驱动统一放到 `User/bsp/<模块>/`，应用Task放到 `User/app/<功能域>/` | LED、Key、USART BSP和LED、Key、Serial应用模块均已按规则落位并完成构建/实机回归 |
| 7. 改造入口程序 | 删除裸机 SysTick 忙等，创建 LED Task 并启动调度器 | 已由用户完成并成功生成ELF |
| 8. 构建和静态检查 | 检查异常符号、内存占用、映射文件和编译警告 | 符号、Heap、向量表、内存、GNU_STACK、Queue和DMA1 Channel 4中断强符号均已复核 |
| 9. 烧录和 GDB 验证 | 验证调度器、Tick、上下文切换、任务阻塞和错误 Hook | 调度器、Task运行、Queue阻塞/唤醒和基础GDB调试已完成实机验证 |
| 10. 最小 Demo 验收 | 连续运行并记录结果，形成可回退的 Git 基线 | Key Task → Queue → LED Task已完成实机验证、提交、推送并建立`freertos-queue-demo-v0.1`标签 |
| 11. USART1 TX DMA同步 | 学习DMA缓冲区生命周期、完成中断和Task Notification | 固定字符串、DMA完成通知和有界TX Queue均已实机验证 |
| 12. USART1 RX循环DMA同步 | 学习CNDTR、软件读位置、回绕、IDLE清除和ISR到Task通知 | 原始字节回显、128字节分块及200+100字节回绕均已实机验证 |
| 13. 最小Console行协议 | 学习有界行缓冲、CR/LF/CRLF、退格、超长恢复和只读命令分发 | `help`、`version`、错误响应及63/64字符边界均已完成构建与实机验收 |
| 14. FreeRTOS运行与Heap诊断 | 学习任务状态、栈高水位、运行时间统计及Heap当前/历史余量 | `task`、`heap`、TIM6回绕和重复查询稳定性均已完成构建与实机验收 |
| 15. ADC1双通道周期采样 | 学习ADC时钟、采样时间、软件触发、EOC轮询和单一外设所有权 | PC1/ADC1_IN11电位器与ADC1_IN16内部温度通道已通过构建和实机验证；单ADC Task直接复用现有Serial TX Queue |
| 16. bxCAN最小双向闭环 | 学习位时序、过滤器、邮箱、硬件ACK、FIFO0 ISR、Queue和应用层ACK | 真实ADC上报、控制、ACK、错误状态、自动恢复和2小时稳定性均已完成实机验收 |
| 17. W5500 SPI与网络基线 | 学习SPI时序、片选边界、硬件复位、驱动回调、静态网络参数、PHY和外设所有权 | 官方驱动适配、连续`VERSIONR = 0x04`、配置读回、PHY变化和Ping均已完成实机验证 |
| 18. 分层TCP字节流 | 学习Socket寄存器、有界单步操作、部分收发、FIN、连接生命周期和持续退避 | 双Session、131700字节、超过64 KiB回绕、FIN/EOF、断服退避、PHY恢复和资源回归均已完成实机验收 |

每完成一步，都应把实际命令、结果、遇到的问题和验证结论补充到本节，不能提前把计划写成已完成结果。

### 下一阶段FreeRTOS学习路线

首个LED Task已经扩展为按键Queue Demo，USART1 TX/RX DMA与Task Notification也已分别形成闭环。后续继续按单一机制逐步验收，不在一次变更中同时启用全部外设和同步机制：

```text
非阻塞按键BSP和轮询Task（已完成构建）
    ↓
Queue传递按键事件（已完成构建）
    ↓
USART1 TX DMA与Task Notification（已完成实机验证）
    ↓
有界Serial TX Queue与单一发送所有者（已完成实机验证）
    ↓
USART1 RX循环DMA与IDLE中断（已完成实机验证）
    ↓
最小Console行协议和命令分发（已完成实机验证）
    ↓
Task状态、栈、CPU与Heap诊断（已完成实机验证）
    ↓
ADC1双通道软件触发、Task轮询EOC和周期串口报告（已完成实机验证）
    ↓
P1 bxCAN + Windows CAN分析仪最小双向闭环（已完成实机验收）
    ↓
W5500 SPI2、硬件复位、官方驱动适配、网络配置、PHY和Ping（已完成实机验证）
    ↓
分层TCP字节流、FIN、持续退避和PHY恢复（已完成实机验收）
```

历史 `User/Key/`阻塞扫描示例已由分层后的 `User/bsp/key/`和 `User/app/key/`替代，历史EXTI与串口示例不直接加入当前CMake。`User/app/startup/`负责按依赖创建Queue和Task，`User/bsp/board/`只编排调度器前的BSP初始化。TX方向保持Serial TX Task为唯一所有者，ADC1由单个ADC Task独占。进入MQTT前，W5500因为即将被MQTT Task、FTP Task和监督Task共同访问而具有真实互斥需求：唯一Mutex放在`User/rtos/w5500/`访问门面中，每个协议Task仍独占自己的Socket。具体FreeRTOS原理见[FreeRTOS学习.md](FreeRTOS学习.md)，目标网络架构见[W5500多协议任务架构](../docs/W5500_MULTITASK_ARCHITECTURE.md)。

### 3. 官方源码下载和版本核验

只使用独立的官方内核仓库，不下载未经固定版本的 `main` 分支：

- 官方仓库：`https://github.com/FreeRTOS/FreeRTOS-Kernel`
- 指定版本源码：`https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.0`
- 指定版本发布页：`https://github.com/FreeRTOS/FreeRTOS-Kernel/releases/tag/V11.3.0`
- 官方发布对应的短提交号：`9b777ae`

当前采用 ZIP 快照方式，避免在独立的 `firmware` Git 仓库中意外创建嵌套仓库：

1. 从 V11.3.0 发布页的 Assets 下载 `Source code (zip)`。
2. 在工程外的临时目录解压。
3. 检查版本、许可证和必要文件。
4. 将完整上游快照保留在 `resources/` 中，用于学习、版本追溯和查阅未启用模块。
5. 从完整快照中选择最小 Demo 需要的内核文件、头文件、Cortex-M3 端口和内存管理器，由用户手动复制到 `firmware/Libraries/FreeRTOS-Kernel/`。
6. 保留许可证和版本记录，并确认固件依赖目录中没有多套一层目录或内嵌 `.git`。

正确路径示例：

```text
firmware/Libraries/FreeRTOS-Kernel/include/FreeRTOS.h
firmware/Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c
firmware/Libraries/FreeRTOS-Kernel/portable/MemMang/heap_4.c
```

错误的多层路径示例：

```text
firmware/Libraries/FreeRTOS-Kernel/FreeRTOS-Kernel-11.3.0/include/FreeRTOS.h
```

下载后至少检查以下文件是否存在：

```text
include/FreeRTOS.h
include/task.h
portable/GCC/ARM_CM3/port.c
portable/GCC/ARM_CM3/portmacro.h
portable/MemMang/heap_4.c
tasks.c
list.c
LICENSE.md
History.txt
```

本次实际下载和核验记录（2026-08-04）：

```text
完整上游快照：resources/开发板资料/FreeRTOS-Kernel-11.3.0/
固件目标目录：firmware/Libraries/FreeRTOS-Kernel/
```

核验结果：

- `include/task.h` 中的 `tskKERNEL_VERSION_NUMBER` 为 `"V11.3.0"`。
- 主版本、次版本和构建版本宏分别为 `11`、`3`、`0`。
- `History.txt` 首部记录 V11.3.0 于 2026 年 3 月发布。
- `include/FreeRTOS.h`、`tasks.c`、`list.c` 和 `queue.c` 存在。
- `portable/GCC/ARM_CM3/port.c` 与 `portmacro.h` 存在。
- `portable/MemMang/heap_4.c` 存在。
- `LICENSE.md`、官方 `README.md` 和 `History.txt` 存在。
- 下载目录没有 `.git`，不会形成嵌套 Git 仓库。

完整快照中的 `.gitmodules` 指向第三方扩展端口，但本项目需要的 `portable/GCC/ARM_CM3` 位于主内核仓库中且已存在，因此不需要为了本次 STM32F103 最小移植初始化第三方端口子模块。

最小内核文件选取验收记录（2026-08-04）：

- 用户已将完整 `include/` 复制到固件依赖目录。
- 已复制 `portable/GCC/ARM_CM3/port.c` 和 `portmacro.h`。
- 已复制 `portable/MemMang/heap_4.c`，未混入其他 Heap 实现。
- 已复制最小任务 Demo 需要的 `tasks.c` 和 `list.c`。
- 已保留 `LICENSE.md`、官方 `README.md` 和 `History.txt`。
- 目标目录中的版本宏仍为 V11.3.0。
- 未复制 `queue.c`、`timers.c`、`event_groups.c`、`stream_buffer.c` 或 `croutine.c`。
- 目标目录没有 `.git`，不会在 `firmware` 仓库中形成嵌套仓库。
- 此时只完成依赖文件准备；尚未加入 CMake，也尚未生成 `FreeRTOSConfig.h`，因此不能称为已经接入 FreeRTOS。

### 4. 内核顶层目录和源文件含义

官方压缩包的核心结构如下：

```text
FreeRTOS-Kernel-11.3.0/
├── include/
├── portable/
├── examples/
├── tasks.c
├── list.c
├── queue.c
├── timers.c
├── event_groups.c
├── stream_buffer.c
├── croutine.c
├── LICENSE.md
├── README.md
└── History.txt
```

各部分职责如下：

| 路径 | 作用 | 本次最小 Demo 如何处理 |
| --- | --- | --- |
| `include/` | FreeRTOS 的公共头文件和内核共享声明，例如 `FreeRTOS.h`、`task.h`、`queue.h`、`list.h` 和 `portable.h`。应用通过这些头文件使用 RTOS API | 整个目录加入编译器头文件搜索路径，不修改官方文件 |
| `portable/` | 把通用内核适配到不同 CPU、编译器和内存分配方案。这里是“移植”最核心的目录 | 只选择 GCC 的 Cortex-M3 端口和一个内存管理器，不编译其他架构端口 |
| `examples/` | 官方提供的配置模板、示例或测试参考，不属于运行时内核本体 | 只用于学习和参考，不直接加入固件目标 |
| `tasks.c` | 任务调度核心，包含任务创建、删除、延时、阻塞/就绪状态转换以及调度器启动等逻辑 | 最小 Demo 必须编译 |
| `list.c` | 内核内部双向链表实现。就绪列表、延时列表等调度数据结构依赖它 | 最小 Demo 必须编译；应用通常不直接调用它 |
| `queue.c` | 队列实现，也是信号量和互斥量的底层基础，因为这些对象复用了队列机制 | 纯 LED Task 暂时可不使用；引入队列或信号量时再加入 |
| `timers.c` | 软件定时器实现，并创建 Timer Service/Daemon Task 处理回调 | 第一版不用软件定时器，不加入 |
| `event_groups.c` | 事件组实现，用若干 bit 表示多个事件，适合一个任务等待多个条件 | 第一版不用，不加入 |
| `stream_buffer.c` | 字节流缓冲区和消息缓冲区实现，适合连续字节或可变长消息传递 | 第一版不用，不加入；后续串口/网络数据通道可再评估 |
| `croutine.c` | 旧式 Co-routine 实现，以更低资源代价在一个栈式环境中协作运行，不等同于普通 FreeRTOS Task | 本项目使用普通任务，不启用也不加入 |
| `LICENSE.md` | FreeRTOS Kernel 的开源许可证 | 必须保留并纳入 Git，不能删除 |
| `README.md` | 官方仓库说明、使用入口和集成提示 | 保留，作为上游资料 |
| `History.txt` | 内核版本历史和变更记录，可用于核对 V11.3.0 | 保留，用于版本追溯 |

`include/` 解决“应用和内核使用哪些声明”，顶层 `.c` 文件实现“与具体 MCU 无关的调度与同步机制”，`portable/` 解决“这些通用机制如何在当前 CPU、编译器和内存模型上运行”。三者共同组成一个可工作的 FreeRTOS 内核。

### 4.1 `FreeRTOS.h` 与 `FreeRTOSConfig.h` 的区别

`FreeRTOS.h` 和 `FreeRTOSConfig.h` 不是重复文件，也不是二选一关系：

| 文件 | 提供者 | 职责 | 是否修改官方文件 |
| --- | --- | --- | --- |
| `include/FreeRTOS.h` | FreeRTOS Kernel | 内核总入口头文件，定义基础类型、公共宏、内核对象结构和配置检查，并连接通用内核与端口层 | 不修改 |
| `User/FreeRTOSConfig.h` | 当前应用工程 | 告诉同一套内核如何服务于本项目，例如 CPU 时钟、Tick、任务优先级数量、Heap 大小、中断优先级和启用功能 | 由项目创建和维护 |

本项目复制的 V11.3.0 `FreeRTOS.h` 在第 58 行直接包含：

```c
#include "FreeRTOSConfig.h"
```

包含之后，`FreeRTOS.h` 会检查应用是否定义了必要配置。例如缺少任务最小栈、最大优先级数量或调度方式时，会通过 `#error` 停止编译。这说明内核源码故意不替具体产品决定这些参数。

典型包含关系为：

```text
main.c
  ├─ include "FreeRTOS.h"
  │      ├─ include "FreeRTOSConfig.h"   当前应用的选择
  │      └─ 连接 portable.h/portmacro.h   当前 CPU 端口能力
  └─ include "task.h"                    任务 API 声明
```

应用通常先包含 `FreeRTOS.h`，再包含所用功能的专用 API 头文件：

```c
#include "FreeRTOS.h"
#include "task.h"
```

`FreeRTOS.h` 不是“所有 API 函数声明的集合”。任务 API 主要在 `task.h`，队列 API 在 `queue.h`，信号量 API 在 `semphr.h`；`FreeRTOS.h` 提供这些模块共同依赖的基础定义和当前工程配置。

之所以不能把配置直接写进官方 `FreeRTOS.h`，是因为同一个 FreeRTOS Kernel 可以同时服务于完全不同的工程：一个 MCU 可能运行在 72 MHz、需要 1 kHz Tick 和 12 KiB RTOS Heap，另一个 MCU 可能运行在其他频率、使用不同 Tick 和静态内存。把项目参数与上游内核分离，还能避免升级内核时覆盖本地配置。

对当前目录理解的准确表述是：

1. `include/` 包含公共 API 头文件，也包含内核内部共享的类型、宏和端口接口；并非每个头文件都由应用直接调用。
2. `portable/` 适配的是“编译器 + CPU 架构”和内存分配策略。`portable/GCC/ARM_CM3` 负责 Cortex-M3 的 SysTick、PendSV、SVC、临界区和上下文切换，但不负责 STM32F103 的 GPIO、RCC 或具体外设初始化。
3. `portable/MemMang/heap_4.c` 是独立于 ARM_CM3 端口的 FreeRTOS 动态内存策略，用于从 SRAM 中为动态任务的 TCB、任务栈和其他内核对象分配空间。
4. `list.c` 实现内核内部链表；`tasks.c` 在这些链表之上实现任务创建、状态转换、延时和调度器核心。不能简单理解为两个互不相关的“链表管理”和“任务管理”模块。

### 4.2 `FreeRTOSConfig.h` 的六类配置

第一次阅读官方模板时不应按文件从上到下机械记忆，而应先把宏分成六类：

| 类别 | 解决的问题 | 典型宏 |
| --- | --- | --- |
| 时钟与 Tick | CPU 每秒运行多少周期，RTOS 多久产生一次系统节拍 | `configCPU_CLOCK_HZ`、`configTICK_RATE_HZ`、可选的 `configSYSTICK_CLOCK_HZ` |
| 调度策略 | 是否抢占、是否时间片轮转、允许多少级任务优先级 | `configUSE_PREEMPTION`、`configUSE_TIME_SLICING`、`configMAX_PRIORITIES` |
| 内存与任务栈 | 动态/静态创建方式、RTOS Heap 大小、Idle Task 最小栈 | `configSUPPORT_DYNAMIC_ALLOCATION`、`configTOTAL_HEAP_SIZE`、`configMINIMAL_STACK_SIZE` |
| Cortex-M 中断 | NVIC有效位、优先级分组、`BASEPRI`边界，以及哪些中断可以调用 FreeRTOS `FromISR` API | `configMAX_SYSCALL_INTERRUPT_PRIORITY`；当前 V11.3.0 ARM_CM3 端口自行把PendSV/SysTick设为最低优先级 |
| 调试和 Hook | 断言、栈溢出、分配失败、Idle/Tick 回调 | `configASSERT`、`configCHECK_FOR_STACK_OVERFLOW`、`configUSE_MALLOC_FAILED_HOOK` |
| 功能和 API 裁剪 | 是否编译互斥量、软件定时器，以及是否暴露某些任务 API | `configUSE_MUTEXES`、`configUSE_TIMERS`、`INCLUDE_vTaskDelay` |

这些宏大多是编译期开关，不是运行时变量。改变配置后需要重新编译内核源码，不能期望程序运行后再动态改变调度器结构。

### 4.3 时钟与 Tick：把 72 MHz 变成 RTOS 时间

当前 CMSIS 系统文件已经确认：

```text
SystemCoreClock = 72,000,000 Hz
```

`configCPU_CLOCK_HZ` 告诉 FreeRTOS Cortex-M3 端口 CPU 核心时钟频率。它可以使用已经由 CMSIS 维护的 `SystemCoreClock`，也可以写成经过板级验证的固定常量；后续建立配置文件时再结合依赖关系选择具体写法，不能同时维护两个可能不一致的时钟事实。

`configTICK_RATE_HZ` 表示每秒产生多少次 RTOS Tick。候选值 1000 Hz 的含义是：

```text
1 秒 / 1000 Tick = 每个 Tick 1 ms
```

V11.3.0 的 ARM_CM3 `port.c` 使用以下关系配置 SysTick：

```text
SysTick LOAD = configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ - 1
```

若 SysTick 使用 72 MHz 内核时钟，Tick 设为 1000 Hz，则：

```text
72,000,000 / 1000 - 1 = 71,999
```

SysTick 从 71,999 递减到 0，共计数 72,000 个时钟周期，因此每 1 ms 进入一次 Tick Handler。当前 ARM_CM3 端口在没有单独定义 `configSYSTICK_CLOCK_HZ` 时，会自动令它等于 `configCPU_CLOCK_HZ`；只有 SysTick 使用不同外部时钟源时才需要单独定义。

当任务执行：

```c
vTaskDelay(pdMS_TO_TICKS(500U));
```

在 1000 Hz Tick 下，`pdMS_TO_TICKS(500U)` 得到 500 Tick。任务会进入阻塞态，调度器可以运行其他任务；这与裸机 `delay_ms()` 占用 CPU 忙等 500 ms 有本质区别。

Tick 频率不是越高越好：频率越高，延时分辨率越细，但 Tick 中断和调度判断的 CPU 开销也越高。1 kHz 是本项目最小学习 Demo 的候选基线，最终结果需要通过实机构建和运行验证后记录。

### 4.4 调度策略和任务优先级

最小 Demo 计划使用抢占式调度：

- `configUSE_PREEMPTION=1`：更高优先级任务变为就绪态时，可以抢占当前低优先级任务。
- `configUSE_TIME_SLICING=1`：多个同优先级就绪任务可以在 Tick 到来时轮转。
- `configMAX_PRIORITIES`：定义可用任务优先级数量，而不是某个任务的实际优先级。

如果 `configMAX_PRIORITIES=5`，可用任务优先级为：

```text
0、1、2、3、4
```

FreeRTOS 任务优先级数值越大，任务优先级越高；Idle Task 固定使用 `tskIDLE_PRIORITY`，其数值为 0。这个规则与 Cortex-M NVIC 的“中断优先级数值越小、硬件优先级越高”正好相反，后续不能混淆：

```text
FreeRTOS Task：数值越大，优先级越高
Cortex-M IRQ： 数值越小，优先级越高
```

第一版只有一个 LED Task 和内核自动创建的 Idle Task。LED Task 后续可设置为 `tskIDLE_PRIORITY + 1`，使其高于 Idle Task；调用 `vTaskDelay()` 后 LED Task 阻塞，Idle Task 才获得运行机会。

### 4.5 `heap_4.c`、RTOS Heap 与任务栈

选用 `heap_4.c` 表示通过 FreeRTOS 的 `pvPortMalloc()`/`vPortFree()` 管理一块 RTOS 专用 Heap。动态创建一个任务时，内核通常需要从这里分配两块内存：

```text
任务控制块 TCB    保存任务状态、优先级、栈顶等信息
任务栈            保存局部变量、函数调用现场和切换时的寄存器
```

相关配置分工如下：

- `configSUPPORT_DYNAMIC_ALLOCATION=1`：允许 `xTaskCreate()` 动态创建任务。
- `configTOTAL_HEAP_SIZE`：决定 `heap_4.c` 管理的总空间。
- `configMINIMAL_STACK_SIZE`：主要决定内核自动创建的 Idle Task 的最小栈深度，不等于所有任务都必须使用这个栈大小。
- `xTaskCreate()` 的栈深度参数：决定该具体任务自己的任务栈大小。

在 Cortex-M3 端口中，`StackType_t` 是 32 位，因此栈深度单位通常是 4 字节的“字”，不是字节。例如：

```text
栈深度 128 × 4 字节 = 512 字节
```

`configTOTAL_HEAP_SIZE` 也不是 STM32F103 全部 64 KiB SRAM。SRAM 还要容纳：

- `.data` 和 `.bss` 全局/静态数据。
- FreeRTOS 的 Heap 数组。
- 中断和启动阶段使用的主栈 MSP。
- 当前链接脚本为 C 运行环境预留的 Heap/Stack 区域。

因此第一版即使选择一个保守的 RTOS Heap 候选值，也必须在链接完成后结合 `firmware.map` 和 `--print-memory-usage` 检查总 RAM，而不是只看 `configTOTAL_HEAP_SIZE`。当前只学习内存模型，具体数值将在创建配置文件时明确，并在实机验证后定稿。

### 5. `portable/` 为什么是移植重点

FreeRTOS 的任务、队列和延时逻辑可以跨处理器复用，但保存寄存器、切换栈、启动第一个任务以及配置 Tick 定时器都与处理器和编译器有关。因此 `portable/` 通常按“编译器 + CPU 架构”组织。

本工程使用：

```text
编译器：arm-none-eabi-gcc
CPU：Cortex-M3
端口：portable/GCC/ARM_CM3
```

其中：

- `port.c` 实现 Cortex-M3 的调度器启动、SysTick 配置以及与上下文切换有关的底层逻辑。
- `portmacro.h` 定义栈类型、Tick 类型、临界区、任务切换和中断屏蔽等端口宏。
- Cortex-M3 通过 SVC 启动第一个任务，通过 PendSV 完成上下文切换，通过 SysTick 提供周期 Tick。
- 不能同时编译 ARM_CM3、ARM_CM4F、ARM_CM3_MPU 或其他端口，否则会出现架构不匹配或重复符号。

### 6. `portable/MemMang/` 和 `heap_4.c`

FreeRTOS 提供多种动态内存实现，应用一次只能选择一种：

| 文件 | 特点 | 是否适合本项目当前阶段 |
| --- | --- | --- |
| `heap_1.c` | 只分配、不释放，结构最简单且确定性强 | 可学习，但不适合作为后续长期工程基线 |
| `heap_2.c` | 支持释放，但不会合并相邻空闲块，长期运行可能碎片化 | 不选择 |
| `heap_3.c` | 封装 C 库 `malloc/free`，依赖 C 运行库及其线程安全行为 | 不选择 |
| `heap_4.c` | 支持分配和释放，并合并相邻空闲块，是常见单一 RAM 区域方案 | 本项目选择 |
| `heap_5.c` | 支持多个不连续内存区域，配置更复杂 | 当前 STM32F103 单 SRAM 最小 Demo 不需要 |

选择 `heap_4.c` 后，`FreeRTOSConfig.h` 中的 `configTOTAL_HEAP_SIZE` 决定 FreeRTOS 堆大小。任务控制块和动态创建的任务栈都从这个堆分配；它与链接脚本中的 C 运行库 Heap/主栈预留不是同一个概念，后续要结合 MAP 文件检查 64 KiB SRAM 的总占用。

### 7. 后续每一步要理解的内容

后续移植不直接一次性复制最终答案，而按以下顺序完成：

1. 下载并核验 V11.3.0，确认目录和版本来源。
2. 阅读 `FreeRTOS.h`、`task.h`、ARM_CM3 的 `port.c/portmacro.h`，建立通用内核与端口层概念。
3. 创建工程自己的 `FreeRTOSConfig.h`，逐项解释时钟、Tick、优先级、堆、栈和 Hook 配置。
4. 修改 CMake 源文件列表和包含路径，并解释每个加入项为什么必要。
5. 将裸机 `delay_ms()` 替换为任务和 `vTaskDelay()`，解释忙等与阻塞的区别。
6. 核对启动文件中的 SVC、PendSV、SysTick 弱符号如何被 FreeRTOS 端口接管。
7. 构建并检查 ELF、MAP、异常符号和 RAM/Flash 增量。
8. 烧录后用断点验证任务入口、SysTick、PendSV、错误 Hook 和调度器不返回。
9. 完成长时间运行验证，并将真实结果补充到本文档。

## 当前范围与限制

- coreMQTT连接历史基线为提交`72e0a5a`；其后的Network/MQTT/TCP简化、ADC物理量换算、`data_query`、control订阅、业务JSON和输出控制均已实机回归。W25Q64与FTP正常下载已实机通过；独立Boot、首次确认和MQTT升级已实现，完整升级及再次复位结果尚未反馈。FTP及Boot故障测试按用户决定跳过，不列为已通过。
- FreeRTOS V11.3.0最小内核已经接入；调度器、Queue和Task Notification已完成真实硬件验证。
- USART1 TX普通DMA、RX循环DMA、IDLE事件、CNDTR位置计算、原始字节回显基线和最小Console均已完成实机验证。
- 当前Console最多接收63个可打印ASCII字符，精确匹配小写`help`、`version`、`task`、`heap`和`data_query`；支持CR、LF、CRLF和退格，超长输入会丢弃整行并在行结束后恢复。参数解析、大小写归一、设备端逐字符回显和命令历史尚未实现。
- 当前RX只依赖IDLE和CNDTR，未启用DMA1 Channel 5半传输/传输完成中断；不宣称支持无IDLE的持续数据流或单次连续满256字节，DMA追满一圈可能覆盖未处理数据并造成读写位置相等。
- Serial RX Task当前分配512 words，实测Stack High Water Mark剩余348 words；Serial TX Task分配256 words，实测剩余96 words。当前保持测量后的安全余量，不仅凭“未触发栈溢出Hook”缩减栈。
- Serial TX Queue长度为4，单条消息上限为512字节；当前`heap_4`总量为32 KiB，历史24 KiB实测结论保留在对应阶段。最终升级增量的运行Heap余量和栈高水位未实测，不能用链接成功替代。外部RAM未接入链接脚本或FreeRTOS Heap。
- ADC1双通道原始采样已经完成构建与实机验证：PC1/ADC1_IN11读取板载电位器，ADC1_IN16读取内部温度传感器；单个ADC Task使用软件触发和EOC轮询，每1 s覆盖发布最新完整快照，不再周期发送串口报告。
- ADC Task以3.3 V、12位满量程和STM32F103数据手册典型温度参数换算电位器电压及CPU温度；`data_query`读取最新快照并按需输出，MQTT每10秒发布当前物理量。内部温度是未补偿实际VDDA与芯片个体误差的估算值，不是精密测温；换算、命令和MQTT上报已经实机回归。
- CAN1真实ADC上报、控制、应用层ACK、错误状态观测、自动Bus-off恢复和2小时稳定性均已完成实机验收。
- W5500 SPI、网络参数、PHY、Ping和分层TCP均已完成实机验收。当前Socket 0由MQTT Task独占，连接Windows EMQX的1883端口；Echo源码及构建路径已由用户移除。NET只监督芯片和PHY；运行期全芯片复位的完整静默协调仍未接入，不据已有代次字段宣称该能力完成。
- 升级只提供候选镜像完整性校验、安装中断后的有限重试和未确认启动的有限尝试；单应用区没有旧版本自动回滚，MD5不提供固件真实性认证。首次确认不依赖MQTT；QoS 0和本机SENDOK不保证MQTTX最终显示或跨重启恰好一次送达。
- WPF已取消；本轮开发结项不增加新业务，也不把未反馈的P4完整实机验收写成已通过。
- 不在固件源码或 README 中保存真实 MQTT/FTP 用户名、密码、证书私钥或设备唯一凭据。
- `build/` 中的 ELF、HEX、BIN、MAP 和中间文件不提交 Git。

## README 维护规则

后续每次代码更新，都要在同一个 Git 提交中同步检查并更新本文件。至少在以下内容变化时更新对应章节：

- 工具链、依赖版本或构建命令。
- 目录结构、源文件或 CMake 目标。
- 硬件引脚、时钟、存储空间或下载方式。
- 已完成功能、实机验证结果和当前限制。
- FreeRTOS、CAN、W5500、MQTT、FTP 或 Bootloader 的接入状态。
- 当前阶段和下一步技术路线。

影响整个系统架构或项目阶段的变化，还必须同步更新根目录 `README.md`、`docs/PROJECT_CONTEXT.md` 和 `docs/NEXT_STEPS.md`。
