#include "boot_controller.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "boot_config.h"
#include "boot_jump.h"
#include "bsp_internal_flash.h"
#include "bsp_w25q64.h"
#include "mbedtls/md5.h"
#include "stm32f10x.h"

/* 这些数值描述W25Q64中的持久记录，不是另一套运行状态。 */
typedef enum
{
    BOOT_PERSISTENT_EMPTY = 0U,
    BOOT_PERSISTENT_PREPARING = 1U,
    BOOT_PERSISTENT_DOWNLOADING = 2U,
    BOOT_PERSISTENT_VERIFYING = 3U,
    BOOT_PERSISTENT_READY_TO_UPGRADE = 4U,
    BOOT_PERSISTENT_UPGRADE_REQUESTED = 5U,
    BOOT_PERSISTENT_INSTALLING = 6U,
    BOOT_PERSISTENT_BOOT_PENDING = 7U,  /* 等待启动确认 */
    BOOT_PERSISTENT_CONFIRMED = 8U,
    BOOT_PERSISTENT_FAILED = 9U
} BootPersistentState_t;

typedef enum
{
    BOOT_ERROR_CANDIDATE_REJECTED_BEFORE_INSTALL = 0x1001U,
    BOOT_ERROR_CANDIDATE_LOST_DURING_INSTALL = 0x1002U,
    BOOT_ERROR_METADATA_COMMIT = 0x1003U,
    BOOT_ERROR_FLASH_ERASE = 0x1004U,
    BOOT_ERROR_FLASH_PROGRAM = 0x1005U,
    BOOT_ERROR_FLASH_READBACK = 0x1006U,
    BOOT_ERROR_INTERNAL_MD5 = 0x1007U,
    BOOT_ERROR_INSTALL_ATTEMPTS_EXHAUSTED = 0x1008U,
    BOOT_ERROR_BOOT_ATTEMPTS_EXHAUSTED = 0x1009U,

    /* 仅供Boot RAM诊断；不提交到元数据或作为MQTT结果值。 */
    BOOT_FAULT_STORAGE_UNAVAILABLE = 0x2001U,
    BOOT_FAULT_METADATA_UNAVAILABLE = 0x2002U
} BootError_t;

typedef struct
{
    uint32_t ulGeneration;
    BootPersistentState_t xState;
    uint32_t ulLastError;
    uint32_t ulFirmwareLength;
    uint8_t aucFirmwareMd5[16];
    uint32_t ulInstallAttempts;
    uint32_t ulBootAttempts;
} BootMetadataRecord_t;

typedef struct
{
    BootMetadataRecord_t xRecord;
    uint32_t ulActiveAddress; /* UINT32_MAX表示A/B均无有效记录。 */
    uint8_t aucBuffer[BOOT_IMAGE_HEADER_SIZE];
} BootContext_t;    /* 元数据上下文 */

_Static_assert(BOOT_METADATA_RECORD_SIZE == 256U, "Metadata v3 requires 256 bytes");
_Static_assert(BOOT_IMAGE_HEADER_SIZE == 512U, "Image header requires 512 bytes");
_Static_assert(sizeof(BOOT_HARDWARE_MODEL) <= 16U, "Hardware model must include its NUL");

static const uint8_t s_aucMetadataMagic[8] = {'S', 'T', 'G', 'W', 'M', 'D', 0U, 0U};
static const uint8_t s_aucCommitMarker[4] = {'C', 'M', 'I', 'T'};
static volatile uint32_t s_ulMilliseconds = 0U;
static volatile uint32_t s_ulBootFaultCode = 0U;

/* 只有SysTick ISR写入；忙轮询只读取，不在ISR中操作Flash或编排升级。 */
void SysTick_Handler(void)
{
    s_ulMilliseconds++;
}

static uint32_t prvReadLe32(const uint8_t *pucData)
{
    return (uint32_t)pucData[0] | ((uint32_t)pucData[1] << 8U) |
        ((uint32_t)pucData[2] << 16U) | ((uint32_t)pucData[3] << 24U);
}

static void prvWriteLe32(uint8_t *pucData, uint32_t ulValue)
{
    pucData[0] = (uint8_t)ulValue;
    pucData[1] = (uint8_t)(ulValue >> 8U);
    pucData[2] = (uint8_t)(ulValue >> 16U);
    pucData[3] = (uint8_t)(ulValue >> 24U);
}

