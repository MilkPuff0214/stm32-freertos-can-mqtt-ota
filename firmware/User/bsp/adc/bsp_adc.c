#include "bsp_adc.h"

#include <stddef.h>

#include "stm32f10x_adc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

#define BSP_ADC_GPIO_CLOCK                 RCC_APB2Periph_GPIOC
#define BSP_ADC_GPIO_PORT                  GPIOC
#define BSP_ADC_POTENTIOMETER_PIN          GPIO_Pin_1

#define BSP_ADC_POTENTIOMETER_CHANNEL      ADC_Channel_11
#define BSP_ADC_TEMPERATURE_CHANNEL        ADC_Channel_TempSensor

#define BSP_ADC_POTENTIOMETER_SAMPLE_TIME  ADC_SampleTime_55Cycles5
#define BSP_ADC_TEMPERATURE_SAMPLE_TIME    ADC_SampleTime_239Cycles5

/*
 * 这是轮询次数上限，不是毫秒数。
 * 它只用于防止ADC硬件异常时程序永久卡在等待循环。
 */
#define BSP_ADC_TIMEOUT_COUNT              100000U

/*
 * ADC1每次只执行一个规则通道转换。
 * 两路使用不同采样时间，因此在读取前重新配置Rank 1。
 */
static bool prvReadChannel(
    uint8_t ucChannel,
    uint8_t ucSampleTime,
    uint16_t *pusRawValue)
{
    uint32_t ulTimeoutCount;

    // 选择这一次要转换的通道
    ADC_RegularChannelConfig(
        ADC1,
        ucChannel,
        1U,
        ucSampleTime);

    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE); //启动ADC1规则通道转换

    ulTimeoutCount = BSP_ADC_TIMEOUT_COUNT;

    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        if (ulTimeoutCount == 0U)
        {
            return false;
        }

        ulTimeoutCount--;
    }

    *pusRawValue = ADC_GetConversionValue(ADC1);    //读取一次结果
    return true;
}

bool BspAdc_Init(void)
{
    GPIO_InitTypeDef xGpioInit;
    ADC_InitTypeDef xAdcInit;
    uint32_t ulTimeoutCount;

    /*
     * 当前PCLK2为72 MHz。
     * 六分频后ADC时钟为12 MHz。
     */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    RCC_APB2PeriphClockCmd(
        BSP_ADC_GPIO_CLOCK | RCC_APB2Periph_ADC1,
        ENABLE);

    GPIO_StructInit(&xGpioInit);
    xGpioInit.GPIO_Pin = BSP_ADC_POTENTIOMETER_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(BSP_ADC_GPIO_PORT, &xGpioInit);

    ADC_DeInit(ADC1);
    ADC_StructInit(&xAdcInit);

    xAdcInit.ADC_Mode = ADC_Mode_Independent;   //独立模式
    xAdcInit.ADC_ScanConvMode = DISABLE;    //单通道模式
    xAdcInit.ADC_ContinuousConvMode = DISABLE;     //单次转换模式
    xAdcInit.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; //软件触发  
    xAdcInit.ADC_DataAlign = ADC_DataAlign_Right; //右对齐  
    xAdcInit.ADC_NbrOfChannel = 1U; //一个转换通道

    ADC_Init(ADC1, &xAdcInit);

    /*
     * ADC1_IN16必须先启用内部温度传感器通路。
     * 后面的ADC校准过程也给传感器留下了建立时间。
     */
    ADC_TempSensorVrefintCmd(ENABLE);   //使能内部温度传感器和Vrefint通路
    ADC_Cmd(ADC1, ENABLE);  //使能ADC1

    ADC_ResetCalibration(ADC1); //复位校准寄存器
    ulTimeoutCount = BSP_ADC_TIMEOUT_COUNT;

    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)    //等待复位完成
    {
        if (ulTimeoutCount == 0U)
        {
            return false;
        }

        ulTimeoutCount--;
    }

    ADC_StartCalibration(ADC1); //开始校准
    ulTimeoutCount = BSP_ADC_TIMEOUT_COUNT;

    while (ADC_GetCalibrationStatus(ADC1) != RESET)    //等待校准完成
    {
        if (ulTimeoutCount == 0U)
        {
            return false;
        }

        ulTimeoutCount--;
    }

    return true;
}

bool BspAdc_ReadRaw(
    uint16_t *pusPotentiometerRaw,
    uint16_t *pusTemperatureRaw)
{
    uint16_t usPotentiometerRaw;
    uint16_t usTemperatureRaw;

    if ((pusPotentiometerRaw == NULL) ||
        (pusTemperatureRaw == NULL))
    {
        return false;
    }

    if (prvReadChannel(
            BSP_ADC_POTENTIOMETER_CHANNEL,
            BSP_ADC_POTENTIOMETER_SAMPLE_TIME,
            &usPotentiometerRaw) == false)
    {
        return false;
    }

    if (prvReadChannel(
            BSP_ADC_TEMPERATURE_CHANNEL,
            BSP_ADC_TEMPERATURE_SAMPLE_TIME,
            &usTemperatureRaw) == false)
    {
        return false;
    }

    /*
     * 两路都成功后再更新调用者对象，
     * 避免失败时只返回一半有效数据。
     */
    *pusPotentiometerRaw = usPotentiometerRaw;
    *pusTemperatureRaw = usTemperatureRaw;

    return true;
}