/*
 * sx1262.h
 *
 *  Created on: Mar 4, 2024
 *      Author: zhangpeng
 */

#ifndef INC_SX126X_H_
	#define INC_SX126X_H_
	//#define XTAL_FREQ		32000000		//1278
	//#define FREQ_STEP		61.03515625		//((double)(XTAL_FREQ / pow(2.0, 19.0)))
	//#define XTAL_FREQ		52000000		//1280
	//#define FREQ_STEP		198.3642578125	//((double)(XTAL_FREQ / pow(2.0, 19.0)))
	#define XTAL_FREQ		32000000		//126x
	#define FREQ_STEP		0.953674316		//((double)(XTAL_FREQ / pow(2.0, 25.0)))
	#define PREAMBLE_LENGTH 6
	#define LORA_BUFFER_LENGTH 256		//Lora数据包长度 256
	#define LORA_ERR_CODING 1 //纠错码1 - 4/5
	#define LORA_TX_TIMEOUT 1200
	#define LORA_RX_TIMEOUT 1500
	#define MAX_LORA_POWER 22				//Lora发射功率最大值(Sx1278的最大值是20，Sx1262的最大值为22)
	#define MIN_LORA_POWER 14				//Lora发射功率最小值
	#define RSSI_OFFSET 20

	#define LORA_READY_TIMEOUT 100			//检查Lora是否Ready(Busy引脚为低电平表示Readu)的超时为50ms
	#define LORA_SPI_TIMEOUT 100			//SPI通信超时为100个tick

	//#define LORA_LISTEN_TX_TIMEOUT 800	//LISTEN通道TX超时
	//#define LORA_LISTEN_RX_TIMEOUT 1200	//LISTEN通道RX超时
	//#define LORA_SCAN_TX_TIMEOUT 1000		//SCAN通道TX超时
	//#define LORA_SCAN_RX_TIMEOUT 1200		//SCAN通道RX超时
	typedef enum {
		LORA_EC1			= (uint8_t)0x01,	//4/5
		LORA_Ec2			= (uint8_t)0x02,	//4/6
		LORA_EC3			= (uint8_t)0x03,	//4/7
		LORA_EC4			= (uint8_t)0x04,	//4/8
	} tLoraEc;
	//SX126X cmd
	typedef enum {
		RADIO_GET_STATUS                        = 0xC0,
		RADIO_WRITE_REGISTER                    = 0x0D,
		RADIO_READ_REGISTER                     = 0x1D,
		RADIO_WRITE_BUFFER                      = 0x0E,
		RADIO_READ_BUFFER                       = 0x1E,
		RADIO_SET_SLEEP                         = 0x84,
		RADIO_SET_STANDBY                       = 0x80,
		RADIO_SET_FS                            = 0xC1,
		RADIO_SET_TX                            = 0x83,
		RADIO_SET_RX                            = 0x82,
		RADIO_SET_RXDUTYCYCLE                   = 0x94,
		RADIO_SET_CAD                           = 0xC5,
		RADIO_SET_TXCONTINUOUSWAVE              = 0xD1,
		RADIO_SET_TXCONTINUOUSPREAMBLE          = 0xD2,
		RADIO_SET_PACKETTYPE                    = 0x8A,
		RADIO_GET_PACKETTYPE                    = 0x11,
		RADIO_SET_RFFREQUENCY                   = 0x86,
		RADIO_SET_TXPARAMS                      = 0x8E,
		RADIO_SET_PACONFIG                      = 0x95,
		RADIO_SET_CADPARAMS                     = 0x88,
		RADIO_SET_BUFFERBASEADDRESS             = 0x8F,
		RADIO_SET_MODULATIONPARAMS              = 0x8B,
		RADIO_SET_PACKETPARAMS                  = 0x8C,
		RADIO_GET_RXBUFFERSTATUS                = 0x13,
		RADIO_GET_PACKETSTATUS                  = 0x14,
		RADIO_GET_RSSIINST                      = 0x15,
		RADIO_GET_STATS                         = 0x10,
		RADIO_RESET_STATS                       = 0x00,
		RADIO_CFG_DIOIRQ                        = 0x08,
		RADIO_GET_IRQSTATUS                     = 0x12,
		RADIO_CLR_IRQSTATUS                     = 0x02,
		RADIO_CALIBRATE                         = 0x89,
		RADIO_CALIBRATEIMAGE                    = 0x98,
		RADIO_SET_REGULATORMODE                 = 0x96,
		RADIO_GET_ERROR                         = 0x17,
		RADIO_CLR_ERROR                         = 0x07,
		RADIO_SET_TCXOMODE                      = 0x97,
		RADIO_SET_TXFALLBACKMODE                = 0x93,
		RADIO_SET_RFSWITCHMODE                  = 0x9D,
		RADIO_SET_STOPRXTIMERONPREAMBLE         = 0x9F,
		RADIO_SET_LORASYMBTIMEOUT               = 0xA0
	} tLoraCmd;

	typedef enum
	{
	    PACKET_TYPE_GFSK                        = 0x00,
	    PACKET_TYPE_LORA                        = 0x01,
	    PACKET_TYPE_NONE                        = 0x0F,
	}RadioPacketTypes_t;
	typedef enum
	{
	    LORA_PACKET_VARIABLE_LENGTH             = 0x00,         //!< The packet is on variable size, header included
	    LORA_PACKET_FIXED_LENGTH                = 0x01,         //!< The packet is known on both sides, no header included in the packet
	    LORA_PACKET_EXPLICIT                    = LORA_PACKET_VARIABLE_LENGTH,
	    LORA_PACKET_IMPLICIT                    = LORA_PACKET_FIXED_LENGTH,
	}RadioLoRaPacketLengthsMode_t;
	typedef enum
	{
	    LORA_CRC_ON                             = 0x01,         //!< CRC activated
	    LORA_CRC_OFF                            = 0x00,         //!< CRC not used
	}RadioLoRaCrcModes_t;

	/*!
	 * \brief Represents the IQ mode for LoRa packet type
	 */
	typedef enum
	{
	    LORA_IQ_NORMAL                          = 0x00,
	    LORA_IQ_INVERTED                        = 0x01,
	}RadioLoRaIQModes_t;

	typedef enum
	{
	    LORA_SF5                                = 0x05,
	    LORA_SF6                                = 0x06,
	    LORA_SF7                                = 0x07,
	    LORA_SF8                                = 0x08,
	    LORA_SF9                                = 0x09,
	    LORA_SF10                               = 0x0A,
	    LORA_SF11                               = 0x0B,
	    LORA_SF12                               = 0x0C,
	}RadioLoRaSpreadingFactors_t;
	typedef enum
	{
	    MODE_SLEEP                              = 0x00,         //! The radio is in sleep mode
	    MODE_STDBY_RC,                                          //! The radio is in standby mode with RC oscillator
	    MODE_STDBY_XOSC,                                        //! The radio is in standby mode with XOSC oscillator
	    MODE_FS,                                                //! The radio is in frequency synthesis mode
	    MODE_TX,                                                //! The radio is in transmit mode
	    MODE_RX,                                                //! The radio is in receive mode
	    MODE_RX_DC,                                             //! The radio is in receive duty cycle mode
	    MODE_CAD                                                //! The radio is in channel activity detection mode
	}RadioOperatingModes_t;
	/*!
	 * \brief Represents the bandwidth values for LoRa packet type
	 */
	typedef enum
	{
	    LORA_BW_500                             = 6,
	    LORA_BW_250                             = 5,
	    LORA_BW_125                             = 4,
	    LORA_BW_062                             = 3,
	    LORA_BW_041                             = 10,
	    LORA_BW_031                             = 2,
	    LORA_BW_020                             = 9,
	    LORA_BW_015                             = 1,
	    LORA_BW_010                             = 8,
	    LORA_BW_007                             = 0,
	}RadioLoRaBandwidths_t;
	typedef enum
	{
	    STDBY_RC                                = 0x00,
	    STDBY_XOSC                              = 0x01,
	}RadioStandbyModes_t;

	/*!
	 * \brief Declares the power regulation used to power the device
	 *
	 * This command allows the user to specify if DC-DC or LDO is used for power regulation.
	 * Using only LDO implies that the Rx or Tx current is doubled
	 */
	typedef enum
	{
	    USE_LDO                                 = 0x00, // default
	    USE_DCDC                                = 0x01,
	}RadioRegulatorMode_t;


	/*!
	 * \brief Represents the coding rate values for LoRa packet type
	 */
	typedef enum
	{
	    LORA_CR_4_5                             = 0x01,
	    LORA_CR_4_6                             = 0x02,
	    LORA_CR_4_7                             = 0x03,
	    LORA_CR_4_8                             = 0x04,
	}RadioLoRaCodingRates_t;
	typedef struct
	{
	    RadioPacketTypes_t                   PacketType;        //!< Packet to which the modulation parameters are referring to.
	    struct
	    {
	        struct
	        {
	            RadioLoRaSpreadingFactors_t  SpreadingFactor;   //!< Spreading Factor for the LoRa modulation
	            RadioLoRaBandwidths_t        Bandwidth;         //!< Bandwidth for the LoRa modulation
	            RadioLoRaCodingRates_t       CodingRate;        //!< Coding rate for the LoRa modulation
	            uint8_t                      LowDatarateOptimize; //!< Indicates if the modem uses the low datarate optimization
	        }LoRa;
	    }Params;                                                //!< Holds the modulation parameters structure
	}ModulationParams_t;

