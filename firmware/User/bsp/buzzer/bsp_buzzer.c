#include "bsp_buzzer.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_BUZZER_GPIO_PORT    GPIOC
#define BSP_BUZZER_GPIO_PIN     GPIO_Pin_0
#define BSP_BUZZER_GPIO_CLOCK   RCC_APB2Periph_GPIOC

void BspBuzzer_Init(void)
{
    GPIO_InitTypeDef xGpioInit;

    RCC_APB2PeriphClockCmd(BSP_BUZZER_GPIO_CLOCK, ENABLE);

    /*
     * PC0高电平使Q2导通并驱动蜂鸣器。
     * 配置为输出前先写入低电平，避免初始化过程中短暂鸣叫。
     */
    GPIO_ResetBits(
        BSP_BUZZER_GPIO_PORT,
        BSP_BUZZER_GPIO_PIN);

    GPIO_StructInit(&xGpioInit);
    xGpioInit.GPIO_Pin = BSP_BUZZER_GPIO_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
    xGpioInit.GPIO_Speed = GPIO_Speed_2MHz;

    GPIO_Init(BSP_BUZZER_GPIO_PORT, &xGpioInit);
}

void BspBuzzer_Set(bool xOn)
{
    if (xOn)
    {
        GPIO_SetBits(
            BSP_BUZZER_GPIO_PORT,
            BSP_BUZZER_GPIO_PIN);
    }
    else
    {
        GPIO_ResetBits(
            BSP_BUZZER_GPIO_PORT,
            BSP_BUZZER_GPIO_PIN);
    }
}