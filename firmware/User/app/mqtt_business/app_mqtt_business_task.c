#include "app_mqtt_business_task.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "task.h"

#include "app_mqtt_message.h"
#include "app_adc_task.h"
#include "app_ftp_task.h"
#include "app_output_control.h"
#include "app_storage_task.h"
#include "app_firmware_image.h"
#include "app_serial_task.h"
#include "stm32f10x.h"
#include <stddef.h>
#include "core_json.h"

#define APP_MQTT_BUSINESS_TASK_STACK_DEPTH          512U    //字节：512*4（FreeRTOS堆栈单位为4字节）
#define APP_MQTT_BUSINESS_TASK_PRIORITY             (tskIDLE_PRIORITY + 1U)
#define APP_MQTT_BUSINESS_SERVICE_PERIOD_MS         100U
#define APP_MQTT_BUSINESS_HEARTBEAT_PERIOD_MS       10000U
#define APP_MQTT_BUSINESS_QUEUE_RETRY_MS            1000U

typedef struct
{
    QueueHandle_t xInboundQueue;
    QueueHandle_t xOutboundQueue;
    QueueHandle_t xBusinessStatusQueue;
    QueueHandle_t xFtpCommandQueue;
    QueueHandle_t xFtpStatusQueue;
    QueueHandle_t xStorageRequestQueue;
    QueueHandle_t xStorageStatusQueue; /* 只Peek，Storage是唯一写入者。 */
    QueueHandle_t xPublishReceiptQueue; /* 业务独占消费。 */
} AppMqttBusinessTaskContext_t;

static const char s_cHeartbeatTopic[] = "Client2Server/heartbeat";
static const char s_cHeartbeatJsonFormat[] =
    "{\"header\":{\"method\":\"heartbeat\",\"messageId\":\"1\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{\"result\":true}}";

static TaskHandle_t s_xBusinessTaskHandle = NULL;
static AppMqttBusinessTaskContext_t s_xBusinessTaskContext = {0};
static AppMqttBusinessStatus_t s_xMqttStatus = {0};

/* 大消息放在本Task私有静态存储中，避免同时压入任务栈。 */
static AppMqttInboundMessage_t s_xInboundMessage;
static AppMqttOutboundMessage_t s_xOutboundMessage;
static AppFtpDownloadCommand_t s_xFtpCommand;
static AppFtpStatus_t s_xFtpStatus = {0};

static TickType_t s_xLastUptimeTick = 0U;
static TickType_t s_xLastHeartbeatTick = 0U;
static TickType_t s_xLastQueueAttemptTick = 0U; // 上一次尝试发送的Tick
static uint64_t s_ullUptimeTicks = 0U;
static bool s_xHeartbeatDue = false;
static bool s_xQueueAttempted = false;

/* 调试器可直接观察这些计数，不需要每10秒打印串口。 */
static volatile uint32_t s_ulHeartbeatQueuedCount = 0U;
static volatile uint32_t s_ulHeartbeatQueueFullCount = 0U;
static volatile uint32_t s_ulHeartbeatFormatFailedCount = 0U;

#define APP_MQTT_BUSINESS_TEMPERATURE_PERIOD_MS       10000U

static const char s_cTemperatureTopic[] = "Client2Server/temperature";
static const char s_cTemperatureJsonFormat[] =
    "{\"header\":{\"method\":\"temperature\",\"messageId\":\"0\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{"
    "\"CPUTemp\":%s%lu.%01lu,\"PotentiometerVoltage\":%lu.%03lu}}";

static TickType_t s_xLastTemperatureTick = 0U;
static TickType_t s_xLastTemperatureQueueAttemptTick = 0U;
static bool s_xTemperatureDue = false;
static bool s_xTemperatureQueueAttempted = false;

static volatile uint32_t s_ulTemperatureQueuedCount = 0U;
static volatile uint32_t s_ulTemperatureQueueFullCount = 0U;
static volatile uint32_t s_ulTemperatureUnavailableCount = 0U;


typedef struct
{
    const char *pcName;
    size_t xLength;
} AppJsonExpectedKey_t;

static const char s_cHeaderKey[] = "header";
static const char s_cBodyKey[] = "body";
static const char s_cMethodKey[] = "method";
static const char s_cMessageIdKey[] = "messageId";
static const char s_cVersionKey[] = "version";
static const char s_cTimestampKey[] = "timestamp";
static const char s_cDeviceIdKey[] = "deviceId";
static const char s_cControlTypeKey[] = "controlType";
static const char s_cControlKey[] = "control";
static const char s_cFtpServerKey[] = "ftpServer";
static const char s_cFtpPortKey[] = "ftpPort";
static const char s_cFtpUserKey[] = "ftpUser";
static const char s_cFtpPasswordKey[] = "ftpPassword";
static const char s_cFtpPathKey[] = "ftpPath";
static const char s_cFtpFileNameKey[] = "ftpFileName";
static const char s_cFtpMd5Key[] = "ftpMD5";

static const char s_cMethodQuery[] = "header.method";
static const char s_cMessageIdQuery[] = "header.messageId";
static const char s_cVersionQuery[] = "header.version";
static const char s_cTimestampQuery[] = "header.timestamp";
static const char s_cDeviceIdQuery[] = "header.deviceId";
static const char s_cControlTypeQuery[] = "body.controlType";
static const char s_cControlQuery[] = "body.control";
static const char s_cFtpServerQuery[] = "body.ftpServer";
static const char s_cFtpPortQuery[] = "body.ftpPort";
static const char s_cFtpUserQuery[] = "body.ftpUser";
static const char s_cFtpPasswordQuery[] = "body.ftpPassword";
static const char s_cFtpPathQuery[] = "body.ftpPath";
static const char s_cFtpFileNameQuery[] = "body.ftpFileName";
static const char s_cFtpMd5Query[] = "body.ftpMD5";

static const char s_cExpectedControlMethod[] = "control";
static const char s_cExpectedControlMessageId[] = "4";
static const char s_cExpectedFtpMethod[] = "filedownloadcommand";
static const char s_cExpectedFtpMessageId[] = "6";
static const char s_cExpectedFtpServer[] = "192.168.101.11";
static const char s_cExpectedVersion[] = "0.0.2";
static const char s_cExpectedDeviceId[] = "192.168.101.50";

static const AppJsonExpectedKey_t s_xRootKeys[] =
{
    {s_cHeaderKey, sizeof(s_cHeaderKey) - 1U},
    {s_cBodyKey, sizeof(s_cBodyKey) - 1U}
};

static const AppJsonExpectedKey_t s_xHeaderKeys[] =
{
    {s_cMethodKey, sizeof(s_cMethodKey) - 1U},
    {s_cMessageIdKey, sizeof(s_cMessageIdKey) - 1U},
    {s_cVersionKey, sizeof(s_cVersionKey) - 1U},
    {s_cTimestampKey, sizeof(s_cTimestampKey) - 1U},
    {s_cDeviceIdKey, sizeof(s_cDeviceIdKey) - 1U}
};

static const AppJsonExpectedKey_t s_xControlBodyKeys[] =
{
    {s_cControlTypeKey, sizeof(s_cControlTypeKey) - 1U},
    {s_cControlKey, sizeof(s_cControlKey) - 1U}
};

static const AppJsonExpectedKey_t s_xFtpBodyKeys[] =
{
    {s_cFtpServerKey, sizeof(s_cFtpServerKey) - 1U},
    {s_cFtpPortKey, sizeof(s_cFtpPortKey) - 1U},
    {s_cFtpUserKey, sizeof(s_cFtpUserKey) - 1U},
    {s_cFtpPasswordKey, sizeof(s_cFtpPasswordKey) - 1U},
    {s_cFtpPathKey, sizeof(s_cFtpPathKey) - 1U},
    {s_cFtpFileNameKey, sizeof(s_cFtpFileNameKey) - 1U},
    {s_cFtpMd5Key, sizeof(s_cFtpMd5Key) - 1U}
};

static const char s_cControlAckTopic[] = "Client2Server/control";
static const char s_cControlAckJsonFormat[] =
    "{\"header\":{\"method\":\"control\",\"messageId\":\"4\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{"
    "\"controlType\":%lu,\"control\":%lu,\"result\":%s}}";

static const char s_cFtpAckTopic[] = "Client2Server/filedownloadcommand";
static const char s_cFtpAckJsonFormat[] =
    "{\"header\":{\"method\":\"filedownloadcommand\",\"messageId\":\"6\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\",\"ftpServer\":\"%s\"},"
    "\"body\":{\"result\":%s}}";
static const char s_cFtpResultTopic[] = "Client2Server/filedownloadresult";
static const char s_cFtpResultJsonFormat[] =
    "{\"header\":{\"method\":\"filedownloadresult\",\"messageId\":\"2\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{"
    "\"fileDownloadResult\":%u,\"fileDownloadRate\":%u}}";

static char s_acPendingFtpAckServer[16];
static uint32_t s_ulPendingFtpAckSession = 0U;
static uint32_t s_ulNextFtpTransactionId = 1U;
static uint32_t s_ulPendingFtpResultTransaction = 0U;
static uint32_t s_ulLastFtpResultTransaction = 0U;
static uint8_t s_ucPendingFtpResult = 0U;
static uint8_t s_ucPendingFtpRate = 0U;
static uint8_t s_ucLastFtpResult = 0U;
static uint8_t s_ucLastFtpRate = UINT8_MAX;
static bool s_xHasFtpStatus = false;
static bool s_xPendingFtpAck = false;
static bool s_xPendingFtpAckResult = false;
static bool s_xPendingFtpResult = false;

