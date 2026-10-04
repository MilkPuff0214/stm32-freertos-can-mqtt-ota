#include "app_startup.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_adc_task.h"
#include "app_can_task.h"
#include "app_key_event.h"
#include "app_key_task.h"
#include "app_led_task.h"
#include "app_serial_task.h"
#include "app_network_task.h"
#include "rtos_w5500_access.h"
#include "app_mqtt_task.h"
#include "app_network_status.h"
#include "app_mqtt_message.h"
#include "app_mqtt_business_task.h"
#include "app_output_control.h"
#include "app_storage_task.h"
#include "app_ftp_task.h"

/*
 * Queue最多缓存4个尚未处理的按键事件。
 * 容量单位是AppKeyEvent_t元素数量，不是字节数。
 */
#define APP_KEY_EVENT_QUEUE_LENGTH    4U

/*
 * Key Task和LED Task共享该Queue。
 * Startup拥有Queue Handle，两个Task分别作为生产者和消费者使用它。
 */
static QueueHandle_t s_xKeyEventQueue = NULL;
/* AppStartup拥有；Network、MQTT和FTP Task后续只保存该对象指针。 */
static RtosW5500Access_t s_xW5500Access = {0};
/*
 * Startup拥有；只供Network生产、MQTT消费。
 */
static QueueHandle_t s_xMqttNetworkStatusQueue = NULL;

/* Startup拥有；MQTT回调生产，下一增量的业务协议Task消费。 */
static QueueHandle_t s_xMqttInboundQueue = NULL;

/* Startup拥有；业务Task生产，MQTT Task消费。 */
static QueueHandle_t s_xMqttOutboundQueue = NULL;

/* Startup拥有；MQTT Task覆盖发布，业务Task消费最新状态。 */
static QueueHandle_t s_xMqttBusinessStatusQueue = NULL;

/* Startup拥有；MQTT Task生产，业务Task独占消费。 */
static QueueHandle_t s_xMqttPublishReceiptQueue = NULL;

static QueueHandle_t s_xStorageRequestQueue = NULL;

/* Storage唯一覆盖写入；FTP和业务Task均只Peek最新快照。 */
static QueueHandle_t s_xStorageStatusQueue = NULL;

static QueueHandle_t s_xFtpNetworkStatusQueue = NULL;

/* MQTT业务Task生产下载命令，FTP Task独占消费。 */
static QueueHandle_t s_xFtpCommandQueue = NULL;

/* FTP Task覆盖发布最新状态，MQTT业务Task独占消费。 */
static QueueHandle_t s_xFtpStatusQueue = NULL;

