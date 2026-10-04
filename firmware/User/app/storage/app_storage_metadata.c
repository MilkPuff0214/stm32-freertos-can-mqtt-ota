#include "app_storage_metadata.h"

#include <stddef.h>
#include <string.h>

#include "app_storage_crc32.h"
#include "app_storage_layout.h"

#define APP_STORAGE_METADATA_FORMAT_VERSION        3U
#define APP_STORAGE_METADATA_MAGIC_OFFSET          0x000U
#define APP_STORAGE_METADATA_VERSION_OFFSET        0x008U
#define APP_STORAGE_METADATA_LENGTH_OFFSET         0x00AU
#define APP_STORAGE_METADATA_GENERATION_OFFSET     0x00CU
#define APP_STORAGE_METADATA_STATE_OFFSET          0x010U
#define APP_STORAGE_METADATA_ERROR_OFFSET          0x014U
#define APP_STORAGE_METADATA_FIRMWARE_LENGTH_OFFSET 0x018U
#define APP_STORAGE_METADATA_FIRMWARE_MD5_OFFSET   0x01CU
#define APP_STORAGE_METADATA_INSTALL_COUNT_OFFSET  0x02CU
#define APP_STORAGE_METADATA_BOOT_COUNT_OFFSET     0x030U
#define APP_STORAGE_METADATA_RESERVED_OFFSET       0x034U
#define APP_STORAGE_METADATA_RESERVED_LENGTH       196U
#define APP_STORAGE_METADATA_CRC_OFFSET            0x0F8U
#define APP_STORAGE_METADATA_COMMIT_OFFSET         0x0FCU

static const uint8_t s_aucMetadataMagic[8] =
{
    'S', 'T', 'G', 'W', 'M', 'D', '\0', '\0'
};

static const uint8_t s_aucCommitMarker[APP_STORAGE_METADATA_COMMIT_SIZE] =
{
    'C', 'M', 'I', 'T'
};

static uint16_t prvReadLe16(const uint8_t *pucData)
{
    return (uint16_t)pucData[0] |
           ((uint16_t)pucData[1] << 8U);
}

static uint32_t prvReadLe32(const uint8_t *pucData)
{
    return (uint32_t)pucData[0] |
           ((uint32_t)pucData[1] << 8U) |
           ((uint32_t)pucData[2] << 16U) |
           ((uint32_t)pucData[3] << 24U);
}

static void prvWriteLe16(uint8_t *pucData, uint16_t usValue)
{
    pucData[0] = (uint8_t)usValue;
    pucData[1] = (uint8_t)(usValue >> 8U);
}

static void prvWriteLe32(uint8_t *pucData, uint32_t ulValue)
{
    pucData[0] = (uint8_t)ulValue;
    pucData[1] = (uint8_t)(ulValue >> 8U);
    pucData[2] = (uint8_t)(ulValue >> 16U);
    pucData[3] = (uint8_t)(ulValue >> 24U);
}

static bool prvPersistentStateIsValid(AppStoragePersistentState_t xState)
{
    return xState <= APP_STORAGE_PERSISTENT_STATE_FAILED;
}

static bool prvBytesAreZero(const uint8_t *pucData, uint32_t ulLength)
{
    uint32_t ulIndex;

    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        if (pucData[ulIndex] != 0U)
        {
            return false;
        }
    }

    return true;
}

static bool prvRecordSemanticsAreValid(const AppStorageMetadataRecord_t *pxRecord)
{
    if ((pxRecord == NULL) ||
        (prvPersistentStateIsValid(pxRecord->xState) == false))
    {
        return false;
    }

    if (pxRecord->xState == APP_STORAGE_PERSISTENT_STATE_EMPTY)
    {
        return pxRecord->ulFirmwareLength == 0U;
    }

    return (pxRecord->ulFirmwareLength > APP_STORAGE_FIRMWARE_HEADER_SIZE) &&
           (pxRecord->ulFirmwareLength <= APP_STORAGE_FIRMWARE_MAX_SIZE) &&
           (pxRecord->ulFirmwareLength <= APP_STORAGE_CANDIDATE_CAPACITY);
}

