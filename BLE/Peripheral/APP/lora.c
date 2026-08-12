#include "CH58x_common.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "lora.h"
#include "board.h"
#include "fixed_math_v2.h"

/**
 * @brief 初始化 LoRa 模块 (SX126x) 的 SPI 接口与控制引脚
 *
 * SPI1 主模式连接 SX126x：
 *   PA0:SCK  PA1:MOSI  PA3:NSS(片选)  PA2:MISO(复用)
 * GPIOB 控制引脚：PB12:BUSY(忙指示, 输入)  PB17:RESET(复位, 输出)  PB13:POWEN(供电使能, 输出)
 */
void Lora_Spi_Init(void)
{
    /* SPI 1 */
    GPIOA_SetBits(GPIO_Pin_1);
    GPIOA_ModeCfg(GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3, GPIO_ModeOut_PP_5mA); // PA3:CS, PA0:SCK, PA1:MOSI
    // GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU); // PA2:MISO - 与官方例程一致，不显式配置
    SPI1_MasterDefInit();
    R8_SPI1_CLOCK_DIV = 8; // 降低 SPI 速率留出裕量 (60MHz/8 = 7.5MHz)
    // PB12 BUSY / PB17 RESET / PB13 POWEN
    GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_17 | GPIO_Pin_13 ,GPIO_ModeOut_PP_5mA);
    GPIOB_SetBits(GPIO_Pin_17); // 复位引脚初始拉高（不复位）
    GPIOB_SetBits(GPIO_Pin_13); // 供电使能初始拉高（模块上电）
}

static int8_t Rssi = 0;
static SX126x_t SX126x;
static RadioOperatingModes_t OperatingMode;
static RadioPacketTypes_t PacketType;

//通过LoraBusy引脚，判断状态是否正常(可用): 0-非Ready状态(即Busy)， 1-Ready
/**
 * @brief 等待 SX126x 释放 BUSY 引脚（高电平表示忙）
 *
 * @retval 1 模块就绪；0 超时或模块已处于异常状态
 * 一旦超时即置位 lora 错误码；后续调用因错误码已置位而快速失败，
 * 避免持续忙等拖垮主循环（详见 recovery 退避逻辑）。
 */
static uint8_t Lora_WaitOnBusy(void) //高电平表示忙
{
    uint32_t timeout = LORA_READY_TIMEOUT;
    if(Dev.errorCode.bit.lora){ //存在busy情况,视为异常,防止长时间堵塞阻碍其他功能运行.
        return 0;
    }
    while( (GPIOB_ReadPortPin(GPIO_Pin_12) != 0) && (timeout > 0) ){
        timeout--;
        mDelaymS(1);
    }
    if(timeout == 0){ // write/read 异常
        PRINT("Lora Busy Timeout.\n");
        Dev.errorCode.bit.lora = 1;
        return 0;
    }
    return 1;
}

/**
 * @brief 硬件复位 SX126x 模块
 *
 * 官方资料要求复位引脚拉低保持 100us，这里使用 20ms 保证可靠。
 * 复位完成后等待 BUSY 释放。
 */
//Reset Lora: 官方资料要求复位引脚拉低并维持100us，安全起见，这里使用20ms
void Lora_Reset( ) {
    //断电
    // GPIOA_ResetBits(Lora_Enable_Pin);
    // mDelaymS(100);
    // GPIOA_SetBits(Lora_Enable_Pin);
    // mDelaymS(50);
    //通过引脚复位Lora
    GPIOB_ResetBits(GPIO_Pin_17);
    mDelaymS(20); //Delay_Ms(20);
    GPIOB_SetBits(GPIO_Pin_17);
    (void)Lora_WaitOnBusy();
}
/**
 * @brief 唤醒处于睡眠态的 SX126x（发送 GET_STATUS 命令试探）
 *
 * 唤醒时 NSS 拉低发送 GET_STATUS 即可，无需完整命令。
 */
