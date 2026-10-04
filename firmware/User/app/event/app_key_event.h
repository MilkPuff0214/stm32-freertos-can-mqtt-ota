#ifndef APP_KEY_EVENT_H
#define APP_KEY_EVENT_H

/*
 * Key Task发送给其他应用任务的按键事件。
 * 当前只产生稳定按下事件，释放状态由消抖状态机内部处理。
 */
typedef enum
{
    APP_KEY_EVENT_KEY1_PRESSED = 0,
    APP_KEY_EVENT_KEY2_PRESSED
} AppKeyEvent_t;

#endif /* APP_KEY_EVENT_H */