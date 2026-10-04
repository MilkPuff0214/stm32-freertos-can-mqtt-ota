#ifndef BSP_INTERNAL_FLASH_H
#define BSP_INTERNAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>

bool BspInternalFlash_Unlock(void);
void BspInternalFlash_Lock(void);
bool BspInternalFlash_EraseApplication(void);
bool BspInternalFlash_ProgramBlock(uint32_t ulOffset, const uint8_t *pucData, uint16_t usLength);
bool BspInternalFlash_Compare(uint32_t ulOffset, const uint8_t *pucData, uint16_t usLength);

#endif /* BSP_INTERNAL_FLASH_H */