static uint32_t prvCalculateCrc32(const uint8_t *pucData, uint32_t ulLength)
{
    uint32_t ulCrc = UINT32_MAX;
    uint32_t ulByte;
    uint32_t ulBit;

    /* 与Application字节协议一致：反射CRC32，初值/最终异或均为FFFFFFFF。 */
    for (ulByte = 0U; ulByte < ulLength; ulByte++)
    {
        ulCrc ^= pucData[ulByte];
        for (ulBit = 0U; ulBit < 8U; ulBit++)
        {
            ulCrc = ((ulCrc & 1U) != 0U) ?
                ((ulCrc >> 1U) ^ 0xEDB88320UL) : (ulCrc >> 1U);
        }
    }
    return ulCrc ^ UINT32_MAX;
}

static bool prvMetadataSemanticsValid(const BootMetadataRecord_t *pxRecord)
{
    if ((uint32_t)pxRecord->xState > (uint32_t)BOOT_PERSISTENT_FAILED)
    {
        return false;
    }
    if (pxRecord->xState == BOOT_PERSISTENT_EMPTY)
    {
        return pxRecord->ulFirmwareLength == 0U;
    }
    return (pxRecord->ulFirmwareLength > BOOT_IMAGE_HEADER_SIZE) &&
        (pxRecord->ulFirmwareLength <= BOOT_APPLICATION_SIZE) &&
        (pxRecord->ulFirmwareLength <= BOOT_CANDIDATE_CAPACITY);
}

static bool prvDecodeMetadata(const uint8_t *pucRaw, BootMetadataRecord_t *pxRecord)
{
    uint32_t ulIndex;

    if ((memcmp(pucRaw, s_aucMetadataMagic, sizeof(s_aucMetadataMagic)) != 0) ||
        (pucRaw[0x08U] != 3U) || (pucRaw[0x09U] != 0U) ||
        (pucRaw[0x0AU] != 0U) || (pucRaw[0x0BU] != 1U) ||
        (memcmp(&pucRaw[0xFCU], s_aucCommitMarker, sizeof(s_aucCommitMarker)) != 0) ||
        (prvReadLe32(&pucRaw[0xF8U]) != prvCalculateCrc32(pucRaw, 0xF8U)))
    {
        return false;
    }
    for (ulIndex = 0x34U; ulIndex < 0xF8U; ulIndex++)
    {
        if (pucRaw[ulIndex] != 0U)
        {
            return false;
        }
    }

    pxRecord->ulGeneration = prvReadLe32(&pucRaw[0x0CU]);
    pxRecord->xState = (BootPersistentState_t)prvReadLe32(&pucRaw[0x10U]);
    pxRecord->ulLastError = prvReadLe32(&pucRaw[0x14U]);
    pxRecord->ulFirmwareLength = prvReadLe32(&pucRaw[0x18U]);
    memcpy(pxRecord->aucFirmwareMd5, &pucRaw[0x1CU], sizeof(pxRecord->aucFirmwareMd5));
    pxRecord->ulInstallAttempts = prvReadLe32(&pucRaw[0x2CU]);
    pxRecord->ulBootAttempts = prvReadLe32(&pucRaw[0x30U]);
    return prvMetadataSemanticsValid(pxRecord);
}

static bool prvEncodeMetadata(const BootMetadataRecord_t *pxRecord, uint8_t *pucRaw)
{
    if (!prvMetadataSemanticsValid(pxRecord))
    {
        return false;
    }

    memset(pucRaw, 0, BOOT_METADATA_RECORD_SIZE);
    memcpy(pucRaw, s_aucMetadataMagic, sizeof(s_aucMetadataMagic));
    pucRaw[0x08U] = 3U;
    pucRaw[0x0BU] = 1U; /* 小端记录长度0100，即256字节。 */
    prvWriteLe32(&pucRaw[0x0CU], pxRecord->ulGeneration);
    prvWriteLe32(&pucRaw[0x10U], (uint32_t)pxRecord->xState);
    prvWriteLe32(&pucRaw[0x14U], pxRecord->ulLastError);
    prvWriteLe32(&pucRaw[0x18U], pxRecord->ulFirmwareLength);
    memcpy(&pucRaw[0x1CU], pxRecord->aucFirmwareMd5, sizeof(pxRecord->aucFirmwareMd5));
    prvWriteLe32(&pucRaw[0x2CU], pxRecord->ulInstallAttempts);
    prvWriteLe32(&pucRaw[0x30U], pxRecord->ulBootAttempts);
    prvWriteLe32(&pucRaw[0xF8U], prvCalculateCrc32(pucRaw, 0xF8U));
    memcpy(&pucRaw[0xFCU], s_aucCommitMarker, sizeof(s_aucCommitMarker));
    return true;
}