void Lora_Wakeup()
{
    if(Dev.errorCode.bit.lora) return;
    /* LoRa NSS 硬件连接在 PA3；旧代码误操作 PB3，模块只能依赖后续命令碰巧唤醒。 */
    GPIOA_ResetBits(GPIO_Pin_3);
    SPI1_MasterSendByte(RADIO_GET_STATUS);
    SPI1_MasterSendByte(0x00);
    GPIOA_SetBits(GPIO_Pin_3);

    (void)Lora_WaitOnBusy();
}

static uint8_t Lora_CheckDeviceReady(void)
{
    return Lora_WaitOnBusy();
}

/**
 * @brief 发送 SX126x 命令（写命令）
 * @param command 命令字节（见 tLoraCmd 枚举）
 * @param buffer  命令参数
 * @param size    参数长度
 *
 * 发送前等待 BUSY 释放；SET_SLEEP 除外（进入睡眠后无 BUSY 响应）。
 */
//SPI写指令
void Lora_WriteCommand( tLoraCmd command, uint8_t *buffer, uint16_t size )
{
    if((size > 0U && buffer == 0) || !Lora_CheckDeviceReady()) return;
    GPIOA_ResetBits(GPIO_Pin_3);
    SPI1_MasterSendByte((uint8_t)command);

    for( uint16_t i = 0; i < size; i++ )
    {
        SPI1_MasterSendByte(buffer[i]);
    }
    GPIOA_SetBits(GPIO_Pin_3);
    if( command != RADIO_SET_SLEEP )
    {
        (void)Lora_WaitOnBusy();
    }
}

/**
 * @brief 读取 SX126x 命令（读命令）
 * @param command 命令字节
 * @param buffer  接收缓冲区；失败时填充 0xFF
 * @param size    期望读取字节数
 */
//SPI读指令
void Lora_ReadCommand( tLoraCmd command, uint8_t *buffer, uint16_t size )
{
    if(size > 0U && buffer == 0) return;
    if(size > 0U) memset(buffer, 0xFF, size);
    if(!Lora_CheckDeviceReady()) return;

    GPIOA_ResetBits(GPIO_Pin_3);
    SPI1_MasterSendByte(command);
    SPI1_MasterSendByte(0);
    for( uint16_t i = 0; i < size; i++ )
    {
        buffer[i] = SPI1_MasterRecvByte();
    }

    GPIOA_SetBits(GPIO_Pin_3);

    if(!Lora_WaitOnBusy() && size > 0U) memset(buffer, 0xFF, size);
}

/**
 * @brief 连续写 SX126x 寄存器
 * @param address 16 位寄存器地址
 * @param buffer  数据源
 * @param size    写入字节数
 */
//写寄存器
void Lora_WriteRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
    if((size > 0U && buffer == 0) || !Lora_CheckDeviceReady()) return;

    GPIOA_ResetBits(GPIO_Pin_3);

    SPI1_MasterSendByte(RADIO_WRITE_REGISTER);
    SPI1_MasterSendByte((uint8_t)((address & 0xff00u) >> 8u));
    SPI1_MasterSendByte((uint8_t)(address & 0x00ffu));

    for( uint16_t i = 0; i < size; i++ )
    {
        SPI1_MasterSendByte(buffer[i]);
    }

    GPIOA_SetBits(GPIO_Pin_3);

    (void)Lora_WaitOnBusy();
}

/**
 * @brief 写单个 SX126x 寄存器（便捷封装）
 */
void Lora_WriteRegister( uint16_t address, uint8_t value )
{
    Lora_WriteRegisters( address, &value, 1 );
}

/**
 * @brief 连续读 SX126x 寄存器
 * @param address 16 位寄存器地址
 * @param buffer  接收缓冲区；失败时填充 0xFF
 * @param size    读取字节数
 */
void Lora_ReadRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
    if(size > 0U && buffer == 0) return;
    if(size > 0U) memset(buffer, 0xFF, size);
    if(!Lora_CheckDeviceReady()) return;

    GPIOA_ResetBits(GPIO_Pin_3);
    SPI1_MasterSendByte(RADIO_READ_REGISTER);
    SPI1_MasterSendByte((uint8_t)((address & 0xff00u) >> 8u));
    SPI1_MasterSendByte((uint8_t)(address & 0x00ffu));
    SPI1_MasterSendByte(0x00);
    for (uint16_t i=0; i<size; i++) {
        buffer[i] = SPI1_MasterRecvByte();
    }
    GPIOA_SetBits(GPIO_Pin_3);

    if(!Lora_WaitOnBusy() && size > 0U) memset(buffer, 0xFF, size);
}

