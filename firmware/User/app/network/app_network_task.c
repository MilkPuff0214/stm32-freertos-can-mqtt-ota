#include "app_network_task.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_serial_task.h"
#include "bsp_w5500.h"
#include "app_network_status.h"

#include <string.h>

/* 当前1 ms/Tick；延时2 Tick，保证复位低电平超过500 us。 */
#define APP_NETWORK_RESET_ASSERT_TIME_MS    2U
#define APP_NETWORK_SERVICE_PERIOD_MS       10U
#define APP_NETWORK_PHY_POLL_PERIOD_MS      500U

static const BspW5500NetworkConfig_t s_xStaticNetworkConfig =
{
    .aucMac = {0x02U, 0x53U, 0x54U, 0x4DU, 0x32U, 0x01U},
    .aucIp = {192U, 168U, 101U, 50U},
    .aucSubnetMask = {255U, 255U, 255U, 0U},
    .aucGateway = {192U, 168U, 101U, 1U}  //aucGateway 是“默认网关地址”；以后 TCP Server 地址是电脑的 .11
};

static bool prvNetworkConfigMatches(
    const BspW5500NetworkConfig_t *pxExpected,
    const BspW5500NetworkConfig_t *pxActual)
{
    return
        (memcmp(
            pxExpected->aucMac,
            pxActual->aucMac,
            BSP_W5500_MAC_ADDRESS_LENGTH) == 0) &&
        (memcmp(
            pxExpected->aucIp,
            pxActual->aucIp,
            BSP_W5500_IPV4_OCTET_COUNT) == 0) &&
        (memcmp(
            pxExpected->aucSubnetMask,
            pxActual->aucSubnetMask,
            BSP_W5500_IPV4_OCTET_COUNT) == 0) &&
        (memcmp(
            pxExpected->aucGateway,
            pxActual->aucGateway,
            BSP_W5500_IPV4_OCTET_COUNT) == 0);
}


/*
 * 单位为StackType_t元素，Cortex-M3上512 words = 2048字节。
 * AppSerial_Write()自身包含512字节消息数组，还需容纳格式化和调用栈。
 * 实机通过task命令检查NET任务栈余量。
 */
#define APP_NETWORK_TASK_STACK_DEPTH    512U        //栈空间：512 words = 2048字节
#define APP_NETWORK_TASK_PRIORITY                \
    (tskIDLE_PRIORITY + 1U)


typedef struct
{
    RtosW5500Access_t *pxW5500Access;
    QueueHandle_t xMqttNetworkStatusQueue;
    QueueHandle_t xFtpNetworkStatusQueue;
} AppNetworkTaskContext_t;

static AppNetworkTaskContext_t s_xNetworkTaskContext = {0};

#define APP_NETWORK_RESET_RELEASE_WAIT_MS         50U
#define APP_NETWORK_VERSION_READ_COUNT            8U
#define APP_NETWORK_REPORT_BUFFER_SIZE            96U

static TaskHandle_t s_xNetworkTaskHandle = NULL;    // Network task handle

static volatile AppNetworkW5500State_t s_xW5500State =
    APP_NETWORK_W5500_STATE_NOT_STARTED;

/* PHY 状态 */
static volatile AppNetworkPhyState_t s_xPhyState = APP_NETWORK_PHY_STATE_UNKNOWN;

static volatile uint8_t s_ucW5500Version = 0U;
static volatile uint32_t s_ulW5500VersionReadCount = 0U;
static volatile uint32_t s_ulW5500ReportDropCount = 0U;

static void prvPublishNetworkStatus(QueueHandle_t xNetworkStatusQueue);

static void prvBroadcastNetworkStatus(void)
{
    prvPublishNetworkStatus(s_xNetworkTaskContext.xMqttNetworkStatusQueue);
    prvPublishNetworkStatus(s_xNetworkTaskContext.xFtpNetworkStatusQueue);
}

