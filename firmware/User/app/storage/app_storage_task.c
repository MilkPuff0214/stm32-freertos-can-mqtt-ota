#include "app_storage_task.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "task.h"

#include "app_storage_layout.h"
#include "app_storage_metadata.h"
#include "app_firmware_image.h"
#include "app_serial_task.h"
#include "bsp_w25q64.h"
#include "mbedtls/md5.h"
#include "app_adc_task.h"

#define APP_STORAGE_TASK_PRIORITY       (tskIDLE_PRIORITY + 1U)

#define APP_STORAGE_TASK_STACK_WORDS                512U
#define APP_STORAGE_BUSY_POLL_TICKS                 1U
#define APP_STORAGE_SECTOR_ERASE_TIMEOUT_MS         1000U
#define APP_STORAGE_PAGE_PROGRAM_TIMEOUT_MS         100U
#define APP_STORAGE_BOOT_CONFIRM_POLL_MS            100U

/* 与protocol.md的持久字节契约一致，不包含Boot工程头文件。 */
#define APP_STORAGE_PERSISTENT_ERROR_CANDIDATE_REJECTED    0x1001UL

typedef enum
{
    APP_STORAGE_OPERATION_IDLE = 0,
    APP_STORAGE_OPERATION_METADATA_ERASE_START,
    APP_STORAGE_OPERATION_METADATA_ERASE_WAIT,
    APP_STORAGE_OPERATION_METADATA_BODY_START,
    APP_STORAGE_OPERATION_METADATA_BODY_WAIT,
    APP_STORAGE_OPERATION_METADATA_BODY_VERIFY,
    APP_STORAGE_OPERATION_METADATA_COMMIT_START,
    APP_STORAGE_OPERATION_METADATA_COMMIT_WAIT,
    APP_STORAGE_OPERATION_METADATA_FINAL_VERIFY,
    APP_STORAGE_OPERATION_CANDIDATE_ERASE_START,        /* 开始擦除候选区扇区 */
    APP_STORAGE_OPERATION_CANDIDATE_ERASE_WAIT,
    APP_STORAGE_OPERATION_PAGE_PROGRAM_START,
    APP_STORAGE_OPERATION_PAGE_PROGRAM_WAIT,
    APP_STORAGE_OPERATION_PAGE_PROGRAM_VERIFY,
    APP_STORAGE_OPERATION_FINAL_VERIFY
} AppStorageOperation_t;

typedef enum
{
    APP_STORAGE_STEP_ERROR = -1,
    APP_STORAGE_STEP_BUSY = 0,
    APP_STORAGE_STEP_COMPLETE = 1
} AppStorageStepResult_t;

/* AppStorageTask独占W25Q64，任一时刻只会有一个异步Flash操作。 */
static AppStorageOperation_t s_xOperation = APP_STORAGE_OPERATION_IDLE;

/* 保存从Queue取出的当前请求；WRITE时需跨多个周期保留完整512字节数据块。 */
static AppStorageRequest_t s_xCurrentRequest;

/* A/B中当前最新且校验有效的元数据记录。 */
static AppStorageMetadataRecord_t s_xActiveMetadata;

/* 启动时从元数据A扇区读取的原始256字节。 */
static uint8_t s_aucMetadataA[APP_STORAGE_METADATA_RECORD_SIZE];

/* 启动时从元数据B扇区读取的原始256字节。 */
static uint8_t s_aucMetadataB[APP_STORAGE_METADATA_RECORD_SIZE];

/* 将待提交元数据编码为准备写入Flash的固定256字节格式。 */
static uint8_t s_aucMetadataWrite[APP_STORAGE_METADATA_RECORD_SIZE];

/* PREPARE提供的完整application.bin预期MD5，计算范围包含512字节描述头。 */
static uint8_t s_aucExpectedFirmwareMd5[APP_STORAGE_MD5_DIGEST_SIZE];

/* 当前最新有效元数据所在地址；UINT32_MAX表示A/B均无有效记录。 */
static uint32_t s_ulActiveMetadataAddress = UINT32_MAX;

/* 当前W25Q64擦除或页编程命令的启动Tick，用于判断单次操作超时。 */
static TickType_t s_xOperationStartTick = 0U;

/* 是否已收到当前事务的ABORT；等在途Flash命令完成后安全停止。 */
static bool s_xAbortRequested = false;

/* 当前WRITE块内已经完成页编程并读回一致的字节数，只由Storage Task维护。 */
static uint16_t s_usCurrentBlockWritten = 0U;

/* 元数据、固件页和描述头串行操作，共用唯一读回缓冲。 */
static uint8_t s_aucReadback[APP_STORAGE_FIRMWARE_HEADER_SIZE];

