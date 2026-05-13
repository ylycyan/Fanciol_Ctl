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
#include <stdlib.h>
#include "CH58x_common.h"
#include "flash.h"
#include "ir_tab.h"
#define Default_DevId 0xBB01 //默认的设备编号
#define Default_Channel 5 
/* Lora */
#define LORA_POWER 22				//lora发送功率：22
#define LORA_SF_LISTEN 9				//监听LORA的扩频因子:10 //Sf10+Bw7:  128b-1210ms, 112b-1050ms, 104b-1000ms(6只热量计上报数据:16*6+7),88b-880ms(5只热量计上报数据:16*5+7),72b-760ms(4只热量计上报数据),56b-600ms,27b-400ms(配置下发)
#define LORA_BW_LISTEN 0x04				//监听LORA的带宽:125K sx1268: 0x02-31.25k,0x0a-41.67k,0x03-62.5k,0x04-125k
#define LORA_SF_SCAN 10					//扫描LORA的扩频因子:8 //Sf8+Bw5: 71b-660ms(4台冷机上报数据) 43b-450ms(4台变频器上报数据) 15b-235ms(4路电流上报)
#define LORA_BW_SCAN 0x05				//扫描LORA的带宽:41.7 sx1268: 0x02-31.25k,0x0a-41.67k,0x03-62.5k,0x04-125k

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

/* Dev Info */
#define DevType  20	// 20：Fancoil 54：eDeviceAirConditioner(分体空调)
#define DevTag   50
#define MAGIC_CODE 0x52AB //首次上电判断
#define AD_INTERVAL 10 //adc采集间隔
/*
20：Fancoil [value0(u16),value1(u8)value2(u16)value3(u16)value4(u16)value5(u16)]


54：eDeviceAirConditioner  [value0(tinyint),value1(tinyint),value2(smallint),value3(float),value4(float),value5(smallint),value6(smallint),value7(smallint)]
v0:开关状态(u8:0-关,1-开)
v1:运行模式(u8:0-自动 1-制冷 2-除湿 3-送风 4-制热)
v2:风速(sf:0-自动 1-低 2-中 3-高)
v3:用电量(U16)
v4:设定温度(sf)
v5:环境温度(sf)
v6:运行时间(u16-0~1440分钟)
v7:故障码(u16-D0~15:通信模块故障,红外模块,状态检测,时间参数...)


*/ 

/* Infrared */
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
#define _BT_INFO_ 0 // 打印蓝牙调试信息
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



#define MAX_IR_LEARNNUM 10
#define MAX_RULES       10

//红外学习结构体,一般空调红外控制包不超过230byte
typedef struct{  
    uint8_t enable:1; //该通道是否学习到数据
    uint8_t type:7; //红外组合命令(0：无匹配操作 1:开机 2:关机 3:制冷开机 4:制热开机)
    uint8_t cmd[256];
}IR_LEARNING_t;
extern IRBUF_t IrBuf;

//本地规则引擎 - 触发类型(3bit, 8种)
typedef enum {
    TRIG_NONE        = 0, // 无触发(禁用)
    TRIG_TIME        = 1, // 按时间触发(每日定时, 时:分)
    TRIG_TEMP_ABOVE  = 2, // 环境温度 > 阈值
    TRIG_TEMP_BELOW  = 3, // 环境温度 < 阈值
    TRIG_POWER_ABOVE = 4, // 实时功率 > 阈值(单位:W)
    TRIG_RUNTIME     = 5, // 累计运行时间 > 阈值(单位:分钟)
    TRIG_ENERGY      = 6, // 累计电量 > 阈值(单位:0.1kWh)
    TRIG_COMBINED    = 7, // 时间窗口 + 条件同时满足(AND)
} TrigType_t;

//本地规则引擎 - 动作类型(2bit, 4种)
typedef enum {
    ACT_TYPE_IR     = 0, // 发送红外指令空调控制(开关/模式/风速/温度)
    ACT_TYPE_LEARN  = 1, // 发送学习的红外码
    // ACT_TYPE_REPORT = 2, // 立即上报数据(无空调操作)
} ActType_t;

