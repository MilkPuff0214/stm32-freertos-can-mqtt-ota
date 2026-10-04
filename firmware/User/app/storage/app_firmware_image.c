#include "app_firmware_image.h"

#include <stddef.h>
#include <string.h>

#define APP_FIRMWARE_HEADER_HARDWARE_OFFSET     0x00U
#define APP_FIRMWARE_HEADER_SOFTWARE_OFFSET     0x10U
#define APP_FIRMWARE_HEADER_LENGTH_OFFSET       0x20U
#define APP_FIRMWARE_HEADER_RESERVED_OFFSET     0x24U
#define APP_FIRMWARE_HEADER_RESERVED_LENGTH       (APP_STORAGE_FIRMWARE_HEADER_SIZE - APP_FIRMWARE_HEADER_RESERVED_OFFSET)

static uint32_t prvReadLe32(const uint8_t *pucData)
{
    return (uint32_t)pucData[0] |
           ((uint32_t)pucData[1] << 8U) |
           ((uint32_t)pucData[2] << 16U) |
           ((uint32_t)pucData[3] << 24U);
}

static bool prvReadModel(const uint8_t *pucSource, uint32_t ulFieldSize,
                         char *pcDestination)
{
    uint32_t ulIndex;
    bool xTerminated = false;

    for (ulIndex = 0U; ulIndex < ulFieldSize; ulIndex++)
    {
        uint8_t ucValue = pucSource[ulIndex];

        if (xTerminated)
        {
            if (ucValue != 0U)
            {
                return false;
            }
        }
        else if (ucValue == 0U)
        {
            xTerminated = true;
        }
        else
        {
            if ((ucValue < 0x21U) || (ucValue > 0x7EU))
            {
                return false;
            }
            pcDestination[ulIndex] = (char)ucValue;
        }
    }

    return xTerminated && (pcDestination[0] != '\0');
}

static bool prvAllZero(const uint8_t *pucData, uint32_t ulLength)
{
    uint32_t ulIndex;

    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        if (pucData[ulIndex] != 0U)
        {
            return false;
        }
    }

    return true;
}

bool AppFirmwareImage_ParseHeader(
    const uint8_t aucHeader[APP_STORAGE_FIRMWARE_HEADER_SIZE],
    uint32_t ulFirmwareLength,
    AppFirmwareImageInfo_t *pxInfo)
{
    uint32_t ulHeaderFirmwareLength;

    if ((aucHeader == NULL) || (pxInfo == NULL))
    {
        return false;
    }

    memset(pxInfo, 0, sizeof(*pxInfo));
    ulHeaderFirmwareLength = prvReadLe32(
        &aucHeader[APP_FIRMWARE_HEADER_LENGTH_OFFSET]);

    if ((prvReadModel(&aucHeader[APP_FIRMWARE_HEADER_HARDWARE_OFFSET],
                      APP_FIRMWARE_HARDWARE_MODEL_SIZE,
                      pxInfo->acHardwareModel) == false) ||
        (prvReadModel(&aucHeader[APP_FIRMWARE_HEADER_SOFTWARE_OFFSET],
                      APP_FIRMWARE_SOFTWARE_MODEL_SIZE,
                      pxInfo->acSoftwareModel) == false) ||
        (ulHeaderFirmwareLength != ulFirmwareLength) ||
        (ulHeaderFirmwareLength <= APP_STORAGE_FIRMWARE_HEADER_SIZE) ||
        (ulHeaderFirmwareLength > APP_STORAGE_FIRMWARE_MAX_SIZE) ||
        (prvAllZero(&aucHeader[APP_FIRMWARE_HEADER_RESERVED_OFFSET],
                    APP_FIRMWARE_HEADER_RESERVED_LENGTH) == false))
    {
        memset(pxInfo, 0, sizeof(*pxInfo));
        return false;
    }

    pxInfo->ulFirmwareLength = ulHeaderFirmwareLength;
    return true;
}