uint8_t Lora_ReadRegister( uint16_t address )
{
    uint8_t data;
    Lora_ReadRegisters( address, &data, 1 );
    return data;
}

/**
 * @brief 写 SX126x 内部数据缓冲区（Tx/Rx 共用 256 字节）
 * @param offset 缓冲区起始偏移
 * @param buffer 数据源
 * @param size   写入字节数
 */
void Lora_WriteBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
    if((size > 0U && buffer == 0) || !Lora_CheckDeviceReady()) return;

    GPIOA_ResetBits(GPIO_Pin_3);

    SPI1_MasterSendByte(RADIO_WRITE_BUFFER);
    SPI1_MasterSendByte(offset);
    for (uint16_t i=0; i<size; i++) {
        SPI1_MasterSendByte(buffer[i]);
    }
    GPIOA_SetBits(GPIO_Pin_3);

    (void)Lora_WaitOnBusy();
}

/**
 * @brief 读 SX126x 内部数据缓冲区
 * @param offset 缓冲区起始偏移
 * @param buffer 接收缓冲区；失败时填充 0xFF
 * @param size   读取字节数
 */
//从数据缓冲区中读取数据
void Lora_ReadBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
    if(size > 0U && buffer == 0) return;
    if(size > 0U) memset(buffer, 0xFF, size);
    if(!Lora_CheckDeviceReady()) return;

    GPIOA_ResetBits(GPIO_Pin_3);

    SPI1_MasterSendByte(RADIO_READ_BUFFER);
    SPI1_MasterSendByte(offset);
    SPI1_MasterSendByte(0x00);
    for (uint16_t i=0; i<size; i++) {
        buffer[i] = SPI1_MasterRecvByte();
    }
    GPIOA_SetBits(GPIO_Pin_3);

    if(!Lora_WaitOnBusy() && size > 0U) memset(buffer, 0xFF, size);
}


/**
 * @brief 设置 SX126x 进入待机模式
 * @param standbyConfig STDBY_RC(内部RC) 或 STDBY_XOSC(外部晶振)
 */
void Lora_SetStandby( RadioStandbyModes_t standbyConfig )
{
    Lora_WriteCommand( RADIO_SET_STANDBY, ( uint8_t* )&standbyConfig, 1 );
    if( standbyConfig == STDBY_RC )
    {
        OperatingMode = MODE_STDBY_RC;
    }
    else
    {
        OperatingMode = MODE_STDBY_XOSC;
    }
}

/**
 * @brief 获取当前数据包类型（内部记录值）
 */
RadioPacketTypes_t Lora_GetPacketType( void )
{
    return PacketType;
}

/**
 * @brief 读取 RX 缓冲区状态：已接收 payload 长度与起始偏移
 * @param payloadLength 输出：有效 payload 字节数
 * @param rxStartBufferPointer 输出：payload 在缓冲区中的起始偏移
 *
 * LoRa 显式头（variable header）模式下长度取状态寄存器；隐式头模式下读寄存器补足。
 */
void Lora_GetRxBufferStatus( uint8_t *payloadLength, uint8_t *rxStartBufferPointer )
{
    uint8_t status[2];

    Lora_ReadCommand( RADIO_GET_RXBUFFERSTATUS, status, 2 );

    // 若为 LoRa 固定头（implicit header），payload 长度由 REG_LR_PAYLOADLENGTH 指定
    if( ( Lora_GetPacketType( ) == PACKET_TYPE_LORA ) && ( Lora_ReadRegister( REG_LR_PACKETPARAMS ) >> 7 == 1 ) )
    {
        *payloadLength = Lora_ReadRegister( REG_LR_PAYLOADLENGTH );
    }
    else
    {
        *payloadLength = status[0];
    }
    *rxStartBufferPointer = status[1];
}

