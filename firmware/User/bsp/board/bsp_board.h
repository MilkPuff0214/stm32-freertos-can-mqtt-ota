#ifndef BSP_BOARD_H
#define BSP_BOARD_H

typedef enum
{
    BSP_BOARD_INIT_STATUS_OK = 0,
    BSP_BOARD_INIT_STATUS_ADC_FAILED,
    BSP_BOARD_INIT_STATUS_CAN_FAILED
} BspBoardInitStatus_t;

/*
 * 按已验证顺序初始化调度器启动前所需的板级硬件。
 * 返回值保留具体失败外设，供main和GDB诊断。
 */
BspBoardInitStatus_t BspBoard_Init(void);

#endif /* BSP_BOARD_H */