static volatile uint32_t s_ulControlAppliedCount = 0U;
static volatile uint32_t s_ulControlRejectedCount = 0U;
static volatile uint32_t s_ulControlAckQueuedCount = 0U;
static volatile uint32_t s_ulControlAckDroppedCount = 0U;

#define APP_UPGRADE_STORAGE_WAIT_MS    5000U
#define APP_UPGRADE_PUBLISH_WAIT_MS    15000U

typedef enum
{
    APP_UPGRADE_IDLE = 0,
    APP_UPGRADE_START_COMMIT,
    APP_UPGRADE_START_COMMIT_CANCEL,
    APP_UPGRADE_START_ACK,
    APP_UPGRADE_START_ACK_CANCEL,
    APP_UPGRADE_PROGRESS,
    APP_UPGRADE_PROGRESS_CANCEL,
    APP_UPGRADE_RESET_READY,
    APP_UPGRADE_CANCEL_COMMIT,
    APP_UPGRADE_CANCEL_ACK,
    APP_UPGRADE_NOTICE,
    APP_UPGRADE_LATE_STORAGE,
    APP_UPGRADE_LATE_STORAGE_CANCEL,
    APP_UPGRADE_LATE_PUBLISH,
    APP_UPGRADE_LATE_PUBLISH_CANCEL
} AppUpgradeFlowState_t;

typedef struct
{
    AppUpgradeFlowState_t xState; /* 本模块唯一升级执行进度。 */
    uint32_t ulRequestId;         /* 当前只等待一笔Storage或发送回执。 */
    uint32_t ulMetadataGeneration; /* 防止把另一候选当成本次受理镜像。 */
    uint32_t ulMqttSession;       /* 当前命令/发布的原会话。 */
    uint32_t ulNextRequestId;     /* 本机非零编号，不写Flash或JSON。 */
    uint32_t ulResultSession;     /* 本次运行最近尝试补报的READY会话。 */
    TickType_t xStartTick;        /* 当前在途请求的期限起点。 */
} AppUpgradeFlow_t;

static AppUpgradeFlow_t s_xUpgrade = {.ulNextRequestId = 1U};
/* Queue按值复制；工作缓存不压入512 words的业务Task栈。 */
static AppStorageRequest_t s_xUpgradeRequest;

static const char s_cUpgradeKey[] = "upgrade";
static const char s_cUpgradeQuery[] = "body.upgrade";
static const char s_cUpgradeMethod[] = "fileupgradecommand";
static const char s_cUpgradeMessageId[] = "8";
static const AppJsonExpectedKey_t s_xUpgradeBodyKeys[] =
{
    {s_cUpgradeKey, sizeof(s_cUpgradeKey) - 1U}
};
static const char s_cUpgradeAckTopic[] = "Client2Server/fileupgradecommand";
static const char s_cUpgradeAckFormat[] =
    "{\"header\":{\"method\":\"fileupgradecommand\",\"messageId\":\"8\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{\"result\":%s}}";
static const char s_cUpgradeResultTopic[] = "Client2Server/filedownloadprocess";
static const char s_cUpgradeResultFormat[] =
    "{\"header\":{\"method\":\"filedownloadprocess\",\"messageId\":\"3\","
    "\"version\":\"0.0.2\",\"timestamp\":%lu,"
    "\"deviceId\":\"192.168.101.50\"},\"body\":{\"upgrade\":%u,\"rate\":%u}}";

static bool prvUpgradeDownloadAllowed(void);
static void prvHandleUpgradeCommand(void);
static void prvServiceUpgrade(TickType_t xNowTick);

static void prvIncrementCounter(volatile uint32_t *pulCounter)
{
    if (*pulCounter < UINT32_MAX)
    {
        (*pulCounter)++;
    }
}

static bool prvHasExpired(TickType_t xStartTick, TickType_t xNowTick, uint32_t ulDelayMs)
{
    return (TickType_t)(xNowTick - xStartTick) >= pdMS_TO_TICKS(ulDelayMs);
}

static void prvUpdateUptime(TickType_t xNowTick)
{
    TickType_t xElapsedTicks = (TickType_t)(xNowTick - s_xLastUptimeTick);

    /* 无符号差值可跨一次32位Tick回绕，累计值本身保持64位。 */
    s_ullUptimeTicks += (uint64_t)xElapsedTicks;
    s_xLastUptimeTick = xNowTick;
}

static uint64_t prvGetUptimeSeconds(void)
{
    return s_ullUptimeTicks / (uint64_t)configTICK_RATE_HZ;
}

static void prvScheduleLatestFtpResult(void);

static void prvReceiveMqttStatus(QueueHandle_t xBusinessStatusQueue)
{
    AppMqttBusinessStatus_t xNewStatus;

    if (xQueueReceive(xBusinessStatusQueue, &xNewStatus, 0U) != pdPASS)
    {
        return;
    }

    if (xNewStatus.xReady &&
        ((s_xMqttStatus.xReady == false) ||
         (xNewStatus.ulSession != s_xMqttStatus.ulSession)))
    {
        /* 新READY Session立即生成第一条心跳。 */
        s_xHeartbeatDue = true;
        s_xQueueAttempted = false;


        s_xTemperatureDue = true;
        s_xTemperatureQueueAttempted = false;

        /* QoS 0旧Session中的待发消息失效，新Session只补当前最新FTP状态。 */
        s_xPendingFtpAck = false;
        s_xPendingFtpResult = false;
        s_ulLastFtpResultTransaction = 0U;
        s_ucLastFtpResult = 0U;
        s_ucLastFtpRate = UINT8_MAX;
    }
    else if (xNewStatus.xReady == false)
    {
        /* 断线不积压周期消息；运行秒数继续累计。 */
        s_xHeartbeatDue = false;
        s_xQueueAttempted = false;

        s_xTemperatureDue = false;
        s_xTemperatureQueueAttempted = false;
        s_xPendingFtpAck = false;
        s_xPendingFtpResult = false;
    }

    s_xMqttStatus = xNewStatus;
    if (xNewStatus.xReady)
    {
        prvScheduleLatestFtpResult();
    }
}

static bool prvParseControlCommand(const uint8_t *pucPayload, uint16_t usPayloadLength,
                                   uint32_t *pulControlType, uint32_t *pulControl);
static bool prvQueueControlAck(QueueHandle_t xOutboundQueue, uint32_t ulControlType,
                               uint32_t ulControl, bool xResult);
static void prvHandleFtpDownloadCommand(QueueHandle_t xFtpCommandQueue);
static void prvReceiveFtpStatus(QueueHandle_t xFtpStatusQueue);
static void prvServicePendingFtpMessages(QueueHandle_t xOutboundQueue);

static void prvProcessInboundMessages(QueueHandle_t xInboundQueue,
                                      QueueHandle_t xOutboundQueue,
                                      QueueHandle_t xFtpCommandQueue)
{
    AppOutputState_t xAppliedState;
    AppOutputTarget_t xTarget;
    uint32_t ulControlType;
    uint32_t ulControl;
    uint32_t ulAttempt;
    bool xResult;

    for (ulAttempt = 0U; ulAttempt < APP_MQTT_INBOUND_QUEUE_LENGTH; ulAttempt++)
    {
        if (s_xPendingFtpAck && (s_xUpgrade.xState == APP_UPGRADE_IDLE))
        {
            break;
        }
        if (xQueueReceive(xInboundQueue, &s_xInboundMessage, 0U) != pdPASS)
        {
            break;
        }

        if ((s_xMqttStatus.xReady == false) ||
            (s_xInboundMessage.ulMqttSession != s_xMqttStatus.ulSession))
        {
            if (s_xInboundMessage.eMethod == APP_MQTT_INBOUND_METHOD_CONTROL)
            {
                prvIncrementCounter(&s_ulControlRejectedCount);
            }
            else
            {
                memset(s_xInboundMessage.aucPayload, 0,
                       s_xInboundMessage.usPayloadLength);
            }
            continue;
        }

        if (s_xInboundMessage.eMethod == APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND)
        {
            prvHandleUpgradeCommand();
            memset(s_xInboundMessage.aucPayload, 0, s_xInboundMessage.usPayloadLength);
            continue;
        }
        if (s_xInboundMessage.eMethod == APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND)
        {
            if (s_xPendingFtpAck == false)
            {
                prvHandleFtpDownloadCommand(xFtpCommandQueue);
            }
            memset(s_xInboundMessage.aucPayload, 0, s_xInboundMessage.usPayloadLength);
            continue;
        }
        if ((s_xInboundMessage.eMethod != APP_MQTT_INBOUND_METHOD_CONTROL) ||
            (prvParseControlCommand(s_xInboundMessage.aucPayload,
                                    s_xInboundMessage.usPayloadLength,
                                    &ulControlType, &ulControl) == false))
        {
            prvIncrementCounter(&s_ulControlRejectedCount);
            continue;
        }

        xResult = false;
        if ((s_xUpgrade.xState == APP_UPGRADE_IDLE) &&
            ((ulControlType == 1U) || (ulControlType == 2U)))
        {
            if ((ulControl == 1U) || (ulControl == 2U))
            {
                xTarget = (ulControlType == 1U) ? APP_OUTPUT_TARGET_RED_LED :
                                                  APP_OUTPUT_TARGET_BUZZER;
                xResult = AppOutputControl_SetOne(xTarget, ulControl == 1U,
                                                  &xAppliedState);
            }
        }

        if (xResult)
        {
            prvIncrementCounter(&s_ulControlAppliedCount);
        }
        else
        {
            prvIncrementCounter(&s_ulControlRejectedCount);
        }

        if (prvQueueControlAck(xOutboundQueue, ulControlType, ulControl, xResult))
        {
            prvIncrementCounter(&s_ulControlAckQueuedCount);
        }
        else
        {
            prvIncrementCounter(&s_ulControlAckDroppedCount);
        }
    }
}

