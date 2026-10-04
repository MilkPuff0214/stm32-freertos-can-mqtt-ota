#include "bsp_usart.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"

#include <stddef.h>

#include "stm32f10x_dma.h"
/*
 * 板载CH340G通过USART1与MCU连接：
 * PA9为USART1_TX，PA10为USART1_RX。
 */
#define BSP_USART1                  USART1
#define BSP_USART1_BAUDRATE         115200U

#define BSP_USART1_GPIO_PORT        GPIOA
#define BSP_USART1_TX_PIN           GPIO_Pin_9
#define BSP_USART1_RX_PIN           GPIO_Pin_10

/*
 * STM32F103的USART1_TX硬件映射到DMA1 Channel 4。
 * 这个映射由芯片决定，不是任意选择的通道。
 */
#define BSP_USART1_TX_DMA_CHANNEL    DMA1_Channel4
/*
 * STM32F103的USART1_RX固定映射到DMA1 Channel 5。
 */
#define BSP_USART1_RX_DMA_CHANNEL    DMA1_Channel5
/*
 * DMA直接写入该循环缓冲区。
 * Cortex-M3没有数据Cache，因此当前不需要Cache一致性处理。
 */
static uint8_t s_ucRxDmaBuffer[BSP_USART1_RX_DMA_BUFFER_SIZE];

void BspUsart1_Init(void)
{
    GPIO_InitTypeDef xGpioInit;
    USART_InitTypeDef xUsartInit;

    /*
     * USART1和GPIOA都挂载在APB2。
     * PA9/PA10使用默认USART1映射，因此不需要AFIO重映射。
     */
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1,
        ENABLE);

    GPIO_StructInit(&xGpioInit);

    /*
     * USART TX由外设主动驱动线路，因此配置为复用推挽输出。
     */
    xGpioInit.GPIO_Pin = BSP_USART1_TX_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
    xGpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BSP_USART1_GPIO_PORT, &xGpioInit);

    /*
     * USART RX由板载CH340G驱动，配置为浮空输入。
     */
    xGpioInit.GPIO_Pin = BSP_USART1_RX_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BSP_USART1_GPIO_PORT, &xGpioInit);

    USART_StructInit(&xUsartInit);

    xUsartInit.USART_BaudRate = BSP_USART1_BAUDRATE;
    xUsartInit.USART_WordLength = USART_WordLength_8b;
    xUsartInit.USART_StopBits = USART_StopBits_1;
    xUsartInit.USART_Parity = USART_Parity_No;
    xUsartInit.USART_HardwareFlowControl =
        USART_HardwareFlowControl_None;   //无硬件流控

    /*
     * 从第一版开始同时启用发送和接收，
     * 后续可以直接增加TX/RX DMA而不改变基础串口参数。
     */
    xUsartInit.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(BSP_USART1, &xUsartInit);        //初始化USART1
    USART_Cmd(BSP_USART1, ENABLE);      //使能USART1

    DMA_InitTypeDef xDmaInit;

    /* DMA1挂载在AHB总线上，使用前必须打开控制器时钟。 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(BSP_USART1_TX_DMA_CHANNEL);
    DMA_StructInit(&xDmaInit);

    /*
    * 外设地址始终是USART1数据寄存器，因此外设地址不递增。
    * 内存地址需要逐字节移动，从而依次读取整个发送缓冲区。
    */
    xDmaInit.DMA_PeripheralBaseAddr = (uint32_t)&BSP_USART1->DR;
    xDmaInit.DMA_MemoryBaseAddr = 0U;
    xDmaInit.DMA_DIR = DMA_DIR_PeripheralDST;  //数据从内存传输到外设
    xDmaInit.DMA_BufferSize = 0U;
    xDmaInit.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    xDmaInit.DMA_MemoryInc = DMA_MemoryInc_Enable;  //内存地址递增
    xDmaInit.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;  //外设数据宽度为字节
    xDmaInit.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte; //内存数据宽度为字节
    xDmaInit.DMA_Mode = DMA_Mode_Normal;            //非循环模式，区别：DMA_Mode_Circular循环模式，两种模式的区别是：Normal模式传输完成后停止，Circular模式传输完成后自动重新开始
    xDmaInit.DMA_Priority = DMA_Priority_Medium;    //DMA通道竞争时，中等优先级
    xDmaInit.DMA_M2M = DMA_M2M_Disable;     // 禁止内存到内存模式

    DMA_Init(BSP_USART1_TX_DMA_CHANNEL, &xDmaInit);

    
    /*
    * 当前只启用传输完成中断。
    * DMA搬运完本次全部字节后，DMA1 Channel 4会进入中断。
    */
    DMA_ClearFlag(DMA1_FLAG_GL4);
    DMA_ITConfig(BSP_USART1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

    /*
    * 该中断会调用FreeRTOS的FromISR API。
    *
    * 当前configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY为5，
    * 所以必须使用逻辑优先级5或更低紧迫度的数值。
    * 这里选择逻辑优先级6。
    */
    NVIC_SetPriority(DMA1_Channel4_IRQn, 6U);
    NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);

    /*
    * 允许USART1在发送数据寄存器空闲时产生DMA请求。
    * 此处只是接通请求源，并不会立即开始发送。
    */
    USART_DMACmd(BSP_USART1, USART_DMAReq_Tx, ENABLE);


    /*
    * 配置USART1 RX循环DMA。
    *
    * RX数据不能等待CPU，因此其DMA优先级高于当前TX DMA。
    */
    DMA_DeInit(BSP_USART1_RX_DMA_CHANNEL);
    DMA_StructInit(&xDmaInit);

    xDmaInit.DMA_PeripheralBaseAddr = (uint32_t)&BSP_USART1->DR;
    xDmaInit.DMA_MemoryBaseAddr = (uint32_t)s_ucRxDmaBuffer;
    xDmaInit.DMA_DIR = DMA_DIR_PeripheralSRC;       //数据从外设传输到内存
    xDmaInit.DMA_BufferSize = BSP_USART1_RX_DMA_BUFFER_SIZE;    //内存缓冲区大小
    xDmaInit.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    xDmaInit.DMA_MemoryInc = DMA_MemoryInc_Enable;
    xDmaInit.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    xDmaInit.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    xDmaInit.DMA_Mode = DMA_Mode_Circular;      //循环模式，区别：DMA_Mode_Circular循环模式，两种模式的区别是：Normal模式传输完成后停止，Circular模式传输完成后自动重新开始
    xDmaInit.DMA_Priority = DMA_Priority_High;
    xDmaInit.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(BSP_USART1_RX_DMA_CHANNEL, &xDmaInit);
    DMA_ClearFlag(DMA1_FLAG_GL5);
}

