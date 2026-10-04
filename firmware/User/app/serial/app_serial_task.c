#include "app_serial_task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_usart.h"
#include "app_adc_task.h"
#include "app_console.h"
#include "app_rtos_diagnostics.h"

/* 最多缓存4条尚未发送的串口消息。 */
#define APP_SERIAL_TX_QUEUE_LENGTH    4U

#define APP_SERIAL_TX_STACK_DEPTH     256U
#define APP_SERIAL_TX_PRIORITY        (tskIDLE_PRIORITY + 1U)

typedef struct
{
    uint16_t usLength;
    uint8_t ucData[APP_SERIAL_TX_MESSAGE_MAX_LENGTH];
} AppSerialTxMessage_t;

/*
 * Queue和Task Handle都由Serial模块私有保存。
 * 其他模块只能通过AppSerial_Write()提交消息。
 */
static QueueHandle_t s_xSerialTxQueue = NULL;
static TaskHandle_t s_xSerialTxTaskHandle = NULL;

/*
 * 当前只有Serial TX Task修改该计数，不存在多Task写竞争。
 * 后续可以通过Console诊断命令读取它。
 */
static volatile uint32_t s_ulTxDmaStartFailureCount = 0U;


/*
 * 192 words在Cortex-M3上是768字节。
 * RX Task会调用AppSerial_Write()并产生额外调用栈，
 * 因此暂时比configMINIMAL_STACK_SIZE多保留一些空间。
 */
#define APP_SERIAL_RX_STACK_DEPTH    512U
#define APP_SERIAL_RX_PRIORITY       (tskIDLE_PRIORITY + 1U)
#define APP_SERIAL_DATA_REPORT_BUFFER_SIZE    96U

static TaskHandle_t s_xSerialRxTaskHandle = NULL;  //接收任务的任务句柄

/*
 * TX Queue满时不阻塞RX Task，丢弃当前Console响应并累计次数。
 * 后续可以通过诊断命令读取该计数。
 */
static volatile uint32_t s_ulConsoleResponseDropCount = 0U;
/*
 * 诊断报告生成失败与TX Queue满是两类故障，不能混为一个计数。
 */
static volatile uint32_t s_ulDiagnosticsBuildFailureCount = 0U;

static bool prvQueueAdcDataReport(void)
{
    AppAdcSample_t xSample;
    char cReport[APP_SERIAL_DATA_REPORT_BUFFER_SIZE];
    const char *pcTemperatureSign = "";
    int32_t lTemperatureCentiDegrees;
    uint32_t ulTemperatureMagnitude;
    uint32_t ulPotentiometerMillivolts;
    int iLength;

    if ((AppAdc_TryReadLatestSample(&xSample) == false) || (xSample.xValid == false))
    {
        iLength = snprintf(cReport, sizeof(cReport), "DATA: unavailable\r\n");
    }
    else
    {
        float fScaledTemperature = xSample.fCpuTemperatureCelsius * 100.0f;

        lTemperatureCentiDegrees = (int32_t)(fScaledTemperature +
            ((fScaledTemperature >= 0.0f) ? 0.5f : -0.5f));
        ulPotentiometerMillivolts =
            (uint32_t)((xSample.fPotentiometerVoltage * 1000.0f) + 0.5f);

        if (lTemperatureCentiDegrees < 0)
        {
            pcTemperatureSign = "-";
            ulTemperatureMagnitude =
                (uint32_t)(-(lTemperatureCentiDegrees + 1)) + 1U;
        }
        else
        {
            ulTemperatureMagnitude = (uint32_t)lTemperatureCentiDegrees;
        }

        /* 避免启用Newlib的浮点printf，使用定点整数输出两位和三位小数。 */
        iLength = snprintf(cReport, sizeof(cReport),
                           "DATA: temperature=%s%lu.%02lu C, voltage=%lu.%03lu V\r\n",
                           pcTemperatureSign,
                           (unsigned long)(ulTemperatureMagnitude / 100U),
                           (unsigned long)(ulTemperatureMagnitude % 100U),
                           (unsigned long)(ulPotentiometerMillivolts / 1000U),
                           (unsigned long)(ulPotentiometerMillivolts % 1000U));
    }

    if ((iLength <= 0) || (iLength >= (int)sizeof(cReport)))
    {
        return false;
    }

    return AppSerial_Write((const uint8_t *)cReport, (uint16_t)iLength);
}

