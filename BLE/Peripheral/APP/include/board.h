/**
 * @file board.h
 * @brief NONE
 * @author zhangpeng
 * @date 2026-1-30
 */
#ifndef __BOARD_H__
#define __BOARD_H__
#include <stdint.h>
#include <stdbool.h>
#include "CH58x_common.h"
#include "flash.h"
#include "ir_tab.h"
#define Default_DevId 0x1122 //默认的网关编号

/* Lora */
#define LORA_POWER 22				//lora发送功率：22
#define LORA_SF_LISTEN 10				//监听LORA的扩频因子:10 //Sf10+Bw7:  128b-1210ms, 112b-1050ms, 104b-1000ms(6只热量计上报数据:16*6+7),88b-880ms(5只热量计上报数据:16*5+7),72b-760ms(4只热量计上报数据),56b-600ms,27b-400ms(配置下发)
#define LORA_BW_LISTEN 0x04				//监听LORA的带宽:125K sx1268: 0x02-31.25k,0x0a-41.67k,0x03-62.5k,0x04-125k
#define LORA_SF_SCAN 8					//扫描LORA的扩频因子:8 //Sf8+Bw5: 71b-660ms(4台冷机上报数据) 43b-450ms(4台变频器上报数据) 15b-235ms(4路电流上报)
#define LORA_BW_SCAN 0x0a				//扫描LORA的带宽:41.7 sx1268: 0x02-31.25k,0x0a-41.67k,0x03-62.5k,0x04-125k

//旧版本(分体空调&三速开关)固定频点(0~31),基于场景普遍安装较分散,兼容普通节点统一改为Radio(0~4)*channel(0~9)版本
//节点以内置的频道 431.1/432.8/434.5/436.2/437.9 为参考，按不同的频点进行扫描注册。如果收到修改频道指令，则更新至 FLASH/EEPROM，重启后以新的频道扫描注册
typedef enum { //LORA频道定义
    eRadioCh0			= 0,	//Lora通信基础频率 431.1M eRadio431_1
    eRadioCh1			= 1,	//Lora通信基础频率 432.8M eRadio432_8
    eRadioCh2			= 2,	//Lora通信基础频率 434.5M eRadio434_5
    eRadioCh3			= 3,	//Lora通信基础频率 436.2M eRadio436_2
    eRadioCh4			= 4,	//Lora通信基础频率 437.9M eRadio437_9
    eRadioIllegal		= 5		//非法的LORA频道
} tRadio;
//LORA频点定义
//	监听频率: Radio+Frequency*0.170       (Radio+Frequency*0.170-0.0625  ~ Radio+Frequency*0.170+0.0625) SF10+BW7
    //Sf10+Bw7: 128b-1210ms, 112b-1050ms, 104b-1000ms(6只热量计上报数据:16*6+7),88b-880ms(5只热量计上报数据:16*5+7),72b-760ms(4只热量计上报数据),56b-600ms,27b-400ms(配置下发)
//	扫描频率: Radio+Frequency*0.170+0.085 (Radio+Frequency*0.170+0.06415 ~ Radio+Frequency*0.170+0.10585)SF8 +BW5
    //Sf8+Bw5: 71b-660ms(4台冷机上报数据) 43b-450ms(4台变频器上报数据) 15b-235ms(4路电流上报数据)
typedef enum { //LORA频点定义: 单位MHz 注册/监听频率 - 扫描频率
    eFrequency0			= 0,	//0.00 - 0.935 (+431.1/432.8/434.5/436.2/437.9)
    eFrequency1			= 1,	//0.17 - 1.105
    eFrequency2			= 2,	//0.34 - 1.275
    eFrequency3			= 3,	//0.51 - 1.445
    eFrequency4			= 4, 	//0.68 - 1.615
    eFrequency5			= 5,	//0.85 - 0.935
    eFrequency6			= 6,	//1.02 - 1.105
    eFrequency7			= 7,	//1.19 - 1.275
    eFrequency8			= 8,	//1.36 - 1.445
    eFrequency9			= 9, 	//1.53 - 1.615
    eFrequencyIllegal	=10
} tFrequency;

/* Infrared */




/* Dev Info */
#define DevType  54	// eDeviceAirConditioner(分体空调)
#define DevTag   50
// ：开关状态(u8:0-关,1-开)、运行模式(u8:0-自动 1-制冷 2-除湿 3-送风 4-制热)、运行时间(u16-0~1440分钟)、设定温度(sf)、环境温度(sf)、用电量(U16)、故障码(u16-D0~15:通信模块故障，红外模块，状态检测，时间参数，温度参数，人感参数，人数参数，待机功率 异常，实时时钟，温度检测，脱机运行，存储参数，预存电量用完，C相参数，B相参数，A相参�??)、查询�??(U16)
											//value0(tinyint),value1(tinyint),value2(smallint),value3(float),value4(float),value5(smallint),value6(smallint),value7(smallint)


#define IRBUFSIZE 128
typedef enum{
    IR_TYPE_NORMAL = 0,
    IR_TYPE_MATCH  = 1,
    IR_TYPE_LEARNing  = 2
}IR_CMD_TYPE_t;
typedef struct{
    uint8_t rxlen:7; 
    uint8_t isFinish:1;
    IR_CMD_TYPE_t type;
    uint8_t rxbuf[IRBUFSIZE];
    uint8_t txbuf[IRBUFSIZE];
}IRBUF_t; //uart3 红外通讯数据包

/* Debug */
#define _BT_INFO_ 1 // 打印蓝牙调试信息
#define _LORA_INFO_ 1 // 打印LORA调试信息
#define _Sensor_INFO_ 1 // 打印传感器调试信息
#define _IR_INFO_ 1 //打印红外调试信息

