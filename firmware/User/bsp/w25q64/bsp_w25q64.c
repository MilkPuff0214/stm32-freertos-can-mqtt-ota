#include "bsp_w25q64.h"

#include <stddef.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"

#define BSP_W25Q64_SPI                         SPI1
#define BSP_W25Q64_GPIO_PORT                   GPIOA
#define BSP_W25Q64_CHIP_SELECT_PIN             GPIO_Pin_4
#define BSP_W25Q64_CLOCK_PIN                   GPIO_Pin_5
#define BSP_W25Q64_MISO_PIN                    GPIO_Pin_6
#define BSP_W25Q64_MOSI_PIN                    GPIO_Pin_7

#define BSP_W25Q64_COMMAND_WRITE_ENABLE        0x06U
#define BSP_W25Q64_COMMAND_READ_STATUS_1       0x05U
#define BSP_W25Q64_COMMAND_READ_STATUS_2       0x35U
#define BSP_W25Q64_COMMAND_READ_DATA           0x03U
#define BSP_W25Q64_COMMAND_PAGE_PROGRAM        0x02U
#define BSP_W25Q64_COMMAND_SECTOR_ERASE        0x20U
#define BSP_W25Q64_COMMAND_JEDEC_ID            0x9FU

#define BSP_W25Q64_STATUS_1_BUSY_BIT           0x01U
#define BSP_W25Q64_STATUS_1_WRITE_ENABLE_BIT   0x02U
#define BSP_W25Q64_STATUS_1_PROTECTION_MASK    0x7CU
#define BSP_W25Q64_STATUS_2_COMPLEMENT_BIT     0x40U

#define BSP_W25Q64_SPI_DUMMY_BYTE              0xFFU
#define BSP_W25Q64_SPI_TIMEOUT_COUNT           100000U

static void prvChipSelect(void)
{
    GPIO_ResetBits(BSP_W25Q64_GPIO_PORT, BSP_W25Q64_CHIP_SELECT_PIN);
}

static void prvChipDeselect(void)
{
    GPIO_SetBits(BSP_W25Q64_GPIO_PORT, BSP_W25Q64_CHIP_SELECT_PIN);
}

static bool prvWaitForSpiFlag(uint16_t usFlag, FlagStatus xExpectedStatus)
{
    uint32_t ulTimeoutCount = BSP_W25Q64_SPI_TIMEOUT_COUNT;

    while (SPI_I2S_GetFlagStatus(BSP_W25Q64_SPI, usFlag) != xExpectedStatus)
    {
        if (ulTimeoutCount == 0U)
        {
            return false;
        }
        ulTimeoutCount--;
    }

    return true;
}

static bool prvSpiTransferByte(uint8_t ucTransmitData, uint8_t *pucReceivedData)
{
    if (pucReceivedData == NULL)
    {
        return false;
    }

    if (prvWaitForSpiFlag(SPI_I2S_FLAG_TXE, SET) == false)
    {
        return false;
    }
    SPI_I2S_SendData(BSP_W25Q64_SPI, ucTransmitData);

    if (prvWaitForSpiFlag(SPI_I2S_FLAG_RXNE, SET) == false)
    {
        return false;
    }
    *pucReceivedData = (uint8_t)SPI_I2S_ReceiveData(BSP_W25Q64_SPI);

    return prvWaitForSpiFlag(SPI_I2S_FLAG_BSY, RESET);
}

static bool prvSendByte(uint8_t ucValue)
{
    uint8_t ucDiscard;

    return prvSpiTransferByte(ucValue, &ucDiscard);
}

static bool prvReceiveByte(uint8_t *pucValue)
{
    return prvSpiTransferByte(BSP_W25Q64_SPI_DUMMY_BYTE, pucValue);
}

static bool prvSendAddress(uint32_t ulAddress)
{
    return prvSendByte((uint8_t)(ulAddress >> 16U)) &&
           prvSendByte((uint8_t)(ulAddress >> 8U)) &&
           prvSendByte((uint8_t)ulAddress);
}

static bool prvAddressRangeIsValid(uint32_t ulAddress, uint32_t ulLength)
{
    if ((ulLength == 0U) || (ulAddress >= BSP_W25Q64_CAPACITY_BYTES))
    {
        return false;
    }

    return ulLength <= (BSP_W25Q64_CAPACITY_BYTES - ulAddress);
}