static void prvSerialTxTask(void *pvParameters)
{
    AppSerialTxMessage_t xMessage;
    (void)pvParameters;

    for (;;)
    {
        /*
         * Queue为空时任务进入Blocked状态。
         * 新消息到达后，FreeRTOS把消息副本写入xMessage并唤醒任务。
         */
        if (xQueueReceive(
                s_xSerialTxQueue,
                &xMessage,
                portMAX_DELAY) == pdPASS)
        {
            /*
             * xMessage位于Serial TX Task自己的任务栈中。
             * Task不会在DMA完成前再次调用xQueueReceive()，
             * 所以DMA读取期间这段内存不会被覆盖。
             */
            if (BspUsart1_TxDmaStart(
                    xMessage.ucData,
                    xMessage.usLength) == true)
            {
                /*
                 * 等待DMA完成中断。
                 * ISR发送通知后，任务才能取出下一条Queue消息，
                 * 因此同一时刻只有一块TX缓冲区交给DMA。
                 */
                (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            else
            {
                /*
                 * Serial TX Task是唯一DMA启动者，正常情况下不应失败。
                 * 当前先记录异常并丢弃本条消息，后续再增加恢复策略。
                 */
                s_ulTxDmaStartFailureCount++;
            }
        }
    }
}

bool AppSerialTxTask_Create(void)
{
    BaseType_t xCreateResult;

    /*
     * 当前模块只能创建一次，防止出现两个Task同时拥有USART TX。
     */
    if (s_xSerialTxQueue != NULL)
    {
        return false;
    }

    s_xSerialTxQueue = xQueueCreate(
        APP_SERIAL_TX_QUEUE_LENGTH,
        sizeof(AppSerialTxMessage_t));

    if (s_xSerialTxQueue == NULL)
    {
        return false;
    }

    xCreateResult = xTaskCreate(
        prvSerialTxTask,
        "SERIAL_TX",
        APP_SERIAL_TX_STACK_DEPTH,
        NULL,
        APP_SERIAL_TX_PRIORITY,
        &s_xSerialTxTaskHandle);

    if (xCreateResult != pdPASS)
    {
        /*
         * Task创建失败时释放已经创建的Queue，
         * 避免模块处于“有Queue但没有消费者”的半初始化状态。
         */
        vQueueDelete(s_xSerialTxQueue);
        s_xSerialTxQueue = NULL;
        s_xSerialTxTaskHandle = NULL;
        return false;
    }

    return true;
}

bool AppSerial_Write(const uint8_t *pData, uint16_t length)
{
    AppSerialTxMessage_t xMessage = {0};

    if ((pData == NULL) ||
        (length == 0U) ||
        (length > APP_SERIAL_TX_MESSAGE_MAX_LENGTH) ||
        (s_xSerialTxQueue == NULL))
    {
        return false;
    }

    xMessage.usLength = length;
    memcpy(xMessage.ucData, pData, length);

    /*
     * 使用0 Tick表示Queue满时立即返回，不阻塞调用者。
     * 日志系统不能因为串口速度较慢而无限阻塞控制任务。
     */
    return xQueueSend(
               s_xSerialTxQueue,
               &xMessage,
               0U) == pdPASS;
}

void DMA1_Channel4_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken;

    xHigherPriorityTaskWoken = pdFALSE;

    if (BspUsart1_TxDmaHandleInterrupt() == true)
    {
        if (s_xSerialTxTaskHandle != NULL)
        {
            vTaskNotifyGiveFromISR(
                s_xSerialTxTaskHandle,
                &xHigherPriorityTaskWoken);  
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void prvSerialRxTask(void *pvParameters)
{
    const uint8_t *pucRxBuffer; //指向USART1 RX DMA循环缓冲区的起始地址
    AppConsoleOutput_t xConsoleOutput = {0}; //当前Console响应
    AppRtosDiagnosticsReport_t xDiagnosticsReport = {0};  //当前诊断报告
    uint16_t usByteOffset;  //表示当前正在处理连续数据块中的第几个字节
    uint16_t usReadPosition;        //当前已经处理到DMA循环缓冲区的哪个位置
    uint16_t usWritePosition;      //下一次DMA写入位置
    uint16_t usChunkLength; //本次处理的数据块长度

    (void)pvParameters;

    pucRxBuffer = BspUsart1_RxDmaGetBuffer();
    usReadPosition = 0U;

    /*
    * Console由Serial RX Task单独拥有，
    * 必须在开始接收字节前重置其行协议状态。
    */
    AppConsole_Init();
    /*
     * 当前已经处于Task上下文，调度器也已经运行，
     * 此时才启动RX DMA和允许USART1 IDLE中断。
     */
    BspUsart1_RxDmaStart();

    for (;;)
    {
        /*
        * 没有IDLE事件时进入Blocked状态。
        * ISR只通知Task，不在中断中复制或解析接收数据，
        * 也不生成Console响应。
        */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /*
         * 只处理取得写位置快照时已经收到的数据。
         * 快照之后到达的新数据留到下一次IDLE事件处理。
         */
        usWritePosition = BspUsart1_RxDmaGetWritePosition();

        while (usReadPosition != usWritePosition)
        {
            if (usWritePosition > usReadPosition)
            {
                usChunkLength =
                    usWritePosition - usReadPosition;
            }
            else
            {
                /*
                 * DMA已经回绕，先处理缓冲区尾部，
                 * 处理到256后再从位置0继续。
                 */
                usChunkLength =
                    BSP_USART1_RX_DMA_BUFFER_SIZE -
                    usReadPosition;
            }

            /*
            * 当前数据块保证不会跨越DMA缓冲区末尾，
            * 因此usReadPosition + usByteOffset不会越界。
            */
            for (usByteOffset = 0U;
                usByteOffset < usChunkLength;
                usByteOffset++)
            {
                if (AppConsole_ProcessByte(pucRxBuffer[usReadPosition + usByteOffset], &xConsoleOutput) == true)
                {
                    switch (xConsoleOutput.eType)
                    {
                        case APP_CONSOLE_OUTPUT_TEXT:
                            if (AppSerial_Write(
                                    xConsoleOutput.pucData,
                                    xConsoleOutput.usLength) == false)
                            {
                                s_ulConsoleResponseDropCount++;
                            }
                            break;

                        case APP_CONSOLE_OUTPUT_RTOS_TASK_LIST:
                            if (AppRtosDiagnostics_BuildTaskList(
                                    &xDiagnosticsReport) == false)
                            {
                                s_ulDiagnosticsBuildFailureCount++;
                            }
                            else
                            {
                                /*
                                * 诊断报告位于模块静态缓冲区；
                                * AppSerial_Write()会立即复制进TX Queue。
                                */
                                if (AppSerial_Write(
                                        xDiagnosticsReport.pucData,
                                        xDiagnosticsReport.usLength) == false)
                                {
                                    s_ulConsoleResponseDropCount++;
                                }
                            }
                            break;
                        case APP_CONSOLE_OUTPUT_RTOS_HEAP:
                            if (AppRtosDiagnostics_BuildHeapReport(
                                    &xDiagnosticsReport) == false)
                            {
                                s_ulDiagnosticsBuildFailureCount++;
                            }
                            else
                            {
                                /*
                                * Heap报告位于诊断模块静态缓冲区，
                                * AppSerial_Write()会立即复制进TX Queue。
                                */
                                if (AppSerial_Write(
                                        xDiagnosticsReport.pucData,
                                        xDiagnosticsReport.usLength) == false)
                                {
                                    s_ulConsoleResponseDropCount++;
                                }
                            }
                            break;

                        case APP_CONSOLE_OUTPUT_ADC_DATA:
                            if (prvQueueAdcDataReport() == false)
                            {
                                s_ulConsoleResponseDropCount++;
                            }
                            break;

                        case APP_CONSOLE_OUTPUT_NONE:
                        default:
                            break;
                    }
                }
            }

            usReadPosition += usChunkLength;

            if (usReadPosition >=
                BSP_USART1_RX_DMA_BUFFER_SIZE)
            {
                usReadPosition = 0U;
            }
        }
    }
}


bool AppSerialRxTask_Create(void)
{
    BaseType_t xCreateResult;

   /*
    * Console响应提交依赖现有TX Queue，
    * 而且RX Task只允许创建一次。
    */
    if ((s_xSerialTxQueue == NULL) ||
        (s_xSerialRxTaskHandle != NULL))
    {
        return false;
    }

    xCreateResult = xTaskCreate(
        prvSerialRxTask,
        "SERIAL_RX",
        APP_SERIAL_RX_STACK_DEPTH,
        NULL,
        APP_SERIAL_RX_PRIORITY,
        &s_xSerialRxTaskHandle);

    if (xCreateResult != pdPASS)
    {
        s_xSerialRxTaskHandle = NULL;
        return false;
    }

    return true;
}

void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken;

    xHigherPriorityTaskWoken = pdFALSE;

    if (BspUsart1_RxIdleHandleInterrupt() == true)
    {
        if (s_xSerialRxTaskHandle != NULL)
        {
            vTaskNotifyGiveFromISR(
                s_xSerialRxTaskHandle,
                &xHigherPriorityTaskWoken);
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
