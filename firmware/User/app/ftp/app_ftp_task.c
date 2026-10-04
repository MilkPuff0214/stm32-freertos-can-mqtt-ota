#include "app_ftp_task.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "task.h"

#include "app_ftp_client.h"
#include "app_network_config.h"
#include "app_network_status.h"
#include "app_storage_layout.h"
#include "app_storage_task.h"
#include "app_tcp_client.h"

#define APP_FTP_TASK_STACK_WORDS                 768U
#define APP_FTP_TASK_PRIORITY       (tskIDLE_PRIORITY + 1U)
#define APP_FTP_SERVICE_PERIOD_MS                  10U

#define APP_FTP_CONTROL_CONNECT_TIMEOUT_MS      10000U
#define APP_FTP_REPLY_TIMEOUT_MS                10000U
#define APP_FTP_DATA_CONNECT_TIMEOUT_MS         10000U
#define APP_FTP_DATA_IDLE_TIMEOUT_MS            15000U
#define APP_FTP_STORAGE_PROGRESS_TIMEOUT_MS     30000U
#define APP_FTP_TRANSACTION_TIMEOUT_MS         600000U

#define APP_FTP_PASSIVE_PORT_FIRST              50000U
#define APP_FTP_PASSIVE_PORT_LAST               50009U
#define APP_FTP_REMOTE_FILE_MAX_LENGTH \
    (APP_FTP_PATH_MAX_LENGTH + 1U + APP_FTP_FILE_NAME_MAX_LENGTH)

_Static_assert(APP_FTP_MD5_DIGEST_SIZE == APP_STORAGE_MD5_DIGEST_SIZE,
               "FTP and Storage MD5 sizes must match");
_Static_assert(APP_FTP_REMOTE_FILE_MAX_LENGTH + 7U <=
               APP_FTP_CONTROL_TX_BUFFER_SIZE,
               "FTP remote path does not fit the control TX buffer");

typedef enum
{
    APP_FTP_PHASE_IDLE = 0,
    APP_FTP_PHASE_START_CONTROL,
    APP_FTP_PHASE_WAIT_CONTROL,
    APP_FTP_PHASE_WAIT_GREETING,
    APP_FTP_PHASE_SEND_USER,
    APP_FTP_PHASE_WAIT_USER,
    APP_FTP_PHASE_SEND_PASS,
    APP_FTP_PHASE_WAIT_PASS,
    APP_FTP_PHASE_SEND_TYPE,
    APP_FTP_PHASE_WAIT_TYPE,
    APP_FTP_PHASE_SEND_SIZE,
    APP_FTP_PHASE_WAIT_SIZE,
    APP_FTP_PHASE_PREPARE_STORAGE,
    APP_FTP_PHASE_WAIT_STORAGE_READY,
    APP_FTP_PHASE_SEND_EPSV,
    APP_FTP_PHASE_WAIT_EPSV,
    APP_FTP_PHASE_STOP_DATA,
    APP_FTP_PHASE_CONFIGURE_DATA,
    APP_FTP_PHASE_WAIT_DATA,
    APP_FTP_PHASE_SEND_RETR,
    APP_FTP_PHASE_WAIT_RETR,
    APP_FTP_PHASE_RECEIVE_DATA,
    APP_FTP_PHASE_WAIT_DATA_EOF,
    APP_FTP_PHASE_WAIT_TRANSFER_COMPLETE,
    APP_FTP_PHASE_FINISH_STORAGE,
    APP_FTP_PHASE_WAIT_STORAGE_VERIFY,
    APP_FTP_PHASE_SEND_QUIT,
    APP_FTP_PHASE_WAIT_QUIT,
    APP_FTP_PHASE_CLEANUP
} AppFtpPhase_t;

static const uint8_t s_aucExpectedServerIp[4] = {192U, 168U, 101U, 11U};

static const AppTcpClientConfig_t s_xControlTcpConfig =
{
    .ucSocket = APP_NETWORK_SOCKET_FTP_CONTROL,
    .aucServerIp = {192U, 168U, 101U, 11U},
    .usServerPort = 21U,
    .usLocalPortFirst = 49152U,
    .usLocalPortLast = 65535U,
    .ulLocalActionTimeoutMs = 100U,
    .ulConnectTimeoutMs = 5000U,
    .ulSendTimeoutMs = 5000U,
    .ulPeerDrainTimeoutMs = 5000U,
    .ulDisconnectTimeoutMs = 5000U,
    .xStartEnabled = false
};

static const AppTcpClientConfig_t s_xDataTcpConfig =
{
    .ucSocket = APP_NETWORK_SOCKET_FTP_DATA,
    .aucServerIp = {192U, 168U, 101U, 11U},
    .usServerPort = APP_FTP_PASSIVE_PORT_FIRST,
    .usLocalPortFirst = 49152U,
    .usLocalPortLast = 65535U,
    .ulLocalActionTimeoutMs = 100U,
    .ulConnectTimeoutMs = 5000U,
    .ulSendTimeoutMs = 5000U,
    .ulPeerDrainTimeoutMs = 5000U,
    .ulDisconnectTimeoutMs = 5000U,
    .xStartEnabled = false
};

static QueueHandle_t s_xNetworkStatusQueue = NULL;
static QueueHandle_t s_xCommandQueue = NULL;
static QueueHandle_t s_xStatusQueue = NULL;
static QueueHandle_t s_xStorageRequestQueue = NULL;
static QueueHandle_t s_xStorageStatusQueue = NULL;

static AppNetworkStatus_t s_xNetworkStatus = {0};
static AppStorageStatus_t s_xStorageStatus = {0};
static AppFtpDownloadCommand_t s_xCommand = {0};
static AppFtpStatus_t s_xPublicStatus = {0};
static AppFtpStatus_t s_xLastPublishedStatus = {0};
static AppTcpClient_t s_xControlTcpClient = {0};
static AppTcpClient_t s_xDataTcpClient = {0};
static AppFtpClient_t s_xFtpClient = {0};
static AppFtpReply_t s_xReply = {0};
static AppStorageRequest_t s_xPendingStorageRequest = {0};
static char s_acRemoteFile[APP_FTP_REMOTE_FILE_MAX_LENGTH + 1U] = {0};

static AppFtpPhase_t s_xPhase = APP_FTP_PHASE_IDLE;
static TickType_t s_xPhaseStartTick = 0U;
static TickType_t s_xTransactionStartTick = 0U;
static TickType_t s_xLastDataTick = 0U;
static TickType_t s_xLastStorageProgressTick = 0U;
static uint32_t s_ulControlSession = 0U;
static uint32_t s_ulDataSession = 0U;
static uint16_t s_usPassivePort = 0U;
static bool s_xHasStorageStatus = false;
static bool s_xHasPendingStorageRequest = false;
static bool s_xStoragePrepareSubmitted = false;
static bool s_xFinishSubmitted = false;
static bool s_xAbortSubmitted = false;
static bool s_xStatusPublished = false;
static bool s_xTaskCreated = false;

