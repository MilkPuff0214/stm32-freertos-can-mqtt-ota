#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化ADC1、板载电位器通道和内部温度通道。
 * @return true表示初始化和校准成功，false表示校准超时。
 */
bool BspAdc_Init(void);

/**
 * @brief 分别执行一次电位器和内部温度传感器转换。
 * @param pusPotentiometerRaw 保存电位器12位原始值。
 * @param pusTemperatureRaw 保存内部温度传感器12位原始值。
 * @return true表示两次转换均完成，false表示参数无效或转换超时。
 *
 * @note 本接口不支持并发调用，后续只允许ADC Sample Task拥有ADC1。
 */
bool BspAdc_ReadRaw(
    uint16_t *pusPotentiometerRaw,
    uint16_t *pusTemperatureRaw);

#endif /* BSP_ADC_H */