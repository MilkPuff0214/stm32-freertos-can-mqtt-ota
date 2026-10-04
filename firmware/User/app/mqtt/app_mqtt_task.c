#include "app_mqtt_task.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "task.h"

#include "app_network_config.h"
#include "app_network_status.h"
#include "app_serial_task.h"
#include "app_tcp_client.h"
#include "app_mqtt_client.h"
#include "app_mqtt_private_credentials.h"

/*
 * coreMQTT同步连接和状态日志，先使用512 words。
 * 通过task命令测量高水位后再决定是否调整。
 */
#define APP_MQTT_TASK_STACK_DEPTH       512U
#define APP_MQTT_TASK_PRIORITY          (tskIDLE_PRIORITY + 2U)
#define APP_MQTT_SERVICE_PERIOD_MS      10U
#define APP_MQTT_REPORT_BUFFER_SIZE     128U

/* Publish已接受全部字节后的末段SEND排空期限，单位ms。 */
#define APP_MQTT_PUBLISH_DRAIN_TIMEOUT_MS    5000U

static uint32_t s_ulLastReportedInboundQueuedCount = 0U;
static uint32_t s_ulLastReportedInboundDroppedCount = 0U;

typedef struct
{
    RtosW5500Access_t *pxW5500Access;   // 对W5500以RTOS方式访问的接口。
    QueueHandle_t xNetworkStatusQueue;  // 用于接收网络状态更新的队列。
    QueueHandle_t xInboundQueue;        /* MQTT回调向业务侧投递消息。 */
    QueueHandle_t xOutboundQueue;
    QueueHandle_t xBusinessStatusQueue;
    QueueHandle_t xPublishReceiptQueue; /* MQTT生产，业务Task独占消费。 */
} AppMqttTaskContext_t;

static AppMqttOutboundMessage_t s_xOutboundMessage; // 用于存储待发送的MQTT出站消息。

/* 只在ulRequestId非零时有效；原TCP会话与该发布的排空期限。 */
static uint32_t s_ulOutboundTcpSession = 0U;
static TickType_t s_xOutboundDrainStartTick = 0U;
static AppMqttBusinessStatus_t s_xLastBusinessStatus = {0}; // 用于存储最后一次发布的业务状态。
static bool s_xBusinessStatusPublished = false; // 标记业务状态是否已发布。

/* 只用于调试器观察，不进行周期串口打印。 */
static volatile uint32_t s_ulOutboundPublishedCount = 0U;
static volatile uint32_t s_ulOutboundDroppedCount = 0U;
static volatile uint32_t s_ulOutboundFailedCount = 0U;

/* MQTT Task独占Socket 0，用于连接Windows侧EMQX Broker。 */
static const AppTcpClientConfig_t s_xMqttTcpConfig =
{
    .ucSocket = APP_NETWORK_SOCKET_MQTT,
    .aucServerIp =
    {
        192U, 168U, 101U, 11U
    },
    .usServerPort = 1883U,
    .usLocalPortFirst = 49152U,
    .usLocalPortLast = 65535U,
    .ulLocalActionTimeoutMs = 100U,
    .ulConnectTimeoutMs = 5000U,
    .ulSendTimeoutMs = 5000U,
    .ulPeerDrainTimeoutMs = 5000U,
    .ulDisconnectTimeoutMs = 5000U,
    .xStartEnabled = true,
};

static const char s_cMqttClientIdentifier[] = "stm32f103-0253544d3201";
static const char s_cMqttUserName[] = APP_MQTT_PRIVATE_USER_NAME;
static const char s_cMqttPassword[] = APP_MQTT_PRIVATE_PASSWORD;

static const AppMqttClientConfig_t s_xMqttClientConfig =
{
    .pcClientIdentifier = s_cMqttClientIdentifier,
    .usClientIdentifierLength = (uint16_t)(sizeof(s_cMqttClientIdentifier) - 1U),
    .pcUserName = s_cMqttUserName,
    .usUserNameLength = (uint16_t)(sizeof(s_cMqttUserName) - 1U),
    .pcPassword = s_cMqttPassword,
    .usPasswordLength = (uint16_t)(sizeof(s_cMqttPassword) - 1U),
    .usKeepAliveSeconds = 30U,
    .ulConnackTimeoutMs = 5000U,
    .ulStableSessionMs = 40000U,
    .ulSubackTimeoutMs = 5000U
};

static TaskHandle_t s_xMqttTaskHandle = NULL;   // MQTT任务句柄
static AppMqttTaskContext_t s_xMqttTaskContext = {0};
static AppNetworkStatus_t s_xNetworkStatus = {0};

