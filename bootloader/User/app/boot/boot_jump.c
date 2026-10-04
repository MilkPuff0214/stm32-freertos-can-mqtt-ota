#include "boot_jump.h"

/* ARM AAPCS：两个 uint32_t 参数分别位于 r0、r1。
 * naked 禁止生成函数入栈/出栈；函数体只能是 basic asm。
 * unused 只消除 C 参数未被引用的警告，参数仍按 ABI 传入。 */
__attribute__((naked, noreturn))    
void BootJump_Transfer(uint32_t ulStackPointer __attribute__((unused)),
                       uint32_t ulResetHandler __attribute__((unused)))
{
    __asm__ volatile(
        "msr msp, r0\n\t"
        "bx r1\n\t"
    );
}