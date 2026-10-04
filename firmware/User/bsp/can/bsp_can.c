#include "bsp_can.h"

#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_CAN_PRESCALER                 4U
#define BSP_CAN_FILTER_NUMBER_MAX         13U
#define BSP_CAN_TX_MAILBOX_MAX            2U
#define BSP_CAN_EXTENDED_ID_MAX           0x1FFFFFFFUL


#define BSP_CAN_RX_INTERRUPT_PRIORITY    6U

/*
 * bxCAN 32位过滤器的扩展帧布局：
 *
 * bit31～3：29位扩展ID
 * bit2：IDE，1表示扩展帧
 * bit1：RTR，0表示数据帧
 * bit0：保留位
 */
#define BSP_CAN_FILTER_IDE_BIT             (1UL << 2U)
#define BSP_CAN_FILTER_RTR_BIT             (1UL << 1U)
#define BSP_CAN_FILTER_EXTENDED_ID_MASK    \
    (BSP_CAN_EXTENDED_ID_MAX << 3U)

static void prvCanGpioInit(void)
{
    GPIO_InitTypeDef xGpioInit;

    /*
     * AFIO负责CAN1重映射，GPIOB承载PB8/PB9。
     * GPIO_Remap1_CAN1把CAN1从默认PA11/PA12映射到PB8/PB9。
     */
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB,
        ENABLE);

    GPIO_PinRemapConfig(GPIO_Remap1_CAN1, ENABLE);  // 将CAN1从默认PA11/PA12映射到PB8/PB9

    GPIO_StructInit(&xGpioInit);

    /* PB9由bxCAN主动驱动，配置为复用推挽输出。 */
    xGpioInit.GPIO_Pin = GPIO_Pin_9;
    xGpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    xGpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &xGpioInit);

    /* PB8只读取收发器输出，不由MCU主动驱动。 */
    xGpioInit.GPIO_Pin = GPIO_Pin_8;
    xGpioInit.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &xGpioInit);
}

static bool prvCanControllerInit(void)
{
    CAN_InitTypeDef xCanInit;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    CAN_DeInit(CAN1);
    CAN_StructInit(&xCanInit);

    xCanInit.CAN_TTCM = DISABLE;    // 禁用时间触发通信模式

    /*
    * Bus-off后由bxCAN按照CAN协议规定的总线空闲条件自动恢复。
    * 应用层只观察状态并限制发送，不在ISR中重新初始化外设。
    */
    xCanInit.CAN_ABOM = ENABLE; //500 kbit/s下的理论最短时间是：128 × 11 × 2 us = 2816 us
    xCanInit.CAN_AWUM = DISABLE; // 禁用自动唤醒模式

    /*
     * 置0，自动重传，CAN硬件在发送报文失败时会一直自动重传直到发送成功
     */
    xCanInit.CAN_NART = DISABLE;

    /*
     * 置0，禁用FIFO锁定，FIFO溢出时，FIFO中最后收到的报文被新报文覆盖
     * 置1，接收FIFO锁定，FIFO溢出时，新收到的报文会被丢弃
     */
    xCanInit.CAN_RFLM = DISABLE;

    /*
     * TXFP=DISABLE时，待发送邮箱按CAN标识符仲裁优先级选择，
     * 与协议ID中的Priority字段保持一致。
     */
    xCanInit.CAN_TXFP = DISABLE;

    /* 必须使用正常模式，回环模式不能验证外部收发器和总线ACK。 */
    xCanInit.CAN_Mode = CAN_Mode_Normal;

    xCanInit.CAN_SJW = CAN_SJW_1tq;
    xCanInit.CAN_BS1 = CAN_BS1_15tq;
    xCanInit.CAN_BS2 = CAN_BS2_2tq;
    xCanInit.CAN_Prescaler = BSP_CAN_PRESCALER; // 设置CAN位时序预分频器

    return CAN_Init(CAN1, &xCanInit) ==
           CAN_InitStatus_Success;
}

bool BspCan_Init(void)
{
    prvCanGpioInit();
    return prvCanControllerInit();
}