/**
 * @brief 将待发送数据写入 SX126x 缓冲区
 */
void Lora_SetPayload( uint8_t *payload, uint8_t size )
{
    Lora_WriteBuffer( 0x00, payload, size );
}

/**
 * @brief 从 SX126x 缓冲区读取已接收数据
 * @param buffer 接收缓冲区
 * @param size   输入：缓冲区容量；输出：实际 payload 长度
 * @param maxSize 允许的最大长度
 * @retval 0 成功；1 payload 长度超出 maxSize
 */
uint8_t Lora_GetPayload( uint8_t *buffer, uint8_t *size,  uint8_t maxSize )
{
    uint8_t offset = 0;

    Lora_GetRxBufferStatus( size, &offset );
    if( *size > maxSize )
    {
        return 1;
    }
    Lora_ReadBuffer( offset, buffer, *size );
    return 0;
}

/**
 * @brief 进入发射模式
 * @param timeout 发射超时（0 = 无超时，持续发射直至完成）
 */
void Lora_SetTx( uint32_t timeout )
{
    uint8_t buf[3];

    OperatingMode = MODE_TX;

    buf[0] = ( uint8_t )( ( timeout >> 16 ) & 0xFF );
    buf[1] = ( uint8_t )( ( timeout >> 8 ) & 0xFF );
    buf[2] = ( uint8_t )( timeout & 0xFF );
    Lora_WriteCommand( RADIO_SET_TX, buf, 3 );
}

/**
 * @brief 快捷发送：写 payload 后进入发射模式
 */
void Lora_SendPayload( uint8_t *payload, uint8_t size, uint32_t timeout )
{
    Lora_SetPayload( payload, size );
    Lora_SetTx( timeout );
}

/**
 * @brief 进入接收模式
 * @param timeout 接收超时（0 = 单次接收无超时）
 */
void Lora_SetRx( uint32_t timeout )
{
    uint8_t buf[3];
    OperatingMode = MODE_RX;

    buf[0] = ( uint8_t )( ( timeout >> 16 ) & 0xFF );
    buf[1] = ( uint8_t )( ( timeout >> 8 ) & 0xFF );
    buf[2] = ( uint8_t )( timeout & 0xFF );
    Lora_WriteCommand( RADIO_SET_RX, buf, 3 );
}

/**
 * @brief 配置检测到前导码时是否停止 RX 定时器
 */
void Lora_SetStopRxTimerOnPreambleDetect( uint8_t enable )
{
    Lora_WriteCommand( RADIO_SET_STOPRXTIMERONPREAMBLE, ( uint8_t* )&enable, 1 );
}

/**
 * @brief 设置 LoRa 符号数超时（无符号超时时用）
 */
void Lora_SetLoRaSymbNumTimeout( uint8_t SymbNum )
{
    Lora_WriteCommand( RADIO_SET_LORASYMBTIMEOUT, &SymbNum, 1 );
}

/**
 * @brief 设置稳压器模式（LDO 或 DC-DC）
 */
void Lora_SetRegulatorMode( RadioRegulatorMode_t mode )
{
    Lora_WriteCommand( RADIO_SET_REGULATORMODE, ( uint8_t* )&mode, 1 );
}

/**
 * @brief 执行镜像频率校准（针对特定频段）
 *
 * 频率落入不同频段时使用不同的校准系数对；低于 210 MHz 时无有效校准对，
 * 按驱动约定仍发送 210 MHz 段系数。
 */
void Lora_CalibrateImage( uint32_t freq )
{
    uint8_t calFreq[2];

    if( freq > 900000000 )
    {
        calFreq[0] = 0xE1;
        calFreq[1] = 0xE9;
    }
    else if( freq > 850000000 )
    {
        calFreq[0] = 0xD7;
        calFreq[1] = 0xD8;
    }
    else if( freq > 770000000 )
    {
        calFreq[0] = 0xC1;
        calFreq[1] = 0xC5;
    }
    else if( freq > 460000000 )
    {
        calFreq[0] = 0x75;
        calFreq[1] = 0x81;
    }
    else if( freq > 425000000 )
    {
        calFreq[0] = 0x6B;
        calFreq[1] = 0x6F;
    }
	else if( freq >= 210000000)
	{
        calFreq[0] = 0x37;
        calFreq[1] = 0x41;
	}

    Lora_WriteCommand( RADIO_CALIBRATEIMAGE, calFreq, 2 );
}

