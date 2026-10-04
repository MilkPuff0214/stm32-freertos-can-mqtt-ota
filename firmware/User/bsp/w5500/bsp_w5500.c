#include "bsp_w5500.h"

#include <stddef.h>

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_spi.h"
#include "wizchip_conf.h"
#include <string.h>

#define BSP_W5500_SPI                       SPI2
#define BSP_W5500_SPI_CLOCK                 RCC_APB1Periph_SPI2
#define BSP_W5500_SPI_GPIO_PORT             GPIOB
#define BSP_W5500_SPI_SCK_PIN               GPIO_Pin_13
#define BSP_W5500_SPI_MISO_PIN              GPIO_Pin_14
#define BSP_W5500_SPI_MOSI_PIN              GPIO_Pin_15

#define BSP_W5500_CONTROL_GPIO_PORT          GPIOG
#define BSP_W5500_INTERRUPT_PIN              GPIO_Pin_8
#define BSP_W5500_CHIP_SELECT_PIN            GPIO_Pin_9
#define BSP_W5500_RESET_PIN                  GPIO_Pin_15

/*
 * SPI2还连接板载VS1053。
 * 本项目不使用VS1053，因此保持其两个CS为高、RST为低，
 * 确保VS1053的MISO处于高阻态。
 */
#define BSP_W5500_VS1053_COMMAND_CS_PORT     GPIOB
#define BSP_W5500_VS1053_COMMAND_CS_PIN      GPIO_Pin_12
#define BSP_W5500_VS1053_DATA_CS_PORT        GPIOE
#define BSP_W5500_VS1053_DATA_CS_PIN         GPIO_Pin_6
#define BSP_W5500_VS1053_RESET_PORT          GPIOC
#define BSP_W5500_VS1053_RESET_PIN           GPIO_Pin_3

/*
 * 这是轮询次数上限，不是毫秒数。
 * 官方ioLibrary回调没有错误返回值，因此通过内部标志锁存超时。
 */
#define BSP_W5500_SPI_TIMEOUT_COUNT          100000U
#define BSP_W5500_SPI_DUMMY_BYTE             0xFFU

static bool s_xSpiTransferFailed = false;   // 标志位，用于锁存SPI传输是否失败


static void prvGpioInit(void)
{
    GPIO_InitTypeDef xGpioInit;

    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOB |
        RCC_APB2Periph_GPIOC |
        RCC_APB2Periph_GPIOE |
        RCC_APB2Periph_GPIOG,
        ENABLE);

    /*
     * 先设置输出锁存值，再切换成输出模式，
     * 避免GPIO初始化瞬间误选中W5500或VS1053。
     * CS = 0  → 选中 W5500 ，CS = 1  → 不选中 W5500
     * RST = 0  → 复位 W5500 ，RST = 1  → 释放复位
     * VS1053_CMD_CS = 1   不选中
     * VS1053_DATA_CS= 1   不选中
     * VS1053_RESET  = 0   保持复位
     */
    GPIO_SetBits(
        BSP_W5500_CONTROL_GPIO_PORT,
        BSP_W5500_CHIP_SELECT_PIN);
    GPIO_ResetBits(
        BSP_W5500_CONTROL_GPIO_PORT,
        BSP_W5500_RESET_PIN);

    GPIO_SetBits(
        BSP_W5500_VS1053_COMMAND_CS_PORT,
        BSP_W5500_VS1053_COMMAND_CS_PIN);
    GPIO_SetBits(
        BSP_W5500_VS1053_DATA_CS_PORT,
        BSP_W5500_VS1053_DATA_CS_PIN);
    GPIO_ResetBits(
        BSP_W5500_VS1053_RESET_PORT,
        BSP_W5500_VS1053_RESET_PIN);

    GPIO_StructInit(&xGpioInit);

    /* PB13和PB15由SPI2主动驱动。 */
    xGpioInit.GPIO_Pin =
        BSP_W5500_SPI_SCK_PIN |
        BSP_W5500_SPI_MOSI_PIN;
    xGpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    xGpioInit.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(BSP_W5500_SPI_GPIO_PORT, &xGpioInit);

    /* PB14只接收从设备MISO数据。 */
    xGpioInit.GPIO_Pin = BSP_W5500_SPI_MISO_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BSP_W5500_SPI_GPIO_PORT, &xGpioInit);

    /* W5500的CS和RST都由软件直接控制。 */
    xGpioInit.GPIO_Pin =
        BSP_W5500_CHIP_SELECT_PIN |
        BSP_W5500_RESET_PIN;
    xGpioInit.GPIO_Speed = GPIO_Speed_2MHz;
    xGpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BSP_W5500_CONTROL_GPIO_PORT, &xGpioInit);

    /* INTn低有效；本阶段只配置输入，不开启EXTI。 */
    xGpioInit.GPIO_Pin = BSP_W5500_INTERRUPT_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BSP_W5500_CONTROL_GPIO_PORT, &xGpioInit);

    /* 主动隔离同一SPI2总线上的VS1053。 */
    xGpioInit.GPIO_Pin = BSP_W5500_VS1053_COMMAND_CS_PIN;
    xGpioInit.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BSP_W5500_VS1053_COMMAND_CS_PORT, &xGpioInit);

    xGpioInit.GPIO_Pin = BSP_W5500_VS1053_DATA_CS_PIN;
    GPIO_Init(BSP_W5500_VS1053_DATA_CS_PORT, &xGpioInit);

    xGpioInit.GPIO_Pin = BSP_W5500_VS1053_RESET_PIN;
    GPIO_Init(BSP_W5500_VS1053_RESET_PORT, &xGpioInit);
}

