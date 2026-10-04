#include "app_can_task.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_can_protocol.h"
#include "bsp_can.h"

#include "queue.h"
#include "app_output_control.h"
#include "app_adc_task.h"

#define APP_CAN_REPORT_PERIOD_MS       1000U
#define APP_CAN_TX_POLL_PERIOD_MS      1U
#define APP_CAN_TX_TIMEOUT_MS          5U
#define APP_CAN_TASK_STACK_DEPTH       256U
#define APP_CAN_TASK_PRIORITY          (tskIDLE_PRIORITY + 1U)
#define APP_CAN_FILTER_NUMBER          0U   // 使用的CAN硬件滤波器编号


#define APP_CAN_RX_QUEUE_LENGTH       8U
#define APP_CAN_ISR_DRAIN_LIMIT       3U


static TaskHandle_t s_xCanTaskHandle = NULL;

/*
 * 这些计数器只有CAN Task写入，因此当前不需要Mutex。
 * volatile使其适合通过GDB直接观察。
 */
static volatile uint32_t s_ulCanTxSuccessCount = 0U;
static volatile uint32_t s_ulCanTxFailedCount = 0U;
static volatile uint32_t s_ulCanTxTimeoutCount = 0U;
static volatile uint32_t s_ulCanTxMailboxBusyCount = 0U;

static QueueHandle_t s_xCanRxQueue = NULL;  // CAN接收队列句柄

/* ISR写入的诊断计数器。 */                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      
static volatile uint32_t s_ulCanRxQueueDropCount = 0U;       // 接收队列丢弃计数
static volatile uint32_t s_ulCanRxFifoOverrunCount = 0U;    // FIFO溢出计数

/* CAN Task写入的命令处理计数器。 */
static volatile uint32_t s_ulCanRxRejectedCount = 0U;           // 接收报文被拒绝计数
static volatile uint32_t s_ulCanCommandAppliedCount = 0U;      // 命令成功应用计数

/*
 * CAN上报到期但ADC Task尚未发布过任何样本的次数。
 * 这与“ADC已经发布了无效样本”是两个不同状态。
 */
static volatile uint32_t s_ulCanAdcSampleUnavailableCount = 0U;


typedef enum
{
    APP_CAN_LINK_STATE_ACTIVE = 0,
    APP_CAN_LINK_STATE_WARNING,
    APP_CAN_LINK_STATE_PASSIVE,
    APP_CAN_LINK_STATE_BUS_OFF
} AppCanLinkState_t;

/*
 * 这些状态只由CAN Task更新。
 * BSP和ISR都不直接修改应用层链路状态，因此不需要Mutex。
 */
static volatile AppCanLinkState_t s_xCanLinkState = APP_CAN_LINK_STATE_ACTIVE;

static volatile BspCanErrorSnapshot_t s_xCanErrorSnapshot;

static volatile uint32_t s_ulCanWarningEntryCount = 0U;
static volatile uint32_t s_ulCanPassiveEntryCount = 0U;
static volatile uint32_t s_ulCanBusOffEntryCount = 0U;
static volatile uint32_t s_ulCanBusOffRecoveryCount = 0U;
static volatile uint32_t s_ulCanTxSkippedBusOffCount = 0U;


static void prvEncodeUint16LittleEndian(
    uint8_t *pucData,
    uint16_t usValue)
{
    pucData[0] = (uint8_t)usValue;
    pucData[1] = (uint8_t)(usValue >> 8U);
}