static bool prvReadStatusRegister(uint8_t ucCommand, uint8_t *pucStatus)
{
    bool xResult;

    if (pucStatus == NULL)
    {
        return false;
    }

    prvChipSelect();
    xResult = prvSendByte(ucCommand) && prvReceiveByte(pucStatus);
    prvChipDeselect();

    return xResult;
}

static BspW25Q64OperationResult_t prvPrepareWrite(void)
{
    uint8_t ucStatus1;
    uint8_t ucStatus2;

    if ((prvReadStatusRegister(BSP_W25Q64_COMMAND_READ_STATUS_1, &ucStatus1) == false) ||
        (prvReadStatusRegister(BSP_W25Q64_COMMAND_READ_STATUS_2, &ucStatus2) == false))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    if ((ucStatus1 & BSP_W25Q64_STATUS_1_BUSY_BIT) != 0U)
    {
        return BSP_W25Q64_OPERATION_BUSY;
    }

    if (((ucStatus1 & BSP_W25Q64_STATUS_1_PROTECTION_MASK) != 0U) ||
        ((ucStatus2 & BSP_W25Q64_STATUS_2_COMPLEMENT_BIT) != 0U))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    prvChipSelect();
    if (prvSendByte(BSP_W25Q64_COMMAND_WRITE_ENABLE) == false)
    {
        prvChipDeselect();
        return BSP_W25Q64_OPERATION_ERROR;
    }
    prvChipDeselect();

    if (prvReadStatusRegister(BSP_W25Q64_COMMAND_READ_STATUS_1, &ucStatus1) == false)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    if ((ucStatus1 & BSP_W25Q64_STATUS_1_WRITE_ENABLE_BIT) == 0U)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    return BSP_W25Q64_OPERATION_STARTED;
}

void BspW25Q64_InterfaceInit(void)
{
    GPIO_InitTypeDef xGpioInit;
    SPI_InitTypeDef xSpiInit;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);
    GPIO_SetBits(BSP_W25Q64_GPIO_PORT, BSP_W25Q64_CHIP_SELECT_PIN);

    GPIO_StructInit(&xGpioInit);
    xGpioInit.GPIO_Pin = BSP_W25Q64_CHIP_SELECT_PIN;
    xGpioInit.GPIO_Speed = GPIO_Speed_2MHz;
    xGpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BSP_W25Q64_GPIO_PORT, &xGpioInit);

    xGpioInit.GPIO_Pin = BSP_W25Q64_CLOCK_PIN | BSP_W25Q64_MOSI_PIN;
    xGpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    xGpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(BSP_W25Q64_GPIO_PORT, &xGpioInit);

    xGpioInit.GPIO_Pin = BSP_W25Q64_MISO_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BSP_W25Q64_GPIO_PORT, &xGpioInit);

    SPI_I2S_DeInit(BSP_W25Q64_SPI);
    SPI_StructInit(&xSpiInit);
    xSpiInit.SPI_Direction = SPI_Direction_2Lines_FullDuplex;   // 全双工模式
    xSpiInit.SPI_Mode = SPI_Mode_Master;   // 主模式
    xSpiInit.SPI_DataSize = SPI_DataSize_8b;   // 8位数据帧
    xSpiInit.SPI_CPOL = SPI_CPOL_Low;   // 时钟空闲状态为低电平
    xSpiInit.SPI_CPHA = SPI_CPHA_1Edge;   // 数据捕获于第1个时钟沿
    xSpiInit.SPI_NSS = SPI_NSS_Soft;   // 软件管理NSS信号
    xSpiInit.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;   // 波特率预分频
    xSpiInit.SPI_FirstBit = SPI_FirstBit_MSB;   // 数据传输从MSB开始
    xSpiInit.SPI_CRCPolynomial = 7U;   // CRC多项式
    SPI_Init(BSP_W25Q64_SPI, &xSpiInit);
    SPI_NSSInternalSoftwareConfig(BSP_W25Q64_SPI, SPI_NSSInternalSoft_Set);   // 配置内部NSS信号为软件管理
    SPI_Cmd(BSP_W25Q64_SPI, ENABLE);
}

bool BspW25Q64_ReadJedecId(BspW25Q64JedecId_t *pxJedecId)
{
    bool xResult;

    if (pxJedecId == NULL)
    {
        return false;
    }

    prvChipSelect();
    xResult = prvSendByte(BSP_W25Q64_COMMAND_JEDEC_ID) &&
              prvReceiveByte(&pxJedecId->ucManufacturer) &&
              prvReceiveByte(&pxJedecId->ucMemoryType) &&
              prvReceiveByte(&pxJedecId->ucCapacity);
    prvChipDeselect();

    return xResult;
}