static bool prvWaitReady(uint32_t ulTimeoutMs)
{
    uint32_t ulStart = s_ulMilliseconds;
    BspW25Q64PollResult_t xResult;

    for (;;)
    {
        xResult = BspW25Q64_PollReady();
        if (xResult == BSP_W25Q64_POLL_READY)
        {
            return true;
        }
        if ((xResult == BSP_W25Q64_POLL_ERROR) ||
            ((uint32_t)(s_ulMilliseconds - ulStart) >= ulTimeoutMs))
        {
            return false;
        }
    }
}

static bool prvLoadMetadata(BootContext_t *pxContext)
{
    BootMetadataRecord_t xRecordA;
    BootMetadataRecord_t xRecordB;
    uint8_t *pucRawA = pxContext->aucBuffer;
    uint8_t *pucRawB = &pxContext->aucBuffer[BOOT_METADATA_RECORD_SIZE];
    bool xValidA;
    bool xValidB;

    /* 通信失败不是“无记录”，必须停留；只有读到但格式无效才参与EMPTY判定。 */
    if (!BspW25Q64_Read(BOOT_METADATA_A_ADDRESS, pucRawA, BOOT_METADATA_RECORD_SIZE) ||
        !BspW25Q64_Read(BOOT_METADATA_B_ADDRESS, pucRawB, BOOT_METADATA_RECORD_SIZE))
    {
        return false;
    }
    xValidA = prvDecodeMetadata(pucRawA, &xRecordA);
    xValidB = prvDecodeMetadata(pucRawB, &xRecordB);

    if (!xValidA && !xValidB)
    {
        memset(&pxContext->xRecord, 0, sizeof(pxContext->xRecord));
        pxContext->ulActiveAddress = UINT32_MAX;
        return true;
    }
    if (xValidA && xValidB && (xRecordA.ulGeneration == xRecordB.ulGeneration))
    {
        if (memcmp(pucRawA, pucRawB, BOOT_METADATA_RECORD_SIZE) != 0)
        {
            return false;
        }
        pxContext->xRecord = xRecordA;
        pxContext->ulActiveAddress = BOOT_METADATA_A_ADDRESS;
    }
    else if (xValidA && (!xValidB ||
        ((int32_t)(xRecordA.ulGeneration - xRecordB.ulGeneration) > 0)))
    {
        pxContext->xRecord = xRecordA;
        pxContext->ulActiveAddress = BOOT_METADATA_A_ADDRESS;
    }
    else
    {
        pxContext->xRecord = xRecordB;
        pxContext->ulActiveAddress = BOOT_METADATA_B_ADDRESS;
    }
    return true;
}

