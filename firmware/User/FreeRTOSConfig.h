#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "system_stm32f10x.h"
#include "bsp_run_time_counter.h"

#define configCPU_CLOCK_HZ                ( SystemCoreClock )   //让FreeRTOS取得当前72 MHz内核时钟。
#define configTICK_RATE_HZ                 1000U        //设置1000 Hz，即1 ms/Tick
#define configTICK_TYPE_WIDTH_IN_BITS      TICK_TYPE_WIDTH_32_BITS

/* Scheduler configuration. */
#define configUSE_PREEMPTION                      1      //使用抢占式调度
#define configUSE_TIME_SLICING                    1     //同优先级Ready任务可以按Tick轮转
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1     //使用Cortex-M3的 CLZ 指令快速寻找最高Ready优先级
#define configMAX_PRIORITIES                      5U    //最大优先级数为5，优先级范围为0~4(决定“有多少个任务优先级档位”)
#define configMAX_TASK_NAME_LEN                   16U   //任务名最大长度为15字节
#define configIDLE_SHOULD_YIELD                   1     //空闲任务是否让出CPU给同优先级的Ready任务
#define configUSE_TICKLESS_IDLE                   0     //暂不加入低功耗Tick补偿


/* Memory allocation configuration. */
#define configSUPPORT_DYNAMIC_ALLOCATION           1       //支持动态内存分配
#define configSUPPORT_STATIC_ALLOCATION            0       //不支持静态内存分配 
/*
Idle Task的TCB和栈
LED Task的TCB和栈
分配器管理信息
剩余空闲空间
 */
/* FTP双TCP Task和三条Queue加入后，保留足够的启动与运行期Heap余量。 */
#define configTOTAL_HEAP_SIZE                      ( 32U * 1024U )
#define configMINIMAL_STACK_SIZE                   128U     //Idle Task栈大小:128 * 4  = 512 bytes，单位为words，1 word=4 bytes
#define configAPPLICATION_ALLOCATED_HEAP           0        //表示由 heap_4.c 自己定义 ucHeap[]
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP  0        //表示任务的TCB和任务栈都从同一个FreeRTOS Heap分配
#define configHEAP_CLEAR_MEMORY_ON_FREE            0        //释放内存时不清零，减少执行开销


/* Cortex-M3 interrupt configuration. */
#define configPRIO_BITS                               4U        //当前芯片的中断优先级位数为4位，优先级范围为0~15
/*
逻辑优先级5是允许调用FromISR API的最高紧急程度；
逻辑5～15允许，逻辑0～4禁止 
 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5U       
#define configMAX_SYSCALL_INTERRUPT_PRIORITY          \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8U - configPRIO_BITS ) )

/* Direct exception handler routing. */
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler


/* Assertion and hook configuration. */
#define configUSE_IDLE_HOOK                   0     //暂不实现Idle回调，但Idle Task仍然存在
#define configUSE_TICK_HOOK                   0     //暂不实现Tick回调，但SysTick仍然运行。
#define configUSE_MALLOC_FAILED_HOOK          1
#define configCHECK_FOR_STACK_OVERFLOW        2
#define configCHECK_HANDLER_INSTALLATION      1     //启动调度器时检查SVC、PendSV是否正确接入向量表

/* 断言失败时屏蔽逻辑优先级5～15，并停在循环中保留GDB现场。 */
#define configASSERT( condition )                         \
    do                                                    \
    {                                                     \
        if( ( condition ) == 0 )                          \
        {                                                 \
            taskDISABLE_INTERRUPTS();                     \
            for( ; ; )                                    \
            {                                             \
            }                                             \
        }                                                 \
    } while( 0 )


/* Optional kernel feature configuration. */
#define configUSE_TASK_NOTIFICATIONS            1  /* 任务通知，增加每个TCB的通知字段。 */
#define configUSE_MUTEXES                       1   /* queue.c已编译；启用普通Mutex和优先级继承。 */
#define configUSE_RECURSIVE_MUTEXES             0  /* 递归Mutex关闭。 */
#define configUSE_COUNTING_SEMAPHORES            0  /* 不使用计数信号量。 */
#define configUSE_QUEUE_SETS                     0  /* 暂不使用Queue Set。 */
#define configUSE_TIMERS                         0  /* 未加入timers.c，不创建Timer Task和Timer Queue。 */
#define configUSE_EVENT_GROUPS                   0  /* 未加入event_groups.c，暂不使用事件组。 */
#define configUSE_STREAM_BUFFERS                 0  /* 未加入stream_buffer.c，暂不使用流/消息缓冲区。 */
#define configUSE_CO_ROUTINES                    0  /* 未加入croutine.c，本项目使用普通Task。 */
#define configUSE_NEWLIB_REENTRANT               0  /* 不为每个TCB分配Newlib重入结构，减少RAM占用。 */
#define configUSE_TRACE_FACILITY                 1  /* 启用TaskStatus_t和任务状态快照接口,会增加任务控制块(TCB)的大小。 */
#define configUSE_STATS_FORMATTING_FUNCTIONS     1  /* 启用任务列表等文本格式化诊断接口。 */
#define configGENERATE_RUN_TIME_STATS            1  /* 让每个任务的TCB累计Running状态时间 */
#define configRUN_TIME_COUNTER_TYPE              uint64_t   /* 任务累计时间和系统总时间使用64位。 */
/*
 * FreeRTOS启动调度器时自动初始化TIM6。
 * 每次任务切换时读取当前运行时间计数。
 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() \
    BspRunTimeCounter_Init()

#define portGET_RUN_TIME_COUNTER_VALUE()         \
    BspRunTimeCounter_GetValue()

#define configQUEUE_REGISTRY_SIZE                0  /* 当前没有Queue/Semaphore需要注册给调试器。 */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS  0  /* 不在每个TCB中预留应用线程局部指针。 */
#define configUSE_APPLICATION_TASK_TAG           0  /* 不为任务增加应用Tag/Hook字段。 */
#define configUSE_POSIX_ERRNO                    0  /* 不在每个TCB中保存任务级FreeRTOS_errno。 */

/* API inclusion configuration. */
#define INCLUDE_vTaskDelay                       1   /* 编译vTaskDelay()，供Key Task周期扫描时阻塞等待。 */
#define INCLUDE_xTaskDelayUntil                  1   /* 编译固定周期任务使用的xTaskDelayUntil()。 */
#define INCLUDE_uxTaskGetStackHighWaterMark      1  /* 启用历史最小剩余任务栈查询，单位为words。 */
#define INCLUDE_uxTaskGetStackHighWaterMark2     0  /* 不重复启用返回configSTACK_DEPTH_TYPE的第二套API。 */

#endif /* FREERTOS_CONFIG_H */
