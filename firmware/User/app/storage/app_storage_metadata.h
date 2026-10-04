#ifndef APP_STORAGE_METADATA_H
#define APP_STORAGE_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "app_storage_layout.h"

#define APP_STORAGE_METADATA_RECORD_SIZE              256U
#define APP_STORAGE_METADATA_BODY_SIZE                252U
#define APP_STORAGE_METADATA_COMMIT_SIZE                4U

typedef enum
{
    APP_STORAGE_PERSISTENT_STATE_EMPTY = 0U,              /* 没有有效候选固件。 */
    APP_STORAGE_PERSISTENT_STATE_PREPARING = 1U,          /* 新下载已开始，正在准备候选区。 */
    APP_STORAGE_PERSISTENT_STATE_DOWNLOADING = 2U,        /* 候选区已准备好，正在写入原始固件。 */
    APP_STORAGE_PERSISTENT_STATE_VERIFYING = 3U,          /* 下载结束，正在校验长度、描述头和MD5。 */
    APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE = 4U,   /* 全部校验通过，可供Bootloader安装。 */
    APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED = 5U,  /* 应用已持久化升级请求。 */
    APP_STORAGE_PERSISTENT_STATE_INSTALLING = 6U,         /* Bootloader正在写内部Flash应用区。 */
    APP_STORAGE_PERSISTENT_STATE_BOOT_PENDING = 7U,       /* 新应用已安装，等待首次启动确认。 */
    APP_STORAGE_PERSISTENT_STATE_CONFIRMED = 8U,          /* 新应用已确认运行。 */
    APP_STORAGE_PERSISTENT_STATE_FAILED = 9U              /* 事务失败，候选包不得用于升级。 */
} AppStoragePersistentState_t;      /* 写入元数据中的持久化状态 */

typedef struct
{
    uint32_t ulGeneration;
    AppStoragePersistentState_t xState;
    uint32_t ulLastError;
    uint32_t ulFirmwareLength;
    uint8_t aucFirmwareMd5[APP_STORAGE_MD5_DIGEST_SIZE]; /* 候选固件的已声明或已验证MD5。 */
    uint32_t ulInstallAttempts;
    uint32_t ulBootAttempts;
} AppStorageMetadataRecord_t;

typedef enum
{
    APP_STORAGE_METADATA_SELECT_ERROR = -1,
    APP_STORAGE_METADATA_SELECT_EMPTY = 0,
    APP_STORAGE_METADATA_SELECT_FOUND = 1
} AppStorageMetadataSelectResult_t;

bool AppStorageMetadata_Encode(const AppStorageMetadataRecord_t *pxRecord,
                               uint8_t pucBuffer[APP_STORAGE_METADATA_RECORD_SIZE]);
bool AppStorageMetadata_Decode(const uint8_t pucBuffer[APP_STORAGE_METADATA_RECORD_SIZE],
                               AppStorageMetadataRecord_t *pxRecord);
AppStorageMetadataSelectResult_t AppStorageMetadata_Select(
    const uint8_t pucRecordA[APP_STORAGE_METADATA_RECORD_SIZE],
    const uint8_t pucRecordB[APP_STORAGE_METADATA_RECORD_SIZE],
    AppStorageMetadataRecord_t *pxSelectedRecord,
    uint32_t *pulActiveAddress);

#endif /* APP_STORAGE_METADATA_H */