_Static_assert(APP_STORAGE_METADATA_RECORD_SIZE <= sizeof(s_aucReadback),
               "Metadata record exceeds the shared readback buffer");
_Static_assert(APP_STORAGE_FIRMWARE_HEADER_SIZE <= sizeof(s_aucReadback),
               "Firmware header exceeds the shared readback buffer");

static QueueHandle_t s_xRequestQueue = NULL;
static QueueHandle_t s_xStatusQueue = NULL;
static AppStorageStatus_t s_xStorageStatus = {0};


/* 累计已经写入W25Q64并读回一致的完整application.bin字节。 */
static mbedtls_md5_context s_xFirmwareMd5;
static void prvReportStatus(void);

static bool prvHasExpired(TickType_t xStartTick, TickType_t xNowTick,
                          uint32_t ulTimeoutMs)
{
    TickType_t xTimeoutTicks = pdMS_TO_TICKS(ulTimeoutMs);

    if (xTimeoutTicks == 0U)
    {
        xTimeoutTicks = 1U;
    }

    return (TickType_t)(xNowTick - xStartTick) >=
           xTimeoutTicks;
}

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

static AppStorageStepResult_t prvServiceMetadataCommit(TickType_t xNowTick)
{
    BspW25Q64OperationResult_t xOperationResult;
    BspW25Q64PollResult_t xPollResult;
    AppStorageMetadataRecord_t xDecodedRecord;
    uint32_t ulMetadataAddress =
        (s_ulActiveMetadataAddress == APP_STORAGE_METADATA_A_ADDRESS) ?
        APP_STORAGE_METADATA_B_ADDRESS : APP_STORAGE_METADATA_A_ADDRESS;

    switch (s_xOperation)
    {
        case APP_STORAGE_OPERATION_IDLE:
            return APP_STORAGE_STEP_COMPLETE;

        case APP_STORAGE_OPERATION_METADATA_ERASE_START:
            xOperationResult = BspW25Q64_SectorEraseStart(ulMetadataAddress);
            if (xOperationResult != BSP_W25Q64_OPERATION_STARTED)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            s_xOperationStartTick = xNowTick;
            s_xOperation = APP_STORAGE_OPERATION_METADATA_ERASE_WAIT;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_ERASE_WAIT:
            xPollResult = BspW25Q64_PollReady();
            if (xPollResult == BSP_W25Q64_POLL_ERROR)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            if (xPollResult == BSP_W25Q64_POLL_BUSY)
            {
                return prvHasExpired(s_xOperationStartTick, xNowTick,
                                     APP_STORAGE_SECTOR_ERASE_TIMEOUT_MS) ?
                    APP_STORAGE_STEP_ERROR : APP_STORAGE_STEP_BUSY;
            }
            s_xOperation = APP_STORAGE_OPERATION_METADATA_BODY_START;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_BODY_START:
            xOperationResult = BspW25Q64_PageProgramStart(
                ulMetadataAddress, s_aucMetadataWrite,
                APP_STORAGE_METADATA_BODY_SIZE);
            if (xOperationResult != BSP_W25Q64_OPERATION_STARTED)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            s_xOperationStartTick = xNowTick;
            s_xOperation = APP_STORAGE_OPERATION_METADATA_BODY_WAIT;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_BODY_WAIT:
            xPollResult = BspW25Q64_PollReady();
            if (xPollResult == BSP_W25Q64_POLL_ERROR)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            if (xPollResult == BSP_W25Q64_POLL_BUSY)
            {
                return prvHasExpired(s_xOperationStartTick, xNowTick,
                                     APP_STORAGE_PAGE_PROGRAM_TIMEOUT_MS) ?
                    APP_STORAGE_STEP_ERROR : APP_STORAGE_STEP_BUSY;
            }
            s_xOperation = APP_STORAGE_OPERATION_METADATA_BODY_VERIFY;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_BODY_VERIFY:
            if ((BspW25Q64_Read(ulMetadataAddress,
                                s_aucReadback,
                                APP_STORAGE_METADATA_BODY_SIZE) == false) ||
                (memcmp(s_aucReadback, s_aucMetadataWrite,
                        APP_STORAGE_METADATA_BODY_SIZE) != 0))
            {
                return APP_STORAGE_STEP_ERROR;
            }
            s_xOperation = APP_STORAGE_OPERATION_METADATA_COMMIT_START;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_COMMIT_START:
            xOperationResult = BspW25Q64_PageProgramStart(
                ulMetadataAddress + APP_STORAGE_METADATA_BODY_SIZE,
                &s_aucMetadataWrite[APP_STORAGE_METADATA_BODY_SIZE],
                APP_STORAGE_METADATA_COMMIT_SIZE);
            if (xOperationResult != BSP_W25Q64_OPERATION_STARTED)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            s_xOperationStartTick = xNowTick;
            s_xOperation = APP_STORAGE_OPERATION_METADATA_COMMIT_WAIT;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_COMMIT_WAIT:
            xPollResult = BspW25Q64_PollReady();
            if (xPollResult == BSP_W25Q64_POLL_ERROR)
            {
                return APP_STORAGE_STEP_ERROR;
            }
            if (xPollResult == BSP_W25Q64_POLL_BUSY)
            {
                return prvHasExpired(s_xOperationStartTick, xNowTick,
                                     APP_STORAGE_PAGE_PROGRAM_TIMEOUT_MS) ?
                    APP_STORAGE_STEP_ERROR : APP_STORAGE_STEP_BUSY;
            }
            s_xOperation = APP_STORAGE_OPERATION_METADATA_FINAL_VERIFY;
            return APP_STORAGE_STEP_BUSY;

        case APP_STORAGE_OPERATION_METADATA_FINAL_VERIFY:
            if ((BspW25Q64_Read(ulMetadataAddress,
                                s_aucReadback,
                                APP_STORAGE_METADATA_RECORD_SIZE) == false) ||
                (memcmp(s_aucReadback, s_aucMetadataWrite,
                        APP_STORAGE_METADATA_RECORD_SIZE) != 0) ||
                (AppStorageMetadata_Decode(s_aucReadback,
                                           &xDecodedRecord) == false))
            {
                return APP_STORAGE_STEP_ERROR;
            }

            s_xActiveMetadata = xDecodedRecord;
            s_ulActiveMetadataAddress = ulMetadataAddress;
            s_xOperation = APP_STORAGE_OPERATION_IDLE;
            return APP_STORAGE_STEP_COMPLETE;

        default:
            return APP_STORAGE_STEP_ERROR;
    }
}

