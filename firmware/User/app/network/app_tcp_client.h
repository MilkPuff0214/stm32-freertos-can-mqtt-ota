#ifndef APP_TCP_CLIENT_H
#define APP_TCP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "rtos_w5500_access.h"

#define APP_TCP_CLIENT_IO_ERROR          (-1)
#define APP_TCP_CLIENT_IO_PEER_CLOSED    (-2)

#define APP_TCP_CLIENT_TX_CHUNK_LIMIT    1024U
#define APP_TCP_CLIENT_RETRY_BASE_MS     1000U
#define APP_TCP_CLIENT_RETRY_MAX_MS      30000U

typedef enum
{
    APP_TCP_CLIENT_STATE_WAIT_LINK = 0,
    APP_TCP_CLIENT_STATE_FORCE_CLOSE_REQUEST,   // 请求强制关闭
    APP_TCP_CLIENT_STATE_FORCE_CLOSE_WAIT,    // 等待强制关闭完成
    APP_TCP_CLIENT_STATE_OPEN_REQUEST,         // 请求打开TCP连接
    APP_TCP_CLIENT_STATE_OPEN_WAIT,            // 等待TCP连接打开完成
    APP_TCP_CLIENT_STATE_CONNECT_REQUEST,      // 请求建立TCP连接
    APP_TCP_CLIENT_STATE_CONNECTING,           // 正在建立TCP连接
    APP_TCP_CLIENT_STATE_CONNECTED,            // TCP连接已建立
    APP_TCP_CLIENT_STATE_PEER_FIN,             // 对端发起关闭
    APP_TCP_CLIENT_STATE_DISCONNECT_REQUEST,   // 请求断开TCP连接
    APP_TCP_CLIENT_STATE_DISCONNECTING,        // 正在断开TCP连接
    APP_TCP_CLIENT_STATE_BACKOFF               // 退避状态
} AppTcpClientState_t;

typedef struct
{
    uint8_t ucSocket;
    uint8_t aucServerIp[4];
    uint16_t usServerPort;
    uint16_t usLocalPortFirst;
    uint16_t usLocalPortLast;
    uint32_t ulLocalActionTimeoutMs;
    uint32_t ulConnectTimeoutMs;
    uint32_t ulSendTimeoutMs;
    uint32_t ulPeerDrainTimeoutMs;
    uint32_t ulDisconnectTimeoutMs;
    bool xStartEnabled;
} AppTcpClientConfig_t;

typedef struct
{
    AppTcpClientConfig_t xConfig;
    /* 由AppStartup拥有；全部TCP硬件操作通过该对象互斥。 */
    RtosW5500Access_t *pxW5500Access;
    AppTcpClientState_t xState;     //TCP连接生命周期
    TickType_t xPhaseStartTick;          // 当前阶段开始的系统滴答值
    TickType_t xTxStartTick;
    TickType_t xSendBusyStartTick;
    TickType_t xReceiveBusyStartTick;
    uint32_t ulSessionGeneration;   // 会话生成计数，每次建立新会话时递增
    uint32_t ulConsecutiveFailures;
    uint32_t ulRetryDelayMs;
    uint16_t usNextLocalPort;
    uint16_t usActiveLocalPort;
    uint8_t ucPostCloseAction;  // 关闭后的后续动作
    bool xInitialized;
    bool xPhyWasUp;     // 物理链路是否曾经处于UP状态
    bool xTxInFlight;       //W5500已经接受一块发送数据，但本次SEND操作还没完成，正在等待SENDOK或超时，xTxInFlight为true表示发送操作仍在进行中
    bool xPeerReceiveDrained; //已收到对端FIN，并且FIN之前遗留在W5500 RX缓冲区的数据已经全部取完
    bool xGracefulCloseRequested; // 已请求优雅关闭
    bool xReconnectRequested; // 当前连接已经不可继续使用，要求Service强制关闭后重新连接
    bool xSessionMarkedStable; // 会话已被标记为稳定
    /* BUSY期限只约束暂时不能访问缓冲的情形，不把正常RX空闲当故障。 */
    bool xSendBusy;          // 新数据尚未提交给W5500， BspW5500_TcpSendStart()持续返回BUSY
    bool xReceiveBusy;       // W5500 RX缓冲区暂时不可用，BspW5500_TcpReceive()持续返回BUSY
    bool xEnabled;
} AppTcpClient_t;

bool AppTcpClient_Init(
    AppTcpClient_t *pxClient,
    const AppTcpClientConfig_t *pxConfig,
    RtosW5500Access_t *pxW5500Access);

void AppTcpClient_Service(
    AppTcpClient_t *pxClient,
    bool xPhyLinkUp,
    TickType_t xNowTick);

AppTcpClientState_t AppTcpClient_GetState(
    const AppTcpClient_t *pxClient);

uint32_t AppTcpClient_GetSessionGeneration(
    const AppTcpClient_t *pxClient);

uint16_t AppTcpClient_GetActiveLocalPort(
    const AppTcpClient_t *pxClient);

uint32_t AppTcpClient_GetRetryDelayMs(
    const AppTcpClient_t *pxClient);

uint32_t AppTcpClient_GetConsecutiveFailureCount(
    const AppTcpClient_t *pxClient);

/*
 * 只由独占本TCP Client的协议Task调用，不访问W5500、不获取Mutex。
 * true仅表示当前有效连接没有SEND在途；调用者仍须核对原会话编号。
 */
bool AppTcpClient_IsSendDrained(const AppTcpClient_t *pxClient);

/*
 * 只能由上层协议在“会话已被证明健康”后调用。
 * TCP ESTABLISHED本身不会自动清除连续失败历史。
 */
void AppTcpClient_MarkSessionStable(AppTcpClient_t *pxClient);

/*
 * coreMQTT兼容语义：
 * >0为本次接受/读取的字节数，0为暂时不可用，<0为连接错误。
 */
int32_t AppTcpClient_Send(
    AppTcpClient_t *pxClient,
    const void *pvData,
    size_t xDataLength);

int32_t AppTcpClient_Receive(
    AppTcpClient_t *pxClient,
    void *pvBuffer,
    size_t xBufferLength);

void AppTcpClient_RequestGracefulClose(AppTcpClient_t *pxClient);

void AppTcpClient_RequestReconnect(AppTcpClient_t *pxClient);

void AppTcpClient_SetEnabled(AppTcpClient_t *pxClient, bool xEnabled);
bool AppTcpClient_SetServerEndpoint(AppTcpClient_t *pxClient,
                                    const uint8_t pucServerIp[4],
                                    uint16_t usServerPort);

#endif /* APP_TCP_CLIENT_H */