static TickType_t prvMillisecondsToTicks(uint32_t ulMilliseconds)
{
    TickType_t xTicks = pdMS_TO_TICKS(ulMilliseconds);

    return (xTicks == 0U) ? 1U : xTicks;
}

static bool prvHasExpired(TickType_t xStartTick, TickType_t xNowTick,
                          uint32_t ulTimeoutMs)
{
    return (TickType_t)(xNowTick - xStartTick) >= prvMillisecondsToTicks(ulTimeoutMs);
}

static void prvSetPhase(AppFtpPhase_t xPhase, TickType_t xNowTick)
{
    s_xPhase = xPhase;
    s_xPhaseStartTick = xNowTick;
}

static bool prvPublicStatusChanged(void)
{
    return (s_xPublicStatus.xState != s_xLastPublishedStatus.xState) ||
           (s_xPublicStatus.xError != s_xLastPublishedStatus.xError) ||
           (s_xPublicStatus.ulTransactionId !=
            s_xLastPublishedStatus.ulTransactionId) ||
           (s_xPublicStatus.ulFirmwareLength !=
            s_xLastPublishedStatus.ulFirmwareLength) ||
           (s_xPublicStatus.ulReceivedLength !=
            s_xLastPublishedStatus.ulReceivedLength) ||
           (s_xPublicStatus.ulWrittenLength !=
            s_xLastPublishedStatus.ulWrittenLength) ||
           (s_xPublicStatus.ucProgressPercent !=
            s_xLastPublishedStatus.ucProgressPercent) ||
           (s_xPublicStatus.xAcceptingCommand !=
            s_xLastPublishedStatus.xAcceptingCommand);
}

static void prvPublishStatusIfChanged(void)
{
    if ((s_xStatusPublished == false) || prvPublicStatusChanged())
    {
        if (xQueueOverwrite(s_xStatusQueue, &s_xPublicStatus) == pdPASS)
        {
            s_xLastPublishedStatus = s_xPublicStatus;
            s_xStatusPublished = true;
        }
    }
}

static bool prvStorageStatusChanged(const AppStorageStatus_t *pxStatus)
{
    return (pxStatus->xState != s_xStorageStatus.xState) ||
           (pxStatus->xError != s_xStorageStatus.xError) ||
           (pxStatus->ulTransactionId != s_xStorageStatus.ulTransactionId) ||
           (pxStatus->ulFirmwareLength != s_xStorageStatus.ulFirmwareLength) ||
           (pxStatus->ulErasedLength != s_xStorageStatus.ulErasedLength) ||
           (pxStatus->ulWrittenLength != s_xStorageStatus.ulWrittenLength);
}

static void prvRefreshNetworkStatus(void)
{
    AppNetworkStatus_t xStatus;

    if (xQueueReceive(s_xNetworkStatusQueue, &xStatus, 0U) == pdPASS)
    {
        s_xNetworkStatus = xStatus;
    }
}

static void prvRefreshStorageStatus(TickType_t xNowTick)
{
    AppStorageStatus_t xStatus;

    /* Storage状态是共享最新快照，不能消费掉业务Task也需要读取的元素。 */
    if (xQueuePeek(s_xStorageStatusQueue, &xStatus, 0U) != pdPASS)
    {
        return;
    }

    if ((xStatus.ulTransactionId == s_xPublicStatus.ulTransactionId) &&
        ((s_xHasStorageStatus == false) || prvStorageStatusChanged(&xStatus)))
    {
        s_xLastStorageProgressTick = xNowTick;
    }
    s_xStorageStatus = xStatus;
    s_xHasStorageStatus = true;

    /* 成功终态的100%由完成流程给出，重复快照不能再按传输进度封顶95%。 */
    if ((s_xPublicStatus.xState != APP_FTP_STATE_COMPLETED) &&
        (xStatus.ulTransactionId == s_xPublicStatus.ulTransactionId) &&
        (xStatus.ulWrittenLength <= s_xPublicStatus.ulFirmwareLength))
    {
        uint64_t ullPercent;

        s_xPublicStatus.ulWrittenLength = xStatus.ulWrittenLength;
        if (s_xPublicStatus.ulFirmwareLength > 0U)
        {
            ullPercent = ((uint64_t)xStatus.ulWrittenLength * 100ULL) /
                         (uint64_t)s_xPublicStatus.ulFirmwareLength;
            s_xPublicStatus.ucProgressPercent = (ullPercent > 95ULL) ?
                95U : (uint8_t)ullPercent;
        }
    }
}

static bool prvStorageStatusMatches(void)
{
    return s_xHasStorageStatus &&
           (s_xStorageStatus.ulTransactionId == s_xPublicStatus.ulTransactionId);
}

static bool prvTcpIsDisabled(const AppTcpClient_t *pxClient)
{
    return (pxClient->xEnabled == false) &&
           (AppTcpClient_GetState(pxClient) == APP_TCP_CLIENT_STATE_WAIT_LINK);
}

static bool prvControlSessionIsUsable(bool xSending)
{
    AppTcpClientState_t xState = AppTcpClient_GetState(&s_xControlTcpClient);

    if ((s_ulControlSession == 0U) ||
        (AppTcpClient_GetSessionGeneration(&s_xControlTcpClient) !=
         s_ulControlSession))
    {
        return false;
    }
    if (xState == APP_TCP_CLIENT_STATE_CONNECTED)
    {
        return true;
    }
    return (xSending == false) && (xState == APP_TCP_CLIENT_STATE_PEER_FIN);
}

static bool prvDataSessionIsUsable(void)
{
    AppTcpClientState_t xState = AppTcpClient_GetState(&s_xDataTcpClient);

    return (s_ulDataSession != 0U) &&
           (AppTcpClient_GetSessionGeneration(&s_xDataTcpClient) == s_ulDataSession) &&
           ((xState == APP_TCP_CLIENT_STATE_CONNECTED) ||
            (xState == APP_TCP_CLIENT_STATE_PEER_FIN));
}