static bool prvCommitMetadata(BootContext_t *pxContext, BootPersistentState_t xNewState,
    uint32_t ulError, uint32_t ulInstallAttempts, uint32_t ulBootAttempts)
{
    BootMetadataRecord_t xNewRecord = pxContext->xRecord;
    uint8_t *pucEncoded = pxContext->aucBuffer;
    uint8_t *pucReadback = &pxContext->aucBuffer[BOOT_METADATA_RECORD_SIZE];
    uint32_t ulTargetAddress;

    /* 本模块只从已选中的有效事务提交状态，不凭空创造升级请求。 */
    if (pxContext->ulActiveAddress == BOOT_METADATA_A_ADDRESS)
    {
        ulTargetAddress = BOOT_METADATA_B_ADDRESS;
    }
    else if (pxContext->ulActiveAddress == BOOT_METADATA_B_ADDRESS)
    {
        ulTargetAddress = BOOT_METADATA_A_ADDRESS;
    }
    else
    {
        return false;
    }
    xNewRecord.ulGeneration++;
    xNewRecord.xState = xNewState;
    xNewRecord.ulLastError = ulError;
    xNewRecord.ulInstallAttempts = ulInstallAttempts;
    xNewRecord.ulBootAttempts = ulBootAttempts;

    if (!prvEncodeMetadata(&xNewRecord, pucEncoded) ||
        !prvWaitReady(BOOT_SECTOR_ERASE_TIMEOUT_MS) ||
        (BspW25Q64_SectorEraseStart(ulTargetAddress) != BSP_W25Q64_OPERATION_STARTED) ||
        !prvWaitReady(BOOT_SECTOR_ERASE_TIMEOUT_MS) ||
        (BspW25Q64_PageProgramStart(ulTargetAddress, pucEncoded, 252U) !=
            BSP_W25Q64_OPERATION_STARTED) ||
        !prvWaitReady(BOOT_PAGE_PROGRAM_TIMEOUT_MS) ||
        !BspW25Q64_Read(ulTargetAddress, pucReadback, 252U) ||
        (memcmp(pucEncoded, pucReadback, 252U) != 0))
    {
        return false;
    }

    /* 主体和CRC读回一致后，最后单独编程CMIT；旧活动扇区始终不擦。 */
    if ((BspW25Q64_PageProgramStart(ulTargetAddress + 0xFCU, &pucEncoded[0xFCU], 4U) !=
            BSP_W25Q64_OPERATION_STARTED) ||
        !prvWaitReady(BOOT_PAGE_PROGRAM_TIMEOUT_MS) ||
        !BspW25Q64_Read(ulTargetAddress, pucReadback, BOOT_METADATA_RECORD_SIZE) ||
        (memcmp(pucEncoded, pucReadback, BOOT_METADATA_RECORD_SIZE) != 0) ||
        !prvDecodeMetadata(pucReadback, &xNewRecord))
    {
        return false;
    }
    pxContext->xRecord = xNewRecord;
    pxContext->ulActiveAddress = ulTargetAddress;
    return true;
}

static bool prvModelFieldValid(const uint8_t *pucField)
{
    uint32_t ulIndex = 0U;

    if (pucField[0] == 0U)
    {
        return false;
    }
    while ((ulIndex < 16U) && (pucField[ulIndex] != 0U))
    {
        if ((pucField[ulIndex] < 0x21U) || (pucField[ulIndex] > 0x7EU))
        {
            return false;
        }
        ulIndex++;
    }
    if (ulIndex == 16U)
    {
        return false;
    }
    for (; ulIndex < 16U; ulIndex++)
    {
        if (pucField[ulIndex] != 0U)
        {
            return false;
        }
    }
    return true;
}

static bool prvParseImageHeader(const uint8_t *pucHeader, uint32_t *pulFileLength)
{
    uint32_t ulIndex;
    uint32_t ulLength = prvReadLe32(&pucHeader[0x20U]);

    if (!prvModelFieldValid(pucHeader) || !prvModelFieldValid(&pucHeader[0x10U]) ||
        (memcmp(pucHeader, BOOT_HARDWARE_MODEL, sizeof(BOOT_HARDWARE_MODEL)) != 0) ||
        (ulLength <= BOOT_IMAGE_HEADER_SIZE) || (ulLength > BOOT_APPLICATION_SIZE) ||
        (ulLength > BOOT_CANDIDATE_CAPACITY))
    {
        return false;
    }
    for (ulIndex = 0x24U; ulIndex < BOOT_IMAGE_HEADER_SIZE; ulIndex++)
    {
        if (pucHeader[ulIndex] != 0U)
        {
            return false;
        }
    }
    *pulFileLength = ulLength;
    return true;
}

