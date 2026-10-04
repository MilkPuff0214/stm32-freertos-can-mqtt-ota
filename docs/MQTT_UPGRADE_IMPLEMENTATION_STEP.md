# MQTT 升级命令与结果上报实施步骤

把已验收的 FTP 下载与现有独立 Bootloader 接通，完成正常升级闭环。本文保留已批准的逐文件代码方案供对照；所有代码均为长期产品代码，不新增测试模块。2026-10-04用户明确授权Codex直接修改本文涉及的Application源码并构建：最终审查修正Startup残留失败返回和FTP终态进度后，Application Debug重新构建通过。用户决定此时开发结项，当前入口为[结项与Git归档](PROJECT_CLOSEOUT.md)，第7节仅保留未执行的实机步骤。未修改Boot、烧录或执行Git写操作。

## 目录

- [1. 范围与冻结契约](#1-范围与冻结契约)
- [2. Storage 受理与共享状态快照](#2-storage-受理与共享状态快照)
  - [Storage 头文件](#storage-头文件)
  - [Storage 快照发布](#storage-快照发布)
  - [Storage 持久错误恢复](#storage-持久错误恢复)
  - [Storage 元数据完成分支](#storage-元数据完成分支)
  - [Storage 失败锁定](#storage-失败锁定)
  - [Storage 升级请求受理](#storage-升级请求受理)
  - [Storage 请求分派](#storage-请求分派)
  - [FTP 非消费读取](#ftp-非消费读取)
- [3. MQTT 发送完成回执](#3-mqtt-发送完成回执)
  - [3.1 范围与接口契约](#31-范围与接口契约)
  - [3.2 app_mqtt_message.h](#32-app_mqtt_messageh)
  - [3.3 app_mqtt_client.c](#33-app_mqtt_clientc)
  - [3.4 app_tcp_client.h与app_tcp_client.c](#34-app_tcp_clienth与app_tcp_clientc)
  - [3.5 app_mqtt_task.h与app_mqtt_task.c](#35-app_mqtt_taskh与app_mqtt_taskc)
- [4. MQTT 业务升级流程](#4-mqtt-业务升级流程)
  - [4.1 创建接口、依赖与私有状态](#41-创建接口依赖与私有状态)
  - [4.2 入站分派与下载互斥](#42-入站分派与下载互斥)
  - [4.3 完整增加业务辅助函数](#43-完整增加业务辅助函数)
  - [4.4 完整替换业务 Task 入口与创建函数](#44-完整替换业务-task-入口与创建函数)
- [5. Startup 接线](#5-startup-接线)
- [6. 手工修改顺序与构建](#6-手工修改顺序与构建)
  - [6.1 本次实施与构建记录](#61-本次实施与构建记录)
- [7. 正常升级实机验收](#7-正常升级实机验收)
  - [7.1 先烧录接通升级能力的基线应用](#71-先烧录接通升级能力的基线应用)
  - [7.2 生成具有不同版本的待升级应用](#72-生成具有不同版本的待升级应用)
  - [7.3 下发开始升级并观察复位](#73-下发开始升级并观察复位)
  - [7.4 确认新版并再次复位](#74-确认新版并再次复位)
- [8. 检查边界与参考依据](#8-检查边界与参考依据)

## 1. 范围与冻结契约

前置条件：FTP 正常下载已通过；Application 描述头固定 512 字节，应用地址 `0x08010000`，向量地址 `0x08010200`；完整文件 MD5 写在 A/B 元数据中。Boot 安装和启动次数均最多 3 次，不保留回滚副本、不做防降级。Application 已录入本地首次启动确认，尚待正常升级实机验收。

本次用户已确认：不新增 Task、不修改 Boot、不更改持久字节格式，不增加第二份 Storage 状态 Queue。仅增加长度 1 的发送回执 Queue，Startup 拥有全部 Queue 生命周期。

| 模块 | 唯一职责与所有权 |
| --- | --- |
| MQTT Task | 独占 MQTT/TCP 会话及 Socket；复制升级入站消息，执行 PUBLISH，确认最后 SENDOK，生产发送回执 |
| MQTT 业务 Task | 校验 JSON、关闭实验输出、提交 Storage 请求、处理取消、串行等待回执及决定复位；不访问 W25Q64/BSP |
| Storage Task | 独占外部 Flash 与 A/B 提交；用原执行状态机持久受理/取消；覆盖发布长度 1 状态快照 |
| FTP Task | 保持下载职责；改为 Peek 同一 Storage 快照，不消费它，不计算升级状态 |
| Bootloader | 保持独立工程；复位后读取已冻结字节格式，重新校验候选、安装、校验内部 Flash 并放行新应用 |

正常顺序：`READY_TO_UPGRADE → Storage 完整提交 UPGRADE_REQUESTED → ACK true 的 SENT → upgrade=3/rate=0 的 SENT → 业务关闭输出并复位 → Boot 安装 → BOOT_PENDING → Application 本地确认 CONFIRMED → 新 MQTT READY 会话上报 upgrade=1/rate=100`。

`SENT`只证明原连接上的 W5500 最后一段 SEND 已完成，不是 QoS 0 的 Broker PUBACK，更不是安装成功。发送失败/队列满/会话变化不自动复位，已提交的请求保留；可重新下发开始命令，或在应用尚未放行复位时取消。**保留请求时手工复位仍会触发 Boot 安装**，不能把“网络发送失败”解释为撤销升级。

取消收到后先等当前 Storage 提交或 tracked PUBLISH 结束，再提交 `READY_TO_UPGRADE`；最终读回成功才 ACK true。此能力仅覆盖应用运行期，进入 Boot 安装后没有 MQTT 取消通道。重复开始/取消按同一候选代次幂等处理。业务同一时刻仅一笔 Storage 或 tracked PUBLISH 在途，超时后只收尾，不放行复位，也不发第二笔 tracked 请求覆盖旧回执。

## 2. Storage 受理与共享状态快照

### Storage 范围与接口契约

本节源码目标只有以下三份；本次已由用户明确授权Codex录入，保留接口供对照：

- `firmware/User/app/storage/app_storage_task.h`：请求类型、请求内容、共享快照。
- `firmware/User/app/storage/app_storage_task.c`：升级请求校验、已有 A/B 状态机复用、完成关联。
- `firmware/User/app/ftp/app_ftp_task.c`：共享状态 Queue 的读取改为单次 Peek。

Storage 继续独占 W25Q64、SPI1、A/B 元数据和现有串行 Flash 执行状态；
不新增 Task、Queue、通知、持久格式、提交算法、第二套升级执行状态或确认标志。
既有 `prvStartMetadataCommit()` 已由用户录入：新下载才建立新记录，其他推进复制有效记录，
因此开始、取消和首次确认均保留长度、MD5、安装次数及启动次数，只推进代次、状态和错误。

依赖保持为 `业务 Task / FTP Task → 有界 Storage 请求 Queue → Storage Task → W25Q64 BSP`。
读取方向为 `Storage Task → 长度 1 最新快照 Queue → 业务 Task / FTP Task 的单次零等待 Peek`。
业务 Task 不直接包含 BSP、不写元数据，MQTT 回调只复制有界报文，不执行 Flash 操作。

本地参考为 FreeRTOS Kernel V11.3.0 的 `include/queue.h` 与 `queue.c`：
`xQueueOverwrite()` 仅用于长度 1 Queue，按值复制；`xQueuePeek(..., 0U)` 不移除记录，
并在内部临界区复制完整快照。STM32F103 的 Task 上下文适用这些语义；不修改官方内核。
现有长度 2 请求 Queue、512 字节内联写块和 Storage 单执行状态保持不变。

升级请求 `xData.xUpgrade` 中两个字段各有一个职责：

| 字段 | 唯一职责 |
| --- | --- |
| `ulRequestId` | 运行期关联这一次 Storage 控制请求与其最终结果；0 无效，由业务 Task 的非零编号生成器生成 |
| `ulExpectedGeneration` | 核对业务观察到的候选仍是同一份已提交元数据；32 位回绕后仍按等值判断，不要求非零 |

`ulTransactionId` 继续只标识 FTP 下载事务，升级请求填写 0，不改动快照里的下载事务号。
固定业务 `messageId="8"`、旧 FTP 事务号和 MQTT 会话号均不能替代 `ulRequestId`。
编号只在本次运行中关联，不写入 Flash；业务 Task 同一时刻只能有一个 Storage 升级控制请求在途，
未等到该请求完成前不得提交下一个编号。若开始提交期间收到取消，业务 Task 先等待开始完成，
随后以新快照中的代次提交取消，不在 Storage 保存第二个“待取消”执行状态。

完成快照的 `ulCompletedUpgradeRequestId` 与 `xUpgradeRequestError` 保留最近一次控制请求结果。
FTP 的 PREPARE/WRITE/FINISH/ABORT、普通状态 QUERY 和擦写进度发布不清除这两项。
业务只有在完成编号与本次请求相等时才使用该结果；旧完成编号不能当作本次完成。
`APP_STORAGE_ERROR_NONE` 是成功，其余是本次 Storage 请求失败原因，不增加 MQTT Body 字段。

开始与取消均要求有效元数据、内部操作空闲、期望代次相等以及运行态/持久态配对正确：

| 请求 | 当前配对 | 动作 |
| --- | --- | --- |
| START | RAM `READY_TO_UPGRADE` + 持久 `READY_TO_UPGRADE` | 先锁定，完整提交 `UPGRADE_REQUESTED`，最终读回成功才回执成功 |
| START | RAM `UPGRADE_LOCKED` + 持久 `UPGRADE_REQUESTED` | 同代次幂等成功；不增加代次或重复擦写 |
| CANCEL | RAM `UPGRADE_LOCKED` + 持久 `UPGRADE_REQUESTED` | 保持锁定，完整提交 `READY_TO_UPGRADE`，最终读回成功才解锁并回执成功 |
| CANCEL | RAM `READY_TO_UPGRADE` + 持久 `READY_TO_UPGRADE` | 同代次幂等成功；不重写元数据 |

`INSTALLING`、`BOOT_PENDING`、`CONFIRMED`、失败态、器件故障及忙态不接受 START/CANCEL。
取消截止点由业务 Task 的唯一执行状态限定为“尚未放行复位”；进入 Boot 后应用不再处理 MQTT，
不能把这里的接口描述成安装中的网络取消能力。

快照新增的持久字段仅由已选中有效记录派生，不是第二份内部权威元数据。
`xMetadataValid=true` 只说明 Storage RAM 中的已选中记录有效，不保证一次失败提交没有在 Flash
中形成更新记录。任何元数据提交错误进入 `DEVICE_FAULT`，当前运行不得自动复位或受理下载、
开始、取消；复位后重新选择实际有效的 A/B。旧 RAM `READY_TO_UPGRADE` 不能解除此锁定。
已完整提交但后续 MQTT 发送失败的 `UPGRADE_REQUESTED` 保留，是否重新发送或取消由业务处理；
手工复位仍会触发 Boot 安装。

成功补报只能依据当前有效持久 `CONFIRMED`，并检查其安装和启动次数均大于 0。
当前下载已使旧 `CONFIRMED` 失效或替换后，不保留另一份“上次升级成功”RAM/Flash 记录补报。
下载已进入 RAM `PREPARING`、失效提交尚未完成时，也不能将残留旧 `CONFIRMED` 作为当前成功：
成功补报还须要求 RAM `IDLE`、无 Storage 故障。有效 P4 `FAILED` 可按 `ulPersistentError`
区分原始 Boot 错误；不能把 P3 下载失败当作安装失败。首次确认仍由已录入的本地条件触发。

### Storage 头文件

目标：`firmware/User/app/storage/app_storage_task.h`。

1. 在已有 `#include "app_storage_layout.h"` 后加入：

```c
#include "app_storage_metadata.h"
```

这是 Application 本地的持久状态类型依赖，Boot 工程不包含此头，也不改 Boot 的独立定义。

2. 在 `AppStorageRequestType_t` 尾部，将原来的最后一项改为以下三项；前四项不动：

```c
    APP_STORAGE_REQUEST_ABORT_DOWNLOAD,    /* 中止当前下载。 */
    APP_STORAGE_REQUEST_START_UPGRADE,     /* 持久受理升级，携带xUpgrade。 */
    APP_STORAGE_REQUEST_CANCEL_UPGRADE     /* 取消未放行复位的升级，携带xUpgrade。 */
```

3. 在 `AppStoragePrepareRequest_t` 定义之后、`AppStorageWriteRequest_t` 定义之前加入：

```c
typedef struct
{
    /* 运行期非零请求编号；不使用固定messageId或FTP事务号。 */
    uint32_t ulRequestId;
    /* 业务Peek观察到的已提交代次；0也可能是合法回绕结果。 */
    uint32_t ulExpectedGeneration;
} AppStorageUpgradeRequest_t;
```

4. 在 `AppStorageRequest_t.xData` 中，已有 `xWrite` 后加入：

```c
        AppStorageUpgradeRequest_t xUpgrade;
```

`xWrite` 仍是 union 最大成员，增加两个 `uint32_t` 不放大现有请求 Queue 的单元素上限；
既有 `sizeof(AppStorageRequest_t) <= 528U` 静态断言保留。

5. 在 `AppStorageStatus_t` 的 `ucCapacity` 后、结构体结束前追加：

```c
    /* 仅由Storage已选中的有效记录派生；读者不能据此忽略DEVICE_FAULT。 */
    bool xMetadataValid;
    AppStoragePersistentState_t xPersistentState;
    uint32_t ulMetadataGeneration;
    uint32_t ulPersistentError;    /* 保留原始Boot/P3数值，不截断为P3错误枚举。 */
    uint32_t ulInstallAttempts;
    uint32_t ulBootAttempts;

    /* 最近已完成的升级控制请求；普通FTP/QUERY发布不覆盖。 */
    uint32_t ulCompletedUpgradeRequestId;
    AppStorageError_t xUpgradeRequestError;
```

保持 `AppStorageTask_Create()` 签名不变。现有 `User/app/startup/app_startup.c`
用 `sizeof(AppStorageStatus_t)` 创建 Queue，
因此会自动采用新元素大小；业务的 Queue 句柄接入由主文档另节给出。
STM32 当前 32 位 ABI 下预计快照从 28 字节变为 56 字节；准确大小和栈余量由用户构建及实机观察。
没有复制 MD5 或文件内容给业务；运行期长度仍保持已有字段，持久字段仅含业务判断所需的标量。

同一头文件的 `APP_STORAGE_ERROR_FIRMWARE_HEADER` 旧注释“前256字节”改成“前512字节描述头非法”；只更正注释，不改变该枚举值。

### Storage 快照发布

目标：`firmware/User/app/storage/app_storage_task.c`。
整段替换现有 `prvPublishStatus()`：

```c
static void prvPublishStatus(void)
{
    BaseType_t xResult;

    /* Flash有效记录仍由s_xActiveMetadata独占；这里生成跨Task的按值快照。 */
    s_xStorageStatus.xMetadataValid = (s_ulActiveMetadataAddress != UINT32_MAX);
    if (s_xStorageStatus.xMetadataValid)
    {
        s_xStorageStatus.xPersistentState = s_xActiveMetadata.xState;
        s_xStorageStatus.ulMetadataGeneration = s_xActiveMetadata.ulGeneration;
        s_xStorageStatus.ulPersistentError = s_xActiveMetadata.ulLastError;
        s_xStorageStatus.ulInstallAttempts = s_xActiveMetadata.ulInstallAttempts;
        s_xStorageStatus.ulBootAttempts = s_xActiveMetadata.ulBootAttempts;
    }
    else
    {
        s_xStorageStatus.xPersistentState = APP_STORAGE_PERSISTENT_STATE_EMPTY;
        s_xStorageStatus.ulMetadataGeneration = 0U;
        s_xStorageStatus.ulPersistentError = 0U;
        s_xStorageStatus.ulInstallAttempts = 0U;
        s_xStorageStatus.ulBootAttempts = 0U;
    }

    /* 不清除最近升级控制完成编号；多个只读者各自Peek同一份最新快照。 */
    xResult = xQueueOverwrite(s_xStatusQueue, &s_xStorageStatus);
    configASSERT(xResult == pdPASS);
    (void)xResult;
}
```

已有 `prvStartMetadataCommit()` 与 `prvServiceMetadataCommit()` 不改，保留主体读回、最后写 CMIT、
整条记录读回比较和解码后才更新 `s_xActiveMetadata` 的语义。
新增 `ulPersistentError` 保留原始记录错误，Boot 错误恢复必须按下一节映射，
避免把有效的候选拒绝终态当作元数据字节损坏。

### Storage 持久错误恢复

已只读核对 `bootloader/User/app/boot/boot_controller.c` 中实际常量和放行条件：

| 原始数值 | Boot 语义 | 应用恢复策略 |
| --- | --- | --- |
| `0x1001` | 首次安装前候选被拒绝；只有尚未安装且旧内部应用校验通过才跳旧应用 | 有效 FAILED 且 `installAttempts=0` 恢复 RAM FAILED，允许正常新下载 |
| `0x1002` | 安装事务中候选丢失 | 禁止应用恢复后覆盖候选 |
| `0x1003` | 元数据提交失败；通常只留 Boot RAM 诊断 | 若有效持久记录出现该值，保守锁定 |
| `0x1004` / `0x1005` / `0x1006` | 内部 Flash 擦除、写入、读回失败 | 保守锁定 |
| `0x1007` | 内部完整镜像 MD5 失败 | 保守锁定 |
| `0x1008` / `0x1009` | 安装、未确认启动尝试耗尽 | 保守锁定 |
| `0x2001` / `0x2002` | Boot 器件/元数据不可用，仅 Boot RAM 诊断 | 不应写入持久记录；若读到这种值仍锁定 |

Boot 的 FAILED 分支对任何 `installAttempts != 0` 都不放行，对未知 P4 错误也不放行。
因此这里仅恢复 Boot 已允许的旧应用路径，不把已发生安装的失败解锁成普通下载失败。
快照 `ulPersistentError` 始终取原值，RAM `xError` 仅使用 Application 的合法错误枚举；
候选拒绝映射为 `STATE_CONFLICT` 表示候选不能升级，不猜测到底是型号、MD5还是描述头问题。
其他持久 P4 故障也使用 RAM `DEVICE_FAULT/STATE_CONFLICT` 表示恢复受到保护，
只有实际 SPI、记录选择或字节解码失败才称为 RAM metadata 故障。

1. 在 `app_storage_task.c` 的现有超时宏之后加入本工程本地常量：

```c
/* 与protocol.md的持久字节契约一致，不包含Boot工程头文件。 */
#define APP_STORAGE_PERSISTENT_ERROR_CANDIDATE_REJECTED    0x1001UL
```

2. 整段替换 `prvLoadMetadata()`：

```c
static bool prvLoadMetadata(void)
{
    AppStorageMetadataSelectResult_t xSelectResult;
    uint32_t ulPersistentError;

    if ((BspW25Q64_Read(APP_STORAGE_METADATA_A_ADDRESS, s_aucMetadataA,
                       APP_STORAGE_METADATA_RECORD_SIZE) == false) ||
        (BspW25Q64_Read(APP_STORAGE_METADATA_B_ADDRESS, s_aucMetadataB,
                       APP_STORAGE_METADATA_RECORD_SIZE) == false))
    {
        return false;
    }

    xSelectResult = AppStorageMetadata_Select(
        s_aucMetadataA, s_aucMetadataB, &s_xActiveMetadata, &s_ulActiveMetadataAddress);
    if (xSelectResult == APP_STORAGE_METADATA_SELECT_ERROR)
    {
        return false;
    }
    if (xSelectResult == APP_STORAGE_METADATA_SELECT_EMPTY)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_IDLE;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
        return true;
    }

    s_xStorageStatus.ulFirmwareLength = s_xActiveMetadata.ulFirmwareLength;
    ulPersistentError = s_xActiveMetadata.ulLastError;
    if (ulPersistentError < (uint32_t)APP_STORAGE_ERROR_COUNT)
    {
        /* 只有确认属于Application的P3错误范围后，才能转成其错误枚举。 */
        s_xStorageStatus.xError = (AppStorageError_t)ulPersistentError;
    }
    else if ((s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_FAILED) &&
             (ulPersistentError == APP_STORAGE_PERSISTENT_ERROR_CANDIDATE_REJECTED) &&
             (s_xActiveMetadata.ulInstallAttempts == 0U))
    {
        /* Boot已验证并放行旧应用；候选失效不等于元数据字节损坏。 */
        s_xStorageStatus.xError = APP_STORAGE_ERROR_STATE_CONFLICT;
    }
    else
    {
        /* 已知严重P4故障或未知错误保留原值，不允许新下载覆盖保护记录。 */
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_STATE_CONFLICT;
        return true;
    }

    if ((ulPersistentError != 0U) &&
        (s_xActiveMetadata.xState != APP_STORAGE_PERSISTENT_STATE_PREPARING) &&
        (s_xActiveMetadata.xState != APP_STORAGE_PERSISTENT_STATE_DOWNLOADING) &&
        (s_xActiveMetadata.xState != APP_STORAGE_PERSISTENT_STATE_VERIFYING) &&
        (s_xActiveMetadata.xState != APP_STORAGE_PERSISTENT_STATE_FAILED))
    {
        /* 成功/请求/待确认状态不应夹带错误，禁止自动首次确认或继续升级。 */
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_STATE_CONFLICT;
        return true;
    }

    switch (s_xActiveMetadata.xState)
    {
        case APP_STORAGE_PERSISTENT_STATE_EMPTY:
        case APP_STORAGE_PERSISTENT_STATE_CONFIRMED:
            s_xStorageStatus.xState = APP_STORAGE_STATE_IDLE;
            break;

        case APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE:
            s_xStorageStatus.xState = APP_STORAGE_STATE_READY_TO_UPGRADE;
            break;

        case APP_STORAGE_PERSISTENT_STATE_PREPARING:
        case APP_STORAGE_PERSISTENT_STATE_DOWNLOADING:
        case APP_STORAGE_PERSISTENT_STATE_VERIFYING:
            s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_INTERRUPTED;
            break;

        case APP_STORAGE_PERSISTENT_STATE_FAILED:
            if (s_xActiveMetadata.ulInstallAttempts != 0U)
            {
                /* 与Boot的FAILED放行条件一致；已开始安装的故障不得解锁。 */
                s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
                s_xStorageStatus.xError = APP_STORAGE_ERROR_STATE_CONFLICT;
            }
            else
            {
                s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;
            }
            break;

        case APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED:
        case APP_STORAGE_PERSISTENT_STATE_INSTALLING:
        case APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING:
            s_xStorageStatus.xState = APP_STORAGE_STATE_UPGRADE_LOCKED;
            break;

        default:
            return false;
    }

    return true;
}
```

`prvInitializeDevice()` 的已有错误范围防护保留；现在加载路径只写入合法 RAM 枚举，
因此不再因原始 `0x1001` 被强转而误入 `APP_STORAGE_ERROR_METADATA`。
保护性 DEVICE_FAULT 返回 true 表示“已成功读取有效记录，但该持久故障禁止覆盖”，
不表示普通空闲；后续快照仍发布有效性、代次和原始 P4 错误。

### Storage 元数据完成分支

在 `prvContinueAfterMetadata()` 中整段替换已有 `READY_TO_UPGRADE` 分支，并在其后插入
`UPGRADE_REQUESTED` 分支；已有 PREPARING、DOWNLOADING、VERIFYING、CONFIRMED、FAILED 分支保留：

```c
        case APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE:
            s_xStorageStatus.xState = APP_STORAGE_STATE_READY_TO_UPGRADE;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            if (s_xCurrentRequest.xType == APP_STORAGE_REQUEST_CANCEL_UPGRADE)
            {
                /* 取消只有完整A/B提交并最终读回后才成功和解锁。 */
                s_xStorageStatus.ulCompletedUpgradeRequestId =
                    s_xCurrentRequest.xData.xUpgrade.ulRequestId;
                s_xStorageStatus.xUpgradeRequestError = APP_STORAGE_ERROR_NONE;
                s_xCurrentRequest.xType = APP_STORAGE_REQUEST_QUERY_STATUS;
                prvPublishStatus();
                prvReportStatus();
            }
            else
            {
                /* 原下载FINISH路径不产生升级控制回执。 */
                prvPublishStatus();
            }
            return true;

        case APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED:
            s_xStorageStatus.xState = APP_STORAGE_STATE_UPGRADE_LOCKED;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            if (s_xCurrentRequest.xType == APP_STORAGE_REQUEST_START_UPGRADE)
            {
                /* 此时持久意图已验证；不在Storage发送MQTT或复位MCU。 */
                s_xStorageStatus.ulCompletedUpgradeRequestId =
                    s_xCurrentRequest.xData.xUpgrade.ulRequestId;
                s_xStorageStatus.xUpgradeRequestError = APP_STORAGE_ERROR_NONE;
                s_xCurrentRequest.xType = APP_STORAGE_REQUEST_QUERY_STATUS;
            }
            prvPublishStatus();
            prvReportStatus();
            return true;
```

清 `xType` 仅结束现有当前请求的生命周期；其他字段不必清零。
它阻止后续下载的 READY 提交被误识别为旧取消请求完成，不新增完成标志。
完成编号与持久快照由同一次 `prvPublishStatus()` 发布，读者不会将开始受理的旧代次当作提交结果。

### Storage 失败锁定

整段替换 `prvEnterTransactionFailure()`，保留既有下载 FAILED 持久化机制并增加升级控制回执：

```c
static void prvEnterTransactionFailure(AppStorageError_t xError)
{
    bool xCanPersistFailure =
        (xError != APP_STORAGE_ERROR_METADATA) &&
        (s_ulActiveMetadataAddress != UINT32_MAX) &&
        ((s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_PREPARING) ||
         (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_DOWNLOADING) ||
         (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_VERIFYING));
    bool xUpgradeRequest =
        (s_xCurrentRequest.xType == APP_STORAGE_REQUEST_START_UPGRADE) ||
        (s_xCurrentRequest.xType == APP_STORAGE_REQUEST_CANCEL_UPGRADE);

    s_xOperation = APP_STORAGE_OPERATION_IDLE;

    /*
     * CMIT可能已写入而最终读回失败，RAM旧记录不能证明Flash还是旧状态。
     * 元数据故障、首次确认故障和升级控制故障均保持下载禁用。
     */
    s_xStorageStatus.xState =
        ((xError == APP_STORAGE_ERROR_METADATA) || xUpgradeRequest ||
         (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING)) ?
        APP_STORAGE_STATE_DEVICE_FAULT : APP_STORAGE_STATE_FAILED;
    s_xStorageStatus.xError = xError;
    s_xAbortRequested = false;
    s_usCurrentBlockWritten = 0U;

    if (xUpgradeRequest)
    {
        s_xStorageStatus.ulCompletedUpgradeRequestId =
            s_xCurrentRequest.xData.xUpgrade.ulRequestId;
        s_xStorageStatus.xUpgradeRequestError = xError;
        s_xCurrentRequest.xType = APP_STORAGE_REQUEST_QUERY_STATUS;
    }

    if (xCanPersistFailure)
    {
        if (prvStartMetadataCommit(APP_STORAGE_PERSISTENT_STATE_FAILED, xError))
        {
            /* 下载FAILED仍只在失败记录完整提交后发布为稳定终态。 */
            return;
        }
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_METADATA;
    }

    prvPublishStatus();
    if (s_xStorageStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT)
    {
        prvReportStatus();
    }
}
```

局部 `xUpgradeRequest` 只用于在清除当前请求类型前判断这一条请求的失败出口，不是全局状态。
元数据故障后不在原运行期重读并猜测提交结果、不自动重试、不将旧 READY 暴露成可覆盖候选。
业务等待超时也不能让已经入队的 Storage 请求失去所有权；应等其完成或保留故障处理状态，
不可因业务计时结束就提交另一条 START/CANCEL。

另外，在 `prvStartPrepare()` 中找到 `prvStartMetadataCommit(PREPARING, NONE)` 返回 false
的分支，只将其中 `s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;` 改为：

```c
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
```

该分支的 `APP_STORAGE_ERROR_METADATA` 和函数末尾发布保留；不要替换同函数 MD5 初始化失败的
FAILED。这样所有元数据提交故障均保持下载禁用，不留下一个错误枚举相同但允许继续覆盖的出口。

### Storage 升级请求受理

在 `prvHandleAbort()` 定义结束后、`prvDispatchRequest()` 定义之前插入以下完整函数：

```c
static void prvHandleUpgradeRequest(const AppStorageRequest_t *pxRequest)
{
    AppStorageError_t xError = APP_STORAGE_ERROR_NONE;
    AppStoragePersistentState_t xTargetState;
    bool xReady;
    bool xRequested;

    if (pxRequest == NULL)
    {
        return;
    }

    xReady = (s_xStorageStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
        (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE);
    xRequested = (s_xStorageStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
        (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED);

    if (pxRequest->xData.xUpgrade.ulRequestId == 0U)
    {
        xError = APP_STORAGE_ERROR_TRANSACTION_MISMATCH;
    }
    else if ((s_xOperation != APP_STORAGE_OPERATION_IDLE) ||
             (s_ulActiveMetadataAddress == UINT32_MAX) ||
             ((xReady == false) && (xRequested == false)))
    {
        xError = APP_STORAGE_ERROR_STATE_CONFLICT;
    }
    else if (pxRequest->xData.xUpgrade.ulExpectedGeneration !=
             s_xActiveMetadata.ulGeneration)
    {
        /* 观察后候选被替换或状态已推进，不升级另一份镜像。 */
        xError = APP_STORAGE_ERROR_TRANSACTION_MISMATCH;
    }

    if (xError == APP_STORAGE_ERROR_NONE)
    {
        xTargetState = (pxRequest->xType == APP_STORAGE_REQUEST_START_UPGRADE) ?
            APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED :
            APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE;

        if (s_xActiveMetadata.xState != xTargetState)
        {
            /* 复用已有请求存储与唯一Flash执行状态，提交期间始终锁定。 */
            s_xCurrentRequest = *pxRequest;
            s_xStorageStatus.xState = APP_STORAGE_STATE_UPGRADE_LOCKED;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            if (prvStartMetadataCommit(xTargetState, APP_STORAGE_ERROR_NONE))
            {
                prvPublishStatus();
            }
            else
            {
                prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
            }
            return;
        }

        /* 相同期望代次且已达到目标状态：幂等成功，不重复擦写。 */
        s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
    }

    /* 拒绝/幂等请求立即完成；不会覆盖正在执行的s_xCurrentRequest。 */
    s_xStorageStatus.ulCompletedUpgradeRequestId = pxRequest->xData.xUpgrade.ulRequestId;
    s_xStorageStatus.xUpgradeRequestError = xError;
    prvPublishStatus();
}
```

`xReady`、`xRequested` 是一次校验的局部推导，不缓存候选准备或升级受理状态。
该函数只由下节两个请求分支调用，`xTargetState` 不用于另建“下一阶段”状态。
普通状态冲突只完成本次控制请求失败，不用下载事务失败路径改变现有记录或当前下载状态。
业务 Task 是唯一控制请求生产者，遵守一个请求在途，因此拒绝回执不会与另一条控制完成互抢。
FTP 即便正在提交自己的请求，也不能覆盖升级回执字段。

### Storage 请求分派

在 `prvDispatchRequest()` 的 `APP_STORAGE_REQUEST_ABORT_DOWNLOAD` 分支之后、`default` 之前加入：

```c
        case APP_STORAGE_REQUEST_START_UPGRADE:
        case APP_STORAGE_REQUEST_CANCEL_UPGRADE:
            prvHandleUpgradeRequest(pxRequest);
            break;
```

其余分支和 `prvServiceCurrentOperation()` 保留。START/CANCEL 只复用元数据操作，
无需修改候选擦除、分页写入、下载终验、Metadata CMIT 流程或首次 ADC 确认 Task 循环。
`prvHandleAbort()` 不承载升级取消，旧 FTP ABORT 不得修改已锁定的升级请求。

### FTP 非消费读取

目标：`firmware/User/app/ftp/app_ftp_task.c`。
只在 `prvRefreshStorageStatus()` 内，把这条条件：

```c
    if (xQueueReceive(s_xStorageStatusQueue, &xStatus, 0U) != pdPASS)
```

替换为：

```c
    /* Storage状态是共享最新快照，不能消费掉业务Task也需要读取的元素。 */
    if (xQueuePeek(s_xStorageStatusQueue, &xStatus, 0U) != pdPASS)
```

同一函数中的下载百分比更新条件还需增加完成态保护，避免重复Peek把100%重新封顶为95%：

```c
    /* 成功终态的100%由完成流程给出，重复快照不能再按传输进度封顶95%。 */
    if ((s_xPublicStatus.xState != APP_FTP_STATE_COMPLETED) &&
        (xStatus.ulTransactionId == s_xPublicStatus.ulTransactionId) &&
        (xStatus.ulWrittenLength <= s_xPublicStatus.ulFirmwareLength))
```

保留该条件原有主体。没有循环排空；每周期至多读一次，保留 `prvStorageStatusChanged()`
只比较下载状态、错误、事务号与进度的既有行为，不把新增升级完成字段算作下载进度。
否则无关升级请求可能重置 FTP 下载进度超时。
网络状态 Queue、FTP 命令 Queue 仍使用原先的 Receive，不机械替换其他 Queue。
业务读取这份 Storage Queue 也必须使用单次零等待 Peek；禁止任何消费者将其改为空。

### Storage 手工顺序与验收

与主文档的 MQTT/业务/`app_startup.c` 变更在同一次手工录入中完成：

1. 录入 Storage 头文件，保留原有枚举值、请求容量及 Create 签名。
2. 替换快照发布和持久恢复函数、元数据完成分支及失败函数。
3. 插入升级受理函数并接入分派。
4. 将 FTP 的共享状态读取改为单次 Peek。
5. 录入主文档的业务单在途请求、最新状态读取与 Queue 句柄接入；之后由用户构建。

在 `firmware/` PowerShell 中由用户执行既有构建命令：

```powershell
cmake --build build --parallel
```

本稿不要求修改 CMake、Boot、链接脚本或第三方库，也不新增测试模块。
应核对新请求/快照类型编译通过；任何消费 `AppStorageStatus_t` 的位置使用新 `sizeof`，
所有 Storage 快照读者为单次 Peek，Storage 仍为唯一写入者。首次确认原录入逻辑继续保留。

后续正常升级的实机判定：FTP 正常下载及完整长度/描述头/MD5校验成功后进入 READY；
START 的完整提交之后日志显示 RAM `state=7`、持久 `persistent=5`，业务获得同编号成功完成；
MQTT 受理 ACK/升级中上报发送完成才由业务复位；Boot 正常安装、新应用本地确认后
显示持久 `persistent=8`，MQTT 报 `upgrade=1, rate=100`；再次按复位键仍正常运行。
成功补报同时要求有效元数据、RAM `IDLE`、持久 `CONFIRMED`、`installAttempts>0`
及 `bootAttempts>0`；新下载替换记录后或 RAM `PREPARING` 期间不报告旧升级成功。

取消是长期产品代码的必要失败出口；本次用户只要求正常升级验收，不追加人为故障矩阵。
取消、提交未知结果、断电与错误注入的代码可做静态核对，但未经板卡操作不得写为实测通过。
尚未测量新快照/业务接入后的栈高水位和 Heap，不仅根据计划容量声明资源验收通过。

## 3. MQTT 发送完成回执

### 3.1 范围与接口契约

以下修改均为长期产品代码，没有阶段测试代码，不新增Task、TCP执行状态、BSP原语或MQTT协议状态机。文件目标为：

| 目标文件 | 修改职责 |
| --- | --- |
| `firmware/User/app/mqtt/app_mqtt_message.h` | 升级入站枚举、出站本机请求编号和发送完成回执契约 |
| `firmware/User/app/mqtt/app_mqtt_client.c` | 新增升级命令订阅并将Payload复制到既有入站Queue |
| `firmware/User/app/network/app_tcp_client.h` | 声明同所有者上下文的发送排空查询 |
| `firmware/User/app/network/app_tcp_client.c` | 从现有连接状态和标志推导查询结果，不操作硬件 |
| `firmware/User/app/mqtt/app_mqtt_task.h` | 创建接口接收Startup拥有的回执Queue |
| `firmware/User/app/mqtt/app_mqtt_task.c` | 串行处理出站发布、等待末段SENDOK并投递回执 |

Startup负责创建长度为1、元素类型为`AppMqttPublishReceipt_t`的Queue，并把同一Handle交给MQTT Task和业务Task。MQTT Task是唯一生产者，业务Task是唯一消费者。Startup及业务Task的代码在主实施文档中给出，不在本节另立实现。

`ulRequestId`是本机Queue关联号，不是JSON中的固定`messageId`，不加入Topic或Payload：

- `ulRequestId=0`：沿用普通心跳、温度、控制ACK和下载消息的既有发送行为，不产生回执。
- `ulRequestId!=0`：要求一条发送回执；业务Task同一时刻最多等待一笔请求，编号不能与尚未结束或迟到的旧请求复用。
- 回执必须同时匹配业务Task当前等待的`ulRequestId`和原`ulMqttSession`；不匹配的迟到回执丢弃。
- `SENT`表示`AppMqttClient_Publish()`已成功，并且原MQTT/TCP会话仍可用、最后一段W5500 SEND已由既有TCP代码确认SENDOK并清除`xTxInFlight`。QoS 0没有PUBACK，该回执不保证MQTTX已经显示，也不表示Boot已安装成功。
- 参数错误、协议尚未初始化、PHY/芯片不可用、会话变化、发布失败或排空超过5000 ms，均返回`FAILED`；协议层不复位MCU、不撤销已提交的升级请求。
- 5000 ms从`Publish()`返回成功之后计时；本地coreMQTT发送API还有约1000 ms的同步发送上限。业务层每条等待采用15000 ms，并处理队列等待和迟到结果；排空等待期间MQTT Task继续Service，任何等待均不持有W5500 Mutex。
- 等待当前回执时不取下一条outbound。取消时业务Task先结束或收取旧回执，取消ACK不能抢占另一笔需回执消息。具体取消截止点和持久提交由业务/Storage章节统一定义。

唯一待回执状态直接从`s_xOutboundMessage.ulRequestId != 0U`推导。仅新增原TCP会话编号和排空起始Tick两项局部模块存储，分别用于排除新连接冒充旧发送、限制等待时间；不新增`pending`、`sent`或“待重启”布尔变量。

每轮调用顺序为`TCP Service → 出站处理/排空回执 → MQTT Service → 最新业务状态发布`。排空先于MQTT Service，可在旧SENDOK刚被确认时识别边界，避免Keep Alive的PINGREQ立即发起另一笔SEND而遮盖该时刻。新发布成功的本轮只保留请求编号，最早下一轮才能成功回执。MQTT Service仍持续处理Keep Alive和入站命令。

依据已核对的本地官方coreMQTT v2.3.1，`Transport.writev=NULL`时PUBLISH按固定头、Topic和Payload分段调用Transport；最后一段被接受时API即可返回。现有BSP在每次SEND前清除旧SENDOK，PollSend同时检查TIMEOUT和Socket状态，成功清除新SENDOK才返回COMPLETE；现有TCP是`xTxInFlight`的唯一写入者。该方案适用STM32F103ZET6、FreeRTOS任务独占Socket 0、W5500非阻塞Transport和QoS 0，不直接照搬官方阻塞轮询，不修改第三方源码。参考：[coreMQTT v2.3.1固定版本源码](https://github.com/FreeRTOS/coreMQTT/blob/v2.3.1/source/core_mqtt.c)、[W5500 Datasheet v1.0.9](https://docs.wiznet.io/img/products/w5500/w5500_ds_v109e.pdf)。

### 3.2 app_mqtt_message.h

在`APP_MQTT_BUSINESS_STATUS_QUEUE_LENGTH`之后加入：

```c
/* 仅一笔本机需回执请求在途；普通出站消息不写此Queue。 */
#define APP_MQTT_PUBLISH_RECEIPT_QUEUE_LENGTH      1U
```

将现有`AppMqttInboundMethod_t`完整替换为：

```c
typedef enum
{
    APP_MQTT_INBOUND_METHOD_CONTROL = 0,
    APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND,
    APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND
} AppMqttInboundMethod_t;
```

在`AppMqttOutboundMessage_t`最后一个字段`aucPayload`之后、右大括号之前追加：

```c
    /* 本机关联号；0沿用普通消息，不产生发送完成回执。 */
    uint32_t ulRequestId;
```

在`AppMqttOutboundMessage_t`定义之后、文件最后的`#endif`之前加入：

```c
typedef enum
{
    APP_MQTT_PUBLISH_RESULT_SENT = 0,
    APP_MQTT_PUBLISH_RESULT_FAILED
} AppMqttPublishResult_t;

/* MQTT Task生产，业务Task消费；不携带协议上下文或临时缓冲区指针。 */
typedef struct
{
    uint32_t ulRequestId;
    uint32_t ulMqttSession;
    AppMqttPublishResult_t eResult;
} AppMqttPublishReceipt_t;
```

现有普通消息编码函数都先`memset()`清零整个出站结构，因此其新字段自然为0。升级章节的需回执消息则在编码后明确填非零编号。

### 3.3 app_mqtt_client.c

#### 3.3.1 增加Topic并替换订阅数组

在`s_cFileDownloadCommandTopic`定义之后加入：

```c
static const char s_cFileUpgradeCommandTopic[] = "Server2Client/fileupgradecommand";
```

把现有`s_xSubscriptions`完整替换为：

```c
static const MQTTSubscribeInfo_t s_xSubscriptions[] =
{
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cControlTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cControlTopic) - 1U)
    },
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cFileDownloadCommandTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cFileDownloadCommandTopic) - 1U)
    },
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cFileUpgradeCommandTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cFileUpgradeCommandTopic) - 1U)
    }
};
```

本地`MQTT_SUB_UNSUB_MAX_VECTORS=4U`仍足够；原SUBACK代码依据数组元素数检查状态码，无须新增订阅计数或改写SUBACK处理。

#### 3.3.2 增加入站Topic映射

在`prvQueueInboundPublish()`中、现有下载Topic的`else if`分支之后、最终拒绝未知Topic的`else`之前，插入：

```c
    else if ((pxPublishInfo->topicNameLength ==
              (sizeof(s_cFileUpgradeCommandTopic) - 1U)) &&
             (memcmp(pxPublishInfo->pTopicName, s_cFileUpgradeCommandTopic,
                     sizeof(s_cFileUpgradeCommandTopic) - 1U) == 0))
    {
        xMethod = APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND;
    }
```

既有长度、QoS、Retain及READY检查保留，依旧只复制并零等待投递Queue。升级JSON校验、Storage请求和复位决策归业务Task，不加入回调。下载消息后的凭据清理分支原样保留。

### 3.4 app_tcp_client.h与app_tcp_client.c

#### 3.4.1 公共声明

在`app_tcp_client.h`中，`AppTcpClient_GetConsecutiveFailureCount()`声明之后、`AppTcpClient_MarkSessionStable()`的注释之前，加入：

```c
/*
 * 只由独占本TCP Client的协议Task调用，不访问W5500、不获取Mutex。
 * true仅表示当前有效连接没有SEND在途；调用者仍须核对原会话编号。
 */
bool AppTcpClient_IsSendDrained(const AppTcpClient_t *pxClient);
```

#### 3.4.2 查询实现

在`app_tcp_client.c`中，`AppTcpClient_GetConsecutiveFailureCount()`定义之后、`AppTcpClient_MarkSessionStable()`之前，加入：

```c
bool AppTcpClient_IsSendDrained(const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return false;
    }

    /* 错误/关闭路径也会清除xTxInFlight，必须同时证明连接仍可继续使用。 */
    return pxClient->xEnabled &&
           (pxClient->xState == APP_TCP_CLIENT_STATE_CONNECTED) &&
           (pxClient->xReconnectRequested == false) &&
           (pxClient->xGracefulCloseRequested == false) &&
           (pxClient->xSendBusy == false) &&
           (pxClient->xTxInFlight == false);
}
```

本函数不主动PollSend；已有`AppTcpClient_Service()`与Transport Send继续唯一处理SENDOK和错误。`PEER_FIN`虽然允许旧字节流收尾，但已经不满足本次复位前“原连接仍可用”的条件，查询返回false。

### 3.5 app_mqtt_task.h与app_mqtt_task.c

#### 3.5.1 创建接口与上下文

在`app_mqtt_task.h`中，将现有创建函数声明完整替换为：

```c
bool AppMqttTask_Create(RtosW5500Access_t *pxW5500Access,
                        QueueHandle_t xNetworkStatusQueue,
                        QueueHandle_t xInboundQueue,
                        QueueHandle_t xOutboundQueue,
                        QueueHandle_t xBusinessStatusQueue,
                        QueueHandle_t xPublishReceiptQueue);
```

在`app_mqtt_task.c`的`AppMqttTaskContext_t`中，紧接`xBusinessStatusQueue`追加：

```c
    QueueHandle_t xPublishReceiptQueue; /* MQTT生产，业务Task独占消费。 */
```

在`APP_MQTT_REPORT_BUFFER_SIZE`之后加入：

```c
/* Publish已接受全部字节后的末段SEND排空期限，单位ms。 */
#define APP_MQTT_PUBLISH_DRAIN_TIMEOUT_MS    5000U
```

在现有`s_xOutboundMessage`定义之后加入：

```c
/* 只在ulRequestId非零时有效；原TCP会话与该发布的排空期限。 */
static uint32_t s_ulOutboundTcpSession = 0U;
static TickType_t s_xOutboundDrainStartTick = 0U;
```

#### 3.5.2 完整替换出站处理

将现有`prvProcessOutbound()`完整替换为下面两个函数，位置仍在`prvPublishBusinessStatus()`之前：

```c
static void prvCompletePublishReceipt(const AppMqttTaskContext_t *pxContext,
                                      AppMqttPublishResult_t eResult)
{
    AppMqttPublishReceipt_t xReceipt;
    BaseType_t xQueueResult;

    if (s_xOutboundMessage.ulRequestId == 0U)
    {
        return;
    }

    xReceipt.ulRequestId = s_xOutboundMessage.ulRequestId;
    xReceipt.ulMqttSession = s_xOutboundMessage.ulMqttSession;
    xReceipt.eResult = eResult;

    /* 单在途请求的完成记录；即使业务暂未调度也不阻塞网络Task。 */
    xQueueResult = xQueueOverwrite(pxContext->xPublishReceiptQueue, &xReceipt);
    configASSERT(xQueueResult == pdPASS);
    (void)xQueueResult;

    /* 请求编号就是唯一待回执标志，清除后才允许取下一条出站。 */
    s_xOutboundMessage.ulRequestId = 0U;
}

static void prvProcessOutbound(const AppMqttTaskContext_t *pxContext)
{
    MQTTStatus_t xStatus;
    uint32_t ulSession;
    TickType_t xNowTick;

    ulSession = AppMqttClient_GetSuccessfulSessionCount(&s_xMqttClient);

    if (s_xOutboundMessage.ulRequestId != 0U)
    {
        /* 错误优先于排空：关闭路径清除SEND标志不能冒充原请求成功。 */
        if ((s_xProtocolModulesReady == false) ||
            (s_xNetworkStatus.xChipReady == false) ||
            (s_xNetworkStatus.xPhyLinkUp == false) ||
            (AppMqttClient_GetState(&s_xMqttClient) != APP_MQTT_CLIENT_STATE_READY) ||
            (s_xOutboundMessage.ulMqttSession != ulSession) ||
            (AppTcpClient_GetState(&s_xTcpClient) != APP_TCP_CLIENT_STATE_CONNECTED) ||
            (AppTcpClient_GetSessionGeneration(&s_xTcpClient) != s_ulOutboundTcpSession))
        {
            if (s_ulOutboundFailedCount < UINT32_MAX)
            {
                s_ulOutboundFailedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
            return;
        }

        xNowTick = xTaskGetTickCount();
        if ((TickType_t)(xNowTick - s_xOutboundDrainStartTick) >=
            pdMS_TO_TICKS(APP_MQTT_PUBLISH_DRAIN_TIMEOUT_MS))
        {
            if (s_ulOutboundFailedCount < UINT32_MAX)
            {
                s_ulOutboundFailedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
            return;
        }

        if (AppTcpClient_IsSendDrained(&s_xTcpClient))
        {
            if (s_ulOutboundPublishedCount < UINT32_MAX)
            {
                s_ulOutboundPublishedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_SENT);
        }

        /* 每轮仍运行协议Service，但当前请求结束前不消费下一条outbound。 */
        return;
    }

    /* 每轮至多处理一条；未初始化时也消费并否决旧请求，避免无限留队。 */
    if (xQueueReceive(pxContext->xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return;
    }

    if ((s_xProtocolModulesReady == false) ||
        (s_xNetworkStatus.xChipReady == false) ||
        (s_xNetworkStatus.xPhyLinkUp == false) ||
        (AppMqttClient_GetState(&s_xMqttClient) != APP_MQTT_CLIENT_STATE_READY) ||
        (s_xOutboundMessage.ulMqttSession != ulSession) ||
        (s_xOutboundMessage.usTopicLength == 0U) ||
        (s_xOutboundMessage.usTopicLength > APP_MQTT_OUTBOUND_TOPIC_MAX_LENGTH) ||
        (s_xOutboundMessage.usPayloadLength == 0U) ||
        (s_xOutboundMessage.usPayloadLength > APP_MQTT_OUTBOUND_PAYLOAD_MAX_LENGTH))
    {
        if (s_ulOutboundDroppedCount < UINT32_MAX)
        {
            s_ulOutboundDroppedCount++;
        }
        prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
        return;
    }

    /* 保存发布开始前的连接编号；只有本Task能够改变该TCP Client。 */
    s_ulOutboundTcpSession = AppTcpClient_GetSessionGeneration(&s_xTcpClient);
    xStatus = AppMqttClient_Publish(&s_xMqttClient, s_xOutboundMessage.acTopic,
                                    s_xOutboundMessage.usTopicLength,
                                    s_xOutboundMessage.aucPayload,
                                    s_xOutboundMessage.usPayloadLength);
    if (xStatus != MQTTSuccess)
    {
        if (s_ulOutboundFailedCount < UINT32_MAX)
        {
            s_ulOutboundFailedCount++;
        }
        prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
        return;
    }

    if (s_xOutboundMessage.ulRequestId != 0U)
    {
        /* 最后一段只是被接受；必须留到下一轮Service观察SENDOK。 */
        s_xOutboundDrainStartTick = xTaskGetTickCount();
        return;
    }

    if (s_ulOutboundPublishedCount < UINT32_MAX)
    {
        s_ulOutboundPublishedCount++;
    }
}
```

普通消息的published计数仍在Publish成功时增加；需回执消息仅在末段排空后增加。无效消息增加dropped计数并给需回执请求返回失败；已经发布但排空失败增加failed计数。上述计数只作诊断，不参与复位策略。

#### 3.5.3 完整替换任务入口

将`prvMqttTask()`完整替换为：

```c
static void prvMqttTask(void *pvParameters)
{
    AppMqttTaskContext_t *pxContext = (AppMqttTaskContext_t *)pvParameters;
    TickType_t xNowTick;

    configASSERT(pxContext != NULL);
    configASSERT(RtosW5500Access_IsReady(pxContext->pxW5500Access));
    configASSERT(pxContext->xNetworkStatusQueue != NULL);
    configASSERT(pxContext->xInboundQueue != NULL);
    configASSERT(pxContext->xOutboundQueue != NULL);
    configASSERT(pxContext->xBusinessStatusQueue != NULL);
    configASSERT(pxContext->xPublishReceiptQueue != NULL);

    for (;;)
    {
        prvReceiveNetworkStatus(pxContext);
        xNowTick = xTaskGetTickCount();

        if (s_xProtocolModulesReady)
        {
            AppTcpClient_Service(&s_xTcpClient, s_xNetworkStatus.xPhyLinkUp, xNowTick);
        }

        /* 先观察SENDOK排空，再允许coreMQTT保活发起新的SEND。 */
        prvProcessOutbound(pxContext);

        if (s_xProtocolModulesReady)
        {
            xNowTick = xTaskGetTickCount();
            AppMqttClient_Service(&s_xMqttClient, xNowTick);
            prvReportTcpStateChange();
            prvReportMqttStateChange();
            prvReportMqttInboundCounters();
        }

        /* 包括未初始化或发送失败后的最新READY状态，不保留过期可用快照。 */
        prvPublishBusinessStatus(pxContext);
        vTaskDelay(pdMS_TO_TICKS(APP_MQTT_SERVICE_PERIOD_MS));
    }
}
```

`prvProcessOutbound()`必须位于初始化条件之外，确保旧的需回执请求有明确失败出口。没有W5500访问权限时该函数仅检查既有RAM状态和投递Queue；唯一硬件Service路径仍经过原访问门面。

#### 3.5.4 完整替换创建函数

将`AppMqttTask_Create()`完整替换为：

```c
bool AppMqttTask_Create(RtosW5500Access_t *pxW5500Access,
                        QueueHandle_t xNetworkStatusQueue,
                        QueueHandle_t xInboundQueue,
                        QueueHandle_t xOutboundQueue,
                        QueueHandle_t xBusinessStatusQueue,
                        QueueHandle_t xPublishReceiptQueue)
{
    BaseType_t xCreateResult;

    if ((s_xMqttTaskHandle != NULL) ||
        (RtosW5500Access_IsReady(pxW5500Access) == false) ||
        (xNetworkStatusQueue == NULL) || (xInboundQueue == NULL) ||
        (xOutboundQueue == NULL) || (xBusinessStatusQueue == NULL) ||
        (xPublishReceiptQueue == NULL))
    {
        return false;
    }

    s_xMqttTaskContext.pxW5500Access = pxW5500Access;
    s_xMqttTaskContext.xNetworkStatusQueue = xNetworkStatusQueue;
    s_xMqttTaskContext.xInboundQueue = xInboundQueue;
    s_xMqttTaskContext.xOutboundQueue = xOutboundQueue;
    s_xMqttTaskContext.xBusinessStatusQueue = xBusinessStatusQueue;
    s_xMqttTaskContext.xPublishReceiptQueue = xPublishReceiptQueue;

    xCreateResult = xTaskCreate(prvMqttTask, "MQTT", APP_MQTT_TASK_STACK_DEPTH,
                                &s_xMqttTaskContext, APP_MQTT_TASK_PRIORITY,
                                &s_xMqttTaskHandle);
    if (xCreateResult != pdPASS)
    {
        s_xMqttTaskHandle = NULL;
        s_xMqttTaskContext = (AppMqttTaskContext_t){0};
        return false;
    }

    return true;
}
```

Task栈仍为512 words；仅新增一个局部小回执结构，不把出站消息改成任务栈上的576字节副本。新Queue内联容量约12字节，现有长度4出站Queue每条增加4字节；具体ABI和FreeRTOS管理结构以用户构建及Heap观察为准，不宣称未测量的栈余量已经通过。

### 3.6 静态检查与正常流程验收边界

手工顺序先录入消息头和TCP查询，再录入client升级Topic、Task创建接口和Task实现；随后按主文档同步Startup与业务Task，全部录入后才由用户构建：

```powershell
cmake --build build --parallel
```

命令在`firmware/`执行，不更改Boot、不引入其他构建模式。2026-10-04已完成Debug构建，实机尚未验证，具体记录见第6.1节。

录入检查重点为：长度1回执Queue元素必须是`AppMqttPublishReceipt_t`；声明/定义/Startup调用均有末尾新参数；原普通编码函数清零结构；业务同时只发一笔需回执请求；先结束原ACK回执再提交下一笔升级中消息；仅业务Task在两条匹配SENT回执、本地持久状态及输出检查全部通过后放行复位。队列满、超时或会话变化必须终止自动复位并保持持久请求锁定。

正常升级验收仍由主文档完成：MQTTX能收到升级受理ACK及`upgrade=3, rate=0`，随后Boot安装、新应用确认并上报`upgrade=1, rate=100`。QoS 0和SENDOK不提供客户端显示保证，因此MQTTX观察是实机证据的一部分。故障矩阵按用户决定跳过，失败出口只作代码审查，不记为实测通过。

## 4. MQTT 业务升级流程

目标：`firmware/User/app/mqtt_business/app_mqtt_business_task.h`、`firmware/User/app/mqtt_business/app_mqtt_business_task.c`。下列内容复用现有 JSON 校验、出站缓存、运行秒数和输出接口，不另建升级 Task。

### 4.1 创建接口、依赖与私有状态

把头文件中的 `AppMqttBusinessTask_Create()` 声明替换为：

```c
bool AppMqttBusinessTask_Create(QueueHandle_t xInboundQueue,
                                QueueHandle_t xOutboundQueue,
                                QueueHandle_t xBusinessStatusQueue,
                                QueueHandle_t xFtpCommandQueue,
                                QueueHandle_t xFtpStatusQueue,
                                QueueHandle_t xStorageRequestQueue,
                                QueueHandle_t xStorageStatusQueue,
                                QueueHandle_t xPublishReceiptQueue);
```

在 `.c` 的已有项目头文件区域增加：

```c
#include "app_storage_task.h"
#include "app_firmware_image.h"
#include "app_serial_task.h"
#include "stm32f10x.h"
```

不要再次包含 `Appinfo.h`：其中是带 section 属性的静态常量，多个编译单元包含会重复生成描述头内容。下面版本日志直接读取 Boot 已校验的内部 Flash 描述头。

在 `AppMqttBusinessTaskContext_t` 的 `xFtpStatusQueue` 后追加：

```c
    QueueHandle_t xStorageRequestQueue;
    QueueHandle_t xStorageStatusQueue; /* 只Peek，Storage是唯一写入者。 */
    QueueHandle_t xPublishReceiptQueue; /* 业务独占消费。 */
```

在现有全部私有变量之后、`prvIncrementCounter()`之前插入以下定义及前置声明：

```c
#define APP_UPGRADE_STORAGE_WAIT_MS    5000U
#define APP_UPGRADE_PUBLISH_WAIT_MS    15000U

typedef enum
{
    APP_UPGRADE_IDLE = 0,
    APP_UPGRADE_START_COMMIT,
    APP_UPGRADE_START_COMMIT_CANCEL,
    APP_UPGRADE_START_ACK,
    APP_UPGRADE_START_ACK_CANCEL,
    APP_UPGRADE_PROGRESS,
    APP_UPGRADE_PROGRESS_CANCEL,
    APP_UPGRADE_RESET_READY,
    APP_UPGRADE_CANCEL_COMMIT,
    APP_UPGRADE_CANCEL_ACK,
    APP_UPGRADE_NOTICE,
    APP_UPGRADE_LATE_STORAGE,
    APP_UPGRADE_LATE_STORAGE_CANCEL,
    APP_UPGRADE_LATE_PUBLISH,
    APP_UPGRADE_LATE_PUBLISH_CANCEL
} AppUpgradeFlowState_t;

typedef struct
{
    AppUpgradeFlowState_t xState; /* 本模块唯一升级执行进度。 */
    uint32_t ulRequestId;         /* 当前只等待一笔Storage或发送回执。 */
    uint32_t ulMetadataGeneration; /* 防止把另一候选当成本次受理镜像。 */
    uint32_t ulMqttSession;       /* 当前命令/发布的原会话。 */
    uint32_t ulNextRequestId;     /* 本机非零编号，不写Flash或JSON。 */
    uint32_t ulResultSession;     /* 本次运行最近尝试补报的READY会话。 */
    TickType_t xStartTick;        /* 当前在途请求的期限起点。 */
} AppUpgradeFlow_t;

static AppUpgradeFlow_t s_xUpgrade = {.ulNextRequestId = 1U};
/* Queue按值复制；工作缓存不压入512 words的业务Task栈。 */
static AppStorageRequest_t s_xUpgradeRequest;

static const char s_cUpgradeKey[] = "upgrade";
static const char s_cUpgradeQuery[] = "body.upgrade";
static const char s_cUpgradeMethod[] = "fileupgradecommand";
static const char s_cUpgradeMessageId[] = "8";
static const AppJsonExpectedKey_t s_xUpgradeBodyKeys[] =
{
    {s_cUpgradeKey, sizeof(s_cUpgradeKey) - 1U}
};
static const char s_cUpgradeAckTopic[] = "Client2Server/fileupgradecommand";
static const char s_cUpgradeAckFormat[] =
    "{\"header\":{\"method\":\"fileupgradecommand\",\"messageId\":\"8\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{\"result\":%s}}";
static const char s_cUpgradeResultTopic[] = "Client2Server/filedownloadprocess";
static const char s_cUpgradeResultFormat[] =
    "{\"header\":{\"method\":\"filedownloadprocess\",\"messageId\":\"3\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{\"upgrade\":%u,\"rate\":%u}}";

static bool prvUpgradeDownloadAllowed(void);
static void prvHandleUpgradeCommand(void);
static void prvServiceUpgrade(TickType_t xNowTick);
```

这些状态不是第二份 Storage/Boot 状态机：它们只描述业务正在等待哪一个异步接口，以及同一等待期内是否已经收到取消。`RESET_READY`专门把发送完成与下一轮最终复位决策分开，以优先处理已入站取消。没有另存 `accepted`、`pending`、`resetRequested`、`confirmed` 等标志；持久状态只从 Storage Peek 获取。

### 4.2 入站分派与下载互斥

在现有 `prvProcessInboundMessages()`中做以下三处局部修改，不覆盖无关控制代码：

1. 原 `if (s_xPendingFtpAck)` 改为：

```c
        if (s_xPendingFtpAck && (s_xUpgrade.xState == APP_UPGRADE_IDLE))
        {
            break;
        }
```

2. 在下载命令分支前插入升级分支；下载分支增加保护，避免在升级中积压/覆盖多个拒绝 ACK：

```c
        if (s_xInboundMessage.eMethod == APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND)
        {
            prvHandleUpgradeCommand();
            memset(s_xInboundMessage.aucPayload, 0, s_xInboundMessage.usPayloadLength);
            continue;
        }
        if (s_xInboundMessage.eMethod == APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND)
        {
            if (s_xPendingFtpAck == false)
            {
                prvHandleFtpDownloadCommand(xFtpCommandQueue);
            }
            memset(s_xInboundMessage.aucPayload, 0, s_xInboundMessage.usPayloadLength);
            continue;
        }
```

3. 原控制执行条件 `if ((ulControlType == 1U) || (ulControlType == 2U))` 改为：

```c
        if ((s_xUpgrade.xState == APP_UPGRADE_IDLE) &&
            ((ulControlType == 1U) || (ulControlType == 2U)))
```

其内部 `SetOne()`、计数及控制 ACK 保留。升级执行期间控制命令不改变输出，沿用 `result=false`。本项目输出仅实验 LED/蜂鸣器；未实现跨 CAN/所有控制源的硬件联锁，不能宣称实际危险设备已有完备安全保护。

在 `prvHandleFtpDownloadCommand()`的原受理 `if` 中，末尾增加 `prvUpgradeDownloadAllowed()`：

```c
    if (xCommandValid && s_xHasFtpStatus && s_xFtpStatus.xAcceptingCommand &&
        (s_xPendingFtpAck == false) && (s_xPendingFtpResult == false) &&
        prvUpgradeDownloadAllowed())
```

其余解析、事务号、清密码、下载 ACK 不变。下载既不能插进业务升级在途，也不能越过 Storage 的持久升级锁定。

### 4.3 完整增加业务辅助函数

以下所有函数整体追加到 `.c` 文件末尾，位于现有 JSON、FTP 函数之后。前置声明已在4.1给出，其余函数按下面顺序定义即可。

```c
static uint32_t prvTakeUpgradeRequestId(void)
{
    uint32_t ulId = s_xUpgrade.ulNextRequestId;

    s_xUpgrade.ulNextRequestId++;
    if (s_xUpgrade.ulNextRequestId == 0U)
    {
        s_xUpgrade.ulNextRequestId = 1U;
    }
    return ulId;
}

static bool prvUpgradeDownloadAllowed(void)
{
    AppStorageStatus_t xStatus;

    if ((s_xUpgrade.xState != APP_UPGRADE_IDLE) ||
        (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS))
    {
        return false;
    }
    return (xStatus.xState == APP_STORAGE_STATE_IDLE) ||
           (xStatus.xState == APP_STORAGE_STATE_FAILED) ||
           (xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE);
}

static bool prvParseUpgradeCommand(bool *pxHeaderValid, uint32_t *pulUpgrade)
{
    const uint8_t *pucPayload = s_xInboundMessage.aucPayload;
    uint16_t usLength = s_xInboundMessage.usPayloadLength;
    const char *pcHeader;
    const char *pcBody;
    const char *pcValue;
    size_t xHeaderLength;
    size_t xBodyLength;
    size_t xValueLength;
    uint64_t ullValue;

    *pxHeaderValid = false;
    if ((JSON_Validate((const char *)pucPayload, usLength) != JSONSuccess) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cHeaderKey,
                             sizeof(s_cHeaderKey) - 1U, JSONObject,
                             &pcHeader, &xHeaderLength) == false) ||
        (prvObjectHasExactKeys(pcHeader, xHeaderLength, s_xHeaderKeys,
                               sizeof(s_xHeaderKeys) / sizeof(s_xHeaderKeys[0])) == false))
    {
        return false;
    }
    if ((prvSearchStringEquals(pucPayload, usLength, s_cMethodQuery,
                               sizeof(s_cMethodQuery) - 1U, s_cUpgradeMethod,
                               sizeof(s_cUpgradeMethod) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cMessageIdQuery,
                               sizeof(s_cMessageIdQuery) - 1U, s_cUpgradeMessageId,
                               sizeof(s_cUpgradeMessageId) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cVersionQuery,
                               sizeof(s_cVersionQuery) - 1U, s_cExpectedVersion,
                               sizeof(s_cExpectedVersion) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cDeviceIdQuery,
                               sizeof(s_cDeviceIdQuery) - 1U, s_cExpectedDeviceId,
                               sizeof(s_cExpectedDeviceId) - 1U) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cTimestampQuery,
                             sizeof(s_cTimestampQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false))
    {
        return false;
    }

    *pxHeaderValid = true; /* 可信头但Body错误，仍可发送否定业务ACK。 */
    if ((prvObjectHasExactKeys((const char *)pucPayload, usLength, s_xRootKeys,
                               sizeof(s_xRootKeys) / sizeof(s_xRootKeys[0])) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cBodyKey,
                             sizeof(s_cBodyKey) - 1U, JSONObject,
                             &pcBody, &xBodyLength) == false) ||
        (prvObjectHasExactKeys(pcBody, xBodyLength, s_xUpgradeBodyKeys,
                               sizeof(s_xUpgradeBodyKeys) /
                               sizeof(s_xUpgradeBodyKeys[0])) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cUpgradeQuery,
                             sizeof(s_cUpgradeQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false) ||
        ((ullValue != 1U) && (ullValue != 2U)))
    {
        return false;
    }
    *pulUpgrade = (uint32_t)ullValue;
    return true;
}

static bool prvQueueUpgradeAck(bool xAccepted, bool xTracked, TickType_t xNowTick)
{
    int iLength;

    if (s_xMqttStatus.xReady == false)
    {
        return false;
    }
    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cUpgradeAckTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cUpgradeAckTopic, sizeof(s_cUpgradeAckTopic));
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cUpgradeAckFormat,
                       (unsigned long)prvGetUptimeSeconds(), xAccepted ? "true" : "false");
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    s_xOutboundMessage.ulRequestId = xTracked ? prvTakeUpgradeRequestId() : 0U;
    if (xQueueSend(s_xBusinessTaskContext.xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return false;
    }
    if (xTracked)
    {
        s_xUpgrade.ulRequestId = s_xOutboundMessage.ulRequestId;
        s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
        s_xUpgrade.xStartTick = xNowTick;
    }
    return true;
}

static bool prvQueueUpgradeResult(uint8_t ucUpgrade, TickType_t xNowTick)
{
    int iLength;

    if (s_xMqttStatus.xReady == false)
    {
        return false;
    }
    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cUpgradeResultTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cUpgradeResultTopic, sizeof(s_cUpgradeResultTopic));
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cUpgradeResultFormat,
                       (unsigned long)prvGetUptimeSeconds(), (unsigned int)ucUpgrade,
                       (unsigned int)((ucUpgrade == 1U) ? 100U : 0U));
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    s_xOutboundMessage.ulRequestId = prvTakeUpgradeRequestId();
    if (xQueueSend(s_xBusinessTaskContext.xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return false;
    }
    s_xUpgrade.ulRequestId = s_xOutboundMessage.ulRequestId;
    s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
    s_xUpgrade.xStartTick = xNowTick;
    return true;
}

static bool prvBeginUpgradeStorage(AppStorageRequestType_t xType, TickType_t xNowTick)
{
    AppStorageStatus_t xStatus;

    if ((xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS) ||
        (xStatus.xMetadataValid == false) ||
        (xStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT))
    {
        return false;
    }
    memset(&s_xUpgradeRequest, 0, sizeof(s_xUpgradeRequest));
    s_xUpgradeRequest.xType = xType;
    s_xUpgradeRequest.xData.xUpgrade.ulRequestId = prvTakeUpgradeRequestId();
    s_xUpgradeRequest.xData.xUpgrade.ulExpectedGeneration = xStatus.ulMetadataGeneration;
    if (xQueueSend(s_xBusinessTaskContext.xStorageRequestQueue,
                   &s_xUpgradeRequest, 0U) != pdPASS)
    {
        return false;
    }
    s_xUpgrade.ulRequestId = s_xUpgradeRequest.xData.xUpgrade.ulRequestId;
    s_xUpgrade.ulMetadataGeneration = xStatus.ulMetadataGeneration;
    s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
    s_xUpgrade.xStartTick = xNowTick;
    s_xUpgrade.xState = (xType == APP_STORAGE_REQUEST_START_UPGRADE) ?
        APP_UPGRADE_START_COMMIT : APP_UPGRADE_CANCEL_COMMIT;
    return true;
}

static void prvHandleUpgradeCommand(void)
{
    AppOutputState_t xApplied;
    AppStorageStatus_t xStatus;
    TickType_t xNowTick = xTaskGetTickCount();
    uint32_t ulUpgrade;
    bool xHeaderValid;
    bool xAlreadyAccepted;

    if (prvParseUpgradeCommand(&xHeaderValid, &ulUpgrade) == false)
    {
        if (xHeaderValid)
        {
            (void)prvQueueUpgradeAck(false, false, xNowTick);
        }
        return;
    }

    if (ulUpgrade == 1U)
    {
        /* 收到取消后绝不沿原开始路径复位；先结束旧异步操作，再提交取消。 */
        switch (s_xUpgrade.xState)
        {
            case APP_UPGRADE_START_COMMIT:
                s_xUpgrade.xState = APP_UPGRADE_START_COMMIT_CANCEL;
                return;
            case APP_UPGRADE_START_ACK:
                s_xUpgrade.xState = APP_UPGRADE_START_ACK_CANCEL;
                return;
            case APP_UPGRADE_PROGRESS:
                s_xUpgrade.xState = APP_UPGRADE_PROGRESS_CANCEL;
                return;
            case APP_UPGRADE_LATE_STORAGE:
                s_xUpgrade.xState = APP_UPGRADE_LATE_STORAGE_CANCEL;
                return;
            case APP_UPGRADE_LATE_PUBLISH:
                s_xUpgrade.xState = APP_UPGRADE_LATE_PUBLISH_CANCEL;
                return;
            case APP_UPGRADE_RESET_READY:
            case APP_UPGRADE_IDLE:
                if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick))
                {
                    return;
                }
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                (void)prvQueueUpgradeAck(false, false, xNowTick);
                return;
            case APP_UPGRADE_START_COMMIT_CANCEL:
            case APP_UPGRADE_START_ACK_CANCEL:
            case APP_UPGRADE_PROGRESS_CANCEL:
            case APP_UPGRADE_CANCEL_COMMIT:
            case APP_UPGRADE_CANCEL_ACK:
            case APP_UPGRADE_LATE_STORAGE_CANCEL:
            case APP_UPGRADE_LATE_PUBLISH_CANCEL:
                return; /* 同一取消仍在处理，不另建请求或提前ACK成功。 */
            default:
                (void)prvQueueUpgradeAck(false, false, xNowTick);
                return;
        }
    }

    if (s_xUpgrade.xState != APP_UPGRADE_IDLE)
    {
        if (s_xUpgrade.xState == APP_UPGRADE_START_COMMIT)
        {
            return; /* 原请求仍在提交，沿原流程等待最终ACK，不能提前成功。 */
        }
        xAlreadyAccepted = ((s_xUpgrade.xState == APP_UPGRADE_START_ACK) ||
                            (s_xUpgrade.xState == APP_UPGRADE_PROGRESS) ||
                            (s_xUpgrade.xState == APP_UPGRADE_RESET_READY)) &&
            (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) == pdPASS) &&
            xStatus.xMetadataValid &&
            (xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
            (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED) &&
            (xStatus.ulMetadataGeneration == s_xUpgrade.ulMetadataGeneration);
        /* 重复开始只回复当前受理事实，不提交第二笔请求或tracked发送。 */
        (void)prvQueueUpgradeAck(xAlreadyAccepted, false, xNowTick);
        return;
    }
    if (s_xPendingFtpAck ||
        s_xPendingFtpResult || (s_xHasFtpStatus == false) ||
        (s_xFtpStatus.xAcceptingCommand == false) ||
        (uxQueueMessagesWaiting(s_xBusinessTaskContext.xFtpCommandQueue) != 0U) ||
        (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS) ||
        (xStatus.xMetadataValid == false) ||
        !(((xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
           (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE)) ||
          ((xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
           (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED))))
    {
        /* 固定messageId无法区分并行事务：未就绪时不接受另一笔开始。 */
        (void)prvQueueUpgradeAck(false, false, xNowTick);
        return;
    }
    if ((AppOutputControl_SetAll(false, false, &xApplied) == false) ||
        (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_START_UPGRADE, xNowTick) == false))
    {
        (void)prvQueueUpgradeAck(false, false, xNowTick);
    }
}

static void prvServiceUpgrade(TickType_t xNowTick)
{
    AppStorageStatus_t xStatus;
    AppMqttPublishReceipt_t xReceipt;
    AppOutputState_t xApplied;
    AppUpgradeFlowState_t xPrevious = s_xUpgrade.xState;
    bool xHasStatus;
    bool xHasReceipt;
    bool xSuccess;
    uint8_t ucResult;

    xHasStatus = xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) == pdPASS;
    xHasReceipt = xQueueReceive(s_xBusinessTaskContext.xPublishReceiptQueue,
                                &xReceipt, 0U) == pdPASS;
    if (xHasReceipt && ((xReceipt.ulRequestId != s_xUpgrade.ulRequestId) ||
                       (xReceipt.ulMqttSession != s_xUpgrade.ulMqttSession)))
    {
        xHasReceipt = false; /* 迟到或旧会话结果，不能推进当前流程。 */
    }

    switch (xPrevious)
    {
        case APP_UPGRADE_START_COMMIT:
        case APP_UPGRADE_START_COMMIT_CANCEL:
        case APP_UPGRADE_CANCEL_COMMIT:
        case APP_UPGRADE_LATE_STORAGE:
        case APP_UPGRADE_LATE_STORAGE_CANCEL:
            if (xHasStatus &&
                (xStatus.ulCompletedUpgradeRequestId == s_xUpgrade.ulRequestId))
            {
                if (prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_STORAGE_WAIT_MS))
                {
                    /* 完成和超时在同一轮被看到时，仍按超时后收尾处理。 */
                    if (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL)
                    {
                        xPrevious = APP_UPGRADE_LATE_STORAGE_CANCEL;
                    }
                    else if ((xPrevious == APP_UPGRADE_START_COMMIT) ||
                             (xPrevious == APP_UPGRADE_CANCEL_COMMIT))
                    {
                        xPrevious = APP_UPGRADE_LATE_STORAGE;
                    }
                }
                xSuccess = (xStatus.xUpgradeRequestError == APP_STORAGE_ERROR_NONE) &&
                           xStatus.xMetadataValid &&
                           (xStatus.xState != APP_STORAGE_STATE_DEVICE_FAULT);
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                if ((xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ||
                    (xPrevious == APP_UPGRADE_LATE_STORAGE_CANCEL))
                {
                    if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick) ==
                        false)
                    {
                        (void)prvQueueUpgradeAck(false, false, xNowTick);
                    }
                    return;
                }
                if ((xPrevious == APP_UPGRADE_LATE_STORAGE) ||
                    (s_xMqttStatus.xReady == false) ||
                    (s_xMqttStatus.ulSession != s_xUpgrade.ulMqttSession))
                {
                    return; /* 超时/会话已变，只收尾；持久请求不自动撤销。 */
                }
                if (xPrevious == APP_UPGRADE_CANCEL_COMMIT)
                {
                    xSuccess = xSuccess &&
                        (xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
                        (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE);
                    if (prvQueueUpgradeAck(xSuccess, true, xNowTick))
                    {
                        s_xUpgrade.xState = APP_UPGRADE_CANCEL_ACK;
                    }
                    return;
                }
                xSuccess = xSuccess &&
                    (xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
                    (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED);
                s_xUpgrade.ulMetadataGeneration = xStatus.ulMetadataGeneration;
                if (prvQueueUpgradeAck(xSuccess, true, xNowTick))
                {
                    s_xUpgrade.xState = xSuccess ? APP_UPGRADE_START_ACK : APP_UPGRADE_NOTICE;
                }
                return;
            }
            if (((xPrevious == APP_UPGRADE_START_COMMIT) ||
                 (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ||
                 (xPrevious == APP_UPGRADE_CANCEL_COMMIT)) &&
                prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_STORAGE_WAIT_MS))
            {
                s_xUpgrade.xState = (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ?
                    APP_UPGRADE_LATE_STORAGE_CANCEL : APP_UPGRADE_LATE_STORAGE;
                (void)AppSerial_Write((const uint8_t *)"UPGRADE: storage wait timeout\r\n", 31U);
            }
            return;

        case APP_UPGRADE_START_ACK:
        case APP_UPGRADE_START_ACK_CANCEL:
        case APP_UPGRADE_PROGRESS:
        case APP_UPGRADE_PROGRESS_CANCEL:
        case APP_UPGRADE_CANCEL_ACK:
        case APP_UPGRADE_NOTICE:
        case APP_UPGRADE_LATE_PUBLISH:
        case APP_UPGRADE_LATE_PUBLISH_CANCEL:
            if (xHasReceipt)
            {
                xSuccess = (xReceipt.eResult == APP_MQTT_PUBLISH_RESULT_SENT) &&
                           s_xMqttStatus.xReady &&
                           (s_xMqttStatus.ulSession == s_xUpgrade.ulMqttSession) &&
                           (prvHasExpired(s_xUpgrade.xStartTick, xNowTick,
                                           APP_UPGRADE_PUBLISH_WAIT_MS) == false);
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                if ((xPrevious == APP_UPGRADE_START_ACK_CANCEL) ||
                    (xPrevious == APP_UPGRADE_PROGRESS_CANCEL) ||
                    (xPrevious == APP_UPGRADE_LATE_PUBLISH_CANCEL))
                {
                    if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick) ==
                        false)
                    {
                        (void)prvQueueUpgradeAck(false, false, xNowTick);
                    }
                    return;
                }
                if (xSuccess && (xPrevious == APP_UPGRADE_START_ACK))
                {
                    if (prvQueueUpgradeResult(3U, xNowTick))
                    {
                        s_xUpgrade.xState = APP_UPGRADE_PROGRESS;
                    }
                }
                else if (xSuccess && (xPrevious == APP_UPGRADE_PROGRESS))
                {
                    /* 下一轮先处理入站命令，再做最后一次放行检查。 */
                    s_xUpgrade.xState = APP_UPGRADE_RESET_READY;
                }
                return;
            }
            if ((xPrevious != APP_UPGRADE_LATE_PUBLISH) &&
                (xPrevious != APP_UPGRADE_LATE_PUBLISH_CANCEL) &&
                prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_PUBLISH_WAIT_MS))
            {
                s_xUpgrade.xState = ((xPrevious == APP_UPGRADE_START_ACK_CANCEL) ||
                                     (xPrevious == APP_UPGRADE_PROGRESS_CANCEL)) ?
                    APP_UPGRADE_LATE_PUBLISH_CANCEL : APP_UPGRADE_LATE_PUBLISH;
                (void)AppSerial_Write((const uint8_t *)"UPGRADE: publish wait timeout\r\n", 31U);
            }
            return;

        case APP_UPGRADE_RESET_READY:
            if ((s_xMqttStatus.xReady == false) ||
                (s_xMqttStatus.ulSession != s_xUpgrade.ulMqttSession) ||
                (xHasStatus == false) || (xStatus.xMetadataValid == false) ||
                (xStatus.xState != APP_STORAGE_STATE_UPGRADE_LOCKED) ||
                (xStatus.xPersistentState != APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED) ||
                (xStatus.ulMetadataGeneration != s_xUpgrade.ulMetadataGeneration))
            {
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                return;
            }
            if (uxQueueMessagesWaiting(s_xBusinessTaskContext.xInboundQueue) != 0U)
            {
                return; /* 刚入队的取消优先，留下一轮有界处理。 */
            }
            if (AppOutputControl_SetAll(false, false, &xApplied) == false)
            {
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                return;
            }
            /* 未持有任何W5500/输出Mutex；取消截止于本业务Task放行复位。 */
            NVIC_SystemReset();
            for (;;)
            {
            }

        case APP_UPGRADE_IDLE:
            if ((s_xMqttStatus.xReady == false) || (xHasStatus == false) ||
                (xStatus.xMetadataValid == false) ||
                (s_xUpgrade.ulResultSession == s_xMqttStatus.ulSession) ||
                s_xPendingFtpAck || s_xPendingFtpResult ||
                (uxQueueMessagesWaiting(s_xBusinessTaskContext.xFtpCommandQueue) != 0U))
            {
                return;
            }
            ucResult = 0U;
            if ((xStatus.xState == APP_STORAGE_STATE_IDLE) &&
                (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_CONFIRMED) &&
                (xStatus.ulInstallAttempts > 0U) && (xStatus.ulBootAttempts > 0U))
            {
                ucResult = 1U;
            }
            else if ((xStatus.xState == APP_STORAGE_STATE_FAILED) &&
                     (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_FAILED) &&
                     (xStatus.ulPersistentError == 0x1001UL) &&
                     (xStatus.ulInstallAttempts == 0U))
            {
                /* Boot候选拒绝且旧应用可运行；不是FTP下载失败。 */
                ucResult = 2U;
            }
            if (ucResult != 0U)
            {
                /* 每会话只尝试一次，断线重连才再次补报；不持久保存发送标志。 */
                s_xUpgrade.ulResultSession = s_xMqttStatus.ulSession;
                if (prvQueueUpgradeResult(ucResult, xNowTick))
                {
                    s_xUpgrade.xState = APP_UPGRADE_NOTICE;
                }
            }
            return;

        default:
            configASSERT(false);
            return;
    }
}
```

`0x1001`是本地独立 Boot 的 `BOOT_ERROR_CANDIDATE_REJECTED_BEFORE_INSTALL` 字节约定；Application 不包含 Boot 头文件。其他 P4 故障 Boot 通常停留，应用没有运行就不能联网报告；不虚构故障停留状态还能收到 MQTT 错误结果。队列满、SENT失败、会话变化时不自动重试复位流程；重新下发开始可重新走幂等受理。补报是 QoS 0 的有限尝试，每次新 READY 会话至多一次，不承诺恰好一次可靠投递。

`LATE_*`不是新的重试流程：5000/15000 ms到期立即终止自动复位资格，之后只保留原关联号收取迟到完成，以免回执 Queue被第二笔请求覆盖。若网络或 Storage 永远没有完成，本次运行不能发起第二笔升级；操作者读取日志后决定恢复，不在后台无限重复提交或自动重启。

### 4.4 完整替换业务 Task 入口与创建函数

将原 `prvMqttBusinessTask()`整体替换为：

```c
static void prvMqttBusinessTask(void *pvParameters)
{
    AppMqttBusinessTaskContext_t *pxContext = (AppMqttBusinessTaskContext_t *)pvParameters;
    TickType_t xLastWakeTick;
    TickType_t xNowTick;
    char acVersionReport[48];
    const char *pcSoftware;
    int iLength;

    configASSERT(pxContext != NULL);
    configASSERT(pxContext->xInboundQueue != NULL);
    configASSERT(pxContext->xOutboundQueue != NULL);
    configASSERT(pxContext->xBusinessStatusQueue != NULL);
    configASSERT(pxContext->xFtpCommandQueue != NULL);
    configASSERT(pxContext->xFtpStatusQueue != NULL);
    configASSERT(pxContext->xStorageRequestQueue != NULL);
    configASSERT(pxContext->xStorageStatusQueue != NULL);
    configASSERT(pxContext->xPublishReceiptQueue != NULL);
    configASSERT(configTICK_RATE_HZ > 0U);

    /* 已装应用自身描述头，不能从会被新下载覆盖的候选槽读取版本。 */
    pcSoftware = (const char *)(uintptr_t)(APP_STORAGE_APPLICATION_TARGET_ADDRESS +
                                          APP_FIRMWARE_HARDWARE_MODEL_SIZE);
    iLength = snprintf(acVersionReport, sizeof(acVersionReport), "APP: software=%.*s\r\n",
                       (int)APP_FIRMWARE_SOFTWARE_MODEL_SIZE, pcSoftware);
    if ((iLength > 0) && (iLength < (int)sizeof(acVersionReport)))
    {
        (void)AppSerial_Write((const uint8_t *)acVersionReport, (uint16_t)iLength);
    }
    xLastWakeTick = xTaskGetTickCount();
    s_xLastUptimeTick = xLastWakeTick;
    s_ullUptimeTicks = (uint64_t)xLastWakeTick;

    for (;;)
    {
        xNowTick = xTaskGetTickCount();
        prvUpdateUptime(xNowTick);
        prvReceiveMqttStatus(pxContext->xBusinessStatusQueue);
        prvReceiveFtpStatus(pxContext->xFtpStatusQueue);
        /* 先有界消费已到达的取消，再处理发送回执和最终复位。 */
        prvProcessInboundMessages(pxContext->xInboundQueue, pxContext->xOutboundQueue,
                                  pxContext->xFtpCommandQueue);
        prvServiceUpgrade(xTaskGetTickCount());
        prvServicePendingFtpMessages(pxContext->xOutboundQueue);
        if ((s_xUpgrade.xState == APP_UPGRADE_IDLE) &&
            (s_xPendingFtpAck == false) && (s_xPendingFtpResult == false))
        {
            prvServiceHeartbeat(pxContext->xOutboundQueue, xNowTick);
            prvServiceTemperature(pxContext->xOutboundQueue, xNowTick);
        }
        vTaskDelayUntil(&xLastWakeTick, pdMS_TO_TICKS(APP_MQTT_BUSINESS_SERVICE_PERIOD_MS));
    }
}

bool AppMqttBusinessTask_Create(QueueHandle_t xInboundQueue,
                                QueueHandle_t xOutboundQueue,
                                QueueHandle_t xBusinessStatusQueue,
                                QueueHandle_t xFtpCommandQueue,
                                QueueHandle_t xFtpStatusQueue,
                                QueueHandle_t xStorageRequestQueue,
                                QueueHandle_t xStorageStatusQueue,
                                QueueHandle_t xPublishReceiptQueue)
{
    BaseType_t xCreateResult;

    if ((s_xBusinessTaskHandle != NULL) || (xInboundQueue == NULL) ||
        (xOutboundQueue == NULL) || (xBusinessStatusQueue == NULL) ||
        (xFtpCommandQueue == NULL) || (xFtpStatusQueue == NULL) ||
        (xStorageRequestQueue == NULL) || (xStorageStatusQueue == NULL) ||
        (xPublishReceiptQueue == NULL))
    {
        return false;
    }
    s_xBusinessTaskContext.xInboundQueue = xInboundQueue;
    s_xBusinessTaskContext.xOutboundQueue = xOutboundQueue;
    s_xBusinessTaskContext.xBusinessStatusQueue = xBusinessStatusQueue;
    s_xBusinessTaskContext.xFtpCommandQueue = xFtpCommandQueue;
    s_xBusinessTaskContext.xFtpStatusQueue = xFtpStatusQueue;
    s_xBusinessTaskContext.xStorageRequestQueue = xStorageRequestQueue;
    s_xBusinessTaskContext.xStorageStatusQueue = xStorageStatusQueue;
    s_xBusinessTaskContext.xPublishReceiptQueue = xPublishReceiptQueue;

    xCreateResult = xTaskCreate(prvMqttBusinessTask, "MQTT_BUS",
                                APP_MQTT_BUSINESS_TASK_STACK_DEPTH,
                                &s_xBusinessTaskContext, APP_MQTT_BUSINESS_TASK_PRIORITY,
                                &s_xBusinessTaskHandle);
    if (xCreateResult != pdPASS)
    {
        s_xBusinessTaskHandle = NULL;
        s_xBusinessTaskContext = (AppMqttBusinessTaskContext_t){0};
        return false;
    }
    return true;
}
```

版本日志是长期诊断，不是另建测试计数。第一次补报不能只看旧 `CONFIRMED`：还要满足运行 `IDLE`、实际安装/启动次数非零，且没有已经排队的下载；下载进入 `PREPARING`或替换旧记录后停止补报旧结果。

## 5. Startup 接线

目标：`firmware/User/app/startup/app_startup.h`、`firmware/User/app/startup/app_startup.c`。不改 `main.c` 的任务编排、不增加源文件或 CMake 配置。

1. `.h` 的状态枚举末尾，把原最后一项改为下面两项，以保留所有旧错误值：

```c
    APP_STARTUP_STATUS_FTP_TASK_CREATE_FAILED,
    APP_STARTUP_STATUS_MQTT_PUBLISH_RECEIPT_QUEUE_CREATE_FAILED
```

2. `.c` 的现有 `s_xMqttBusinessStatusQueue` 定义之后加入：

```c
/* Startup拥有；MQTT Task生产，业务Task独占消费。 */
static QueueHandle_t s_xMqttPublishReceiptQueue = NULL;
```

`s_xStorageStatusQueue`的说明改为“Storage覆盖写入；FTP和业务各自零等待Peek，不消费”，其长度仍为1。

3. `AppStartup_Create()`的“已经创建”检查中，紧接 `s_xMqttBusinessStatusQueue` 的检查增加：

```c
        (s_xMqttPublishReceiptQueue != NULL) ||
```

4. 现有业务状态 Queue 创建及 NULL 判断完成后、按键 Queue 创建之前加入：

```c
    s_xMqttPublishReceiptQueue = xQueueCreate(APP_MQTT_PUBLISH_RECEIPT_QUEUE_LENGTH,
                                              sizeof(AppMqttPublishReceipt_t));
    if (s_xMqttPublishReceiptQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_PUBLISH_RECEIPT_QUEUE_CREATE_FAILED;
    }
```

5. 将原MQTT创建的整个`if`块（包括旧失败返回和大括号）替换为下面这一块，不保留旧主体：

```c
    if (AppMqttTask_Create(&s_xW5500Access, s_xMqttNetworkStatusQueue,
                           s_xMqttInboundQueue, s_xMqttOutboundQueue,
                           s_xMqttBusinessStatusQueue, s_xMqttPublishReceiptQueue) == false)
    {
        return APP_STARTUP_STATUS_MQTT_TASK_CREATE_FAILED;
    }
```

6. 将原业务Task创建的整个`if`块（包括旧失败返回和大括号）替换为下面这一块，不保留旧主体：

```c
    if (AppMqttBusinessTask_Create(s_xMqttInboundQueue, s_xMqttOutboundQueue,
                                   s_xMqttBusinessStatusQueue, s_xFtpCommandQueue,
                                   s_xFtpStatusQueue, s_xStorageRequestQueue,
                                   s_xStorageStatusQueue, s_xMqttPublishReceiptQueue) == false)
    {
        return APP_STARTUP_STATUS_MQTT_BUSINESS_TASK_CREATE_FAILED;
    }
```

原 Storage、FTP 创建接口与顺序不变。Startup 使用 `sizeof()` 创建 Queue，重新构建后自动采用扩展后的元素大小，不能保留旧大小或把回执 Queue设成出站消息类型。

## 6. 手工修改顺序与构建

分三批录入，全部接线后统一构建，避免先改声明但遗漏调用造成链接报错：

1. 第2节：Storage 请求/快照、持久错误映射、升级提交和完成；FTP读取改为单次Peek。
2. 第3节：MQTT消息契约、订阅、TCP发送排空查询、发送回执及任务创建接口。
3. 第4～5节：业务解析/执行、下载互斥、版本日志、创建接口、Startup回执Queue及两处调用。

没有新的 `.c/.h` 文件、第三方依赖、Boot接口、链接脚本或构建模式。资源新增主要是一个长度1回执Queue、一个约524字节业务Storage请求工作缓存、少量关联字段及Queue元素扩展；无新Task栈。准确RAM/Flash与栈余量以用户构建和运行观察为准，未测量不得宣称已通过。

由用户在项目的 `firmware/` 目录执行：

```powershell
cmake --build build --parallel
```

已核对当前CMake输出仍为 `application.elf/.bin/.hex`，不新增另一种模式。构建失败先提供第一处错误；不要先烧录或把未完成代码混进已有FTP验收记录。

### 6.1 本次实施与构建记录

2026-10-04先按用户授权接入本文13份Application源码，保留用户首次确认、Flash分页和MD5逻辑；随后结项审查发现Startup替换调用时残留了两个孤立的失败返回块。它们会让`AppStartup_Create()`无条件失败，即使编译通过也不能启动调度器。按本轮新的明确授权已删除两块；每个创建调用只保留对应`if (... == false)`的失败出口，所有任务创建成功时才返回`APP_STARTUP_STATUS_OK`。

同轮修正`prvRefreshStorageStatus()`，成功完成态不再重算并封顶传输百分比，FTP内部与MQTT外部均保留100%。只改`app_startup.c`和`app_ftp_task.c`两个源码文件；Boot源码、本次之外的Application源码及CMake均未改，不新增Task、状态机、库或构建模式。

修正后的Debug重新构建通过，输出`application.elf/.bin/.hex`。沙箱内GCC启动遇到Windows DLL重定位错误，经授权在沙箱外使用本机已有GNU Make 4.2.1、同一编译器与现有build完成增量构建，不读取或输出外部MQTT凭据。普通构建仍按第6节执行；本机替代命令在`firmware/`执行：

```powershell
mingw32-make -C build -j2 "SHELL=cmd.exe"
```

- ARM GCC 14.3.1，Debug构建通过；新编译的两个源码无新增警告，链接保留原newlib/nosys占位警告。
- FLASH 87476字节/448 KiB、RAM 47112字节/64 KiB；运行期Task栈与动态Heap余量未实测。
- `application.bin`实际长度与头中长度均为87476字节，硬件型号`FIRE_F103_V2`、软件型号`APP.V1.0.0`，476字节保留区全0。
- 描述头地址`0x08010000`、长度512；向量表`0x08010200`，MSP`0x20010000`、Reset`0x08010D65`。
- 源码Diff检查通过；旧64364字节构建受无条件返回导致的链接裁剪影响，不能作为最终升级基线，已被本次产物替代。

没有烧录、操作板卡或执行Git写操作。用户已决定结束开发并完整归档，发送回执、候选安装、首次确认和再次复位仍没有最终实机反馈，不能写成已通过；故障测试保持跳过。收尾入口为[结项记录](PROJECT_CLOSEOUT.md)。

## 7. 正常升级实机验收

本节作为未来维护时的正常升级操作参考保留，按用户最新决定不再作为开发结项必做项。未执行的步骤不能按“默认成功”填写已通过，不做故障注入或命令错误矩阵。

### 7.1 先烧录接通升级能力的基线应用

保留现有Boot。在受控稳定供电下，由用户烧录本轮修改后的基线 `application.elf`；记录串口 `APP: software=APP.V1.0.0`（若当前版本不同，使用实际值），确认 MQTT READY、原心跳/温度正常。

烧录前若元数据仍是 `UPGRADE_REQUESTED/INSTALLING/BOOT_PENDING`，先说明实际状态再处理；尤其 `BOOT_PENDING` 的完整文件 MD5 与基线ELF不同，不能直接覆盖内部Flash后期待旧确认路径继续通过。正常起点应是先前已验收的可启动应用，且无在途升级。本轮不指导全片擦除或删除候选/元数据。

在 `firmware/` 执行已使用的应用烧录命令（不必再次烧Boot）：

```powershell
openocd -f interface/cmsis-dap.cfg `
    -c "transport select swd" `
    -f target/stm32f1x.cfg `
    -c "adapter speed 500" `
    -c "program build/application.elf verify reset exit"
```

应看到 `Verified OK`，并以串口实际运行作为启动证据。擦除范围补齐警告不是成功判据。

### 7.2 生成具有不同版本的待升级应用

只把现有 `Appinfo.h` 的软件型号由当前版本改为下一版，例如 `APP.V1.0.0 → APP.V1.0.1`，硬件型号/描述头布局/地址不变；再次构建。**此时不烧录新版应用**，否则不能证明Boot通过FTP候选安装成功。

将新 `build/application.bin` 复制到现有 FileZilla 映射的 `firmware` 目录，并在PowerShell计算实际文件MD5：

```powershell
Get-FileHash -LiteralPath "build/application.bin" -Algorithm MD5
```

用现有下载命令下发这个完整文件MD5。等待下载 `fileDownloadResult=1/fileDownloadRate=100`，确认Storage为 `READY_TO_UPGRADE`。不要沿用之前82624字节文件的旧MD5；新增业务代码后文件长度已经可能变化。FileZilla凭据仍在MQTTX本机输入，不写进仓库。

### 7.3 下发开始升级并观察复位

MQTTX订阅 `Client2Server/fileupgradecommand` 与 `Client2Server/filedownloadprocess`，QoS0、不勾Retain。向 `Server2Client/fileupgradecommand` 发：

```json
{
  "header": {
    "method": "fileupgradecommand",
    "messageId": "8",
    "version": "0.0.2",
    "timestamp": 0,
    "deviceId": "192.168.101.50"
  },
  "body": {
    "upgrade": 2
  }
}
```

应观察到受理 ACK `result=true`，随后 `filedownloadprocess` 的 `upgrade=3/rate=0`，再出现Boot安装及应用重新启动。若ACK成功却未复位，查看发送/Storage状态，不手工复位“代替”本项正常自动复位验收。

`upgrade=3`不是FTP百分比，更不是Boot逐页进度；Boot裸机安装不运行MQTT，首版只报告“已受理/升级中”和新应用确认后的最终结果。

### 7.4 确认新版并再次复位

证据必须同时满足：串口新应用 `APP: software=APP.V1.0.1`，Storage持久状态已提交 `CONFIRMED=8`（运行态IDLE，不把运行态数字8当CONFIRMED），新MQTT READY会话收到 `upgrade=1/rate=100`。成功不是从候选描述头猜测，而是由已经运行的内部新版和本地首次确认共同证明。

再按一次开发板复位键：仍打印新版、正常恢复心跳/温度/CAN，保持 `CONFIRMED`，不重复安装、不再消耗首次启动次数。随后这个项目的技术开发结束，仅根据真实通过的功能定稿 `docs/项目简历.md`；不做WPF，不扩展新功能。

取消功能的代码边界已经给出；用户跳过错误/故障场景，不能写取消/断电恢复/重试上限的实机测试已通过。

## 8. 检查边界与参考依据

本轮按已批准方案完成源码接入、接口/Queue所有权检查、Debug构建及镜像布局检查，记录见第6.1节。未执行设备测试或Git写操作；接下来由用户进行第7节正常升级实机验收，构建通过不等于硬件链路已通过。

参考本地固定版本 FreeRTOS Kernel V11.3.0 Queue实现、coreJSON及coreMQTT v2.3.1、W5500 Datasheet v1.0.9。仅采用适配本项目单Socket所有者、有界队列和非阻塞发送的语义，第三方源码不改；SENDOK引用与细节见第3节。Boot的持久错误0x1001约定以独立 `bootloader/User/app/boot/boot_controller.c` 中实际实现为准，不建立源码复用。