static bool prvCommandIsValid(const AppFtpDownloadCommand_t *pxCommand)
{
    uint16_t usIndex;

    if ((pxCommand->ulTransactionId == 0U) ||
        (memcmp(pxCommand->aucServerIp, s_aucExpectedServerIp,
                sizeof(s_aucExpectedServerIp)) != 0) ||
        (pxCommand->usServerPort != 21U) ||
        (pxCommand->usUserLength == 0U) ||
        (pxCommand->usUserLength > APP_FTP_USER_MAX_LENGTH) ||
        (pxCommand->usPasswordLength == 0U) ||
        (pxCommand->usPasswordLength > APP_FTP_PASSWORD_MAX_LENGTH) ||
        (pxCommand->usPathLength == 0U) ||
        (pxCommand->usPathLength > APP_FTP_PATH_MAX_LENGTH) ||
        (pxCommand->usFileNameLength == 0U) ||
        (pxCommand->usFileNameLength > APP_FTP_FILE_NAME_MAX_LENGTH) ||
        (pxCommand->acUser[pxCommand->usUserLength] != '\0') ||
        (pxCommand->acPassword[pxCommand->usPasswordLength] != '\0') ||
        (pxCommand->acPath[pxCommand->usPathLength] != '\0') ||
        (pxCommand->acFileName[pxCommand->usFileNameLength] != '\0') ||
        (pxCommand->acPath[0] != '/') ||
        (pxCommand->acPath[pxCommand->usPathLength - 1U] == '/'))
    {
        return false;
    }

    for (usIndex = 0U; usIndex < pxCommand->usFileNameLength; usIndex++)
    {
        if ((pxCommand->acFileName[usIndex] == '/') ||
            (pxCommand->acFileName[usIndex] == '\\') ||
            (((usIndex + 1U) < pxCommand->usFileNameLength) &&
             (pxCommand->acFileName[usIndex] == '.') &&
             (pxCommand->acFileName[usIndex + 1U] == '.')))
        {
            return false;
        }
    }
    return true;
}

static bool prvBuildRemoteFile(void)
{
    uint32_t ulLength = (uint32_t)s_xCommand.usPathLength + 1U +
                        (uint32_t)s_xCommand.usFileNameLength;

    if (ulLength > APP_FTP_REMOTE_FILE_MAX_LENGTH)
    {
        return false;
    }

    memcpy(s_acRemoteFile, s_xCommand.acPath, s_xCommand.usPathLength);
    s_acRemoteFile[s_xCommand.usPathLength] = '/';
    memcpy(&s_acRemoteFile[s_xCommand.usPathLength + 1U],
           s_xCommand.acFileName, s_xCommand.usFileNameLength);
    s_acRemoteFile[ulLength] = '\0';
    return true;
}

static uint16_t prvRemoteFileLength(void)
{
    return (uint16_t)(s_xCommand.usPathLength + 1U +
                      s_xCommand.usFileNameLength);
}

static void prvDisableConnections(void)
{
    AppTcpClient_SetEnabled(&s_xDataTcpClient, false);
    AppTcpClient_SetEnabled(&s_xControlTcpClient, false);
}

static void prvFail(AppFtpError_t xError, TickType_t xNowTick)
{
    if (s_xPhase == APP_FTP_PHASE_CLEANUP)
    {
        return;
    }

    /* FINISH入队后结果由Storage最终裁定，避免先发布失败又改成成功。 */
    s_xPublicStatus.xState = s_xFinishSubmitted ?
        APP_FTP_STATE_VERIFYING : APP_FTP_STATE_FAILED;
    s_xPublicStatus.xError = xError;
    s_xPublicStatus.xAcceptingCommand = false;
    memset(s_xCommand.acPassword, 0, sizeof(s_xCommand.acPassword));

    /* 尚未进入Storage Queue的数据块不再属于任何消费者，直接丢弃。 */
    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    s_xHasPendingStorageRequest = false;
    prvDisableConnections();
    prvSetPhase(APP_FTP_PHASE_CLEANUP, xNowTick);
}

static void prvComplete(TickType_t xNowTick)
{
    s_xPublicStatus.xState = APP_FTP_STATE_COMPLETED;
    s_xPublicStatus.xError = APP_FTP_ERROR_NONE;
    s_xPublicStatus.ulWrittenLength = s_xPublicStatus.ulFirmwareLength;
    s_xPublicStatus.ucProgressPercent = 100U;
    s_xPublicStatus.xAcceptingCommand = false;
    s_xStoragePrepareSubmitted = false;
    s_xAbortSubmitted = true;
    memset(s_xCommand.acPassword, 0, sizeof(s_xCommand.acPassword));
    prvDisableConnections();
    prvSetPhase(APP_FTP_PHASE_CLEANUP, xNowTick);
}

static bool prvTryQueueStorageRequest(void)
{
    BaseType_t xResult;
    AppStorageRequestType_t xType;
    uint16_t usWriteLength = 0U;

    if (s_xHasPendingStorageRequest == false)
    {
        return true;
    }

    xType = s_xPendingStorageRequest.xType;
    if (xType == APP_STORAGE_REQUEST_WRITE_BLOCK)
    {
        usWriteLength = s_xPendingStorageRequest.xData.xWrite.usLength;
    }

    xResult = xQueueSendToBack(s_xStorageRequestQueue,
                               &s_xPendingStorageRequest, 0U);
    if (xResult != pdPASS)
    {
        return false;
    }

    if (xType == APP_STORAGE_REQUEST_PREPARE_DOWNLOAD)
    {
        s_xStoragePrepareSubmitted = true;
    }
    else if (xType == APP_STORAGE_REQUEST_WRITE_BLOCK)
    {
        s_xPublicStatus.ulReceivedLength += usWriteLength;
    }
    else if (xType == APP_STORAGE_REQUEST_FINISH_DOWNLOAD)
    {
        s_xFinishSubmitted = true;
    }
    else if (xType == APP_STORAGE_REQUEST_ABORT_DOWNLOAD)
    {
        s_xAbortSubmitted = true;
    }

    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    s_xHasPendingStorageRequest = false;
    return true;
}

static void prvPrepareStorageRequest(void)
{
    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    s_xPendingStorageRequest.xType = APP_STORAGE_REQUEST_PREPARE_DOWNLOAD;
    s_xPendingStorageRequest.ulTransactionId = s_xPublicStatus.ulTransactionId;
    s_xPendingStorageRequest.xData.xPrepare.ulFirmwareLength =
        s_xPublicStatus.ulFirmwareLength;
    memcpy(s_xPendingStorageRequest.xData.xPrepare.aucExpectedFirmwareMd5,
           s_xCommand.aucExpectedFirmwareMd5, APP_FTP_MD5_DIGEST_SIZE);
    s_xHasPendingStorageRequest = true;
}

static void prvPrepareFinishRequest(void)
{
    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    s_xPendingStorageRequest.xType = APP_STORAGE_REQUEST_FINISH_DOWNLOAD;
    s_xPendingStorageRequest.ulTransactionId = s_xPublicStatus.ulTransactionId;
    s_xHasPendingStorageRequest = true;
}