static bool prvVectorsValid(const uint8_t *pucVectors, uint32_t ulFileLength)
{
    uint32_t ulStackPointer;
    uint32_t ulResetHandler;

    /* 头之后至少要包含MSP/Reset两个字；不能把文件外的旧Flash当作向量。 */
    if (ulFileLength < (BOOT_IMAGE_HEADER_SIZE + 8U))
    {
        return false;
    }
    ulStackPointer = prvReadLe32(pucVectors);
    ulResetHandler = prvReadLe32(&pucVectors[4]);
    return (ulStackPointer > 0x20000000UL) && (ulStackPointer <= 0x20010000UL) &&
        ((ulStackPointer & 7U) == 0U) && ((ulResetHandler & 1U) != 0U) &&
        ((ulResetHandler & ~1UL) >= (BOOT_APPLICATION_START + BOOT_IMAGE_HEADER_SIZE)) &&
        ((ulResetHandler & ~1UL) < (BOOT_APPLICATION_START + ulFileLength));
}

static bool prvCheckImageMd5(BootContext_t *pxContext, uint32_t ulLength, bool xExternal)
{
    mbedtls_md5_context xMd5;
    uint8_t aucDigest[16];      /* 最终的 MD5 摘要 */
    uint32_t ulOffset = 0U;
    uint16_t usLength;
    const uint8_t *pucData;
    bool xOk;

    /* 同一份完整文件算法；外部源读入共享块，内部源直接读取内存映射Flash。 */
    mbedtls_md5_init(&xMd5);
    xOk = (mbedtls_md5_starts(&xMd5) == 0);
    while (xOk && (ulOffset < ulLength))
    {
        usLength = (uint16_t)(((ulLength - ulOffset) > sizeof(pxContext->aucBuffer)) ?
            sizeof(pxContext->aucBuffer) : (ulLength - ulOffset));
        if (xExternal)
        {
            xOk = BspW25Q64_Read(BOOT_CANDIDATE_ADDRESS + ulOffset,
                pxContext->aucBuffer, usLength);
            pucData = pxContext->aucBuffer;
        }
        else
        {
            pucData = (const uint8_t *)(uintptr_t)(BOOT_APPLICATION_START + ulOffset);
        }
        if (xOk)
        {
            xOk = (mbedtls_md5_update(&xMd5, pucData, usLength) == 0);
        }
        ulOffset += usLength;
    }
    if (xOk)
    {
        xOk = (mbedtls_md5_finish(&xMd5, aucDigest) == 0);
    }
    mbedtls_md5_free(&xMd5);
    return xOk && (memcmp(aucDigest, pxContext->xRecord.aucFirmwareMd5, 16U) == 0);
}

static bool prvValidateCandidate(BootContext_t *pxContext)
{
    uint32_t ulLength;

    if (!BspW25Q64_Read(BOOT_CANDIDATE_ADDRESS, pxContext->aucBuffer, BOOT_IMAGE_HEADER_SIZE) ||
        !prvParseImageHeader(pxContext->aucBuffer, &ulLength) ||
        (ulLength != pxContext->xRecord.ulFirmwareLength) ||
        (ulLength < (BOOT_IMAGE_HEADER_SIZE + 8U)))
    {
        return false;
    }
    if (!BspW25Q64_Read(BOOT_CANDIDATE_ADDRESS + BOOT_IMAGE_HEADER_SIZE,
            pxContext->aucBuffer, 8U) ||
        !prvVectorsValid(pxContext->aucBuffer, ulLength))
    {
        return false;
    }
    return prvCheckImageMd5(pxContext, ulLength, true);
}

static uint32_t prvValidateInstalled(BootContext_t *pxContext, bool xCheckPendingMd5)
{
    const uint8_t *pucHeader = (const uint8_t *)(uintptr_t)BOOT_APPLICATION_START;
    uint32_t ulLength;

    if (!prvParseImageHeader(pucHeader, &ulLength) ||
        !prvVectorsValid(&pucHeader[BOOT_IMAGE_HEADER_SIZE], ulLength))
    {
        return BOOT_ERROR_FLASH_READBACK;
    }
    if (xCheckPendingMd5)
    {
        if (ulLength != pxContext->xRecord.ulFirmwareLength)
        {
            return BOOT_ERROR_FLASH_READBACK;
        }
        if (!prvCheckImageMd5(pxContext, ulLength, false))
        {
            return BOOT_ERROR_INTERNAL_MD5;
        }
    }
    return 0U;
}

