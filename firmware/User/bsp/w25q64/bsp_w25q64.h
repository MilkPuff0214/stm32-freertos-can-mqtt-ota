#ifndef BSP_W25Q64_H
#define BSP_W25Q64_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_W25Q64_CAPACITY_BYTES          0x00800000UL     //芯片容量：8 MB
#define BSP_W25Q64_PAGE_SIZE_BYTES         256U           //页大小：256 字节
#define BSP_W25Q64_SECTOR_SIZE_BYTES       4096U          //扇区大小：4 KB

#define BSP_W25Q64_MANUFACTURER_ID         0xEFU
#define BSP_W25Q64_MEMORY_TYPE             0x40U
#define BSP_W25Q64_CAPACITY_ID             0x17U

typedef struct
{
    uint8_t ucManufacturer;
    uint8_t ucMemoryType;
    uint8_t ucCapacity;
} BspW25Q64JedecId_t;

typedef enum
{
    BSP_W25Q64_OPERATION_ERROR = -1,
    BSP_W25Q64_OPERATION_BUSY = 0,
    BSP_W25Q64_OPERATION_STARTED = 1    /* 写使能和擦除/编程命令已经成功发送 */
} BspW25Q64OperationResult_t;       /* 这次擦除或编程命令是否成功启动？ */

typedef enum
{
    BSP_W25Q64_POLL_ERROR = -1,
    BSP_W25Q64_POLL_BUSY = 0,
    BSP_W25Q64_POLL_READY = 1
} BspW25Q64PollResult_t;    /* 已启动的命令现在完成了吗？ */

void BspW25Q64_InterfaceInit(void);
bool BspW25Q64_ReadJedecId(BspW25Q64JedecId_t *pxJedecId);
bool BspW25Q64_Read(uint32_t ulAddress, uint8_t *pucData, uint16_t usLength);
BspW25Q64OperationResult_t BspW25Q64_SectorEraseStart(uint32_t ulAddress);
BspW25Q64OperationResult_t BspW25Q64_PageProgramStart(
    uint32_t ulAddress,
    const uint8_t *pucData,
    uint16_t usLength);
BspW25Q64PollResult_t BspW25Q64_PollReady(void);

#endif /* BSP_W25Q64_H */