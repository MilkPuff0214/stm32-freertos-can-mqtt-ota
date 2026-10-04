#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

/* 仅供裸机 Controller 在验证向量、屏蔽中断及清理外设后调用。
 * Reset 地址保留 Thumb bit0；成功交接后不返回。 */
void BootJump_Transfer(uint32_t ulStackPointer, uint32_t ulResetHandler)
    __attribute__((noreturn));

#endif /* BOOT_JUMP_H */