static void prvSpiInit(void)
{
    SPI_InitTypeDef xSpiInit;

    RCC_APB1PeriphClockCmd(BSP_W5500_SPI_CLOCK, ENABLE);

    SPI_I2S_DeInit(BSP_W5500_SPI);
    SPI_StructInit(&xSpiInit);

    xSpiInit.SPI_Direction = SPI_Direction_2Lines_FullDuplex;   //全双工
    xSpiInit.SPI_Mode = SPI_Mode_Master;    //SPI_Mode_Master 表示 STM32 作为 SPI 主机
    xSpiInit.SPI_DataSize = SPI_DataSize_8b;    //表示 SPI 每一帧数据是 8 bit
    xSpiInit.SPI_CPOL = SPI_CPOL_Low;   //SPI_CPOL_Low 表示时钟空闲状态为低电平
    xSpiInit.SPI_CPHA = SPI_CPHA_1Edge; //SPI_CPHA_1Edge 表示在第一个有效时钟边沿采样数据,闲时 SCK 为低，第一个有效边沿就是“上升沿”
    xSpiInit.SPI_NSS = SPI_NSS_Soft;    //片选 CS

    /*
     * SPI2来自36 MHz PCLK1。
     * 首轮使用16分频，即2.25 MHz，优先保证首次硬件识别稳定。
     */
    xSpiInit.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    xSpiInit.SPI_FirstBit = SPI_FirstBit_MSB;   //表示 SPI 数据传输从最高位开始
    xSpiInit.SPI_CRCPolynomial = 7U;

    SPI_Init(BSP_W5500_SPI, &xSpiInit);
    SPI_NSSInternalSoftwareConfig(
        BSP_W5500_SPI,
        SPI_NSSInternalSoft_Set);
    SPI_Cmd(BSP_W5500_SPI, ENABLE); //正式启动 SPI
}

static bool prvWaitForSpiFlag(
    uint16_t usFlag,
    FlagStatus xExpectedStatus)
{
    uint32_t ulTimeoutCount = BSP_W5500_SPI_TIMEOUT_COUNT;

    while (SPI_I2S_GetFlagStatus(BSP_W5500_SPI, usFlag) !=
           xExpectedStatus)
    {
        if (ulTimeoutCount == 0U)
        {
            return false;
        }

        ulTimeoutCount--;
    }

    return true;
}

// SPI 传输一个字节
// 该函数通过 SPI 接口发送一个字节数据，并接收一个字节数据
static bool prvSpiTransferByte(
    uint8_t ucTransmitData,
    uint8_t *pucReceivedData)
{
    if (pucReceivedData == NULL)
    {
        return false;
    }
    //Transmit buffer empty
    if (prvWaitForSpiFlag(SPI_I2S_FLAG_TXE, SET) == false)
    {
        return false;
    }

    SPI_I2S_SendData(BSP_W5500_SPI, ucTransmitData);

    //Receive buffer not empty
    if (prvWaitForSpiFlag(SPI_I2S_FLAG_RXNE, SET) == false)
    {
        return false;
    }

    *pucReceivedData =
        (uint8_t)SPI_I2S_ReceiveData(BSP_W5500_SPI);

    /*
     * 等待最后一位真正移出后再允许官方驱动拉高CS。
     * SPI_I2S_FLAG_BSY 表示 SPI 总线是否忙碌，等待其复位以确保最后一位数据传输完成
     */
    return prvWaitForSpiFlag(SPI_I2S_FLAG_BSY, RESET);
}

