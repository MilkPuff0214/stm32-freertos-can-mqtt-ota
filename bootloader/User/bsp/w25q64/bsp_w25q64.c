#include "bsp_w25q64.h"

#include <stddef.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"

#define BOOT_W25_COMMAND_JEDEC           0x9FU
#define BOOT_W25_COMMAND_READ            0x03U
#define BOOT_W25_COMMAND_WRITE_ENABLE    0x06U
#define BOOT_W25_COMMAND_STATUS1         0x05U
#define BOOT_W25_COMMAND_STATUS2         0x35U
#define BOOT_W25_COMMAND_PROGRAM         0x02U
#define BOOT_W25_COMMAND_ERASE_SECTOR    0x20U
#define BOOT_W25_STATUS_BUSY             0x01U
#define BOOT_W25_STATUS_WEL              0x02U
#define BOOT_W25_STATUS_PROTECTION       0x7CU
#define BOOT_W25_STATUS_COMPLEMENT       0x40U
#define BOOT_W25_STATUS_SUSPENDED        0x80U
#define BOOT_W25_SPI_WAIT_LIMIT          100000U

static bool prvWaitSpi(uint16_t usFlag, FlagStatus xExpected)
{
    uint32_t ulRemaining = BOOT_W25_SPI_WAIT_LIMIT;

    while (SPI_I2S_GetFlagStatus(SPI1, usFlag) != xExpected)
    {
        if (ulRemaining == 0U)
        {
            return false;
        }
        ulRemaining--;
    }
    return true;
}

/* 每发送一个字节也取走接收字节，避免全双工 SPI 的 RXNE/OVR 残留。
 * pucReceived == NULL 表示本次接收内容无用，不表示停止提供 SPI 时钟。 */
static bool prvTransferByte(uint8_t ucSend, uint8_t *pucReceived)
{
    uint8_t ucReceived;

    if (prvWaitSpi(SPI_I2S_FLAG_TXE, SET) == false)
    {
        return false;
    }
    SPI_I2S_SendData(SPI1, ucSend);
    if (prvWaitSpi(SPI_I2S_FLAG_RXNE, SET) == false)
    {
        return false;
    }
    ucReceived = (uint8_t)SPI_I2S_ReceiveData(SPI1);
    if (pucReceived != NULL)
    {
        *pucReceived = ucReceived;
    }
    return prvWaitSpi(SPI_I2S_FLAG_BSY, RESET);
}

static bool prvSendAddress(uint32_t ulAddress)
{
    return prvTransferByte((uint8_t)(ulAddress >> 16U), NULL) &&
           prvTransferByte((uint8_t)(ulAddress >> 8U), NULL) &&
           prvTransferByte((uint8_t)ulAddress, NULL);
}

static bool prvReadStatus(uint8_t ucCommand, uint8_t *pucStatus)
{
    bool xOk;

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(ucCommand, NULL) && prvTransferByte(0xFFU, pucStatus);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    return xOk;
}

/* 检查物理写保护和 WEL，不改写状态寄存器，不自行解除任何保护。
 * 本工程不使用 Suspend；发现暂停操作时也拒绝发新擦写命令。 */
static BspW25Q64OperationResult_t prvPrepareWrite(void)
{
    uint8_t ucStatus1;
    uint8_t ucStatus2;
    bool xOk;

    if (prvReadStatus(BOOT_W25_COMMAND_STATUS1, &ucStatus1) == false)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    if ((ucStatus1 & BOOT_W25_STATUS_BUSY) != 0U)
    {
        return BSP_W25Q64_OPERATION_BUSY;
    }
    if (prvReadStatus(BOOT_W25_COMMAND_STATUS2, &ucStatus2) == false)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    if (((ucStatus1 & BOOT_W25_STATUS_PROTECTION) != 0U) ||
        ((ucStatus2 & (BOOT_W25_STATUS_COMPLEMENT | BOOT_W25_STATUS_SUSPENDED)) != 0U))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(BOOT_W25_COMMAND_WRITE_ENABLE, NULL);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    if ((xOk == false) || (prvReadStatus(BOOT_W25_COMMAND_STATUS1, &ucStatus1) == false))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    if ((ucStatus1 & BOOT_W25_STATUS_WEL) == 0U)
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    return BSP_W25Q64_OPERATION_STARTED;
}

void BspW25Q64_InterfaceInit(void)
{
    GPIO_InitTypeDef xGpio;
    SPI_InitTypeDef xSpi;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    /* PA4 的 /CS 低有效；先写高电平，再启用输出，避免初始化误选芯片。 */
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    GPIO_StructInit(&xGpio);
    xGpio.GPIO_Pin = GPIO_Pin_4;
    xGpio.GPIO_Mode = GPIO_Mode_Out_PP;
    xGpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &xGpio);

    xGpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    xGpio.GPIO_Mode = GPIO_Mode_AF_PP;
    xGpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &xGpio);
    xGpio.GPIO_Pin = GPIO_Pin_6;
    xGpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &xGpio);

    SPI_I2S_DeInit(SPI1);
    SPI_StructInit(&xSpi);
    xSpi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    xSpi.SPI_Mode = SPI_Mode_Master;
    xSpi.SPI_DataSize = SPI_DataSize_8b;
    xSpi.SPI_CPOL = SPI_CPOL_Low;
    xSpi.SPI_CPHA = SPI_CPHA_1Edge;
    xSpi.SPI_NSS = SPI_NSS_Soft;
    xSpi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    xSpi.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &xSpi);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI1, ENABLE);
}