static void prvBuildAdcReportFrame(
    BspCanTxFrame_t *pxFrame,
    uint8_t ucSequence)
{
    AppAdcSample_t xSample;
    uint16_t usPotentiometerRaw =
        APP_CAN_ADC_INVALID_RAW_VALUE;
    uint16_t usTemperatureRaw =
        APP_CAN_ADC_INVALID_RAW_VALUE;
    uint8_t ucValidFlags = 0U;

    *pxFrame = (BspCanTxFrame_t){0};

    pxFrame->ulExtendedId =
        APP_CAN_ADC_REPORT_EXTENDED_ID;
    pxFrame->ucDlc = APP_CAN_ADC_REPORT_DLC;
    pxFrame->ucData[0] = ucSequence;

    /*
     * 零等待读取ADC Task最近发布的完整样本。
     * CAN Task不启动ADC转换，也不等待ADC采样完成。
     */
    if (AppAdc_TryReadLatestSample(&xSample) == false)
    {
        /*
         * 系统启动后，CAN第一次上报可能早于ADC第一次采样。
         * 此时按协议发送两个0xFFFF和valid_flags=0。
         */
        s_ulCanAdcSampleUnavailableCount++;
    }
    else
    {
        if (xSample.xValid == true)
        {
            usPotentiometerRaw = xSample.usPotentiometerRaw;
            usTemperatureRaw = xSample.usTemperatureRaw;
            ucValidFlags = APP_CAN_ADC_POTENTIOMETER_VALID_MASK |
                           APP_CAN_ADC_TEMPERATURE_VALID_MASK;
        }
    }

    pxFrame->ucData[1] = ucValidFlags;

    /*
     * 协议规定uint16_t使用小端序。
     * 不直接memcpy结构体，避免结构体布局和主机字节序影响协议。
     */
    prvEncodeUint16LittleEndian(
        &pxFrame->ucData[2],
        usPotentiometerRaw);

    prvEncodeUint16LittleEndian(
        &pxFrame->ucData[4],
        usTemperatureRaw);
}

static void prvRefreshCanErrorState(void)
{
    BspCanErrorSnapshot_t xSnapshot;
    AppCanLinkState_t xNewState;
    AppCanLinkState_t xPreviousState;

    if (BspCan_GetErrorSnapshot(&xSnapshot) == false)
    {
        return;
    }

    /*
     * 状态严重程度按照Bus-off、Passive、Warning、Active判断。
     * 同一时刻可能有多个硬件标志，所以必须先判断最严重状态。
     */
    if (xSnapshot.xBusOff == true)
    {
        xNewState = APP_CAN_LINK_STATE_BUS_OFF;
    }
    else if (xSnapshot.xErrorPassive == true)
    {
        xNewState = APP_CAN_LINK_STATE_PASSIVE;
    }
    else if (xSnapshot.xErrorWarning == true)
    {
        xNewState = APP_CAN_LINK_STATE_WARNING;
    }
    else
    {
        xNewState = APP_CAN_LINK_STATE_ACTIVE;
    }

    xPreviousState = s_xCanLinkState;
    s_xCanErrorSnapshot = xSnapshot;

    if (xNewState == xPreviousState)
    {
        return;
    }

    if ((xPreviousState == APP_CAN_LINK_STATE_BUS_OFF) &&
        (xNewState != APP_CAN_LINK_STATE_BUS_OFF))
    {
        /*
         * 这里只记录已经观察到硬件退出Bus-off。
         * 真正的恢复时序由bxCAN的ABOM硬件完成。
         */
        s_ulCanBusOffRecoveryCount++;
    }

    switch (xNewState)
    {
        case APP_CAN_LINK_STATE_WARNING:
            s_ulCanWarningEntryCount++;
            break;

        case APP_CAN_LINK_STATE_PASSIVE:
            s_ulCanPassiveEntryCount++;
            break;

        case APP_CAN_LINK_STATE_BUS_OFF:
            s_ulCanBusOffEntryCount++;
            break;

        case APP_CAN_LINK_STATE_ACTIVE:
        default:
            break;
    }

    s_xCanLinkState = xNewState;
}
/**
 * @brief 发送CAN帧并等待发送完成或超时。
 * @param pxFrame 指向要发送的CAN帧的指针。
 */
