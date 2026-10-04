#include "app_storage_crc32.h"

#include <stddef.h>

#define APP_STORAGE_CRC32_INITIAL_VALUE    0xFFFFFFFFUL
#define APP_STORAGE_CRC32_POLYNOMIAL       0xEDB88320UL

uint32_t AppStorageCrc32_Calculate(const uint8_t *pucData, size_t xLength)
{
    uint32_t ulCrc = APP_STORAGE_CRC32_INITIAL_VALUE;
    uint32_t ulBit;
    size_t xIndex;

    if ((pucData == NULL) && (xLength > 0U))
    {
        return 0U;
    }

    for (xIndex = 0U; xIndex < xLength; xIndex++)
    {
        ulCrc ^= (uint32_t)pucData[xIndex];
        for (ulBit = 0U; ulBit < 8U; ulBit++)
        {
            if ((ulCrc & 1UL) != 0U)
            {
                ulCrc = (ulCrc >> 1U) ^ APP_STORAGE_CRC32_POLYNOMIAL;
            }
            else
            {
                ulCrc >>= 1U;
            }
        }
    }

    return ulCrc ^ APP_STORAGE_CRC32_INITIAL_VALUE;
}