//本地规则引擎 - ctrl控制字位域
typedef struct {
    uint8_t enable    : 1;  // 规则使能 0:禁用 1:启用
    uint8_t trig_type : 3;  // TrigType_t 触发类型(0~7)
    uint8_t executed  : 1;  // 已执行标志(单次规则防重复, 每天0点自动清除)
    uint8_t reserved  : 3;  // 保留
} RuleCtrl_t;

//本地规则引擎 - 16字节紧凑规则结构体(精确对齐, 无填充)
typedef struct {
    // === Byte 0: 控制字 (位域) ===
    RuleCtrl_t ctrl;

    // === Byte 1: 调度/条件标志 ===
    uint8_t flags;
    // 时间触发: bit[0~6] = 周日~周六(1=启用), bit7 = 每月标志(1=忽略月份)
    // 条件触发: bit0 = 锁存(触发后保持直到手动复位)
    //           bit1 = 反向动作(条件不满足时执行, 即"回差恢复")

    // === Byte 2~3: 触发主值 (uint16, 按trig_type复用) ===
    uint16_t trig_val;
    // TRIG_TIME:        自00:00起的分钟数(0~1439, 精度1分钟)
    // TRIG_TEMP_ABOVE/BELOW: 温度×10 (200~350 = 20.0°C~35.0°C)
    // TRIG_POWER_ABOVE: 功率×10 (0~65535 = 0~6553.5W)
    // TRIG_RUNTIME:     累计分钟数(0~65535)
    // TRIG_ENERGY:      累计0.1kWh(0~6553.5)
    // TRIG_COMBINED:    起始时间(分钟)

    // === Byte 4~5: 触发副值 (uint16) ===
    uint16_t trig_val2;
    // TRIG_TIME:        停止时间分钟(0=不停止, 0xFFFF=单点触发)
    // TRIG_TEMP/POWER:  回差值(×10, 防抖用, 如回差5=0.5°C)
    // TRIG_RUNTIME:     0
    // TRIG_ENERGY:      0
    // TRIG_COMBINED:    停止时间分钟

    // === Byte 6~7: 调度扩展/阈值上限 (uint16) ===
    uint16_t sched;
    // TRIG_TIME:        月调度 bit[0~11] = 1~12月(1=启用), 全0=每月
    // TRIG_COMBINED:    月调度(同上)
    // TRIG_TEMP_ABOVE:  温度上限×10(区间触发时用, 0=单阈值)
    // TRIG_POWER_ABOVE: 功率上限×10(区间触发时用, 0=单阈值)
    // TRIG_TEMP_BELOW/其他: 条件持续时间(秒), 0=立即触发

    // === Byte 8~15: 动作载荷 (union, 8字节) ===
    union {
        uint8_t raw[8];
        struct {                            // ACT_AC: 空调控制
            uint8_t onOff   : 1;            // 0:关 1:开 (OnOff_t)
            uint8_t mode    : 3;            // Mode_t
            uint8_t wind    : 2;            // Wind_t
            uint8_t sweep   : 1;            // 扫风 0:关 1:开
            uint8_t sleep   : 1;            // 睡眠 0:关 1:开
            uint8_t temSet;                 // 设定温度(整数, 16~32)
            uint8_t reserved[6];            // 保留扩展
        } ir;
        struct {                            // ACT_LEARN: 学习码
            uint8_t learnIdx;               // 学习码索引(0~9)
            uint8_t reserved[7];
        } learn;
    } act;
} DEV_RULE_T;                               // 精确16字节, 无填充

//本地规则引擎 - 计量数据结构体(12字节)
typedef struct {
    uint32_t energy_wh;      // 累计电量 (0.1kWh, 最大6553.5)
    uint32_t run_minutes;    // 累计运行时间 (分钟, 最大~45天)
    uint16_t onoff_count;    // 开关机次数 (0~65535)
    uint16_t fault_count;    // 故障次数 (0~65535)
    uint32_t last_save_ts;   // 上次保存时间戳(秒)
} DEV_METER_T;              // 12字节

