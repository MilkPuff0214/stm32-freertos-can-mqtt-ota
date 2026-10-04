#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BSP_CAN_MAX_DATA_LENGTH    8U

typedef struct
{
    uint32_t ulExtendedId;
    uint8_t ucDlc;
    uint8_t ucData[BSP_CAN_MAX_DATA_LENGTH];
} BspCanTxFrame_t;

typedef struct
{
    uint32_t ulExtendedId;
    bool xIsExtended;
    bool xIsRemote;
    uint8_t ucDlc;
    uint8_t ucData[BSP_CAN_MAX_DATA_LENGTH];
} BspCanRxFrame_t;


typedef enum
{
    BSP_CAN_TX_STATUS_PENDING = 0,
    BSP_CAN_TX_STATUS_SUCCESS,
    BSP_CAN_TX_STATUS_FAILED,
    BSP_CAN_TX_STATUS_INVALID
} BspCanTxStatus_t;

typedef enum
{
    BSP_CAN_LAST_ERROR_NONE = 0x00U,
    BSP_CAN_LAST_ERROR_STUFF = 0x10U,
    BSP_CAN_LAST_ERROR_FORM = 0x20U,
    BSP_CAN_LAST_ERROR_ACK = 0x30U,
    BSP_CAN_LAST_ERROR_BIT_RECESSIVE = 0x40U,
    BSP_CAN_LAST_ERROR_BIT_DOMINANT = 0x50U,
    BSP_CAN_LAST_ERROR_CRC = 0x60U,
    BSP_CAN_LAST_ERROR_SOFTWARE = 0x70U
} BspCanLastError_t;

typedef struct
{
    BspCanLastError_t xLastError;
    uint8_t ucTransmitErrorCounter;
    uint8_t ucReceiveErrorCounter;
    bool xErrorWarning;
    bool xErrorPassive;
    bool xBusOff;
} BspCanErrorSnapshot_t;

/**
 * @brief 读取CAN1当前错误状态的完整快照。
 *
 * 本接口只读取硬件状态，不清除错误码、不执行恢复，
 * 也不包含应用层错误处理策略。
 *
 * @param pxSnapshot 用于接收错误状态快照。
 * @return true表示读取成功，false表示参数无效。
 */
bool BspCan_GetErrorSnapshot(
    BspCanErrorSnapshot_t *pxSnapshot);
    
/**
 * @brief 初始化CAN1引脚、重映射、控制器和500 kbit/s位时序。
 * @return true表示bxCAN成功进入正常模式，false表示初始化握手超时。
 */
bool BspCan_Init(void);

/**
 * @brief 配置一个只接收指定29位扩展数据帧ID的32位掩码过滤器。
 *
 * 该接口只负责bxCAN过滤器编码，不包含具体业务协议判断。
 *
 * @param ucFilterNumber 过滤器编号，STM32F103范围为0～13。
 * @param ulExtendedId 29位扩展帧ID。
 * @return 参数合法时返回true，否则返回false。
 */
bool BspCan_ConfigureExactExtendedDataFilter(
    uint8_t ucFilterNumber,
    uint32_t ulExtendedId);

/**
 * @brief 尝试把扩展数据帧提交给一个空闲发送邮箱。
 *
 * 返回true只表示报文已经交给bxCAN，并不表示发送成功或收到总线ACK。
 * 本项目当前规定只有CAN Task调用发送接口，因此BSP内部不使用Mutex。
 *
 * @param pxFrame 待发送帧。
 * @param pucMailbox 返回实际占用的邮箱编号0～2。
 * @return true表示成功提交；false表示参数非法或三个邮箱都忙。
 */
bool BspCan_TryTransmit(
    const BspCanTxFrame_t *pxFrame,
    uint8_t *pucMailbox);

/**
 * @brief 查询指定发送邮箱的当前状态。
 */
BspCanTxStatus_t BspCan_GetTransmitStatus(uint8_t ucMailbox);

/**
 * @brief 中止指定邮箱尚未完成的发送请求。
 *
 * 主要用于任务级有限超时，避免未完成报文永久占用邮箱。
 */
bool BspCan_CancelTransmit(uint8_t ucMailbox);

/**
 * @brief 开启CAN1 FIFO0消息挂起和溢出中断。
 *
 * RX0 ISR将调用FreeRTOS FromISR API，因此中断逻辑优先级配置为6。
 */
void BspCan_EnableRxFifo0Interrupt(void);

/**
 * @brief 从CAN1 FIFO0读取并释放一帧。
 *
 * @return true表示成功取出一帧，false表示参数非法或FIFO为空。
 */
bool BspCan_TryReceiveFifo0(BspCanRxFrame_t *pxFrame);

/**
 * @brief 检查并清除FIFO0溢出状态。
 *
 * @return true表示发生过FIFO0溢出。
 */
bool BspCan_TakeRxFifo0Overrun(void);

#endif /* BSP_CAN_H */