#ifndef APP_NETWORK_STATUS_H
#define APP_NETWORK_STATUS_H

#include <stdbool.h>

/*
 * 每个协议Task必须拥有自己的长度1状态Queue。
 * 同一个Queue不能同时被MQTT和FTP两个消费者读取。
 */
#define APP_NETWORK_STATUS_QUEUE_LENGTH    1U

typedef struct
{
    /* 启动阶段公共寄存器和网络配置均验证成功后才为true。 */
    bool xChipReady;

    /* 最近一次可信PHY结果为LINK UP时才为true。 */
    bool xPhyLinkUp;
} AppNetworkStatus_t;

#endif /* APP_NETWORK_STATUS_H */