/* 擦除候选区扇区，按扇区大小对齐擦除长度 */
static AppStorageStepResult_t prvServiceCandidateErase(TickType_t xNowTick,
                                                       AppStorageError_t *pxError)
{
    BspW25Q64OperationResult_t xOperationResult;
    BspW25Q64PollResult_t xPollResult;
    uint32_t ulEraseLength =
        ((s_xStorageStatus.ulFirmwareLength + BSP_W25Q64_SECTOR_SIZE_BYTES - 1U) /
         BSP_W25Q64_SECTOR_SIZE_BYTES) * BSP_W25Q64_SECTOR_SIZE_BYTES;
    uint32_t ulEraseAddress = APP_STORAGE_CANDIDATE_ADDRESS +
        s_xStorageStatus.ulErasedLength;

    *pxError = APP_STORAGE_ERROR_NONE;
    if (s_xOperation == APP_STORAGE_OPERATION_CANDIDATE_ERASE_START)
    {
        if (s_xAbortRequested)
        {
            s_xOperation = APP_STORAGE_OPERATION_IDLE;
            return APP_STORAGE_STEP_COMPLETE;
        }

        if (s_xStorageStatus.ulErasedLength >= ulEraseLength)
        {
            s_xOperation = APP_STORAGE_OPERATION_IDLE;
            return APP_STORAGE_STEP_COMPLETE;
        }

        xOperationResult = BspW25Q64_SectorEraseStart(ulEraseAddress);
        if (xOperationResult != BSP_W25Q64_OPERATION_STARTED)
        {
            *pxError = APP_STORAGE_ERROR_ERASE_START;
            return APP_STORAGE_STEP_ERROR;
        }
        s_xOperationStartTick = xNowTick;
        s_xOperation = APP_STORAGE_OPERATION_CANDIDATE_ERASE_WAIT;
        return APP_STORAGE_STEP_BUSY;
    }

    xPollResult = BspW25Q64_PollReady();
    if (xPollResult == BSP_W25Q64_POLL_ERROR)
    {
        *pxError = APP_STORAGE_ERROR_SPI;
        return APP_STORAGE_STEP_ERROR;
    }
    if (xPollResult == BSP_W25Q64_POLL_BUSY)
    {
        if (prvHasExpired(s_xOperationStartTick, xNowTick,
                          APP_STORAGE_SECTOR_ERASE_TIMEOUT_MS))
        {
            *pxError = APP_STORAGE_ERROR_ERASE_TIMEOUT;
            return APP_STORAGE_STEP_ERROR;
        }
        return APP_STORAGE_STEP_BUSY;
    }

    s_xStorageStatus.ulErasedLength += BSP_W25Q64_SECTOR_SIZE_BYTES;
    prvPublishStatus();
    if (s_xStorageStatus.ulErasedLength >= ulEraseLength)
    {
        s_xOperation = APP_STORAGE_OPERATION_IDLE;
        return APP_STORAGE_STEP_COMPLETE;
    }

    s_xOperation = APP_STORAGE_OPERATION_CANDIDATE_ERASE_START;
    return APP_STORAGE_STEP_BUSY;
}

