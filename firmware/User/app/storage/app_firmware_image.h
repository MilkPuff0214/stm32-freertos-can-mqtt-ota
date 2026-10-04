#ifndef APP_FIRMWARE_IMAGE_H
#define APP_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_storage_layout.h"

#define APP_FIRMWARE_HARDWARE_MODEL_SIZE    16U
#define APP_FIRMWARE_SOFTWARE_MODEL_SIZE    16U

typedef struct
{
    char acHardwareModel[APP_FIRMWARE_HARDWARE_MODEL_SIZE];
    char acSoftwareModel[APP_FIRMWARE_SOFTWARE_MODEL_SIZE];
    uint32_t ulFirmwareLength;
} AppFirmwareImageInfo_t;

bool AppFirmwareImage_ParseHeader(
    const uint8_t aucHeader[APP_STORAGE_FIRMWARE_HEADER_SIZE],
    uint32_t ulFirmwareLength,
    AppFirmwareImageInfo_t *pxInfo);

#endif /* APP_FIRMWARE_IMAGE_H */
