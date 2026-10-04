#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdbool.h>
#include <stdint.h>
/*
 * 初始化板载CH340G连接的USART1。
 * 当前配置为115200、8位数据、无校验、1位停止位。
 */
void BspUsart1_Init(void);

/*
 * 启动一次USART1 TX DMA传输。
 *
 * pData指向的内存在DMA传输结束前必须保持有效且不能被改写。
 * length的单位是字节，最大值由DMA的16位传输计数器限制。
 *
 * 返回true表示DMA已经启动；
 * 返回false表示参数无效，或者上一次传输尚未完成。
 */
bool BspUsart1_TxDmaStart(const uint8_t *pData, uint16_t length);

/*
 * 处理USART1 TX DMA完成中断。
 *
 * 本函数只检查并清除BSP硬件状态，不调用FreeRTOS API。
 * 返回true表示本次中断确实由USART1 TX DMA传输完成产生。
 */
bool BspUsart1_TxDmaHandleInterrupt(void);


/*
 * USART1 RX循环DMA缓冲区容量，单位为字节。
 * 应用层需要这个值来处理软件读取位置回绕。
 */
#define BSP_USART1_RX_DMA_BUFFER_SIZE 256U

/*
 * 启动USART1 RX循环DMA和IDLE中断。
 *
 * 本函数由Serial RX Task在调度器启动后调用，
 * 避免FreeRTOS调度器启动前产生调用FromISR API的中断。
 */
void BspUsart1_RxDmaStart(void);

/*
 * 获取RX DMA缓冲区的只读地址。
 * 缓冲区所有权仍属于USART BSP，应用层只能读取。
 */
const uint8_t *BspUsart1_RxDmaGetBuffer(void);

/*
 * 获取DMA下一次写入的位置，范围为0～255。
 */
uint16_t BspUsart1_RxDmaGetWritePosition(void);

/*
 * 检查并清除USART1 IDLE中断状态。
 * 返回true表示本次确实发生了IDLE事件。
 */
bool BspUsart1_RxIdleHandleInterrupt(void);

#endif /* BSP_USART_H */