static uint32_t prvInstallCandidate(BootContext_t *pxContext)
{
    uint32_t ulError = 0U;
    uint32_t ulOffset = 0U;
    uint32_t ulLength = pxContext->xRecord.ulFirmwareLength;
    uint16_t usLength;

    if (!BspInternalFlash_Unlock() || !BspInternalFlash_EraseApplication())
    {
        ulError = BOOT_ERROR_FLASH_ERASE;
    }
    else
    {
        while (ulOffset < ulLength)
        {
            usLength = (uint16_t)(((ulLength - ulOffset) > sizeof(pxContext->aucBuffer)) ?
                sizeof(pxContext->aucBuffer) : (ulLength - ulOffset));
            if (!BspW25Q64_Read(BOOT_CANDIDATE_ADDRESS + ulOffset,
                    pxContext->aucBuffer, usLength))
            {
                ulError = BOOT_ERROR_CANDIDATE_LOST_DURING_INSTALL;
                break;
            }
            if (!BspInternalFlash_ProgramBlock(ulOffset, pxContext->aucBuffer, usLength))
            {
                ulError = BOOT_ERROR_FLASH_PROGRAM;
                break;
            }
            if (!BspInternalFlash_Compare(ulOffset, pxContext->aucBuffer, usLength))
            {
                ulError = BOOT_ERROR_FLASH_READBACK;
                break;
            }
            ulOffset += usLength;
        }
    }
    /* 无论解锁/擦除/编程在哪一步失败，都从同一个出口锁回内部Flash。 */
    BspInternalFlash_Lock();
    if (ulError == 0U)
    {
        ulError = prvValidateInstalled(pxContext, true);
    }
    return ulError;
}

__attribute__((noreturn)) static void prvFaultStop(uint32_t ulError)
{
    s_ulBootFaultCode = ulError; /* 用GDB观察；不假称已提交到W25Q64。 */
    BspInternalFlash_Lock();
    __disable_irq();
    SysTick->CTRL = 0U;
    for (;;)
    {
        __NOP();
    }
}

__attribute__((noreturn)) static void prvJumpApplication(void)
{
    const uint8_t *pucVectors = (const uint8_t *)(uintptr_t)
        (BOOT_APPLICATION_START + BOOT_IMAGE_HEADER_SIZE);
    uint32_t ulStackPointer = prvReadLe32(pucVectors);
    uint32_t ulResetHandler = prvReadLe32(&pucVectors[4]);
    uint32_t ulIndex;

    /* 调用者已校验内部镜像；这里仅在特权Thread/MSP上下文做CPU交接。 */
    /* 关闭 CPU 的中断“总开关” */  /* 不会自动清除已经挂起的中断请求，也不能屏蔽 NMI 和 HardFault，所以后面仍要清理挂起状态 */
    __disable_irq();        
    /* 停止计数，并关闭 SysTick 中断 */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (ulIndex = 0U; ulIndex < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0])); ulIndex++)
    {
        NVIC->ICER[ulIndex] = UINT32_MAX;
        NVIC->ICPR[ulIndex] = UINT32_MAX;
    }
    BspW25Q64_InterfaceDeInit();
    SCB->VTOR = BOOT_APPLICATION_START + BOOT_IMAGE_HEADER_SIZE;
    __DSB();
    __ISB();
    /* PRIMASK保持1；应用main重设VTOR后再开中断，切MSP后不再执行C语句。 */
    BootJump_Transfer(ulStackPointer, ulResetHandler);
}