#define CRC_IBM_SEED                                0xFFFF

/*!
 * \brief LFSR initial value to compute CCIT type CRC
 */
#define CRC_CCITT_SEED                              0x1D0F

/*!
 * \brief Polynomial used to compute IBM CRC
 */
#define CRC_POLYNOMIAL_IBM                          0x8005

/*!
 * \brief Polynomial used to compute CCIT CRC
 */
#define CRC_POLYNOMIAL_CCITT                        0x1021

/*!
 * \brief The address of the register holding the first byte defining the CRC seed
 *
 */
#define REG_LR_CRCSEEDBASEADDR                      0x06BC

/*!
 * \brief The address of the register holding the first byte defining the CRC polynomial
 */
#define REG_LR_CRCPOLYBASEADDR                      0x06BE

/*!
 * \brief The address of the register holding the first byte defining the whitening seed
 */
#define REG_LR_WHITSEEDBASEADDR_MSB                 0x06B8
#define REG_LR_WHITSEEDBASEADDR_LSB                 0x06B9

/*!
 * \brief The address of the register holding the packet configuration
 */
#define REG_LR_PACKETPARAMS                         0x0704

/*!
 * \brief The address of the register holding the payload size
 */
#define REG_LR_PAYLOADLENGTH                        0x0702

/*!
 * \brief The addresses of the registers holding SyncWords values
 */