static bool prvBuildHeartbeat(void)
{
    uint64_t ullUptimeSeconds = prvGetUptimeSeconds();
    int iLength;

    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cHeartbeatTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cHeartbeatTopic, s_xOutboundMessage.usTopicLength);

    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload),
                       s_cHeartbeatJsonFormat,
                       (unsigned long)ullUptimeSeconds);
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }

    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    return true;
}

static void prvServiceHeartbeat(QueueHandle_t xOutboundQueue, TickType_t xNowTick)
{
    if (s_xMqttStatus.xReady == false)
    {
        return;
    }

    if ((s_xHeartbeatDue == false) &&
        prvHasExpired(s_xLastHeartbeatTick, xNowTick,
                      APP_MQTT_BUSINESS_HEARTBEAT_PERIOD_MS))
    {
        s_xHeartbeatDue = true;
        s_xQueueAttempted = false;
    }

    if (s_xHeartbeatDue == false)
    {
        return;
    }

    if (s_xQueueAttempted &&
        (prvHasExpired(s_xLastQueueAttemptTick, xNowTick,
                       APP_MQTT_BUSINESS_QUEUE_RETRY_MS) == false))
    {
        return;
    }

    s_xQueueAttempted = true;
    s_xLastQueueAttemptTick = xNowTick;

    if (prvBuildHeartbeat() == false)
    {
        prvIncrementCounter(&s_ulHeartbeatFormatFailedCount);
        s_xHeartbeatDue = false;
        s_xLastHeartbeatTick = xNowTick;
        return;
    }

    if (xQueueSend(xOutboundQueue, &s_xOutboundMessage, 0U) == pdPASS)
    {
        prvIncrementCounter(&s_ulHeartbeatQueuedCount);
        s_xHeartbeatDue = false;
        s_xQueueAttempted = false;
        s_xLastHeartbeatTick = xNowTick;
    }
    else
    {
        /* Queue满时保留due，每1秒重试；不阻塞MQTT ProcessLoop。 */
        prvIncrementCounter(&s_ulHeartbeatQueueFullCount);
    }
}

static bool prvBuildTemperature(void);

static void prvServiceTemperature(QueueHandle_t xOutboundQueue, TickType_t xNowTick)
{
    if (s_xMqttStatus.xReady == false)
    {
        return;
    }

    if ((s_xTemperatureDue == false) &&
        prvHasExpired(s_xLastTemperatureTick, xNowTick,
                      APP_MQTT_BUSINESS_TEMPERATURE_PERIOD_MS))
    {
        s_xTemperatureDue = true;
        s_xTemperatureQueueAttempted = false;
    }

    if (s_xTemperatureDue == false)
    {
        return;
    }

    if (s_xTemperatureQueueAttempted &&
        (prvHasExpired(s_xLastTemperatureQueueAttemptTick, xNowTick,
                       APP_MQTT_BUSINESS_QUEUE_RETRY_MS) == false))
    {
        return;
    }

    s_xTemperatureQueueAttempted = true;
    s_xLastTemperatureQueueAttemptTick = xNowTick;

    if (prvBuildTemperature() == false)
    {
        prvIncrementCounter(&s_ulTemperatureUnavailableCount);
        s_xTemperatureDue = false;
        s_xTemperatureQueueAttempted = false;
        s_xLastTemperatureTick = xNowTick;
        return;
    }

    if (xQueueSend(xOutboundQueue, &s_xOutboundMessage, 0U) == pdPASS)
    {
        prvIncrementCounter(&s_ulTemperatureQueuedCount);
        s_xTemperatureDue = false;
        s_xTemperatureQueueAttempted = false;
        s_xLastTemperatureTick = xNowTick;
    }
    else
    {
        prvIncrementCounter(&s_ulTemperatureQueueFullCount);
    }
}
static uint32_t prvInt32Magnitude(int32_t lValue)
{
    if (lValue >= 0)
    {
        return (uint32_t)lValue;
    }

    return (uint32_t)(-(lValue + 1)) + 1U;
}

static bool prvBuildTemperature(void)
{
    AppAdcSample_t xSample;
    int32_t lTemperatureTenths;
    uint32_t ulTemperatureMagnitude;
    uint32_t ulVoltageMillivolts;
    uint64_t ullUptimeSeconds;
    const char *pcTemperatureSign;
    int iLength;

    if ((AppAdc_TryReadLatestSample(&xSample) == false) || (xSample.xValid == false))
    {
        return false;
    }

    if (xSample.fCpuTemperatureCelsius >= 0.0f)
    {
        lTemperatureTenths = (int32_t)(xSample.fCpuTemperatureCelsius * 10.0f + 0.5f);
    }
    else
    {
        lTemperatureTenths = (int32_t)(xSample.fCpuTemperatureCelsius * 10.0f - 0.5f);
    }

    ulVoltageMillivolts = (uint32_t)(xSample.fPotentiometerVoltage * 1000.0f + 0.5f);
    ulTemperatureMagnitude = prvInt32Magnitude(lTemperatureTenths);
    pcTemperatureSign = (lTemperatureTenths < 0) ? "-" : "";
    ullUptimeSeconds = prvGetUptimeSeconds();

    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cTemperatureTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cTemperatureTopic,
           s_xOutboundMessage.usTopicLength);

    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload),
                       s_cTemperatureJsonFormat,
                       (unsigned long)ullUptimeSeconds,
                       pcTemperatureSign,
                       (unsigned long)(ulTemperatureMagnitude / 10U),
                       (unsigned long)(ulTemperatureMagnitude % 10U),
                       (unsigned long)(ulVoltageMillivolts / 1000U),
                       (unsigned long)(ulVoltageMillivolts % 1000U));
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }

    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    return true;
}

static void prvMqttBusinessTask(void *pvParameters)
{
    AppMqttBusinessTaskContext_t *pxContext = (AppMqttBusinessTaskContext_t *)pvParameters;
    TickType_t xLastWakeTick;
    TickType_t xNowTick;
    char acVersionReport[48];
    const char *pcSoftware;
    int iLength;

    configASSERT(pxContext != NULL);
    configASSERT(pxContext->xInboundQueue != NULL);
    configASSERT(pxContext->xOutboundQueue != NULL);
    configASSERT(pxContext->xBusinessStatusQueue != NULL);
    configASSERT(pxContext->xFtpCommandQueue != NULL);
    configASSERT(pxContext->xFtpStatusQueue != NULL);
    configASSERT(pxContext->xStorageRequestQueue != NULL);
    configASSERT(pxContext->xStorageStatusQueue != NULL);
    configASSERT(pxContext->xPublishReceiptQueue != NULL);
    configASSERT(configTICK_RATE_HZ > 0U);

    /* 已装应用自身描述头，不能从会被新下载覆盖的候选槽读取版本。 */
    pcSoftware = (const char *)(uintptr_t)(APP_STORAGE_APPLICATION_TARGET_ADDRESS +
                                          APP_FIRMWARE_HARDWARE_MODEL_SIZE);
    iLength = snprintf(acVersionReport, sizeof(acVersionReport), "APP: software=%.*s\r\n",
                       (int)APP_FIRMWARE_SOFTWARE_MODEL_SIZE, pcSoftware);
    if ((iLength > 0) && (iLength < (int)sizeof(acVersionReport)))
    {
        (void)AppSerial_Write((const uint8_t *)acVersionReport, (uint16_t)iLength);
    }
    xLastWakeTick = xTaskGetTickCount();
    s_xLastUptimeTick = xLastWakeTick;
    s_ullUptimeTicks = (uint64_t)xLastWakeTick;

    for (;;)
    {
        xNowTick = xTaskGetTickCount();
        prvUpdateUptime(xNowTick);
        prvReceiveMqttStatus(pxContext->xBusinessStatusQueue);
        prvReceiveFtpStatus(pxContext->xFtpStatusQueue);
        /* 先有界消费已到达的取消，再处理发送回执和最终复位。 */
        prvProcessInboundMessages(pxContext->xInboundQueue, pxContext->xOutboundQueue,
                                  pxContext->xFtpCommandQueue);
        prvServiceUpgrade(xTaskGetTickCount());
        prvServicePendingFtpMessages(pxContext->xOutboundQueue);
        if ((s_xUpgrade.xState == APP_UPGRADE_IDLE) &&
            (s_xPendingFtpAck == false) && (s_xPendingFtpResult == false))
        {
            prvServiceHeartbeat(pxContext->xOutboundQueue, xNowTick);
            prvServiceTemperature(pxContext->xOutboundQueue, xNowTick);
        }
        vTaskDelayUntil(&xLastWakeTick, pdMS_TO_TICKS(APP_MQTT_BUSINESS_SERVICE_PERIOD_MS));
    }
}