// 片选控制函数
// 该函数用于拉低 W5500 的 CS 引脚，开始 SPI 通信
static void prvChipSelect(void)
{
    GPIO_ResetBits(
        BSP_W5500_CONTROL_GPIO_PORT,
        BSP_W5500_CHIP_SELECT_PIN);
}

static void prvChipDeselect(void)
{
    GPIO_SetBits(
        BSP_W5500_CONTROL_GPIO_PORT,
        BSP_W5500_CHIP_SELECT_PIN);
}

// SPI 读一个字节
// 该函数通过 SPI 接口从 W5500 读取一个字节数据
static uint8_t prvSpiReadByte(void)
{
    uint8_t ucReceivedData = 0U;

    if (s_xSpiTransferFailed)
    {
        return 0U;
    }

    if (prvSpiTransferByte(
            BSP_W5500_SPI_DUMMY_BYTE,
            &ucReceivedData) == false)
    {
        s_xSpiTransferFailed = true;
    }

    return ucReceivedData;
}

static void prvSpiWriteByte(uint8_t ucTransmitData)
{
    uint8_t ucDiscardedData = 0U;

    if (s_xSpiTransferFailed)
    {
        return;
    }
    /*
     * STM32 SPI是全双工的：发送一字节时也会收到一字节。
     * 必须读取DR，即使当前收到的数据没有业务意义。
     */
    if (prvSpiTransferByte(
            ucTransmitData,
            &ucDiscardedData) == false)
    {
        s_xSpiTransferFailed = true;
    }

    (void)ucDiscardedData; // 避免编译器对未使用变量的警告
}

void BspW5500_InterfaceInit(void)
{
    prvGpioInit();  // 初始化W5500控制引脚
    prvSpiInit();   // 初始化W5500 SPI接口

    reg_wizchip_cs_cbfunc(
        prvChipSelect,
        prvChipDeselect);

    reg_wizchip_spi_cbfunc(
        prvSpiReadByte,
        prvSpiWriteByte);

    /*
     * 当前版本不要注册NULL burst回调。
     * 该旧库会把它替换成非空但不传输数据的默认函数。
     */
    s_xSpiTransferFailed = false;
}

void BspW5500_SetResetAsserted(bool xAsserted)
{
    if (xAsserted)
    {
        GPIO_ResetBits(
            BSP_W5500_CONTROL_GPIO_PORT,
            BSP_W5500_RESET_PIN);
    }
    else
    {
        GPIO_SetBits(
            BSP_W5500_CONTROL_GPIO_PORT,
            BSP_W5500_RESET_PIN);
    }
}

bool BspW5500_ReadVersion(uint8_t *pucVersion)
{
    uint8_t ucVersion;

    if (pucVersion == NULL)
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    ucVersion = getVERSIONR();

    if (s_xSpiTransferFailed)
    {
        return false;
    }

    *pucVersion = ucVersion;
    return true;
}

bool BspW5500_IsInterruptAsserted(void)
{
    return GPIO_ReadInputDataBit(
               BSP_W5500_CONTROL_GPIO_PORT,
               BSP_W5500_INTERRUPT_PIN) == Bit_RESET;
}


static void prvBuildWizNetworkInfo(
    const BspW5500NetworkConfig_t *pxSource,
    wiz_NetInfo *pxDestination)
{
    memset(pxDestination, 0, sizeof(*pxDestination));

    memcpy(
        pxDestination->mac,
        pxSource->aucMac,
        BSP_W5500_MAC_ADDRESS_LENGTH);

    memcpy(
        pxDestination->ip,
        pxSource->aucIp,
        BSP_W5500_IPV4_OCTET_COUNT);

    memcpy(
        pxDestination->sn,
        pxSource->aucSubnetMask,
        BSP_W5500_IPV4_OCTET_COUNT);

    memcpy(
        pxDestination->gw,
        pxSource->aucGateway,
        BSP_W5500_IPV4_OCTET_COUNT);

    pxDestination->dhcp = NETINFO_STATIC;
}

