# 下一步与验收条件

本文记录当前阶段、进入下一实现阶段前必须完成的准备项，以及各里程碑的验收边界。

## 目录

- [当前阶段](#当前阶段)
- [已完成：应用交接构建与静态检查](#已完成应用交接构建与静态检查)
- [当前增量：首次烧录与正常启动实机验证](#当前增量首次烧录与正常启动实机验证)
- [当前增量：应用首次启动确认](#当前增量应用首次启动确认)
- [下一增量：MQTT升级命令与结果上报](#下一增量mqtt升级命令与结果上报)
- [已完成基线](#已完成基线)
- [P1 bxCAN准备清单](#p1-bxcan准备清单)
- [P1 bxCAN实施步骤](#p1-bxcan实施步骤)
- [P1验收条件](#p1验收条件)
- [后续阶段待办](#后续阶段待办)
- [后续里程碑](#后续里程碑)
- [当前需要用户继续确认](#当前需要用户继续确认)

## 当前阶段

2026-10-04用户明确决定：更新Application并检查Boot/Application后结束开发，不再继续实机步骤或增加业务。当前按“代码开发结项”归档，而非“P4全链路实机验收通过”；完整记录与手动Git步骤见[项目结项](PROJECT_CLOSEOUT.md)，最终简历见[项目简历](项目简历.md)。

Boot源码及历史Debug/Release构建已完成，用户提供过Boot/Application双镜像烧录读回成功日志。P1 CAN、P2 TCP/MQTT业务及P3正常FTP下载已有实机证据；P4普通跳转、候选安装、首次`CONFIRMED`及再次复位尚未提供最终运行结果，继续记为未验收。用户跳过的故障测试仍是跳过，不记为通过。

最终审查发现并按本轮明确授权修正：`app_startup.c`的两个孤立失败返回块、`app_ftp_task.c`重复快照造成的成功终态进度回落。修正后的Application Debug构建通过，FLASH 87476字节/448 KiB、RAM 47112字节/64 KiB；BIN长87476字节与512字节描述头中的长度相同。链接保留既有newlib/nosys占位警告。Boot源码本轮未改，未烧录或执行Git写操作。

后续仅剩用户执行归档：新项目根仓库、新空远端，保留旧`firmware` Git历史。以下实现方案、里程碑与实机步骤作为历史对照保留，不再表示当前必须继续的开发任务。Boot和Application始终独立，只通过镜像描述头、元数据格式及地址交接；代码检查不替代硬件验证。

## 已完成：应用交接构建与静态检查

应用`main()`已经在 VTOR、`__DSB()`、`__ISB()`之后加入`__enable_irq()`，用户已构建成功。
已有检查确认描述头位于`0x08010000`、大小为512字节，向量表位于`0x08010200`。
Boot Debug/Release 也已构建通过。上述检查不再重复；应用现有系统调用占位警告仍保留记录。
后续直接使用`bootloader/build/bootloader.hex`和`firmware/build/application.hex`。

## 当前增量：首次烧录与正常启动实机验证

按用户最新要求，不再安排命令验证、首烧备份或人为故障测试，直接验证正常启动。
本次不新增代码、构建模式或测试模块，不执行 Git 写操作。两份 HEX 由用户烧录；
这会覆盖板上原来位于`0x08000000`的旧运行固件，不是只把候选包下载进 W25Q64。

### 1. 烧录 Boot 和 Application

保持实验开发板稳定供电，连接此前使用的 CMSIS-DAP，停止其他调试/下载会话。
串口助手保持115200波特率，电脑 EMQX 保持运行，网线接好；不下发下载或升级命令。
不要按现有 VS Code F5，其调试配置仍指向遗留`build/firmware.elf`。

以下历史烧录命令从项目根目录的PowerShell执行（不再作为结项必做项）。HEX 自带目标地址，不手填 BIN 偏移，
不执行全片擦除或 Option Byte 修改；`verify`只是烧录工具的常规写后读回，
不另开命令验证或错误测试步骤。烧录后复位运行：

```powershell
openocd `
    -f 'interface/cmsis-dap.cfg' `
    -c 'transport select swd' `
    -f 'target/stm32f1x.cfg' `
    -c 'adapter speed 1000' `
    -c 'program bootloader/build/bootloader.hex verify' `
    -c 'program firmware/build/application.hex verify' `
    -c 'reset run' `
    -c 'shutdown'
```

Boot 从`0x08000000`启动，Application 文件从`0x08010000`开始，
Boot 跳转到`0x08010200`向量表指定的应用入口。
本次按未请求升级的普通启动场景进行，不主动修改 W25Q64 元数据。
`READY_TO_UPGRADE`不会自动触发安装；实际若已有升级请求、安装中或未确认启动记录，
Boot 仍按该持久记录处理，不因为本轮跳过错误测试而强行绕过。

### 2. 观察正常运行

Boot 当前没有串口日志，不要求看到“Boot成功”等新增打印。正常进入应用后观察：

1. 串口恢复应用启动日志，W5500/MQTT 能到达`CONNECTED`、`SUBSCRIBING`和`READY`。
2. MQTTX 收到`Client2Server/heartbeat`和`Client2Server/temperature`，连续观察约30秒。
3. 手工按一次开发板复位键，仍能恢复上述日志和上报，不依赖调试器手工跳到应用。

能正常运行后，由用户反馈串口或 MQTT 截图，记录为普通 Boot → Application 跳转实机通过。
若现场确实出现失败，再针对实际现象处理，不预先制造错误场景。
用户已提供Boot和Application两份ELF各自Programming Finished及Verified OK的日志，烧录读回成功；普通启动仍待运行反馈，也不代表候选安装、首次健康确认或掉电恢复已经验收。
正常启动通过后，下一增量接入应用升级请求与成功启动确认，再进行一次正常升级实机验证。

## 当前增量：应用首次启动确认

用户已同意首版确认条件：FreeRTOS 调度器已运行、Storage 初始化和元数据读取成功、
ADC 已发布第一份有效样本。不要求 MQTT、FTP、PC 或网络在线，不新增自检 Task。

2026-10-03用户已录入本节长期产品代码，逻辑静态检查通过；本节保留为对照资料。
后续同名Storage函数以[MQTT升级实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)为最新替换依据：
它扩展了共享快照、升级控制完成和所有元数据失败的锁定出口；不要再用本节旧片段覆盖新实现。
本轮没有运行构建或实机验证，不能据此认定首次确认路径已实机通过。
Boot 与 Application 的双镜像烧录和工具读回校验已有用户日志证据；普通启动仍待运行结果，
首次确认要在后续一次正常升级中验证，当前不制造故障或人工修改元数据。

### 1. 范围、依赖与接口契约

本步唯一源码目标为 `firmware/User/app/storage/app_storage_task.c`。
不修改 Boot、ADC Task、MQTT/FTP、Startup、`main.c`、头文件、CMake 或链接脚本，
不新增公共请求类型、Task、Queue、通知、全局标志或第二套状态机。

- Storage Task 继续独占 W25Q64、A/B 记录和现有串行执行状态。
- ADC Task 仍是 ADC 和样本 Queue 的唯一写入者；Storage 调用已有
  `AppAdc_TryReadLatestSample()`，只取得一个局部副本，不消费样本、不直接采样。
- 参考本地 FreeRTOS Kernel V11.3.0 的 `include/task.h`、`include/queue.h`：
  Task 上下文执行、零等待读取样本。该接口的 `xQueuePeek(..., 0U)` 不移除样本，
  所以不会抢走 CAN 或 MQTT 所需的数据；不移植第三方确认框架。
- Storage 公共请求、状态 Queue 和创建接口均不变；现有状态 Queue 仍只有 FTP 一个消费者。
  已有 CMake 包含路径覆盖 ADC，不新增库或构建配置。
- 只允许已选出的有效 `BOOT_PENDING` 在运行状态 `UPGRADE_LOCKED`、内部操作空闲时发起确认。
  Task 已执行本身证明调度器在运行，该运行状态来自成功初始化和元数据恢复，
  再取得有效 ADC 样本即可，不另存“调度器已启动”“ADC已就绪”或“已确认”的标志。
- `CONFIRMED` 提交必须复制当前有效记录的完整文件长度、MD5、安装次数和启动次数；
  只改变代次、持久状态和错误值。仅新下载 `PREPARING` 才重新建立记录并清零次数。
  这也为下一步升级请求保留正确的元数据推进语义，不靠重启后的下载 RAM 缓存重建记录。
- 继续通过现有擦除、主体写入、读回、最后写 `CMIT`、整条解码流程提交。
  启动提交不等于成功；只有最终读回验证完成后，才把运行状态改为 `IDLE`。
- 等 ADC 时使用100 ms阻塞等待周期，不是忙循环，也不是额外启动延时或确认超时。
  已有1000 ms扇区擦除和100 ms页编程超时保持不变；当前1000 Hz Tick可准确表达100 ms。
  一直没有有效样本时保持未确认，不自动制造复位；3次限制仍由 Boot 在每次放行启动前记账。
- 确认提交失败只记录 RAM 故障并禁止新下载，本次运行不自动重试、不按下载失败去提交
  `FAILED`。旧有效记录不擦除；提交结果不确定时，复位由 A/B 选择实际有效记录。
- 已经 `CONFIRMED` 的普通复位不重复提交；`READY_TO_UPGRADE` 仍是候选包状态，
  不能因当前旧应用运行正常就把候选包确认掉。

所有新增代码均为长期代码，没有阶段测试代码。新增运行期存储只有一个局部
`AppAdcSample_t`，沿用现有512 words任务栈；新增日志沿用128字节局部缓冲。
未测量新栈高水位，不仅凭现有分配量宣称余量已验收。

### 2. 增加包含、检查周期和已有日志函数声明

在现有应用头文件包含区加入：

```c
#include "app_adc_task.h"
```

在现有 `APP_STORAGE_*_MS` 宏附近加入：

```c
#define APP_STORAGE_BOOT_CONFIRM_POLL_MS          100U
```

在 `static mbedtls_md5_context s_xFirmwareMd5;` 后、私有函数定义前加入：

```c
static void prvReportStatus(void);
```

这是下方已有函数的前向声明，不是新增一个函数或包装层。

### 3. 替换 prvStartMetadataCommit

整段替换现有同名函数，其他元数据擦写函数保留：

```c
static bool prvStartMetadataCommit(AppStoragePersistentState_t xPersistentState,
                                   AppStorageError_t xError)
{
    AppStorageMetadataRecord_t xRecord;

    if (s_xOperation != APP_STORAGE_OPERATION_IDLE)
    {
        return false;
    }

    if (xPersistentState == APP_STORAGE_PERSISTENT_STATE_PREPARING)
    {
        /* 新下载才建立新记录，重置旧事务的安装和启动次数。 */
        memset(&xRecord, 0, sizeof(xRecord));
        xRecord.ulFirmwareLength = s_xStorageStatus.ulFirmwareLength;
        memcpy(xRecord.aucFirmwareMd5, s_aucExpectedFirmwareMd5, APP_STORAGE_MD5_DIGEST_SIZE);
    }
    else
    {
        if (s_ulActiveMetadataAddress == UINT32_MAX)
        {
            return false;
        }

        /* 同一事务只推进状态；长度、MD5和Boot写入的次数沿用有效记录。 */
        xRecord = s_xActiveMetadata;
    }

    xRecord.ulGeneration = (s_ulActiveMetadataAddress == UINT32_MAX) ?
        1U : (s_xActiveMetadata.ulGeneration + 1U);
    xRecord.xState = xPersistentState;
    xRecord.ulLastError = (uint32_t)xError;

    if (AppStorageMetadata_Encode(&xRecord, s_aucMetadataWrite) == false)
    {
        return false;
    }

    /* 仍由现有状态机执行擦除、主体写入、CMIT及最终读回校验。 */
    s_xOperation = APP_STORAGE_OPERATION_METADATA_ERASE_START;
    return true;
}
```

新下载从 `PREPARING` 建立记录；随后下载状态推进和首次启动确认均保留该事务的记录。
尤其不能用启动后尚未由 PREPARE 填充的 `s_aucExpectedFirmwareMd5` 覆盖 Boot 的 MD5。

### 4. 给 prvContinueAfterMetadata 增加 CONFIRMED 分支

在现有 `case APP_STORAGE_PERSISTENT_STATE_FAILED:` 之前插入：

```c
        case APP_STORAGE_PERSISTENT_STATE_CONFIRMED:
            /* 只有完整记录提交并读回通过后，才解除下载锁定。 */
            s_xStorageStatus.xState = APP_STORAGE_STATE_IDLE;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            prvPublishStatus();
            prvReportStatus();
            return true;
```

这个函数只在最终读回和解码通过后调用，因此此处才是解锁、发布和打印成功的位置。
不在 `prvStartMetadataCommit()` 返回true时提前发布成功。

### 5. 保留启动确认失败的下载锁定

在 `prvEnterTransactionFailure()` 中，仅把下面这个赋值：

```c
    s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;
```

替换为：

```c
    /* 启动确认失败不能按下载失败处理，否则新PREPARE会覆盖未确认记录。 */
    s_xStorageStatus.xState =
        (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING) ?
        APP_STORAGE_STATE_DEVICE_FAULT : APP_STORAGE_STATE_FAILED;
```

该函数中原有“仅PREPARING/DOWNLOADING/VERIFYING允许持久化下载失败”的判断保留。
在该函数的末尾，将最后的 `prvPublishStatus();` 替换成：

```c
    prvPublishStatus();
    if (s_xStorageStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT)
    {
        prvReportStatus();
    }
```

不要替换其他函数的同名语句；确认失败保留旧有效记录，不新增一个持久失败记录或确认重试计数。

### 6. 替换 prvReportStatus

整段替换同名函数，增加已选中记录的 `persistent` 诊断字段：

```c
static void prvReportStatus(void)
{
    char acLine[128];
    int iLength;

    iLength = snprintf(acLine, sizeof(acLine),
                       "STORAGE: state=%u persistent=%u error=%u txn=%lu written=%lu/%lu "
                       "JEDEC=%02X %02X %02X\r\n",
                       (unsigned int)s_xStorageStatus.xState,
                       (unsigned int)s_xActiveMetadata.xState,
                       (unsigned int)s_xStorageStatus.xError,
                       (unsigned long)s_xStorageStatus.ulTransactionId,
                       (unsigned long)s_xStorageStatus.ulWrittenLength,
                       (unsigned long)s_xStorageStatus.ulFirmwareLength,
                       (unsigned int)s_xStorageStatus.ucManufacturer,
                       (unsigned int)s_xStorageStatus.ucMemoryType,
                       (unsigned int)s_xStorageStatus.ucCapacity);

    if ((iLength > 0) && ((size_t)iLength < sizeof(acLine)))
    {
        (void)AppSerial_Write((const uint8_t *)acLine, (uint16_t)iLength);
    }
}
```

`state` 是 RAM 运行状态，`persistent` 是本任务最新选中并验证的元数据状态，不是两套执行状态机。
未成功加载有效记录时只结合错误和设备故障判断，不把日志中的默认0当作Flash擦除证据。

### 7. 替换 prvStorageTask

整段替换同名任务入口，保留其后的 `AppStorageTask_Create()` 不变：

```c
static void prvStorageTask(void *pvParameters)
{
    AppStorageRequest_t xRequest;
    AppAdcSample_t xAdcSample;
    TickType_t xWaitTicks;

    (void)pvParameters;
    mbedtls_md5_init(&s_xFirmwareMd5);
    prvInitializeDevice();

    for (;;)
    {
        xWaitTicks = (s_xOperation == APP_STORAGE_OPERATION_IDLE) ?
            portMAX_DELAY : APP_STORAGE_BUSY_POLL_TICKS;

        /*
         * 执行到Task循环本身就说明调度器已经运行。
         * UPGRADE_LOCKED且有效记录为BOOT_PENDING，说明初始化和元数据读取成功。
         * 不等待MQTT，也不读取候选槽来判断当前运行的应用。
         */
        if ((s_xOperation == APP_STORAGE_OPERATION_IDLE) &&
            (s_xStorageStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
            (s_ulActiveMetadataAddress != UINT32_MAX) &&
            (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING))
        {
            if (AppAdc_TryReadLatestSample(&xAdcSample) && xAdcSample.xValid)
            {
                if (prvStartMetadataCommit(APP_STORAGE_PERSISTENT_STATE_CONFIRMED,
                                           APP_STORAGE_ERROR_NONE))
                {
                    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
                    xWaitTicks = APP_STORAGE_BUSY_POLL_TICKS;
                }
                else
                {
                    /* 不自动重试，不冒称CONFIRMED；保留旧记录并禁止新下载。 */
                    s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
                    s_xStorageStatus.xError = APP_STORAGE_ERROR_METADATA;
                    prvPublishStatus();
                    prvReportStatus();
                }
            }
            else
            {
                /* ADC尚未提供有效样本时定期醒来；普通空闲仍阻塞等待请求。 */
                xWaitTicks = pdMS_TO_TICKS(APP_STORAGE_BOOT_CONFIRM_POLL_MS);
            }
        }

        if (xQueueReceive(s_xRequestQueue, &xRequest, xWaitTicks) == pdPASS)
        {
            prvDispatchRequest(&xRequest);
        }

        if (s_xOperation != APP_STORAGE_OPERATION_IDLE)
        {
            prvServiceCurrentOperation(xTaskGetTickCount());
        }
    }
}
```

普通空闲仍阻塞等待请求；只有未确认且尚无有效ADC样本时周期醒来。
确认写入期间仍保持 `UPGRADE_LOCKED`，新下载请求继续由既有状态检查拒绝。
读回解码成功后，持久状态自身结束确认路径，不需要 `s_xBootConfirmed` 等重复标志。

### 8. 用户构建与正常流程验收

完成以上七处录入后，在 `firmware/` 的 PowerShell 中执行：

```powershell
cmake --build build --parallel
```

Boot 未改，不用重新构建或重复烧录 Boot。需要更新板上应用时，沿用当前ELF烧录方式：

```powershell
openocd -f interface/cmsis-dap.cfg `
    -c "transport select swd" `
    -f target/stm32f1x.cfg `
    -c "adapter speed 500" `
    -c "program build/application.elf verify reset exit"
```

烧录地址由ELF决定，禁止改成遗留 `firmware.elf`，不执行全片擦除或Option Byte修改。
若板上实际已经是 `BOOT_PENDING`，不要用修改后的不同应用覆盖后反复复位：
Boot 要按原元数据 MD5 校验内部文件。该场景应保留与元数据匹配的镜像走正常确认，
不能靠清空计数或绕过 MD5 解决。本步当前只是普通启动后的应用接入准备。

正常启动回归只观察应用恢复、ADC有效，以及原心跳/温度继续工作，不额外安排命令或故障矩阵。
手工烧录 Application 不会自动创建 `BOOT_PENDING`；已有 `READY_TO_UPGRADE`、`EMPTY`
或 `CONFIRMED` 时不应看到一次新的启动确认，这是正常的，不能强写元数据来制造验收。

后续接入升级命令并完成一次正常升级后，应看到：

```text
STORAGE: state=7 persistent=7 error=0 ...
STORAGE: state=1 persistent=8 error=0 ...
```

第一行是等待确认且下载锁定，第二行是已确认且空闲；只有持久 `persistent=8` 才是
`CONFIRMED`，原来的运行 `state=8` 仍表示 `FAILED`。日志是非阻塞提交，Queue满时可能丢日志，
不能因此反向改变持久提交结果。后续正常复位从 `CONFIRMED` 恢复空闲，不再次提交确认或消耗次数。

本步不接入 `fileupgradecommand`、取消升级或跨重启MQTT升级结果；这些仍是下一增量。
构建、改码和烧录由用户执行；本轮仅检查了用户录入的源码，没有编译或板上验证。

本轮另发现`app_storage_task.c`第617行条件赋值为103列，属于非阻塞格式问题。
用户可将该赋值按下面换行；没有逻辑、接口或行为变化，与本节第5步原示例一致：

```c
    s_xStorageStatus.xState =
        (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING) ?
        APP_STORAGE_STATE_DEVICE_FAULT : APP_STORAGE_STATE_FAILED;
```

## 下一增量：MQTT升级命令与结果上报

以下接口、发送失败策略和跨重启结果上报方案已获用户批准。
逐文件手工改码入口为[MQTT升级命令与结果上报实施文档](MQTT_UPGRADE_IMPLEMENTATION_STEP.md)。
2026-10-04用户明确授权Codex录入本节相关Application源码并构建，Debug构建已通过；未烧录或实机验收，源码和构建完成不等于正常升级已完成。
不修改Boot，不新增Task、构建模式、共享工程文件或业务协议字段。

正常链路为：业务Task校验`fileupgradecommand` → Storage完整提交`UPGRADE_REQUESTED`
→ MQTT发送受理ACK并收到匹配`SENT`回执 → 发送`upgrade=3, rate=0`
→ 原会话末段SENDOK完成并收到匹配`SENT`回执 → 业务Task复位
→ Boot安装 → 新应用提交`CONFIRMED` → MQTT联网后上报`upgrade=1, rate=100`。
当前只在稳定实验供电和无危险负载的开发板上验收；本地输出在放行复位前置为关闭。
这不是实际危险执行器的持续联锁，不能把一次输出状态检查写成已实现安全保护。

已冻结的两项接口扩展：

1. Storage仍是元数据唯一写入者。现有长度1状态Queue改为非消费的最新快照：
   FTP从`xQueueReceive`改为单次零等待`xQueuePeek`，业务Task同样只读Peek，
   不新增Storage转发层或第二个状态Queue。快照里的持久状态和代次只从已选中的有效记录生成。
   Peek不移除元素，禁止保留旧的循环排空写法；FTP读取契约随本次手工改码一并迁移。
   升级请求带观察到的元数据代次，Storage在受理时核对，避免候选已经被新下载替换；
   请求完成还需内部关联标识，不能借用固定的业务`messageId=8`。
2. MQTT新增长度1发送回执Queue，最多一个需要回执的出站请求；本机请求编号0表示普通消息，
   心跳和遥测不需要回执。业务Task必须核对请求编号与原MQTT会话，且结果为`SENT`。
   成功条件不仅是`MQTT_Publish()`成功，还必须是同一TCP会话
   的最后一段发送已通过W5500 `SENDOK`完成。现有Transport返回正数只证明数据已被网络层接受；
   `AppTcpClient_Send()`会先置`xTxInFlight=true`再返回，当前不能把它当作可立即复位的证据。
   MQTT Task等待时继续服务连接，不持Mutex等待，也不由协议层直接执行设备复位。
   依据为本地官方coreMQTT v2.3.1的`source/core_mqtt.c`及现有TCP SEND/SENDOK实现，
   适用于本项目QoS 0、非阻塞W5500 Transport，不修改第三方源码。
   待回执状态从当前出站消息的非零请求编号推导，TCP排空由原状态和`xTxInFlight`推导；
   不新增第二套发送状态机。发布成功后的末段排空期限为5000 ms，业务每条等待为15000 ms。

已批准的发送失败策略：Queue满、发送失败、超时或会话变化时不自动复位；
已完整提交的`UPGRADE_REQUESTED`继续保留且禁止新下载，等待重发开始命令或取消。
**此时手工复位仍会让Boot按已持久请求安装**，不能把“不自动复位”理解为请求已经撤销。
取消只允许在业务Task尚未放行复位时生效，由Storage完整提交回`READY_TO_UPGRADE`后回复成功；
进入Boot安装后不能再通过MQTT取消。元数据提交结果不确定时运行期保持锁定，
不得仅因RAM还显示旧READY就允许新下载，复位后按实际有效A/B记录重新判断。
取消须等旧的需回执出站请求结束后再发取消ACK；不匹配的迟到回执丢弃，不能触发复位。

升级结果在每个新的MQTT READY会话补报一次最新有效持久终态，不宣称跨重启恰好一次送达；
QoS 0本身不提供MQTT PUBACK，也不能保证MQTTX最终显示。
只有有效`CONFIRMED`且确实来自安装的记录才报升级成功，新下载重用元数据后不沿用旧成功记录。
有效`FAILED`且错误为`CANDIDATE_REJECTED_BEFORE_INSTALL`、旧应用仍可运行时上报
`upgrade=2, rate=0`；停留Boot故障态不能通过MQTT上报。
实施文档把请求关联、有限超时、取消截止点、安全输出、Queue容量及失败出口落实为
一套业务执行状态，不增加重复的“已受理/待重启/已确认”全局布尔变量。

## 已完成基线

- [x] 开发板：野火STM32F103霸道V2。
- [x] MCU：STM32F103ZET6，使用bxCAN。
- [x] CAN：PB8/PB9，CAN1重映射，板载SIT1042AQT/3收发器。
- [x] 网络：板载W5500，SPI2接口。
- [x] 固件暂存：板载W25Q64，容量8 MB。
- [x] USB-CAN：创芯科技CAN分析仪至尊版，只支持Windows。
- [x] STM32构建工具链：ARM GCC 14.3.1 + CMake + GNU Make，不使用Keil。
- [x] CMSIS-DAP + OpenOCD下载、GDB调试和USART1日志链路。
- [x] FreeRTOS Kernel V11.3.0调度器、任务、Queue、Task Notification和24 KiB `heap_4`基线。
- [x] USART1 TX DMA、RX循环DMA、IDLE通知、最小Console和任务/CPU/栈/Heap诊断。
- [x] ADC1软件触发单次转换：PC1/ADC1_IN11板载电位器与ADC1_IN16内部温度传感器由单个ADC Task顺序采样；原始采样、最新快照物理量换算、`data_query`按需报告和MQTT周期上报均已实机验证。

## P1 bxCAN准备清单

### 1. 确认CAN电气条件

- [x] 确认板上J40的1-2跳帽已连接，使CAN/485_3V3正常供电。
- [x] 确认STM32端和CAN分析仪端各有一个120 Ω终端电阻；断电测量CANH与CANL约60 Ω。
- [x] 确认CANH接CANH、CANL接CANL，并建立可靠共地。
- [x] 记录创芯科技CAN分析仪驱动和厂商软件版本。
- [x] 确认分析仪使用正常工作模式而非只听模式，使STM32发送帧能够获得ACK。

### 2. 确认波特率与位时序

- [x] P1初始CAN波特率确定为500 kbit/s。
- [x] 当前HCLK为72 MHz、PCLK1为36 MHz，bxCAN内核时钟直接来自PCLK1；CAN不套用APB定时器的倍频规则。
- [x] 位时序采用Prescaler=4、SJW=1 TQ、BS1=15 TQ、BS2=2 TQ；总计18 TQ，采样点88.89%，实际500 kbit/s，误差0%。
- [x] P1统一使用29位Classic CAN扩展数据帧和正常工作模式；不混用11位标准帧，不加入远程帧或CAN FD。

### 3. 完成P1测试帧定义

- [x] 在`docs/protocol.md`中定义Windows→STM32输出控制命令的CAN ID、DLC和全部字节。
- [x] 定义STM32→Windows应用层ACK与ADC周期上报的CAN ID、DLC和全部字节。
- [x] 明确字节序、单在途命令关联规则、状态值、错误码和无效值。
- [x] 定义ACK、超时、有限重试和重复请求处理规则。
- [x] 所有P1字段完成评审后再写入固件，不能在多个模块分别猜测常量。

### 4. 固化P1测试方法

- [x] 定义STM32周期发送、Windows接收的测试步骤。
- [x] 定义Windows发送、STM32接收并应答的测试步骤。
- [x] 定义总线未接、无ACK、波特率不匹配、Queue满、Error Passive和Bus-Off测试。
- [x] 定义Bus-Off有限恢复策略和失败可观察项。
- [x] 明确2小时连续收发指标，以及Task栈、Heap、错误计数和异常复位的判定方法。

## P1 bxCAN实施步骤

准备清单完成后，按单一增量依次实现：

1. [x] 建立`User/bsp/can/`，完成CAN1时钟、PB8/PB9重映射、位时序、过滤器和底层收发接口。
2. [x] 建立`User/app/can/`，由CAN Task周期发送协议合法的固定`0x110001FF`扩展数据帧，让Windows分析仪完成接收和CAN硬件ACK。
3. [x] 增加FIFO0接收中断，通过有界CAN RX Queue把扩展ID、IDE、RTR、DLC和数据字节作为一帧完整快照交给CAN Task。
4. [x] 实现Windows单在途测试请求→STM32校验→应用层应答的最小双向闭环。
5. [x] 由ADC Task通过长度为1的最新值Queue发布样本；CAN Task只做零等待读取和协议编码，不直接调用ADC BSP。
6. [x] 增加CAN错误状态观测以及Bus-off恢复策略。
7. [x] 完成发送超时、Queue满、错误状态和恢复行为的集中实机验收。
8. [x] 完成2小时稳定性测试并更新文档；Git基线由用户按自己的提交节奏建立。

CAN RX Queue承担扩展ID、IDE、RTR、DLC和数据字节，具有真实的数据传递职责；Binary Semaphore只能表达事件发生，不能替代帧Queue。中断只读取必要硬件状态并投递有界数据，协议处理和应答编排留在Task上下文。

## P1验收条件

1. 构建和链接无新增警告，CAN中断强符号、过滤器、位时序和内存增量可核对。
2. Windows分析仪在正常模式下能够稳定接收STM32发送的29位扩展数据帧，显示的扩展ID和Payload与协议一致，STM32发送能够获得CAN硬件ACK。
3. STM32能够接收Windows单在途测试请求，并返回结果和已应用状态均正确的应答。
4. CAN ID、DLC、字节序和每个字节均与`docs/protocol.md`一致。
5. RX Queue满、发送超时和CAN错误计数均有明确可观察结果。
6. 总线断开、无ACK、错误波特率、Error Passive和Bus-Off行为符合已评审规则，恢复次数有限且不会形成忙循环。
7. `task`和`heap`显示Task栈与FreeRTOS Heap保持安全余量，无持续内存下降。
8. 连续双向收发2小时无资源泄漏、异常复位或不可恢复通信中断。

## 后续阶段待办

以下是历史实施顺序与验收边界，开发阶段已按用户决定收尾，不再安排新增实现。未完成的实机项仍保留未勾选。

### P2：W5500、Windows网络服务与MQTT

- [x] 在`firmware/Libraries/`保存带版本和许可证的WIZnet官方ioLibrary Driver v3.2.0快照；当前仅编译`wizchip_conf.c`和`W5500/w5500.c`。
- [x] 建立`User/bsp/w5500/`，完成SPI2、PG9/CS、PG15/RST和PG8/INT输入的板级配置；使用SPI Mode 0、MSB first和软件控制CS，并隔离共享SPI2的VS1053。
- [x] 完成硬件复位和`VERSIONR`读回；连续8次稳定为`0x04`，错误显式返回。
- [x] 确定静态IP：STM32为`192.168.101.50`，PC为`192.168.101.11`，掩码`255.255.255.0`，网关`192.168.101.1`。
- [x] 配置MAC、IP、子网掩码和网关并读回比较；PHY断开/恢复和PC Ping由用户确认通过。
- [x] 只使用一个W5500硬件Socket完成STM32到Windows测试服务的分层TCP连接、双向字节收发、关闭和持续重连。
  - [x] 电脑监听TCP 5000，Socket 0主动连接并在两侧确认ESTABLISHED；拔线后PHY DOWN和强制关闭可观察，ADC继续运行。
  - [x] 当前架构增量：BSP只保留有界Socket原语；永久TCP Client负责Tick期限、端口轮换和唯一重连策略；Echo放入可识别的临时测试模块；对外字节接口冻结为`>0`本次已接受或读出的字节数、`0`暂不可用、`<0`错误；发送接受不等于对端业务执行成功；用户已完成合并和构建验收。
  - [x] 双Session功能增量：逐字节回显ASCII和二进制数据，累计131700字节；超过64 KiB指针回绕、FIN尾部、正常EOF和首次自动重连均已通过实机自动验收。
  - [x] 恢复增量：服务停止后按1、2、4、8、16秒增长并封顶30秒持续探测；PHY恢复后不等待旧退避，立即开始连接清理与重建；已由用户完成实机验收。
  - [x] 同步回归CAN周期帧、Console、NET任务栈、Heap和传输中拔线恢复；确认测试程序结束后设备进入低频重连而非永久停止；已由用户完成实机验收。
  - [x] 验证空连接收到FIN后正常关闭，但不会把该零数据Session误判为稳定会话并清零连续失败历史；已由用户完成实机验收，TCP基础阶段关闭。
  - [x] 更新`firmware/README.md`并建立TCP里程碑Git提交`13f1b6b`及标签`freertos-w5500-tcp-v0.1`；提交已推送至`origin/main`。
- [ ] 核对并记录现有EMQX的版本、Windows侧运行方式、监听地址、1883端口和防火墙规则。
- [ ] 规划仅用于实验环境的MQTT账号；真实凭据不得写入仓库。
- [ ] 定义EMQX独立服务的启动、停止、日志和连通性检查步骤。
- [x] 冻结MQTT传输参数：Broker为`192.168.101.11:1883`，Client ID为`stm32f103-0253544d3201`，Keep Alive为30秒，Clean Session为`true`，当前不使用LWT；所有发布和订阅使用QoS 0，所有业务消息Retain为`false`。这些参数不进入业务Topic或JSON；业务层严格使用`Client2Server/{method}`和`Server2Client/{method}`。心跳固定10秒上报，不要求ACK。
- [x] 独立MQTT验证工具已由用户完成试用；按用户要求不再重复该工具验证，后续直接进入STM32端接入。
- [x] 冻结W5500多协议任务架构：`AppNetworkTask`只监督芯片，MQTT/FTP各有Task并独占Socket，W5500用唯一Mutex串行访问，P3由Storage Task独占W25Q64。
- [x] 完成W5500 Mutex访问门面、独立MQTT Task和Socket 0所有权迁移；`AppNetworkTask`只保留芯片监督职责。
- [x] 引入官方coreMQTT v2.3.1和W5500 Transport，完成CONNECT、Keep Alive、断线重连、SUBACK与有界入站/出站Queue。
- [x] 完成MQTT对象与网络状态传递简化，从运行和构建路径移除阶段Echo。
- [x] 建立独立业务Task、三条Queue桥接和FreeRTOS运行秒数，10秒`Client2Server/heartbeat`已通过实机验收。
- [x] 用户决定取消`Server2Client/heartbeat` ACK解析和180秒业务离线判定，相关一次性方案已删除。
- [x] 按`docs/MQTT_TEMPERATURE_CONTROL_STEP.md`复用ADC最新快照实现`Client2Server/temperature`，并通过唯一输出控制服务实现`Server2Client/control`、红色LED1/蜂鸣器控制及ACK；构建、烧录和真实硬件验收已通过。

### P3：W25Q64与FTP下载

- [x] 已冻结W25Q64分区、最大448 KiB的`application.bin`、单1 MiB候选固件槽、A/B元数据和提交标记最后写入的原子规则；文件前512字节为应用工程生成且不含MD5的描述头。
- [x] 已建立SPI1基础驱动和正式`AppStorageTask`；启动时只识别器件，`JEDEC=EF 40 17`已经通过实机验收，没有增加专用自检或启动擦写路径。
- [x] 已完成W25Q64擦除、跨页写入、读取、边界保护和掉电后元数据恢复代码的静态检查。`AppStorageTask`是W25Q64唯一所有者，FTP Task只通过有界Queue移交固定大小数据块。
- [x] 已接通TCP按需生命周期、FTP专属网络状态Queue、控制/数据连接、Storage背压以及MQTT下载命令和5%进度上报；全量构建、烧录和启动回归通过，FTP主流程已看到成功及100%结果。
- [x] 已使用唯一的`build/`生成带512字节描述头、向量表位于`0x08010200`的`application.bin`并用于FTP下载；P3执行体是板上此前已烧录的`0x08000000`旧固件，新镜像在Bootloader完成前不能单独硬复位运行。
- 新增独立`AppFtpTask`，固定拥有FTP控制连接和数据连接对应的Socket；被动模式只实现`EPSV/229`，不增加`PASV/227`回退，分块接收并流式提交给Storage Task。
- “下载”的数据路径固定为`Windows FTP Server → STM32 FTP Client → AppStorageTask → W25Q64暂存区`，验收终点是镜像通过长度和MD5校验并进入`READY_TO_UPGRADE`，不得擦除或写入MCU内部Flash。
- 不开发FTP Server程序；使用FileZilla Server免费标准版Windows 64位最新稳定版，安装后记录准确版本；固定虚拟目录`/firmware`、控制监听`192.168.101.11:21`、被动端口`50000-50009`及对应防火墙规则。
- 使用MQTTX发布已冻结的`filedownloadcommand`并观察ACK、真实进度和最终结果；验证工具不得改变产品协议或进入正式固件，不再开发WPF替代它。
- 错误MD5、断网、超时、空间不足、越界写入和设备重启后的恢复尚未逐项验收；用户决定当前跳过错误测试，不作为正常流程验收的前置步骤。正式升级仍由程序检查候选`READY_TO_UPGRADE`状态及完整性。

### P4：Bootloader可靠升级

- [ ] 完成独立裸机Bootloader的普通跳转和正常安装实机验收。Boot源码和完整构建先前已按用户授权完成；用户已烧录两份ELF并读回校验成功，普通启动和正式升级仍待实机运行结果，P4不能据此标记完成。应用首次启动确认代码已由用户录入并通过逻辑静态检查，2026-10-04随升级增量完成Debug构建，实机确认随正常升级验收。故障测试按用户最新要求跳过，不等同于故障路径已实测通过。原Boot实施文档当前已不在工作区，本次不重建，现有接口和格式继续遵守`protocol.md`。
- 底层代码、Controller、GNU 启动文件和 Boot 本地 MD5 子集已齐备，`_init()`及 Debug/Release 链接通过。安装后持久登记首次启动并直接跳转的代码已接入，不增加状态源；后续复位仍校验内部应用并登记尝试。用户已完成应用中断交接改码、构建和镜像静态检查；构建中的系统调用占位警告尚未清理。当前直接首烧并验收普通跳转，随后接入应用升级命令与确认；不重复命令检查或备份步骤。跳转使用`boot_jump.c/.h`，厂商复位启动文件仍使用汇编。
- Boot全部源码、头文件、第三方库、工具链文件、CMake和链接脚本均放在根目录`bootloader/`；`firmware/`保持独立Application工程，不向Boot提供共享文件。两者不得相互包含、编译或链接，分别输出到`bootloader/build/`和`firmware/build/`；只按`protocol.md`规定的固件描述头、元数据字节格式和地址约定交接。
- [x] 已确认首版不做软件版本防降级；安装尝试与新应用首次启动未确认的尝试次数各最多3次。
- 使用已经前置冻结的64 KiB Bootloader区和448 KiB单应用区，建立不使用FreeRTOS的裸机Bootloader、升级标志、安装重试和首次启动确认；升级只消费P3写入W25Q64且已经校验通过的候选镜像。
- Bootloader按块读取W25Q64，擦除、写入并读回校验STM32内部Flash应用区；该阶段不连接FTP Server，也不重新下载文件。
- 安装中断时，Bootloader根据A/B元数据重新校验并从头安装同一候选包；不保存旧固件副本，不实现自动回滚。
- 新应用逻辑故障或超过有限安装/启动尝试后，Bootloader停留安全故障状态，由ST-Link人工恢复；不宣称自动回滚能力。
- 按已经冻结的“硬件型号、软件型号、完整文件长度”描述头实现硬件型号准入；首版不做软件版本防降级，安装前复验完整文件MD5，随后实现首次启动确认、升级失败和断电恢复。
- 使用MQTTX发布`fileupgradecommand`；应用只负责确认请求、持久化升级意图并重启，Bootloader完成真正的升级。
- 当前不安排损坏镜像、升级中断电、内部Flash写入失败或新应用启动失败测试；保留对应产品保护代码，但不宣称这些故障路径已实机验收或绝不会变砖。

### 最后一步：正常升级验收与项目简历

原P5 WPF阶段已取消。2026-10-04按用户决定开发结项：

1. [x] Application首次确认、MQTT升级受理、发送回执、取消和跨重启上报代码已接入。
2. [x] Boot/Application静态审查完成；两个启动残留失败返回块及FTP终态进度已修正，Application Debug构建通过。
3. [ ] 完整升级、新应用`CONFIRMED`、成功上报及再次复位的实机验收：当前未提供结果，随开发结项归档，不伪造通过。
4. [x] 定稿[项目简历](项目简历.md)及[结项记录](PROJECT_CLOSEOUT.md)，区分实现成果与实测结果。
5. [ ] 用户已保留旧历史、初始化项目根仓库`main`并关联新远端；首次提交、代码基线标签及推送仍待完成。Codex未执行Git写操作。

## 后续里程碑

| 阶段 | 范围 | 核心验收 |
| --- | --- | --- |
| P1 | STM32 + FreeRTOS + Windows CAN | 双向收发与错误恢复正常，连续运行2小时无资源泄漏或异常复位 |
| P2 | W5500 + TCP + Windows侧EMQX + MQTT业务 | 能连接EMQX Broker、自动Keep Alive/重连，周期上报心跳和温度/电压，控制红色LED1/蜂鸣器并返回ACK |
| P3 | W25Q64 + STM32 FTP Client + Windows FTP Server + MD5 | 正常下载、长度/描述头/完整文件MD5校验及100%成功结果已验收；不写MCU内部Flash，故障测试跳过 |
| P4 | W25Q64已校验镜像 + 裸机Bootloader + STM32内部Flash | 正常安装及内部Flash读回校验、新应用首次确认、MQTT成功结果和再次复位后正常运行；故障保护保留，但故障测试跳过 |
| 收尾 | 开发结项、项目简历与Git归档 | 按用户新决定结束代码开发；缺少的P4实机结果如实归档，由用户手动提交新根仓库 |

## 当前需要用户继续确认

代码开发与文档收尾已完成，不再安排新功能或强制硬件测试。用户已选择完整归档应用、Boot和文档，并已提供[新项目远端](https://github.com/MilkPuff0214/stm32-freertos-can-mqtt-ota)。旧历史已保留为`firmware/.git-legacy-backup`，根仓库`main`及新远端关联已完成。下一次按[结项记录的Git步骤](PROJECT_CLOSEOUT.md#git完整归档步骤)由用户暂存、首次提交和推送；新远端是否为空尚未核实，未实际推送前不记为Git归档成功。