/**
 * @brief 启动一次USART1 TX DMA传输。
 * @param pData 指向要发送的数据缓冲区。
 * @param length 要发送的数据字节数。
 * @return true表示成功启动，false表示启动失败。
 */
bool BspUsart1_TxDmaStart(const uint8_t *pData, uint16_t length)
{
    /*
     * DMA不能读取空指针，也没有必要启动零字节传输。
     */
    if ((pData == NULL) || (length == 0U))
    {
        return false;
    }

    /*
     * CNDTR不为0表示通道仍有数据没有搬运完成。
     * 当前阶段不允许用新数据覆盖正在工作的DMA通道。
     */
    if (DMA_GetCurrDataCounter(BSP_USART1_TX_DMA_CHANNEL) != 0U)
    {
        return false;
    }

    /*
     * 修改DMA地址和计数器前必须先关闭通道。
     * GL4会同时清除Channel 4的全局、完成、半完成和错误标志。
     */
    DMA_Cmd(BSP_USART1_TX_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL4);

    BSP_USART1_TX_DMA_CHANNEL->CMAR = (uint32_t)pData;
    DMA_SetCurrDataCounter(BSP_USART1_TX_DMA_CHANNEL, length);

    /*
     * 使能后，USART1_TX请求会驱动DMA逐字节搬运数据。
     * CPU不需要再等待每一个TXE标志。
     */
    DMA_Cmd(BSP_USART1_TX_DMA_CHANNEL, ENABLE);

    return true;
}

