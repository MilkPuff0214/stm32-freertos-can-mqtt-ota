#ifndef APP_MQTT_TASK_H
#define APP_MQTT_TASK_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "rtos_w5500_access.h"

/*
 * 访问对象和Queue由AppStartup拥有，
 * 生命周期覆盖MQTT Task整个运行期。
 */
bool AppMqttTask_Create(RtosW5500Access_t *pxW5500Access,
                        QueueHandle_t xNetworkStatusQueue,
                        QueueHandle_t xInboundQueue,
                        QueueHandle_t xOutboundQueue,
                        QueueHandle_t xBusinessStatusQueue,
                        QueueHandle_t xPublishReceiptQueue);

#endif /* APP_MQTT_TASK_H */
