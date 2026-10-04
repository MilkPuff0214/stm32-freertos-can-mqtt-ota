#include "app_tcp_client.h"

#include <limits.h>
#include <string.h>

#include "task.h"

#include "bsp_w5500.h"

#define APP_TCP_CLIENT_SOCKET_COUNT              8U

//关闭完成后的下一步动作,等网线、立即打开、进入退避
#define APP_TCP_CLIENT_POST_CLOSE_WAIT_LINK      0U
#define APP_TCP_CLIENT_POST_CLOSE_OPEN           1U
#define APP_TCP_CLIENT_POST_CLOSE_BACKOFF        2U

static TickType_t prvMillisecondsToTicks(uint32_t ulMilliseconds)
{
    TickType_t xTicks = pdMS_TO_TICKS(ulMilliseconds);

    if (xTicks == 0U)
    {
        xTicks = 1U;
    }
    return xTicks;
}

static bool prvHasExpired(
    TickType_t xStartTick,
    TickType_t xNowTick,
    uint32_t ulTimeoutMs)
{
    return (TickType_t)(xNowTick - xStartTick) >=
        prvMillisecondsToTicks(ulTimeoutMs);
}

static void prvSetState(
    AppTcpClient_t *pxClient,
    AppTcpClientState_t xState,
    TickType_t xNowTick)
{
    pxClient->xState = xState;
    pxClient->xPhaseStartTick = xNowTick;
}

static bool prvConfigIsValid(const AppTcpClientConfig_t *pxConfig)
{
    if ((pxConfig == NULL) ||
        (pxConfig->ucSocket >= APP_TCP_CLIENT_SOCKET_COUNT) ||
        (pxConfig->aucServerIp[0] == 0U) ||
        (pxConfig->aucServerIp[0] >= 224U) ||
        (pxConfig->usServerPort == 0U) ||
        (pxConfig->usLocalPortFirst == 0U) ||
        (pxConfig->usLocalPortFirst > pxConfig->usLocalPortLast) ||
        (pxConfig->ulLocalActionTimeoutMs == 0U) ||
        (pxConfig->ulConnectTimeoutMs == 0U) ||
        (pxConfig->ulSendTimeoutMs == 0U) ||
        (pxConfig->ulPeerDrainTimeoutMs == 0U) ||
        (pxConfig->ulDisconnectTimeoutMs == 0U))
    {
        return false;
    }
    return true;
}

static uint32_t prvCalculateRetryDelayMs(uint32_t ulFailureCount)
{
    uint32_t ulDelayMs = APP_TCP_CLIENT_RETRY_BASE_MS;
    uint32_t ulIndex;

    if (ulFailureCount == 0U)
    {
        return APP_TCP_CLIENT_RETRY_BASE_MS;
    }

    for (ulIndex = 1U;
         (ulIndex < ulFailureCount) &&
         (ulDelayMs < APP_TCP_CLIENT_RETRY_MAX_MS);
         ulIndex++)
    {
        if (ulDelayMs >
            (APP_TCP_CLIENT_RETRY_MAX_MS / 2U))
        {
            ulDelayMs = APP_TCP_CLIENT_RETRY_MAX_MS;
        }
        else
        {
            ulDelayMs *= 2U;
        }
    }

    if (ulDelayMs > APP_TCP_CLIENT_RETRY_MAX_MS)
    {
        ulDelayMs = APP_TCP_CLIENT_RETRY_MAX_MS;
    }
    return ulDelayMs;
}

static void prvResetConnectionFlags(AppTcpClient_t *pxClient)
{
    pxClient->xTxInFlight = false;
    pxClient->xPeerReceiveDrained = false;
    pxClient->xGracefulCloseRequested = false;
    pxClient->xReconnectRequested = false;
    pxClient->xSendBusy = false;
    pxClient->xReceiveBusy = false;
}

