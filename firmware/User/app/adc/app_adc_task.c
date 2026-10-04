#include "app_adc_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_adc.h"

#define APP_ADC_SAMPLE_PERIOD_MS       1000U
#define APP_ADC_TASK_STACK_DEPTH       256U
#define APP_ADC_TASK_PRIORITY          (tskIDLE_PRIORITY + 1U)

/* STM32F103数据手册给出的典型换算参数，未补偿实际VDDA和个体误差。 */
#define APP_ADC_REFERENCE_VOLTAGE_V              3.3f
#define APP_ADC_FULL_SCALE                       4095.0f
#define APP_ADC_TEMPERATURE_V25_V                1.43f
#define APP_ADC_TEMPERATURE_SLOPE_V_PER_C        0.0043f

static TaskHandle_t s_xAdcTaskHandle = NULL;    //任务句柄

/* 便于调试器观察ADC读取失败次数。 */
static volatile uint32_t s_ulAdcReadFailureCount = 0U;


#define APP_ADC_SAMPLE_QUEUE_LENGTH    1U

static QueueHandle_t s_xAdcSampleQueue = NULL;  //ADC样本队列句柄

static float prvRawToVoltage(uint16_t usRaw)
{
    return ((float)usRaw * APP_ADC_REFERENCE_VOLTAGE_V) / APP_ADC_FULL_SCALE;
}

static float prvRawToTemperatureCelsius(uint16_t usRaw)
{
    float fSensorVoltage = prvRawToVoltage(usRaw);

    return ((APP_ADC_TEMPERATURE_V25_V - fSensorVoltage) /
            APP_ADC_TEMPERATURE_SLOPE_V_PER_C) + 25.0f;
}

static void prvAdcTask(void *pvParameters)
{
    AppAdcSample_t xSample;
    BaseType_t xPublishResult;

    (void)pvParameters;

    for (;;)
    {
        /*
         * 启动后先阻塞1秒，让启动信息优先通过串口发送。
         * ADC Task是ADC1的唯一所有者。
         */
        vTaskDelay(pdMS_TO_TICKS(APP_ADC_SAMPLE_PERIOD_MS));

        /*
         * 每轮先清空整个样本。
         * 如果本次读取失败，有效标志保持false。
         */
        xSample = (AppAdcSample_t){0};

        if (BspAdc_ReadRaw(
                &xSample.usPotentiometerRaw,
                &xSample.usTemperatureRaw) == true)
        {
            xSample.fPotentiometerVoltage = prvRawToVoltage(xSample.usPotentiometerRaw);
            xSample.fCpuTemperatureCelsius =
                prvRawToTemperatureCelsius(xSample.usTemperatureRaw);
            xSample.xValid = true;
        }
        else
        {
            s_ulAdcReadFailureCount++;
        }

        /*
         * Queue长度必须为1。
         * 新样本直接覆盖旧样本，不积压已经失去实时价值的数据。
         *
         * 读取失败时也发布无效样本，防止CAN Task永远把上一次
         * 成功采样误认为当前仍然有效。
         */
        xPublishResult = xQueueOverwrite(s_xAdcSampleQueue, &xSample);
        configASSERT(xPublishResult == pdPASS);
        (void)xPublishResult;
    }
}

bool AppAdcTask_Create(void)
{
    BaseType_t xCreateResult;

    if ((s_xAdcTaskHandle != NULL) ||
        (s_xAdcSampleQueue != NULL))
    {
        return false;
    }

    s_xAdcSampleQueue = xQueueCreate(
        APP_ADC_SAMPLE_QUEUE_LENGTH,
        sizeof(AppAdcSample_t));

    if (s_xAdcSampleQueue == NULL)
    {
        return false;
    }

    xCreateResult = xTaskCreate(
        prvAdcTask,
        "ADC",
        APP_ADC_TASK_STACK_DEPTH,
        NULL,
        APP_ADC_TASK_PRIORITY,
        &s_xAdcTaskHandle);

    if (xCreateResult != pdPASS)
    {
        vQueueDelete(s_xAdcSampleQueue);
        s_xAdcSampleQueue = NULL;
        s_xAdcTaskHandle = NULL;
        return false;
    }

    return true;
}

bool AppAdc_TryReadLatestSample(AppAdcSample_t *pxSample)
{
    if ((pxSample == NULL) ||
        (s_xAdcSampleQueue == NULL))
    {
        return false;
    }

    /*
     * 使用Peek而不是Receive：
     * Peek复制最新样本但不把它从Queue删除，因此在下一次ADC采样
     * 到来之前，CAN Task仍然可以取得当前最新的完整样本。
     *
     * 等待时间为0，CAN Task绝不会在这里等待ADC Task。
     */
    return xQueuePeek(
               s_xAdcSampleQueue,
               pxSample,
               0U) == pdPASS;
}
