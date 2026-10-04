#include "bsp_internal_flash.h"

#include <stddef.h>

#include "boot_config.h"
#include "stm32f10x_flash.h"

/* 仅适用于已经冻结的 STM32F103ZET6 高密度分区，不是通用 F1 Flash 驱动。 */
#define BOOT_INTERNAL_PAGE_SIZE    2048U
#define BOOT_INTERNAL_APP_WRP_MASK 0xFFFF0000UL

_Static_assert(BOOT_APPLICATION_START == 0x08010000UL, "review WRP mask after partition change");
_Static_assert(BOOT_APPLICATION_SIZE == 0x70000UL, "review application range after change");

bool BspInternalFlash_Unlock(void)
{
    /* F1 内部擦写需要 HSI 开启；正常 SystemInit 保持 HSI 可用。
     * 若时钟或保护前提不满足，只报失败，不在 BSP 内改变系统策略。 */
    if (((RCC->CR & RCC_CR_HSIRDY) == 0U) ||
        ((FLASH_GetWriteProtectionOptionByte() & BOOT_INTERNAL_APP_WRP_MASK) !=
         BOOT_INTERNAL_APP_WRP_MASK))
    {
        return false;
    }

    FLASH_Unlock();
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return false;
    }
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    return true;
}

void BspInternalFlash_Lock(void)
{
    FLASH_Lock();
}

bool BspInternalFlash_EraseApplication(void)
{
    uint32_t ulPageOffset;
    uint32_t ulHalfwordOffset;
    uint32_t ulAddress;

    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return false;
    }

    /* 只遍历应用区 224 页；前 32 页 Boot 不在任何擦除地址范围内。 */
    for (ulPageOffset = 0U; ulPageOffset < BOOT_APPLICATION_SIZE;
         ulPageOffset += BOOT_INTERNAL_PAGE_SIZE)
    {
        ulAddress = BOOT_APPLICATION_START + ulPageOffset;
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
        if (FLASH_ErasePage(ulAddress) != FLASH_COMPLETE)
        {
            return false;
        }
        for (ulHalfwordOffset = 0U; ulHalfwordOffset < BOOT_INTERNAL_PAGE_SIZE;
             ulHalfwordOffset += 2U)
        {
            if (*(const volatile uint16_t *)(uintptr_t)(ulAddress + ulHalfwordOffset) != 0xFFFFU)
            {
                return false;
            }
        }
    }
    return true;
}

bool BspInternalFlash_ProgramBlock(uint32_t ulOffset, const uint8_t *pucData, uint16_t usLength)
{
    uint16_t usIndex;
    uint16_t usValue;
    uint32_t ulAddress;

    if ((pucData == NULL) || (usLength == 0U) || (usLength > 512U) ||
        ((ulOffset & 1U) != 0U) || (ulOffset >= BOOT_APPLICATION_SIZE) ||
        (usLength > (BOOT_APPLICATION_SIZE - ulOffset)) ||
        ((FLASH->CR & FLASH_CR_LOCK) != 0U))
    {
        return false;
    }

    /* Controller 顺序调用，只有最后一块可以是奇数长度；普通块必须是偶数。
     * 最后一字节所在半字的高字节填 FF，不把它计入文件长度或 MD5。 */
    for (usIndex = 0U; usIndex < usLength; usIndex += 2U)
    {
        usValue = pucData[usIndex];
        usValue |= ((usIndex + 1U) < usLength) ?
                   (uint16_t)((uint16_t)pucData[usIndex + 1U] << 8U) : 0xFF00U;
        ulAddress = BOOT_APPLICATION_START + ulOffset + usIndex;
        FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
        if (FLASH_ProgramHalfWord(ulAddress, usValue) != FLASH_COMPLETE)
        {
            return false;
        }
    }

    if (((usLength & 1U) != 0U) &&
        (*(const volatile uint8_t *)(uintptr_t)
         (BOOT_APPLICATION_START + ulOffset + usLength) != 0xFFU))
    {
        return false;
    }
    return true;
}

bool BspInternalFlash_Compare(uint32_t ulOffset, const uint8_t *pucData, uint16_t usLength)
{
    uint16_t usIndex;
    const volatile uint8_t *pucFlash;

    if ((pucData == NULL) || (usLength == 0U) || (usLength > 512U) ||
        (ulOffset >= BOOT_APPLICATION_SIZE) ||
        (usLength > (BOOT_APPLICATION_SIZE - ulOffset)))
    {
        return false;
    }
    pucFlash = (const volatile uint8_t *)(uintptr_t)(BOOT_APPLICATION_START + ulOffset);
    for (usIndex = 0U; usIndex < usLength; usIndex++)
    {
        if (pucFlash[usIndex] != pucData[usIndex])
        {
            return false;
        }
    }
    return true;
}