static void prvResetBackoff(AppTcpClient_t *pxClient)
{
    pxClient->ulConsecutiveFailures = 0U;
    pxClient->ulRetryDelayMs = 0U;
}

static void prvRegisterFailure(AppTcpClient_t *pxClient)
{
    if (pxClient->ulConsecutiveFailures < UINT32_MAX)
    {
        pxClient->ulConsecutiveFailures++;
    }

    pxClient->ulRetryDelayMs = prvCalculateRetryDelayMs(
        pxClient->ulConsecutiveFailures);
    pxClient->xSessionMarkedStable = false;
}

static void prvBeginForcedClose(
    AppTcpClient_t *pxClient,
    uint8_t ucPostCloseAction,
    TickType_t xNowTick)
{
    prvResetConnectionFlags(pxClient);
    pxClient->ucPostCloseAction = ucPostCloseAction;
    prvSetState(
        pxClient,
        APP_TCP_CLIENT_STATE_FORCE_CLOSE_REQUEST,
        xNowTick);
}

static void prvEnterBackoff(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    if (pxClient->ulRetryDelayMs == 0U)
    {
        pxClient->ulRetryDelayMs = APP_TCP_CLIENT_RETRY_BASE_MS;
    }
    prvSetState(
        pxClient,
        APP_TCP_CLIENT_STATE_BACKOFF,
        xNowTick);
}

static void prvFailAndClose(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    prvRegisterFailure(pxClient);
    prvBeginForcedClose(
        pxClient,
        APP_TCP_CLIENT_POST_CLOSE_BACKOFF,
        xNowTick);
}

static void prvPrepareOpen(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    pxClient->usActiveLocalPort = pxClient->usNextLocalPort;

    if (pxClient->usNextLocalPort >=
        pxClient->xConfig.usLocalPortLast)
    {
        pxClient->usNextLocalPort =
            pxClient->xConfig.usLocalPortFirst;
    }
    else
    {
        pxClient->usNextLocalPort++;
    }

    prvSetState(
        pxClient,
        APP_TCP_CLIENT_STATE_OPEN_REQUEST,
        xNowTick);
}

static void prvHandleForcedCloseFailure(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    if (pxClient->ucPostCloseAction ==
        APP_TCP_CLIENT_POST_CLOSE_WAIT_LINK)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_WAIT_LINK,
            xNowTick);
        return;
    }

    /*
     * BACKOFF动作说明触发关闭之前已经登记过本次失败。
     * OPEN动作则是一次新的关闭/恢复失败，需要登记。
     */
    if (pxClient->ucPostCloseAction !=
        APP_TCP_CLIENT_POST_CLOSE_BACKOFF)
    {
        prvRegisterFailure(pxClient);
    }
    prvEnterBackoff(pxClient, xNowTick);
}

static void prvFinishForcedClose(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    if (BspW5500_TcpClearEvents(
            pxClient->xConfig.ucSocket) == false)
    {
        prvHandleForcedCloseFailure(pxClient, xNowTick);
        return;
    }

    if (pxClient->ucPostCloseAction ==
        APP_TCP_CLIENT_POST_CLOSE_WAIT_LINK)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_WAIT_LINK,
            xNowTick);
    }
    else if (pxClient->ucPostCloseAction ==
             APP_TCP_CLIENT_POST_CLOSE_OPEN)
    {
        prvPrepareOpen(pxClient, xNowTick);
    }
    else
    {
        prvEnterBackoff(pxClient, xNowTick);
    }
}