/* Socket 0、TCP Client和MQTTContext只允许本Task访问。 */
static AppTcpClient_t s_xTcpClient; //Socket 0对应的TCP Client对象
static AppMqttClient_t s_xMqttClient; //MQTT相关信息
static bool s_xProtocolModulesReady = false;

static bool s_xMqttStateReported = false;
static AppMqttClientState_t s_xLastReportedMqttState = APP_MQTT_CLIENT_STATE_WAIT_TCP;
static MQTTStatus_t s_xLastReportedMqttStatus = MQTTSuccess;

static bool s_xTcpStateReported = false;
static AppTcpClientState_t s_xLastReportedTcpState =
    APP_TCP_CLIENT_STATE_WAIT_LINK;
static uint32_t s_ulMqttReportDropCount = 0U;   // MQTT报告丢失计数

static void prvReportText(const char *pcMessage)
{
    if (AppSerial_Write(
            (const uint8_t *)pcMessage,
            (uint16_t)strlen(pcMessage)) == false)
    {
        s_ulMqttReportDropCount++;
    }
}

static bool prvProtocolModulesInit(RtosW5500Access_t *pxW5500Access)
{
    // 初始化TCP客户端。
    if (AppTcpClient_Init(&s_xTcpClient, &s_xMqttTcpConfig, pxW5500Access) == false)
    {
        return false;
    }
    // 初始化MQTT客户端。
    return AppMqttClient_Init(&s_xMqttClient, &s_xMqttClientConfig, &s_xTcpClient,
                              s_xMqttTaskContext.xInboundQueue);
}

static void prvReceiveNetworkStatus(const AppMqttTaskContext_t *pxContext)
{
    /* Queue为空时保留上一份状态；收到新快照时直接覆盖本任务副本。 */
    if (xQueueReceive(pxContext->xNetworkStatusQueue, &s_xNetworkStatus, 0U) != pdPASS)
    {
        return;
    }

    if ((s_xNetworkStatus.xChipReady == false) || s_xProtocolModulesReady)
    {
        return;
    }

    s_xProtocolModulesReady = prvProtocolModulesInit(pxContext->pxW5500Access);
    if (s_xProtocolModulesReady == false)
    {
        prvReportText("MQTT: protocol module configuration invalid\r\n");
    }
}

static void prvReportTcpStateChange(void)
{
    AppTcpClientState_t xState;
    char cReport[APP_MQTT_REPORT_BUFFER_SIZE];
    int iLength;

    xState = AppTcpClient_GetState(&s_xTcpClient);
    if (s_xTcpStateReported &&
        (xState == s_xLastReportedTcpState))
    {
        return;
    }

    s_xTcpStateReported = true;
    s_xLastReportedTcpState = xState;

    switch (xState)
    {
        case APP_TCP_CLIENT_STATE_WAIT_LINK:
            iLength = snprintf(
                cReport,
                sizeof(cReport),
                "TCP: WAIT LINK\r\n");
            break;

        case APP_TCP_CLIENT_STATE_CONNECTING:
            iLength = snprintf(
                cReport,
                sizeof(cReport),
                "TCP: CONNECTING %u.%u.%u.%u:%u local=%u\r\n",
                (unsigned int)s_xMqttTcpConfig.aucServerIp[0],
                (unsigned int)s_xMqttTcpConfig.aucServerIp[1],
                (unsigned int)s_xMqttTcpConfig.aucServerIp[2],
                (unsigned int)s_xMqttTcpConfig.aucServerIp[3],
                (unsigned int)s_xMqttTcpConfig.usServerPort,
                (unsigned int)AppTcpClient_GetActiveLocalPort(
                    &s_xTcpClient));
            break;

        case APP_TCP_CLIENT_STATE_CONNECTED:
            iLength = snprintf(
                cReport,
                sizeof(cReport),
                "TCP: ESTABLISHED session=%lu\r\n",
                (unsigned long)AppTcpClient_GetSessionGeneration(
                    &s_xTcpClient));
            break;

        case APP_TCP_CLIENT_STATE_PEER_FIN:
            iLength = snprintf(cReport, sizeof(cReport), "TCP: PEER FIN\r\n");
            break;

        case APP_TCP_CLIENT_STATE_DISCONNECTING:
            iLength = snprintf(
                cReport,
                sizeof(cReport),
                "TCP: DISCONNECTING\r\n");
            break;

        case APP_TCP_CLIENT_STATE_BACKOFF:
            iLength = snprintf(
                cReport,
                sizeof(cReport),
                "TCP: RETRY in %lu ms failures=%lu\r\n",
                (unsigned long)AppTcpClient_GetRetryDelayMs(
                    &s_xTcpClient),
                (unsigned long)AppTcpClient_GetConsecutiveFailureCount(
                    &s_xTcpClient));
            break;

        default:
            return;
    }

    if ((iLength <= 0) ||
        (iLength >= (int)sizeof(cReport)) ||
        (AppSerial_Write(
            (const uint8_t *)cReport,
            (uint16_t)iLength) == false))
    {
        s_ulMqttReportDropCount++;
    }
}