static void prvTransmitAndWait(const BspCanTxFrame_t *pxFrame)
{
    BspCanTxStatus_t xStatus;
    TickType_t xStartTick;
    const TickType_t xTimeoutTicks =
        pdMS_TO_TICKS(APP_CAN_TX_TIMEOUT_MS);
    uint8_t ucMailbox;

    prvRefreshCanErrorState();

    if (s_xCanLinkState == APP_CAN_LINK_STATE_BUS_OFF)
    {
        /*
        * Bus-off期间不再向发送邮箱提交新报文。
        * ABOM恢复后，下一个周期再发送当前最新遥测即可，
        * 不补发已经过时的历史遥测。
        */
        s_ulCanTxSkippedBusOffCount++;
        return;
    }

    if (BspCan_TryTransmit(pxFrame, &ucMailbox) == false)
    {
        /*
         * 本任务构造的帧参数固定合法，因此这里通常表示
         * 三个硬件发送邮箱当前全部被占用。
         */
        s_ulCanTxMailboxBusyCount++;
        return;
    }

    xStartTick = xTaskGetTickCount();

    for (;;)
    {
        xStatus = BspCan_GetTransmitStatus(ucMailbox);

         /*
        * 发送期间ACK错误会推动TEC快速变化。
        * 每轮发送状态检查时同步更新CAN错误快照。
        */
        prvRefreshCanErrorState();
        
        if (xStatus == BSP_CAN_TX_STATUS_SUCCESS)
        {
            s_ulCanTxSuccessCount++;
            return;
        }

        if ((xStatus == BSP_CAN_TX_STATUS_FAILED) ||
            (xStatus == BSP_CAN_TX_STATUS_INVALID))
        {
            s_ulCanTxFailedCount++;
            return;
        }

        if ((xTaskGetTickCount() - xStartTick) >= xTimeoutTicks)
        {
            /*
            * 先读取错误状态，再撤销邮箱
            */
            prvRefreshCanErrorState();
            /*
             * NART=DISABLE时bxCAN会自动重发。
             * 任务级超时后主动撤销请求，避免邮箱无限期被占用。
             */
            (void)BspCan_CancelTransmit(ucMailbox);
            s_ulCanTxTimeoutCount++;
            return;
        }

        /*
         * CAN发送由硬件继续进行；任务在轮询间隔内进入Blocked，
         * 不使用忙等待霸占CPU。
         */
        vTaskDelay(pdMS_TO_TICKS(APP_CAN_TX_POLL_PERIOD_MS));
    }
}
/**
 * @brief 计算距离下一次遥测上报的等待Ticks数。
 * @param xLastReportTick 上次遥测上报的Tick数。
 * @return 距离下一次遥测上报的等待Ticks数，如果已经到期则返回0。
 */
static TickType_t prvGetReportWaitTicks(
    TickType_t xLastReportTick)
{
    TickType_t xElapsedTicks;   //已经过去的Ticks数
    TickType_t xPeriodTicks;

    xPeriodTicks =
        pdMS_TO_TICKS(APP_CAN_REPORT_PERIOD_MS);

    xElapsedTicks =
        xTaskGetTickCount() - xLastReportTick;

    if (xElapsedTicks >= xPeriodTicks)
    {
        return 0U;
    }

    return xPeriodTicks - xElapsedTicks;
}

/**
 * @brief 构建控制命令的ACK帧。
 * @param pxAckFrame 指向要构建的ACK帧的指针。
 * @param ucResult 控制命令的执行结果。
 */
static void prvBuildControlAck(BspCanTxFrame_t *pxAckFrame, uint8_t ucResult,
                               uint8_t ucAppliedState)
{
    *pxAckFrame = (BspCanTxFrame_t){0};
    pxAckFrame->ulExtendedId = APP_CAN_CONTROL_ACK_EXTENDED_ID;
    pxAckFrame->ucDlc = APP_CAN_CONTROL_ACK_DLC;
    pxAckFrame->ucData[0] = ucResult;
    pxAckFrame->ucData[1] = ucAppliedState;
}

