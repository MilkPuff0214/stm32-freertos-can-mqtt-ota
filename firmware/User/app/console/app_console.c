#include "app_console.h"

#include <stddef.h>
#include <string.h>

/*
 * 行缓冲区最多保存63个有效字符。
 * 第64个字节始终留给C字符串结束符'\0'。
 */
#define APP_CONSOLE_LINE_MAX_LENGTH          63U

#define APP_CONSOLE_CHAR_BACKSPACE           0x08U      //删除表示的字符
#define APP_CONSOLE_CHAR_LINE_FEED           0x0AU      //换行符 ‘\n’
#define APP_CONSOLE_CHAR_CARRIAGE_RETURN     0x0DU      //回车符 ‘\r’
#define APP_CONSOLE_CHAR_PRINTABLE_MIN       0x20U      //可打印字符的最小ASCII码
#define APP_CONSOLE_CHAR_PRINTABLE_MAX       0x7EU      //可打印字符的最大ASCII码
#define APP_CONSOLE_CHAR_DELETE              0x7FU      //删除字符

static char s_cLineBuffer[APP_CONSOLE_LINE_MAX_LENGTH + 1U];  //一条命令行的缓冲区，包含C字符串结束符
static uint16_t s_usLineLength = 0U;  //当前已经保存多少个有效字符

/*
 * 一旦输入超过行缓冲区容量，就丢弃后续字符直到行结束。
 * 这样不会把一个被截断的长命令误认为有效命令。
 */
static bool s_xDiscardUntilEndOfLine = false;

/*
 * 用于识别CRLF组合。
 * CR已经执行命令后，紧随其后的LF必须被忽略。
 * CR：Carriage Return
 * LF：Line Feed
 */
static bool s_xPreviousByteWasCarriageReturn = false;

static const uint8_t s_ucHelpResponse[] =
    "Commands:\r\n"
    "  help     - list commands\r\n"
    "  version  - show console version\r\n"
    "  task     - show task state, stack and CPU\r\n"
    "  heap     - show FreeRTOS heap usage\r\n"
    "  data_query - show current temperature and voltage\r\n";

static const uint8_t s_ucVersionResponse[] =
    "STM32F103 FreeRTOS Console v0.2\r\n";

static const uint8_t s_ucUnknownCommandResponse[] =
    "ERROR: unknown command\r\n";

static const uint8_t s_ucLineTooLongResponse[] =
    "ERROR: line too long\r\n";

static void prvResetCurrentLine(void)
{
    s_usLineLength = 0U;
    s_cLineBuffer[0] = '\0';
    s_xDiscardUntilEndOfLine = false;

    /*
     * 此处不能清除s_xPreviousByteWasCarriageReturn，
     * 否则处理完CR后无法识别紧随其后的LF。
     */
}

/**
 * @brief 处理当前完整命令行并生成响应
 * @param  pOutput 输出对象，必须由调用者提供有效内存。
 */
static bool prvCompleteLine(AppConsoleOutput_t *pOutput)
{
    if (s_xDiscardUntilEndOfLine == true)
    {
        pOutput->eType = APP_CONSOLE_OUTPUT_TEXT;
        pOutput->pucData = s_ucLineTooLongResponse;
        pOutput->usLength =
            (uint16_t)(sizeof(s_ucLineTooLongResponse) - 1U);

        prvResetCurrentLine();
        return true;
    }

    /*
     * 行缓冲区始终预留了一个字节，
     * 因此这里写入字符串结束符不会越界。
     */
    s_cLineBuffer[s_usLineLength] = '\0';

    if (s_usLineLength == 0U)
    {
        /*
         * 空行不执行命令，也不产生错误响应。
         */
        prvResetCurrentLine();
        return false;
    }
    //strcmp比较两个C字符串是否相等，返回0表示相等
    if (strcmp(s_cLineBuffer, "help") == 0)
    {
        pOutput->eType = APP_CONSOLE_OUTPUT_TEXT;
        pOutput->pucData = s_ucHelpResponse;
        pOutput->usLength =
            (uint16_t)(sizeof(s_ucHelpResponse) - 1U);
    }
    else if (strcmp(s_cLineBuffer, "version") == 0)
    {
        pOutput->eType = APP_CONSOLE_OUTPUT_TEXT;
        pOutput->pucData = s_ucVersionResponse;
        pOutput->usLength =
            (uint16_t)(sizeof(s_ucVersionResponse) - 1U);
    }
    else if (strcmp(s_cLineBuffer, "task") == 0)
    {
        /*
        * Console只报告命令类型，不直接调用FreeRTOS。
        */
        pOutput->eType =
            APP_CONSOLE_OUTPUT_RTOS_TASK_LIST;
    }
    else if (strcmp(s_cLineBuffer, "heap") == 0)
    {
        /*
        * Console只报告Heap诊断请求，不直接依赖FreeRTOS。
        */
        pOutput->eType =
            APP_CONSOLE_OUTPUT_RTOS_HEAP;
    }
    else if (strcmp(s_cLineBuffer, "data_query") == 0)
    {
        /* Console只识别命令，ADC快照读取和格式化由调用者完成。 */
        pOutput->eType = APP_CONSOLE_OUTPUT_ADC_DATA;
    }
    else
    {
        pOutput->eType = APP_CONSOLE_OUTPUT_TEXT;
        pOutput->pucData = s_ucUnknownCommandResponse;
        pOutput->usLength =
            (uint16_t)(sizeof(s_ucUnknownCommandResponse) - 1U);
    }

    prvResetCurrentLine();
    return true;
}