/* 元数据写完后的动作直接由刚刚提交成功的持久状态决定 */
static bool prvContinueAfterMetadata(void)
{
    switch (s_xActiveMetadata.xState)
    {
        case APP_STORAGE_PERSISTENT_STATE_PREPARING:
            s_xOperation = APP_STORAGE_OPERATION_CANDIDATE_ERASE_START;
            return true;

        case APP_STORAGE_PERSISTENT_STATE_DOWNLOADING:
            s_xStorageStatus.xState = APP_STORAGE_STATE_READY_FOR_DATA;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            prvPublishStatus();
            return true;

        case APP_STORAGE_PERSISTENT_STATE_VERIFYING:
            s_xOperation = APP_STORAGE_OPERATION_FINAL_VERIFY;
            return true;

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

        case APP_STORAGE_PERSISTENT_STATE_CONFIRMED:
            /* 只有完整记录提交并读回通过后，才解除下载锁定。 */
            s_xStorageStatus.xState = APP_STORAGE_STATE_IDLE;
            s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
            prvPublishStatus();
            prvReportStatus();
            return true;

        case APP_STORAGE_PERSISTENT_STATE_FAILED:
            s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;
            prvPublishStatus();
            return true;

        default:
            return false;
    }
}

/* 设备启动时从W25Q64读取元数据A、B，恢复上一次断电前的下载/升级事务状态 */
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

    /* 选出最新有效记录。 */
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

static void prvInitializeDevice(void)
{
    BspW25Q64JedecId_t xJedecId = {0};

    s_xStorageStatus.xState = APP_STORAGE_STATE_INITIALIZING;
    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
    prvPublishStatus();

    if (BspW25Q64_ReadJedecId(&xJedecId) == false)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_SPI;
        prvPublishStatus();
        prvReportStatus();
        return;
    }

    s_xStorageStatus.ucManufacturer = xJedecId.ucManufacturer;
    s_xStorageStatus.ucMemoryType = xJedecId.ucMemoryType;
    s_xStorageStatus.ucCapacity = xJedecId.ucCapacity;

    if ((xJedecId.ucManufacturer != BSP_W25Q64_MANUFACTURER_ID) ||
        (xJedecId.ucMemoryType != BSP_W25Q64_MEMORY_TYPE) ||
        (xJedecId.ucCapacity != BSP_W25Q64_CAPACITY_ID))
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_JEDEC_ID;
        prvPublishStatus();
        prvReportStatus();
        return;
    }

    s_xStorageStatus.xState = APP_STORAGE_STATE_IDLE;
    /* 恢复Storage运行状态。 */
    if (prvLoadMetadata() == false)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_METADATA;
    }
    else if (s_xStorageStatus.xError >= APP_STORAGE_ERROR_COUNT)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_METADATA;
    }
    prvPublishStatus();
    prvReportStatus();
}

static void prvRejectRequest(AppStorageError_t xError)
{
    s_xStorageStatus.xError = xError;
    prvPublishStatus();
}

static void prvStartPrepare(const AppStorageRequest_t *pxRequest)
{
    uint32_t ulFirmwareLength;

    if ((pxRequest == NULL) ||
        (s_xOperation != APP_STORAGE_OPERATION_IDLE) ||
        ((s_xStorageStatus.xState != APP_STORAGE_STATE_IDLE) &&
         (s_xStorageStatus.xState != APP_STORAGE_STATE_FAILED) &&
         (s_xStorageStatus.xState != APP_STORAGE_STATE_READY_TO_UPGRADE)))
    {
        prvRejectRequest(APP_STORAGE_ERROR_STATE_CONFLICT);
        return;
    }

    if (pxRequest->ulTransactionId == 0U)
    {
        prvRejectRequest(APP_STORAGE_ERROR_TRANSACTION_MISMATCH);
        return;
    }

    ulFirmwareLength = pxRequest->xData.xPrepare.ulFirmwareLength;
    if ((ulFirmwareLength <= APP_STORAGE_FIRMWARE_HEADER_SIZE) ||
        (ulFirmwareLength > APP_STORAGE_FIRMWARE_MAX_SIZE) ||
        (ulFirmwareLength > APP_STORAGE_CANDIDATE_CAPACITY))
    {
        prvRejectRequest(APP_STORAGE_ERROR_FIRMWARE_LENGTH);
        return;
    }

    s_xStorageStatus.xState = APP_STORAGE_STATE_PREPARING;
    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
    s_xStorageStatus.ulTransactionId = pxRequest->ulTransactionId;
    s_xStorageStatus.ulFirmwareLength = ulFirmwareLength;
    s_xStorageStatus.ulErasedLength = 0U;
    s_xStorageStatus.ulWrittenLength = 0U;
    s_usCurrentBlockWritten = 0U;
    memcpy(s_aucExpectedFirmwareMd5,
           pxRequest->xData.xPrepare.aucExpectedFirmwareMd5,
           APP_STORAGE_MD5_DIGEST_SIZE);
    s_xAbortRequested = false;

    if (mbedtls_md5_starts(&s_xFirmwareMd5) != 0)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_FAILED;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_MD5_ENGINE;
        prvPublishStatus();
        return;
    }

    if (prvStartMetadataCommit(APP_STORAGE_PERSISTENT_STATE_PREPARING,
                               APP_STORAGE_ERROR_NONE) == false)
    {
        s_xStorageStatus.xState = APP_STORAGE_STATE_DEVICE_FAULT;
        s_xStorageStatus.xError = APP_STORAGE_ERROR_METADATA;
    }
    prvPublishStatus();
}

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

