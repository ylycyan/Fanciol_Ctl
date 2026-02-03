#include "CH58x_common.h"
#include <stdint.h>
#include <stdbool.h>
#include "lora.h"
/*
void Lora_Pio_Init(void)
{
        GPIOA_SetBits(GPIO_Pin_12);
        GPIOA_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
        SPI0_MasterDefInit();   
        // 单字节发送
        GPIOA_ResetBits(GPIO_Pin_12);
        SPI0_MasterSendByte(0x55);
        GPIOA_SetBits(GPIO_Pin_12);
        DelayMs(1);
        GPIOA_ResetBits(GPIO_Pin_12);
        i = SPI0_MasterRecvByte();
        GPIOA_SetBits(GPIO_Pin_12);
        DelayMs(2);
        PRINT("receive %x\n",i);
    
        // FIFO 连续发送
        GPIOA_ResetBits(GPIO_Pin_12);
        SPI0_MasterTrans(spiBuff, 8);
        GPIOA_SetBits(GPIO_Pin_12);
        DelayMs(2);
        GPIOA_ResetBits(GPIO_Pin_12);
        SPI0_MasterRecv(spiBuffrev, 8);
        GPIOA_SetBits(GPIO_Pin_12);
        DelayMs(2);
        PRINT("FIFO recv ");
        for(i = 0; i < 8; i++)
        {
            PRINT(" %x", spiBuffrev[i]);
        }
        PRINT("\n");
}
*/

void Lora_Spi_Init(void)
{
    /* SPI 0 */
    GPIOA_SetBits(GPIO_Pin_12); 
    GPIOA_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeOut_PP_5mA); // 12:CS, 13:SCK, 14:MOSI  15:MISO
    SPI0_MasterDefInit();
    //PA11 BUSY PA10 RESET PA7
    GPIOA_ModeCfg(GPIO_Pin_11,GPIO_ModeIN_Floating);
    GPIOA_ModeCfg(GPIO_Pin_10,GPIO_ModeOut_PP_5mA);
    GPIOA_SetBits(GPIO_Pin_10);
}

void Lora_Spi_Transmit(uint8_t *data, uint16_t len)
{
    int16_t timeout = 100;
    while((GPIOA_ReadPortPin(GPIO_Pin_11) != 0) && (timeout-- > 0));
    if(timeout <= 0)
    {
        PRINT("Lora_Spi_Transmit busy\n");
        return;
    }
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_MasterTrans(data, len);
    GPIOA_SetBits(GPIO_Pin_12);
}

void Lora_Spi_Receive(uint8_t *data, uint16_t len)
{
    int16_t timeout = 100;
    while((GPIOA_ReadPortPin(GPIO_Pin_11) != 0) && (timeout-- > 0));
    if(timeout <= 0)
    {
        PRINT("Lora_Spi_Receive busy\n");
        return;
    }
    GPIOA_ResetBits(GPIO_Pin_12);
    SPI0_MasterRecv(data, len);
    GPIOA_SetBits(GPIO_Pin_12);
}

#define NSS_LOW() HAL_GPIO_WritePin(Lora_Nss_GPIO_Port, Lora_Nss_Pin, GPIO_PIN_RESET)
#define NSS_HIGH() HAL_GPIO_WritePin(Lora_Nss_GPIO_Port, Lora_Nss_Pin, GPIO_PIN_SET)

static int8_t Rssi = 0;
static SX126x_t SX126x;
static RadioOperatingModes_t OperatingMode;
static RadioPacketTypes_t PacketType;


//通过LoraBysy引脚，判断状态是否正常(可用): 0-非Ready状态(即Busy)， 1-Ready
void Lora_WaitOnBusy( void ) //高电平表示忙
{
	uint32_t timeout = LORA_READY_TIMEOUT;
    while( (HAL_GPIO_ReadPin(Lora_Busy_GPIO_Port, Lora_Busy_Pin) != 0) &&(timeout--)){
    	DelayMs(1);
    }
    if(timeout == 0){
    	PRINTF("Lora Busy Timeout.\n");
        DelayMs(50);
    }
}

