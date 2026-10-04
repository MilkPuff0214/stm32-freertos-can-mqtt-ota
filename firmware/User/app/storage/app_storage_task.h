#ifndef APP_STORAGE_TASK_H
#define APP_STORAGE_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "app_storage_layout.h"
#include "app_storage_metadata.h"

#define APP_STORAGE_REQUEST_QUEUE_LENGTH    2U
#define APP_STORAGE_STATUS_QUEUE_LENGTH     1U
#define APP_STORAGE_WRITE_BLOCK_SIZE           512U

typedef enum
{
    APP_STORAGE_REQUEST_QUERY_STATUS = 0,   //请求重新查询存储状态
    APP_STORAGE_REQUEST_PREPARE_DOWNLOAD,   //准备接收一个新固件,携带的数据为AppStoragePrepareRequest_t
    APP_STORAGE_REQUEST_WRITE_BLOCK,         //写入一个数据块,携带的数据为AppStorageWriteRequest_t
    APP_STORAGE_REQUEST_FINISH_DOWNLOAD,     //完成固件下载；长度由Storage自身的已写长度判定
    APP_STORAGE_REQUEST_ABORT_DOWNLOAD,      //中止当前固件下载，不携带额外数据
    APP_STORAGE_REQUEST_START_UPGRADE,     /* 持久受理升级，携带xUpgrade。 */
    APP_STORAGE_REQUEST_CANCEL_UPGRADE     /* 取消未放行复位的升级，携带xUpgrade。 */
} AppStorageRequestType_t;      //FTP下载请求或MQTT业务Task的升级控制请求

typedef struct
{
    uint32_t ulFirmwareLength;
    uint8_t aucExpectedFirmwareMd5[APP_STORAGE_MD5_DIGEST_SIZE];
} AppStoragePrepareRequest_t;

typedef struct
{
    uint16_t usLength;
    uint8_t aucData[APP_STORAGE_WRITE_BLOCK_SIZE];
} AppStorageWriteRequest_t;

typedef struct
{
    /* 运行期非零请求编号；不使用固定messageId或FTP事务号。 */
    uint32_t ulRequestId;
    /* 业务Peek观察到的已提交代次；0也可能是合法回绕结果。 */
    uint32_t ulExpectedGeneration;
} AppStorageUpgradeRequest_t;

typedef enum
{
    APP_STORAGE_STATE_INITIALIZING = 0,     /* Storage Task正在初始化W25Q64并读取元数据 */
    APP_STORAGE_STATE_IDLE,     /* 当前没有下载事务 */
    APP_STORAGE_STATE_PREPARING,          /* 准备下载：正在使旧候选失效并擦除新固件需要的区域 */
    APP_STORAGE_STATE_READY_FOR_DATA,       /* 准备好接收下一条WRITE_BLOCK */
    APP_STORAGE_STATE_WRITING,               /* 正在把当前数据块写入W25Q64并读回校验 */
    APP_STORAGE_STATE_VERIFYING,    /* 已收到FINISH_DOWNLOAD，正在校验长度、描述头和MD5 */
    APP_STORAGE_STATE_READY_TO_UPGRADE,    /* W25Q64中存在一个完整且校验通过的候选固件 */
    APP_STORAGE_STATE_UPGRADE_LOCKED,    /* 候选固件正在被升级流程使用，不允许新下载覆盖 */
    APP_STORAGE_STATE_FAILED,             /* 当前下载事务失败。允许重新开始新的下载，不运行升级 */
    APP_STORAGE_STATE_DEVICE_FAULT       /* W25Q64识别、SPI或器件本身存在故障 */
} AppStorageState_t;    //AppStorageTask的运行状态

typedef enum
{
    APP_STORAGE_ERROR_NONE = 0,
    APP_STORAGE_ERROR_SPI,
    APP_STORAGE_ERROR_JEDEC_ID,
    APP_STORAGE_ERROR_STATE_CONFLICT,   /* 当前运行状态不接受该请求。 */
    APP_STORAGE_ERROR_TRANSACTION_MISMATCH, /* 请求不属于当前下载事务。 */
    APP_STORAGE_ERROR_FIRMWARE_LENGTH,
    APP_STORAGE_ERROR_BLOCK_LENGTH,
    APP_STORAGE_ERROR_ERASE_START,
    APP_STORAGE_ERROR_ERASE_TIMEOUT,
    APP_STORAGE_ERROR_PROGRAM_START,
    APP_STORAGE_ERROR_PROGRAM_TIMEOUT,
    APP_STORAGE_ERROR_READBACK,
    APP_STORAGE_ERROR_FIRMWARE_HEADER,  /* application.bin前512字节描述头非法。 */
    APP_STORAGE_ERROR_FIRMWARE_MD5,     /* MQTT期望值与实际固件MD5不一致。 */
    APP_STORAGE_ERROR_MD5_ENGINE,       /* Mbed TLS MD5接口执行失败。 */
    APP_STORAGE_ERROR_ABORTED,
    APP_STORAGE_ERROR_INTERRUPTED,
    APP_STORAGE_ERROR_METADATA,
    APP_STORAGE_ERROR_COUNT
} AppStorageError_t;    //AppStorageTask可能返回的错误码

typedef struct
{
    AppStorageRequestType_t xType;
    uint32_t ulTransactionId;
    union
    {
        AppStoragePrepareRequest_t xPrepare;
        AppStorageWriteRequest_t xWrite;
        AppStorageUpgradeRequest_t xUpgrade;
    } xData;
} AppStorageRequest_t;  /* Storage Task接收的请求结构体 */

typedef struct
{
    AppStorageState_t xState;   /* 运行状态，只存在RAM中，对外报告当前状态 */
    AppStorageError_t xError;
    uint32_t ulTransactionId;
    uint32_t ulFirmwareLength;  /* 当前升级事务的原始固件总长度 */
    uint32_t ulErasedLength;
    uint32_t ulWrittenLength;
    uint8_t ucManufacturer;          //读取到的JEDEC厂商编号 0xEF
    uint8_t ucMemoryType;            //读取到的JEDEC存储器类型 0x40
    uint8_t ucCapacity;              //读取到的JEDEC存储器容量 0x17

    /* 仅由Storage已选中的有效记录派生；读者不能据此忽略DEVICE_FAULT。 */
    bool xMetadataValid;
    AppStoragePersistentState_t xPersistentState;
    uint32_t ulMetadataGeneration;
    uint32_t ulPersistentError;    /* 保留原始Boot/P3数值，不截断为P3错误枚举。 */
    uint32_t ulInstallAttempts;
    uint32_t ulBootAttempts;

    /* 最近已完成的升级控制请求；普通FTP/QUERY发布不覆盖。 */
    uint32_t ulCompletedUpgradeRequestId;
    AppStorageError_t xUpgradeRequestError;
} AppStorageStatus_t;       /* 发布的状态结构体 */

_Static_assert(sizeof(AppStorageRequest_t) <= 528U,
               "AppStorageRequest_t exceeds the reviewed Queue budget");

bool AppStorageTask_Create(QueueHandle_t xRequestQueue, QueueHandle_t xStatusQueue);

#endif /* APP_STORAGE_TASK_H */
