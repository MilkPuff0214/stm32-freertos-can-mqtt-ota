#ifndef APP_MQTT_CLIENT_H
#define APP_MQTT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "app_mqtt_message.h"
#include "app_mqtt_transport_w5500.h"
#include "app_tcp_client.h"
#include "core_mqtt.h"

#define APP_MQTT_CLIENT_NETWORK_BUFFER_SIZE    1024U

typedef enum
{
    APP_MQTT_CLIENT_STATE_WAIT_TCP = 0,  //TCP还没有可用，MQTT等待
    APP_MQTT_CLIENT_STATE_CONNECTING,  //正在执行MQTT连接流程
    APP_MQTT_CLIENT_STATE_CONNECTED,  //Broker已经接受MQTT连接，但还没有完成订阅
    APP_MQTT_CLIENT_STATE_SUBSCRIBING,  //已发送SUBSCRIBE，正在等待SUBACK
    APP_MQTT_CLIENT_STATE_READY  //MQTT连接和订阅都成功，可以正常收发业务消息
} AppMqttClientState_t;

typedef struct
{
    const char *pcClientIdentifier;
    uint16_t usClientIdentifierLength;
    const char *pcUserName;
    uint16_t usUserNameLength;
    const char *pcPassword;
    uint16_t usPasswordLength;
    uint16_t usKeepAliveSeconds;
    uint32_t ulConnackTimeoutMs;
    uint32_t ulSubackTimeoutMs;
    uint32_t ulStableSessionMs;
    
} AppMqttClientConfig_t;

typedef struct
{
    AppMqttClientConfig_t xConfig;
    NetworkContext_t xNetworkContext;
    MQTTContext_t xMqttContext;
    QueueHandle_t xInboundQueue;
    AppMqttInboundMessage_t xInboundStagingMessage;
    uint8_t aucNetworkBuffer[APP_MQTT_CLIENT_NETWORK_BUFFER_SIZE];
    TickType_t xSubscribeStartTick;
    TickType_t xReadyTick;
    uint32_t ulObservedTcpSession;
    uint32_t ulSuccessfulSessionCount;
    uint32_t ulInboundQueuedCount;  //入站消息队列中的消息数量
    uint32_t ulInboundDroppedCount;  //入站消息队列中被丢弃的消息数量
    uint16_t usPendingSubscribePacketId;
    AppMqttClientState_t xState;
    MQTTStatus_t xLastStatus;
    MQTTStatus_t xCallbackStatus;   //MQTT回调延迟上报的处理结果
    bool xInitialized;
    bool xTcpSessionMarkedStable;
} AppMqttClient_t;

bool AppMqttClient_Init(AppMqttClient_t *pxClient,
                        const AppMqttClientConfig_t *pxConfig,
                        AppTcpClient_t *pxTcpClient,
                        QueueHandle_t xInboundQueue);

void AppMqttClient_Service(AppMqttClient_t *pxClient, TickType_t xNowTick);

/* 只能由拥有pxClient及其MQTTContext_t的MQTT Task调用。 */
MQTTStatus_t AppMqttClient_Publish(AppMqttClient_t *pxClient, const char *pcTopic,
                                   uint16_t usTopicLength, const uint8_t *pucPayload,
                                   uint16_t usPayloadLength);

AppMqttClientState_t AppMqttClient_GetState(const AppMqttClient_t *pxClient);
MQTTStatus_t AppMqttClient_GetLastStatus(const AppMqttClient_t *pxClient);

uint32_t AppMqttClient_GetSuccessfulSessionCount(const AppMqttClient_t *pxClient);
uint32_t AppMqttClient_GetInboundQueuedCount(const AppMqttClient_t *pxClient);
uint32_t AppMqttClient_GetInboundDroppedCount(const AppMqttClient_t *pxClient);

#endif /* APP_MQTT_CLIENT_H */
