#ifndef APP_CAN_TASK_H
#define APP_CAN_TASK_H

#include <stdbool.h>

/**
 * @brief 配置当前协议过滤器并创建CAN应用任务。
 * @return true表示成功，false表示已创建、配置失败或Heap不足。
 */
bool AppCanTask_Create(void);


#endif /* APP_CAN_TASK_H */