//设备结构体,存入DataFlash,掉电保存
typedef struct{
    uint16_t magicCode; //用于检测是否首次上电 0x55AA
    uint16_t nodeId; // 节点ID
    uint16_t channel;  //lora通道(0~32)？
    LoraStatus_t loraStatus; // lora状态
    uint32_t lastReportTime; // 上次上报时间戳
    uint32_t lastOnTime; // 上次空调开机时间,用于计算运行时间(由负载进行计算)
    float loraFrequency; //lora频率
    uint16_t gatewayId; //网关Id
    uint8_t scanCycle; //数据上报周期

    ActType_t irActType; // 动作类型(0~2) 0:红外控制 1:红外学习控制 //2:上报数据
    uint8_t irIdx; // 空调号索引(83),对应g_arc_info中的空调品牌
    uint16_t irType; // 空调类型(<200),对应g_arc_info中各品牌的指令下标
    uint8_t learnNum; //学习指令个数(0~10 MAX_IR_LEARNNUM)
    IR_LEARNING_t learnCode[MAX_IR_LEARNNUM];
    DEV_RULE_T    rules[MAX_RULES];       //本地规则引擎(定时/条件触发/计量,不上云, 160字节)
    DEV_METER_T   meter;                  //计量数据(12字节)
    //上报数据
    OnOff_t onOff; // 空调开关状态,0:关 1:开
    float tem; // 环境温度
    Mode_t ctlMode; // 空调运行模式
    uint16_t temSet; // 设定温度
    Wind_t wind; // 风速
    uint16_t runTime; // 空调运行时间,单位:分钟
    uint16_t loadPower; // 负载功率,单位:W*10
    uint8_t mode; // 控制模式(0:本地 1:远程)
    union{  // 故障码(0:正常 \\ 异常>> bit 0:lora离线 1:红外学习异常 2：红外匹配异常(未匹配设备或找不到索引或索引错误[或无反馈?]) 3:ad转换异常 4:功率转换异常 5:flash操作异常)
        uint16_t u16Val; 
        struct{
            uint16_t lora:1; // lora离线
            uint16_t irLearn:1; // 红外学习异常(红外自匹配或学习错误)
            uint16_t irMatch:1; // 红外匹配异常(未匹配设备或找不到索引或索引错误)
            uint16_t ad:1; // ad转换异常
            uint16_t power:1; // 功率转换异常
            uint16_t flash:1; //flash(内部eeprom)操作异常
        }bit;
    }errorCode;
}t_dev;
//cmd： len(1)cmd(1)Data(1)Crc(1)
// len(1)cmd(1:)Version(1)NodeId(2)channel(1)irIdx(1)
extern t_dev Dev;
extern uint32_t LocalTimestamp;
//led 
// #define LED_PORT GPIOB
#define LED_RED_PIN     GPIO_Pin_3
#define LED_GREEN_PIN   GPIO_Pin_2
#define LED_BLUE_PIN    GPIO_Pin_1
#define LED_WHITE_PIN   GPIO_Pin_0
static inline void Led_Init(void){
    GPIOB_ResetBits(LED_RED_PIN | LED_GREEN_PIN | LED_WHITE_PIN | LED_BLUE_PIN);
    GPIOB_ModeCfg(LED_RED_PIN | LED_GREEN_PIN | LED_WHITE_PIN | LED_BLUE_PIN,GPIO_ModeOut_PP_5mA);
}
// #define LED_(color) (GPIOB_SetBits(LED_##color##_PIN))
#define LED_RED(x) (x?GPIOB_SetBits(LED_RED_PIN):GPIOB_ResetBits(LED_RED_PIN))
#define LED_BLUE(x) (x?GPIOB_SetBits(LED_BLUE_PIN):GPIOB_ResetBits(LED_BLUE_PIN))
#define LED_WHITE(x) (x?GPIOB_SetBits(LED_WHITE_PIN):GPIOB_ResetBits(LED_WHITE_PIN))
#define LED_GREEN(x) (x?GPIOB_SetBits(LED_GREEN_PIN):GPIOB_ResetBits(LED_GREEN_PIN))