static bool prvWriteRequestIsValid(const AppStorageRequest_t *pxRequest,
                                   AppStorageError_t *pxError)
{
    uint32_t ulLength;

    if ((pxRequest == NULL) || (pxError == NULL))
    {
        return false;
    }

    if ((s_xOperation != APP_STORAGE_OPERATION_IDLE) ||
        (s_xStorageStatus.xState != APP_STORAGE_STATE_READY_FOR_DATA))
    {
        *pxError = APP_STORAGE_ERROR_STATE_CONFLICT;
        return false;
    }

    if (pxRequest->ulTransactionId != s_xStorageStatus.ulTransactionId)
    {
        *pxError = APP_STORAGE_ERROR_TRANSACTION_MISMATCH;
        return false;
    }

    ulLength = pxRequest->xData.xWrite.usLength;
    if ((ulLength == 0U) || (ulLength > APP_STORAGE_WRITE_BLOCK_SIZE))
    {
        *pxError = APP_STORAGE_ERROR_BLOCK_LENGTH;
        return false;
    }

    if ((s_xStorageStatus.ulWrittenLength > s_xStorageStatus.ulFirmwareLength) ||
        (ulLength > (s_xStorageStatus.ulFirmwareLength -
                     s_xStorageStatus.ulWrittenLength)))
    {
        *pxError = APP_STORAGE_ERROR_BLOCK_LENGTH;
        return false;
    }

    *pxError = APP_STORAGE_ERROR_NONE;
    return true;
}

static void prvStartWriteBlock(const AppStorageRequest_t *pxRequest)
{
    AppStorageError_t xError;

    if (prvWriteRequestIsValid(pxRequest, &xError) == false)
    {
        if ((xError == APP_STORAGE_ERROR_STATE_CONFLICT) ||
            (xError == APP_STORAGE_ERROR_TRANSACTION_MISMATCH))
        {
            prvRejectRequest(xError);
        }
        else
        {
            prvEnterTransactionFailure(xError);
        }
        return;
    }
    s_xCurrentRequest = *pxRequest;
    s_xStorageStatus.xState = APP_STORAGE_STATE_WRITING;
    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
    s_usCurrentBlockWritten = 0U;
    s_xOperation = APP_STORAGE_OPERATION_PAGE_PROGRAM_START;
    prvPublishStatus();
}

/* 计算下一页片段 */
static void prvGetCurrentPage(uint32_t *pulAddress, const uint8_t **ppucSource,
                              uint16_t *pusLength)
{
    uint16_t usRemaining =
        (uint16_t)(s_xCurrentRequest.xData.xWrite.usLength -
                   s_usCurrentBlockWritten);
    uint16_t usPageRemaining;

    *pulAddress = APP_STORAGE_CANDIDATE_ADDRESS +
        s_xStorageStatus.ulWrittenLength;
    *ppucSource =
        &s_xCurrentRequest.xData.xWrite.aucData[s_usCurrentBlockWritten];
    usPageRemaining = (uint16_t)(BSP_W25Q64_PAGE_SIZE_BYTES -
        (*pulAddress % BSP_W25Q64_PAGE_SIZE_BYTES));
    *pusLength = (usRemaining < usPageRemaining) ?
        usRemaining : usPageRemaining;
}