AppStartupStatus_t AppStartup_Create(void)
{
    BaseType_t xCreateResult;

    if ((s_xKeyEventQueue != NULL) ||
        (s_xMqttNetworkStatusQueue != NULL) ||
        (s_xMqttInboundQueue != NULL) ||
        (s_xMqttOutboundQueue != NULL) ||
        (s_xMqttBusinessStatusQueue != NULL) ||
        (s_xMqttPublishReceiptQueue != NULL) ||
        (s_xStorageRequestQueue != NULL) ||
        (s_xStorageStatusQueue != NULL) ||
        (s_xFtpNetworkStatusQueue != NULL) ||
        (s_xFtpCommandQueue != NULL) ||
        (s_xFtpStatusQueue != NULL))
    {
        return APP_STARTUP_STATUS_ALREADY_CREATED;
    }

    if (RtosW5500Access_Init(&s_xW5500Access) == false)
    {
        return APP_STARTUP_STATUS_W5500_ACCESS_CREATE_FAILED;
    }

    s_xMqttNetworkStatusQueue = xQueueCreate(APP_NETWORK_STATUS_QUEUE_LENGTH,
                                             sizeof(AppNetworkStatus_t));

    if (s_xMqttNetworkStatusQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_NETWORK_STATUS_QUEUE_CREATE_FAILED;
    }

    s_xMqttInboundQueue = xQueueCreate(APP_MQTT_INBOUND_QUEUE_LENGTH,
                                       sizeof(AppMqttInboundMessage_t));
    if (s_xMqttInboundQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_INBOUND_QUEUE_CREATE_FAILED;
    }

    s_xMqttOutboundQueue = xQueueCreate(APP_MQTT_OUTBOUND_QUEUE_LENGTH,
                                        sizeof(AppMqttOutboundMessage_t));
    if (s_xMqttOutboundQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_OUTBOUND_QUEUE_CREATE_FAILED;
    }

    s_xMqttBusinessStatusQueue = xQueueCreate(APP_MQTT_BUSINESS_STATUS_QUEUE_LENGTH,
                                              sizeof(AppMqttBusinessStatus_t));
    if (s_xMqttBusinessStatusQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_BUSINESS_STATUS_QUEUE_CREATE_FAILED;
    }

    s_xMqttPublishReceiptQueue = xQueueCreate(APP_MQTT_PUBLISH_RECEIPT_QUEUE_LENGTH,
                                              sizeof(AppMqttPublishReceipt_t));
    if (s_xMqttPublishReceiptQueue == NULL)
    {
        return APP_STARTUP_STATUS_MQTT_PUBLISH_RECEIPT_QUEUE_CREATE_FAILED;
    }

    s_xKeyEventQueue = xQueueCreate(APP_KEY_EVENT_QUEUE_LENGTH,
                                    sizeof(AppKeyEvent_t));

    if (s_xKeyEventQueue == NULL)
    {
        return APP_STARTUP_STATUS_KEY_EVENT_QUEUE_CREATE_FAILED;
    }

    s_xStorageRequestQueue = xQueueCreate(APP_STORAGE_REQUEST_QUEUE_LENGTH,
                                          sizeof(AppStorageRequest_t));
    if (s_xStorageRequestQueue == NULL)
    {
        return APP_STARTUP_STATUS_STORAGE_REQUEST_QUEUE_CREATE_FAILED;
    }

    s_xStorageStatusQueue = xQueueCreate(APP_STORAGE_STATUS_QUEUE_LENGTH,
                                        sizeof(AppStorageStatus_t));
    if (s_xStorageStatusQueue == NULL)
    {
        return APP_STARTUP_STATUS_STORAGE_STATUS_QUEUE_CREATE_FAILED;
    }

    s_xFtpNetworkStatusQueue = xQueueCreate(APP_NETWORK_STATUS_QUEUE_LENGTH,
                                            sizeof(AppNetworkStatus_t));
    if (s_xFtpNetworkStatusQueue == NULL)
    {
        return APP_STARTUP_STATUS_FTP_NETWORK_STATUS_QUEUE_CREATE_FAILED;
    }

    s_xFtpCommandQueue = xQueueCreate(APP_FTP_COMMAND_QUEUE_LENGTH,
                                     sizeof(AppFtpDownloadCommand_t));
    if (s_xFtpCommandQueue == NULL)
    {
        return APP_STARTUP_STATUS_FTP_COMMAND_QUEUE_CREATE_FAILED;
    }

    s_xFtpStatusQueue = xQueueCreate(APP_FTP_STATUS_QUEUE_LENGTH,
                                    sizeof(AppFtpStatus_t));
    if (s_xFtpStatusQueue == NULL)
    {
        return APP_STARTUP_STATUS_FTP_STATUS_QUEUE_CREATE_FAILED;
    }

    if (AppStorageTask_Create(s_xStorageRequestQueue, s_xStorageStatusQueue) == false)
    {
        return APP_STARTUP_STATUS_STORAGE_TASK_CREATE_FAILED;
    }
    xCreateResult = xTaskCreate(
        vLedTask,
        "LED",
        configMINIMAL_STACK_SIZE,
        s_xKeyEventQueue,
        tskIDLE_PRIORITY + 1U,
        NULL);

    if (xCreateResult != pdPASS)
    {
        return APP_STARTUP_STATUS_LED_TASK_CREATE_FAILED;
    }

    xCreateResult = xTaskCreate(
        vKeyTask,
        "KEY",
        configMINIMAL_STACK_SIZE,
        s_xKeyEventQueue,
        tskIDLE_PRIORITY + 1U,
        NULL);

    if (xCreateResult != pdPASS)
    {
        return APP_STARTUP_STATUS_KEY_TASK_CREATE_FAILED;
    }

    /*
     * RX Task生成的Console响应依赖Serial TX Queue，
     * 因此必须先创建TX模块，再创建RX Task。
     */
    if (AppSerialTxTask_Create() == false)
    {
        return APP_STARTUP_STATUS_SERIAL_TX_TASK_CREATE_FAILED;
    }

    if (AppSerialRxTask_Create() == false)
    {
        return APP_STARTUP_STATUS_SERIAL_RX_TASK_CREATE_FAILED;
    }

    /*
     * ADC模块必须先创建最新样本Queue，
     * CAN Task随后才能读取该Queue中的样本。
     */
    if (AppAdcTask_Create() == false)
    {
        return APP_STARTUP_STATUS_ADC_TASK_CREATE_FAILED;
    }

    if (AppOutputControl_Init() == false)
    {
        return APP_STARTUP_STATUS_OUTPUT_CONTROL_INIT_FAILED;
    }

    if (AppCanTask_Create() == false)
    {
        return APP_STARTUP_STATUS_CAN_TASK_CREATE_FAILED;
    }

    if (AppNetworkTask_Create(
        &s_xW5500Access,
        s_xMqttNetworkStatusQueue,
        s_xFtpNetworkStatusQueue) == false)
    {
        return APP_STARTUP_STATUS_NETWORK_TASK_CREATE_FAILED;
    }

    if (AppMqttTask_Create(&s_xW5500Access, s_xMqttNetworkStatusQueue,
                           s_xMqttInboundQueue, s_xMqttOutboundQueue,
                           s_xMqttBusinessStatusQueue, s_xMqttPublishReceiptQueue) == false)
    {
        return APP_STARTUP_STATUS_MQTT_TASK_CREATE_FAILED;
    }

    if (AppFtpTask_Create(&s_xW5500Access, s_xFtpNetworkStatusQueue,
                          s_xFtpCommandQueue, s_xFtpStatusQueue,
                          s_xStorageRequestQueue, s_xStorageStatusQueue) == false)
    {
        return APP_STARTUP_STATUS_FTP_TASK_CREATE_FAILED;
    }

    if (AppMqttBusinessTask_Create(s_xMqttInboundQueue, s_xMqttOutboundQueue,
                                   s_xMqttBusinessStatusQueue, s_xFtpCommandQueue,
                                   s_xFtpStatusQueue, s_xStorageRequestQueue,
                                   s_xStorageStatusQueue, s_xMqttPublishReceiptQueue) == false)
    {
        return APP_STARTUP_STATUS_MQTT_BUSINESS_TASK_CREATE_FAILED;
    }
    return APP_STARTUP_STATUS_OK;
}