bool BspW5500_ChipInit(void)
{
    int8_t iResult;

    s_xSpiTransferFailed = false;
    iResult = wizchip_init(NULL, NULL);

    return (iResult == 0) &&
           (s_xSpiTransferFailed == false);
}

bool BspW5500_SetNetworkConfig(
    const BspW5500NetworkConfig_t *pxConfig)
{
    wiz_NetInfo xWizNetworkInfo;

    if (pxConfig == NULL)
    {
        return false;
    }

    prvBuildWizNetworkInfo(pxConfig, &xWizNetworkInfo);

    s_xSpiTransferFailed = false;
    wizchip_setnetinfo(&xWizNetworkInfo);

    return s_xSpiTransferFailed == false;
}

bool BspW5500_GetNetworkConfig(
    BspW5500NetworkConfig_t *pxConfig)
{
    wiz_NetInfo xWizNetworkInfo = {0};

    if (pxConfig == NULL)
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    wizchip_getnetinfo(&xWizNetworkInfo);

    if (s_xSpiTransferFailed)
    {
        return false;
    }

    memcpy(
        pxConfig->aucMac,
        xWizNetworkInfo.mac,
        BSP_W5500_MAC_ADDRESS_LENGTH);

    memcpy(
        pxConfig->aucIp,
        xWizNetworkInfo.ip,
        BSP_W5500_IPV4_OCTET_COUNT);

    memcpy(
        pxConfig->aucSubnetMask,
        xWizNetworkInfo.sn,
        BSP_W5500_IPV4_OCTET_COUNT);

    memcpy(
        pxConfig->aucGateway,
        xWizNetworkInfo.gw,
        BSP_W5500_IPV4_OCTET_COUNT);

    return true;
}

bool BspW5500_GetPhyLink(bool *pxLinkUp)
{
    int8_t iPhyLink;

    if (pxLinkUp == NULL)
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    iPhyLink = wizphy_getphylink();

    if (s_xSpiTransferFailed ||
        ((iPhyLink != PHY_LINK_OFF) &&
         (iPhyLink != PHY_LINK_ON)))
    {
        return false;
    }

    *pxLinkUp = iPhyLink == PHY_LINK_ON;
    return true;
}


#define BSP_W5500_STABLE_READ_ATTEMPTS    4U

static uint16_t prvReadRegister16Once(uint32_t ulAddress)
{
    uint16_t usValue;

    usValue = (uint16_t)WIZCHIP_READ(ulAddress) << 8;
    usValue |= WIZCHIP_READ(WIZCHIP_OFFSET_INC(ulAddress, 1));
    return usValue;
}

static bool prvReadStableRegister16(
    uint32_t ulAddress,
    uint16_t *pusValue,
    bool *pxStable)
{
    uint32_t ulAttempt;
    uint16_t usFirst;
    uint16_t usSecond;

    *pxStable = false;
    for (ulAttempt = 0U;
         ulAttempt < BSP_W5500_STABLE_READ_ATTEMPTS;
         ulAttempt++)
    {
        usFirst = prvReadRegister16Once(ulAddress);
        usSecond = prvReadRegister16Once(ulAddress);
        if (s_xSpiTransferFailed)
        {
            return false;
        }

        if (usFirst == usSecond)
        {
            *pusValue = usFirst;
            *pxStable = true;
            break;
        }
    }
    return true;
}

static bool prvBufferSizeToBytes(
    uint8_t ucSizeKiB,
    uint16_t *pusSizeBytes)
{
    if ((pusSizeBytes == NULL) ||
        ((ucSizeKiB != 1U) && (ucSizeKiB != 2U) &&
         (ucSizeKiB != 4U) && (ucSizeKiB != 8U) &&
         (ucSizeKiB != 16U)))
    {
        return false;
    }

    *pusSizeBytes = (uint16_t)((uint16_t)ucSizeKiB * 1024U);
    return true;
}