//blueTooth 蓝牙相关配置
#define BT_DEFAULT_ADVERTISING_INTERVAL         80
#define BT_DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL
#define BT_DEFAULT_DESIRED_MIN_CONN_INTERVAL    6
#define BT_DEFAULT_DESIRED_MAX_CONN_INTERVAL    12
#define BT_DEFAULT_DESIRED_SLAVE_LATENCY        0
#define BT_DEFAULT_DESIRED_CONN_TIMEOUT         1000
#define BT_COMPANY_ID                           0x07D7  //蓝牙厂商 ID
#define BT_DEVICE_NAME                          "matter lights" //设备名
// #define BT_DEFAULT_MAC_ADDR                     {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02} //BLE MAC 地址 默认由芯片地址随机生成

//蓝牙协议
typedef enum {
    BT_CMD_NULL     = 0, //空指令
    BT_CMD_OK       = 1, 
    BT_CMD_FAIL     = 2,
    BT_CMD_DATA     = 3, 
    BT_CMD_OPERATE  = 4, //同云端Operate+OperateTag+OperateParameter配置,[len(1)cmd(1:4)operate(1)operateTag(1)operateParameter(f:4)Crc(1)]
    BT_CMD_IRTRANS  = 5,
    BT_CMD_IRMATCH  = 6,  
    BT_CMD_IRLEARN  = 7,  //红外学习
    BT_CMD_SYSPARAMS= 8,  //系统参数
    BT_CMD_UPDATE   = 9,  //固件更新
    BT_CMD_RESET    = 10, //设备软重启
    BT_CMD_ILLEGAL  = 11,
}BT_CMD_t;

typedef enum{
    eBtSet = 0, //设置指令
    eBtGet = 1, //查询指令
}BT_SET_GET_t;

//ble帧结构
typedef struct{
    uint8_t len; //数据长度
    uint8_t cmd; 
    uint8_t dat[128];
    uint8_t checksum; 
}BT_FRAME_T;
static BT_FRAME_T BTFrame;

//uilt functions
extern void PrintHex(char *msg, uint8_t *buffer, uint16_t size);

//兼容普通节点板crc算法
extern void AddCrc(uint8_t *buf, uint16_t len);

extern int ChkCrc(uint8_t *buf, uint16_t len);

#define BITGET(val, bit)      (((val) >> (bit)) & 1)              // 获取 val 的第 bit 位（0 或 1）
#define BITSET(val, bit)      ((val) |= (1U << (bit)))            // 将 val 的第 bit 位置 1
#define BITCLR(val, bit)      ((val) &= ~(1U << (bit)))           // 将 val 的第 bit 位清 0
#define BITTOG(val, bit)   ((val) ^= (1U << (bit)))            // 将 val 的第 bit 位取反
extern void Lora_Pro(void);
extern void ADC_Pro(void);
extern void LED_Pro(void);
extern void Rule_Pro(void);
extern void Rule_Init(void);
extern void Rule_DailyReset(void);
extern void Rule_ResetOne(uint8_t index);
extern const DEV_RULE_T* Rule_Get(uint8_t index);
extern void Rule_Set(uint8_t index, const DEV_RULE_T *rule);
extern void Rule_Clear(uint8_t index);
extern void Meter_Update(uint32_t dt_sec);
extern void Meter_Save(void);
extern void Meter_Reset(void);
void LED_GREEN_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_RED_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_BLUE_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_WHITE_BLINK(bool IsBlinking, uint32_t BlinkInterval);
extern volatile uint32_t CurTick;
extern volatile uint16_t Timer_Lora;
#endif
