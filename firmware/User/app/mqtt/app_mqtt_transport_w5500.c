#include "app_mqtt_transport_w5500.h"

#include <stddef.h>
#include "task.h"

static int32_t prvTransportSend(
    NetworkContext_t *pxNetworkContext,
    const void *pvBuffer,
    size_t xBytesToSend)
{
    int32_t lResult;

    if ((pxNetworkContext == NULL) ||
        (pxNetworkContext->pxTcpClient == NULL) ||
        ((pvBuffer == NULL) && (xBytesToSend > 0U)))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    if (xBytesToSend == 0U)
    {
        return 0;
    }

    lResult = AppTcpClient_Send(pxNetworkContext->pxTcpClient, pvBuffer, xBytesToSend);

    if (lResult == 0)
    {
        vTaskDelay(1U);
    }
    return lResult;
}

static int32_t prvTransportReceive(
    NetworkContext_t *pxNetworkContext,
    void *pvBuffer,
    size_t xBytesToReceive)
{
    int32_t lResult;
    if ((pxNetworkContext == NULL) ||
        (pxNetworkContext->pxTcpClient == NULL) ||
        ((pvBuffer == NULL) && (xBytesToReceive > 0U)))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    if (xBytesToReceive == 0U)
    {
        return 0;
    }

    lResult = AppTcpClient_Receive(pxNetworkContext->pxTcpClient, pvBuffer, xBytesToReceive);

    if (lResult == 0)
    {
        vTaskDelay(1U);
    }

    return lResult;
}

bool AppMqttTransportW5500_Init(NetworkContext_t *pxNetworkContext,
                                TransportInterface_t *pxTransportInterface)
{
    if ((pxNetworkContext == NULL) ||
        (pxNetworkContext->pxTcpClient == NULL) ||
        (pxTransportInterface == NULL))
    {
        return false;
    }

    pxTransportInterface->pNetworkContext = pxNetworkContext;
    pxTransportInterface->send = prvTransportSend;
    pxTransportInterface->recv = prvTransportReceive;
    pxTransportInterface->writev = NULL;

    return true;
}