static bool prvStorageCanStartDownload(void)
{
    if (s_xHasStorageStatus == false)
    {
        return false;
    }

    return (s_xStorageStatus.xState == APP_STORAGE_STATE_IDLE) ||
           (s_xStorageStatus.xState == APP_STORAGE_STATE_FAILED) ||
           (s_xStorageStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE);
}

static bool prvStorageAbortIsComplete(void)
{
    return prvStorageStatusMatches() &&
           ((s_xStorageStatus.xState == APP_STORAGE_STATE_FAILED) ||
            (s_xStorageStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT));
}

static bool prvStorageVerificationSucceeded(void)
{
    return prvStorageStatusMatches() &&
           (s_xStorageStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
           (s_xStorageStatus.ulWrittenLength ==
            s_xPublicStatus.ulFirmwareLength);
}

static void prvServiceCleanup(void)
{
    if (s_xFinishSubmitted && prvStorageVerificationSucceeded())
    {
        s_xPublicStatus.xState = APP_FTP_STATE_COMPLETED;
        s_xPublicStatus.xError = APP_FTP_ERROR_NONE;
        s_xPublicStatus.ulWrittenLength = s_xPublicStatus.ulFirmwareLength;
        s_xPublicStatus.ucProgressPercent = 100U;
        s_xStoragePrepareSubmitted = false;
        s_xAbortSubmitted = true;
    }

    prvDisableConnections();

    if ((s_xPublicStatus.xError != APP_FTP_ERROR_NONE) &&
        s_xStoragePrepareSubmitted)
    {
        if (s_xAbortSubmitted == false)
        {
            if (prvStorageStatusMatches() &&
                ((s_xStorageStatus.xState == APP_STORAGE_STATE_FAILED) ||
                 (s_xStorageStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT)))
            {
                s_xAbortSubmitted = true;
            }
            else
            {
                if (s_xHasPendingStorageRequest == false)
                {
                    memset(&s_xPendingStorageRequest, 0,
                           sizeof(s_xPendingStorageRequest));
                    s_xPendingStorageRequest.xType =
                        APP_STORAGE_REQUEST_ABORT_DOWNLOAD;
                    s_xPendingStorageRequest.ulTransactionId =
                        s_xPublicStatus.ulTransactionId;
                    s_xHasPendingStorageRequest = true;
                }
                /* 单块背压无排队WRITE；FIFO保留PREPARE/FINISH顺序。 */
                if (prvTryQueueStorageRequest() == false)
                {
                    return;
                }
            }
        }

        if (prvStorageAbortIsComplete() == false)
        {
            return;
        }
        s_xPublicStatus.xState = APP_FTP_STATE_FAILED;
    }

    if ((prvTcpIsDisabled(&s_xControlTcpClient) == false) ||
        (prvTcpIsDisabled(&s_xDataTcpClient) == false))
    {
        return;
    }

    AppFtpClient_ResetSession(&s_xFtpClient);
    memset(&s_xCommand, 0, sizeof(s_xCommand));
    memset(&s_xReply, 0, sizeof(s_xReply));
    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    memset(s_acRemoteFile, 0, sizeof(s_acRemoteFile));
    s_xHasPendingStorageRequest = false;
    s_xFinishSubmitted = false;
    s_ulControlSession = 0U;
    s_ulDataSession = 0U;
    s_usPassivePort = 0U;
    s_xPublicStatus.xAcceptingCommand = true;
    s_xPhase = APP_FTP_PHASE_IDLE;
}

static bool prvParseUnsignedDecimal(const char *pcText, uint16_t usLength,
                                    uint32_t *pulValue)
{
    uint16_t usStart = 0U;
    uint16_t usEnd = usLength;
    uint16_t usIndex;
    uint32_t ulValue = 0U;
    uint32_t ulDigit;

    while ((usStart < usEnd) && (pcText[usStart] == ' '))
    {
        usStart++;
    }
    while ((usEnd > usStart) && (pcText[usEnd - 1U] == ' '))
    {
        usEnd--;
    }
    if (usStart == usEnd)
    {
        return false;
    }

    for (usIndex = usStart; usIndex < usEnd; usIndex++)
    {
        if ((pcText[usIndex] < '0') || (pcText[usIndex] > '9'))
        {
            return false;
        }
        ulDigit = (uint32_t)(pcText[usIndex] - '0');
        if (ulValue > ((UINT32_MAX - ulDigit) / 10U))
        {
            return false;
        }
        ulValue = (ulValue * 10U) + ulDigit;
    }

    *pulValue = ulValue;
    return true;
}

static bool prvParseEpsvPort(const AppFtpReply_t *pxReply, uint16_t *pusPort)
{
    uint16_t usLeft = UINT16_MAX;
    uint16_t usRight = UINT16_MAX;
    uint16_t usDigitStart;
    uint16_t usIndex;
    uint32_t ulPort;
    char cDelimiter;

    for (usIndex = 0U; usIndex < pxReply->usTextLength; usIndex++)
    {
        if ((pxReply->acText[usIndex] == '(') && (usLeft == UINT16_MAX))
        {
            usLeft = usIndex;
        }
        else if ((pxReply->acText[usIndex] == ')') && (usLeft != UINT16_MAX))
        {
            usRight = usIndex;
            break;
        }
    }

    if ((usLeft == UINT16_MAX) || (usRight == UINT16_MAX) ||
        ((uint16_t)(usRight - usLeft) < 6U))
    {
        return false;
    }

    cDelimiter = pxReply->acText[usLeft + 1U];
    if ((cDelimiter < '!') || (cDelimiter > '~') ||
        ((cDelimiter >= '0') && (cDelimiter <= '9')) ||
        (pxReply->acText[usLeft + 2U] != cDelimiter) ||
        (pxReply->acText[usLeft + 3U] != cDelimiter) ||
        (pxReply->acText[usRight - 1U] != cDelimiter))
    {
        return false;
    }

    usDigitStart = (uint16_t)(usLeft + 4U);
    if ((usDigitStart >= (uint16_t)(usRight - 1U)) ||
        (prvParseUnsignedDecimal(&pxReply->acText[usDigitStart],
                                 (uint16_t)(usRight - 1U - usDigitStart),
                                 &ulPort) == false) ||
        (ulPort < APP_FTP_PASSIVE_PORT_FIRST) ||
        (ulPort > APP_FTP_PASSIVE_PORT_LAST))
    {
        return false;
    }

    *pusPort = (uint16_t)ulPort;
    return true;
}

static void prvServiceCommandSend(const char *pcVerb, uint16_t usVerbLength,
                                  const char *pcArgument, uint16_t usArgumentLength,
                                  AppFtpPhase_t xNextPhase, AppFtpError_t xError,
                                  TickType_t xNowTick)
{
    AppFtpIoResult_t xResult;

    if (prvControlSessionIsUsable(true) == false)
    {
        prvFail(xError, xNowTick);
        return;
    }

    if ((s_xFtpClient.usTxLength == 0U) &&
        (AppFtpClient_PrepareCommand(&s_xFtpClient, pcVerb, usVerbLength,
                                     pcArgument, usArgumentLength) == false))
    {
        prvFail(xError, xNowTick);
        return;
    }

    xResult = AppFtpClient_ServiceSend(&s_xFtpClient);
    if (xResult == APP_FTP_IO_ERROR)
    {
        prvFail(xError, xNowTick);
    }
    else if (xResult == APP_FTP_IO_COMPLETE)
    {
        prvSetPhase(xNextPhase, xNowTick);
    }
    else if (prvHasExpired(s_xPhaseStartTick, xNowTick,
                           APP_FTP_REPLY_TIMEOUT_MS))
    {
        prvFail(APP_FTP_ERROR_TIMEOUT, xNowTick);
    }
}

static AppFtpIoResult_t prvPollControlReply(AppFtpError_t xError,
                                            TickType_t xNowTick)
{
    AppFtpIoResult_t xResult;

    if (prvControlSessionIsUsable(false) == false)
    {
        prvFail(xError, xNowTick);
        return APP_FTP_IO_ERROR;
    }

    xResult = AppFtpClient_PollReply(&s_xFtpClient, &s_xReply);
    if (xResult == APP_FTP_IO_ERROR)
    {
        prvFail(xError, xNowTick);
    }
    else if ((xResult == APP_FTP_IO_BUSY) &&
             prvHasExpired(s_xPhaseStartTick, xNowTick,
                           APP_FTP_REPLY_TIMEOUT_MS))
    {
        prvFail(APP_FTP_ERROR_TIMEOUT, xNowTick);
        return APP_FTP_IO_ERROR;
    }
    return xResult;
}

static void prvStartTransaction(TickType_t xNowTick)
{
    uint32_t ulTransactionId = s_xCommand.ulTransactionId;

    memset(&s_xPublicStatus, 0, sizeof(s_xPublicStatus));
    s_xPublicStatus.xState = APP_FTP_STATE_CONNECTING_CONTROL;
    s_xPublicStatus.xError = APP_FTP_ERROR_NONE;
    s_xPublicStatus.ulTransactionId = ulTransactionId;
    s_xPublicStatus.xAcceptingCommand = false;
    memset(&s_xReply, 0, sizeof(s_xReply));
    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    s_xHasPendingStorageRequest = false;
    s_xStoragePrepareSubmitted = false;
    s_xFinishSubmitted = false;
    s_xAbortSubmitted = false;
    s_ulControlSession = 0U;
    s_ulDataSession = 0U;
    s_usPassivePort = 0U;
    s_xTransactionStartTick = xNowTick;
    s_xLastDataTick = xNowTick;
    s_xLastStorageProgressTick = xNowTick;

    if ((prvCommandIsValid(&s_xCommand) == false) ||
        (prvBuildRemoteFile() == false))
    {
        prvFail(APP_FTP_ERROR_COMMAND_INVALID, xNowTick);
        return;
    }
    if ((s_xNetworkStatus.xChipReady == false) ||
        (s_xNetworkStatus.xPhyLinkUp == false))
    {
        prvFail(APP_FTP_ERROR_NETWORK_UNAVAILABLE, xNowTick);
        return;
    }

    prvSetPhase(APP_FTP_PHASE_START_CONTROL, xNowTick);
}

static void prvTryAcceptCommand(TickType_t xNowTick)
{
    if ((s_xPublicStatus.xAcceptingCommand == false) ||
        (xQueueReceive(s_xCommandQueue, &s_xCommand, 0U) != pdPASS))
    {
        return;
    }

    s_xPublicStatus.xAcceptingCommand = false;
    prvStartTransaction(xNowTick);
}

static bool prvStorageHasFailed(void)
{
    return prvStorageStatusMatches() &&
           ((s_xStorageStatus.xState == APP_STORAGE_STATE_FAILED) ||
            (s_xStorageStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT) ||
            (s_xStorageStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED));
}

static bool prvStorageReadyForNextBlock(void)
{
    return prvStorageStatusMatches() &&
           (s_xStorageStatus.xState == APP_STORAGE_STATE_READY_FOR_DATA) &&
           (s_xStorageStatus.ulWrittenLength ==
            s_xPublicStatus.ulReceivedLength);
}

static void prvServiceReceiveData(TickType_t xNowTick)
{
    int32_t lReceived;
    uint32_t ulProspectiveLength;

    if (prvStorageHasFailed())
    {
        prvFail(APP_FTP_ERROR_STORAGE_WRITE, xNowTick);
        return;
    }
    if (prvHasExpired(s_xLastStorageProgressTick, xNowTick,
                      APP_FTP_STORAGE_PROGRESS_TIMEOUT_MS))
    {
        prvFail(APP_FTP_ERROR_STORAGE_WRITE, xNowTick);
        return;
    }

    if (s_xHasPendingStorageRequest)
    {
        (void)prvTryQueueStorageRequest();
        return;
    }
    if (prvStorageReadyForNextBlock() == false)
    {
        return;
    }
    if (prvDataSessionIsUsable() == false)
    {
        prvFail(APP_FTP_ERROR_DATA_TRANSFER, xNowTick);
        return;
    }

    memset(&s_xPendingStorageRequest, 0, sizeof(s_xPendingStorageRequest));
    lReceived = AppTcpClient_Receive(
        &s_xDataTcpClient, s_xPendingStorageRequest.xData.xWrite.aucData,
        APP_STORAGE_WRITE_BLOCK_SIZE);
    if (lReceived > 0)
    {
        ulProspectiveLength = s_xPublicStatus.ulReceivedLength +
                              (uint32_t)lReceived;
        if (ulProspectiveLength > s_xPublicStatus.ulFirmwareLength)
        {
            prvFail(APP_FTP_ERROR_DATA_TRANSFER, xNowTick);
            return;
        }

        s_xPendingStorageRequest.xType = APP_STORAGE_REQUEST_WRITE_BLOCK;
        s_xPendingStorageRequest.ulTransactionId =
            s_xPublicStatus.ulTransactionId;
        s_xPendingStorageRequest.xData.xWrite.usLength = (uint16_t)lReceived;
        s_xHasPendingStorageRequest = true;
        s_xLastDataTick = xNowTick;
        (void)prvTryQueueStorageRequest();
    }
    else if (lReceived == APP_TCP_CLIENT_IO_PEER_CLOSED)
    {
        AppTcpClient_SetEnabled(&s_xDataTcpClient, false);
        prvSetPhase(APP_FTP_PHASE_WAIT_DATA_EOF, xNowTick);
    }
    else if (lReceived < 0)
    {
        prvFail(APP_FTP_ERROR_DATA_TRANSFER, xNowTick);
    }
    else if (prvHasExpired(s_xLastDataTick, xNowTick,
                           APP_FTP_DATA_IDLE_TIMEOUT_MS))
    {
        prvFail(APP_FTP_ERROR_TIMEOUT, xNowTick);
    }
}

static void prvServiceFtpTransaction(TickType_t xNowTick)
{
    AppFtpIoResult_t xIoResult;
    AppTcpClientState_t xTcpState;
    uint32_t ulFirmwareLength;

    if (s_xPhase == APP_FTP_PHASE_IDLE)
    {
        prvTryAcceptCommand(xNowTick);
        return;
    }
    if (s_xPhase == APP_FTP_PHASE_CLEANUP)
    {
        prvServiceCleanup();
        return;
    }

    /* Storage READY已经确定业务成功，后续QUIT不得再把结果翻成失败。 */
    if ((s_xPhase == APP_FTP_PHASE_WAIT_STORAGE_VERIFY) &&
        prvStorageVerificationSucceeded())
    {
        s_xStoragePrepareSubmitted = false;
        s_xPublicStatus.xState = APP_FTP_STATE_COMPLETED;
        s_xPublicStatus.xError = APP_FTP_ERROR_NONE;
        s_xPublicStatus.ulWrittenLength = s_xPublicStatus.ulFirmwareLength;
        s_xPublicStatus.ucProgressPercent = 100U;
        prvSetPhase(APP_FTP_PHASE_SEND_QUIT, xNowTick);
    }

    if ((s_xPhase != APP_FTP_PHASE_SEND_QUIT) &&
        (s_xPhase != APP_FTP_PHASE_WAIT_QUIT) &&
        ((s_xNetworkStatus.xChipReady == false) ||
         (s_xNetworkStatus.xPhyLinkUp == false)))
    {
        prvFail(APP_FTP_ERROR_NETWORK_UNAVAILABLE, xNowTick);
        return;
    }
    if ((s_xPhase != APP_FTP_PHASE_SEND_QUIT) &&
        (s_xPhase != APP_FTP_PHASE_WAIT_QUIT) &&
        prvHasExpired(s_xTransactionStartTick, xNowTick,
                      APP_FTP_TRANSACTION_TIMEOUT_MS))
    {
        prvFail(APP_FTP_ERROR_TIMEOUT, xNowTick);
        return;
    }

    switch (s_xPhase)
    {
        case APP_FTP_PHASE_START_CONTROL:
            if ((prvTcpIsDisabled(&s_xControlTcpClient) == false) ||
                (prvTcpIsDisabled(&s_xDataTcpClient) == false))
            {
                if (prvHasExpired(s_xPhaseStartTick, xNowTick,
                                  APP_FTP_CONTROL_CONNECT_TIMEOUT_MS))
                {
                    prvFail(APP_FTP_ERROR_CONTROL_CONNECT, xNowTick);
                }
                break;
            }
            if (AppTcpClient_SetServerEndpoint(&s_xControlTcpClient,
                                               s_xCommand.aucServerIp,
                                               s_xCommand.usServerPort) == false)
            {
                prvFail(APP_FTP_ERROR_CONTROL_CONNECT, xNowTick);
                break;
            }
            AppFtpClient_ResetSession(&s_xFtpClient);
            AppTcpClient_SetEnabled(&s_xControlTcpClient, true);
            prvSetPhase(APP_FTP_PHASE_WAIT_CONTROL, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_CONTROL:
            xTcpState = AppTcpClient_GetState(&s_xControlTcpClient);
            if (xTcpState == APP_TCP_CLIENT_STATE_CONNECTED)
            {
                s_ulControlSession =
                    AppTcpClient_GetSessionGeneration(&s_xControlTcpClient);
                s_xPublicStatus.xState = APP_FTP_STATE_AUTHENTICATING;
                prvSetPhase(APP_FTP_PHASE_WAIT_GREETING, xNowTick);
            }
            else if (prvHasExpired(s_xPhaseStartTick, xNowTick,
                                   APP_FTP_CONTROL_CONNECT_TIMEOUT_MS))
            {
                prvFail(APP_FTP_ERROR_CONTROL_CONNECT, xNowTick);
            }
            break;

        case APP_FTP_PHASE_WAIT_GREETING:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_GREETING, xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode == 220U)
                {
                    prvSetPhase(APP_FTP_PHASE_SEND_USER, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_GREETING, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_SEND_USER:
            prvServiceCommandSend("USER", 4U, s_xCommand.acUser,
                                  s_xCommand.usUserLength,
                                  APP_FTP_PHASE_WAIT_USER,
                                  APP_FTP_ERROR_AUTHENTICATION, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_USER:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_AUTHENTICATION,
                                            xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode == 230U)
                {
                    AppTcpClient_MarkSessionStable(&s_xControlTcpClient);
                    prvSetPhase(APP_FTP_PHASE_SEND_TYPE, xNowTick);
                }
                else if (s_xReply.usCode == 331U)
                {
                    prvSetPhase(APP_FTP_PHASE_SEND_PASS, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_AUTHENTICATION, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_SEND_PASS:
            prvServiceCommandSend("PASS", 4U, s_xCommand.acPassword,
                                  s_xCommand.usPasswordLength,
                                  APP_FTP_PHASE_WAIT_PASS,
                                  APP_FTP_ERROR_AUTHENTICATION, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_PASS:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_AUTHENTICATION,
                                            xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode == 230U)
                {
                    memset(s_xCommand.acPassword, 0,
                           sizeof(s_xCommand.acPassword));
                    memset(s_xFtpClient.aucTxBuffer, 0,
                           sizeof(s_xFtpClient.aucTxBuffer));
                    AppTcpClient_MarkSessionStable(&s_xControlTcpClient);
                    prvSetPhase(APP_FTP_PHASE_SEND_TYPE, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_AUTHENTICATION, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_SEND_TYPE:
            prvServiceCommandSend("TYPE", 4U, "I", 1U,
                                  APP_FTP_PHASE_WAIT_TYPE,
                                  APP_FTP_ERROR_BINARY_MODE, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_TYPE:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_BINARY_MODE, xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode == 200U)
                {
                    s_xPublicStatus.xState = APP_FTP_STATE_QUERYING_SIZE;
                    prvSetPhase(APP_FTP_PHASE_SEND_SIZE, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_BINARY_MODE, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_SEND_SIZE:
            prvServiceCommandSend("SIZE", 4U, s_acRemoteFile,
                                  prvRemoteFileLength(),
                                  APP_FTP_PHASE_WAIT_SIZE,
                                  APP_FTP_ERROR_REMOTE_SIZE, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_SIZE:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_REMOTE_SIZE,
                                            xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if ((s_xReply.usCode != 213U) ||
                    (prvParseUnsignedDecimal(s_xReply.acText,
                                             s_xReply.usTextLength,
                                             &ulFirmwareLength) == false) ||
                    (ulFirmwareLength <= APP_STORAGE_FIRMWARE_HEADER_SIZE) ||
                    (ulFirmwareLength > APP_STORAGE_FIRMWARE_MAX_SIZE) ||
                    (ulFirmwareLength > APP_STORAGE_CANDIDATE_CAPACITY))
                {
                    prvFail(APP_FTP_ERROR_REMOTE_SIZE, xNowTick);
                }
                else
                {
                    s_xPublicStatus.ulFirmwareLength = ulFirmwareLength;
                    s_xPublicStatus.xState = APP_FTP_STATE_PREPARING_STORAGE;
                    prvSetPhase(APP_FTP_PHASE_PREPARE_STORAGE, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_PREPARE_STORAGE:
            if ((s_xHasStorageStatus == false) ||
                (s_xStorageStatus.xState == APP_STORAGE_STATE_INITIALIZING))
            {
                if (prvHasExpired(s_xLastStorageProgressTick, xNowTick,
                                  APP_FTP_STORAGE_PROGRESS_TIMEOUT_MS))
                {
                    prvFail(APP_FTP_ERROR_STORAGE_PREPARE, xNowTick);
                }
                break;
            }
            if (prvStorageCanStartDownload() == false)
            {
                prvFail(APP_FTP_ERROR_STORAGE_PREPARE, xNowTick);
                break;
            }
            if (s_xHasPendingStorageRequest == false)
            {
                prvPrepareStorageRequest();
            }
            if (prvTryQueueStorageRequest())
            {
                s_xLastStorageProgressTick = xNowTick;
                prvSetPhase(APP_FTP_PHASE_WAIT_STORAGE_READY, xNowTick);
            }
            break;

        case APP_FTP_PHASE_WAIT_STORAGE_READY:
            if (prvStorageHasFailed())
            {
                prvFail(APP_FTP_ERROR_STORAGE_PREPARE, xNowTick);
            }
            else if (prvStorageReadyForNextBlock() &&
                     (s_xStorageStatus.ulFirmwareLength ==
                      s_xPublicStatus.ulFirmwareLength))
            {
                s_xPublicStatus.xState = APP_FTP_STATE_ENTERING_PASSIVE;
                prvSetPhase(APP_FTP_PHASE_SEND_EPSV, xNowTick);
            }
            else if (prvHasExpired(s_xLastStorageProgressTick, xNowTick,
                                   APP_FTP_STORAGE_PROGRESS_TIMEOUT_MS))
            {
                prvFail(APP_FTP_ERROR_STORAGE_PREPARE, xNowTick);
            }
            break;

        case APP_FTP_PHASE_SEND_EPSV:
            prvServiceCommandSend("EPSV", 4U, NULL, 0U,
                                  APP_FTP_PHASE_WAIT_EPSV,
                                  APP_FTP_ERROR_PASSIVE_RESPONSE, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_EPSV:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_PASSIVE_RESPONSE,
                                            xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode != 229U)
                {
                    prvFail(APP_FTP_ERROR_PASSIVE_RESPONSE, xNowTick);
                }
                else if (prvParseEpsvPort(&s_xReply, &s_usPassivePort) == false)
                {
                    prvFail(APP_FTP_ERROR_PASSIVE_PORT, xNowTick);
                }
                else
                {
                    AppTcpClient_SetEnabled(&s_xDataTcpClient, false);
                    prvSetPhase(APP_FTP_PHASE_STOP_DATA, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_STOP_DATA:
            AppTcpClient_SetEnabled(&s_xDataTcpClient, false);
            if (prvTcpIsDisabled(&s_xDataTcpClient))
            {
                prvSetPhase(APP_FTP_PHASE_CONFIGURE_DATA, xNowTick);
            }
            else if (prvHasExpired(s_xPhaseStartTick, xNowTick,
                                   APP_FTP_DATA_CONNECT_TIMEOUT_MS))
            {
                prvFail(APP_FTP_ERROR_DATA_CONNECT, xNowTick);
            }
            break;

        case APP_FTP_PHASE_CONFIGURE_DATA:
            if (AppTcpClient_SetServerEndpoint(&s_xDataTcpClient,
                                               s_xCommand.aucServerIp,
                                               s_usPassivePort) == false)
            {
                prvFail(APP_FTP_ERROR_PASSIVE_PORT, xNowTick);
            }
            else
            {
                s_xPublicStatus.xState = APP_FTP_STATE_CONNECTING_DATA;
                AppTcpClient_SetEnabled(&s_xDataTcpClient, true);
                prvSetPhase(APP_FTP_PHASE_WAIT_DATA, xNowTick);
            }
            break;

        case APP_FTP_PHASE_WAIT_DATA:
            xTcpState = AppTcpClient_GetState(&s_xDataTcpClient);
            if (xTcpState == APP_TCP_CLIENT_STATE_CONNECTED)
            {
                s_ulDataSession =
                    AppTcpClient_GetSessionGeneration(&s_xDataTcpClient);
                prvSetPhase(APP_FTP_PHASE_SEND_RETR, xNowTick);
            }
            else if (prvHasExpired(s_xPhaseStartTick, xNowTick,
                                   APP_FTP_DATA_CONNECT_TIMEOUT_MS))
            {
                prvFail(APP_FTP_ERROR_DATA_CONNECT, xNowTick);
            }
            break;

        case APP_FTP_PHASE_SEND_RETR:
            prvServiceCommandSend("RETR", 4U, s_acRemoteFile,
                                  prvRemoteFileLength(),
                                  APP_FTP_PHASE_WAIT_RETR,
                                  APP_FTP_ERROR_RETRIEVE, xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_RETR:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_RETRIEVE, xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if ((s_xReply.usCode == 125U) || (s_xReply.usCode == 150U))
                {
                    s_xPublicStatus.xState = APP_FTP_STATE_TRANSFERRING;
                    s_xLastDataTick = xNowTick;
                    s_xLastStorageProgressTick = xNowTick;
                    prvSetPhase(APP_FTP_PHASE_RECEIVE_DATA, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_RETRIEVE, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_RECEIVE_DATA:
            prvServiceReceiveData(xNowTick);
            break;

        case APP_FTP_PHASE_WAIT_DATA_EOF:
            if (s_xHasPendingStorageRequest ||
                (s_xPublicStatus.ulReceivedLength !=
                 s_xPublicStatus.ulFirmwareLength) ||
                (prvStorageReadyForNextBlock() == false))
            {
                prvFail(APP_FTP_ERROR_DATA_TRANSFER, xNowTick);
            }
            else
            {
                prvSetPhase(APP_FTP_PHASE_WAIT_TRANSFER_COMPLETE, xNowTick);
            }
            break;

        case APP_FTP_PHASE_WAIT_TRANSFER_COMPLETE:
            xIoResult = prvPollControlReply(APP_FTP_ERROR_TRANSFER_COMPLETE,
                                            xNowTick);
            if (xIoResult == APP_FTP_IO_COMPLETE)
            {
                if (s_xReply.usCode == 226U)
                {
                    s_xPublicStatus.xState = APP_FTP_STATE_VERIFYING;
                    prvSetPhase(APP_FTP_PHASE_FINISH_STORAGE, xNowTick);
                }
                else
                {
                    prvFail(APP_FTP_ERROR_TRANSFER_COMPLETE, xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_FINISH_STORAGE:
            /* FINISH排在最后一个WRITE之后，Storage按Queue顺序处理。 */
            if (s_xHasPendingStorageRequest == false)
            {
                prvPrepareFinishRequest();
            }
            if (prvTryQueueStorageRequest())
            {
                s_xLastStorageProgressTick = xNowTick;
                prvSetPhase(APP_FTP_PHASE_WAIT_STORAGE_VERIFY, xNowTick);
            }
            break;

        case APP_FTP_PHASE_WAIT_STORAGE_VERIFY:
            if (prvStorageHasFailed())
            {
                prvFail(APP_FTP_ERROR_STORAGE_VERIFY, xNowTick);
            }
            else if (prvHasExpired(s_xLastStorageProgressTick, xNowTick,
                                   APP_FTP_STORAGE_PROGRESS_TIMEOUT_MS))
            {
                prvFail(APP_FTP_ERROR_STORAGE_VERIFY, xNowTick);
            }
            break;

        case APP_FTP_PHASE_SEND_QUIT:
            if (prvControlSessionIsUsable(true) == false)
            {
                prvComplete(xNowTick);
            }
            else
            {
                prvServiceCommandSend("QUIT", 4U, NULL, 0U,
                                      APP_FTP_PHASE_WAIT_QUIT,
                                      APP_FTP_ERROR_NONE, xNowTick);
                if (s_xPublicStatus.xState == APP_FTP_STATE_FAILED)
                {
                    /* 固件已验证，QUIT失败只影响连接清理，不反转结果。 */
                    prvComplete(xNowTick);
                }
            }
            break;

        case APP_FTP_PHASE_WAIT_QUIT:
            if (prvControlSessionIsUsable(false) == false)
            {
                prvComplete(xNowTick);
                break;
            }
            xIoResult = AppFtpClient_PollReply(&s_xFtpClient, &s_xReply);
            if ((xIoResult == APP_FTP_IO_COMPLETE) ||
                (xIoResult == APP_FTP_IO_ERROR) ||
                prvHasExpired(s_xPhaseStartTick, xNowTick,
                              APP_FTP_REPLY_TIMEOUT_MS))
            {
                prvComplete(xNowTick);
            }
            break;

        case APP_FTP_PHASE_IDLE:
        case APP_FTP_PHASE_CLEANUP:
        default:
            break;
    }
}

static void prvFtpTask(void *pvParameters)
{
    TickType_t xNowTick;

    (void)pvParameters;
    memset(&s_xPublicStatus, 0, sizeof(s_xPublicStatus));
    s_xPublicStatus.xState = APP_FTP_STATE_IDLE;
    s_xPublicStatus.xError = APP_FTP_ERROR_NONE;
    s_xPublicStatus.xAcceptingCommand = true;
    prvPublishStatusIfChanged();

    for (;;)
    {
        xNowTick = xTaskGetTickCount();
        prvRefreshNetworkStatus();
        prvRefreshStorageStatus(xNowTick);

        AppTcpClient_Service(&s_xControlTcpClient,
                             s_xNetworkStatus.xPhyLinkUp, xNowTick);
        AppTcpClient_Service(&s_xDataTcpClient,
                             s_xNetworkStatus.xPhyLinkUp, xNowTick);

        prvServiceFtpTransaction(xNowTick);
        prvPublishStatusIfChanged();
        vTaskDelay(prvMillisecondsToTicks(APP_FTP_SERVICE_PERIOD_MS));
    }
}

bool AppFtpTask_Create(RtosW5500Access_t *pxW5500Access,
                       QueueHandle_t xNetworkStatusQueue,
                       QueueHandle_t xCommandQueue,
                       QueueHandle_t xStatusQueue,
                       QueueHandle_t xStorageRequestQueue,
                       QueueHandle_t xStorageStatusQueue)
{
    if (s_xTaskCreated || (pxW5500Access == NULL) ||
        (xNetworkStatusQueue == NULL) || (xCommandQueue == NULL) ||
        (xStatusQueue == NULL) || (xStorageRequestQueue == NULL) ||
        (xStorageStatusQueue == NULL))
    {
        return false;
    }

    if ((AppTcpClient_Init(&s_xControlTcpClient, &s_xControlTcpConfig,
                           pxW5500Access) == false) ||
        (AppTcpClient_Init(&s_xDataTcpClient, &s_xDataTcpConfig,
                           pxW5500Access) == false) ||
        (AppFtpClient_Init(&s_xFtpClient, &s_xControlTcpClient) == false))
    {
        return false;
    }

    s_xNetworkStatusQueue = xNetworkStatusQueue;
    s_xCommandQueue = xCommandQueue;
    s_xStatusQueue = xStatusQueue;
    s_xStorageRequestQueue = xStorageRequestQueue;
    s_xStorageStatusQueue = xStorageStatusQueue;

    if (xTaskCreate(prvFtpTask, "ftp", APP_FTP_TASK_STACK_WORDS, NULL,
                    APP_FTP_TASK_PRIORITY, NULL) != pdPASS)
    {
        s_xNetworkStatusQueue = NULL;
        s_xCommandQueue = NULL;
        s_xStatusQueue = NULL;
        s_xStorageRequestQueue = NULL;
        s_xStorageStatusQueue = NULL;
        return false;
    }

    s_xTaskCreated = true;
    return true;
}
