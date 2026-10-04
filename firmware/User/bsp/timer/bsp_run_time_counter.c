#include "bsp_run_time_counter.h"

#include "stm32f10x_dbgmcu.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"

#define BSP_RUN_TIME_COUNTER_TIMER             TIM6
#define BSP_RUN_TIME_COUNTER_TIMER_CLOCK       RCC_APB1Periph_TIM6
#define BSP_RUN_TIME_COUNTER_IRQ               TIM6_IRQn

#define BSP_RUN_TIME_COUNTER_FREQUENCY_HZ      10000UL
#define BSP_RUN_TIME_COUNTER_PERIOD            0xFFFFU
#define BSP_RUN_TIME_COUNTER_IRQ_PRIORITY      4U
#define BSP_RUN_TIME_COUNTER_LOW_BIT_COUNT     16U

/*
 * 记录TIM6发生过多少次16位回绕。
 * ISR和任务切换路径都会访问，因此必须使用volatile。
 */
static volatile uint32_t s_ulTimerOverflowCount = 0U;

void BspRunTimeCounter_Init(void)
{
    RCC_ClocksTypeDef xRccClocks;   //用于获取当前时钟频率
    TIM_TimeBaseInitTypeDef xTimeBaseInit;
    uint32_t ulTimerClockHz;

    NVIC_DisableIRQ(BSP_RUN_TIME_COUNTER_IRQ);  //禁用TIM6中断，避免在初始化过程中触发中断

    RCC_APB1PeriphClockCmd(
        BSP_RUN_TIME_COUNTER_TIMER_CLOCK,
        ENABLE);    //使能TIM6时钟，确保定时器可以正常工作

    TIM_DeInit(BSP_RUN_TIME_COUNTER_TIMER);
    s_ulTimerOverflowCount = 0U;

    /*
     * APB1分频不为1时，STM32F1会自动把定时器时钟乘2。
     * 当前PCLK1为36 MHz，因此TIM6实际输入时钟为72 MHz。
     */
    RCC_GetClocksFreq(&xRccClocks);
    ulTimerClockHz = xRccClocks.PCLK1_Frequency;

    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U)
    {
        ulTimerClockHz *= 2UL;
    }

    TIM_TimeBaseStructInit(&xTimeBaseInit);

    xTimeBaseInit.TIM_Prescaler =
        (uint16_t)(
            (ulTimerClockHz /
             BSP_RUN_TIME_COUNTER_FREQUENCY_HZ) - 1UL); //设置预分频器，使TIM6的计数频率为10 kHz，即每个计数周期为100 us

    xTimeBaseInit.TIM_Period =
        BSP_RUN_TIME_COUNTER_PERIOD;    //设置自动重装载寄存器的值，使计数器在达到0xFFFF时回绕

    TIM_TimeBaseInit(
        BSP_RUN_TIME_COUNTER_TIMER,
        &xTimeBaseInit);

    TIM_SetCounter(BSP_RUN_TIME_COUNTER_TIMER, 0U);    //将计数器清零，确保从0开始计数

    /*
     * TIM_TimeBaseInit()会产生一次更新事件，
     * 必须清除由该事件产生的UIF，避免启动后误计一次回绕。
     */
    TIM_ClearITPendingBit(
        BSP_RUN_TIME_COUNTER_TIMER,
        TIM_IT_Update);

    TIM_ITConfig(
        BSP_RUN_TIME_COUNTER_TIMER,
        TIM_IT_Update,
        ENABLE);    //使能TIM6的更新中断，当计数器回绕时触发中断

    /*
     * 调试器暂停CPU时同时冻结TIM6，
     * 防止断点停留时间被计算到当前任务中。
     */
    DBGMCU_Config(DBGMCU_TIM6_STOP, ENABLE);

    /*
     * TIM6 ISR不调用FreeRTOS API，因此可以使用逻辑优先级4。
     * 它不会被FreeRTOS的BASEPRI临界区屏蔽。
     */
    NVIC_SetPriority(
        BSP_RUN_TIME_COUNTER_IRQ,
        BSP_RUN_TIME_COUNTER_IRQ_PRIORITY);

    NVIC_ClearPendingIRQ(BSP_RUN_TIME_COUNTER_IRQ);
    NVIC_EnableIRQ(BSP_RUN_TIME_COUNTER_IRQ);

    TIM_Cmd(BSP_RUN_TIME_COUNTER_TIMER, ENABLE);
}

uint64_t BspRunTimeCounter_GetValue(void)
{
    uint32_t ulOverflowCount;
    uint16_t usCounterLow;

    /*
    FreeRTOS 在 Cortex-M3 上平时进入临界区主要使用 BASEPRI 屏蔽一部分中断，
    并没有把“全局中断”真正关闭；所以调用到这里时，PRIMASK 通常仍然是 0。此时再执行 __disable_irq()，
    才会通过 PRIMASK 把所有普通可屏蔽中断整体关掉。
     */
    __disable_irq();

    ulOverflowCount = s_ulTimerOverflowCount;
    usCounterLow =
        (uint16_t)BSP_RUN_TIME_COUNTER_TIMER->CNT;

    /*
     * 低16位已经回绕、ISR还没执行时，
     * 临时在本次读取结果中补偿一次高位。
     */
    if ((BSP_RUN_TIME_COUNTER_TIMER->SR &
         TIM_FLAG_Update) != 0U)
    {
        ulOverflowCount++;

        usCounterLow =
            (uint16_t)BSP_RUN_TIME_COUNTER_TIMER->CNT;
    }

    __enable_irq();

    return
        ((uint64_t)ulOverflowCount <<
         BSP_RUN_TIME_COUNTER_LOW_BIT_COUNT) |
        (uint64_t)usCounterLow;
}

/**
 * @brief 处理TIM6计数器回绕中断。
 *
 * @note 本ISR只扩展计数器高位，不调用任何FreeRTOS API。
 */
void TIM6_IRQHandler(void)
{
    if (TIM_GetITStatus(
            BSP_RUN_TIME_COUNTER_TIMER,
            TIM_IT_Update) == RESET)
    {
        return;
    }

    s_ulTimerOverflowCount++;

    TIM_ClearITPendingBit(
        BSP_RUN_TIME_COUNTER_TIMER,
        TIM_IT_Update);
}