//Reset Lora: 官方资料要求复位引脚拉低并维持100us，安全起见，这里使用20ms
void Lora_Reset( ) {
	//断电
	HAL_GPIO_WritePin(Lora_Enable_GPIO_Port, Lora_Enable_Pin, RESET);
	DelayMs(100);
	HAL_GPIO_WritePin(Lora_Enable_GPIO_Port, Lora_Enable_Pin, SET);
	DelayMs(50);
	//通过引脚复位Lora
	LORA_RESET_LOW();
	DelayMs(20); //Delay_Ms(20);
	LORA_RESET_HIGH();
	Lora_WaitOnBusy();
}
/**
 * @brief 唤醒Lora设备
 */
void Lora_Wakeup()
{
    uint8_t tx, rx;
    NSS_LOW();

    tx = RADIO_GET_STATUS;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    tx = 0x00;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    NSS_HIGH();

    Lora_WaitOnBusy();
}

void Lora_CheckDeviceReady( void )
{
    Lora_WaitOnBusy( );
}

//SPI写指令
void Lora_WriteCommand( tLoraCmd command, uint8_t *buffer, uint16_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );
    NSS_LOW();
    tx = (uint8_t)command;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);

    for( uint16_t i = 0; i < size; i++ )
    {
    	HAL_SPI_TransmitReceive(&hspi4, buffer+i, &rx, 1, 100);
    }
    NSS_HIGH();
    if( command != RADIO_SET_SLEEP )
    {
        Lora_WaitOnBusy( );
    }
}

//SPI读指令
void Lora_ReadCommand( tLoraCmd command, uint8_t *buffer, uint16_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );

    NSS_LOW();
    tx = command;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    tx = 0;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    for( uint16_t i = 0; i < size; i++ )
    {
    	HAL_SPI_TransmitReceive(&hspi4, &tx, buffer+i, 1, 100);
    }

    NSS_HIGH();

    Lora_WaitOnBusy( );
}

//写寄存器
void Lora_WriteRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );

    NSS_LOW();

    tx = RADIO_WRITE_REGISTER;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte(RADIO_WRITE_REGISTER); //SPI_InOut((uint8_t)RADIO_WRITE_REGISTER);
    tx = (uint8_t)((address & 0xff00u) >> 8u);
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)((addr & 0xff00u) >> 8u)); //SPI_InOut((uint8_t)((addr & 0xff00u) >> 8u));
    tx = (uint8_t)(address & 0x00ffu);
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);

    for( uint16_t i = 0; i < size; i++ )
    {
    	HAL_SPI_TransmitReceive(&hspi4, buffer+i, &rx, 1, 100);
    }

    NSS_HIGH();

    Lora_WaitOnBusy( );
}

void Lora_WriteRegister( uint16_t address, uint8_t value )
{
    Lora_WriteRegisters( address, &value, 1 );
}

void Lora_ReadRegisters( uint16_t address, uint8_t *buffer, uint16_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );

    NSS_LOW();
    tx = RADIO_READ_REGISTER;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)RADIO_READ_REGISTER); //SPI_InOut((uint8_t)RADIO_READ_REGISTER);
    tx = (uint8_t)((address & 0xff00u) >> 8u);
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)((addr & 0xff00u) >> 8u)); //SPI_InOut((uint8_t)((addr & 0xff00u) >> 8u));
    tx = (uint8_t)(address & 0x00ffu);
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)(addr & 0x00ffu)); //SPI_InOut((uint8_t)(addr & 0x00ffu));
    tx = 0;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte(0x00); //SPI_InOut(0); //status
    for (uint16_t i=0; i<size; i++) {
    	HAL_SPI_TransmitReceive(&hspi4, &tx, buffer+i, 1, 100); //data[i] = spiReadByte();
    }
    NSS_HIGH();

    Lora_WaitOnBusy( );
}