static void prvServiceForcedClose(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500OperationResult_t xOperationResult;
    BspW5500TcpStatus_t xStatus;

    if (pxClient->xState ==
        APP_TCP_CLIENT_STATE_FORCE_CLOSE_REQUEST)
    {
        xOperationResult = BspW5500_TcpCloseStart(
            pxClient->xConfig.ucSocket);

        if (xOperationResult ==
            BSP_W5500_OPERATION_STARTED)
        {
            prvSetState(
                pxClient,
                APP_TCP_CLIENT_STATE_FORCE_CLOSE_WAIT,
                xNowTick);
        }
        else if ((xOperationResult ==
                  BSP_W5500_OPERATION_ERROR) ||
                 prvHasExpired(
                     pxClient->xPhaseStartTick,
                     xNowTick,
                     pxClient->xConfig.ulLocalActionTimeoutMs))
        {
            prvHandleForcedCloseFailure(pxClient, xNowTick);
        }
        return;
    }

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) &&
        (xStatus.xState == BSP_W5500_TCP_CLOSED))
    {
        prvFinishForcedClose(pxClient, xNowTick);
    }
    else if (prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulLocalActionTimeoutMs))
    {
        prvHandleForcedCloseFailure(pxClient, xNowTick);
    }
}

static bool prvPollSendInFlight(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500IoResult_t xResult;

    if (pxClient->xTxInFlight == false)
    {
        return true;
    }

    xResult = BspW5500_TcpPollSend(
        pxClient->xConfig.ucSocket);

    if (xResult == BSP_W5500_IO_COMPLETE)
    {
        pxClient->xTxInFlight = false;
        return true;
    }

    if ((xResult == BSP_W5500_IO_ERROR) ||
        prvHasExpired(
            pxClient->xTxStartTick,
            xNowTick,
            pxClient->xConfig.ulSendTimeoutMs))
    {
        pxClient->xTxInFlight = false;
        pxClient->xReconnectRequested = true;
        return false;
    }
    return true;
}

static void prvEnterConnected(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    if (BspW5500_TcpClearConnectedEvent(
            pxClient->xConfig.ucSocket) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    prvResetConnectionFlags(pxClient);
    pxClient->xSessionMarkedStable = false;
    pxClient->ulSessionGeneration++;
    prvSetState(
        pxClient,
        APP_TCP_CLIENT_STATE_CONNECTED,
        xNowTick);
}

static void prvServiceOpenRequest(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500OperationResult_t xResult;

    xResult = BspW5500_TcpOpenStart(
        pxClient->xConfig.ucSocket,
        pxClient->usActiveLocalPort);

    if (xResult == BSP_W5500_OPERATION_STARTED)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_OPEN_WAIT,
            xNowTick);
    }
    else if ((xResult == BSP_W5500_OPERATION_ERROR) ||
             prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulLocalActionTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceOpenWait(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500TcpStatus_t xStatus;

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (xStatus.xState == BSP_W5500_TCP_INIT)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_CONNECT_REQUEST,
            xNowTick);
    }
    else if ((xStatus.xState != BSP_W5500_TCP_CLOSED) ||
             prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulLocalActionTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceConnectRequest(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500OperationResult_t xResult;

    xResult = BspW5500_TcpConnectStart(
        pxClient->xConfig.ucSocket,
        pxClient->xConfig.aucServerIp,
        pxClient->xConfig.usServerPort);

    if (xResult == BSP_W5500_OPERATION_STARTED)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_CONNECTING,
            xNowTick);
    }
    else if ((xResult == BSP_W5500_OPERATION_ERROR) ||
             prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulLocalActionTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceConnecting(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500TcpStatus_t xStatus;

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (xStatus.xTimedOut)
    {
        prvFailAndClose(pxClient, xNowTick);
    }
    else if ((xStatus.xState == BSP_W5500_TCP_ESTABLISHED) ||
             (xStatus.xState == BSP_W5500_TCP_PEER_CLOSED_SEND))
    {
        prvEnterConnected(pxClient, xNowTick);
        /* 对端可能在两次任务采样之间完成握手并发送FIN，仍需交付尾部。 */
        if ((pxClient->xState == APP_TCP_CLIENT_STATE_CONNECTED) &&
            (xStatus.xState == BSP_W5500_TCP_PEER_CLOSED_SEND))
        {
            prvSetState(pxClient, APP_TCP_CLIENT_STATE_PEER_FIN, xNowTick);
        }
    }
    else if ((xStatus.xState ==
              BSP_W5500_TCP_CLOSED) ||
             (xStatus.xState ==
              BSP_W5500_TCP_CLOSING) ||
             (xStatus.xState ==
              BSP_W5500_TCP_OTHER) ||
             prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulConnectTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceConnected(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500TcpStatus_t xStatus;

    (void)prvPollSendInFlight(pxClient, xNowTick);
    if (pxClient->xReconnectRequested)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (xStatus.xTimedOut ||
        (xStatus.xState == BSP_W5500_TCP_CLOSED) ||
        (xStatus.xState == BSP_W5500_TCP_CLOSING) ||
        (xStatus.xState == BSP_W5500_TCP_OTHER))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
    else if (xStatus.xState ==
             BSP_W5500_TCP_PEER_CLOSED_SEND)
    {
        pxClient->xPeerReceiveDrained = false;
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_PEER_FIN,
            xNowTick);
    }
    else if (xStatus.xState !=
             BSP_W5500_TCP_ESTABLISHED)
    {
        prvFailAndClose(pxClient, xNowTick);
    }
    else if (pxClient->xGracefulCloseRequested &&
             (pxClient->xTxInFlight == false))
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_DISCONNECT_REQUEST,
            xNowTick);
    }
}