bool BspW25Q64_Read(uint32_t ulAddress, uint8_t *pucData, uint16_t usLength)
{
    uint16_t usIndex;
    uint8_t ucStatus1;

    if ((pucData == NULL) ||
        (prvAddressRangeIsValid(ulAddress, (uint32_t)usLength) == false))
    {
        return false;
    }

    if ((prvReadStatusRegister(BSP_W25Q64_COMMAND_READ_STATUS_1, &ucStatus1) == false) ||
        ((ucStatus1 & BSP_W25Q64_STATUS_1_BUSY_BIT) != 0U))
    {
        return false;
    }

    prvChipSelect();
    if ((prvSendByte(BSP_W25Q64_COMMAND_READ_DATA) == false) ||
        (prvSendAddress(ulAddress) == false))
    {
        prvChipDeselect();
        return false;
    }

    for (usIndex = 0U; usIndex < usLength; usIndex++)
    {
        if (prvReceiveByte(&pucData[usIndex]) == false)
        {
            prvChipDeselect();
            return false;
        }
    }

    prvChipDeselect();
    return true;
}

BspW25Q64OperationResult_t BspW25Q64_SectorEraseStart(uint32_t ulAddress)
{
    BspW25Q64OperationResult_t xPrepareResult;

    if (((ulAddress % BSP_W25Q64_SECTOR_SIZE_BYTES) != 0U) ||
        (prvAddressRangeIsValid(ulAddress, BSP_W25Q64_SECTOR_SIZE_BYTES) == false))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    xPrepareResult = prvPrepareWrite();
    if (xPrepareResult != BSP_W25Q64_OPERATION_STARTED)
    {
        return xPrepareResult;
    }

    prvChipSelect();
    if ((prvSendByte(BSP_W25Q64_COMMAND_SECTOR_ERASE) == false) ||
        (prvSendAddress(ulAddress) == false))
    {
        prvChipDeselect();
        return BSP_W25Q64_OPERATION_ERROR;
    }
    prvChipDeselect();

    return BSP_W25Q64_OPERATION_STARTED;
}

BspW25Q64OperationResult_t BspW25Q64_PageProgramStart(
    uint32_t ulAddress,
    const uint8_t *pucData,
    uint16_t usLength)
{
    BspW25Q64OperationResult_t xPrepareResult;
    uint16_t usIndex;
    uint32_t ulPageOffset;

    if ((pucData == NULL) ||
        (usLength == 0U) ||
        (usLength > BSP_W25Q64_PAGE_SIZE_BYTES) ||
        (prvAddressRangeIsValid(ulAddress, (uint32_t)usLength) == false))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    ulPageOffset = ulAddress % BSP_W25Q64_PAGE_SIZE_BYTES;
    if ((ulPageOffset + usLength) > BSP_W25Q64_PAGE_SIZE_BYTES)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    xPrepareResult = prvPrepareWrite();
    if (xPrepareResult != BSP_W25Q64_OPERATION_STARTED)
    {
        return xPrepareResult;
    }

    prvChipSelect();
    if ((prvSendByte(BSP_W25Q64_COMMAND_PAGE_PROGRAM) == false) ||
        (prvSendAddress(ulAddress) == false))
    {
        prvChipDeselect();
        return BSP_W25Q64_OPERATION_ERROR;
    }

    for (usIndex = 0U; usIndex < usLength; usIndex++)
    {
        if (prvSendByte(pucData[usIndex]) == false)
        {
            prvChipDeselect();
            return BSP_W25Q64_OPERATION_ERROR;
        }
    }

    prvChipDeselect();
    return BSP_W25Q64_OPERATION_STARTED;
}

BspW25Q64PollResult_t BspW25Q64_PollReady(void)
{
    uint8_t ucStatus1;

    if (prvReadStatusRegister(BSP_W25Q64_COMMAND_READ_STATUS_1, &ucStatus1) == false)
    {
        return BSP_W25Q64_POLL_ERROR;
    }

    if ((ucStatus1 & BSP_W25Q64_STATUS_1_BUSY_BIT) != 0U)
    {
        return BSP_W25Q64_POLL_BUSY;
    }

    return BSP_W25Q64_POLL_READY;
}