/* 分页编程状态机 */
static AppStorageStepResult_t prvServicePageProgram(TickType_t xNowTick,
                                                    AppStorageError_t *pxError)
{
    BspW25Q64OperationResult_t xOperationResult;
    BspW25Q64PollResult_t xPollResult;
    const uint8_t *pucSource;
    uint32_t ulPageAddress;
    uint16_t usPageLength;

    *pxError = APP_STORAGE_ERROR_NONE;
    if (s_xOperation == APP_STORAGE_OPERATION_PAGE_PROGRAM_START)
    {
        if (s_xAbortRequested)
        {
            s_xOperation = APP_STORAGE_OPERATION_IDLE;
            return APP_STORAGE_STEP_COMPLETE;
        }

        prvGetCurrentPage(&ulPageAddress, &pucSource, &usPageLength);
        xOperationResult = BspW25Q64_PageProgramStart(ulPageAddress, pucSource,
                                                     usPageLength);
        if (xOperationResult != BSP_W25Q64_OPERATION_STARTED)
        {
            *pxError = APP_STORAGE_ERROR_PROGRAM_START;
            return APP_STORAGE_STEP_ERROR;
        }

        s_xOperationStartTick = xNowTick;
        s_xOperation = APP_STORAGE_OPERATION_PAGE_PROGRAM_WAIT;
        return APP_STORAGE_STEP_BUSY;
    }

    if (s_xOperation == APP_STORAGE_OPERATION_PAGE_PROGRAM_WAIT)
    {
        xPollResult = BspW25Q64_PollReady();
        if (xPollResult == BSP_W25Q64_POLL_ERROR)
        {
            *pxError = APP_STORAGE_ERROR_SPI;
            return APP_STORAGE_STEP_ERROR;
        }
        if (xPollResult == BSP_W25Q64_POLL_BUSY)
        {
            if (prvHasExpired(s_xOperationStartTick, xNowTick,
                              APP_STORAGE_PAGE_PROGRAM_TIMEOUT_MS))
            {
                *pxError = APP_STORAGE_ERROR_PROGRAM_TIMEOUT;
                return APP_STORAGE_STEP_ERROR;
            }
            return APP_STORAGE_STEP_BUSY;
        }

        s_xOperation = APP_STORAGE_OPERATION_PAGE_PROGRAM_VERIFY;
        return APP_STORAGE_STEP_BUSY;
    }

    prvGetCurrentPage(&ulPageAddress, &pucSource, &usPageLength);
    if ((BspW25Q64_Read(ulPageAddress, s_aucReadback,
                        usPageLength) == false) ||
        (memcmp(s_aucReadback, pucSource, usPageLength) != 0))
    {
        *pxError = APP_STORAGE_ERROR_READBACK;
        return APP_STORAGE_STEP_ERROR;
    }

    if (mbedtls_md5_update(&s_xFirmwareMd5, s_aucReadback, usPageLength) != 0)
    {
        *pxError = APP_STORAGE_ERROR_MD5_ENGINE;
        return APP_STORAGE_STEP_ERROR;
    }

    /* 只有读回比较和MD5累计都成功后，才推进权威写入长度。 */
    s_xStorageStatus.ulWrittenLength += usPageLength;
    s_usCurrentBlockWritten += usPageLength;
    if (s_usCurrentBlockWritten < s_xCurrentRequest.xData.xWrite.usLength)
    {
        s_xOperation = APP_STORAGE_OPERATION_PAGE_PROGRAM_START;
        return APP_STORAGE_STEP_BUSY;
    }

    s_xOperation = APP_STORAGE_OPERATION_IDLE;
    s_usCurrentBlockWritten = 0U;
    return APP_STORAGE_STEP_COMPLETE;
}

static AppStorageError_t prvFinishVerification(void)
{
    uint8_t aucActualFirmwareMd5[APP_STORAGE_MD5_DIGEST_SIZE];
    AppFirmwareImageInfo_t xImageInfo;

    if (mbedtls_md5_finish(&s_xFirmwareMd5, aucActualFirmwareMd5) != 0)
    {
        return APP_STORAGE_ERROR_MD5_ENGINE;
    }

    if (memcmp(aucActualFirmwareMd5, s_aucExpectedFirmwareMd5,
               APP_STORAGE_MD5_DIGEST_SIZE) != 0)
    {
        return APP_STORAGE_ERROR_FIRMWARE_MD5;
    }

    if (BspW25Q64_Read(APP_STORAGE_CANDIDATE_ADDRESS, s_aucReadback,
                       APP_STORAGE_FIRMWARE_HEADER_SIZE) == false)
    {
        return APP_STORAGE_ERROR_READBACK;
    }
    if (AppFirmwareImage_ParseHeader(s_aucReadback,
                                     s_xStorageStatus.ulFirmwareLength,
                                     &xImageInfo) == false)
    {
        return APP_STORAGE_ERROR_FIRMWARE_HEADER;
    }

    return APP_STORAGE_ERROR_NONE;
}

