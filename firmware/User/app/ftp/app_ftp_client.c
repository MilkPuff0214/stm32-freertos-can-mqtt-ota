#include "app_ftp_client.h"

#include <stddef.h>
#include <string.h>

static bool prvContainsLineBreak(const char *pcText, uint16_t usLength)
{
    uint16_t usIndex;

    for (usIndex = 0U; usIndex < usLength; usIndex++)
    {
        if ((pcText[usIndex] == '\r') || (pcText[usIndex] == '\n'))
        {
            return true;
        }
    }

    return false;
}

static int32_t prvFindLineEnd(const uint8_t *pucData, uint16_t usLength)
{
    uint16_t usIndex;

    for (usIndex = 0U; (usIndex + 1U) < usLength; usIndex++)
    {
        if ((pucData[usIndex] == '\r') && (pucData[usIndex + 1U] == '\n'))
        {
            return (int32_t)usIndex;
        }
    }

    return -1;
}

static bool prvParseReplyCode(const uint8_t *pucLine, uint16_t usLineLength,
                              uint16_t *pusCode)
{
    if ((usLineLength < 4U) || (pucLine[0] < '0') || (pucLine[0] > '9') ||
        (pucLine[1] < '0') || (pucLine[1] > '9') ||
        (pucLine[2] < '0') || (pucLine[2] > '9'))
    {
        return false;
    }

    *pusCode = (uint16_t)(((uint16_t)(pucLine[0] - '0') * 100U) +
                          ((uint16_t)(pucLine[1] - '0') * 10U) +
                          (uint16_t)(pucLine[2] - '0'));
    return true;
}

static void prvConsumeLine(AppFtpClient_t *pxClient, uint16_t usLineLength)
{
    uint16_t usConsumedLength = (uint16_t)(usLineLength + 2U);
    uint16_t usRemainingLength = (uint16_t)(pxClient->usRxLength - usConsumedLength);

    if (usRemainingLength > 0U)
    {
        memmove(pxClient->aucRxBuffer, &pxClient->aucRxBuffer[usConsumedLength],
                usRemainingLength);
    }
    pxClient->usRxLength = usRemainingLength;
}

static AppFtpIoResult_t prvTryExtractReply(AppFtpClient_t *pxClient,
                                           AppFtpReply_t *pxReply)
{
    int32_t lLineEnd;
    uint16_t usCode;
    uint16_t usLineLength;
    uint16_t usTextLength;
    uint8_t ucSeparator;

    for (;;)
    {
        lLineEnd = prvFindLineEnd(pxClient->aucRxBuffer, pxClient->usRxLength);
        if (lLineEnd < 0)
        {
            return APP_FTP_IO_BUSY;
        }

        usLineLength = (uint16_t)lLineEnd;
        if (pxClient->xMultilineActive)
        {
            if ((usLineLength >= 4U) &&
                prvParseReplyCode(pxClient->aucRxBuffer, usLineLength, &usCode) &&
                (usCode == pxClient->usMultilineCode) &&
                (pxClient->aucRxBuffer[3] == ' '))
            {
                pxClient->xMultilineActive = false;
            }
            else
            {
                prvConsumeLine(pxClient, usLineLength);
                continue;
            }
        }
        else
        {
            if (prvParseReplyCode(pxClient->aucRxBuffer, usLineLength, &usCode) == false)
            {
                return APP_FTP_IO_ERROR;
            }

            ucSeparator = pxClient->aucRxBuffer[3];
            if (ucSeparator == '-')
            {
                pxClient->usMultilineCode = usCode;
                pxClient->xMultilineActive = true;
                prvConsumeLine(pxClient, usLineLength);
                continue;
            }
            if (ucSeparator != ' ')
            {
                return APP_FTP_IO_ERROR;
            }
        }

        if (prvParseReplyCode(pxClient->aucRxBuffer, usLineLength, &usCode) == false)
        {
            return APP_FTP_IO_ERROR;
        }

        usTextLength = (uint16_t)(usLineLength - 4U);
        if (usTextLength > APP_FTP_REPLY_TEXT_MAX_LENGTH)
        {
            return APP_FTP_IO_ERROR;
        }

        memset(pxReply, 0, sizeof(*pxReply));
        pxReply->usCode = usCode;
        pxReply->usTextLength = usTextLength;
        if (usTextLength > 0U)
        {
            memcpy(pxReply->acText, &pxClient->aucRxBuffer[4], usTextLength);
        }
        pxReply->acText[usTextLength] = '\0';
        prvConsumeLine(pxClient, usLineLength);
        return APP_FTP_IO_COMPLETE;
    }
}

bool AppFtpClient_Init(AppFtpClient_t *pxClient, AppTcpClient_t *pxControlTcpClient)
{
    if ((pxClient == NULL) || (pxControlTcpClient == NULL))
    {
        return false;
    }

    memset(pxClient, 0, sizeof(*pxClient));
    pxClient->pxControlTcpClient = pxControlTcpClient;
    pxClient->xInitialized = true;
    return true;
}

