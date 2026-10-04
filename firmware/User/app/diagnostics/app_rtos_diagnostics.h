#ifndef APP_RTOS_DIAGNOSTICS_H
#define APP_RTOS_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    /*
     * 报告内存由诊断模块持有，调用者不能修改或释放。
     */
    const uint8_t *pucData;
    uint16_t usLength;
} AppRtosDiagnosticsReport_t;

/**
 * @brief 生成当前FreeRTOS任务状态、栈余量和运行时间报告。
 * @param pReport 输出报告，必须指向有效内存。
 * @return true表示成功生成报告；false表示参数无效或生成失败。
 *
 * @note 本接口只用于低频诊断，不能在ISR或硬实时路径中调用。
 */
bool AppRtosDiagnostics_BuildTaskList(
    AppRtosDiagnosticsReport_t *pReport);

/**
 * @brief 生成FreeRTOS Heap当前余量和历史最小余量报告。
 * @param pReport 输出报告，必须指向有效内存。
 * @return true表示成功生成报告；false表示参数无效或生成失败。
 *
 * @note 报告单位为字节，只表示heap_4管理的FreeRTOS Heap，
 *       不表示STM32全部SRAM。
 */
bool AppRtosDiagnostics_BuildHeapReport(
    AppRtosDiagnosticsReport_t *pReport);

#endif /* APP_RTOS_DIAGNOSTICS_H */