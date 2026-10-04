#ifndef APP_MQTT_BUSINESS_TASK_H
#define APP_MQTT_BUSINESS_TASK_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

/* Queue均由AppStartup创建并拥有，生命周期覆盖本Task。 */
bool AppMqttBusinessTask_Create(QueueHandle_t xInboundQueue,
                                QueueHandle_t xOutboundQueue,
                                QueueHandle_t xBusinessStatusQueue,
                                QueueHandle_t xFtpCommandQueue,
                                QueueHandle_t xFtpStatusQueue,
                                QueueHandle_t xStorageRequestQueue,
                                QueueHandle_t xStorageStatusQueue,
                                QueueHandle_t xPublishReceiptQueue);

#endif /* APP_MQTT_BUSINESS_TASK_H */