void AppFtpClient_ResetSession(AppFtpClient_t *pxClient)
{
    AppTcpClient_t *pxControlTcpClient;

    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return;
    }

    pxControlTcpClient = pxClient->pxControlTcpClient;
    memset(pxClient, 0, sizeof(*pxClient));
    pxClient->pxControlTcpClient = pxControlTcpClient;
    pxClient->xInitialized = true;
}

bool AppFtpClient_PrepareCommand(AppFtpClient_t *pxClient, const char *pcVerb,
                                 uint16_t usVerbLength, const char *pcArgument,
                                 uint16_t usArgumentLength)
{
    uint32_t ulRequiredLength;

    if ((pxClient == NULL) || (pxClient->xInitialized == false) ||
        (pcVerb == NULL) || (usVerbLength == 0U) ||
        ((pcArgument == NULL) && (usArgumentLength != 0U)) ||
        (pxClient->usTxLength != 0U) || prvContainsLineBreak(pcVerb, usVerbLength) ||
        ((usArgumentLength > 0U) && prvContainsLineBreak(pcArgument, usArgumentLength)))
    {
        return false;
    }

    ulRequiredLength = (uint32_t)usVerbLength + 2U;
    if (usArgumentLength > 0U)
    {
        ulRequiredLength += 1U + usArgumentLength;
    }
    if (ulRequiredLength > APP_FTP_CONTROL_TX_BUFFER_SIZE)
    {
        return false;
    }

    memcpy(pxClient->aucTxBuffer, pcVerb, usVerbLength);
    pxClient->usTxLength = usVerbLength;
    if (usArgumentLength > 0U)
    {
        pxClient->aucTxBuffer[pxClient->usTxLength++] = ' ';
        memcpy(&pxClient->aucTxBuffer[pxClient->usTxLength], pcArgument,
               usArgumentLength);
        pxClient->usTxLength = (uint16_t)(pxClient->usTxLength + usArgumentLength);
    }
    pxClient->aucTxBuffer[pxClient->usTxLength++] = '\r';
    pxClient->aucTxBuffer[pxClient->usTxLength++] = '\n';
    pxClient->usTxOffset = 0U;
    return true;
}

AppFtpIoResult_t AppFtpClient_ServiceSend(AppFtpClient_t *pxClient)
{
    int32_t lSentLength;
    uint16_t usRemainingLength;

    if ((pxClient == NULL) || (pxClient->xInitialized == false) ||
        (pxClient->usTxLength == 0U) || (pxClient->usTxOffset >= pxClient->usTxLength))
    {
        return APP_FTP_IO_ERROR;
    }

    usRemainingLength = (uint16_t)(pxClient->usTxLength - pxClient->usTxOffset);
    lSentLength = AppTcpClient_Send(pxClient->pxControlTcpClient,
                                   &pxClient->aucTxBuffer[pxClient->usTxOffset],
                                   usRemainingLength);
    if (lSentLength < 0)
    {
        pxClient->usTxLength = 0U;
        pxClient->usTxOffset = 0U;
        return APP_FTP_IO_ERROR;
    }
    if (lSentLength == 0)
    {
        return APP_FTP_IO_BUSY;
    }
    if ((uint32_t)lSentLength > usRemainingLength)
    {
        return APP_FTP_IO_ERROR;
    }

    pxClient->usTxOffset = (uint16_t)(pxClient->usTxOffset + (uint16_t)lSentLength);
    if (pxClient->usTxOffset < pxClient->usTxLength)
    {
        return APP_FTP_IO_BUSY;
    }

    pxClient->usTxLength = 0U;
    pxClient->usTxOffset = 0U;
    return APP_FTP_IO_COMPLETE;
}

AppFtpIoResult_t AppFtpClient_PollReply(AppFtpClient_t *pxClient, AppFtpReply_t *pxReply)
{
    AppFtpIoResult_t xResult;
    int32_t lReceivedLength;
    uint16_t usRemainingCapacity;

    if ((pxClient == NULL) || (pxReply == NULL) || (pxClient->xInitialized == false))
    {
        return APP_FTP_IO_ERROR;
    }

    xResult = prvTryExtractReply(pxClient, pxReply);
    if (xResult != APP_FTP_IO_BUSY)
    {
        return xResult;
    }
    if (pxClient->usRxLength >= APP_FTP_CONTROL_RX_BUFFER_SIZE)
    {
        return APP_FTP_IO_ERROR;
    }

    usRemainingCapacity = (uint16_t)(APP_FTP_CONTROL_RX_BUFFER_SIZE - pxClient->usRxLength);
    lReceivedLength = AppTcpClient_Receive(pxClient->pxControlTcpClient,
                                          &pxClient->aucRxBuffer[pxClient->usRxLength],
                                          usRemainingCapacity);
    if (lReceivedLength < 0)
    {
        return APP_FTP_IO_ERROR;
    }
    if (lReceivedLength == 0)
    {
        return APP_FTP_IO_BUSY;
    }
    if ((uint32_t)lReceivedLength > usRemainingCapacity)
    {
        return APP_FTP_IO_ERROR;
    }

    pxClient->usRxLength = (uint16_t)(pxClient->usRxLength + (uint16_t)lReceivedLength);
    return prvTryExtractReply(pxClient, pxReply);
}
