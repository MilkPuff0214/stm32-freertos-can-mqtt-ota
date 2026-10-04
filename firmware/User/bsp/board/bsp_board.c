#include "bsp_board.h"

#include "bsp_adc.h"
#include "bsp_buzzer.h"
#include "bsp_can.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "bsp_w5500.h"
#include "bsp_w25q64.h"

BspBoardInitStatus_t BspBoard_Init(void)
{
    /*
     * 顺序与P1实机验收版本保持一致。
     * 本层只编排BSP，不创建RTOS对象或启用应用侧接收中断。
     */
    LED_GPIO_Config();
    BspBuzzer_Init();
    BspKey_Init();
    BspUsart1_Init();
    /* 只配置接口并保持W5500复位；释放复位和网络验证由NET任务负责。 */
    BspW5500_InterfaceInit();
    BspW25Q64_InterfaceInit();
    
    if (BspAdc_Init() == false)
    {
        return BSP_BOARD_INIT_STATUS_ADC_FAILED;
    }

    if (BspCan_Init() == false)
    {
        return BSP_BOARD_INIT_STATUS_CAN_FAILED;
    }

    return BSP_BOARD_INIT_STATUS_OK;
}
