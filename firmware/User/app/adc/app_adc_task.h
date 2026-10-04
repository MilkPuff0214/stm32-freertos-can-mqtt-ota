#ifndef APP_ADC_TASK_H
#define APP_ADC_TASK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 这是ADC Task向其他应用模块发布的完整样本快照。
 * 原始值和换算结果作为同一个Queue元素一起复制，避免消费者
 * 读到一半旧数据、一半新数据。当前BSP按两路全部成功或失败返回，
 * 因此只需要一个完整样本有效标志。
 */
typedef struct
{
    uint16_t usPotentiometerRaw;    //原始ADC值
    uint16_t usTemperatureRaw;       //原始ADC值
    float fPotentiometerVoltage;     //换算后的电压值
    float fCpuTemperatureCelsius;    //换算后的CPU温度值（摄氏度）
    bool xValid;
} AppAdcSample_t;

/**
 * @brief 创建ADC周期采样任务及最新样本Queue。
 * @return true表示创建成功，false表示已经创建或创建失败。
 */
bool AppAdcTask_Create(void);

/**
 * @brief 非阻塞读取最新ADC样本的完整副本。
 * @param pxSample 用于接收样本副本。
 * @return true表示已有样本，false表示尚未发布样本或参数无效。
 */
bool AppAdc_TryReadLatestSample(AppAdcSample_t *pxSample);

#endif