bool AppStorageMetadata_Encode(const AppStorageMetadataRecord_t *pxRecord,
                               uint8_t pucBuffer[APP_STORAGE_METADATA_RECORD_SIZE])
{
    uint32_t ulCrc;

    if ((pucBuffer == NULL) || (prvRecordSemanticsAreValid(pxRecord) == false))
    {
        return false;
    }

    memset(pucBuffer, 0, APP_STORAGE_METADATA_RECORD_SIZE);
    memcpy(&pucBuffer[APP_STORAGE_METADATA_MAGIC_OFFSET], s_aucMetadataMagic,
           sizeof(s_aucMetadataMagic));
    prvWriteLe16(&pucBuffer[APP_STORAGE_METADATA_VERSION_OFFSET],
                 APP_STORAGE_METADATA_FORMAT_VERSION);
    prvWriteLe16(&pucBuffer[APP_STORAGE_METADATA_LENGTH_OFFSET],
                 APP_STORAGE_METADATA_RECORD_SIZE);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_GENERATION_OFFSET],
                 pxRecord->ulGeneration);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_STATE_OFFSET],
                 (uint32_t)pxRecord->xState);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_ERROR_OFFSET], pxRecord->ulLastError);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_FIRMWARE_LENGTH_OFFSET],
                 pxRecord->ulFirmwareLength);

    memcpy(&pucBuffer[APP_STORAGE_METADATA_FIRMWARE_MD5_OFFSET],
           pxRecord->aucFirmwareMd5, APP_STORAGE_MD5_DIGEST_SIZE);

    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_INSTALL_COUNT_OFFSET],
                 pxRecord->ulInstallAttempts);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_BOOT_COUNT_OFFSET],
                 pxRecord->ulBootAttempts);

    ulCrc = AppStorageCrc32_Calculate(pucBuffer, APP_STORAGE_METADATA_CRC_OFFSET);
    prvWriteLe32(&pucBuffer[APP_STORAGE_METADATA_CRC_OFFSET], ulCrc);
    memcpy(&pucBuffer[APP_STORAGE_METADATA_COMMIT_OFFSET], s_aucCommitMarker,
           sizeof(s_aucCommitMarker));
    return true;
}

bool AppStorageMetadata_Decode(const uint8_t pucBuffer[APP_STORAGE_METADATA_RECORD_SIZE],
                               AppStorageMetadataRecord_t *pxRecord)
{
    uint32_t ulStoredCrc;
    uint32_t ulCalculatedCrc;
    if ((pucBuffer == NULL) || (pxRecord == NULL) ||
        (memcmp(&pucBuffer[APP_STORAGE_METADATA_MAGIC_OFFSET], s_aucMetadataMagic,
                sizeof(s_aucMetadataMagic)) != 0) ||
        (prvReadLe16(&pucBuffer[APP_STORAGE_METADATA_VERSION_OFFSET]) !=
         APP_STORAGE_METADATA_FORMAT_VERSION) ||
        (prvReadLe16(&pucBuffer[APP_STORAGE_METADATA_LENGTH_OFFSET]) !=
         APP_STORAGE_METADATA_RECORD_SIZE) ||
        (prvBytesAreZero(&pucBuffer[APP_STORAGE_METADATA_RESERVED_OFFSET],
                         APP_STORAGE_METADATA_RESERVED_LENGTH) == false) ||
        (memcmp(&pucBuffer[APP_STORAGE_METADATA_COMMIT_OFFSET], s_aucCommitMarker,
                sizeof(s_aucCommitMarker)) != 0))
    {
        return false;
    }

    ulStoredCrc = prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_CRC_OFFSET]);
    ulCalculatedCrc =
        AppStorageCrc32_Calculate(pucBuffer, APP_STORAGE_METADATA_CRC_OFFSET);
    if (ulStoredCrc != ulCalculatedCrc)
    {
        return false;
    }

    memset(pxRecord, 0, sizeof(*pxRecord));
    pxRecord->ulGeneration =
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_GENERATION_OFFSET]);
    pxRecord->xState = (AppStoragePersistentState_t)
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_STATE_OFFSET]);
    pxRecord->ulLastError =
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_ERROR_OFFSET]);
    pxRecord->ulFirmwareLength =
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_FIRMWARE_LENGTH_OFFSET]);

    memcpy(pxRecord->aucFirmwareMd5,
           &pucBuffer[APP_STORAGE_METADATA_FIRMWARE_MD5_OFFSET],
           APP_STORAGE_MD5_DIGEST_SIZE);

    pxRecord->ulInstallAttempts =
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_INSTALL_COUNT_OFFSET]);
    pxRecord->ulBootAttempts =
        prvReadLe32(&pucBuffer[APP_STORAGE_METADATA_BOOT_COUNT_OFFSET]);

    return prvRecordSemanticsAreValid(pxRecord);
}