uint8_t Lora_ReadRegister( uint16_t address )
{
    uint8_t data;
    Lora_ReadRegisters( address, &data, 1 );
    return data;
}

void Lora_WriteBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );

    NSS_LOW();

    tx = RADIO_WRITE_BUFFER;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)RADIO_WRITE_BUFFER); //SPI_InOut((uint8_t)RADIO_WRITE_BUFFER);
    tx = offset;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte(offset); //SPI_InOut(offset); //相对于 TxBufferBaseAddress 的偏移量
    for (uint16_t i=0; i<size; i++) {
    	HAL_SPI_TransmitReceive(&hspi4, buffer+i, &rx, 1, 100); //spiWriteByte(data[i]); //SPI_InOut(data[i]);
    }
    NSS_HIGH();

    Lora_WaitOnBusy( );
}

//从数据缓冲区中读取数据
void Lora_ReadBuffer( uint8_t offset, uint8_t *buffer, uint8_t size )
{
	uint8_t tx,rx;
    Lora_CheckDeviceReady( );

    NSS_LOW();

    tx = RADIO_READ_BUFFER;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100); //spiWriteByte((uint8_t)RADIO_WRITE_BUFFER); //SPI_InOut((uint8_t)RADIO_WRITE_BUFFER);
    tx = offset;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    tx = 0;
    HAL_SPI_TransmitReceive(&hspi4, &tx, &rx, 1, 100);
    for (uint16_t i=0; i<size; i++) {
       	HAL_SPI_TransmitReceive(&hspi4, &tx, buffer+i, 1, 100); //data[i] = spiReadByte();
    }
    NSS_HIGH();

    Lora_WaitOnBusy( );
}


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

RadioPacketTypes_t Lora_GetPacketType( void )
{
    return PacketType;
}