void BootController_Run(void)
{
    BootContext_t xContext = {0};
    BspW25Q64JedecId_t xJedecId;
    uint32_t ulError;

    if (!prvWaitReady(BOOT_SECTOR_ERASE_TIMEOUT_MS) ||
        !BspW25Q64_ReadJedecId(&xJedecId) || (xJedecId.ucManufacturer != 0xEFU) ||
        (xJedecId.ucMemoryType != 0x40U) || (xJedecId.ucCapacity != 0x17U))
    {
        prvFaultStop(BOOT_FAULT_STORAGE_UNAVAILABLE);
    }
    if (!prvLoadMetadata(&xContext))
    {
        prvFaultStop(BOOT_FAULT_METADATA_UNAVAILABLE);
    }

    switch (xContext.xRecord.xState)
    {
        case BOOT_PERSISTENT_UPGRADE_REQUESTED:
        case BOOT_PERSISTENT_INSTALLING:
            if (!prvValidateCandidate(&xContext))
            {
                /* 只有从未进入安装事务的首次拒绝，才能考虑启动仍完整的旧应用。 */
                ulError = ((xContext.xRecord.xState == BOOT_PERSISTENT_UPGRADE_REQUESTED) &&
                    (xContext.xRecord.ulInstallAttempts == 0U)) ?
                    BOOT_ERROR_CANDIDATE_REJECTED_BEFORE_INSTALL :
                    BOOT_ERROR_CANDIDATE_LOST_DURING_INSTALL;
                if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_FAILED, ulError,
                        xContext.xRecord.ulInstallAttempts, xContext.xRecord.ulBootAttempts))
                {
                    prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
                }
                if ((ulError == BOOT_ERROR_CANDIDATE_REJECTED_BEFORE_INSTALL) &&
                    (prvValidateInstalled(&xContext, false) == 0U))
                {
                    prvJumpApplication();
                }
                prvFaultStop(ulError);
            }
            if (xContext.xRecord.ulInstallAttempts >= BOOT_INSTALL_ATTEMPT_LIMIT)
            {
                if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_FAILED,
                        BOOT_ERROR_INSTALL_ATTEMPTS_EXHAUSTED,
                        xContext.xRecord.ulInstallAttempts, xContext.xRecord.ulBootAttempts))
                {
                    prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
                }
                prvFaultStop(BOOT_ERROR_INSTALL_ATTEMPTS_EXHAUSTED);
            }

            /* 先持久记账，提交成功之前绝不擦内部应用区。 */
            if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_INSTALLING, 0U,
                    xContext.xRecord.ulInstallAttempts + 1U, 0U))
            {
                prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
            }
            ulError = prvInstallCandidate(&xContext);
            if (ulError != 0U)
            {
                if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_FAILED, ulError,
                        xContext.xRecord.ulInstallAttempts, xContext.xRecord.ulBootAttempts))
                {
                    prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
                }
                prvFaultStop(ulError);
            }
            /* 内部镜像已在 prvInstallCandidate 中读回并通过完整 MD5 校验。
             * 一次提交 BOOT_PENDING 和首次启动次数；成功后直接跳转，不再额外复位。
             * 提交后若掉电，本次会保守占用一次尝试，但绝不出现先启动、后记账。 */
            if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_BOOT_PENDING, 0U,
                    xContext.xRecord.ulInstallAttempts, 1U))
            {
                prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
            }
            prvJumpApplication();

        case BOOT_PERSISTENT_BOOT_PENDING:
            ulError = prvValidateInstalled(&xContext, true);
            if (ulError == 0U &&
                (xContext.xRecord.ulBootAttempts >= BOOT_START_ATTEMPT_LIMIT))
            {
                ulError = BOOT_ERROR_BOOT_ATTEMPTS_EXHAUSTED;
            }
            if (ulError != 0U)
            {
                if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_FAILED, ulError,
                        xContext.xRecord.ulInstallAttempts, xContext.xRecord.ulBootAttempts))
                {
                    prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
                }
                prvFaultStop(ulError);
            }
            if (!prvCommitMetadata(&xContext, BOOT_PERSISTENT_BOOT_PENDING, 0U,
                    xContext.xRecord.ulInstallAttempts, xContext.xRecord.ulBootAttempts + 1U))
            {
                prvFaultStop(BOOT_ERROR_METADATA_COMMIT);
            }
            prvJumpApplication();

        case BOOT_PERSISTENT_FAILED:
            /* P3失败或首次拒绝候选可保留旧应用；安装过/未知P4失败一律停留。 */
            if ((xContext.xRecord.ulInstallAttempts != 0U) ||
                ((xContext.xRecord.ulLastError >= 0x1000U) &&
                 (xContext.xRecord.ulLastError != BOOT_ERROR_CANDIDATE_REJECTED_BEFORE_INSTALL)))
            {
                prvFaultStop(xContext.xRecord.ulLastError);
            }
            break;

        default:
            /* EMPTY/CONFIRMED/READY及未完成下载：没有升级请求，不安装候选。 */
            break;
    }

    ulError = prvValidateInstalled(&xContext, false);
    if (ulError != 0U)
    {
        prvFaultStop(ulError);
    }
    prvJumpApplication();
}
