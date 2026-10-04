#include "app_led_task.h"

#include "FreeRTOS.h"
#include "queue.h"

#include "bsp_led.h"
#include "app_key_event.h"

void vLedTask(void *pvParameters)
{
    QueueHandle_t xKeyEventQueue;
    AppKeyEvent_t eKeyEvent;

    /*
     * Queue由main()创建，LED Task只借用Handle并消费事件。
     * Key Task和LED Task拿到的是同一个Queue对象。
     */
    xKeyEventQueue = (QueueHandle_t)pvParameters;
    configASSERT(xKeyEventQueue != NULL);

    for (;;)
    {
        /*
         * Queue为空时，LED Task进入Blocked状态，不占用CPU。
         * Key Task发送事件后，FreeRTOS会重新将LED Task转为Ready。
         */
        if (xQueueReceive(
                xKeyEventQueue,     /* 从哪个Queue接收。 */
                &eKeyEvent,          /* 把一个项目/队列里面的一个元素复制到哪里。 */
                portMAX_DELAY) == pdPASS)   /* Queue为空时最多等待多少个Tick。 */
        {
            /*
             * xQueueReceive()已将Queue中的事件复制到eKeyEvent。
             * LED Task根据事件类型决定具体的硬件动作。
             */
            switch (eKeyEvent)
            {
                case APP_KEY_EVENT_KEY1_PRESSED:
                    LED2_TOGGLE;
                    break;

                case APP_KEY_EVENT_KEY2_PRESSED:
                    LED3_TOGGLE;
                    break;

                default:
                    /*
                     * 忽略当前协议未定义的事件值，避免错误事件
                     * 意外操作LED。
                     */
                    break;
            }
        }
    }
}