void Lora_GetRxBufferStatus( uint8_t *payloadLength, uint8_t *rxStartBufferPointer )
{
    uint8_t status[2];

    Lora_ReadCommand( RADIO_GET_RXBUFFERSTATUS, status, 2 );

    // In case of LORA fixed header, the payloadLength is obtained by reading
    // the register REG_LR_PAYLOADLENGTH
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
void Lora_SetPayload( uint8_t *payload, uint8_t size )
{
    Lora_WriteBuffer( 0x00, payload, size );
}

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


void Lora_SetTx( uint32_t timeout )
{
    uint8_t buf[3];

    OperatingMode = MODE_TX;

    buf[0] = ( uint8_t )( ( timeout >> 16 ) & 0xFF );
    buf[1] = ( uint8_t )( ( timeout >> 8 ) & 0xFF );
    buf[2] = ( uint8_t )( timeout & 0xFF );
    Lora_WriteCommand( RADIO_SET_TX, buf, 3 );
}

void Lora_SendPayload( uint8_t *payload, uint8_t size, uint32_t timeout )
{
    Lora_SetPayload( payload, size );
    Lora_SetTx( timeout );
}

void Lora_SetRx( uint32_t timeout )
{
    uint8_t buf[3];
    OperatingMode = MODE_RX;

    buf[0] = ( uint8_t )( ( timeout >> 16 ) & 0xFF );
    buf[1] = ( uint8_t )( ( timeout >> 8 ) & 0xFF );
    buf[2] = ( uint8_t )( timeout & 0xFF );
    Lora_WriteCommand( RADIO_SET_RX, buf, 3 );
}

void Lora_SetTxContinuousWave( void )
{
    Lora_WriteCommand( RADIO_SET_TXCONTINUOUSWAVE, 0, 0 );
}


void Lora_SetStopRxTimerOnPreambleDetect( uint8_t enable )
{
    Lora_WriteCommand( RADIO_SET_STOPRXTIMERONPREAMBLE, ( uint8_t* )&enable, 1 );
}

void Lora_SetLoRaSymbNumTimeout( uint8_t SymbNum )
{
    Lora_WriteCommand( RADIO_SET_LORASYMBTIMEOUT, &SymbNum, 1 );
}

void Lora_SetRegulatorMode( RadioRegulatorMode_t mode )
{
    Lora_WriteCommand( RADIO_SET_REGULATORMODE, ( uint8_t* )&mode, 1 );
}

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


void Lora_SetPaConfig( uint8_t paDutyCycle, uint8_t hpMax, uint8_t deviceSel, uint8_t paLut )
{
    uint8_t buf[4];

    buf[0] = paDutyCycle;
    buf[1] = hpMax;
    buf[2] = deviceSel;
    buf[3] = paLut;
    Lora_WriteCommand( RADIO_SET_PACONFIG, buf, 4 );
}


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

uint16_t Lora_GetIrqStatus( void )
{
    uint8_t irqStatus[2];
    Lora_ReadCommand( RADIO_GET_IRQSTATUS, irqStatus, 2 );
    return ( irqStatus[0] << 8 ) | irqStatus[1];
}


void Lora_SetDio2AsRfSwitchCtrl( uint8_t enable )
{
    Lora_WriteCommand( RADIO_SET_RFSWITCHMODE, &enable, 1 );
}



//101////////////////////////////////////////////////////////////////////
void Lora_SetRfFrequency( uint32_t frequency )
{
    uint8_t buf[4];
    uint32_t freq = 0;

    Lora_CalibrateImage( frequency );

    freq = ( uint32_t )( ( double )frequency / ( double )FREQ_STEP );
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

void Lora_SetPacketType( RadioPacketTypes_t packetType )
{
    // Save packet type internally to avoid questioning the radio
    PacketType = packetType;
    Lora_WriteCommand( RADIO_SET_PACKETTYPE, ( uint8_t* )&packetType, 1 );
}


/**
 * @brief 设置LoRa发送参数
 *
 * @param power 发射功率，范围：14~22
 * @param rampTime 发射功率变化时间
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


//101////////////////////////////////////////////////////////////////////
void Lora_SetModulationParams( ModulationParams_t *modulationParams )
{
//    uint8_t n;
//    uint32_t tempVal = 0;
//    uint8_t buf[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
//
//    // Check if required configuration corresponds to the stored packet type
//    // If not, silently update radio packet type
//    if( PacketType != modulationParams->PacketType )
//    {
//        Lora_SetPacketType( modulationParams->PacketType );
//    }
//
//    switch( modulationParams->PacketType )
//    {
//        case PACKET_TYPE_GFSK:
//            n = 8;
//            tempVal = ( uint32_t )( 32 * ( ( double )XTAL_FREQ / ( double )modulationParams->Params.Gfsk.BitRate ) );
//            buf[0] = ( tempVal >> 16 ) & 0xFF;
//            buf[1] = ( tempVal >> 8 ) & 0xFF;
//            buf[2] = tempVal & 0xFF;
//            buf[3] = modulationParams->Params.Gfsk.ModulationShaping;
//            buf[4] = modulationParams->Params.Gfsk.Bandwidth;
//            tempVal = ( uint32_t )( ( double )modulationParams->Params.Gfsk.Fdev / ( double )FREQ_STEP );
//            buf[5] = ( tempVal >> 16 ) & 0xFF;
//            buf[6] = ( tempVal >> 8 ) & 0xFF;
//            buf[7] = ( tempVal& 0xFF );
//            Lora_WriteCommand( RADIO_SET_MODULATIONPARAMS, buf, n );
//            break;
//        case PACKET_TYPE_LORA:
//            n = 4;
//            buf[0] = modulationParams->Params.LoRa.SpreadingFactor;
//            buf[1] = modulationParams->Params.LoRa.Bandwidth;
//            buf[2] = modulationParams->Params.LoRa.CodingRate;
//            buf[3] = modulationParams->Params.LoRa.LowDatarateOptimize;
//
//            Lora_WriteCommand( RADIO_SET_MODULATIONPARAMS, buf, n );
//
//            break;
//        default:
//            return;
//    }
	uint8_t n = 4;
    uint8_t buf[4] = { 0x00 };

	buf[0] = modulationParams->Params.LoRa.SpreadingFactor;
	buf[1] = modulationParams->Params.LoRa.Bandwidth;
	buf[2] = modulationParams->Params.LoRa.CodingRate;
	buf[3] = modulationParams->Params.LoRa.LowDatarateOptimize;

	Lora_WriteCommand( RADIO_SET_MODULATIONPARAMS, buf, n );

}


/**
 * @brief 设置LoRa数据包参数
 * @param packetParams 指向PacketParams_t结构体的指针，包含LoRa数据包的参数信息
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


void Lora_SetBufferBaseAddress( uint8_t txBaseAddress, uint8_t rxBaseAddress )
{
    uint8_t buf[2];

    buf[0] = txBaseAddress;
    buf[1] = rxBaseAddress;
    Lora_WriteCommand( RADIO_SET_BUFFERBASEADDRESS, buf, 2 );
}

RadioStatus_t Lora_GetStatus( void )
{
    uint8_t stat = 0;
    RadioStatus_t status;

    Lora_ReadCommand( RADIO_GET_STATUS, ( uint8_t * )&stat, 1 );
    status.Value = stat;
    return status;
}


//读取获取rssi Snr值,
void Lora_GetPacketStatus( PacketStatus_t *pktStatus )
{
    uint8_t status[3];

    Lora_ReadCommand( RADIO_GET_PACKETSTATUS, status, 3 );

    pktStatus->Params.LoRa.RssiPkt = -status[0] >> 1;
    ( status[1] < 128 ) ? ( pktStatus->Params.LoRa.SnrPkt = status[1] >> 2 ) : ( pktStatus->Params.LoRa.SnrPkt = ( ( status[1] - 256 ) >> 2 ) );
    pktStatus->Params.LoRa.SignalRssiPkt = -status[2] >> 1;
}


//清除Irq中断寄存器状态
void Lora_ClearIrqStatus( uint16_t irq )
{
    uint8_t buf[2];

    buf[0] = ( uint8_t )( ( ( uint16_t )irq >> 8 ) & 0x00FF );
    buf[1] = ( uint8_t )( ( uint16_t )irq & 0x00FF );
    Lora_WriteCommand( RADIO_CLR_IRQSTATUS, buf, 2 );
}


//Init lora
/**
 * @brief 初始化LoRa模块，并设置指定参数。
 * 
 * @param freq 频率（单位：MHz）。
 * @param power 功率级别。
 * @param sf 扩频因子。
 * @param bw 带宽。
 */
void Lora_Init(float freq, uint8_t power, uint8_t sf, uint8_t bw) {
    uint8_t buf[8] = {0};
	
	Lora_Reset( );
    // 唤醒LoRa模块,wait for busy 
    Lora_Wakeup( );

    // standby mode
    Lora_SetStandby( STDBY_RC );
    
    // 设置调节器DCDC
    Lora_SetRegulatorMode( USE_DCDC );
    
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
    SX126x.ModulationParams.Params.LoRa.CodingRate= 1;
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
    Lora_SetTxParams(power,RADIO_RAMP_40_US);
    
    // 设置射频频率
    Lora_SetRfFrequency( freq *1000000.0f );

	//tcxo enable
	#if USING_TCXO
	//tcxo enable
	buf[0] = 0x07;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0xaa;
	Lora_WriteCommand(RADIO_SET_TCXOMODE, buf, 4);
	Lora_WaitOnBusy();
#endif
}

/**
  *  @brief lora发送数据：发送完成后进入 Standby 模式
  *  @param data 发送数据地址
  *  @param len  发送长度
  *  @param ms   发送超时检测
  *  @retval 1:发送失败 0：发送成功
  */
bool Lora_Tx(uint8_t *data, uint8_t len, uint32_t ms){
	int timeoutMs = ms;
	uint16_t irqRegs = 0;
	if(ms < 200){
		timeoutMs = 1500;
	}
	Lora_ClearIrqStatus(IRQ_RADIO_ALL);
	LORA_TX_EN();
	Lora_SetDioIrqParams( IRQ_TX_DONE);
	SX126x.PacketParams.Params.LoRa.PayloadLength = len;
	Lora_SetPacketParams( &SX126x.PacketParams );
	Lora_SendPayload( data, len, 0 );

	while(timeoutMs > 0){
		irqRegs = Lora_GetIrqStatus();
		if( ( irqRegs & IRQ_TX_DONE ) == IRQ_TX_DONE ){
//			PRINTF("Tx Success \n");
			break;
		}else if((irqRegs & IRQ_RX_TX_TIMEOUT) == IRQ_RX_TX_TIMEOUT){
			PRINTF("Tx Irq Timeout .\n");
			break;
		}
		timeoutMs -= 20;
		DelayMs(20);
	}
	if(timeoutMs <= 0){
		PRINTF("\t#Tx timeout(%ld ms) for %d bytes @ %ld.\n", ms, len, Rtc_GetTimestamp());
		return 0;
	}else{
		//PRINTF("\tTx(in %ld ms) for %d bytes @ %ld.\n",(ms-timeoutMs), len, Rtc_GetTimestamp());
		return 1;
	}
}


/**
 * @brief Sends a LoRa transmission request.
 * @param data Pointer to the data to be transmitted.
 * @param len Length of the data in bytes.
 * @param ms Timeout value in milliseconds.
 * 
 * @return Returns true if the transmission was successful, false otherwise.
 */
bool Lora_TxRequest(uint8_t *data, uint8_t len, uint32_t ms){
	int timeoutMs = ms;
	uint16_t irqRegs = 0;
	if(ms < 200){
		timeoutMs = 1500;
	}
	Lora_ClearIrqStatus(IRQ_RADIO_ALL);
	LORA_TX_EN();
	Lora_SetDioIrqParams( IRQ_TX_DONE);
	SX126x.PacketParams.Params.LoRa.PayloadLength = len;
	Lora_SetPacketParams( &SX126x.PacketParams );
	Lora_SendPayload( data, len, 0 );
	while(timeoutMs > 0){
		irqRegs = Lora_GetIrqStatus();
		if( ( irqRegs & IRQ_TX_DONE ) == IRQ_TX_DONE ){
	//		PRINTF("Tx Success \n");
			break;
		}else if((irqRegs & IRQ_RX_TX_TIMEOUT) == IRQ_RX_TX_TIMEOUT){
			PRINTF("Tx Irq Timeout .\n");
			break;
		}
		timeoutMs -= 20;
		DelayMs(20);
	}
	if(timeoutMs <= 0){
		PRINTF("\t#Tx timeout(%ld ms) for %d bytes @ %ld.\n", ms, len, Rtc_GetTimestamp());
		Lora_Listening();
		return false;
	}else{
		//PRINTF("\tTx(in %ld ms) for %d bytes @ %ld.\n",(ms-timeoutMs), len, Rtc_GetTimestamp());
		Lora_Listening();
		return true;
	}
}

void Lora_Rx(uint8_t *data, uint8_t *len, uint32_t ms){
	int32_t timeoutMs = ms;
	uint16_t irqRegs = 0;
	PacketStatus_t pktStatus;
	if(ms < 200){
		timeoutMs = 1500;
	}
	*len = 0;
	while(timeoutMs > 0){
		irqRegs = Lora_GetIrqStatus();
//		PRINTF("irqRegs = %d\n",irqRegs);
		if((irqRegs & IRQ_RX_DONE) == IRQ_RX_DONE){
			Lora_GetPayload(data,len,0xFF);
			Lora_GetPacketStatus( &pktStatus );
			Rssi = pktStatus.Params.LoRa.RssiPkt+20;
			break;
		}else if((irqRegs & IRQ_RX_TX_TIMEOUT) == IRQ_RX_TX_TIMEOUT){
			PRINTF("rx Irq timeout .\n");
			break;
		}
		timeoutMs -= 20;
		DelayMs(20);
	}
	if (*len == 0) {
		PRINTF("\tRx timeout(%ld ms) @ %ld.\n", ms, Rtc_GetTimestamp());
		Lora_Listening();
	} else {
		//PRINTF("\tRx(%d bytes in %ld ms) @ %ld.\n", *len, (ms-timeoutMs), Rtc_GetTimestamp());
		Lora_Listening();
	}
}

//发送信息，然后切换至接收模式
void Lora_RxRequest(uint8_t *data, uint8_t *len, uint32_t ms){
	int32_t timeoutMs = ms;
	uint16_t irqRegs = 0;
	PacketStatus_t pktStatus;
	if(ms < 200){
		timeoutMs = 1500;
	}
	*len = 0;
	while(timeoutMs > 0){
		irqRegs = Lora_GetIrqStatus();
//		PRINTF("irqRegs = %d\n",irqRegs);
		if((irqRegs & IRQ_RX_DONE) == IRQ_RX_DONE){
			Lora_GetPayload(data,len,0xFF);
			Lora_GetPacketStatus( &pktStatus );
			Rssi = pktStatus.Params.LoRa.RssiPkt+20;
			break;
		}else if((irqRegs & IRQ_RX_TX_TIMEOUT) == IRQ_RX_TX_TIMEOUT){
			PRINTF("rx Irq timeout .\n");
			break;
		}
		timeoutMs -= 20;
		DelayMs(20);
	}
	if (*len == 0) {
		PRINTF("\tRx timeout(%ld ms) @ %ld.\n", ms, Rtc_GetTimestamp());
	} else {
		//PRINTF("\tRx(%d bytes in %ld ms) @ %ld.\n", *len, (ms-timeoutMs), Rtc_GetTimestamp());
	}
	Lora_Listening();
}

void Lora_CheckData(uint8_t *data, uint8_t *len){
	*len = 0;
	uint16_t irqRegs = 0;
	PacketStatus_t pktStatus;
	irqRegs = Lora_GetIrqStatus();
	if((irqRegs & IRQ_RX_DONE) == IRQ_RX_DONE){
		Lora_GetPayload(data,len,0xFF);
		Lora_GetPacketStatus( &pktStatus );
		Rssi = pktStatus.Params.LoRa.RssiPkt+20;
		Lora_Listening();
	}else if(irqRegs != 0){
		PRINTF("\t#Irq(%d).\n", irqRegs);
		Lora_Listening();
	}
}

int8_t Lora_GetRssi(){
	return Rssi;
}

//监听模式: Single Rx, 无超时，收到信号后自动进入 STBY_RC 模式
void Lora_Listening(){
	uint8_t buf[3] = {0x00, 0x00, 0x00};
	Lora_ClearIrqStatus(IRQ_RADIO_ALL);
	LORA_RX_EN(); //天线切换至接收模式
	Lora_SetDioIrqParams( IRQ_RX_DONE);
	buf[0] = 0x96;
	Lora_WriteRegisters(0x08AC, buf, 1);
	Lora_SetRx(0);
}


void restartNode(uint16_t nodeId, uint8_t cmdTag){
	//仅单线发送
	uint8_t txBuf[LORA_BUFFER_LENGTH];

	*txBuf = eCmdRestart;
	*(txBuf + 1) = cmdTag;
	*((uint16_t *)(txBuf + 2)) = Gateway.gwId;
	*((uint16_t *)(txBuf + 4)) = nodeId; //nodeId;
	AddCrc(txBuf, 6);
	Lora_TxRequest(txBuf, 7, LORA_TX_TIMEOUT);
	PRINTF("NodeID/cmdTag(%04x/%04x) restart[%d] @ %lu.\n", nodeId, cmdTag,Rtc_GetTimestamp());
 	return;

}
