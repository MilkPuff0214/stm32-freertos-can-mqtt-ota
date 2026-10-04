#include "boot_controller.h"
#include "bsp_w25q64.h"
#include "stm32f10x.h"

/* 官方启动文件调用 __libc_init_array()；-nostartfiles 时由本工程提供 _init。
 * 裸机 Boot 没有额外的 C 库初始化工作，不在这里初始化外设。 */
void _init(void)
{
}

int main(void)
{
    SystemCoreClockUpdate();
    if ((SystemCoreClock < 1000U) ||
        (SysTick_Config(SystemCoreClock / 1000U) != 0U))
    {
        for (;;)
        {
            /* 时基不可用时不进入任何擦写路径。 */
        }
    }

    BspW25Q64_InterfaceInit();
    BootController_Run();

    for (;;)
    {
        /* Controller 不应返回；停留供调试器检查。 */
    }
}