static uint8_t prvEncodeAppliedState(const AppOutputState_t *pxState)
{
    uint8_t ucAppliedState = 0U;

    if (pxState->xRedLedOn)
    {
        ucAppliedState |= APP_CAN_TARGET_RED_MASK;
    }
    if (pxState->xBuzzerOn)
    {
        ucAppliedState |= APP_CAN_TARGET_BUZZER_MASK;
    }
    return ucAppliedState;
}

/**
 * @brief 处理接收到的控制命令。
 * @param pxFrame 指向接收到的控制命令帧的指针。
 */
static void prvProcessControlCommand(
    const BspCanRxFrame_t *pxFrame)
{
    BspCanTxFrame_t xAckFrame;
    uint8_t ucResult;
    AppOutputState_t xAppliedState;
    bool xOutputUpdated;
    /*
     * 硬件过滤器已经进行第一层筛选，
     * Task仍再次校验帧类型和扩展ID。
     */
    if ((pxFrame->xIsExtended == false) ||
        (pxFrame->xIsRemote == true) ||
        (pxFrame->ulExtendedId !=
         APP_CAN_CONTROL_COMMAND_EXTENDED_ID))
    {
        s_ulCanRxRejectedCount++;
        return;
    }

    if (pxFrame->ucDlc != APP_CAN_CONTROL_COMMAND_DLC)
    {
        /*
         * 控制命令现在只有一个target_state字节，
         * 因此合法DLC必须等于1。
         */
        ucResult =
            APP_CAN_COMMAND_RESULT_INVALID_DLC;
    }
    else if ((pxFrame->ucData[0] &
              (uint8_t)(~APP_CAN_TARGET_VALID_MASK)) != 0U)
    {
        ucResult =
            APP_CAN_COMMAND_RESULT_INVALID_STATE;
    }
    else
    {
        xOutputUpdated = AppOutputControl_SetAll(
            (pxFrame->ucData[0] & APP_CAN_TARGET_RED_MASK) != 0U,
            (pxFrame->ucData[0] & APP_CAN_TARGET_BUZZER_MASK) != 0U,
            &xAppliedState);
        configASSERT(xOutputUpdated);
        if (xOutputUpdated)
        {
            s_ulCanCommandAppliedCount++;
            ucResult = APP_CAN_COMMAND_RESULT_OK;
        }
        else
        {
            ucResult = APP_CAN_COMMAND_RESULT_INVALID_STATE;
        }
    }

    xOutputUpdated = AppOutputControl_GetState(&xAppliedState);
    configASSERT(xOutputUpdated);
    if (xOutputUpdated == false)
    {
        xAppliedState = (AppOutputState_t){0};
    }
    prvBuildControlAck(&xAckFrame, ucResult, prvEncodeAppliedState(&xAppliedState));

    prvTransmitAndWait(&xAckFrame);
}
static void prvCanTask(void *pvParameters)
{
    BspCanRxFrame_t xRxFrame;
    BspCanTxFrame_t xTelemetryFrame;
    TickType_t xLastReportTick; //上次一数据上报的时刻
    TickType_t xCurrentTick;
    TickType_t xReportPeriodTicks;  //数据上报周期
    TickType_t xQueueWaitTicks;
    uint8_t ucTelemetrySequence = 0U;

    (void)pvParameters;

    /*
     * Queue和调度器都已经可用后才允许CAN RX中断，
     * 防止ISR在接收Queue创建前运行。
     */
    BspCan_EnableRxFifo0Interrupt();

    xLastReportTick = xTaskGetTickCount();
    xReportPeriodTicks =
        pdMS_TO_TICKS(APP_CAN_REPORT_PERIOD_MS);

    for (;;)
    {
        /*
         * 没有命令时最多阻塞到下一次遥测时刻；
         * 有命令进入Queue时立即被ISR唤醒。
         */
        xQueueWaitTicks =
            prvGetReportWaitTicks(xLastReportTick);

        prvRefreshCanErrorState();

        if (xQueueReceive(
                s_xCanRxQueue,
                &xRxFrame,
                xQueueWaitTicks) == pdPASS) //如果收到CAN消息，处理控制命令。如果没有xQueueReceive返回pdFAIL
        {
            /*
             * 控制ACK优先处理，然后再检查遥测是否到期。
             */
            prvProcessControlCommand(&xRxFrame);
        }

        xCurrentTick = xTaskGetTickCount();

        if ((xCurrentTick - xLastReportTick) >=
            xReportPeriodTicks)
        {
            /*
             * 按原周期基准推进，不把本轮命令和ACK处理耗时
             * 永久累计到后续遥测周期。
             *
             * 如果任务意外错过多个周期，只发送最新的一帧，
             * 不突发补发已经过时的历史遥测。
             */
            do
            {
                xLastReportTick += xReportPeriodTicks;
            }
            while ((xCurrentTick - xLastReportTick) >=
                   xReportPeriodTicks);

            prvBuildAdcReportFrame(&xTelemetryFrame, ucTelemetrySequence);

            ucTelemetrySequence++;

            prvTransmitAndWait(&xTelemetryFrame);
        }
    }
}