/**
 * @brief 配置 PA（功率放大器）参数
 * @param paDutyCycle PA 占空比
 * @param hpMax 高功率最大值
 * @param deviceSel 器件选择（0=SX1262 等）
 * @param paLut PA 查找表
 */
void Lora_SetPaConfig( uint8_t paDutyCycle, uint8_t hpMax, uint8_t deviceSel, uint8_t paLut )
{
    uint8_t buf[4];

    buf[0] = paDutyCycle;
    buf[1] = hpMax;
    buf[2] = deviceSel;
    buf[3] = paLut;
    Lora_WriteCommand( RADIO_SET_PACONFIG, buf, 4 );
}

/**
 * @brief 使能 TX/RX 完成中断标志（仅开放 TX、RX 两个中断位）
 *
 * 固定网关协议只依赖 TX_DONE / RX_DONE 中断；其余中断位一律屏蔽。
 */
//使能lora  tx/rx中断标志位
void Lora_SetDioIrqParams( uint16_t irqMask)
{
	uint8_t buf[8] = {0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00};

	//

	buf[0] = 0x02;     //timeout
	buf[1] = irqMask&0x03u; //屏蔽其它位，只允许TX,RX两个中断

//    buf[0] = ( uint8_t )( ( irqMask >> 8 ) & 0x00FF );
//    buf[1] = ( uint8_t )( irqMask & 0x00FF );
//    buf[2] = ( uint8_t )( ( dio1Mask >> 8 ) & 0x00FF );
//    buf[3] = ( uint8_t )( dio1Mask & 0x00FF );
//    buf[4] = ( uint8_t )( ( dio2Mask >> 8 ) & 0x00FF );
//    buf[5] = ( uint8_t )( dio2Mask & 0x00FF );
//    buf[6] = ( uint8_t )( ( dio3Mask >> 8 ) & 0x00FF );
//    buf[7] = ( uint8_t )( dio3Mask & 0x00FF );
    Lora_WriteCommand( RADIO_CFG_DIOIRQ, buf, 8 );
}

/**
 * @brief 读取当前中断状态（2 字节位图）
 */
uint16_t Lora_GetIrqStatus( void )
{
    uint8_t irqStatus[2];
    Lora_ReadCommand( RADIO_GET_IRQSTATUS, irqStatus, 2 );
    return ( irqStatus[0] << 8 ) | irqStatus[1];
}

/**
 * @brief 配置 DIO2 作为射频开关控制信号
 */
void Lora_SetDio2AsRfSwitchCtrl( uint8_t enable )
{
    Lora_WriteCommand( RADIO_SET_RFSWITCHMODE, &enable, 1 );
}

/**
 * @brief 设置射频中心频率
 *
 * 频率先经镜像校准，再换算为 SX126x 的 PLL 分频值（见 FixedMathV2_FrequencyHzToPll）。
 */
//101////////////////////////////////////////////////////////////////////
void Lora_SetRfFrequency( uint32_t frequency )
{
    uint8_t buf[4];
    uint32_t freq = 0;

    Lora_CalibrateImage( frequency );

    freq = FixedMathV2_FrequencyHzToPll(frequency);
//	switch(frequency)
//	{
//		case 410000000: freq = 429916160; break;
//		case 411000000: freq = 430964736; break;
//		default: break;
//	}

    buf[0] = ( uint8_t )( ( freq >> 24 ) & 0xFF );
    buf[1] = ( uint8_t )( ( freq >> 16 ) & 0xFF );
    buf[2] = ( uint8_t )( ( freq >> 8 ) & 0xFF );
    buf[3] = ( uint8_t )( freq & 0xFF );
    Lora_WriteCommand( RADIO_SET_RFFREQUENCY, buf, 4 );
}

/**
 * @brief 设置数据包类型（LoRa / GFSK）并记录内部状态
 */
