#include "FreeRTOS.h"
#include "task.h"

#include "app_startup.h"
#include "bsp_board.h"
#include "stm32f10x.h"

#include "Appinfo.h"

/* 启动状态保留为volatile，便于停机后通过GDB检查。 */
static volatile BspBoardInitStatus_t s_xBoardInitStatus =
    BSP_BOARD_INIT_STATUS_OK;

static volatile AppStartupStatus_t s_xAppStartupStatus =
    APP_STARTUP_STATUS_OK;

static volatile BaseType_t s_xSchedulerReturned = pdFALSE;

static void prvHalt(void)
{
    for (;;)
    {
    }
}

extern const uint32_t g_pfnVectors[];

int main(void)
{
    SCB->VTOR = (uint32_t)(uintptr_t)g_pfnVectors;
    __DSB();
    __ISB();
    __enable_irq();
    /*
     * 所有板级硬件都在创建RTOS对象前完成初始化。
     */
    s_xBoardInitStatus = BspBoard_Init();

    if (s_xBoardInitStatus != BSP_BOARD_INIT_STATUS_OK)
    {
        prvHalt();
    }

    /*
     * 创建Queue、Task并按依赖关系连接应用模块。
     */
    s_xAppStartupStatus = AppStartup_Create();

    if (s_xAppStartupStatus != APP_STARTUP_STATUS_OK)
    {
        prvHalt();
    }

    vTaskStartScheduler();

    /*
     * Cortex-M3端口正常启动后不会返回。
     * 返回通常表示Idle Task创建所需的Heap不足。
     */
    s_xSchedulerReturned = pdTRUE;
    prvHalt();
}
