#ifndef BSP_W25Q64_H
#define BSP_W25Q64_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_W25Q64_CAPACITY_BYTES     0x00800000UL
#define BSP_W25Q64_PAGE_SIZE_BYTES    256U
#define BSP_W25Q64_SECTOR_SIZE_BYTES  4096U

typedef struct
{
    uint8_t ucManufacturer;
    uint8_t ucMemoryType;
    uint8_t ucCapacity;
} BspW25Q64JedecId_t;

/* Start仅表示命令已发出；Ready由之后独立的状态读取确认。 */
typedef enum
{
    BSP_W25Q64_OPERATION_ERROR = -1,
    BSP_W25Q64_OPERATION_BUSY = 0,
    BSP_W25Q64_OPERATION_STARTED = 1
} BspW25Q64OperationResult_t;

typedef enum
{
    BSP_W25Q64_POLL_ERROR = -1,
    BSP_W25Q64_POLL_BUSY = 0,
    BSP_W25Q64_POLL_READY = 1
} BspW25Q64PollResult_t;

void BspW25Q64_InterfaceInit(void);
bool BspW25Q64_ReadJedecId(BspW25Q64JedecId_t *pxJedecId);
bool BspW25Q64_Read(uint32_t ulAddress, uint8_t *pucData, uint16_t usLength);
BspW25Q64OperationResult_t BspW25Q64_SectorEraseStart(uint32_t ulAddress);
BspW25Q64OperationResult_t BspW25Q64_PageProgramStart(uint32_t ulAddress,
    const uint8_t *pucData, uint16_t usLength);
BspW25Q64PollResult_t BspW25Q64_PollReady(void);

/* 仅在全部同步SPI操作结束后、跳转应用前调用；/CS保持高电平。 */
void BspW25Q64_InterfaceDeInit(void);

#endif /* BSP_W25Q64_H */