void Lora_SetPacketType( RadioPacketTypes_t packetType )
{
    // 保存包类型到内部变量，避免重复查询射频
    PacketType = packetType;
    Lora_WriteCommand( RADIO_SET_PACKETTYPE, ( uint8_t* )&packetType, 1 );
}

/**
 * @brief 设置LoRa发送参数
 *
 * @param power 发射功率，范围：14~22
 * @param rampTime 发射功率变化时间
 */
/**
 * @brief 设置发射功率与上升时间
 *
 * @param power 功率 dBm，范围 14~22，超出时自动钳制
 * @param rampTime 功率斜坡时间（见 RadioRampTimes_t）
 *
 * 不同功率段对应不同的 PA 配置（占空比/hpMax），并固定过流保护寄存器。
 */
void Lora_SetTxParams( int8_t power, RadioRampTimes_t rampTime )
{
    uint8_t buf[2];
    uint8_t paDutyCycle, hpMax;
    if (power < MIN_LORA_POWER) { //限定功率范围： 14~22
        power = MIN_LORA_POWER;
    } else if (power > MAX_LORA_POWER) {
        power = MAX_LORA_POWER;
    }
    if (power < 17) { //14~16
        paDutyCycle = 0x02;
        hpMax = 0x02;
    } else if (power < 20) { //17~19
        paDutyCycle = 0x02;
        hpMax = 0x03;
    } else if (power < 22) { //20~21
        paDutyCycle = 0x03;
        hpMax = 0x05;
    } else { //22
        paDutyCycle = 0x04;
        hpMax = 0x07;
    }
    Lora_SetPaConfig( paDutyCycle, hpMax, 0x00, 0x01 );

    Lora_WriteRegister( REG_OCP, 0x38 );

    buf[0] = power;
    buf[1] = ( uint8_t )rampTime;
    Lora_WriteCommand( RADIO_SET_TXPARAMS, buf, 2 );
}

/**
 * @brief 设置 LoRa 调制参数（扩频因子/带宽/编码率/低速率优化）
 *
 * 固定网关协议只使用 LoRa 包类型，GFSK 分支已按精简实现保留。
 */
//101////////////////////////////////////////////////////////////////////
void Lora_SetModulationParams( ModulationParams_t *modulationParams )
{
	uint8_t n = 4;
    uint8_t buf[4] = { 0x00 };
	buf[0] = modulationParams->Params.LoRa.SpreadingFactor;
	buf[1] = modulationParams->Params.LoRa.Bandwidth;
	buf[2] = modulationParams->Params.LoRa.CodingRate;
	buf[3] = modulationParams->Params.LoRa.LowDatarateOptimize;
	Lora_WriteCommand( RADIO_SET_MODULATIONPARAMS, buf, n );

}

/**
 * @brief 设置 LoRa 数据包参数（前导码/头模式/payload 长度/CRC/IQ）
 *
 * HeaderType=0 为显式头（variable header），PayloadLength 在显式头下由对端帧头决定。
 */
void Lora_SetPacketParams( PacketParams_t *packetParams )
{
    uint8_t n = 6;
    uint8_t buf[6] = { 0x00 };
    buf[0] = ( packetParams->Params.LoRa.PreambleLength >> 8 ) & 0xFF;
    buf[1] = packetParams->Params.LoRa.PreambleLength;
    buf[2] = packetParams->Params.LoRa.HeaderType;
    buf[3] = packetParams->Params.LoRa.PayloadLength;
    buf[4] = packetParams->Params.LoRa.CrcMode;
    buf[5] = packetParams->Params.LoRa.InvertIQ;
    Lora_WriteCommand( RADIO_SET_PACKETPARAMS, buf, n );
}

/**
 * @brief 设置 Tx/Rx 缓冲区基地址（内部 256 字节 buffer 的起始偏移）
 */
void Lora_SetBufferBaseAddress( uint8_t txBaseAddress, uint8_t rxBaseAddress )
{
    uint8_t buf[2];
    buf[0] = txBaseAddress;
    buf[1] = rxBaseAddress;
    Lora_WriteCommand( RADIO_SET_BUFFERBASEADDRESS, buf, 2 );
}