#define REG_LR_SYNCWORDBASEADDRESS                  0x06C0

/*!
 * \brief The addresses of the register holding LoRa Modem SyncWord value
 */
#define REG_LR_SYNCWORD                             0x0740

/*!
 * Syncword for Private LoRa networks
 */
#define LORA_MAC_PRIVATE_SYNCWORD                   0x1424

/*!
 * Syncword for Public LoRa networks
 */
#define LORA_MAC_PUBLIC_SYNCWORD                    0x3444

/*!
 * The address of the register giving a 4 bytes random number
 */
#define RANDOM_NUMBER_GENERATORBASEADDR             0x0819

/*!
 * The address of the register holding RX Gain value (0x94: power saving, 0x96: rx boosted)
 */
#define REG_RX_GAIN                                 0x08AC

/*!
 * Change the value on the device internal trimming capacitor
 */
#define REG_XTA_TRIM                                0x0911
	typedef enum
	{
	    RADIO_RAMP_10_US                        = 0x00,
	    RADIO_RAMP_20_US                        = 0x01,
	    RADIO_RAMP_40_US                        = 0x02,
	    RADIO_RAMP_80_US                        = 0x03,
	    RADIO_RAMP_200_US                       = 0x04,
	    RADIO_RAMP_800_US                       = 0x05,
	    RADIO_RAMP_1700_US                      = 0x06,
	    RADIO_RAMP_3400_US                      = 0x07,
	}RadioRampTimes_t;
	typedef enum
	{
	    IRQ_RADIO_NONE                          = 0x0000,
	    IRQ_TX_DONE                             = 0x0001,
	    IRQ_RX_DONE                             = 0x0002,
	    IRQ_PREAMBLE_DETECTED                   = 0x0004,
	    IRQ_SYNCWORD_VALID                      = 0x0008,
	    IRQ_HEADER_VALID                        = 0x0010,
	    IRQ_HEADER_ERROR                        = 0x0020,
	    IRQ_CRC_ERROR                           = 0x0040,
	    IRQ_CAD_DONE                            = 0x0080,
	    IRQ_CAD_ACTIVITY_DETECTED               = 0x0100,
	    IRQ_RX_TX_TIMEOUT                       = 0x0200,
	    IRQ_RADIO_ALL                           = 0xFFFF,
	}RadioIrqMasks_t;
