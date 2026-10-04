#ifndef APP_SERIAL_TASK_H
#define APP_SERIAL_TASK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 单条串口消息的最大有效长度，单位为字节。
 * 固定上限可以保证Queue和RAM占用是可计算的。
 */
#define APP_SERIAL_TX_MESSAGE_MAX_LENGTH 512U
/*
 * 创建Serial TX Task。
 *
 * 返回true表示任务创建成功；
 * 返回false通常表示FreeRTOS Heap不足。
 */
bool AppSerialTxTask_Create(void);

/*
 * 非阻塞提交一条待发送消息。
 *
 * 函数会把数据复制进模块内部Queue，因此返回后调用者可以
 * 立即修改或释放自己的原始缓冲区。
 *
 * 本接口只能在Task上下文或调度器启动前调用，不能在ISR调用。
 * 返回false表示参数无效、模块尚未创建或Queue已满。
 */
bool AppSerial_Write(const uint8_t *pData, uint16_t length);

/*
 * 创建Serial RX Task。
 *
 * 必须先成功调用AppSerialTxTask_Create()，
 * 保证Console响应提交时TX Queue已经存在。
 */
bool AppSerialRxTask_Create(void);

#endif /* APP_SERIAL_TASK_H */