static bool prvGenerationIsNewer(uint32_t ulLeft, uint32_t ulRight)
{
    return (int32_t)(ulLeft - ulRight) > 0;
}

AppStorageMetadataSelectResult_t AppStorageMetadata_Select(
    const uint8_t pucRecordA[APP_STORAGE_METADATA_RECORD_SIZE],
    const uint8_t pucRecordB[APP_STORAGE_METADATA_RECORD_SIZE],
    AppStorageMetadataRecord_t *pxSelectedRecord,
    uint32_t *pulActiveAddress)
{
    AppStorageMetadataRecord_t xRecordA;
    AppStorageMetadataRecord_t xRecordB;
    bool xRecordAValid;
    bool xRecordBValid;

    if ((pucRecordA == NULL) || (pucRecordB == NULL) ||
        (pxSelectedRecord == NULL) || (pulActiveAddress == NULL))
    {
        return APP_STORAGE_METADATA_SELECT_ERROR;
    }

    xRecordAValid = AppStorageMetadata_Decode(pucRecordA, &xRecordA);
    xRecordBValid = AppStorageMetadata_Decode(pucRecordB, &xRecordB);

    if ((xRecordAValid == false) && (xRecordBValid == false))
    {
        memset(pxSelectedRecord, 0, sizeof(*pxSelectedRecord));
        *pulActiveAddress = UINT32_MAX;
        return APP_STORAGE_METADATA_SELECT_EMPTY;
    }

    if (xRecordAValid && (xRecordBValid == false))
    {
        *pxSelectedRecord = xRecordA;
        *pulActiveAddress = APP_STORAGE_METADATA_A_ADDRESS;
        return APP_STORAGE_METADATA_SELECT_FOUND;
    }

    if ((xRecordAValid == false) && xRecordBValid)
    {
        *pxSelectedRecord = xRecordB;
        *pulActiveAddress = APP_STORAGE_METADATA_B_ADDRESS;
        return APP_STORAGE_METADATA_SELECT_FOUND;
    }

    if (xRecordA.ulGeneration == xRecordB.ulGeneration)
    {
        if (memcmp(pucRecordA, pucRecordB, APP_STORAGE_METADATA_RECORD_SIZE) != 0)
        {
            return APP_STORAGE_METADATA_SELECT_ERROR;
        }

        *pxSelectedRecord = xRecordA;
        *pulActiveAddress = APP_STORAGE_METADATA_A_ADDRESS;
        return APP_STORAGE_METADATA_SELECT_FOUND;
    }

    if (prvGenerationIsNewer(xRecordA.ulGeneration, xRecordB.ulGeneration))
    {
        *pxSelectedRecord = xRecordA;
        *pulActiveAddress = APP_STORAGE_METADATA_A_ADDRESS;
    }
    else
    {
        *pxSelectedRecord = xRecordB;
        *pulActiveAddress = APP_STORAGE_METADATA_B_ADDRESS;
    }

    return APP_STORAGE_METADATA_SELECT_FOUND;
}