static void prvReportMqttStateChange(void)
{
    AppMqttClientState_t xState = AppMqttClient_GetState(&s_xMqttClient);
    MQTTStatus_t xStatus = AppMqttClient_GetLastStatus(&s_xMqttClient);
    char cReport[APP_MQTT_REPORT_BUFFER_SIZE];
    unsigned long ulSessionCount;
    int iLength;

    if (s_xMqttStateReported &&
        (xState == s_xLastReportedMqttState) &&
        (xStatus == s_xLastReportedMqttStatus))
    {
        return;
    }

    s_xMqttStateReported = true;
    s_xLastReportedMqttState = xState;
    s_xLastReportedMqttStatus = xStatus;

    switch (xState)
    {
        case APP_MQTT_CLIENT_STATE_WAIT_TCP:
            iLength = snprintf(cReport,
                               sizeof(cReport),
                               "MQTT: WAIT TCP last=%s\r\n",
                               MQTT_Status_strerror(xStatus));
            break;

        case APP_MQTT_CLIENT_STATE_CONNECTING:
            iLength = snprintf(cReport, sizeof(cReport), "MQTT: CONNECTING\r\n");
            break;

        case APP_MQTT_CLIENT_STATE_CONNECTED:
            ulSessionCount = (unsigned long)AppMqttClient_GetSuccessfulSessionCount(&s_xMqttClient);
            iLength = snprintf(cReport,
                               sizeof(cReport),
                               "MQTT: CONNECTED session=%lu\r\n",
                               ulSessionCount);
            break;
        case APP_MQTT_CLIENT_STATE_SUBSCRIBING:
            iLength = snprintf(cReport, sizeof(cReport), "MQTT: SUBSCRIBING\r\n");
            break;

        case APP_MQTT_CLIENT_STATE_READY:
            ulSessionCount =
                (unsigned long)AppMqttClient_GetSuccessfulSessionCount(&s_xMqttClient);
            iLength = snprintf(cReport, sizeof(cReport),
                               "MQTT: READY session=%lu\r\n", ulSessionCount);
            break;
        default:
            return;
    }

    if ((iLength <= 0) ||
        (iLength >= (int)sizeof(cReport)) ||
        (AppSerial_Write((const uint8_t *)cReport, (uint16_t)iLength) == false))
    {
        s_ulMqttReportDropCount++;
    }
}

static void prvReportMqttInboundCounters(void)
{
    uint32_t ulQueuedCount = AppMqttClient_GetInboundQueuedCount(&s_xMqttClient);
    uint32_t ulDroppedCount = AppMqttClient_GetInboundDroppedCount(&s_xMqttClient);
    char cReport[APP_MQTT_REPORT_BUFFER_SIZE];
    int iLength;

    if ((ulQueuedCount == s_ulLastReportedInboundQueuedCount) &&
        (ulDroppedCount == s_ulLastReportedInboundDroppedCount))
    {
        return;
    }

    s_ulLastReportedInboundQueuedCount = ulQueuedCount;
    s_ulLastReportedInboundDroppedCount = ulDroppedCount;
    iLength = snprintf(cReport, sizeof(cReport),
                       "MQTT RX: queued=%lu dropped=%lu\r\n",
                       (unsigned long)ulQueuedCount, (unsigned long)ulDroppedCount);

    if ((iLength <= 0) ||
        (iLength >= (int)sizeof(cReport)) ||
        (AppSerial_Write((const uint8_t *)cReport, (uint16_t)iLength) == false))
    {
        s_ulMqttReportDropCount++;
    }
}

static void prvCompletePublishReceipt(const AppMqttTaskContext_t *pxContext,
                                      AppMqttPublishResult_t eResult)
{
    AppMqttPublishReceipt_t xReceipt;
    BaseType_t xQueueResult;

    if (s_xOutboundMessage.ulRequestId == 0U)
    {
        return;
    }

    xReceipt.ulRequestId = s_xOutboundMessage.ulRequestId;
    xReceipt.ulMqttSession = s_xOutboundMessage.ulMqttSession;
    xReceipt.eResult = eResult;

    /* 单在途请求的完成记录；即使业务暂未调度也不阻塞网络Task。 */
    xQueueResult = xQueueOverwrite(pxContext->xPublishReceiptQueue, &xReceipt);
    configASSERT(xQueueResult == pdPASS);
    (void)xQueueResult;

    /* 请求编号就是唯一待回执标志，清除后才允许取下一条出站。 */
    s_xOutboundMessage.ulRequestId = 0U;
}