bool AppMqttBusinessTask_Create(QueueHandle_t xInboundQueue,
                                QueueHandle_t xOutboundQueue,
                                QueueHandle_t xBusinessStatusQueue,
                                QueueHandle_t xFtpCommandQueue,
                                QueueHandle_t xFtpStatusQueue,
                                QueueHandle_t xStorageRequestQueue,
                                QueueHandle_t xStorageStatusQueue,
                                QueueHandle_t xPublishReceiptQueue)
{
    BaseType_t xCreateResult;

    if ((s_xBusinessTaskHandle != NULL) || (xInboundQueue == NULL) ||
        (xOutboundQueue == NULL) || (xBusinessStatusQueue == NULL) ||
        (xFtpCommandQueue == NULL) || (xFtpStatusQueue == NULL) ||
        (xStorageRequestQueue == NULL) || (xStorageStatusQueue == NULL) ||
        (xPublishReceiptQueue == NULL))
    {
        return false;
    }
    s_xBusinessTaskContext.xInboundQueue = xInboundQueue;
    s_xBusinessTaskContext.xOutboundQueue = xOutboundQueue;
    s_xBusinessTaskContext.xBusinessStatusQueue = xBusinessStatusQueue;
    s_xBusinessTaskContext.xFtpCommandQueue = xFtpCommandQueue;
    s_xBusinessTaskContext.xFtpStatusQueue = xFtpStatusQueue;
    s_xBusinessTaskContext.xStorageRequestQueue = xStorageRequestQueue;
    s_xBusinessTaskContext.xStorageStatusQueue = xStorageStatusQueue;
    s_xBusinessTaskContext.xPublishReceiptQueue = xPublishReceiptQueue;

    xCreateResult = xTaskCreate(prvMqttBusinessTask, "MQTT_BUS",
                                APP_MQTT_BUSINESS_TASK_STACK_DEPTH,
                                &s_xBusinessTaskContext, APP_MQTT_BUSINESS_TASK_PRIORITY,
                                &s_xBusinessTaskHandle);
    if (xCreateResult != pdPASS)
    {
        s_xBusinessTaskHandle = NULL;
        s_xBusinessTaskContext = (AppMqttBusinessTaskContext_t){0};
        return false;
    }
    return true;
}

static bool prvValueEquals(const char *pcValue, size_t xValueLength,
                           const char *pcExpected, size_t xExpectedLength)
{
    return (xValueLength == xExpectedLength) &&
           (memcmp(pcValue, pcExpected, xExpectedLength) == 0);
}

static bool prvSearchTypedValue(const uint8_t *pucPayload, uint16_t usPayloadLength,
                                const char *pcQuery, size_t xQueryLength,
                                JSONTypes_t xExpectedType, const char **ppcValue,
                                size_t *pxValueLength)
{
    JSONTypes_t xActualType = JSONInvalid;
    JSONStatus_t xStatus;

    xStatus = JSON_SearchConst((const char *)pucPayload, (size_t)usPayloadLength,
                               pcQuery, xQueryLength, ppcValue, pxValueLength,
                               &xActualType);
    return (xStatus == JSONSuccess) && (xActualType == xExpectedType);
}

static bool prvSearchStringEquals(const uint8_t *pucPayload, uint16_t usPayloadLength,
                                  const char *pcQuery, size_t xQueryLength,
                                  const char *pcExpected, size_t xExpectedLength)
{
    const char *pcValue;
    size_t xValueLength;

    if (prvSearchTypedValue(pucPayload, usPayloadLength, pcQuery, xQueryLength,
                            JSONString, &pcValue, &xValueLength) == false)
    {
        return false;
    }

    return prvValueEquals(pcValue, xValueLength, pcExpected, xExpectedLength);
}

static bool prvObjectHasExactKeys(const char *pcObject, size_t xObjectLength,
                                  const AppJsonExpectedKey_t *pxExpectedKeys,
                                  uint32_t ulExpectedKeyCount)
{
    JSONPair_t xPair = {0};
    JSONStatus_t xStatus;
    size_t xStart = 0U;
    size_t xNext = 0U;
    uint32_t ulFoundMask = 0U;
    uint32_t ulKeyIndex;
    bool xMatched;

    if ((pcObject == NULL) || (xObjectLength == 0U) ||
        (pxExpectedKeys == NULL) || (ulExpectedKeyCount == 0U) ||
        (ulExpectedKeyCount >= 32U))
    {
        return false;
    }

    xStatus = JSON_Iterate(pcObject, xObjectLength, &xStart, &xNext, &xPair);
    while (xStatus == JSONSuccess)
    {
        xMatched = false;
        for (ulKeyIndex = 0U; ulKeyIndex < ulExpectedKeyCount; ulKeyIndex++)
        {
            if ((xPair.keyLength == pxExpectedKeys[ulKeyIndex].xLength) &&
                (memcmp(xPair.key, pxExpectedKeys[ulKeyIndex].pcName,
                        xPair.keyLength) == 0))
            {
                if ((ulFoundMask & (1UL << ulKeyIndex)) != 0U)
                {
                    return false;
                }

                ulFoundMask |= 1UL << ulKeyIndex;
                xMatched = true;
                break;
            }
        }

        if (xMatched == false)
        {
            return false;
        }

        xStatus = JSON_Iterate(pcObject, xObjectLength, &xStart, &xNext, &xPair);
    }

    return (xStatus == JSONNotFound) &&
           (ulFoundMask == ((1UL << ulExpectedKeyCount) - 1UL));
}

static bool prvParseUint64(const char *pcValue, size_t xValueLength, uint64_t *pullValue)
{
    uint64_t ullResult = 0U;
    uint64_t ullDigit;
    size_t xIndex;

    if ((pcValue == NULL) || (xValueLength == 0U) || (pullValue == NULL))
    {
        return false;
    }

    if ((xValueLength > 1U) && (pcValue[0] == '0'))
    {
        return false;
    }

    for (xIndex = 0U; xIndex < xValueLength; xIndex++)
    {
        if ((pcValue[xIndex] < '0') || (pcValue[xIndex] > '9'))
        {
            return false;
        }

        ullDigit = (uint64_t)(pcValue[xIndex] - '0');
        if (ullResult > ((UINT64_MAX - ullDigit) / 10U))
        {
            return false;
        }
        ullResult = (ullResult * 10U) + ullDigit;
    }

    *pullValue = ullResult;
    return true;
}