static BspW5500OperationResult_t prvTrySocketCommand(
    uint8_t ucSocket,
    uint8_t ucCommand)
{
    uint8_t ucCurrentCommand = getSn_CR(ucSocket);

    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if (ucCurrentCommand != 0U)
    {
        return BSP_W5500_OPERATION_BUSY;
    }

    setSn_CR(ucSocket, ucCommand);
    return s_xSpiTransferFailed ?
        BSP_W5500_OPERATION_ERROR :
        BSP_W5500_OPERATION_STARTED;
}

BspW5500OperationResult_t BspW5500_TcpCloseStart(uint8_t ucSocket)
{
    if (ucSocket >= _WIZCHIP_SOCK_NUM_)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    s_xSpiTransferFailed = false;
    return prvTrySocketCommand(ucSocket, Sn_CR_CLOSE);
}

BspW5500OperationResult_t BspW5500_TcpOpenStart(
    uint8_t ucSocket,
    uint16_t usLocalPort)
{
    uint8_t ucCurrentCommand;

    if ((ucSocket >= _WIZCHIP_SOCK_NUM_) || (usLocalPort == 0U))
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    s_xSpiTransferFailed = false;
    ucCurrentCommand = getSn_CR(ucSocket);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if (ucCurrentCommand != 0U)
    {
        return BSP_W5500_OPERATION_BUSY;
    }

    setSn_MR(ucSocket, Sn_MR_TCP);  //设置所选Socket的模式
    setSn_PORT(ucSocket, usLocalPort);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    setSn_CR(ucSocket, Sn_CR_OPEN);  // 启动Socket打开命令
    return s_xSpiTransferFailed ?
        BSP_W5500_OPERATION_ERROR :
        BSP_W5500_OPERATION_STARTED;
}

BspW5500OperationResult_t BspW5500_TcpConnectStart(
    uint8_t ucSocket,
    const uint8_t *pucServerIp,
    uint16_t usServerPort)
{
    uint8_t aucServerIp[BSP_W5500_IPV4_OCTET_COUNT];
    uint8_t ucCurrentCommand;

    if ((ucSocket >= _WIZCHIP_SOCK_NUM_) ||
        (pucServerIp == NULL) || (usServerPort == 0U) ||
        (pucServerIp[0] == 0U) || (pucServerIp[0] >= 224U))
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    memcpy(aucServerIp, pucServerIp, sizeof(aucServerIp));
    s_xSpiTransferFailed = false;
    ucCurrentCommand = getSn_CR(ucSocket);  // 读取当前Socket命令寄存器,CR：Command Register
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if (ucCurrentCommand != 0U)
    {
        return BSP_W5500_OPERATION_BUSY;
    }

    setSn_DIPR(ucSocket, aucServerIp);
    setSn_DPORT(ucSocket, usServerPort);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    setSn_CR(ucSocket, Sn_CR_CONNECT);
    return s_xSpiTransferFailed ?
        BSP_W5500_OPERATION_ERROR :
        BSP_W5500_OPERATION_STARTED;
}

BspW5500OperationResult_t BspW5500_TcpDisconnectStart(uint8_t ucSocket)
{
    if (ucSocket >= _WIZCHIP_SOCK_NUM_)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    s_xSpiTransferFailed = false;
    return prvTrySocketCommand(ucSocket, Sn_CR_DISCON);
}

bool BspW5500_TcpReadStatus(
    uint8_t ucSocket,
    BspW5500TcpStatus_t *pxStatus)
{
    uint8_t ucState;
    uint8_t ucInterrupt;

    if ((ucSocket >= _WIZCHIP_SOCK_NUM_) || (pxStatus == NULL))
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    ucState = getSn_SR(ucSocket);
    ucInterrupt = getSn_IR(ucSocket);
    if (s_xSpiTransferFailed)
    {
        return false;
    }

    switch (ucState)
    {
        case SOCK_CLOSED:
            pxStatus->xState = BSP_W5500_TCP_CLOSED;
            break;
        case SOCK_INIT:
            pxStatus->xState = BSP_W5500_TCP_INIT;
            break;
        case SOCK_SYNSENT:
        case SOCK_SYNRECV:
            pxStatus->xState = BSP_W5500_TCP_CONNECTING;
            break;
        case SOCK_ESTABLISHED:
            pxStatus->xState = BSP_W5500_TCP_ESTABLISHED;
            break;
        case SOCK_CLOSE_WAIT:
            pxStatus->xState = BSP_W5500_TCP_PEER_CLOSED_SEND;
            break;
        case SOCK_FIN_WAIT:
        case SOCK_CLOSING:
        case SOCK_TIME_WAIT:
        case SOCK_LAST_ACK:
            pxStatus->xState = BSP_W5500_TCP_CLOSING;
            break;
        default:
            pxStatus->xState = BSP_W5500_TCP_OTHER;
            break;
    }

    pxStatus->ucRawState = ucState;
    pxStatus->xTimedOut = (ucInterrupt & Sn_IR_TIMEOUT) != 0U;
    return true;
}