static void prvServicePeerFin(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500TcpStatus_t xStatus;

    (void)prvPollSendInFlight(pxClient, xNowTick);
    if (pxClient->xReconnectRequested)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (xStatus.xTimedOut ||
        (xStatus.xState !=
         BSP_W5500_TCP_PEER_CLOSED_SEND))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
    else if (pxClient->xGracefulCloseRequested &&
             pxClient->xPeerReceiveDrained &&
             (pxClient->xTxInFlight == false))
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_DISCONNECT_REQUEST,
            xNowTick);
    }
    else if (prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulPeerDrainTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceDisconnectRequest(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500OperationResult_t xResult;

    if (pxClient->xTxInFlight)
    {
        (void)prvPollSendInFlight(pxClient, xNowTick);
        if (pxClient->xReconnectRequested)
        {
            prvFailAndClose(pxClient, xNowTick);
        }
        return;
    }

    xResult = BspW5500_TcpDisconnectStart(
        pxClient->xConfig.ucSocket);

    if (xResult == BSP_W5500_OPERATION_STARTED)
    {
        prvSetState(
            pxClient,
            APP_TCP_CLIENT_STATE_DISCONNECTING,
            xNowTick);
    }
    else if ((xResult == BSP_W5500_OPERATION_ERROR) ||
             prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulLocalActionTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

static void prvServiceDisconnecting(
    AppTcpClient_t *pxClient,
    TickType_t xNowTick)
{
    BspW5500TcpStatus_t xStatus;

    if (BspW5500_TcpReadStatus(
            pxClient->xConfig.ucSocket,
            &xStatus) == false)
    {
        prvFailAndClose(pxClient, xNowTick);
        return;
    }

    if (xStatus.xTimedOut)
    {
        /* TIMEOUT和CLOSED可同时出现，不能把超时关闭记作正常FIN完成。 */
        prvFailAndClose(pxClient, xNowTick);
    }
    else if (xStatus.xState == BSP_W5500_TCP_CLOSED)
    {
        if (BspW5500_TcpClearEvents(
                pxClient->xConfig.ucSocket) == false)
        {
            prvFailAndClose(pxClient, xNowTick);
            return;
        }

        if (pxClient->xSessionMarkedStable == false)
        {
            prvRegisterFailure(pxClient);
        }
        prvResetConnectionFlags(pxClient);
        prvEnterBackoff(pxClient, xNowTick);
    }
    else if (prvHasExpired(
                 pxClient->xPhaseStartTick,
                 xNowTick,
                 pxClient->xConfig.ulDisconnectTimeoutMs))
    {
        prvFailAndClose(pxClient, xNowTick);
    }
}

bool AppTcpClient_Init(
    AppTcpClient_t *pxClient,
    const AppTcpClientConfig_t *pxConfig,
    RtosW5500Access_t *pxW5500Access)
{
    if ((pxClient == NULL) ||
        (prvConfigIsValid(pxConfig) == false) ||
        (RtosW5500Access_IsReady(pxW5500Access) == false))
    {
        return false;
    }

    memset(pxClient, 0, sizeof(*pxClient));
    pxClient->xEnabled = pxConfig->xStartEnabled;
    pxClient->xConfig = *pxConfig;
    pxClient->pxW5500Access = pxW5500Access;
    pxClient->usNextLocalPort = pxConfig->usLocalPortFirst;
    pxClient->xState = APP_TCP_CLIENT_STATE_WAIT_LINK;
    pxClient->xInitialized = true;
    return true;
}

/**
 * @brief 服务TCP客户端状态机。
 *
 * @param pxClient TCP客户端实例指针。
 * @param xPhyLinkUp 物理链路是否可用。
 * @param xNowTick 当前系统滴答值。
 */
static void prvServiceWithAccessLocked(
    AppTcpClient_t *pxClient,
    bool xPhyLinkUp,
    TickType_t xNowTick)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return;
    }

    if ((pxClient->xEnabled == false) || (xPhyLinkUp == false))
    {
        pxClient->xPhyWasUp = false;

        if (pxClient->xState ==
            APP_TCP_CLIENT_STATE_WAIT_LINK)
        {
            return;
        }

        if ((pxClient->xState ==
             APP_TCP_CLIENT_STATE_FORCE_CLOSE_REQUEST) ||
            (pxClient->xState ==
             APP_TCP_CLIENT_STATE_FORCE_CLOSE_WAIT))
        {
            pxClient->ucPostCloseAction =
                APP_TCP_CLIENT_POST_CLOSE_WAIT_LINK;
        }
        else
        {
            prvBeginForcedClose(
                pxClient,
                APP_TCP_CLIENT_POST_CLOSE_WAIT_LINK,
                xNowTick);
        }

        prvServiceForcedClose(pxClient, xNowTick);
        return;
    }

    if (pxClient->xPhyWasUp == false)
    {
        /*
         * DOWN到UP是新的物理可用机会，跳过旧退避。
         * 仍先确认Socket关闭，再分配端口并OPEN。
         */
        pxClient->xPhyWasUp = true;
        prvResetBackoff(pxClient);
        pxClient->xSessionMarkedStable = false;
        prvBeginForcedClose(
            pxClient,
            APP_TCP_CLIENT_POST_CLOSE_OPEN,
            xNowTick);
    }

    switch (pxClient->xState)
    {
        case APP_TCP_CLIENT_STATE_FORCE_CLOSE_REQUEST:
        case APP_TCP_CLIENT_STATE_FORCE_CLOSE_WAIT:
            prvServiceForcedClose(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_OPEN_REQUEST:
            prvServiceOpenRequest(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_OPEN_WAIT:
            prvServiceOpenWait(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_CONNECT_REQUEST:
            prvServiceConnectRequest(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_CONNECTING:
            prvServiceConnecting(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_CONNECTED:
            prvServiceConnected(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_PEER_FIN:
            prvServicePeerFin(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_DISCONNECT_REQUEST:
            prvServiceDisconnectRequest(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_DISCONNECTING:
            prvServiceDisconnecting(pxClient, xNowTick);
            break;

        case APP_TCP_CLIENT_STATE_BACKOFF:
            if (prvHasExpired(
                    pxClient->xPhaseStartTick,
                    xNowTick,
                    pxClient->ulRetryDelayMs))
            {
                prvBeginForcedClose(
                    pxClient,
                    APP_TCP_CLIENT_POST_CLOSE_OPEN,
                    xNowTick);
            }
            break;

        case APP_TCP_CLIENT_STATE_WAIT_LINK:
        default:
            break;
    }
}

void AppTcpClient_Service(
    AppTcpClient_t *pxClient,
    bool xPhyLinkUp,
    TickType_t xNowTick)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        (RtosW5500Access_IsReady(
             pxClient->pxW5500Access) == false))
    {
        return;
    }

    if (RtosW5500Access_Lock(
            pxClient->pxW5500Access) == false)
    {
        /* 共享芯片暂忙；保留状态，下一个Service周期继续。 */
        return;
    }

    prvServiceWithAccessLocked(
        pxClient,
        xPhyLinkUp,
        xNowTick);

    RtosW5500Access_Unlock(pxClient->pxW5500Access);
}
AppTcpClientState_t AppTcpClient_GetState(
    const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return APP_TCP_CLIENT_STATE_WAIT_LINK;
    }
    return pxClient->xState;
}

uint32_t AppTcpClient_GetSessionGeneration(
    const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulSessionGeneration;
}

uint16_t AppTcpClient_GetActiveLocalPort(
    const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->usActiveLocalPort;
}

uint32_t AppTcpClient_GetRetryDelayMs(
    const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulRetryDelayMs;
}

uint32_t AppTcpClient_GetConsecutiveFailureCount(
    const AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulConsecutiveFailures;
}

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

// 标记当前TCP会话为稳定，重置退避计数。
void AppTcpClient_MarkSessionStable(AppTcpClient_t *pxClient)
{
    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        ((pxClient->xState !=
          APP_TCP_CLIENT_STATE_CONNECTED) &&
         (pxClient->xState !=
          APP_TCP_CLIENT_STATE_PEER_FIN)))
    {
        return;
    }

    pxClient->xSessionMarkedStable = true;
    prvResetBackoff(pxClient);
}

static int32_t prvSendWithAccessLocked(
    AppTcpClient_t *pxClient,
    const void *pvData,
    size_t xDataLength)
{
    BspW5500IoResult_t xPollResult;
    BspW5500OperationResult_t xStartResult;
    TickType_t xNowTick;
    size_t xSendLength;

    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        (pvData == NULL))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }
    if (xDataLength == 0U)
    {
        return 0;
    }
    if ((pxClient->xState !=
         APP_TCP_CLIENT_STATE_CONNECTED) &&
        (pxClient->xState !=
         APP_TCP_CLIENT_STATE_PEER_FIN))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }
    if (pxClient->xReconnectRequested ||
        pxClient->xGracefulCloseRequested)
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    /*
     * coreMQTT可能在同一次同步协议调用中反复调用TransportSend，
     * 所以必须读取当前Tick，不能只依赖最近一次Service保存的Tick。
     */
    xNowTick = xTaskGetTickCount();

    if (pxClient->xTxInFlight)
    {
        xPollResult = BspW5500_TcpPollSend(
            pxClient->xConfig.ucSocket);

        if (xPollResult == BSP_W5500_IO_COMPLETE)
        {
            pxClient->xTxInFlight = false;
        }
        else if ((xPollResult == BSP_W5500_IO_ERROR) ||
                 prvHasExpired(
                     pxClient->xTxStartTick,
                     xNowTick,
                     pxClient->xConfig.ulSendTimeoutMs))
        {
            pxClient->xTxInFlight = false;
            pxClient->xReconnectRequested = true;
            return APP_TCP_CLIENT_IO_ERROR;
        }
        else
        {
            return 0;
        }
    }

    xSendLength = xDataLength;
    if (xSendLength > APP_TCP_CLIENT_TX_CHUNK_LIMIT)
    {
        xSendLength = APP_TCP_CLIENT_TX_CHUNK_LIMIT;
    }
    if (xSendLength > UINT16_MAX)
    {
        xSendLength = UINT16_MAX;
    }
    if (xSendLength > INT32_MAX)
    {
        xSendLength = INT32_MAX;
    }

    xStartResult = BspW5500_TcpSendStart(
        pxClient->xConfig.ucSocket,
        (const uint8_t *)pvData,
        (uint16_t)xSendLength);

    if (xStartResult == BSP_W5500_OPERATION_STARTED)
    {
        pxClient->xSendBusy = false;
        pxClient->xTxInFlight = true;
        pxClient->xTxStartTick = xNowTick;
        return (int32_t)xSendLength;
    }
    if (xStartResult == BSP_W5500_OPERATION_BUSY)
    {
        /* 没有SEND在途时，TX空间或命令寄存器持续忙也必须有期限。 */
        if (pxClient->xSendBusy == false)
        {
            pxClient->xSendBusy = true;
            pxClient->xSendBusyStartTick = xNowTick;
        }
        if (prvHasExpired(pxClient->xSendBusyStartTick,
                          xNowTick,
                          pxClient->xConfig.ulSendTimeoutMs))
        {
            pxClient->xReconnectRequested = true;
            return APP_TCP_CLIENT_IO_ERROR;
        }
        return 0;
    }

    pxClient->xReconnectRequested = true;
    return APP_TCP_CLIENT_IO_ERROR;
}

int32_t AppTcpClient_Send(
    AppTcpClient_t *pxClient,
    const void *pvData,
    size_t xDataLength)
{
    int32_t lResult;

    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        (RtosW5500Access_IsReady(
             pxClient->pxW5500Access) == false))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    if (RtosW5500Access_Lock(
            pxClient->pxW5500Access) == false)
    {
        /* Transport语义：共享设备暂忙是0，不是断链。 */
        return 0;
    }

    lResult = prvSendWithAccessLocked(
        pxClient,
        pvData,
        xDataLength);

    RtosW5500Access_Unlock(pxClient->pxW5500Access);
    return lResult;
}

static int32_t prvReceiveWithAccessLocked(
    AppTcpClient_t *pxClient,
    void *pvBuffer,
    size_t xBufferLength)
{
    BspW5500IoResult_t xResult;
    BspW5500TcpStatus_t xStatus;
    TickType_t xNowTick;
    uint16_t usCapacity;
    uint16_t usReceivedLength = 0U;

    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        (pvBuffer == NULL))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }
    if (xBufferLength == 0U)
    {
        return 0;
    }
    if ((pxClient->xState !=
         APP_TCP_CLIENT_STATE_CONNECTED) &&
        (pxClient->xState !=
         APP_TCP_CLIENT_STATE_PEER_FIN))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    if (pxClient->xReconnectRequested)
    {
        /* 指针提交后发生错误，本连接已失效，不允许再次读取同一数据流。 */
        return APP_TCP_CLIENT_IO_ERROR;
    }

    xNowTick = xTaskGetTickCount();
    if ((BspW5500_TcpReadStatus(pxClient->xConfig.ucSocket, &xStatus) == false) ||
        xStatus.xTimedOut ||
        ((xStatus.xState != BSP_W5500_TCP_ESTABLISHED) &&
         (xStatus.xState != BSP_W5500_TCP_PEER_CLOSED_SEND)))
    {
        pxClient->xReconnectRequested = true;
        return APP_TCP_CLIENT_IO_ERROR;
    }
    if ((xStatus.xState == BSP_W5500_TCP_PEER_CLOSED_SEND) &&
        (pxClient->xState != APP_TCP_CLIENT_STATE_PEER_FIN))
    {
        /* coreMQTT同步重试时也能识别FIN，不依赖外层下一次Service。 */
        pxClient->xPeerReceiveDrained = false;
        prvSetState(pxClient, APP_TCP_CLIENT_STATE_PEER_FIN, xNowTick);
    }

    /* 先观察FIN，再读取RX长度；不能用观察FIN之前的空快照判定EOF。 */
    usCapacity = (xBufferLength > UINT16_MAX) ?
        UINT16_MAX : (uint16_t)xBufferLength;

    xResult = BspW5500_TcpReceive(
        pxClient->xConfig.ucSocket,
        (uint8_t *)pvBuffer,
        usCapacity,
        &usReceivedLength);

    if (xResult == BSP_W5500_IO_COMPLETE)
    {
        pxClient->xReceiveBusy = false;
        pxClient->xPeerReceiveDrained = false;
        return (int32_t)usReceivedLength;
    }
    if (xResult == BSP_W5500_IO_BUSY)
    {
        if (pxClient->xReceiveBusy == false)
        {
            pxClient->xReceiveBusy = true;
            pxClient->xReceiveBusyStartTick = xNowTick;
        }
        if (prvHasExpired(pxClient->xReceiveBusyStartTick,
                          xNowTick,
                          pxClient->xConfig.ulLocalActionTimeoutMs))
        {
            pxClient->xReconnectRequested = true;
            return APP_TCP_CLIENT_IO_ERROR;
        }
        return 0;
    }
    if (xResult == BSP_W5500_IO_IDLE)
    {
        pxClient->xReceiveBusy = false;
        if (pxClient->xState ==
            APP_TCP_CLIENT_STATE_PEER_FIN)
        {
            pxClient->xPeerReceiveDrained = true;
            return APP_TCP_CLIENT_IO_PEER_CLOSED;
        }
        return 0;
    }

    pxClient->xReconnectRequested = true;
    return APP_TCP_CLIENT_IO_ERROR;
}

