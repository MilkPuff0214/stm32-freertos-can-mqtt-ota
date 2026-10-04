#ifndef APP_MQTT_MESSAGE_H
#define APP_MQTT_MESSAGE_H

#include <stdbool.h>
#include <stdint.h>

#define APP_MQTT_INBOUND_QUEUE_LENGTH             4U
#define APP_MQTT_INBOUND_PAYLOAD_MAX_LENGTH       768U

#define APP_MQTT_OUTBOUND_QUEUE_LENGTH            4U
#define APP_MQTT_OUTBOUND_TOPIC_MAX_LENGTH        64U
#define APP_MQTT_OUTBOUND_PAYLOAD_MAX_LENGTH      512U

#define APP_MQTT_BUSINESS_STATUS_QUEUE_LENGTH     1U

/* 仅一笔本机需回执请求在途；普通出站消息不写此Queue。 */
#define APP_MQTT_PUBLISH_RECEIPT_QUEUE_LENGTH      1U

typedef enum
{
    APP_MQTT_INBOUND_METHOD_CONTROL = 0,
    APP_MQTT_INBOUND_METHOD_FILE_DOWNLOAD_COMMAND,
    APP_MQTT_INBOUND_METHOD_FILE_UPGRADE_COMMAND
} AppMqttInboundMethod_t;

typedef struct
{
    AppMqttInboundMethod_t eMethod;
    uint32_t ulMqttSession;
    uint16_t usPayloadLength;
    uint8_t aucPayload[APP_MQTT_INBOUND_PAYLOAD_MAX_LENGTH];
} AppMqttInboundMessage_t;

/* MQTT Task发布的最新业务可用状态，不暴露MQTTContext_t。 */
typedef struct
{
    uint32_t ulSession; //本机成功建立MQTT会话的递增编号
    bool xReady;
} AppMqttBusinessStatus_t;

/* Queue保存完整副本，禁止保存指向业务Task临时缓冲区的指针。 */
typedef struct
{
    uint32_t ulMqttSession;
    uint16_t usTopicLength;
    uint16_t usPayloadLength;
    char acTopic[APP_MQTT_OUTBOUND_TOPIC_MAX_LENGTH];
    uint8_t aucPayload[APP_MQTT_OUTBOUND_PAYLOAD_MAX_LENGTH];
    /* 本机关联号；0沿用普通消息，不产生发送完成回执。 */
    uint32_t ulRequestId;
} AppMqttOutboundMessage_t;

typedef enum
{
    APP_MQTT_PUBLISH_RESULT_SENT = 0,
    APP_MQTT_PUBLISH_RESULT_FAILED
} AppMqttPublishResult_t;

/* MQTT Task生产，业务Task消费；不携带协议上下文或临时缓冲区指针。 */
typedef struct
{
    uint32_t ulRequestId;
    uint32_t ulMqttSession;
    AppMqttPublishResult_t eResult;
} AppMqttPublishReceipt_t;

#endif /* APP_MQTT_MESSAGE_H */
