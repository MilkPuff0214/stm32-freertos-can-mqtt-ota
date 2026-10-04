#include "app_mqtt_client.h"

#include <stddef.h>
#include <string.h>

#include "task.h"

static const char s_cControlTopic[] = "Server2Client/control";
static const char s_cFileDownloadCommandTopic[] =
    "Server2Client/filedownloadcommand";
static const char s_cFileUpgradeCommandTopic[] = "Server2Client/fileupgradecommand";

static const MQTTSubscribeInfo_t s_xSubscriptions[] =
{
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cControlTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cControlTopic) - 1U)
    },
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cFileDownloadCommandTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cFileDownloadCommandTopic) - 1U)
    },
    {
        .qos = MQTTQoS0,
        .pTopicFilter = s_cFileUpgradeCommandTopic,
        .topicFilterLength = (uint16_t)(sizeof(s_cFileUpgradeCommandTopic) - 1U)
    }
};

static AppMqttClient_t *prvClientFromMqttContext(MQTTContext_t *pxMqttContext)
{
    return (AppMqttClient_t *)((uint8_t *)pxMqttContext -
                               offsetof(AppMqttClient_t, xMqttContext));
}

static void prvIncrementCounter(uint32_t *pulCounter)
{
    if (*pulCounter < UINT32_MAX)
    {
        (*pulCounter)++;
    }
}

static TickType_t prvMillisecondsToTicks(uint32_t ulMilliseconds)
{
    TickType_t xTicks = pdMS_TO_TICKS(ulMilliseconds);

    if (xTicks == 0U)
    {
        xTicks = 1U;
    }
    return xTicks;
}

static bool prvHasExpired(TickType_t xStartTick, TickType_t xNowTick, uint32_t ulTimeoutMs)
{
    return (TickType_t)(xNowTick - xStartTick) >= prvMillisecondsToTicks(ulTimeoutMs);
}

static bool prvOptionalFieldIsValid(const char *pcValue, uint16_t usLength)
{
    return ((pcValue == NULL) && (usLength == 0U)) ||
           ((pcValue != NULL) && (usLength > 0U));
}

static bool prvConfigIsValid(const AppMqttClientConfig_t *pxConfig)
{
    if ((pxConfig == NULL) ||
        (pxConfig->ulConnackTimeoutMs == 0U) ||
        (pxConfig->ulSubackTimeoutMs == 0U) ||
        (pxConfig->ulStableSessionMs == 0U) ||
        (pxConfig->pcClientIdentifier == NULL) ||
        (pxConfig->usClientIdentifierLength == 0U) ||
        (pxConfig->usKeepAliveSeconds == 0U) ||
        (prvOptionalFieldIsValid(pxConfig->pcUserName, pxConfig->usUserNameLength) == false) ||
        (prvOptionalFieldIsValid(pxConfig->pcPassword, pxConfig->usPasswordLength) == false))
    {
        return false;
    }
    return true;
}