/**
 * @brief 读取射频状态（模式/忙标志）
 */
RadioStatus_t Lora_GetStatus( void )
{
    uint8_t stat = 0;
    RadioStatus_t status;
    Lora_ReadCommand( RADIO_GET_STATUS, ( uint8_t * )&stat, 1 );
    status.Value = stat;
    return status;
}

/**
 * @brief 读取最近一次收包的 RSSI/SNR 状态
 */
void Lora_GetPacketStatus( PacketStatus_t *pktStatus )
{
    uint8_t status[3];
    Lora_ReadCommand( RADIO_GET_PACKETSTATUS, status, 3 );
    pktStatus->Params.LoRa.RssiPkt = -status[0] >> 1;
    ( status[1] < 128 ) ? ( pktStatus->Params.LoRa.SnrPkt = status[1] >> 2 ) : ( pktStatus->Params.LoRa.SnrPkt = ( ( status[1] - 256 ) >> 2 ) );
    pktStatus->Params.LoRa.SignalRssiPkt = -status[2] >> 1;
}

/**
 * @brief 清除指定中断标志
 */
void Lora_ClearIrqStatus( uint16_t irq )
{
    uint8_t buf[2];
    buf[0] = ( uint8_t )( ( ( uint16_t )irq >> 8 ) & 0x00FF );
    buf[1] = ( uint8_t )( ( uint16_t )irq & 0x00FF );
    Lora_WriteCommand( RADIO_CLR_IRQSTATUS, buf, 2 );
}

/**
 * @brief 初始化 LoRa 模块（SX126x）并配置指定射频参数
 *
 * @param frequencyHz 中心频率（Hz）
 * @param power 发射功率（14~22 dBm）
 * @param sf 扩频因子
 * @param bw 带宽
 * @retval 0 成功；1 模块无响应（SPI 校验失败）
 *
 * 完成复位、唤醒、稳压器、缓冲区、调制/包参数、频率设置，
 * 最后读取状态寄存器校验 SPI 通路是否正常。
 */
uint8_t Lora_Init(uint32_t frequencyHz, uint8_t power, uint8_t sf, uint8_t bw) {
    Dev.errorCode.bit.lora = 0;
	Lora_Spi_Init();
	Lora_Reset( );
    // 唤醒LoRa模块,wait for busy
    Lora_Wakeup( );

    // standby mode
    Lora_SetStandby( STDBY_RC );

    // 设置调节器DCDC
    Lora_SetRegulatorMode( USE_DCDC );

        // Rx Gain 保持寄存器：确保Sleep/Wake后Rx Boosted Gain不丢失
    {
        uint8_t tmp;
        tmp = 0x01; Lora_WriteRegisters(0x029F, &tmp, 1);
        tmp = 0x08; Lora_WriteRegisters(0x02A0, &tmp, 1);
        tmp = 0xAC; Lora_WriteRegisters(0x02A1, &tmp, 1);
    }
    // 设置缓冲区基地址
    Lora_SetBufferBaseAddress( 0x00, 0x00 );

    // 设置发送参数
    Lora_SetTxParams( 0, RADIO_RAMP_200_US );

    // DIO2设置为RF开关控制
    Lora_SetDio2AsRfSwitchCtrl(1);

    Lora_SetStandby( STDBY_RC );

    // 在检测到前导码时停止RX定时器
    Lora_SetStopRxTimerOnPreambleDetect( 0 );

    // 设置LoRa符号数超时
    Lora_SetLoRaSymbNumTimeout( 0 );

    // 设置LoRa的调制参数,sf,bw,codingrate,lowdatarateoptimize
    SX126x.ModulationParams.PacketType = PACKET_TYPE_LORA;
    SX126x.ModulationParams.Params.LoRa.SpreadingFactor = sf;
    SX126x.ModulationParams.Params.LoRa.Bandwidth =  bw;
    SX126x.ModulationParams.Params.LoRa.CodingRate = LORA_CR_4_5;
    SX126x.ModulationParams.Params.LoRa.LowDatarateOptimize = 0x00;

    // 设置LoRa的数据包参数
    SX126x.PacketParams.Params.LoRa.PreambleLength = PREAMBLE_LENGTH;
    SX126x.PacketParams.Params.LoRa.HeaderType = 0;
    SX126x.PacketParams.Params.LoRa.PayloadLength = 0xFF;
    SX126x.PacketParams.Params.LoRa.CrcMode = 1;
    SX126x.PacketParams.Params.LoRa.InvertIQ = 0;

    Lora_SetStandby(0);

    // 设置数据包类型为LoRa
    Lora_SetPacketType( PACKET_TYPE_LORA );

    // 设置调制参数
    Lora_SetModulationParams( &SX126x.ModulationParams );

    // 设置数据包参数
    Lora_SetPacketParams( &SX126x.PacketParams );

    // 设置发送功率参数
    Lora_SetTxParams(power,RADIO_RAMP_200_US);

    // 设置射频频率
    Lora_SetRfFrequency(frequencyHz);

    // 通过读取状态寄存器验证 SPI 通信
    RadioStatus_t status = Lora_GetStatus();
    if (status.Value == 0x00 || status.Value == 0xFF) {
        Dev.errorCode.bit.lora = 1;
        PRINT("Error: LoRa module not responding (Status=0x%02x). Check wiring.\n", status.Value);
        return 1;
    } else {
        PRINT("LoRa Init Verified. Status = 0x%02x\n", status.Value);
        return 0;
    }
}

