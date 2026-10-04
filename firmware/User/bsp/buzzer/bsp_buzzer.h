#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include <stdbool.h>

/**
 * @brief 初始化板载蜂鸣器PC0，并保证初始状态为关闭。
 */
void BspBuzzer_Init(void);

/**
 * @brief 设置板载蜂鸣器逻辑状态。
 *
 * @param xOn true表示响，false表示关闭。
 */
void BspBuzzer_Set(bool xOn);

#endif /* BSP_BUZZER_H */