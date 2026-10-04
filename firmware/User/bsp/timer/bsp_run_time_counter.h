#ifndef BSP_RUN_TIME_COUNTER_H
#define BSP_RUN_TIME_COUNTER_H

#include <stdint.h>

/**
 * @brief 初始化专用于FreeRTOS运行时间统计的硬件计数器。
 *
 * @note 由FreeRTOS在启动调度器时调用一次，不需要main.c主动调用。
 */
void BspRunTimeCounter_Init(void);

/**
 * @brief 获取单调递增的运行时间计数值。
 * @return 当前计数值，每个计数代表100 us。
 *
 * @note 本函数会在任务切换路径中频繁调用，
 *       因此不能阻塞，也不能调用FreeRTOS API。
 */
uint64_t BspRunTimeCounter_GetValue(void);

#endif /* BSP_RUN_TIME_COUNTER_H */