bool BspW5500_TcpClearEvents(uint8_t ucSocket)
{
    uint8_t ucState;

    if (ucSocket >= _WIZCHIP_SOCK_NUM_)
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    ucState = getSn_SR(ucSocket);
    if (s_xSpiTransferFailed || (ucState != SOCK_CLOSED))
    {
        return false;
    }

    /* Sn_IR为写1清除。 */
    setSn_IR(ucSocket, 0xFFU);
    return s_xSpiTransferFailed == false;
}

bool BspW5500_TcpClearConnectedEvent(uint8_t ucSocket)
{
    if (ucSocket >= _WIZCHIP_SOCK_NUM_)
    {
        return false;
    }

    s_xSpiTransferFailed = false;
    setSn_IR(ucSocket, Sn_IR_CON);
    return s_xSpiTransferFailed == false;
}

BspW5500IoResult_t BspW5500_TcpReceive(
    uint8_t ucSocket,
    uint8_t *pucBuffer,
    uint16_t usCapacity,
    uint16_t *pusReceivedLength)
{
    bool xStable;
    uint8_t ucBufferSizeKiB;
    uint8_t ucCommand;
    uint8_t ucInterrupt;
    uint8_t ucState;
    uint16_t usAvailable;   // 从RX_RSR寄存器中读出来的数据长度
    uint16_t usBufferSizeBytes;
    uint16_t usReadLength;

    if ((ucSocket >= _WIZCHIP_SOCK_NUM_) ||
        (pucBuffer == NULL) || (pusReceivedLength == NULL) ||
        (usCapacity == 0U))
    {
        return BSP_W5500_IO_ERROR;
    }

    *pusReceivedLength = 0U;
    s_xSpiTransferFailed = false;
    ucCommand = getSn_CR(ucSocket);
    ucInterrupt = getSn_IR(ucSocket);
    ucState = getSn_SR(ucSocket);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_IO_ERROR;
    }
    if ((ucState != SOCK_ESTABLISHED) &&
        (ucState != SOCK_CLOSE_WAIT))
    {
        return BSP_W5500_IO_ERROR;
    }
    if ((ucInterrupt & Sn_IR_TIMEOUT) != 0U)
    {
        return BSP_W5500_IO_ERROR;
    }
    if (ucCommand != 0U)
    {
        return BSP_W5500_IO_BUSY;
    }

    ucBufferSizeKiB = getSn_RXBUF_SIZE(ucSocket);
    if (s_xSpiTransferFailed ||
        (prvBufferSizeToBytes(
             ucBufferSizeKiB,
             &usBufferSizeBytes) == false))
    {
        return BSP_W5500_IO_ERROR;
    }
    //Received Size Register，已接收数据长度寄存器
    if (prvReadStableRegister16(
            Sn_RX_RSR(ucSocket),
            &usAvailable,
            &xStable) == false)
    {
        return BSP_W5500_IO_ERROR;
    }
    if (xStable == false)
    {
        return BSP_W5500_IO_BUSY;
    }
    if (usAvailable > usBufferSizeBytes)
    {
        return BSP_W5500_IO_ERROR;
    }

    if ((ucInterrupt & Sn_IR_RECV) != 0U)
    {
        /* 数据存在与否仍以RX_RSR为准；这里只确认已观察到的事件。 */
        setSn_IR(ucSocket, Sn_IR_RECV);
        if (s_xSpiTransferFailed)
        {
            return BSP_W5500_IO_ERROR;
        }
    }
    if (usAvailable == 0U)
    {
        return BSP_W5500_IO_IDLE;
    }

    usReadLength = (usAvailable < usCapacity) ?
        usAvailable : usCapacity;
    wiz_recv_data(ucSocket, pucBuffer, usReadLength);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_IO_ERROR;
    }

    /* 通知W5500：Sn_RX_RD已经向前移动，可以释放这段RX空间。 */
    setSn_CR(ucSocket, Sn_CR_RECV);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_IO_ERROR;
    }

    ucState = getSn_SR(ucSocket);
    if (s_xSpiTransferFailed ||
        ((ucState != SOCK_ESTABLISHED) &&
         (ucState != SOCK_CLOSE_WAIT)))
    {
        /* RX指针已经推进；连接异常时丢弃本块，不能原地重读。 */
        return BSP_W5500_IO_ERROR;
    }

    *pusReceivedLength = usReadLength;
    return BSP_W5500_IO_COMPLETE;
}