static bool prvParseControlCommand(const uint8_t *pucPayload, uint16_t usPayloadLength,
                                   uint32_t *pulControlType, uint32_t *pulControl)
{
    const char *pcHeader;
    const char *pcBody;
    const char *pcValue;
    size_t xHeaderLength;
    size_t xBodyLength;
    size_t xValueLength;
    uint64_t ullValue;

    if ((pucPayload == NULL) || (usPayloadLength == 0U) ||
        (pulControlType == NULL) || (pulControl == NULL))
    {
        return false;
    }

    if (JSON_Validate((const char *)pucPayload, usPayloadLength) != JSONSuccess)
    {
        return false;
    }

    if ((prvSearchTypedValue(pucPayload, usPayloadLength, s_cHeaderKey,
                             sizeof(s_cHeaderKey) - 1U, JSONObject,
                             &pcHeader, &xHeaderLength) == false) ||
        (prvSearchTypedValue(pucPayload, usPayloadLength, s_cBodyKey,
                             sizeof(s_cBodyKey) - 1U, JSONObject,
                             &pcBody, &xBodyLength) == false))
    {
        return false;
    }

    if ((prvObjectHasExactKeys((const char *)pucPayload, usPayloadLength, s_xRootKeys,
                               sizeof(s_xRootKeys) / sizeof(s_xRootKeys[0])) == false) ||
        (prvObjectHasExactKeys(pcHeader, xHeaderLength, s_xHeaderKeys,
                               sizeof(s_xHeaderKeys) / sizeof(s_xHeaderKeys[0])) == false) ||
        (prvObjectHasExactKeys(pcBody, xBodyLength, s_xControlBodyKeys,
                               sizeof(s_xControlBodyKeys) /
                               sizeof(s_xControlBodyKeys[0])) == false))
    {
        return false;
    }

    if ((prvSearchStringEquals(pucPayload, usPayloadLength, s_cMethodQuery,
                               sizeof(s_cMethodQuery) - 1U, s_cExpectedControlMethod,
                               sizeof(s_cExpectedControlMethod) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cMessageIdQuery,
                               sizeof(s_cMessageIdQuery) - 1U,
                               s_cExpectedControlMessageId,
                               sizeof(s_cExpectedControlMessageId) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cVersionQuery,
                               sizeof(s_cVersionQuery) - 1U, s_cExpectedVersion,
                               sizeof(s_cExpectedVersion) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cDeviceIdQuery,
                               sizeof(s_cDeviceIdQuery) - 1U, s_cExpectedDeviceId,
                               sizeof(s_cExpectedDeviceId) - 1U) == false))
    {
        return false;
    }

    if ((prvSearchTypedValue(pucPayload, usPayloadLength, s_cTimestampQuery,
                             sizeof(s_cTimestampQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false))
    {
        return false;
    }

    if ((prvSearchTypedValue(pucPayload, usPayloadLength, s_cControlTypeQuery,
                             sizeof(s_cControlTypeQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false) ||
        (ullValue > UINT32_MAX))
    {
        return false;
    }
    *pulControlType = (uint32_t)ullValue;

    if ((prvSearchTypedValue(pucPayload, usPayloadLength, s_cControlQuery,
                             sizeof(s_cControlQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false) ||
        (ullValue > UINT32_MAX))
    {
        return false;
    }
    *pulControl = (uint32_t)ullValue;
    return true;
}

static bool prvQueueControlAck(QueueHandle_t xOutboundQueue, uint32_t ulControlType,
                               uint32_t ulControl, bool xResult)
{
    int iLength;

    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cControlAckTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cControlAckTopic,
           s_xOutboundMessage.usTopicLength);

    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload),
                       s_cControlAckJsonFormat,
                       (unsigned long)prvGetUptimeSeconds(),
                       (unsigned long)ulControlType,
                       (unsigned long)ulControl,
                       xResult ? "true" : "false");
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }

    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    return xQueueSend(xOutboundQueue, &s_xOutboundMessage, 0U) == pdPASS;
}

static bool prvCopyJsonString(const uint8_t *pucPayload, uint16_t usPayloadLength,
                              const char *pcQuery, size_t xQueryLength, char *pcDestination,
                              uint16_t usMaximumLength, uint16_t *pusLength)
{
    const char *pcValue;
    size_t xValueLength;
    size_t xIndex;

    if ((pcDestination == NULL) || (pusLength == NULL) ||
        (prvSearchTypedValue(pucPayload, usPayloadLength, pcQuery, xQueryLength,
                             JSONString, &pcValue, &xValueLength) == false) ||
        (xValueLength == 0U) || (xValueLength > usMaximumLength))
    {
        return false;
    }

    for (xIndex = 0U; xIndex < xValueLength; xIndex++)
    {
        if (((uint8_t)pcValue[xIndex] < 0x20U) ||
            ((uint8_t)pcValue[xIndex] > 0x7EU) || (pcValue[xIndex] == '\\'))
        {
            return false;
        }
    }

    memcpy(pcDestination, pcValue, xValueLength);
    pcDestination[xValueLength] = '\0';
    *pusLength = (uint16_t)xValueLength;
    return true;
}

static bool prvParseIpv4(const char *pcText, uint16_t usLength, uint8_t pucAddress[4])
{
    uint32_t ulPart = 0U;
    uint16_t usIndex;
    uint8_t ucPartIndex = 0U;
    bool xHasDigit = false;

    for (usIndex = 0U; usIndex <= usLength; usIndex++)
    {
        if ((usIndex < usLength) && (pcText[usIndex] >= '0') &&
            (pcText[usIndex] <= '9'))
        {
            xHasDigit = true;
            ulPart = (ulPart * 10U) + (uint32_t)(pcText[usIndex] - '0');
            if (ulPart > 255U)
            {
                return false;
            }
        }
        else if (((usIndex == usLength) || (pcText[usIndex] == '.')) && xHasDigit &&
                 (ucPartIndex < 4U))
        {
            pucAddress[ucPartIndex++] = (uint8_t)ulPart;
            ulPart = 0U;
            xHasDigit = false;
        }
        else
        {
            return false;
        }
    }
    return ucPartIndex == 4U;
}

static bool prvDecodeMd5(const char *pcText, uint16_t usLength,
                         uint8_t pucDigest[APP_FTP_MD5_DIGEST_SIZE])
{
    uint16_t usIndex;
    uint8_t ucHigh;
    uint8_t ucLow;
    char cCharacter;

    if (usLength != (APP_FTP_MD5_DIGEST_SIZE * 2U))
    {
        return false;
    }

    for (usIndex = 0U; usIndex < APP_FTP_MD5_DIGEST_SIZE; usIndex++)
    {
        cCharacter = pcText[usIndex * 2U];
        if ((cCharacter >= '0') && (cCharacter <= '9'))
        {
            ucHigh = (uint8_t)(cCharacter - '0');
        }
        else if ((cCharacter >= 'a') && (cCharacter <= 'f'))
        {
            ucHigh = (uint8_t)(cCharacter - 'a' + 10);
        }
        else if ((cCharacter >= 'A') && (cCharacter <= 'F'))
        {
            ucHigh = (uint8_t)(cCharacter - 'A' + 10);
        }
        else
        {
            return false;
        }

        cCharacter = pcText[(usIndex * 2U) + 1U];
        if ((cCharacter >= '0') && (cCharacter <= '9'))
        {
            ucLow = (uint8_t)(cCharacter - '0');
        }
        else if ((cCharacter >= 'a') && (cCharacter <= 'f'))
        {
            ucLow = (uint8_t)(cCharacter - 'a' + 10);
        }
        else if ((cCharacter >= 'A') && (cCharacter <= 'F'))
        {
            ucLow = (uint8_t)(cCharacter - 'A' + 10);
        }
        else
        {
            return false;
        }
        pucDigest[usIndex] = (uint8_t)((ucHigh << 4U) | ucLow);
    }
    return true;
}

static bool prvFtpPathAndNameAreValid(const AppFtpDownloadCommand_t *pxCommand)
{
    uint16_t usIndex;

    if ((pxCommand->usPathLength == 0U) || (pxCommand->acPath[0] != '/') ||
        (pxCommand->acPath[pxCommand->usPathLength - 1U] == '/'))
    {
        return false;
    }

    for (usIndex = 0U; usIndex < pxCommand->usFileNameLength; usIndex++)
    {
        if (pxCommand->acFileName[usIndex] == '/')
        {
            return false;
        }
        if (((usIndex + 1U) < pxCommand->usFileNameLength) &&
            (pxCommand->acFileName[usIndex] == '.') &&
            (pxCommand->acFileName[usIndex + 1U] == '.'))
        {
            return false;
        }
    }
    return true;
}

static bool prvParseFtpDownloadCommand(bool *pxHeaderValid, bool *pxServerAvailable,
                                       char pcAckServer[16])
{
    const uint8_t *pucPayload = s_xInboundMessage.aucPayload;
    uint16_t usPayloadLength = s_xInboundMessage.usPayloadLength;
    const char *pcHeader;
    const char *pcBody;
    const char *pcValue;
    char acMd5Text[(APP_FTP_MD5_DIGEST_SIZE * 2U) + 1U];
    size_t xHeaderLength;
    size_t xBodyLength;
    size_t xValueLength;
    uint64_t ullValue;
    uint16_t usMd5Length;
    uint16_t usServerLength;

    /* 先确认公共header，才能为body参数错误生成有依据的拒绝ACK。 */
    *pxHeaderValid = false;
    *pxServerAvailable = false;
    memset(&s_xFtpCommand, 0, sizeof(s_xFtpCommand));

    if (JSON_Validate((const char *)pucPayload, usPayloadLength) != JSONSuccess)
    {
        return false;
    }
    if ((prvSearchTypedValue(pucPayload, usPayloadLength, s_cHeaderKey,
                             sizeof(s_cHeaderKey) - 1U, JSONObject,
                             &pcHeader, &xHeaderLength) == false) ||
        (prvSearchTypedValue(pucPayload, usPayloadLength, s_cBodyKey,
                             sizeof(s_cBodyKey) - 1U, JSONObject,
                             &pcBody, &xBodyLength) == false) ||
        (prvObjectHasExactKeys((const char *)pucPayload, usPayloadLength, s_xRootKeys,
                               sizeof(s_xRootKeys) / sizeof(s_xRootKeys[0])) == false) ||
        (prvObjectHasExactKeys(pcHeader, xHeaderLength, s_xHeaderKeys,
                               sizeof(s_xHeaderKeys) / sizeof(s_xHeaderKeys[0])) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cMethodQuery,
                               sizeof(s_cMethodQuery) - 1U, s_cExpectedFtpMethod,
                               sizeof(s_cExpectedFtpMethod) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cMessageIdQuery,
                               sizeof(s_cMessageIdQuery) - 1U, s_cExpectedFtpMessageId,
                               sizeof(s_cExpectedFtpMessageId) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cVersionQuery,
                               sizeof(s_cVersionQuery) - 1U, s_cExpectedVersion,
                               sizeof(s_cExpectedVersion) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usPayloadLength, s_cDeviceIdQuery,
                               sizeof(s_cDeviceIdQuery) - 1U, s_cExpectedDeviceId,
                               sizeof(s_cExpectedDeviceId) - 1U) == false) ||
        (prvSearchTypedValue(pucPayload, usPayloadLength, s_cTimestampQuery,
                             sizeof(s_cTimestampQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false))
    {
        return false;
    }
    *pxHeaderValid = true;

    if (prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpServerQuery,
                          sizeof(s_cFtpServerQuery) - 1U, pcAckServer, 15U,
                          &usServerLength) == false)
    {
        return false;
    }
    *pxServerAvailable = true;

    if ((prvObjectHasExactKeys(pcBody, xBodyLength, s_xFtpBodyKeys,
                               sizeof(s_xFtpBodyKeys) / sizeof(s_xFtpBodyKeys[0])) == false) ||
        (strcmp(pcAckServer, s_cExpectedFtpServer) != 0) ||
        (prvParseIpv4(pcAckServer, usServerLength,
                      s_xFtpCommand.aucServerIp) == false) ||
        (prvSearchTypedValue(pucPayload, usPayloadLength, s_cFtpPortQuery,
                             sizeof(s_cFtpPortQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false) || (ullValue != 21U))
    {
        return false;
    }
    s_xFtpCommand.usServerPort = (uint16_t)ullValue;

    if ((prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpUserQuery,
                           sizeof(s_cFtpUserQuery) - 1U, s_xFtpCommand.acUser,
                           APP_FTP_USER_MAX_LENGTH, &s_xFtpCommand.usUserLength) == false) ||
        (prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpPasswordQuery,
                           sizeof(s_cFtpPasswordQuery) - 1U, s_xFtpCommand.acPassword,
                           APP_FTP_PASSWORD_MAX_LENGTH,
                           &s_xFtpCommand.usPasswordLength) == false) ||
        (prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpPathQuery,
                           sizeof(s_cFtpPathQuery) - 1U, s_xFtpCommand.acPath,
                           APP_FTP_PATH_MAX_LENGTH, &s_xFtpCommand.usPathLength) == false) ||
        (prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpFileNameQuery,
                           sizeof(s_cFtpFileNameQuery) - 1U, s_xFtpCommand.acFileName,
                           APP_FTP_FILE_NAME_MAX_LENGTH,
                           &s_xFtpCommand.usFileNameLength) == false) ||
        (prvCopyJsonString(pucPayload, usPayloadLength, s_cFtpMd5Query,
                           sizeof(s_cFtpMd5Query) - 1U, acMd5Text,
                           APP_FTP_MD5_DIGEST_SIZE * 2U, &usMd5Length) == false) ||
        (prvDecodeMd5(acMd5Text, usMd5Length,
                      s_xFtpCommand.aucExpectedFirmwareMd5) == false) ||
        (prvFtpPathAndNameAreValid(&s_xFtpCommand) == false))
    {
        memset(s_xFtpCommand.acPassword, 0, sizeof(s_xFtpCommand.acPassword));
        return false;
    }
    return true;
}