int32_t AppTcpClient_Receive(
    AppTcpClient_t *pxClient,
    void *pvBuffer,
    size_t xBufferLength)
{
    int32_t lResult;

    if ((pxClient == NULL) ||
        (pxClient->xInitialized == false) ||
        (RtosW5500Access_IsReady(
             pxClient->pxW5500Access) == false))
    {
        return APP_TCP_CLIENT_IO_ERROR;
    }

    if (RtosW5500Access_Lock(
            pxClient->pxW5500Access) == false)
    {
        /* 暂时没有取得W5500，不修改TCP状态或接收指针。 */
        return 0;
    }

    lResult = prvReceiveWithAccessLocked(
        pxClient,
        pvBuffer,
        xBufferLength);

    RtosW5500Access_Unlock(pxClient->pxW5500Access);
    return lResult;
}

void AppTcpClient_RequestGracefulClose(AppTcpClient_t *pxClient)
{
    if ((pxClient != NULL) &&
        pxClient->xInitialized &&
        ((pxClient->xState ==
          APP_TCP_CLIENT_STATE_CONNECTED) ||
         (pxClient->xState ==
          APP_TCP_CLIENT_STATE_PEER_FIN)))
    {
        pxClient->xGracefulCloseRequested = true;
    }
}

void AppTcpClient_RequestReconnect(AppTcpClient_t *pxClient)
{
    if ((pxClient != NULL) &&
        pxClient->xInitialized)
    {
        pxClient->xReconnectRequested = true;
    }
}

void AppTcpClient_SetEnabled(AppTcpClient_t *pxClient, bool xEnabled)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return;
    }

    pxClient->xEnabled = xEnabled;
}

bool AppTcpClient_SetServerEndpoint(AppTcpClient_t *pxClient,
                                    const uint8_t pucServerIp[4],
                                    uint16_t usServerPort)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false) ||
        pxClient->xEnabled || (pxClient->xState != APP_TCP_CLIENT_STATE_WAIT_LINK) ||
        (pucServerIp == NULL) || (usServerPort == 0U))
    {
        return false;
    }

    memcpy(pxClient->xConfig.aucServerIp, pucServerIp,
           sizeof(pxClient->xConfig.aucServerIp));
    pxClient->xConfig.usServerPort = usServerPort;
    return true;
}
