#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief  Malloc failed hook function.
 * @note   动态内存分配失败时调用该函数，通常是因为堆空间不足。
 * @param  None
 * @return None
 */
void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
/**
 * @brief  Stack overflow hook function.
 * @note   当任务栈溢出时调用该函数。
 * @param  xTask      溢出的任务句柄
 * @param  pcTaskName 溢出的任务名称
 * @return None
 */
void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                    char * pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;

    taskDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