static void prvPublishNetworkStatus(
    QueueHandle_t xNetworkStatusQueue)
{
    AppNetworkStatus_t xStatus =
    {
        .xChipReady =
            s_xW5500State == APP_NETWORK_W5500_STATE_READY,
        .xPhyLinkUp =
            s_xPhyState == APP_NETWORK_PHY_STATE_UP
    };
    BaseType_t xResult;

    xResult = xQueueOverwrite(
        xNetworkStatusQueue,
        &xStatus);

    /* 长度1 Queue使用覆盖语义时应始终成功。 */
    configASSERT(xResult == pdPASS);
    (void)xResult;
}


static void prvReportW5500Result(void)
{
    char cReport[APP_NETWORK_REPORT_BUFFER_SIZE];
    int iReportLength;

    switch (s_xW5500State)
    {
        case APP_NETWORK_W5500_STATE_READY:
            iReportLength = snprintf(
                cReport,
                sizeof(cReport),
                "W5500: VERSIONR=0x%02X, reads=%lu, CONFIG OK\r\n",
                (unsigned int)s_ucW5500Version,
                (unsigned long)s_ulW5500VersionReadCount);
            break;
        case APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT:
            iReportLength = snprintf(
                cReport,
                sizeof(cReport),
                "W5500: access mutex timeout\r\n");
            break;
        case APP_NETWORK_W5500_STATE_SPI_TRANSFER_FAILED:
            iReportLength = snprintf(
                cReport,
                sizeof(cReport),
                "W5500: SPI transfer failed (VERSIONR reads=%lu)\r\n",
                (unsigned long)s_ulW5500VersionReadCount);
            break;

        case APP_NETWORK_W5500_STATE_VERSION_MISMATCH:
            iReportLength = snprintf(
                cReport,
                sizeof(cReport),
                "W5500: VERSIONR=0x%02X, expected=0x%02X\r\n",
                (unsigned int)s_ucW5500Version,
                (unsigned int)BSP_W5500_EXPECTED_VERSION);
            break;
        case APP_NETWORK_W5500_STATE_NETWORK_CONFIG_MISMATCH:
            iReportLength = snprintf(
                cReport,
                sizeof(cReport),
                "W5500: network config readback mismatch\r\n");
            break;        
        default:
            return;
    }

    if ((iReportLength <= 0) ||
        (iReportLength >= (int)sizeof(cReport)) ||
        (AppSerial_Write(
            (const uint8_t *)cReport,
            (uint16_t)iReportLength) == false))
    {
        s_ulW5500ReportDropCount++;
    }
}

static AppNetworkW5500State_t prvInitializeW5500(
    RtosW5500Access_t *pxW5500Access,
    BspW5500NetworkConfig_t *pxReadbackConfig)
{
    uint8_t ucIndex;
    uint8_t ucVersion;
    bool xOperationSucceeded;

    if ((RtosW5500Access_IsReady(pxW5500Access) == false) ||
        (pxReadbackConfig == NULL))
    {
        return APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT;
    }

    if (RtosW5500Access_Lock(pxW5500Access) == false)
    {
        return APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT;
    }
    BspW5500_SetResetAsserted(true);
    RtosW5500Access_Unlock(pxW5500Access);

    /* W5500要求RST低电平至少500 us；延时期间不能持有Mutex。 */
    vTaskDelay(pdMS_TO_TICKS(APP_NETWORK_RESET_ASSERT_TIME_MS));

    if (RtosW5500Access_Lock(pxW5500Access) == false)
    {
        return APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT;
    }
    BspW5500_SetResetAsserted(false);
    RtosW5500Access_Unlock(pxW5500Access);

    vTaskDelay(pdMS_TO_TICKS(APP_NETWORK_RESET_RELEASE_WAIT_MS));

    s_xW5500State = APP_NETWORK_W5500_STATE_VERIFYING;
    for (ucIndex = 0U;
         ucIndex < APP_NETWORK_VERSION_READ_COUNT;
         ucIndex++)
    {
        if (RtosW5500Access_Lock(pxW5500Access) == false)
        {
            return APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT;
        }

        xOperationSucceeded = BspW5500_ReadVersion(&ucVersion);
        RtosW5500Access_Unlock(pxW5500Access);

        if (xOperationSucceeded == false)
        {
            return APP_NETWORK_W5500_STATE_SPI_TRANSFER_FAILED;
        }

        s_ucW5500Version = ucVersion;
        s_ulW5500VersionReadCount++;

        if (ucVersion != BSP_W5500_EXPECTED_VERSION)
        {
            return APP_NETWORK_W5500_STATE_VERSION_MISMATCH;
        }
    }

    s_xW5500State = APP_NETWORK_W5500_STATE_CONFIGURING_NETWORK;
    if (RtosW5500Access_Lock(pxW5500Access) == false)
    {
        return APP_NETWORK_W5500_STATE_ACCESS_TIMEOUT;
    }

    xOperationSucceeded =
        BspW5500_ChipInit() &&
        BspW5500_SetNetworkConfig(&s_xStaticNetworkConfig) &&
        BspW5500_GetNetworkConfig(pxReadbackConfig);

    RtosW5500Access_Unlock(pxW5500Access);

    if (xOperationSucceeded == false)
    {
        return APP_NETWORK_W5500_STATE_SPI_TRANSFER_FAILED;
    }
    if (prvNetworkConfigMatches(
            &s_xStaticNetworkConfig,
            pxReadbackConfig) == false)
    {
        return APP_NETWORK_W5500_STATE_NETWORK_CONFIG_MISMATCH;
    }

    return APP_NETWORK_W5500_STATE_READY;
}