BspW5500OperationResult_t BspW5500_TcpSendStart(
    uint8_t ucSocket,
    const uint8_t *pucData,
    uint16_t usLength)
{
    bool xStable;
    uint8_t ucBufferSizeKiB;
    uint8_t ucCommand;
    uint8_t ucInterrupt;
    uint8_t ucState;
    uint16_t usBufferSizeBytes;
    uint16_t usFreeSize;

    if ((ucSocket >= _WIZCHIP_SOCK_NUM_) ||
        (pucData == NULL) || (usLength == 0U))
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    s_xSpiTransferFailed = false;
    ucCommand = getSn_CR(ucSocket);
    ucInterrupt = getSn_IR(ucSocket);
    ucState = getSn_SR(ucSocket);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if ((ucState != SOCK_ESTABLISHED) &&
        (ucState != SOCK_CLOSE_WAIT))
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if ((ucInterrupt & Sn_IR_TIMEOUT) != 0U)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if (ucCommand != 0U)
    {
        /* 已知断链/超时优先于BUSY，不能把失效连接伪装成暂时空闲。 */
        return BSP_W5500_OPERATION_BUSY;
    }

    ucBufferSizeKiB = getSn_TXBUF_SIZE(ucSocket);
    if (s_xSpiTransferFailed ||
        (prvBufferSizeToBytes(
             ucBufferSizeKiB,
             &usBufferSizeBytes) == false) ||
        (usLength > usBufferSizeBytes))
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    if (prvReadStableRegister16(
            Sn_TX_FSR(ucSocket),
            &usFreeSize,
            &xStable) == false)
    {
        return BSP_W5500_OPERATION_ERROR;
    }
    if ((xStable == false) || (usFreeSize < usLength))
    {
        return BSP_W5500_OPERATION_BUSY;
    }
    if (usFreeSize > usBufferSizeBytes)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    /* 清除上一笔SENDOK，避免把旧事件误认为本次完成。 */
    setSn_IR(ucSocket, Sn_IR_SENDOK);
    /* ioLibrary v3.2.0接口未声明const，但不会修改源缓冲。 */
    wiz_send_data(ucSocket, (uint8_t *)pucData, usLength);
    if (s_xSpiTransferFailed)
    {
        return BSP_W5500_OPERATION_ERROR;
    }

    setSn_CR(ucSocket, Sn_CR_SEND);
    return s_xSpiTransferFailed ?
        BSP_W5500_OPERATION_ERROR :
        BSP_W5500_OPERATION_STARTED;
}

BspW5500IoResult_t BspW5500_TcpPollSend(uint8_t ucSocket)
{
    uint8_t ucInterrupt;
    uint8_t ucState;

    if (ucSocket >= _WIZCHIP_SOCK_NUM_)
    {
        return BSP_W5500_IO_ERROR;
    }

    s_xSpiTransferFailed = false;
    ucInterrupt = getSn_IR(ucSocket);
    ucState = getSn_SR(ucSocket);
    if (s_xSpiTransferFailed ||
        ((ucInterrupt & Sn_IR_TIMEOUT) != 0U))
    {
        return BSP_W5500_IO_ERROR;
    }
    if ((ucState != SOCK_ESTABLISHED) &&
        (ucState != SOCK_CLOSE_WAIT))
    {
        return BSP_W5500_IO_ERROR;
    }
    if ((ucInterrupt & Sn_IR_SENDOK) == 0U)
    {
        return BSP_W5500_IO_BUSY;
    }

    setSn_IR(ucSocket, Sn_IR_SENDOK); /* 向SENDOK对应位写1，让硬件清除它。 */
    return s_xSpiTransferFailed ?
        BSP_W5500_IO_ERROR : BSP_W5500_IO_COMPLETE;
}