bool BspUsart1_TxDmaHandleInterrupt(void)
{
    /*
     * DMA1可能还有其他通道产生中断，因此先确认Channel 4的
     * Transfer Complete标志是否有效。
     */
    if (DMA_GetITStatus(DMA1_IT_TC4) == RESET)
    {
        return false;
    }

    /*
     * 普通模式传输完成后关闭通道。
     * 下一次发送前，任务会重新填写CMAR和CNDTR。
     */
    DMA_Cmd(BSP_USART1_TX_DMA_CHANNEL, DISABLE);
    DMA_ClearITPendingBit(DMA1_IT_GL4);

    return true;
}


void BspUsart1_RxDmaStart(void)
{
    volatile uint32_t ulStatus;
    volatile uint32_t ulData;

    /*
     * 重新装载循环DMA前，先断开USART RX DMA请求并关闭通道。
     */
    USART_DMACmd(BSP_USART1, USART_DMAReq_Rx, DISABLE); 
    DMA_Cmd(BSP_USART1_RX_DMA_CHANNEL, DISABLE);

    DMA_ClearFlag(DMA1_FLAG_GL5);
    //设置DMA传输计数器为缓冲区大小，准备接收数据
    DMA_SetCurrDataCounter(
        BSP_USART1_RX_DMA_CHANNEL,
        BSP_USART1_RX_DMA_BUFFER_SIZE);

    /*
     * STM32F1通过“先读SR、再读DR”的固定顺序清除
     * 可能残留的IDLE、RXNE和ORE接收状态。
     */
    ulStatus = BSP_USART1->SR;
    ulData = BSP_USART1->DR;
    (void)ulStatus;
    (void)ulData;

    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_ClearPendingIRQ(USART1_IRQn);

    /*
     * 先准备好DMA，再允许USART产生RX DMA请求。
     */
    DMA_Cmd(BSP_USART1_RX_DMA_CHANNEL, ENABLE);
    USART_DMACmd(BSP_USART1, USART_DMAReq_Rx, ENABLE);  

    /*
     * USART1中断会调用vTaskNotifyGiveFromISR()，
     * 因此使用允许调用FreeRTOS FromISR API的逻辑优先级6。
     */
    USART_ITConfig(BSP_USART1, USART_IT_IDLE, ENABLE);
    NVIC_EnableIRQ(USART1_IRQn);
}


const uint8_t *BspUsart1_RxDmaGetBuffer(void)
{
    return s_ucRxDmaBuffer;
}

uint16_t BspUsart1_RxDmaGetWritePosition(void)
{
    uint16_t usRemaining; //DMA CNDTR寄存器保存当前传输还剩多少字节没有搬运。
    uint16_t usWritePosition;   //计算出下一次DMA写入的位置，范围为0～255。

    /*
     * CNDTR保存本轮循环还剩多少字节没有传输。
     *
     * 例如CNDTR从256下降到246，表示已经收到10字节，
     * 所以下一个写入位置是10。
     */
    usRemaining = DMA_GetCurrDataCounter(
        BSP_USART1_RX_DMA_CHANNEL);

    usWritePosition =
        BSP_USART1_RX_DMA_BUFFER_SIZE - usRemaining;

    /*
     * DMA完成一整圈时可能短暂得到256，
     * 对循环缓冲区来说它等价于位置0。
     */
    if (usWritePosition >= BSP_USART1_RX_DMA_BUFFER_SIZE)
    {
        usWritePosition = 0U;
    }

    return usWritePosition;
}

bool BspUsart1_RxIdleHandleInterrupt(void)
{
    volatile uint32_t ulStatus;
    volatile uint32_t ulData;

    if (USART_GetITStatus(BSP_USART1, USART_IT_IDLE) == RESET)
    {
        return false;
    }

    /*
     * STM32F1的IDLE标志不能只靠普通写位清除，
     * 必须按顺序读取SR和DR。
     */
    ulStatus = BSP_USART1->SR;
    ulData = BSP_USART1->DR;
    (void)ulStatus;
    (void)ulData;

    return true;
}