/**
 * @brief 初始化Console行缓冲和协议状态。
 *
 * @note 由Serial RX Task在开始接收前调用一次。
 */
void AppConsole_Init(void)
{
    s_usLineLength = 0U;
    s_cLineBuffer[0] = '\0';
    s_xDiscardUntilEndOfLine = false;
    s_xPreviousByteWasCarriageReturn = false;
}

/**
 * @brief 向Console状态机提交一个接收字节。
 * @param ucByte 接收到的字节
 * @param pOutput 输出对象，必须由调用者提供有效内存。
 * @return true表示已经产生一条响应，此时pOutput有效；
 *         false表示一行尚未完成，或者当前无需输出。
 */
bool AppConsole_ProcessByte(
    uint8_t ucByte,
    AppConsoleOutput_t *pOutput)
{
    if (pOutput == NULL)
    {
        /*
         * 输出对象无效时不消费当前字节，
         * 避免Console状态已经改变但响应无法交给调用者。
         */
        return false;
    }

    /*
     * 默认没有输出，避免调用者误用上一次响应。
     */
    pOutput->eType = APP_CONSOLE_OUTPUT_NONE;
    pOutput->pucData = NULL;
    pOutput->usLength = 0U;

    if (ucByte == APP_CONSOLE_CHAR_CARRIAGE_RETURN)
    {
        s_xPreviousByteWasCarriageReturn = true;
        return prvCompleteLine(pOutput);
    }

    if (ucByte == APP_CONSOLE_CHAR_LINE_FEED)
    {
        if (s_xPreviousByteWasCarriageReturn == true)
        {
            /*
             * CR已经完成过本行，忽略CRLF中的LF。
             */
            s_xPreviousByteWasCarriageReturn = false;
            return false;
        }

        s_xPreviousByteWasCarriageReturn = false;
        return prvCompleteLine(pOutput);
    }

    /*
     * 当前字节不是LF，因此不能再与前面的CR组成CRLF。
     */
    s_xPreviousByteWasCarriageReturn = false;

    if (s_xDiscardUntilEndOfLine == true)
    {
        /*
         * 超长行只能通过下一个CR或LF恢复。
         * 即使收到退格，也不重新执行已被截断的前缀。
         */
        return false;
    }

    if ((ucByte == APP_CONSOLE_CHAR_BACKSPACE) ||
        (ucByte == APP_CONSOLE_CHAR_DELETE))
    {
        if (s_usLineLength > 0U)
        {
            s_usLineLength--;
            s_cLineBuffer[s_usLineLength] = '\0';
        }

        return false;
    }

    if ((ucByte >= APP_CONSOLE_CHAR_PRINTABLE_MIN) &&
        (ucByte <= APP_CONSOLE_CHAR_PRINTABLE_MAX))
    {
        if (s_usLineLength < APP_CONSOLE_LINE_MAX_LENGTH)
        {
            s_cLineBuffer[s_usLineLength] = (char)ucByte;
            s_usLineLength++;
            s_cLineBuffer[s_usLineLength] = '\0';
        }
        else
        {
            /*
             * 第64个可打印字符无法保存，进入整行丢弃状态。
             */
            s_xDiscardUntilEndOfLine = true;
        }
    }

    /*
     * 其他控制字符暂时忽略，不加入命令行。
     */
    return false;
}
