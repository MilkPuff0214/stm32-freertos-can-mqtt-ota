#include "bsp_key.h"

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_KEY_1_GPIO_CLOCK    RCC_APB2Periph_GPIOA
#define BSP_KEY_1_GPIO_PORT     GPIOA
#define BSP_KEY_1_GPIO_PIN      GPIO_Pin_0

#define BSP_KEY_2_GPIO_CLOCK    RCC_APB2Periph_GPIOC
#define BSP_KEY_2_GPIO_PORT     GPIOC
#define BSP_KEY_2_GPIO_PIN      GPIO_Pin_13

/**
  * @brief  配置按键用到的I/O口
  * @param  无
  * @retval 无
  */
void BspKey_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	
     /*
     * 先设置结构体默认值，避免没有显式赋值的成员保留未定义内容。
     * 虽然输入模式通常不使用GPIO_Speed，完整初始化仍然更加稳妥。
     */
    GPIO_StructInit(&GPIO_InitStructure);

	/*开启按键端口的时钟*/
	RCC_APB2PeriphClockCmd(BSP_KEY_1_GPIO_CLOCK | BSP_KEY_2_GPIO_CLOCK, ENABLE);
	
	//选择按键的引脚
	GPIO_InitStructure.GPIO_Pin = BSP_KEY_1_GPIO_PIN; 
    /*
    * 开发板已有4.7 kΩ外部下拉电阻：
    * 释放时为低电平，按下时接通3.3 V并变为高电平。
    * 因此外部电阻已经确定了默认电平，GPIO使用浮空输入。
    */
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; 
	//使用结构体初始化按键
	GPIO_Init(BSP_KEY_1_GPIO_PORT, &GPIO_InitStructure);
	
	//选择按键的引脚
	GPIO_InitStructure.GPIO_Pin = BSP_KEY_2_GPIO_PIN; 
	//设置按键的引脚为浮空输入
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; 
	//使用结构体初始化按键
	GPIO_Init(BSP_KEY_2_GPIO_PORT, &GPIO_InitStructure);
}

/**
  * @brief  读取按键的状态
  * @param  keyId: 按键的ID
  * @retval 按键的状态，BSP_KEY_RELEASED表示未按下，BSP_KEY_PRESSED表示按下
  */
BspKeyState_t BspKey_Read(BspKeyId_t keyId)
{
    uint8_t pinState;

    switch (keyId)
    {
        case BSP_KEY_ID_1:
            pinState = GPIO_ReadInputDataBit(
                BSP_KEY_1_GPIO_PORT,
                BSP_KEY_1_GPIO_PIN);
            break;

        case BSP_KEY_ID_2:
            pinState = GPIO_ReadInputDataBit(
                BSP_KEY_2_GPIO_PORT,
                BSP_KEY_2_GPIO_PIN);
            break;

        default:
            return BSP_KEY_RELEASED;
    }

    if (pinState != Bit_RESET)
    {
        return BSP_KEY_PRESSED;
    }

    return BSP_KEY_RELEASED;
}