/**
 * @brief 发送数据（发送完成后自动进入 Standby）
 * @param data 数据源
 * @param len  发送长度
 */
void Lora_Tx(uint8_t *data, uint8_t len){
    if(Dev.errorCode.bit.lora || data == 0 || len == 0U) return;
	Lora_ClearIrqStatus(IRQ_RADIO_ALL);
	Lora_SetDioIrqParams( IRQ_TX_DONE);
	SX126x.PacketParams.Params.LoRa.PayloadLength = len;
	Lora_SetPacketParams( &SX126x.PacketParams );
	Lora_SendPayload( data, len, 0 );
}

/**
 * @brief 检查并取回收到的数据
 * @param data 接收缓冲区（调用方提供，最大 255 字节）
 * @param len  输出：实际收到的 payload 长度；未收到时置 0
 *
 * RX_DONE 置位时读取 payload 与 RSSI 后重新进入监听；
 * 非 RX 中断（如超时/CRC 错误）直接重新监听。
 */
void Lora_CheckData(uint8_t *data, uint8_t *len){
    if(len == 0) return;
	*len = 0;
    if(Dev.errorCode.bit.lora || data == 0) return;
	uint16_t irqRegs = 0;
	PacketStatus_t pktStatus;
	irqRegs = Lora_GetIrqStatus();
    if( irqRegs == 0xFFFF ) return;
	if((irqRegs & IRQ_RX_DONE) == IRQ_RX_DONE){
		if(Lora_GetPayload(data, len, 0xFF) != 0U || Dev.errorCode.bit.lora) {
            *len = 0;
            return;
        }
		Lora_GetPacketStatus( &pktStatus );
        if(Dev.errorCode.bit.lora) {
            *len = 0;
            return;
        }
		Rssi = pktStatus.Params.LoRa.RssiPkt+20;
		Lora_Listening();
	}else if(irqRegs != 0){
		PRINT("\t#Irq(%d).\n", irqRegs);
		Lora_Listening();
	}
}

/**
 * @brief 获取最近一次收包的 RSSI（含固定偏移补偿）
 */
int8_t Lora_GetRssi(){
	return Rssi;
}

/**
 * @brief 进入监听模式：单次接收、无超时，收到信号后自动回到 STBY_RC
 */
void Lora_Listening(){
	uint8_t buf[3] = {0x00, 0x00, 0x00};
    if(Dev.errorCode.bit.lora) return;
	Lora_ClearIrqStatus(IRQ_RADIO_ALL);
 //天线切换至接收模式
	Lora_SetDioIrqParams( IRQ_RX_DONE);
	buf[0] = 0x96;
	Lora_WriteRegisters(0x08AC, buf, 1);
	Lora_SetRx(0);
}