static uint32_t prvGetTimeMs(void)
{
    /* 当前FreeRTOS配置为1000 Hz；无符号回绕符合coreMQTT计时契约。 */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void prvQueueInboundPublish(AppMqttClient_t *pxClient,
                                   const MQTTPublishInfo_t *pxPublishInfo)
{
    AppMqttInboundMessage_t *pxMessage = &pxClient->xInboundStagingMessage;
    AppMqttInboundMethod_t xMethod;

    if ((pxClient->xState != APP_MQTT_CLIENT_STATE_READY) ||
        (pxPublishInfo == NULL) ||
        (pxPublishInfo->qos != MQTTQoS0) ||
        (pxPublishInfo->retain == true) ||
        (pxPublishInfo->pTopicName == NULL) ||
        (pxPublishInfo->pPayload == NULL) ||
        (pxPublishInfo->payloadLength == 0U) ||
        (pxPublishInfo->payloadLength > APP_MQTT_INBOUND_PAYLOAD_MAX_LENGTH))
    {
        prvIncrementCounter(&pxClient->ulInboundDroppedCount);
        return;
    }

    if ((pxPublishInfo->topicNameLength == (sizeof(s_cControlTopic) - 1U)) &&
        (memcmp(pxPublishInfo->pTopicName, s_cControlTopic,
                sizeof(s_cControlTopic) - 1U) == 0))
    {
        xMethod = APP_MQTT_INBOUND_METHOD_CONTROL;
    }
    else if ((pxPublishInfo->topicNameLength ==
              (sizeof(s_cFileDownloadCommandTopic) - 1U)) &&
             (memcmp(pxPublishInfo->pTopicName, s_cFileDownloadCommandTopic,
                     sizeof(s_cFileDownloadCommandTopic) - 1U) == 0))
    {
        xMethod = APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND;
    }
    else if ((pxPublishInfo->topicNameLength ==
              (sizeof(s_cFileUpgradeCommandTopic) - 1U)) &&
             (memcmp(pxPublishInfo->pTopicName, s_cFileUpgradeCommandTopic,
                     sizeof(s_cFileUpgradeCommandTopic) - 1U) == 0))
    {
        xMethod = APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND;
    }
    else
    {
        prvIncrementCounter(&pxClient->ulInboundDroppedCount);
        return;
    }

    pxMessage->eMethod = xMethod;
    pxMessage->ulMqttSession = pxClient->ulSuccessfulSessionCount;
    pxMessage->usPayloadLength = (uint16_t)pxPublishInfo->payloadLength;
    memcpy(pxMessage->aucPayload, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);

    if (xQueueSend(pxClient->xInboundQueue, pxMessage, 0U) == pdPASS)
    {
        prvIncrementCounter(&pxClient->ulInboundQueuedCount);
    }
    else
    {
        prvIncrementCounter(&pxClient->ulInboundDroppedCount);
    }

    if (xMethod == APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND)
    {
        memset(pxMessage->aucPayload, 0, pxMessage->usPayloadLength);
        pxMessage->usPayloadLength = 0U;
    }
}

static void prvHandleSubAck(AppMqttClient_t *pxClient,
                            MQTTPacketInfo_t *pxPacketInfo,
                            const MQTTDeserializedInfo_t *pxDeserializedInfo)
{
    uint8_t *pucStatusCodes = NULL;
    size_t xStatusCodeCount = 0U;
    MQTTStatus_t xStatus;
    size_t xIndex;

    if ((pxClient->xState != APP_MQTT_CLIENT_STATE_SUBSCRIBING) ||
        (pxDeserializedInfo->packetIdentifier != pxClient->usPendingSubscribePacketId))
    {
        pxClient->xCallbackStatus = MQTTBadResponse;
        return;
    }

    if (pxDeserializedInfo->deserializationResult != MQTTSuccess)
    {
        pxClient->xCallbackStatus = pxDeserializedInfo->deserializationResult;
        return;
    }

    xStatus = MQTT_GetSubAckStatusCodes(pxPacketInfo, &pucStatusCodes, &xStatusCodeCount);
    if ((xStatus != MQTTSuccess) ||
        (xStatusCodeCount != (sizeof(s_xSubscriptions) / sizeof(s_xSubscriptions[0]))) ||
        (pucStatusCodes == NULL))
    {
        pxClient->xCallbackStatus = MQTTBadResponse;
        return;
    }

    for (xIndex = 0U; xIndex < xStatusCodeCount; xIndex++)
    {
        if (pucStatusCodes[xIndex] == MQTTSubAckFailure)
        {
            pxClient->xCallbackStatus = MQTTServerRefused;
            return;
        }
        if (pucStatusCodes[xIndex] != MQTTSubAckSuccessQos0)
        {
            pxClient->xCallbackStatus = MQTTBadResponse;
            return;
        }
    }

    pxClient->usPendingSubscribePacketId = MQTT_PACKET_ID_INVALID;
    pxClient->xReadyTick = xTaskGetTickCount();
    pxClient->xLastStatus = MQTTSuccess;
    pxClient->xState = APP_MQTT_CLIENT_STATE_READY;
}

static void prvEventCallback(MQTTContext_t *pxMqttContext,
                             MQTTPacketInfo_t *pxPacketInfo,
                             MQTTDeserializedInfo_t *pxDeserializedInfo)
{
    AppMqttClient_t *pxClient;

    if ((pxMqttContext == NULL) ||
        (pxPacketInfo == NULL) ||
        (pxDeserializedInfo == NULL))
    {
        return;
    }

    pxClient = prvClientFromMqttContext(pxMqttContext);

    if (pxPacketInfo->type == MQTT_PACKET_TYPE_SUBACK)
    {
        prvHandleSubAck(pxClient, pxPacketInfo, pxDeserializedInfo);
    }
    else if ((pxPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH)
    {
        prvQueueInboundPublish(pxClient, pxDeserializedInfo->pPublishInfo);
    }
    else
    {
        /* PINGRESP等控制包由coreMQTT处理，本层无需保存。 */
    }
}

static MQTTStatus_t prvInitContext(AppMqttClient_t *pxClient)
{
    TransportInterface_t xTransportInterface = {0};
    MQTTFixedBuffer_t xNetworkBuffer;

    if (AppMqttTransportW5500_Init(&pxClient->xNetworkContext, &xTransportInterface) == false)
    {
        return MQTTBadParameter;
    }

    xNetworkBuffer.pBuffer = pxClient->aucNetworkBuffer;
    xNetworkBuffer.size = sizeof(pxClient->aucNetworkBuffer);

    /* MQTT_Init会按值复制接口表和缓冲区描述，局部变量可以在返回后释放。 */
    return MQTT_Init(&pxClient->xMqttContext, &xTransportInterface,
                     prvGetTimeMs, prvEventCallback, &xNetworkBuffer);
}

static void prvRequestReconnect(AppMqttClient_t *pxClient, MQTTStatus_t xStatus)
{
    pxClient->xLastStatus = xStatus;
    pxClient->xCallbackStatus = MQTTSuccess;
    pxClient->usPendingSubscribePacketId = MQTT_PACKET_ID_INVALID;
    pxClient->xState = APP_MQTT_CLIENT_STATE_WAIT_TCP;
    pxClient->xTcpSessionMarkedStable = false;
    AppTcpClient_RequestReconnect(pxClient->xNetworkContext.pxTcpClient);
}

static void prvStartSubscription(AppMqttClient_t *pxClient, TickType_t xNowTick)
{
    uint16_t usPacketId = MQTT_GetPacketId(&pxClient->xMqttContext);
    MQTTStatus_t xStatus;

    xStatus = MQTT_Subscribe(&pxClient->xMqttContext, s_xSubscriptions,
                             sizeof(s_xSubscriptions) / sizeof(s_xSubscriptions[0]),
                             usPacketId);
    if (xStatus != MQTTSuccess)
    {
        prvRequestReconnect(pxClient, xStatus);
        return;
    }

    pxClient->usPendingSubscribePacketId = usPacketId;
    pxClient->xSubscribeStartTick = xNowTick;
    pxClient->xCallbackStatus = MQTTSuccess;
    pxClient->xState = APP_MQTT_CLIENT_STATE_SUBSCRIBING;
}

static void prvConnectNewTcpSession(AppMqttClient_t *pxClient, uint32_t ulTcpSession)
{
    MQTTConnectInfo_t xConnectInfo;
    MQTTStatus_t xStatus;
    bool xSessionPresent = false;

    pxClient->ulObservedTcpSession = ulTcpSession;
    pxClient->xState = APP_MQTT_CLIENT_STATE_CONNECTING;
    pxClient->xTcpSessionMarkedStable = false;
    pxClient->xCallbackStatus = MQTTSuccess;
    pxClient->usPendingSubscribePacketId = MQTT_PACKET_ID_INVALID;

    xStatus = prvInitContext(pxClient);
    if (xStatus != MQTTSuccess)
    {
        prvRequestReconnect(pxClient, xStatus);
        return;
    }

    memset(&xConnectInfo, 0, sizeof(xConnectInfo));
    xConnectInfo.cleanSession = true;
    xConnectInfo.keepAliveSeconds = pxClient->xConfig.usKeepAliveSeconds;
    xConnectInfo.pClientIdentifier = pxClient->xConfig.pcClientIdentifier;
    xConnectInfo.clientIdentifierLength = pxClient->xConfig.usClientIdentifierLength;
    xConnectInfo.pUserName = pxClient->xConfig.pcUserName;
    xConnectInfo.userNameLength = pxClient->xConfig.usUserNameLength;
    xConnectInfo.pPassword = pxClient->xConfig.pcPassword;
    xConnectInfo.passwordLength = pxClient->xConfig.usPasswordLength;

    /* 当前协议未定义LWT，因此第三个参数必须是NULL。 */
    xStatus = MQTT_Connect(&pxClient->xMqttContext,
                           &xConnectInfo,
                           NULL,
                           pxClient->xConfig.ulConnackTimeoutMs,
                           &xSessionPresent);

    /* Clean Session为true时，Broker不得返回旧会话存在。 */
    if ((xStatus != MQTTSuccess) || xSessionPresent)
    {
        if ((xStatus == MQTTSuccess) && xSessionPresent)
        {
            xStatus = MQTTBadResponse;
        }
        prvRequestReconnect(pxClient, xStatus);
        return;
    }

    pxClient->xLastStatus = MQTTSuccess;
    pxClient->xState = APP_MQTT_CLIENT_STATE_CONNECTED;
    if (pxClient->ulSuccessfulSessionCount < UINT32_MAX)
    {
        pxClient->ulSuccessfulSessionCount++;
    }
}

static void prvProcessConnectedSession(AppMqttClient_t *pxClient, TickType_t xNowTick)
{
    MQTTStatus_t xStatus = MQTT_ProcessLoop(&pxClient->xMqttContext);

    if ((xStatus != MQTTSuccess) && (xStatus != MQTTNeedMoreBytes))
    {
        prvRequestReconnect(pxClient, xStatus);
        return;
    }

    if (pxClient->xCallbackStatus != MQTTSuccess)
    {
        prvRequestReconnect(pxClient, pxClient->xCallbackStatus);
        return;
    }

    if (pxClient->xState == APP_MQTT_CLIENT_STATE_SUBSCRIBING)
    {
        if (prvHasExpired(pxClient->xSubscribeStartTick, xNowTick,
                          pxClient->xConfig.ulSubackTimeoutMs))
        {
            prvRequestReconnect(pxClient, MQTTNoDataAvailable);
        }
        return;
    }

    if ((pxClient->xState == APP_MQTT_CLIENT_STATE_READY) &&
        (pxClient->xTcpSessionMarkedStable == false) &&
        prvHasExpired(pxClient->xReadyTick, xNowTick, pxClient->xConfig.ulStableSessionMs))
    {
        AppTcpClient_MarkSessionStable(pxClient->xNetworkContext.pxTcpClient);
        pxClient->xTcpSessionMarkedStable = true;
    }
}

bool AppMqttClient_Init(AppMqttClient_t *pxClient, const AppMqttClientConfig_t *pxConfig,
                        AppTcpClient_t *pxTcpClient, QueueHandle_t xInboundQueue)
{
    if ((pxClient == NULL) ||
        (pxTcpClient == NULL) ||
        (xInboundQueue == NULL) ||
        (prvConfigIsValid(pxConfig) == false))
    {
        return false;
    }

    memset(pxClient, 0, sizeof(*pxClient));
    pxClient->xConfig = *pxConfig;
    pxClient->xNetworkContext.pxTcpClient = pxTcpClient;
    pxClient->xInboundQueue = xInboundQueue;
    pxClient->usPendingSubscribePacketId = MQTT_PACKET_ID_INVALID;
    pxClient->xState = APP_MQTT_CLIENT_STATE_WAIT_TCP;
    pxClient->xLastStatus = MQTTSuccess;
    pxClient->xCallbackStatus = MQTTSuccess;
    pxClient->xInitialized = true;
    return true;
}

void AppMqttClient_Service(AppMqttClient_t *pxClient, TickType_t xNowTick)
{
    AppTcpClientState_t xTcpState;
    uint32_t ulTcpSession;
    bool xMqttSessionActive;

    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return;
    }

    xTcpState = AppTcpClient_GetState(pxClient->xNetworkContext.pxTcpClient);
    ulTcpSession = AppTcpClient_GetSessionGeneration(pxClient->xNetworkContext.pxTcpClient);
    xMqttSessionActive =
        (pxClient->xState == APP_MQTT_CLIENT_STATE_CONNECTED) ||
        (pxClient->xState == APP_MQTT_CLIENT_STATE_SUBSCRIBING) ||
        (pxClient->xState == APP_MQTT_CLIENT_STATE_READY);

    if (xMqttSessionActive)
    {
        if (((xTcpState != APP_TCP_CLIENT_STATE_CONNECTED) &&
             (xTcpState != APP_TCP_CLIENT_STATE_PEER_FIN)) ||
            (ulTcpSession != pxClient->ulObservedTcpSession))
        {
            pxClient->usPendingSubscribePacketId = MQTT_PACKET_ID_INVALID;
            pxClient->xState = APP_MQTT_CLIENT_STATE_WAIT_TCP;
            pxClient->xTcpSessionMarkedStable = false;
            return;
        }

        if (pxClient->xState == APP_MQTT_CLIENT_STATE_CONNECTED)
        {
            if (xTcpState == APP_TCP_CLIENT_STATE_CONNECTED)
            {
                prvStartSubscription(pxClient, xNowTick);
            }
            else
            {
                prvRequestReconnect(pxClient, MQTTRecvFailed);
            }
            return;
        }

        /* SUBSCRIBING和READY都必须持续处理SUBACK、PUBLISH及Keep Alive。 */
        prvProcessConnectedSession(pxClient, xNowTick);
        return;
    }

    if ((xTcpState == APP_TCP_CLIENT_STATE_CONNECTED) &&
        (ulTcpSession != 0U) &&
        (ulTcpSession != pxClient->ulObservedTcpSession))
    {
        prvConnectNewTcpSession(pxClient, ulTcpSession);
    }
}

MQTTStatus_t AppMqttClient_Publish(AppMqttClient_t *pxClient, const char *pcTopic,
                                   uint16_t usTopicLength, const uint8_t *pucPayload,
                                   uint16_t usPayloadLength)
{
    MQTTPublishInfo_t xPublishInfo = {0};
    MQTTStatus_t xStatus;

    if ((pxClient == NULL) || (pcTopic == NULL) || (usTopicLength == 0U) ||
        (pucPayload == NULL) || (usPayloadLength == 0U) ||
        (pxClient->xInitialized == false))
    {
        return MQTTBadParameter;
    }

    if (pxClient->xState != APP_MQTT_CLIENT_STATE_READY)
    {
        return MQTTIllegalState;
    }

    xPublishInfo.qos = MQTTQoS0;
    xPublishInfo.retain = false;
    xPublishInfo.dup = false;
    xPublishInfo.pTopicName = pcTopic;
    xPublishInfo.topicNameLength = usTopicLength;
    xPublishInfo.pPayload = pucPayload;
    xPublishInfo.payloadLength = usPayloadLength;

    /* QoS 0没有报文级PUBACK，因此Packet ID固定使用无效值0。 */
    xStatus = MQTT_Publish(&pxClient->xMqttContext, &xPublishInfo, MQTT_PACKET_ID_INVALID);
    pxClient->xLastStatus = xStatus;

    /* 发送失败说明当前字节流已经不再可信，交回统一重连状态机。 */
    if (xStatus == MQTTSendFailed)
    {
        prvRequestReconnect(pxClient, xStatus);
    }

    return xStatus;
}

AppMqttClientState_t AppMqttClient_GetState(const AppMqttClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return APP_MQTT_CLIENT_STATE_WAIT_TCP;
    }
    return pxClient->xState;
}

MQTTStatus_t AppMqttClient_GetLastStatus(const AppMqttClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return MQTTBadParameter;
    }
    return pxClient->xLastStatus;
}

uint32_t AppMqttClient_GetSuccessfulSessionCount(const AppMqttClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulSuccessfulSessionCount;
}

uint32_t AppMqttClient_GetInboundQueuedCount(const AppMqttClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulInboundQueuedCount;
}

uint32_t AppMqttClient_GetInboundDroppedCount(const AppMqttClient_t *pxClient)
{
    if ((pxClient == NULL) || (pxClient->xInitialized == false))
    {
        return 0U;
    }
    return pxClient->ulInboundDroppedCount;
}
