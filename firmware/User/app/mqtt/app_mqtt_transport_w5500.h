#ifndef APP_MQTT_TRANSPORT_W5500_H
#define APP_MQTT_TRANSPORT_W5500_H

#include <stdbool.h>

#include "app_tcp_client.h"
#include "transport_interface.h"

/*
 * coreMQTT把NetworkContext_t声明为不完整类型，由平台适配层补全。
 * 这里只保存非拥有型TCP Client指针；对象生命周期由MQTT Task管理。
 */
struct NetworkContext
{
    AppTcpClient_t *pxTcpClient;
};

/*
 * 使用NetworkContext中已经绑定的TCP Client填充Transport回调。
 * 本函数不打开Socket、不建立TCP连接，也不拥有TCP Client。
 */
bool AppMqttTransportW5500_Init(NetworkContext_t *pxNetworkContext,
                                TransportInterface_t *pxTransportInterface);

#endif /* APP_MQTT_TRANSPORT_W5500_H */