static void prvProcessOutbound(const AppMqttTaskContext_t *pxContext)
{
    MQTTStatus_t xStatus;
    uint32_t ulSession;
    TickType_t xNowTick;

    ulSession = AppMqttClient_GetSuccessfulSessionCount(&s_xMqttClient);

    if (s_xOutboundMessage.ulRequestId != 0U)
    {
        /* 错误优先于排空：关闭路径清除SEND标志不能冒充原请求成功。 */
        if ((s_xProtocolModulesReady == false) ||
            (s_xNetworkStatus.xChipReady == false) ||
            (s_xNetworkStatus.xPhyLinkUp == false) ||
            (AppMqttClient_GetState(&s_xMqttClient) != APP_MQTT_CLIENT_STATE_READY) ||
            (s_xOutboundMessage.ulMqttSession != ulSession) ||
            (AppTcpClient_GetState(&s_xTcpClient) != APP_TCP_CLIENT_STATE_CONNECTED) ||
            (AppTcpClient_GetSessionGeneration(&s_xTcpClient) != s_ulOutboundTcpSession))
        {
            if (s_ulOutboundFailedCount < UINT32_MAX)
            {
                s_ulOutboundFailedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
            return;
        }

        xNowTick = xTaskGetTickCount();
        if ((TickType_t)(xNowTick - s_xOutboundDrainStartTick) >=
            pdMS_TO_TICKS(APP_MQTT_PUBLISH_DRAIN_TIMEOUT_MS))
        {
            if (s_ulOutboundFailedCount < UINT32_MAX)
            {
                s_ulOutboundFailedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
            return;
        }

        if (AppTcpClient_IsSendDrained(&s_xTcpClient))
        {
            if (s_ulOutboundPublishedCount < UINT32_MAX)
            {
                s_ulOutboundPublishedCount++;
            }
            prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_SENT);
        }

        /* 每轮仍运行协议Service，但当前请求结束前不消费下一条outbound。 */
        return;
    }

    /* 每轮至多处理一条；未初始化时也消费并否决旧请求，避免无限留队。 */
    if (xQueueReceive(pxContext->xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return;
    }

    if ((s_xProtocolModulesReady == false) ||
        (s_xNetworkStatus.xChipReady == false) ||
        (s_xNetworkStatus.xPhyLinkUp == false) ||
        (AppMqttClient_GetState(&s_xMqttClient) != APP_MQTT_CLIENT_STATE_READY) ||
        (s_xOutboundMessage.ulMqttSession != ulSession) ||
        (s_xOutboundMessage.usTopicLength == 0U) ||
        (s_xOutboundMessage.usTopicLength > APP_MQTT_OUTBOUND_TOPIC_MAX_LENGTH) ||
        (s_xOutboundMessage.usPayloadLength == 0U) ||
        (s_xOutboundMessage.usPayloadLength > APP_MQTT_OUTBOUND_PAYLOAD_MAX_LENGTH))
    {
        if (s_ulOutboundDroppedCount < UINT32_MAX)
        {
            s_ulOutboundDroppedCount++;
        }
        prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
        return;
    }

    /* 保存发布开始前的连接编号；只有本Task能够改变该TCP Client。 */
    s_ulOutboundTcpSession = AppTcpClient_GetSessionGeneration(&s_xTcpClient);
    xStatus = AppMqttClient_Publish(&s_xMqttClient, s_xOutboundMessage.acTopic,
                                    s_xOutboundMessage.usTopicLength,
                                    s_xOutboundMessage.aucPayload,
                                    s_xOutboundMessage.usPayloadLength);
    if (xStatus != MQTTSuccess)
    {
        if (s_ulOutboundFailedCount < UINT32_MAX)
        {
            s_ulOutboundFailedCount++;
        }
        prvCompletePublishReceipt(pxContext, APP_MQTT_PUBLISH_RESULT_FAILED);
        return;
    }

    if (s_xOutboundMessage.ulRequestId != 0U)
    {
        /* 最后一段只是被接受；必须留到下一轮Service观察SENDOK。 */
        s_xOutboundDrainStartTick = xTaskGetTickCount();
        return;
    }

    if (s_ulOutboundPublishedCount < UINT32_MAX)
    {
        s_ulOutboundPublishedCount++;
    }
}

static void prvPublishBusinessStatus(const AppMqttTaskContext_t *pxContext)
{
    AppMqttBusinessStatus_t xStatus = {0};
    BaseType_t xResult;

    if (s_xProtocolModulesReady)
    {
        xStatus.ulSession = AppMqttClient_GetSuccessfulSessionCount(&s_xMqttClient);
        xStatus.xReady =
            AppMqttClient_GetState(&s_xMqttClient) == APP_MQTT_CLIENT_STATE_READY;
    }

    if (s_xBusinessStatusPublished &&
        (xStatus.ulSession == s_xLastBusinessStatus.ulSession) &&
        (xStatus.xReady == s_xLastBusinessStatus.xReady))
    {
        return;
    }

    /* 长度1 Queue只保存最新状态，旧状态没有补处理价值。 */
    xResult = xQueueOverwrite(pxContext->xBusinessStatusQueue, &xStatus);
    configASSERT(xResult == pdPASS);

    if (xResult == pdPASS)
    {
        s_xLastBusinessStatus = xStatus;
        s_xBusinessStatusPublished = true;
    }
}

static void prvMqttTask(void *pvParameters)
{
    AppMqttTaskContext_t *pxContext = (AppMqttTaskContext_t *)pvParameters;
    TickType_t xNowTick;

    configASSERT(pxContext != NULL);
    configASSERT(RtosW5500Access_IsReady(pxContext->pxW5500Access));
    configASSERT(pxContext->xNetworkStatusQueue != NULL);
    configASSERT(pxContext->xInboundQueue != NULL);
    configASSERT(pxContext->xOutboundQueue != NULL);
    configASSERT(pxContext->xBusinessStatusQueue != NULL);
    configASSERT(pxContext->xPublishReceiptQueue != NULL);

    for (;;)
    {
        prvReceiveNetworkStatus(pxContext);
        xNowTick = xTaskGetTickCount();

        if (s_xProtocolModulesReady)
        {
            AppTcpClient_Service(&s_xTcpClient, s_xNetworkStatus.xPhyLinkUp, xNowTick);
        }

        /* 先观察SENDOK排空，再允许coreMQTT保活发起新的SEND。 */
        prvProcessOutbound(pxContext);

        if (s_xProtocolModulesReady)
        {
            xNowTick = xTaskGetTickCount();
            AppMqttClient_Service(&s_xMqttClient, xNowTick);
            prvReportTcpStateChange();
            prvReportMqttStateChange();
            prvReportMqttInboundCounters();
        }

        /* 包括未初始化或发送失败后的最新READY状态，不保留过期可用快照。 */
        prvPublishBusinessStatus(pxContext);
        vTaskDelay(pdMS_TO_TICKS(APP_MQTT_SERVICE_PERIOD_MS));
    }
}

bool AppMqttTask_Create(RtosW5500Access_t *pxW5500Access,
                        QueueHandle_t xNetworkStatusQueue,
                        QueueHandle_t xInboundQueue,
                        QueueHandle_t xOutboundQueue,
                        QueueHandle_t xBusinessStatusQueue,
                        QueueHandle_t xPublishReceiptQueue)
{
    BaseType_t xCreateResult;

    if ((s_xMqttTaskHandle != NULL) ||
        (RtosW5500Access_IsReady(pxW5500Access) == false) ||
        (xNetworkStatusQueue == NULL) || (xInboundQueue == NULL) ||
        (xOutboundQueue == NULL) || (xBusinessStatusQueue == NULL) ||
        (xPublishReceiptQueue == NULL))
    {
        return false;
    }

    s_xMqttTaskContext.pxW5500Access = pxW5500Access;
    s_xMqttTaskContext.xNetworkStatusQueue = xNetworkStatusQueue;
    s_xMqttTaskContext.xInboundQueue = xInboundQueue;
    s_xMqttTaskContext.xOutboundQueue = xOutboundQueue;
    s_xMqttTaskContext.xBusinessStatusQueue = xBusinessStatusQueue;
    s_xMqttTaskContext.xPublishReceiptQueue = xPublishReceiptQueue;

    xCreateResult = xTaskCreate(prvMqttTask, "MQTT", APP_MQTT_TASK_STACK_DEPTH,
                                &s_xMqttTaskContext, APP_MQTT_TASK_PRIORITY,
                                &s_xMqttTaskHandle);
    if (xCreateResult != pdPASS)
    {
        s_xMqttTaskHandle = NULL;
        s_xMqttTaskContext = (AppMqttTaskContext_t){0};
        return false;
    }

    return true;
}