static void prvReportNetworkConfig(
    const BspW5500NetworkConfig_t *pxConfig)
{
    char cReport[128];
    int iLength;

    iLength = snprintf(
        cReport,
        sizeof(cReport),
        "W5500 NET: IP=%u.%u.%u.%u MASK=%u.%u.%u.%u "
        "GW=%u.%u.%u.%u\r\n",
        (unsigned int)pxConfig->aucIp[0],
        (unsigned int)pxConfig->aucIp[1],
        (unsigned int)pxConfig->aucIp[2],
        (unsigned int)pxConfig->aucIp[3],
        (unsigned int)pxConfig->aucSubnetMask[0],
        (unsigned int)pxConfig->aucSubnetMask[1],
        (unsigned int)pxConfig->aucSubnetMask[2],
        (unsigned int)pxConfig->aucSubnetMask[3],
        (unsigned int)pxConfig->aucGateway[0],
        (unsigned int)pxConfig->aucGateway[1],
        (unsigned int)pxConfig->aucGateway[2],
        (unsigned int)pxConfig->aucGateway[3]);

    /*
     * Serial接口会复制数据，函数返回后局部缓冲区可以释放。
     * Queue满时记录丢日志次数，不阻塞网络任务。
     */
    if ((iLength <= 0) ||
        (iLength >= (int)sizeof(cReport)) ||
        (AppSerial_Write(
            (const uint8_t *)cReport,
            (uint16_t)iLength) == false))
    {
        s_ulW5500ReportDropCount++;
    }
}

// 轮询PHY链路状态并报告变化。
static void prvPollPhyLink(RtosW5500Access_t *pxW5500Access)
{
    bool xLinkUp = false;
    bool xReadSucceeded;
    AppNetworkPhyState_t xNewState;
    const char *pcMessage;

    if (RtosW5500Access_Lock(pxW5500Access) == false)
    {
        /* 普通锁竞争不改变最近一次可信PHY状态。 */
        return;
    }

    xReadSucceeded = BspW5500_GetPhyLink(&xLinkUp);
    RtosW5500Access_Unlock(pxW5500Access);

    if (xReadSucceeded == false)
    {
        xNewState = APP_NETWORK_PHY_STATE_READ_FAILED;
        pcMessage = "W5500 PHY: READ FAILED\r\n";
    }
    else if (xLinkUp)
    {
        xNewState = APP_NETWORK_PHY_STATE_UP;
        pcMessage = "W5500 PHY: LINK UP\r\n";
    }
    else
    {
        xNewState = APP_NETWORK_PHY_STATE_DOWN;
        pcMessage = "W5500 PHY: LINK DOWN\r\n";
    }

    if (xNewState == s_xPhyState)
    {
        return;
    }

    s_xPhyState = xNewState;

    prvBroadcastNetworkStatus();

    if (AppSerial_Write(
            (const uint8_t *)pcMessage,
            (uint16_t)strlen(pcMessage)) == false)
    {
        s_ulW5500ReportDropCount++;
    }
}

