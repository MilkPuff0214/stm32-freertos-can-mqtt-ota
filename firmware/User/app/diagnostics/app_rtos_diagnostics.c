#include "app_rtos_diagnostics.h"

#include <stddef.h> // for size_t
#include <string.h> // for memcpy() and strlen()
#include <stdio.h>  // for snprintf()

#include "FreeRTOS.h"
#include "task.h"

/*
 * 报告包含任务状态表和运行时间表。
 * 当前只有Serial RX Task调用，并由AppSerial_Write()立即复制。
 */
#define APP_RTOS_TASK_REPORT_BUFFER_LENGTH        512U

static const char s_cTaskStateHeader[] =
    "Task State/Stack:\r\n"
    "Name\t\tState\tPrio\tStackFree\tTaskNo\r\n";

static const char s_cRunTimeStatsHeader[] =
    "\r\nTask Run Time:\r\n"
    "Name\t\tRunCount(100us)\tCPU\r\n";
/*
 * 使用静态缓冲区，避免把256字节报告放进Serial RX Task栈。
 * 后续仅允许Serial RX Task触发该诊断命令。
 */
static char s_cTaskReportBuffer[APP_RTOS_TASK_REPORT_BUFFER_LENGTH];

#define APP_RTOS_HEAP_REPORT_BUFFER_LENGTH        128U

/*
 * Heap报告较短，使用独立静态缓冲区保持所有权清晰，
 * 不需要为了节省少量RAM复用任务报告缓冲区。
 */
static char s_cHeapReportBuffer[APP_RTOS_HEAP_REPORT_BUFFER_LENGTH];

bool AppRtosDiagnostics_BuildTaskList(
    AppRtosDiagnosticsReport_t *pReport)
{
    size_t xBodyLength;
    size_t xUsedLength;     //已经写入缓冲区的字节数  
    size_t xRunTimeHeaderLength;

    if (pReport == NULL)
    {
        return false;
    }

    pReport->pucData = NULL;
    pReport->usLength = 0U;

    /*
     * 先写入状态表表头，再让FreeRTOS从表头后面写正文。
     */
    memcpy(
        s_cTaskReportBuffer,
        s_cTaskStateHeader,
        sizeof(s_cTaskStateHeader) - 1U);

    xUsedLength = sizeof(s_cTaskStateHeader) - 1U;

    vTaskListTasks(
        &s_cTaskReportBuffer[xUsedLength],
        sizeof(s_cTaskReportBuffer) - xUsedLength);

    xBodyLength =
        strlen(&s_cTaskReportBuffer[xUsedLength]);

    if (xBodyLength == 0U)
    {
        return false;
    }

    xUsedLength += xBodyLength;

    /*
     * 保证运行时间表头后至少还剩一个字节，
     * 供FreeRTOS写入字符串结束符。
     */
    xRunTimeHeaderLength =
        sizeof(s_cRunTimeStatsHeader) - 1U;

    if ((xUsedLength + xRunTimeHeaderLength) >=
        sizeof(s_cTaskReportBuffer))
    {
        return false;
    }

    memcpy(
        &s_cTaskReportBuffer[xUsedLength],
        s_cRunTimeStatsHeader,
        xRunTimeHeaderLength);

    xUsedLength += xRunTimeHeaderLength;

    vTaskGetRunTimeStatistics(
        &s_cTaskReportBuffer[xUsedLength],
        sizeof(s_cTaskReportBuffer) - xUsedLength);

    xBodyLength =
        strlen(&s_cTaskReportBuffer[xUsedLength]);

    if (xBodyLength == 0U)
    {
        return false;
    }

    xUsedLength += xBodyLength;

    pReport->pucData =
        (const uint8_t *)s_cTaskReportBuffer;
    pReport->usLength = (uint16_t)xUsedLength;

    return true;
}

bool AppRtosDiagnostics_BuildHeapReport(
    AppRtosDiagnosticsReport_t *pReport)
{
    size_t xFreeHeapSize;   //当前FreeRTOS Heap空闲总量
    size_t xMinimumEverFreeHeapSize;    //系统启动以来出现过的最低空闲量，是历史低水位
    int iWrittenLength;

    if (pReport == NULL)
    {
        return false;
    }

    pReport->pucData = NULL;
    pReport->usLength = 0U;

    xFreeHeapSize = xPortGetFreeHeapSize();
    xMinimumEverFreeHeapSize =
        xPortGetMinimumEverFreeHeapSize();

    iWrittenLength = snprintf(
        s_cHeapReportBuffer,
        sizeof(s_cHeapReportBuffer),
        "FreeRTOS Heap (bytes):\r\n"
        "Total\tFree\tMinEverFree\r\n"
        "%lu\t%lu\t%lu\r\n",
        (unsigned long)configTOTAL_HEAP_SIZE,
        (unsigned long)xFreeHeapSize,
        (unsigned long)xMinimumEverFreeHeapSize);

    /*
     * snprintf()返回本来需要写入的字符数，不包含结尾的'\0'。
     * 返回值达到缓冲区容量表示输出被截断，不能发送残缺报告。
     */
    if ((iWrittenLength < 0) ||
        ((size_t)iWrittenLength >=
         sizeof(s_cHeapReportBuffer)))
    {
        return false;
    }

    pReport->pucData =
        (const uint8_t *)s_cHeapReportBuffer;
    pReport->usLength =
        (uint16_t)iWrittenLength;

    return true;
}