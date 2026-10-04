#ifndef APP_STORAGE_CRC32_H
#define APP_STORAGE_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t AppStorageCrc32_Calculate(const uint8_t *pucData, size_t xLength);

#endif /* APP_STORAGE_CRC32_H */