static void prvStartFinish(const AppStorageRequest_t *pxRequest)
{
    if ((pxRequest == NULL) ||
        (s_xOperation != APP_STORAGE_OPERATION_IDLE) ||
        (s_xStorageStatus.xState != APP_STORAGE_STATE_READY_FOR_DATA))
    {
        prvRejectRequest(APP_STORAGE_ERROR_STATE_CONFLICT);
        return;
    }

    if (pxRequest->ulTransactionId != s_xStorageStatus.ulTransactionId)
    {
        prvRejectRequest(APP_STORAGE_ERROR_TRANSACTION_MISMATCH);
        return;
    }

    if (s_xStorageStatus.ulWrittenLength != s_xStorageStatus.ulFirmwareLength)
    {
        prvEnterTransactionFailure(APP_STORAGE_ERROR_FIRMWARE_LENGTH);
        return;
    }

    s_xStorageStatus.xState = APP_STORAGE_STATE_VERIFYING;
    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
    if (prvStartMetadataCommit(APP_STORAGE_PERSISTENT_STATE_VERIFYING,
                               APP_STORAGE_ERROR_NONE) == false)
    {
        prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
        return;
    }

    prvPublishStatus();
}

static void prvHandleAbort(const AppStorageRequest_t *pxRequest)
{
    if (pxRequest == NULL)
    {
        prvRejectRequest(APP_STORAGE_ERROR_STATE_CONFLICT);
        return;
    }

    if (pxRequest->ulTransactionId != s_xStorageStatus.ulTransactionId)
    {
        prvRejectRequest(APP_STORAGE_ERROR_TRANSACTION_MISMATCH);
        return;
    }

    /* 同一事务已经结束时，迟到或重复的ABORT不得覆盖最终结果及原始错误。 */
    if ((s_xStorageStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) ||
        (s_xStorageStatus.xState == APP_STORAGE_STATE_FAILED))
    {
        /* FAILED可能仍在提交失败元数据；只由提交完成路径发布稳定终态。 */
        return;
    }

    if ((s_xStorageStatus.xState != APP_STORAGE_STATE_PREPARING) &&
        (s_xStorageStatus.xState != APP_STORAGE_STATE_READY_FOR_DATA) &&
        (s_xStorageStatus.xState != APP_STORAGE_STATE_WRITING) &&
        (s_xStorageStatus.xState != APP_STORAGE_STATE_VERIFYING))
    {
        prvRejectRequest(APP_STORAGE_ERROR_STATE_CONFLICT);
        return;
    }

    if (s_xOperation == APP_STORAGE_OPERATION_IDLE)
    {
        prvEnterTransactionFailure(APP_STORAGE_ERROR_ABORTED);
    }
    else
    {
        /* 等待已经发给W25Q64的擦除或页编程命令完成。 */
        s_xAbortRequested = true;
    }
}

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


static void prvDispatchRequest(const AppStorageRequest_t *pxRequest)
{
    if (pxRequest == NULL)
    {
        return;
    }

    switch (pxRequest->xType)
    {
        case APP_STORAGE_REQUEST_QUERY_STATUS:
            prvPublishStatus();
            break;

        case APP_STORAGE_REQUEST_PREPARE_DOWNLOAD:
            prvStartPrepare(pxRequest);
            break;

        case APP_STORAGE_REQUEST_WRITE_BLOCK:
            prvStartWriteBlock(pxRequest);
            break;

        case APP_STORAGE_REQUEST_FINISH_DOWNLOAD:
            prvStartFinish(pxRequest);
            break;

        case APP_STORAGE_REQUEST_ABORT_DOWNLOAD:
            prvHandleAbort(pxRequest);
            break;

        case APP_STORAGE_REQUEST_START_UPGRADE:
        case APP_STORAGE_REQUEST_CANCEL_UPGRADE:
            prvHandleUpgradeRequest(pxRequest);
            break;

        default:
            prvRejectRequest(APP_STORAGE_ERROR_STATE_CONFLICT);
            break;
    }
}

