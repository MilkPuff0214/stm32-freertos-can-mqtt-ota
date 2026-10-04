#ifndef BSP_W5500_H
#define BSP_W5500_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_W5500_EXPECTED_VERSION    0x04U

/**
 * @brief 初始化SPI2、W5500控制引脚和ioLibrary回调。
 *
 * 初始化完成后RST仍保持低电平，调用者必须等待至少500 us，
 * 再通过BspW5500_SetResetAsserted(false)释放复位。
 */
void BspW5500_InterfaceInit(void);

/**
 * @brief 控制W5500低有效硬件复位。
 * @param xAsserted true表示拉低RST并保持复位，false表示释放复位。
 */
void BspW5500_SetResetAsserted(bool xAsserted);

/**
 * @brief 通过官方ioLibrary读取VERSIONR。
 * @param pucVersion 成功时保存芯片版本寄存器值。
 * @return true表示完整SPI事务成功，false表示参数无效或SPI超时。
 *
 * @note 只能由Network Task调用，不支持并发访问。
 */
bool BspW5500_ReadVersion(uint8_t *pucVersion);

/**
 * @brief 查询低有效INT引脚是否已经置位。
 */
bool BspW5500_IsInterruptAsserted(void);



#define BSP_W5500_MAC_ADDRESS_LENGTH    6U
#define BSP_W5500_IPV4_OCTET_COUNT      4U

typedef struct
{
    uint8_t aucMac[BSP_W5500_MAC_ADDRESS_LENGTH];
    uint8_t aucIp[BSP_W5500_IPV4_OCTET_COUNT];
    uint8_t aucSubnetMask[BSP_W5500_IPV4_OCTET_COUNT];
    uint8_t aucGateway[BSP_W5500_IPV4_OCTET_COUNT];
} BspW5500NetworkConfig_t;


typedef enum
{
    BSP_W5500_TCP_CLOSED = 0,
    BSP_W5500_TCP_INIT,
    BSP_W5500_TCP_CONNECTING,
    BSP_W5500_TCP_ESTABLISHED,
    BSP_W5500_TCP_PEER_CLOSED_SEND,
    BSP_W5500_TCP_CLOSING,
    BSP_W5500_TCP_OTHER
} BspW5500TcpState_t;

typedef struct
{
    BspW5500TcpState_t xState;
    uint8_t ucRawState;
    bool xTimedOut;
} BspW5500TcpStatus_t;

typedef enum
{
    BSP_W5500_OPERATION_ERROR = -1, //W5500操作错误，SPI事务失败，还包括参数、Socket状态和缓冲配置错误
    BSP_W5500_OPERATION_BUSY = 0,   //W5500操作忙，当前命令未完成,还可能表示TX空间不足或动态寄存器暂时不稳定
    BSP_W5500_OPERATION_STARTED = 1
} BspW5500OperationResult_t;        //表示操作是否成功启动

typedef enum
{
    BSP_W5500_IO_ERROR = -1,
    BSP_W5500_IO_IDLE = 0,
    BSP_W5500_IO_BUSY = 1,
    BSP_W5500_IO_COMPLETE = 2
} BspW5500IoResult_t;   //收发检查或处理的结果

/*
 * 执行W5500软件复位及Socket缓冲区基础初始化。
 * 只能由Network Task调用。
 */
bool BspW5500_ChipInit(void);

bool BspW5500_SetNetworkConfig(
    const BspW5500NetworkConfig_t *pxConfig);

bool BspW5500_GetNetworkConfig(
    BspW5500NetworkConfig_t *pxConfig);

/*
 * LINK UP只证明网线和对端PHY完成协商，不证明IP或TCP可用。
 */
bool BspW5500_GetPhyLink(bool *pxLinkUp);


//不走完整FIN协商，直接关闭本地Socket,这条连接不要了，清理本地资源
BspW5500OperationResult_t BspW5500_TcpCloseStart(uint8_t ucSocket);

BspW5500OperationResult_t BspW5500_TcpOpenStart(
    uint8_t ucSocket,
    uint16_t usLocalPort);

BspW5500OperationResult_t BspW5500_TcpConnectStart(
    uint8_t ucSocket,
    const uint8_t *pucServerIp,
    uint16_t usServerPort); // 启动TCP连接，非阻塞，返回操作结果

//发送FIN，按TCP流程与对端完成断开
BspW5500OperationResult_t BspW5500_TcpDisconnectStart(
    uint8_t ucSocket);

bool BspW5500_TcpReadStatus(
    uint8_t ucSocket,
    BspW5500TcpStatus_t *pxStatus);


//Sn_IR，即Socket中断/事件寄存器
//清理连接事件
bool BspW5500_TcpClearConnectedEvent(uint8_t ucSocket);
//清理所有事件
bool BspW5500_TcpClearEvents(uint8_t ucSocket);

/*
 * Receive返回COMPLETE时*pucBuffer中有*pusReceivedLength字节；
 * IDLE表示稳定确认当前没有数据，BUSY表示下周期重试。
 */
BspW5500IoResult_t BspW5500_TcpReceive(
    uint8_t ucSocket,
    uint8_t *pucBuffer,
    uint16_t usCapacity,
    uint16_t *pusReceivedLength);

/* 数据会在返回STARTED前复制到W5500 TX缓冲区。 */
BspW5500OperationResult_t BspW5500_TcpSendStart(
    uint8_t ucSocket,
    const uint8_t *pucData,
    uint16_t usLength);

/* COMPLETE表示观察到并清除了SENDOK；BUSY表示发送尚未结束。 */
BspW5500IoResult_t BspW5500_TcpPollSend(uint8_t ucSocket);

#endif /* BSP_W5500_H */
