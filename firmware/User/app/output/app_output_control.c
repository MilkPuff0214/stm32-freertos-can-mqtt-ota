#include "app_output_control.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "bsp_buzzer.h"
#include "bsp_led.h"

static SemaphoreHandle_t s_xOutputMutex = NULL;
static AppOutputState_t s_xOutputState = {0};
static bool s_xInitialized = false;

static void prvWriteHardware(const AppOutputState_t *pxState)
{
    if (pxState->xRedLedOn)
    {
        LED1_ON;
    }
    else
    {
        LED1_OFF;
    }

    BspBuzzer_Set(pxState->xBuzzerOn);
}

bool AppOutputControl_Init(void)
{
    if (s_xInitialized || (s_xOutputMutex != NULL))
    {
        return false;
    }

    s_xOutputMutex = xSemaphoreCreateMutex();
    if (s_xOutputMutex == NULL)
    {
        return false;
    }

    s_xOutputState.xRedLedOn = false;
    s_xOutputState.xBuzzerOn = false;
    prvWriteHardware(&s_xOutputState);
    s_xInitialized = true;
    return true;
}

bool AppOutputControl_SetAll(bool xRedLedOn, bool xBuzzerOn,
                             AppOutputState_t *pxAppliedState)
{
    if ((pxAppliedState == NULL) || (s_xInitialized == false))
    {
        return false;
    }

    if (xSemaphoreTake(s_xOutputMutex, portMAX_DELAY) != pdPASS)
    {
        return false;
    }
    s_xOutputState.xRedLedOn = xRedLedOn;
    s_xOutputState.xBuzzerOn = xBuzzerOn;
    prvWriteHardware(&s_xOutputState);
    *pxAppliedState = s_xOutputState;
    (void)xSemaphoreGive(s_xOutputMutex);
    return true;
}

bool AppOutputControl_SetOne(AppOutputTarget_t xTarget, bool xOn,
                             AppOutputState_t *pxAppliedState)
{
    if ((pxAppliedState == NULL) || (s_xInitialized == false) ||
        ((xTarget != APP_OUTPUT_TARGET_RED_LED) &&
         (xTarget != APP_OUTPUT_TARGET_BUZZER)))
    {
        return false;
    }

    if (xSemaphoreTake(s_xOutputMutex, portMAX_DELAY) != pdPASS)
    {
        return false;
    }
    if (xTarget == APP_OUTPUT_TARGET_RED_LED)
    {
        s_xOutputState.xRedLedOn = xOn;
    }
    else
    {
        s_xOutputState.xBuzzerOn = xOn;
    }
    prvWriteHardware(&s_xOutputState);
    *pxAppliedState = s_xOutputState;
    (void)xSemaphoreGive(s_xOutputMutex);
    return true;
}

bool AppOutputControl_GetState(AppOutputState_t *pxState)
{
    if ((pxState == NULL) || (s_xInitialized == false))
    {
        return false;
    }

    if (xSemaphoreTake(s_xOutputMutex, portMAX_DELAY) != pdPASS)
    {
        return false;
    }
    *pxState = s_xOutputState;
    (void)xSemaphoreGive(s_xOutputMutex);
    return true;
}