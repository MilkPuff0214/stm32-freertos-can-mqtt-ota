#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    APP_CONSOLE_OUTPUT_NONE = 0,
    APP_CONSOLE_OUTPUT_TEXT,
    APP_CONSOLE_OUTPUT_RTOS_TASK_LIST,
    APP_CONSOLE_OUTPUT_RTOS_HEAP,
    APP_CONSOLE_OUTPUT_ADC_DATA
} AppConsoleOutputType_t;

typedef struct
{
    /*
     * TEXT表示pucData和usLength有效；
     * 其他类型表示调用者需要生成对应的动态报告。
     */
    AppConsoleOutputType_t eType;
    const uint8_t *pucData;
    uint16_t usLength;
} AppConsoleOutput_t;

/**
 * @brief 初始化Console行缓冲和协议状态。
 *
 * @note 由Serial RX Task在开始接收前调用一次。
 */
void AppConsole_Init(void);

/**
 * @brief 向Console状态机提交一个接收字节。
 * @param ucByte 接收到的字节。
 * @param pOutput 命令处理结果，必须指向有效内存。
 * @return true表示完成一条命令并产生输出请求；
 *         false表示一行尚未完成或当前无需输出。
 *
 * @note TEXT输出指针由Console持有，调用者应立即复制其内容。
 */
bool AppConsole_ProcessByte(
    uint8_t ucByte,
    AppConsoleOutput_t *pOutput);

#endif /* APP_CONSOLE_H */