/*!
 * Set the current max value in the over current protection
 */
#define REG_OCP                                     0x08E7
	typedef union RadioStatus_u
	{
	    uint8_t Value;
	    struct
	    {   //bit order is lsb -> msb
	        uint8_t Reserved  : 1;  //!< Reserved
	        uint8_t CmdStatus : 3;  //!< Command status
	        uint8_t ChipMode  : 3;  //!< Chip mode
	        uint8_t CpuBusy   : 1;  //!< Flag for CPU radio busy
	    }Fields;
	}RadioStatus_t;
	/*!
	 * \brief The type describing the packet parameters for every packet types
	 */
	typedef struct
	{
	    RadioPacketTypes_t                    PacketType;        //!< Packet to which the packet parameters are referring to.
	    struct
	    {
	        struct
	        {
	            uint16_t                     PreambleLength;    //!< The preamble length is the number of LoRa symbols in the preamble
	            RadioLoRaPacketLengthsMode_t HeaderType;        //!< If the header is explicit, it will be transmitted in the LoRa packet. If the header is implicit, it will not be transmitted
	            uint8_t                      PayloadLength;     //!< Size of the payload in the LoRa packet
	            RadioLoRaCrcModes_t          CrcMode;           //!< Size of CRC block in LoRa packet
	            RadioLoRaIQModes_t           InvertIQ;          //!< Allows to swap IQ for LoRa packet
	        }LoRa;
	    }Params;                                                //!< Holds the packet parameters structure
	}PacketParams_t;
	typedef struct
	{
	    RadioPacketTypes_t                    packetType;      //!< Packet to which the packet status are referring to.
	    struct
	    {
	        struct
	        {
	            uint8_t RxStatus;
	            int8_t RssiAvg;                                //!< The averaged RSSI
	            int8_t RssiSync;                               //!< The RSSI measured on last packet
	            uint32_t FreqError;
	        }Gfsk;
	        struct
	        {
	            int8_t RssiPkt;                                //!< The RSSI of the last packet
	            int8_t SnrPkt;                                 //!< The SNR of the last packet
	            int8_t SignalRssiPkt;
	            uint32_t FreqError;
	        }LoRa;
	    }Params;
	}PacketStatus_t;
	typedef struct SX126x_s
	{
	    PacketParams_t PacketParams;
	    PacketStatus_t PacketStatus;
	    ModulationParams_t ModulationParams;
	}SX126x_t;

    uint8_t Lora_Init(float freq, uint8_t power, uint8_t sf, uint8_t bw);
	void Lora_Reset();
	void Lora_Sleep();
	void Lora_Standby(uint8_t xosc);
	int8_t Lora_GetRssi();
	void Lora_SetFrequency(float freq);
	void Lora_Listening();
	bool Lora_Tx(uint8_t *data, uint8_t len, uint32_t ms);
	bool Lora_TxRequest(uint8_t *data, uint8_t len, uint32_t ms);
	void Lora_Rx(uint8_t *data, uint8_t *len, uint32_t ms);
	void Lora_RxRequest(uint8_t *data, uint8_t *len, uint32_t ms);
	void Lora_CheckData(uint8_t *data, uint8_t *len);
#endif /* INC_SX126X_H_ */