static uint32_t prvTakeNextFtpTransactionId(void)
{
    uint32_t ulTransactionId = s_ulNextFtpTransactionId;

    s_ulNextFtpTransactionId++;
    if (s_ulNextFtpTransactionId == 0U)
    {
        s_ulNextFtpTransactionId = 1U;
    }
    return ulTransactionId;
}

static void prvHandleFtpDownloadCommand(QueueHandle_t xFtpCommandQueue)
{
    char acAckServer[16] = {0};
    bool xHeaderValid;
    bool xServerAvailable;
    bool xCommandValid;
    bool xAccepted = false;

    xCommandValid = prvParseFtpDownloadCommand(&xHeaderValid, &xServerAvailable,
                                                acAckServer);
    if (xCommandValid && s_xHasFtpStatus && s_xFtpStatus.xAcceptingCommand &&
        (s_xPendingFtpAck == false) && (s_xPendingFtpResult == false) &&
        prvUpgradeDownloadAllowed())
    {
        s_xFtpCommand.ulTransactionId = prvTakeNextFtpTransactionId();
        xAccepted = xQueueSend(xFtpCommandQueue, &s_xFtpCommand, 0U) == pdPASS;
    }

    memset(s_xFtpCommand.acPassword, 0, sizeof(s_xFtpCommand.acPassword));
    if (xHeaderValid && xServerAvailable)
    {
        memcpy(s_acPendingFtpAckServer, acAckServer, sizeof(s_acPendingFtpAckServer));
        s_ulPendingFtpAckSession = s_xMqttStatus.ulSession;
        s_xPendingFtpAckResult = xAccepted;
        s_xPendingFtpAck = true;
    }
}

static void prvScheduleLatestFtpResult(void)
{
    uint8_t ucResult;
    uint8_t ucRate;

    if ((s_xMqttStatus.xReady == false) || (s_xHasFtpStatus == false) ||
        (s_xFtpStatus.ulTransactionId == 0U) ||
        (s_xFtpStatus.xState == APP_FTP_STATE_IDLE))
    {
        return;
    }

    if ((s_ulLastFtpResultTransaction != s_xFtpStatus.ulTransactionId) &&
        ((s_xPendingFtpResult == false) ||
         (s_ulPendingFtpResultTransaction != s_xFtpStatus.ulTransactionId)))
    {
        ucResult = 3U;
        ucRate = 0U;
    }
    else if (s_xFtpStatus.xState == APP_FTP_STATE_COMPLETED)
    {
        ucResult = 1U;
        ucRate = 100U;
    }
    else if (s_xFtpStatus.xState == APP_FTP_STATE_FAILED)
    {
        ucResult = 2U;
        ucRate = (uint8_t)((s_xFtpStatus.ucProgressPercent / 5U) * 5U);
    }
    else
    {
        ucResult = 3U;
        ucRate = (uint8_t)((s_xFtpStatus.ucProgressPercent / 5U) * 5U);
        if (ucRate > 95U)
        {
            ucRate = 95U;
        }
    }

    if (s_xPendingFtpResult &&
        (s_ulPendingFtpResultTransaction == s_xFtpStatus.ulTransactionId) &&
        ((s_ucPendingFtpResult == 1U) || (s_ucPendingFtpResult == 2U)) &&
        (ucResult == 3U))
    {
        return;
    }
    if (((s_xPendingFtpResult) &&
         (s_ulPendingFtpResultTransaction == s_xFtpStatus.ulTransactionId) &&
         (s_ucPendingFtpResult == ucResult) && (s_ucPendingFtpRate == ucRate)) ||
        ((s_ulLastFtpResultTransaction == s_xFtpStatus.ulTransactionId) &&
         (s_ucLastFtpResult == ucResult) && (s_ucLastFtpRate == ucRate)))
    {
        return;
    }

    s_ulPendingFtpResultTransaction = s_xFtpStatus.ulTransactionId;
    s_ucPendingFtpResult = ucResult;
    s_ucPendingFtpRate = ucRate;
    s_xPendingFtpResult = true;
}

static void prvReceiveFtpStatus(QueueHandle_t xFtpStatusQueue)
{
    AppFtpStatus_t xNewStatus;

    if (xQueueReceive(xFtpStatusQueue, &xNewStatus, 0U) == pdPASS)
    {
        s_xFtpStatus = xNewStatus;
        s_xHasFtpStatus = true;
        prvScheduleLatestFtpResult();
    }
}

static bool prvBuildFtpAck(void)
{
    int iLength;

    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cFtpAckTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cFtpAckTopic,
           s_xOutboundMessage.usTopicLength);
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cFtpAckJsonFormat,
                       (unsigned long)prvGetUptimeSeconds(), s_acPendingFtpAckServer,
                       s_xPendingFtpAckResult ? "true" : "false");
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    return true;
}

static bool prvBuildFtpResult(void)
{
    int iLength;

    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cFtpResultTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cFtpResultTopic,
           s_xOutboundMessage.usTopicLength);
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cFtpResultJsonFormat,
                       (unsigned long)prvGetUptimeSeconds(),
                       (unsigned int)s_ucPendingFtpResult,
                       (unsigned int)s_ucPendingFtpRate);
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    return true;
}

static void prvServicePendingFtpMessages(QueueHandle_t xOutboundQueue)
{
    if (s_xMqttStatus.xReady == false)
    {
        return;
    }

    if (s_xPendingFtpAck)
    {
        if (s_ulPendingFtpAckSession != s_xMqttStatus.ulSession)
        {
            s_xPendingFtpAck = false;
            return;
        }
        if (prvBuildFtpAck() &&
            (xQueueSend(xOutboundQueue, &s_xOutboundMessage, 0U) == pdPASS))
        {
            s_xPendingFtpAck = false;
            memset(s_acPendingFtpAckServer, 0, sizeof(s_acPendingFtpAckServer));
        }
        return;
    }

    prvScheduleLatestFtpResult();
    if (s_xPendingFtpResult && prvBuildFtpResult() &&
        (xQueueSend(xOutboundQueue, &s_xOutboundMessage, 0U) == pdPASS))
    {
        s_ulLastFtpResultTransaction = s_ulPendingFtpResultTransaction;
        s_ucLastFtpResult = s_ucPendingFtpResult;
        s_ucLastFtpRate = s_ucPendingFtpRate;
        s_xPendingFtpResult = false;
    }
}

static uint32_t prvTakeUpgradeRequestId(void)
{
    uint32_t ulId = s_xUpgrade.ulNextRequestId;

    s_xUpgrade.ulNextRequestId++;
    if (s_xUpgrade.ulNextRequestId == 0U)
    {
        s_xUpgrade.ulNextRequestId = 1U;
    }
    return ulId;
}

static bool prvUpgradeDownloadAllowed(void)
{
    AppStorageStatus_t xStatus;

    if ((s_xUpgrade.xState != APP_UPGRADE_IDLE) ||
        (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS))
    {
        return false;
    }
    return (xStatus.xState == APP_STORAGE_STATE_IDLE) ||
           (xStatus.xState == APP_STORAGE_STATE_FAILED) ||
           (xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE);
}

static bool prvParseUpgradeCommand(bool *pxHeaderValid, uint32_t *pulUpgrade)
{
    const uint8_t *pucPayload = s_xInboundMessage.aucPayload;
    uint16_t usLength = s_xInboundMessage.usPayloadLength;
    const char *pcHeader;
    const char *pcBody;
    const char *pcValue;
    size_t xHeaderLength;
    size_t xBodyLength;
    size_t xValueLength;
    uint64_t ullValue;

    *pxHeaderValid = false;
    if ((JSON_Validate((const char *)pucPayload, usLength) != JSONSuccess) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cHeaderKey,
                             sizeof(s_cHeaderKey) - 1U, JSONObject,
                             &pcHeader, &xHeaderLength) == false) ||
        (prvObjectHasExactKeys(pcHeader, xHeaderLength, s_xHeaderKeys,
                               sizeof(s_xHeaderKeys) / sizeof(s_xHeaderKeys[0])) == false))
    {
        return false;
    }
    if ((prvSearchStringEquals(pucPayload, usLength, s_cMethodQuery,
                               sizeof(s_cMethodQuery) - 1U, s_cUpgradeMethod,
                               sizeof(s_cUpgradeMethod) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cMessageIdQuery,
                               sizeof(s_cMessageIdQuery) - 1U, s_cUpgradeMessageId,
                               sizeof(s_cUpgradeMessageId) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cVersionQuery,
                               sizeof(s_cVersionQuery) - 1U, s_cExpectedVersion,
                               sizeof(s_cExpectedVersion) - 1U) == false) ||
        (prvSearchStringEquals(pucPayload, usLength, s_cDeviceIdQuery,
                               sizeof(s_cDeviceIdQuery) - 1U, s_cExpectedDeviceId,
                               sizeof(s_cExpectedDeviceId) - 1U) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cTimestampQuery,
                             sizeof(s_cTimestampQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false))
    {
        return false;
    }

    *pxHeaderValid = true; /* 可信头但Body错误，仍可发送否定业务ACK。 */
    if ((prvObjectHasExactKeys((const char *)pucPayload, usLength, s_xRootKeys,
                               sizeof(s_xRootKeys) / sizeof(s_xRootKeys[0])) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cBodyKey,
                             sizeof(s_cBodyKey) - 1U, JSONObject,
                             &pcBody, &xBodyLength) == false) ||
        (prvObjectHasExactKeys(pcBody, xBodyLength, s_xUpgradeBodyKeys,
                               sizeof(s_xUpgradeBodyKeys) /
                               sizeof(s_xUpgradeBodyKeys[0])) == false) ||
        (prvSearchTypedValue(pucPayload, usLength, s_cUpgradeQuery,
                             sizeof(s_cUpgradeQuery) - 1U, JSONNumber,
                             &pcValue, &xValueLength) == false) ||
        (prvParseUint64(pcValue, xValueLength, &ullValue) == false) ||
        ((ullValue != 1U) && (ullValue != 2U)))
    {
        return false;
    }
    *pulUpgrade = (uint32_t)ullValue;
    return true;
}