bool BspCan_ConfigureExactExtendedDataFilter(
    uint8_t ucFilterNumber,
    uint32_t ulExtendedId)
{
    CAN_FilterInitTypeDef xFilterInit = {0};
    uint32_t ulFilterId;
    uint32_t ulFilterMask;

    if ((ucFilterNumber > BSP_CAN_FILTER_NUMBER_MAX) ||
        (ulExtendedId > BSP_CAN_EXTENDED_ID_MAX))
    {
        return false;
    }

    /*
     * 期望IDE=1、RTR=0：
     * - IDE参与比较，拒绝标准帧；
     * - RTR参与比较，拒绝远程帧；
     * - 29位ID全部参与比较，只允许一个精确ID。
     */
    ulFilterId =
        (ulExtendedId << 3U) |
        BSP_CAN_FILTER_IDE_BIT;

    ulFilterMask =
        BSP_CAN_FILTER_EXTENDED_ID_MASK |
        BSP_CAN_FILTER_IDE_BIT |
        BSP_CAN_FILTER_RTR_BIT;

    xFilterInit.CAN_FilterNumber = ucFilterNumber;
    xFilterInit.CAN_FilterMode = CAN_FilterMode_IdMask; // 使用标识符掩码模式
    xFilterInit.CAN_FilterScale = CAN_FilterScale_32bit; // 32位滤波器

    xFilterInit.CAN_FilterIdHigh =
        (uint16_t)(ulFilterId >> 16U);
    xFilterInit.CAN_FilterIdLow =
        (uint16_t)ulFilterId;

    xFilterInit.CAN_FilterMaskIdHigh =
        (uint16_t)(ulFilterMask >> 16U);
    xFilterInit.CAN_FilterMaskIdLow =
        (uint16_t)ulFilterMask;

    xFilterInit.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    xFilterInit.CAN_FilterActivation = ENABLE; // 激活滤波器

    CAN_FilterInit(&xFilterInit);

    return true;
}

bool BspCan_TryTransmit(
    const BspCanTxFrame_t *pxFrame,
    uint8_t *pucMailbox)
{
    CanTxMsg xTxMessage = {0};
    uint8_t ucMailbox;
    uint8_t ucIndex;

    if ((pxFrame == NULL) ||
        (pucMailbox == NULL) ||
        (pxFrame->ulExtendedId > BSP_CAN_EXTENDED_ID_MAX) ||
        (pxFrame->ucDlc > BSP_CAN_MAX_DATA_LENGTH))
    {
        return false;
    }

    xTxMessage.ExtId = pxFrame->ulExtendedId;
    xTxMessage.IDE = CAN_ID_EXT;
    xTxMessage.RTR = CAN_RTR_DATA;
    xTxMessage.DLC = pxFrame->ucDlc;

    for (ucIndex = 0U; ucIndex < pxFrame->ucDlc; ucIndex++)
    {
        xTxMessage.Data[ucIndex] = pxFrame->ucData[ucIndex];
    }

    /*
     * CAN_Transmit只把报文装入邮箱并置位TXRQ。
     * 它返回邮箱编号不等于发送成功。
     */
    ucMailbox = CAN_Transmit(CAN1, &xTxMessage);

    if (ucMailbox == CAN_TxStatus_NoMailBox)
    {
        return false;
    }

    *pucMailbox = ucMailbox;
    return true;
}

BspCanTxStatus_t BspCan_GetTransmitStatus(uint8_t ucMailbox)
{
    uint8_t ucStatus;

    if (ucMailbox > BSP_CAN_TX_MAILBOX_MAX)
    {
        return BSP_CAN_TX_STATUS_INVALID;
    }

    ucStatus = CAN_TransmitStatus(CAN1, ucMailbox);

    switch (ucStatus)
    {
        case CAN_TxStatus_Pending:
            return BSP_CAN_TX_STATUS_PENDING;

        case CAN_TxStatus_Ok:
            return BSP_CAN_TX_STATUS_SUCCESS;

        case CAN_TxStatus_Failed:
            return BSP_CAN_TX_STATUS_FAILED;

        default:
            return BSP_CAN_TX_STATUS_INVALID;
    }
}

bool BspCan_CancelTransmit(uint8_t ucMailbox)
{
    if (ucMailbox > BSP_CAN_TX_MAILBOX_MAX)
    {
        return false;
    }

    CAN_CancelTransmit(CAN1, ucMailbox);
    return true;
}

