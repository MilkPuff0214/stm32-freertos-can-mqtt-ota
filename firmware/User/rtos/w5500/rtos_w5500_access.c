#include "rtos_w5500_access.h"

#include <stddef.h>

/*
 * 当前SPI2为2.25 MHz，TCP单次提交上限为1024字节。
 * 10 ms是Mutex竞争等待上限，不是SPI或TCP超时。
 */
#define RTOS_W5500_ACCESS_LOCK_TIMEOUT_MS    10U

static TickType_t prvMillisecondsToTicks(uint32_t ulMilliseconds)
{
    TickType_t xTicks = pdMS_TO_TICKS(ulMilliseconds);

    if (xTicks == 0U)
    {
        xTicks = 1U;
    }

    return xTicks;
}

bool RtosW5500Access_Init(RtosW5500Access_t *pxAccess)
{
    if ((pxAccess == NULL) || (pxAccess->xMutex != NULL))
    {
        return false;
    }

    pxAccess->xMutex = xSemaphoreCreateMutex();  // 创建普通Mutex，用于保护对W5500芯片的访问。
    return pxAccess->xMutex != NULL;
}

bool RtosW5500Access_IsReady(const RtosW5500Access_t *pxAccess)
{
    return (pxAccess != NULL) && (pxAccess->xMutex != NULL);
}

bool RtosW5500Access_Lock(RtosW5500Access_t *pxAccess)
{
    if (RtosW5500Access_IsReady(pxAccess) == false)
    {
        return false;
    }

    return xSemaphoreTake(
               pxAccess->xMutex,
               prvMillisecondsToTicks(
                   RTOS_W5500_ACCESS_LOCK_TIMEOUT_MS)) == pdTRUE;   // 尝试获取Mutex，超时返回false。
}

void RtosW5500Access_Unlock(RtosW5500Access_t *pxAccess)
{
    BaseType_t xGiveResult;

    configASSERT(RtosW5500Access_IsReady(pxAccess));
    if (RtosW5500Access_IsReady(pxAccess) == false)
    {
        return;
    }

    xGiveResult = xSemaphoreGive(pxAccess->xMutex);  // 释放Mutex，返回值应为pdTRUE。
    configASSERT(xGiveResult == pdTRUE);
    (void)xGiveResult;
}