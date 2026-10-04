#ifndef RTOS_W5500_ACCESS_H
#define RTOS_W5500_ACCESS_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"

/*
 * 一个对象对应当前板上的唯一W5500芯片。
 * 对象由AppStartup静态保存，初始化后Mutex句柄不再改变。
 */
typedef struct
{
    SemaphoreHandle_t xMutex;
} RtosW5500Access_t;

/*
 * 在调度器启动前创建普通Mutex。
 * pxAccess必须指向零初始化、且在全部网络Task存活期间始终有效的对象。
 */
bool RtosW5500Access_Init(RtosW5500Access_t *pxAccess);

bool RtosW5500Access_IsReady(const RtosW5500Access_t *pxAccess);

/*
 * 仅供Task上下文使用；最多等待固定的有界时间。
 * false表示共享芯片暂忙，不表示SPI、TCP或PHY发生错误。
 */
bool RtosW5500Access_Lock(RtosW5500Access_t *pxAccess);

/* 只能由成功Lock的同一个Task调用一次。 */
void RtosW5500Access_Unlock(RtosW5500Access_t *pxAccess);

#endif /* RTOS_W5500_ACCESS_H */