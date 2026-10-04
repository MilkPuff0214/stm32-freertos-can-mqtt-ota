#ifndef BSP_KEY_H
#define BSP_KEY_H

typedef enum
{
    BSP_KEY_ID_1 = 0,
    BSP_KEY_ID_2,
    BSP_KEY_ID_COUNT
} BspKeyId_t;

typedef enum
{
    BSP_KEY_RELEASED = 0,
    BSP_KEY_PRESSED = 1
} BspKeyState_t;

/* 初始化板载按键GPIO。 */
void BspKey_Init(void);

/* 非阻塞读取指定按键的当前状态。 */
BspKeyState_t BspKey_Read(BspKeyId_t keyId);

#endif /* BSP_KEY_H */