/* Flash操作推进函数 */
static void prvServiceCurrentOperation(TickType_t xNowTick)
{
    AppStorageStepResult_t xStepResult;
    AppStorageError_t xError = APP_STORAGE_ERROR_NONE;

    switch (s_xOperation)
    {
        case APP_STORAGE_OPERATION_IDLE:
            return;

        case APP_STORAGE_OPERATION_METADATA_ERASE_START:
        case APP_STORAGE_OPERATION_METADATA_ERASE_WAIT:
        case APP_STORAGE_OPERATION_METADATA_BODY_START:
        case APP_STORAGE_OPERATION_METADATA_BODY_WAIT:
        case APP_STORAGE_OPERATION_METADATA_BODY_VERIFY:
        case APP_STORAGE_OPERATION_METADATA_COMMIT_START:
        case APP_STORAGE_OPERATION_METADATA_COMMIT_WAIT:
        case APP_STORAGE_OPERATION_METADATA_FINAL_VERIFY:
            xStepResult = prvServiceMetadataCommit(xNowTick);
            if (xStepResult == APP_STORAGE_STEP_ERROR)
            {
                prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
            }
            else if (xStepResult == APP_STORAGE_STEP_COMPLETE)
            {
                /* READY已经持久提交后，迟到的ABORT不得把有效候选固件改成失败。 */
                if (s_xActiveMetadata.xState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE)
                {
                    s_xAbortRequested = false;
                    if (prvContinueAfterMetadata() == false)
                    {
                        prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
                    }
                }
                else if (s_xAbortRequested)
                {
                    prvEnterTransactionFailure(APP_STORAGE_ERROR_ABORTED);
                }
                else if (prvContinueAfterMetadata() == false)
                {
                    prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
                }
            }
            return;

        case APP_STORAGE_OPERATION_CANDIDATE_ERASE_START:
        case APP_STORAGE_OPERATION_CANDIDATE_ERASE_WAIT:
            xStepResult = prvServiceCandidateErase(xNowTick, &xError);
            if (xStepResult == APP_STORAGE_STEP_ERROR)
            {
                prvEnterTransactionFailure(xError);
            }
            else if (xStepResult == APP_STORAGE_STEP_COMPLETE)
            {
                if (s_xAbortRequested)
                {
                    prvEnterTransactionFailure(APP_STORAGE_ERROR_ABORTED);
                }
                else if (prvStartMetadataCommit(
                             APP_STORAGE_PERSISTENT_STATE_DOWNLOADING,
                             APP_STORAGE_ERROR_NONE) == false)
                {
                    prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
                }
            }
            return;

        case APP_STORAGE_OPERATION_PAGE_PROGRAM_START:
        case APP_STORAGE_OPERATION_PAGE_PROGRAM_WAIT:
        case APP_STORAGE_OPERATION_PAGE_PROGRAM_VERIFY:
            xStepResult = prvServicePageProgram(xNowTick, &xError);
            if (xStepResult == APP_STORAGE_STEP_ERROR)
            {
                prvEnterTransactionFailure(xError);
            }
            else if (xStepResult == APP_STORAGE_STEP_COMPLETE)
            {
                if (s_xAbortRequested)
                {
                    prvEnterTransactionFailure(APP_STORAGE_ERROR_ABORTED);
                }
                else
                {
                    s_xStorageStatus.xState = APP_STORAGE_STATE_READY_FOR_DATA;
                    s_xStorageStatus.xError = APP_STORAGE_ERROR_NONE;
                    prvPublishStatus();
                }
            }
            return;

        case APP_STORAGE_OPERATION_FINAL_VERIFY:
            s_xOperation = APP_STORAGE_OPERATION_IDLE;
            if (s_xAbortRequested)
            {
                prvEnterTransactionFailure(APP_STORAGE_ERROR_ABORTED);
                return;
            }

            xError = prvFinishVerification();
            if (xError != APP_STORAGE_ERROR_NONE)
            {
                prvEnterTransactionFailure(xError);
            }
            else if (prvStartMetadataCommit(
                         APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE,
                         APP_STORAGE_ERROR_NONE) == false)
            {
                prvEnterTransactionFailure(APP_STORAGE_ERROR_METADATA);
            }
            return;

        default:
            prvEnterTransactionFailure(APP_STORAGE_ERROR_STATE_CONFLICT);
            return;
    }
}

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

bool AppStorageTask_Create(QueueHandle_t xRequestQueue, QueueHandle_t xStatusQueue)
{
    BaseType_t xResult;

    if ((xRequestQueue == NULL) ||
        (xStatusQueue == NULL) ||
        (s_xRequestQueue != NULL) ||
        (s_xStatusQueue != NULL))
    {
        return false;
    }

    s_xRequestQueue = xRequestQueue;
    s_xStatusQueue = xStatusQueue;

    xResult = xTaskCreate(prvStorageTask,
                          "STOR",
                          APP_STORAGE_TASK_STACK_WORDS,
                          NULL,
                          APP_STORAGE_TASK_PRIORITY,
                          NULL);

    if (xResult != pdPASS)
    {
        s_xRequestQueue = NULL;
        s_xStatusQueue = NULL;
        return false;
    }

    return true;
}