bool BspCan_GetErrorSnapshot(
    BspCanErrorSnapshot_t *pxSnapshot)
{
    if (pxSnapshot == NULL)
    {
        return false;
    }

    /*
     * SPL只能返回TEC九位计数器的低八位。
     * 当TEC已经越过255时，必须以Bus-off标志作为判断依据，
     * 不能仅根据该八位值判断是否Bus-off。
     */
    pxSnapshot->ucTransmitErrorCounter =
        CAN_GetLSBTransmitErrorCounter(CAN1);

    pxSnapshot->ucReceiveErrorCounter =
        CAN_GetReceiveErrorCounter(CAN1);

    pxSnapshot->xLastError =
        (BspCanLastError_t)CAN_GetLastErrorCode(CAN1);

    pxSnapshot->xErrorWarning =
        CAN_GetFlagStatus(CAN1, CAN_FLAG_EWG) == SET;

    pxSnapshot->xErrorPassive =
        CAN_GetFlagStatus(CAN1, CAN_FLAG_EPV) == SET;

    pxSnapshot->xBusOff =
        CAN_GetFlagStatus(CAN1, CAN_FLAG_BOF) == SET;

    return true;
}

void BspCan_EnableRxFifo0Interrupt(void)
{
    /*
     * 清除可能遗留的溢出标志。FMP0不通过写位清除，
     * 后续读取并释放FIFO中的报文后会自动撤销。
     */
    CAN_ClearITPendingBit(CAN1, CAN_IT_FOV0);

    /*
     * ISR会调用xQueueSendFromISR()。
     * 当前FreeRTOS允许逻辑优先级5～15调用FromISR API，
     * 因此这里选择逻辑优先级6。
     */
    NVIC_SetPriority(
        USB_LP_CAN1_RX0_IRQn,
        BSP_CAN_RX_INTERRUPT_PRIORITY);
    NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);

    /*
     * 当前SPL的CAN_ITConfig()参数断言只接受单个中断源，
     * 所以两个中断必须分别配置，不能按位或后一次调用。
     */
    CAN_ITConfig(CAN1, CAN_IT_FMP0, ENABLE);    /* 消息挂起中断 */
    CAN_ITConfig(CAN1, CAN_IT_FOV0, ENABLE);    /* 溢出中断 */

    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
}

bool BspCan_TryReceiveFifo0(BspCanRxFrame_t *pxFrame)
{
    CanRxMsg xRxMessage = {0};
    uint8_t ucCopyLength;
    uint8_t ucIndex;

    if ((pxFrame == NULL) ||
        (CAN_MessagePending(CAN1, CAN_FIFO0) == 0U)) // CAN_MessagePending返回0表示FIFO为空
    {
        return false;
    }

    /*
     * CAN_Receive读取邮箱后会置位RFOM0，
     * 从而释放FIFO0中当前最早的一帧。
     */
    CAN_Receive(CAN1, CAN_FIFO0, &xRxMessage);

    *pxFrame = (BspCanRxFrame_t){0};

    pxFrame->xIsExtended =
        xRxMessage.IDE == CAN_ID_EXT;
    pxFrame->xIsRemote =
        xRxMessage.RTR == CAN_RTR_REMOTE; // 判断是否是远程帧

    if (pxFrame->xIsExtended)
    {
        pxFrame->ulExtendedId = xRxMessage.ExtId;
    }

    pxFrame->ucDlc = xRxMessage.DLC;

    /*
     * Classic CAN数据区最多8字节。
     * 原始DLC仍保留，供应用层识别DLC=9～15等非法命令。
     */
    ucCopyLength = xRxMessage.DLC;

    if (ucCopyLength > BSP_CAN_MAX_DATA_LENGTH)
    {
        ucCopyLength = BSP_CAN_MAX_DATA_LENGTH;
    }

    for (ucIndex = 0U; ucIndex < ucCopyLength; ucIndex++)
    {
        pxFrame->ucData[ucIndex] =
            xRxMessage.Data[ucIndex];
    }

    return true;
}

bool BspCan_TakeRxFifo0Overrun(void)
{
    if (CAN_GetITStatus(CAN1, CAN_IT_FOV0) == RESET)
    {
        return false;
    }

    CAN_ClearITPendingBit(CAN1, CAN_IT_FOV0);
    return true;
}