bool AppCanTask_Create(void)
{
    BaseType_t xCreateResult;

    if ((s_xCanTaskHandle != NULL) ||
        (s_xCanRxQueue != NULL))
    {
        return false;
    }

    s_xCanRxQueue = xQueueCreate(
        APP_CAN_RX_QUEUE_LENGTH,
        sizeof(BspCanRxFrame_t));

    if (s_xCanRxQueue == NULL)
    {
        return false;
    }

    if (BspCan_ConfigureExactExtendedDataFilter(
            APP_CAN_FILTER_NUMBER,
            APP_CAN_CONTROL_COMMAND_EXTENDED_ID) == false)
    {
        vQueueDelete(s_xCanRxQueue);
        s_xCanRxQueue = NULL;
        return false;
    }

    xCreateResult = xTaskCreate(
        prvCanTask,
        "CAN",
        APP_CAN_TASK_STACK_DEPTH,
        NULL,
        APP_CAN_TASK_PRIORITY,
        &s_xCanTaskHandle);

    if (xCreateResult != pdPASS)
    {
        vQueueDelete(s_xCanRxQueue);
        s_xCanRxQueue = NULL;
        s_xCanTaskHandle = NULL;
        return false;
    }

    return true;
}

//消息挂起或者FIFO0溢出中断处理函数
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken;
    BspCanRxFrame_t xRxFrame;
    uint8_t ucDrainCount;

    xHigherPriorityTaskWoken = pdFALSE;

    if (BspCan_TakeRxFifo0Overrun())
    {
        s_ulCanRxFifoOverrunCount++;
    }

    /*
     * bxCAN单个接收FIFO深度为3。
     * 每次ISR最多处理3帧，避免外部节点持续发送时
     * 形成无界ISR循环。退出后如果FIFO仍非空，
     * FMP0会继续保持并再次触发中断。
     */
    for (ucDrainCount = 0U;
         ucDrainCount < APP_CAN_ISR_DRAIN_LIMIT;
         ucDrainCount++)
    {
        if (BspCan_TryReceiveFifo0(&xRxFrame) == false)
        {
            break;
        }

        /*
         * Queue复制整个接收帧，因此ISR返回后局部变量失效
         * 不会影响CAN Task读取。
         */
        if ((s_xCanRxQueue == NULL) ||
            (xQueueSendFromISR(
                s_xCanRxQueue,
                &xRxFrame,
                &xHigherPriorityTaskWoken) != pdPASS))
        {
            /*
             * ISR不能等待Queue腾出空间。
             * Queue满时丢弃当前帧并记录次数。
             */
            s_ulCanRxQueueDropCount++;
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
