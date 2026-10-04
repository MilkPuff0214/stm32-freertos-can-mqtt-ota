#include "app_key_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_key.h"
#include "queue.h"

#include "app_key_event.h"

/* 每10 ms读取一次按键，任务在两次读取之间进入Blocked状态。 */
#define APP_KEY_SCAN_PERIOD_MS          10U

/* 连续3次读到相同状态才确认状态变化，消抖时间约为20～30 ms。 */
#define APP_KEY_DEBOUNCE_SAMPLE_COUNT   3U

typedef struct
{
    BspKeyState_t eLastRawState;    //上一次读取到的原始电平
    BspKeyState_t eStableState;       //当前稳定的按键状态
    uint8_t ucSameSampleCount;      /* 连续采样到相同原始状态的次数。 */
} AppKeyDebounce_t;

/*
 * 记录因为Queue已满而未能发送的按键事件数量。
 * 当前只有Key Task会修改它，因此不存在多个Task同时写入的问题。
 */
static volatile uint32_t ulDroppedKeyEventCount = 0U;

/*
 * 对一个按键执行连续采样消抖。
 *
 * 返回pdTRUE仅表示检测到一次稳定的“释放→按下”事件。
 * 持续按住按键不会重复返回pdTRUE，必须先释放再按下。
 */
static BaseType_t prvKeyPressDetected(
    BspKeyId_t keyId,
    AppKeyDebounce_t *pDebounceState)
{
    BspKeyState_t eRawState;  

    eRawState = BspKey_Read(keyId);  // 当前读取到的按键状态

    if (eRawState != pDebounceState->eLastRawState)
    {
        /*
         * 原始电平发生变化时重新计数，防止触点抖动被误认为
         * 一个已经稳定的按键状态。
         */
        pDebounceState->eLastRawState = eRawState;
        pDebounceState->ucSameSampleCount = 1U;
        return pdFALSE;
    }

    if (pDebounceState->ucSameSampleCount <
        APP_KEY_DEBOUNCE_SAMPLE_COUNT)
    {
        pDebounceState->ucSameSampleCount++;
    }

    if ((pDebounceState->ucSameSampleCount ==
         APP_KEY_DEBOUNCE_SAMPLE_COUNT) &&
        (pDebounceState->eStableState != eRawState))
    {
        pDebounceState->eStableState = eRawState;

        if (eRawState == BSP_KEY_PRESSED)
        {
            return pdTRUE;
        }
    }

    return pdFALSE;
}

/*
 * 非阻塞发送一个按键事件。
 *
 * Key Task需要保持稳定的扫描周期，因此Queue已满时不无限等待。
 * xQueueSend()会把eEvent的内容复制到Queue内部存储区。
 */
static void prvSendKeyEvent(
    QueueHandle_t xKeyEventQueue,
    AppKeyEvent_t eEvent)
{
    BaseType_t xSendResult;

    xSendResult = xQueueSend(
        xKeyEventQueue, /* 要发送到哪个Queue。 */
        &eEvent,        /* 从哪里复制一个Queue项目。 */
        0U);            /* Queue满时最多等待多少个Tick。 */

    if (xSendResult != pdPASS)
    {
        /*
         * Queue已满时丢弃本次事件并记录次数。
         * 后续可以把这个计数纳入系统诊断信息。
         */
        ulDroppedKeyEventCount++;
    }
}

void vKeyTask(void *pvParameters)
{
    QueueHandle_t xKeyEventQueue;

    /*
     * main()通过xTaskCreate()的pvParameters参数，
     * 把同一个Queue Handle传给Key Task。
     */
    xKeyEventQueue = (QueueHandle_t)pvParameters;
    configASSERT(xKeyEventQueue != NULL);

    AppKeyDebounce_t xKey1State =
    {
        .eLastRawState = BSP_KEY_RELEASED,
        .eStableState = BSP_KEY_RELEASED,
        .ucSameSampleCount = 0U
    };

    AppKeyDebounce_t xKey2State =
    {
        .eLastRawState = BSP_KEY_RELEASED,
        .eStableState = BSP_KEY_RELEASED,
        .ucSameSampleCount = 0U
    };

    for (;;)
    {
        /*
        * Key Task只负责识别稳定的按下事件并发送到Queue。
        * LED动作由消费事件的LED Task决定。
        */
        if (prvKeyPressDetected(
        BSP_KEY_ID_1,
        &xKey1State) == pdTRUE)
        {
            /*
            * Key Task只报告发生了KEY1按下事件，
            * 不决定这个事件最终控制哪一盏LED。
            */
            prvSendKeyEvent(
                xKeyEventQueue,
                APP_KEY_EVENT_KEY1_PRESSED);
        }

        if (prvKeyPressDetected(
        BSP_KEY_ID_2,
        &xKey2State) == pdTRUE)
        {
            prvSendKeyEvent(
                xKeyEventQueue,
                APP_KEY_EVENT_KEY2_PRESSED);
        }

        /*
         * 不能使用空转延时。vTaskDelay()会阻塞当前任务，
         * 让调度器在等待期间运行LED Task或Idle Task。
         */
        vTaskDelay(pdMS_TO_TICKS(APP_KEY_SCAN_PERIOD_MS));
    }
}