static bool prvQueueUpgradeAck(bool xAccepted, bool xTracked, TickType_t xNowTick)
{
    int iLength;

    if (s_xMqttStatus.xReady == false)
    {
        return false;
    }
    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cUpgradeAckTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cUpgradeAckTopic, sizeof(s_cUpgradeAckTopic));
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cUpgradeAckFormat,
                       (unsigned long)prvGetUptimeSeconds(), xAccepted ? "true" : "false");
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    s_xOutboundMessage.ulRequestId = xTracked ? prvTakeUpgradeRequestId() : 0U;
    if (xQueueSend(s_xBusinessTaskContext.xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return false;
    }
    if (xTracked)
    {
        s_xUpgrade.ulRequestId = s_xOutboundMessage.ulRequestId;
        s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
        s_xUpgrade.xStartTick = xNowTick;
    }
    return true;
}

static bool prvQueueUpgradeResult(uint8_t ucUpgrade, TickType_t xNowTick)
{
    int iLength;

    if (s_xMqttStatus.xReady == false)
    {
        return false;
    }
    memset(&s_xOutboundMessage, 0, sizeof(s_xOutboundMessage));
    s_xOutboundMessage.ulMqttSession = s_xMqttStatus.ulSession;
    s_xOutboundMessage.usTopicLength = (uint16_t)(sizeof(s_cUpgradeResultTopic) - 1U);
    memcpy(s_xOutboundMessage.acTopic, s_cUpgradeResultTopic, sizeof(s_cUpgradeResultTopic));
    iLength = snprintf((char *)s_xOutboundMessage.aucPayload,
                       sizeof(s_xOutboundMessage.aucPayload), s_cUpgradeResultFormat,
                       (unsigned long)prvGetUptimeSeconds(), (unsigned int)ucUpgrade,
                       (unsigned int)((ucUpgrade == 1U) ? 100U : 0U));
    if ((iLength <= 0) || (iLength >= (int)sizeof(s_xOutboundMessage.aucPayload)))
    {
        return false;
    }
    s_xOutboundMessage.usPayloadLength = (uint16_t)iLength;
    s_xOutboundMessage.ulRequestId = prvTakeUpgradeRequestId();
    if (xQueueSend(s_xBusinessTaskContext.xOutboundQueue, &s_xOutboundMessage, 0U) != pdPASS)
    {
        return false;
    }
    s_xUpgrade.ulRequestId = s_xOutboundMessage.ulRequestId;
    s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
    s_xUpgrade.xStartTick = xNowTick;
    return true;
}

static bool prvBeginUpgradeStorage(AppStorageRequestType_t xType, TickType_t xNowTick)
{
    AppStorageStatus_t xStatus;

    if ((xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS) ||
        (xStatus.xMetadataValid == false) ||
        (xStatus.xState == APP_STORAGE_STATE_DEVICE_FAULT))
    {
        return false;
    }
    memset(&s_xUpgradeRequest, 0, sizeof(s_xUpgradeRequest));
    s_xUpgradeRequest.xType = xType;
    s_xUpgradeRequest.xData.xUpgrade.ulRequestId = prvTakeUpgradeRequestId();
    s_xUpgradeRequest.xData.xUpgrade.ulExpectedGeneration = xStatus.ulMetadataGeneration;
    if (xQueueSend(s_xBusinessTaskContext.xStorageRequestQueue,
                   &s_xUpgradeRequest, 0U) != pdPASS)
    {
        return false;
    }
    s_xUpgrade.ulRequestId = s_xUpgradeRequest.xData.xUpgrade.ulRequestId;
    s_xUpgrade.ulMetadataGeneration = xStatus.ulMetadataGeneration;
    s_xUpgrade.ulMqttSession = s_xMqttStatus.ulSession;
    s_xUpgrade.xStartTick = xNowTick;
    s_xUpgrade.xState = (xType == APP_STORAGE_REQUEST_START_UPGRADE) ?
        APP_UPGRADE_START_COMMIT : APP_UPGRADE_CANCEL_COMMIT;
    return true;
}

static void prvHandleUpgradeCommand(void)
{
    AppOutputState_t xApplied;
    AppStorageStatus_t xStatus;
    TickType_t xNowTick = xTaskGetTickCount();
    uint32_t ulUpgrade;
    bool xHeaderValid;
    bool xAlreadyAccepted;

    if (prvParseUpgradeCommand(&xHeaderValid, &ulUpgrade) == false)
    {
        if (xHeaderValid)
        {
            (void)prvQueueUpgradeAck(false, false, xNowTick);
        }
        return;
    }

    if (ulUpgrade == 1U)
    {
        /* 收到取消后绝不沿原开始路径复位；先结束旧异步操作，再提交取消。 */
        switch (s_xUpgrade.xState)
        {
            case APP_UPGRADE_START_COMMIT:
                s_xUpgrade.xState = APP_UPGRADE_START_COMMIT_CANCEL;
                return;
            case APP_UPGRADE_START_ACK:
                s_xUpgrade.xState = APP_UPGRADE_START_ACK_CANCEL;
                return;
            case APP_UPGRADE_PROGRESS:
                s_xUpgrade.xState = APP_UPGRADE_PROGRESS_CANCEL;
                return;
            case APP_UPGRADE_LATE_STORAGE:
                s_xUpgrade.xState = APP_UPGRADE_LATE_STORAGE_CANCEL;
                return;
            case APP_UPGRADE_LATE_PUBLISH:
                s_xUpgrade.xState = APP_UPGRADE_LATE_PUBLISH_CANCEL;
                return;
            case APP_UPGRADE_RESET_READY:
            case APP_UPGRADE_IDLE:
                if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick))
                {
                    return;
                }
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                (void)prvQueueUpgradeAck(false, false, xNowTick);
                return;
            case APP_UPGRADE_START_COMMIT_CANCEL:
            case APP_UPGRADE_START_ACK_CANCEL:
            case APP_UPGRADE_PROGRESS_CANCEL:
            case APP_UPGRADE_CANCEL_COMMIT:
            case APP_UPGRADE_CANCEL_ACK:
            case APP_UPGRADE_LATE_STORAGE_CANCEL:
            case APP_UPGRADE_LATE_PUBLISH_CANCEL:
                return; /* 同一取消仍在处理，不另建请求或提前ACK成功。 */
            default:
                (void)prvQueueUpgradeAck(false, false, xNowTick);
                return;
        }
    }

    if (s_xUpgrade.xState != APP_UPGRADE_IDLE)
    {
        if (s_xUpgrade.xState == APP_UPGRADE_START_COMMIT)
        {
            return; /* 原请求仍在提交，沿原流程等待最终ACK，不能提前成功。 */
        }
        xAlreadyAccepted = ((s_xUpgrade.xState == APP_UPGRADE_START_ACK) ||
                            (s_xUpgrade.xState == APP_UPGRADE_PROGRESS) ||
                            (s_xUpgrade.xState == APP_UPGRADE_RESET_READY)) &&
            (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) == pdPASS) &&
            xStatus.xMetadataValid &&
            (xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
            (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED) &&
            (xStatus.ulMetadataGeneration == s_xUpgrade.ulMetadataGeneration);
        /* 重复开始只回复当前受理事实，不提交第二笔请求或tracked发送。 */
        (void)prvQueueUpgradeAck(xAlreadyAccepted, false, xNowTick);
        return;
    }
    if (s_xPendingFtpAck ||
        s_xPendingFtpResult || (s_xHasFtpStatus == false) ||
        (s_xFtpStatus.xAcceptingCommand == false) ||
        (uxQueueMessagesWaiting(s_xBusinessTaskContext.xFtpCommandQueue) != 0U) ||
        (xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) != pdPASS) ||
        (xStatus.xMetadataValid == false) ||
        !(((xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
           (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE)) ||
          ((xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
           (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED))))
    {
        /* 固定messageId无法区分并行事务：未就绪时不接受另一笔开始。 */
        (void)prvQueueUpgradeAck(false, false, xNowTick);
        return;
    }
    if ((AppOutputControl_SetAll(false, false, &xApplied) == false) ||
        (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_START_UPGRADE, xNowTick) == false))
    {
        (void)prvQueueUpgradeAck(false, false, xNowTick);
    }
}

