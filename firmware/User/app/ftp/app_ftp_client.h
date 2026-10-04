#ifndef APP_FTP_CLIENT_H
#define APP_FTP_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "app_tcp_client.h"

#define APP_FTP_CONTROL_TX_BUFFER_SIZE    256U
#define APP_FTP_CONTROL_RX_BUFFER_SIZE    512U
#define APP_FTP_REPLY_TEXT_MAX_LENGTH     480U

typedef enum
{
    APP_FTP_IO_ERROR = -1,
    APP_FTP_IO_BUSY = 0,
    APP_FTP_IO_COMPLETE = 1
} AppFtpIoResult_t;

typedef struct
{
    uint16_t usCode;
    uint16_t usTextLength;
    char acText[APP_FTP_REPLY_TEXT_MAX_LENGTH + 1U];
} AppFtpReply_t;

typedef struct
{
    AppTcpClient_t *pxControlTcpClient;
    uint8_t aucTxBuffer[APP_FTP_CONTROL_TX_BUFFER_SIZE];
    uint8_t aucRxBuffer[APP_FTP_CONTROL_RX_BUFFER_SIZE];
    uint16_t usTxLength;
    uint16_t usTxOffset;
    uint16_t usRxLength;
    uint16_t usMultilineCode;
    bool xMultilineActive;
    bool xInitialized;
} AppFtpClient_t;

bool AppFtpClient_Init(AppFtpClient_t *pxClient, AppTcpClient_t *pxControlTcpClient);
void AppFtpClient_ResetSession(AppFtpClient_t *pxClient);
bool AppFtpClient_PrepareCommand(AppFtpClient_t *pxClient, const char *pcVerb,
                                 uint16_t usVerbLength, const char *pcArgument,
                                 uint16_t usArgumentLength);
AppFtpIoResult_t AppFtpClient_ServiceSend(AppFtpClient_t *pxClient);
AppFtpIoResult_t AppFtpClient_PollReply(AppFtpClient_t *pxClient, AppFtpReply_t *pxReply);

#endif /* APP_FTP_CLIENT_H */
