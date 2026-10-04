#ifndef APP_FTP_TASK_H
#define APP_FTP_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "rtos_w5500_access.h"

#define APP_FTP_COMMAND_QUEUE_LENGTH           1U
#define APP_FTP_STATUS_QUEUE_LENGTH            1U

#define APP_FTP_USER_MAX_LENGTH               32U
#define APP_FTP_PASSWORD_MAX_LENGTH           64U
#define APP_FTP_PATH_MAX_LENGTH               96U
#define APP_FTP_FILE_NAME_MAX_LENGTH          96U
#define APP_FTP_MD5_DIGEST_SIZE               16U

typedef enum
{
    APP_FTP_STATE_IDLE = 0,
    APP_FTP_STATE_CONNECTING_CONTROL,
    APP_FTP_STATE_AUTHENTICATING,
    APP_FTP_STATE_QUERYING_SIZE,
    APP_FTP_STATE_PREPARING_STORAGE,
    APP_FTP_STATE_ENTERING_PASSIVE,
    APP_FTP_STATE_CONNECTING_DATA,
    APP_FTP_STATE_TRANSFERRING,
    APP_FTP_STATE_VERIFYING,
    APP_FTP_STATE_COMPLETED,
    APP_FTP_STATE_FAILED
} AppFtpState_t;

typedef enum
{
    APP_FTP_ERROR_NONE = 0,
    APP_FTP_ERROR_COMMAND_INVALID,
    APP_FTP_ERROR_NETWORK_UNAVAILABLE,
    APP_FTP_ERROR_CONTROL_CONNECT,
    APP_FTP_ERROR_GREETING,
    APP_FTP_ERROR_AUTHENTICATION,
    APP_FTP_ERROR_BINARY_MODE,
    APP_FTP_ERROR_REMOTE_SIZE,
    APP_FTP_ERROR_STORAGE_PREPARE,
    APP_FTP_ERROR_PASSIVE_RESPONSE,
    APP_FTP_ERROR_PASSIVE_PORT,
    APP_FTP_ERROR_DATA_CONNECT,
    APP_FTP_ERROR_RETRIEVE,
    APP_FTP_ERROR_DATA_TRANSFER,
    APP_FTP_ERROR_TRANSFER_COMPLETE,
    APP_FTP_ERROR_STORAGE_WRITE,
    APP_FTP_ERROR_STORAGE_VERIFY,
    APP_FTP_ERROR_TIMEOUT
} AppFtpError_t;

typedef struct
{
    uint32_t ulTransactionId;
    uint8_t aucServerIp[4];
    uint16_t usServerPort;
    uint16_t usUserLength;
    uint16_t usPasswordLength;
    uint16_t usPathLength;
    uint16_t usFileNameLength;
    char acUser[APP_FTP_USER_MAX_LENGTH + 1U];
    char acPassword[APP_FTP_PASSWORD_MAX_LENGTH + 1U];
    char acPath[APP_FTP_PATH_MAX_LENGTH + 1U];
    char acFileName[APP_FTP_FILE_NAME_MAX_LENGTH + 1U];
    uint8_t aucExpectedFirmwareMd5[APP_FTP_MD5_DIGEST_SIZE];
} AppFtpDownloadCommand_t;

typedef struct
{
    AppFtpState_t xState;
    AppFtpError_t xError;
    uint32_t ulTransactionId;
    uint32_t ulFirmwareLength;
    uint32_t ulReceivedLength;
    uint32_t ulWrittenLength;
    uint8_t ucProgressPercent;
    bool xAcceptingCommand;
} AppFtpStatus_t;

bool AppFtpTask_Create(RtosW5500Access_t *pxW5500Access,
                       QueueHandle_t xNetworkStatusQueue,
                       QueueHandle_t xCommandQueue,
                       QueueHandle_t xStatusQueue,
                       QueueHandle_t xStorageRequestQueue,
                       QueueHandle_t xStorageStatusQueue);

#endif /* APP_FTP_TASK_H */
