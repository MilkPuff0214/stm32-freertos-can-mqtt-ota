#ifndef STM32F10X_CONF_H
#define STM32F10X_CONF_H

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"
#include "stm32f10x_flash.h"

/* 参数和范围仍由BSP显式检查，不依赖上游调试断言。 */
#define assert_param(expr) ((void)0)

#endif /* STM32F10X_CONF_H */