static TickType_t s_xLastPhyPollTick;   // 上一次轮询PHY链路的系统滴答值


static void prvNetworkTask(void *pvParameters)
{
    AppNetworkTaskContext_t *pxContext =
        (AppNetworkTaskContext_t *)pvParameters;
    TickType_t xNowTick;
    TickType_t xLastWakeTick;
    BspW5500NetworkConfig_t xReadbackConfig;

    configASSERT(pxContext != NULL);
    configASSERT(
        RtosW5500Access_IsReady(
            pxContext->pxW5500Access));
    configASSERT(pxContext->xMqttNetworkStatusQueue != NULL);
    configASSERT(pxContext->xFtpNetworkStatusQueue != NULL);

    s_xPhyState = APP_NETWORK_PHY_STATE_UNKNOWN;
    s_xW5500State = APP_NETWORK_W5500_STATE_INITIALIZING;
    prvBroadcastNetworkStatus();

    s_xW5500State = prvInitializeW5500(
        pxContext->pxW5500Access,
        &xReadbackConfig);

    prvReportW5500Result();

    if (s_xW5500State == APP_NETWORK_W5500_STATE_READY)
    {
        prvReportNetworkConfig(&xReadbackConfig);
    }

    prvBroadcastNetworkStatus();

    xLastWakeTick = xTaskGetTickCount();
    s_xLastPhyPollTick = xLastWakeTick;

    for (;;)
    {
        xNowTick = xTaskGetTickCount();

        if ((s_xW5500State == APP_NETWORK_W5500_STATE_READY) &&
            ((s_xPhyState == APP_NETWORK_PHY_STATE_UNKNOWN) ||
             ((TickType_t)(xNowTick - s_xLastPhyPollTick) >=
              pdMS_TO_TICKS(APP_NETWORK_PHY_POLL_PERIOD_MS))))
        {
            s_xLastPhyPollTick = xNowTick;
            prvPollPhyLink(pxContext->pxW5500Access);
        }

        vTaskDelayUntil(
            &xLastWakeTick,
            pdMS_TO_TICKS(
                APP_NETWORK_SERVICE_PERIOD_MS));
    }
}

bool AppNetworkTask_Create(
    RtosW5500Access_t *pxW5500Access,
    QueueHandle_t xMqttNetworkStatusQueue,
    QueueHandle_t xFtpNetworkStatusQueue)
{
    BaseType_t xCreateResult;

    if ((s_xNetworkTaskHandle != NULL) ||
        (RtosW5500Access_IsReady(pxW5500Access) == false) ||
        (xMqttNetworkStatusQueue == NULL) ||
        (xFtpNetworkStatusQueue == NULL))
    {
        return false;
    }

    s_xNetworkTaskContext.pxW5500Access = pxW5500Access;
    s_xNetworkTaskContext.xMqttNetworkStatusQueue =
        xMqttNetworkStatusQueue;
    s_xNetworkTaskContext.xFtpNetworkStatusQueue =
        xFtpNetworkStatusQueue;

    xCreateResult = xTaskCreate(
        prvNetworkTask,
        "NET",
        APP_NETWORK_TASK_STACK_DEPTH,
        &s_xNetworkTaskContext,
        APP_NETWORK_TASK_PRIORITY,
        &s_xNetworkTaskHandle);

    if (xCreateResult != pdPASS)
    {
        s_xNetworkTaskHandle = NULL;
        s_xNetworkTaskContext.pxW5500Access = NULL;
        s_xNetworkTaskContext.xMqttNetworkStatusQueue = NULL;
        s_xNetworkTaskContext.xFtpNetworkStatusQueue = NULL;
        return false;
    }

    return true;
}

AppNetworkW5500State_t AppNetwork_GetW5500State(void)
{
    return s_xW5500State;
}

uint8_t AppNetwork_GetW5500Version(void)
{
    return s_ucW5500Version;
}