typedef enum{
    Status_Uninit = 0, // 未初始化
    Status_Logining = 1, // lora初始化,发送注册请求
    Status_CheckSend = 2, // 检查是否发送完成
    Status_RecvLogin = 3, // 接收注册响应
    Status_Connected = 4, // 已连接
    Status_CheckData = 5, // 检查数据是否发送完成
}LoraStatus_t;

typedef enum{
    Mode_Auto = 0, // 自动
    Mode_Cool = 1, // 制冷
    Mode_Dry = 2, // 抽湿
    Mode_Fan = 3, // 送风
    Mode_Heat = 4, // 制热
}Mode_t;

typedef enum{
    Wind_Auto = 0, // 自动风向
    Wind_Low = 1, // 风向低
    Wind_Mid = 2, // 风向中
    Wind_High = 3, // 风向高
}Wind_t;

typedef enum{
    PowerOn = 0, // 开
    PowerOff = 1, // 关
}OnOff_t;

#define MAGIC_CODE 0x55AA //首次上电判断

#define MAX_IR_LEARNNUM 10
typedef struct{
    uint8_t type; //红外组合命令,暂不做定义，仅作为区分
    uint8_t cmd[256];
}IR_LEARNING_t;


//设备结构体,存储设备信息,同时保存进DataFlash,上电时读取
typedef struct{
    uint16_t magicCode; //用于检测是否首次上电 0x55AA
    uint16_t nodeId; // 节点ID
    uint16_t radio;  //lora通道(0~4)
    uint8_t channel; //lora频点(0~9)
    LoraStatus_t loraStatus; // lora状态
    uint32_t lastReportTime; // 上次上报时间戳
    uint32_t lastOnTime; // 上次空调开机时间,用于计算运行时间(由负载进行计算)
    uint8_t irIdx; // 空调号索引(83),对应g_arc_info中的空调品牌
    uint8_t irType; // 空调类型(<200),对应g_arc_info中各品牌的指令下标
    uint8_t learnNum; //学习指令个数(0~MAX_IR_LEARNNUM)
    IR_LEARNING_t learnCode[MAX_IR_LEARNNUM];
    //上报数据
    OnOff_t onOff; // 空调开关状态,0:关 1:开
    uint16_t temp; // 环境温度
    Mode_t mode; // 空调运行模式
    uint16_t setTemp; // 设定温度
    Wind_t wind; // 风速
    uint16_t runTime; // 空调运行时间,单位:分钟
    uint16_t loadPower; // 负载功率,单位:W*10
    uint16_t isLock; // 本地操作禁用? 暂不考虑,应只有远程控制,本地由遥控器控制
    union{  // 故障码(0:正常 \\ 异常>> bit 0:lora离线 1:红外学习异常 2：红外匹配异常(未匹配设备或找不到索引或索引错误[或无反馈?]) 3:ad转换异常 4:功率转换异常 5:flash操作异常)
        uint16_t u16Val; 
        struct{
            uint16_t lora:1; // lora离线
            uint16_t irLearn:1; // 红外学习异常(红外自匹配或学习错误)
            uint16_t irMatch:1; // 红外匹配异常(未匹配设备或找不到索引或索引错误)
            uint16_t ad:1; // ad转换异常
            uint16_t power:1; // 功率转换异常
            uint16_t flash:1; //flash操作异常
        }bit;
    }errorCode;
}t_dev;

extern t_dev Dev;

//led 
#define LED0_PIN GPIO_Pin_19
#define LED1_PIN GPIO_Pin_18
static inline void Led_Init(void){
    GPIOB_SetBits(LED0_PIN | LED1_PIN);
    GPIOB_ModeCfg(LED0_PIN | LED1_PIN,GPIO_ModeOut_PP_5mA);
}
#define LED0(x) (x?GPIOB_ResetBits(LED0_PIN):GPIOB_SetBits(LED0_PIN))
#define LED1(x) (x?GPIOB_ResetBits(LED1_PIN):GPIOB_SetBits(LED1_PIN))
#define LED0_TOGGLE() (GPIOB_ReadPortPin(LED0_PIN)?GPIOB_ResetBits(LED0_PIN):GPIOB_SetBits(LED0_PIN))
#define LED1_TOGGLE() (GPIOB_ReadPortPin(LED1_PIN)?GPIOB_ResetBits(LED1_PIN):GPIOB_SetBits(LED1_PIN))

//blueTooth 蓝牙相关配置
#define BT_DEFAULT_ADVERTISING_INTERVAL         80 //广播间隔，单位 0.625ms，80=50ms。
#define BT_DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL  //可发现模式(一般可发现)
#define BT_DEFAULT_DESIRED_MIN_CONN_INTERVAL    6  //最小连接间隔(单位1.25ms)
#define BT_DEFAULT_DESIRED_MAX_CONN_INTERVAL    100 //最大连接间隔(单位1.25ms)
#define BT_DEFAULT_DESIRED_SLAVE_LATENCY        0   //从机延时
#define BT_DEFAULT_DESIRED_CONN_TIMEOUT         100 //连接监督超时时间(单位:10ms)1s
#define BT_COMPANY_ID                           0x07D7  //蓝牙厂商 ID
#define BT_DEVICE_NAME                          "Wch Bt 123" //设备名
// #define BT_DEFAULT_MAC_ADDR                     {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02} //BLE MAC 地址 默认由芯片地址随机生成

//uilt functions
static inline void PrintHex(char *msg, uint8_t *buffer, uint16_t size){
    uint16_t i;
	if (buffer == NULL) {
		return;
	}
    if (msg != NULL) {
		PRINT("%s(%d bytes): ", msg, size);
	}
    for (i = 0; i < size; i++) {
		PRINT("%02x ", buffer[i]);
	}
	PRINT("\n");
}

#endif
