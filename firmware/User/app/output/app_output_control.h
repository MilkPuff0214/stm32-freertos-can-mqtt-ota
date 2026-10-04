#ifndef APP_OUTPUT_CONTROL_H
#define APP_OUTPUT_CONTROL_H

#include <stdbool.h>

typedef enum
{
    APP_OUTPUT_TARGET_RED_LED = 0,
    APP_OUTPUT_TARGET_BUZZER
} AppOutputTarget_t;

typedef struct
{
    bool xRedLedOn;
    bool xBuzzerOn;
} AppOutputState_t;

bool AppOutputControl_Init(void);
bool AppOutputControl_SetAll(bool xRedLedOn, bool xBuzzerOn,
                             AppOutputState_t *pxAppliedState);
bool AppOutputControl_SetOne(AppOutputTarget_t xTarget, bool xOn,
                             AppOutputState_t *pxAppliedState);
bool AppOutputControl_GetState(AppOutputState_t *pxState);

#endif /* APP_OUTPUT_CONTROL_H */