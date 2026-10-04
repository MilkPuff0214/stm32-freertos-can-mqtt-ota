#ifndef CORE_MQTT_CONFIG_H
#define CORE_MQTT_CONFIG_H

/*
 * coreMQTT在收到部分报文后允许连续轮询Transport。
 * 10 ms是单次“无新字节”轮询上限，不是MQTT连接超时。
 */
#define MQTT_RECV_POLLING_TIMEOUT_MS         (10U)

/*
 * coreMQTT公开发送API的同步等待上限。
 * W5500/TCP层仍保留自己的硬件发送超时；任一层失败都应使MQTT会话失效。
 */
#define MQTT_SEND_TIMEOUT_MS                 (1000U)

/* PINGREQ发出后等待PINGRESP的上限；Keep Alive周期在连接步骤单独冻结。 */
#define MQTT_PINGRESP_TIMEOUT_MS             (5000U)

/*
 * 这两个值限制长时间没有收发活动时的内部保活检查。
 * 它们不是10秒业务heartbeat，也不参与双方180秒业务离线判定。
 */
#define PACKET_TX_TIMEOUT_MS                 (30000U)
#define PACKET_RX_TIMEOUT_MS                 (30000U)

/* P2一次只需要少量订阅向量，保留官方默认容量，不做无测量依据的压缩。 */
#define MQTT_SUB_UNSUB_MAX_VECTORS           (4U)

/*
 * 连接步骤会给MQTT_Connect()传入非零、可测量的CONNACK超时，
 * 因此本宏不参与正常连接等待；仍显式保留官方默认值作为防误用边界。
 */
#define MQTT_MAX_CONNACK_RECEIVE_RETRY_COUNT (5U)

#endif /* CORE_MQTT_CONFIG_H */