static void prvServiceUpgrade(TickType_t xNowTick)
{
    AppStorageStatus_t xStatus;
    AppMqttPublishReceipt_t xReceipt;
    AppOutputState_t xApplied;
    AppUpgradeFlowState_t xPrevious = s_xUpgrade.xState;
    bool xHasStatus;
    bool xHasReceipt;
    bool xSuccess;
    uint8_t ucResult;

    xHasStatus = xQueuePeek(s_xBusinessTaskContext.xStorageStatusQueue, &xStatus, 0U) == pdPASS;
    xHasReceipt = xQueueReceive(s_xBusinessTaskContext.xPublishReceiptQueue,
                                &xReceipt, 0U) == pdPASS;
    if (xHasReceipt && ((xReceipt.ulRequestId != s_xUpgrade.ulRequestId) ||
                       (xReceipt.ulMqttSession != s_xUpgrade.ulMqttSession)))
    {
        xHasReceipt = false; /* 迟到或旧会话结果，不能推进当前流程。 */
    }

    switch (xPrevious)
    {
        case APP_UPGRADE_START_COMMIT:
        case APP_UPGRADE_START_COMMIT_CANCEL:
        case APP_UPGRADE_CANCEL_COMMIT:
        case APP_UPGRADE_LATE_STORAGE:
        case APP_UPGRADE_LATE_STORAGE_CANCEL:
            if (xHasStatus &&
                (xStatus.ulCompletedUpgradeRequestId == s_xUpgrade.ulRequestId))
            {
                if (prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_STORAGE_WAIT_MS))
                {
                    /* 完成和超时在同一轮被看到时，仍按超时后收尾处理。 */
                    if (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL)
                    {
                        xPrevious = APP_UPGRADE_LATE_STORAGE_CANCEL;
                    }
                    else if ((xPrevious == APP_UPGRADE_START_COMMIT) ||
                             (xPrevious == APP_UPGRADE_CANCEL_COMMIT))
                    {
                        xPrevious = APP_UPGRADE_LATE_STORAGE;
                    }
                }
                xSuccess = (xStatus.xUpgradeRequestError == APP_STORAGE_ERROR_NONE) &&
                           xStatus.xMetadataValid &&
                           (xStatus.xState != APP_STORAGE_STATE_DEVICE_FAULT);
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                if ((xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ||
                    (xPrevious == APP_UPGRADE_LATE_STORAGE_CANCEL))
                {
                    if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick) ==
                        false)
                    {
                        (void)prvQueueUpgradeAck(false, false, xNowTick);
                    }
                    return;
                }
                if ((xPrevious == APP_UPGRADE_LATE_STORAGE) ||
                    (s_xMqttStatus.xReady == false) ||
                    (s_xMqttStatus.ulSession != s_xUpgrade.ulMqttSession))
                {
                    return; /* 超时/会话已变，只收尾；持久请求不自动撤销。 */
                }
                if (xPrevious == APP_UPGRADE_CANCEL_COMMIT)
                {
                    xSuccess = xSuccess &&
                        (xStatus.xState == APP_STORAGE_STATE_READY_TO_UPGRADE) &&
                        (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_READY_TO_UPGRADE);
                    if (prvQueueUpgradeAck(xSuccess, true, xNowTick))
                    {
                        s_xUpgrade.xState = APP_UPGRADE_CANCEL_ACK;
                    }
                    return;
                }
                xSuccess = xSuccess &&
                    (xStatus.xState == APP_STORAGE_STATE_UPGRADE_LOCKED) &&
                    (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED);
                s_xUpgrade.ulMetadataGeneration = xStatus.ulMetadataGeneration;
                if (prvQueueUpgradeAck(xSuccess, true, xNowTick))
                {
                    s_xUpgrade.xState = xSuccess ? APP_UPGRADE_START_ACK : APP_UPGRADE_NOTICE;
                }
                return;
            }
            if (((xPrevious == APP_UPGRADE_START_COMMIT) ||
                 (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ||
                 (xPrevious == APP_UPGRADE_CANCEL_COMMIT)) &&
                prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_STORAGE_WAIT_MS))
            {
                s_xUpgrade.xState = (xPrevious == APP_UPGRADE_START_COMMIT_CANCEL) ?
                    APP_UPGRADE_LATE_STORAGE_CANCEL : APP_UPGRADE_LATE_STORAGE;
                (void)AppSerial_Write((const uint8_t *)"UPGRADE: storage wait timeout\r\n", 31U);
            }
            return;

        case APP_UPGRADE_START_ACK:
        case APP_UPGRADE_START_ACK_CANCEL:
        case APP_UPGRADE_PROGRESS:
        case APP_UPGRADE_PROGRESS_CANCEL:
        case APP_UPGRADE_CANCEL_ACK:
        case APP_UPGRADE_NOTICE:
        case APP_UPGRADE_LATE_PUBLISH:
        case APP_UPGRADE_LATE_PUBLISH_CANCEL:
            if (xHasReceipt)
            {
                xSuccess = (xReceipt.eResult == APP_MQTT_PUBLISH_RESULT_SENT) &&
                           s_xMqttStatus.xReady &&
                           (s_xMqttStatus.ulSession == s_xUpgrade.ulMqttSession) &&
                           (prvHasExpired(s_xUpgrade.xStartTick, xNowTick,
                                           APP_UPGRADE_PUBLISH_WAIT_MS) == false);
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                if ((xPrevious == APP_UPGRADE_START_ACK_CANCEL) ||
                    (xPrevious == APP_UPGRADE_PROGRESS_CANCEL) ||
                    (xPrevious == APP_UPGRADE_LATE_PUBLISH_CANCEL))
                {
                    if (prvBeginUpgradeStorage(APP_STORAGE_REQUEST_CANCEL_UPGRADE, xNowTick) ==
                        false)
                    {
                        (void)prvQueueUpgradeAck(false, false, xNowTick);
                    }
                    return;
                }
                if (xSuccess && (xPrevious == APP_UPGRADE_START_ACK))
                {
                    if (prvQueueUpgradeResult(3U, xNowTick))
                    {
                        s_xUpgrade.xState = APP_UPGRADE_PROGRESS;
                    }
                }
                else if (xSuccess && (xPrevious == APP_UPGRADE_PROGRESS))
                {
                    /* 下一轮先处理入站命令，再做最后一次放行检查。 */
                    s_xUpgrade.xState = APP_UPGRADE_RESET_READY;
                }
                return;
            }
            if ((xPrevious != APP_UPGRADE_LATE_PUBLISH) &&
                (xPrevious != APP_UPGRADE_LATE_PUBLISH_CANCEL) &&
                prvHasExpired(s_xUpgrade.xStartTick, xNowTick, APP_UPGRADE_PUBLISH_WAIT_MS))
            {
                s_xUpgrade.xState = ((xPrevious == APP_UPGRADE_START_ACK_CANCEL) ||
                                     (xPrevious == APP_UPGRADE_PROGRESS_CANCEL)) ?
                    APP_UPGRADE_LATE_PUBLISH_CANCEL : APP_UPGRADE_LATE_PUBLISH;
                (void)AppSerial_Write((const uint8_t *)"UPGRADE: publish wait timeout\r\n", 31U);
            }
            return;

        case APP_UPGRADE_RESET_READY:
            if ((s_xMqttStatus.xReady == false) ||
                (s_xMqttStatus.ulSession != s_xUpgrade.ulMqttSession) ||
                (xHasStatus == false) || (xStatus.xMetadataValid == false) ||
                (xStatus.xState != APP_STORAGE_STATE_UPGRADE_LOCKED) ||
                (xStatus.xPersistentState != APP_STORAGE_PERSISTENT_STATE_UPGRADE_REQUESTED) ||
                (xStatus.ulMetadataGeneration != s_xUpgrade.ulMetadataGeneration))
            {
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                return;
            }
            if (uxQueueMessagesWaiting(s_xBusinessTaskContext.xInboundQueue) != 0U)
            {
                return; /* 刚入队的取消优先，留下一轮有界处理。 */
            }
            if (AppOutputControl_SetAll(false, false, &xApplied) == false)
            {
                s_xUpgrade.xState = APP_UPGRADE_IDLE;
                return;
            }
            /* 未持有任何W5500/输出Mutex；取消截止于本业务Task放行复位。 */
            NVIC_SystemReset();
            for (;;)
            {
            }

        case APP_UPGRADE_IDLE:
            if ((s_xMqttStatus.xReady == false) || (xHasStatus == false) ||
                (xStatus.xMetadataValid == false) ||
                (s_xUpgrade.ulResultSession == s_xMqttStatus.ulSession) ||
                s_xPendingFtpAck || s_xPendingFtpResult ||
                (uxQueueMessagesWaiting(s_xBusinessTaskContext.xFtpCommandQueue) != 0U))
            {
                return;
            }
            ucResult = 0U;
            if ((xStatus.xState == APP_STORAGE_STATE_IDLE) &&
                (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_CONFIRMED) &&
                (xStatus.ulInstallAttempts > 0U) && (xStatus.ulBootAttempts > 0U))
            {
                ucResult = 1U;
            }
            else if ((xStatus.xState == APP_STORAGE_STATE_FAILED) &&
                     (xStatus.xPersistentState == APP_STORAGE_PERSISTENT_STATE_FAILED) &&
                     (xStatus.ulPersistentError == 0x1001UL) &&
                     (xStatus.ulInstallAttempts == 0U))
            {
                /* Boot候选拒绝且旧应用可运行；不是FTP下载失败。 */
                ucResult = 2U;
            }
            if (ucResult != 0U)
            {
                /* 每会话只尝试一次，断线重连才再次补报；不持久保存发送标志。 */
                s_xUpgrade.ulResultSession = s_xMqttStatus.ulSession;
                if (prvQueueUpgradeResult(ucResult, xNowTick))
                {
                    s_xUpgrade.xState = APP_UPGRADE_NOTICE;
                }
            }
            return;

        default:
            configASSERT(false);
            return;
    }
}
