#ifndef APP_NETWORK_TASK_H
#define APP_NETWORK_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "rtos_w5500_access.h"
#include "FreeRTOS.h"
#include "queue.h"

typedef enum
{
    APP_NETWORK_W5500_STATE_NOT_STARTED = 0,
    APP_NETWORK_W5500_STATE_INITIALIZING,   // 表示 W5500 正在初始化
    APP_NETWORK_W5500_STATE_VERIFYING,      // 表示正在验证 W5500 芯片版本
    APP_NETWORK_W5500_STATE_READY,
    APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT,
    APP_NETWORK_W5500_STATE_SPI_TRANSFER_FAILED,  //SPI 本身通信失败
    APP_NETWORK_W5500_STATE_VERSION_MISMATCH,    //表示 SPI 通信完成了，但是读出来的芯片版本值不对
    APP_NETWORK_W5500_STATE_CONFIGURING_NETWORK,        //表示正在配置网络
    APP_NETWORK_W5500_STATE_NETWORK_CONFIG_MISMATCH    //表示网络配置不匹配
} AppNetworkW5500State_t;

typedef enum
{
    APP_NETWORK_PHY_STATE_UNKNOWN = 0,  // PHY 状态未知
    APP_NETWORK_PHY_STATE_DOWN,           // PHY 成功读到物理链路断开，常见原因是没插网线
    APP_NETWORK_PHY_STATE_UP,             // PHY W5500 与网线另一端建立了物理链路
    APP_NETWORK_PHY_STATE_READ_FAILED     // PHY 状态读取失败
} AppNetworkPhyState_t;     

bool AppNetworkTask_Create(RtosW5500Access_t *pxW5500Access,
                           QueueHandle_t xMqttNetworkStatusQueue,
                           QueueHandle_t xFtpNetworkStatusQueue);

AppNetworkW5500State_t AppNetwork_GetW5500State(void);
uint8_t AppNetwork_GetW5500Version(void);

#endif /* APP_NETWORK_TASK_H */