void BspW25Q64_InterfaceDeInit(void)
{
    /* /CS低有效；结束芯片选择后关闭SPI，GPIOA仍保留供应用重新初始化。 */
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    SPI_Cmd(SPI1, DISABLE);
    SPI_I2S_DeInit(SPI1);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, DISABLE);
}

bool BspW25Q64_ReadJedecId(BspW25Q64JedecId_t *pxJedecId)
{
    bool xOk;

    if (pxJedecId == NULL)
    {
        return false;
    }
    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(BOOT_W25_COMMAND_JEDEC, NULL) &&
          prvTransferByte(0xFFU, &pxJedecId->ucManufacturer) &&
          prvTransferByte(0xFFU, &pxJedecId->ucMemoryType) &&
          prvTransferByte(0xFFU, &pxJedecId->ucCapacity);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    return xOk;
}

bool BspW25Q64_Read(uint32_t ulAddress, uint8_t *pucData, uint16_t usLength)
{
    uint16_t usIndex;
    bool xOk;

    if ((pucData == NULL) || (usLength == 0U) ||
        (ulAddress >= BSP_W25Q64_CAPACITY_BYTES) ||
        (usLength > (BSP_W25Q64_CAPACITY_BYTES - ulAddress)))
    {
        return false;
    }
    if (BspW25Q64_PollReady() != BSP_W25Q64_POLL_READY)
    {
        return false;
    }

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(BOOT_W25_COMMAND_READ, NULL) && prvSendAddress(ulAddress);
    for (usIndex = 0U; (usIndex < usLength) && xOk; usIndex++)
    {
        xOk = prvTransferByte(0xFFU, &pucData[usIndex]);
    }
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    return xOk;
}

BspW25Q64OperationResult_t BspW25Q64_SectorEraseStart(uint32_t ulAddress)
{
    BspW25Q64OperationResult_t xPrepared;
    bool xOk;

    if ((ulAddress >= BSP_W25Q64_CAPACITY_BYTES) ||
        ((ulAddress % BSP_W25Q64_SECTOR_SIZE_BYTES) != 0U))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    xPrepared = prvPrepareWrite();
    if (xPrepared != BSP_W25Q64_OPERATION_STARTED)
    {
        return xPrepared;
    }

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(BOOT_W25_COMMAND_ERASE_SECTOR, NULL) && prvSendAddress(ulAddress);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    return xOk ? BSP_W25Q64_OPERATION_STARTED : BSP_W25Q64_OPERATION_ERROR;
}

BspW25Q64OperationResult_t BspW25Q64_PageProgramStart(uint32_t ulAddress,
    const uint8_t *pucData, uint16_t usLength)
{
    BspW25Q64OperationResult_t xPrepared;
    uint16_t usIndex;
    bool xOk;

    if ((pucData == NULL) || (usLength == 0U) || (usLength > BSP_W25Q64_PAGE_SIZE_BYTES) ||
        (ulAddress >= BSP_W25Q64_CAPACITY_BYTES) ||
        (usLength > (BSP_W25Q64_CAPACITY_BYTES - ulAddress)) ||
        (usLength > (BSP_W25Q64_PAGE_SIZE_BYTES - (ulAddress % BSP_W25Q64_PAGE_SIZE_BYTES))))
    {
        return BSP_W25Q64_OPERATION_ERROR;
    }
    xPrepared = prvPrepareWrite();
    if (xPrepared != BSP_W25Q64_OPERATION_STARTED)
    {
        return xPrepared;
    }

    GPIO_ResetBits(GPIOA, GPIO_Pin_4);
    xOk = prvTransferByte(BOOT_W25_COMMAND_PROGRAM, NULL) && prvSendAddress(ulAddress);
    for (usIndex = 0U; (usIndex < usLength) && xOk; usIndex++)
    {
        xOk = prvTransferByte(pucData[usIndex], NULL);
    }
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
    return xOk ? BSP_W25Q64_OPERATION_STARTED : BSP_W25Q64_OPERATION_ERROR;
}

BspW25Q64PollResult_t BspW25Q64_PollReady(void)
{
    uint8_t ucStatus;

    if (prvReadStatus(BOOT_W25_COMMAND_STATUS1, &ucStatus) == false)
    {
        return BSP_W25Q64_POLL_ERROR;
    }
    return ((ucStatus & BOOT_W25_STATUS_BUSY) != 0U) ?
           BSP_W25Q64_POLL_BUSY : BSP_W25Q64_POLL_READY;
}
