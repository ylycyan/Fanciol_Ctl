/**
 * @file board.h
 * @brief NONE
 * @author zhangpeng
 * @date 2026-1-30
 */
#ifndef __BOARD_H__
#define __BOARD_H__

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
typedef enum{
    80 ,//关闭空调
    81 ,//开启空调
    a1 ,//模式自动
    a2 ,//模式制冷
    a3 ,//模式抽湿
    a4 ,//模式送风
    a5 ,//模式制热
    40 ,//16°
    41 ,//17°
    42 ,//18°
    43 ,//19°
    44,//20°
    45,//21°
    46,//22°
    47,//23°
    48,//24°
    49,//25°
    4a,//26°
    4b,//27°
    4c,//28°
    4d,//29°
    4e,//30°
    4f,//31°
    51,//自动风速
    52,//风速低
    53,//风速中
    54,//风速高
    61,//风向向上(上下摆风)
    62,//风向中
    63,//风向向下(上下停摆)
    70,//自动风向关闭
    71,//自动风向打开
    B0,//睡眠关
    b1,//睡眠开
    c0,//辅热关
    c1,//辅热开
    d0,//灯光关
    d1,//灯光开
    e0,//节能关
    e1,//节能开
    96,//温度减
    97,//温度加
    9c,//快速制冷
    9d,//快速制热
    9e,//静音
    9f,//强经
    Ir_Illegal
}IR_CMD;



/* Debug */
#define _BT_INFO_ 1 // 打印蓝牙调试信息
#define _LORA_INFO_ 1 // 打印LORA调试信息
#define _Sensor_INFO_ 1 // 打印传感器调试信息




#endif