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
#include "gateway_lora_codec.h"
#define Default_DevId 0xBB01 //默认的设备编号
#define Default_Channel 5 
/* Lora */
#define LORA_POWER 22				//lora发送功率：22
#define LORA_SF_LISTEN GATEWAY_LORA_REGISTER_SF //固定网关注册参数：SF9 + BW125K
#define LORA_BW_LISTEN GATEWAY_LORA_REGISTER_BW
#define LORA_SF_SCAN GATEWAY_LORA_WORK_SF       //固定网关轮询参数：SF10 + BW250K
#define LORA_BW_SCAN GATEWAY_LORA_WORK_BW

#define LORA_SF_MIN 5u
#define LORA_SF_MAX 12u

/*
 * 固定网关频道使用合并编号 channel = Radio * 10 + Frequency，范围 0~32。
 * 注册频率：420.05 MHz + channel * 0.3 MHz。
 * 工作频率：channel <= 22 时为注册频率 + 3.1375 MHz；否则为
 * 420.1875 MHz + (channel - 23) * 0.3 MHz。
 * 以下枚举仅保留两级编号语义，实际频率统一由 gateway_lora_codec 计算。
 */
typedef enum {
    eRadioCh0 = 0,
    eRadioCh1 = 1,
    eRadioCh2 = 2,
    eRadioCh3 = 3,
    eRadioCh4 = 4,
    eRadioIllegal = 5
} tRadio;

typedef enum {
    eFrequency0 = 0,
    eFrequency1 = 1,
    eFrequency2 = 2,
    eFrequency3 = 3,
    eFrequency4 = 4,
    eFrequency5 = 5,
    eFrequency6 = 6,
    eFrequency7 = 7,
    eFrequency8 = 8,
    eFrequency9 = 9,
    eFrequencyIllegal = 10
} tFrequency;

/* Dev Info */
#define DevType  20 /* 产品编译期声明；旧登录包不携带该值，云端节点表也必须配置为 20。 */
#define DevTag   50
#define MAGIC_CODE 0x52AB //首次上电判断
#define AD_INTERVAL 10 //adc采集间隔
/*
 * 网关固定设备类型 20，数据区固定 11 字节：
 * v0 设定温度(sf)，v1 开关设定(u8)，v2 环境温度(sf)，
 * v3 工作模式(sf)，v4 风速档位(sf)，v5 运行反馈(u16，负载电流 mA)。
 */

/* Infrared */
#define IRBUFSIZE 256
typedef enum{
    IR_TYPE_NORMAL = 0,
    IR_TYPE_MATCH  = 1,
    IR_TYPE_LEARNing  = 2,
    IR_TYPE_RAW = 3
}IR_CMD_TYPE_t;
typedef struct{
    uint8_t rxlen; 
    uint8_t isFinish:1;
    IR_CMD_TYPE_t type:7;
    uint8_t rxbuf[IRBUFSIZE];
    uint8_t txbuf[IRBUFSIZE];
}IRBUF_t; //uart3 红外通讯数据包

/* Debug */
#define _BT_INFO_ 0 // 打印蓝牙调试信息
#define _LORA_INFO_ 0 // 量产固件关闭逐帧日志，保留关键状态与错误日志
#define _Sensor_INFO_ 0
#define _IR_INFO_ 0

typedef enum{
    Status_Uninit = 0, // 未初始化
    Status_Logining = 1, // lora初始化,发送注册请求
    Status_CheckSend = 2, // 检查是否发送完成
    Status_RecvLogin = 3, // 接收注册响应
    Status_Connected = 4, // 已连接
    Status_CheckData = 5, // 检查数据是否发送完成
}LoraStatus_t;

// LoRa 多跳中继角色
typedef enum {
    LINK_DIRECT = 0,   // 直连节点 (与网关直接通信)
    LINK_RELAY   = 1,  // 中继节点 (管理自己的空调 + 转发子节点)
    LINK_CHILD   = 2,  // 中继子节点 (通过中继与网关通信)
} tLinkRole;

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
#define MAX_CHILD_NODES 8  // 中继节点最大子节点数
#define RELAY_TRIAL_MAX_CHILD_NODES 4
#define RELAY_RELEASE_MAX_CHILD_NODES 8
#define RELAY_MAX_ACTIVE_CHILD_NODES RELAY_TRIAL_MAX_CHILD_NODES  // 试点阶段限制4个/中继, 正式发布改为8
#define LORA_POLL_INTERVAL_MS 20
#define LORA_MS_TO_TICKS(ms) ((((uint32_t)(ms) + LORA_POLL_INTERVAL_MS - 1) / LORA_POLL_INTERVAL_MS))
#define LORA_SEC_TO_TICKS(sec) ((((uint32_t)(sec) * 1000U) / LORA_POLL_INTERVAL_MS))

// 中继子节点信息
typedef struct {
    uint16_t nodeId;
    uint8_t  online;       // 1=本周期有响应, 0=超时
    uint8_t  lastRssi;
    uint32_t lastSeenTs;
} child_info_t;

//红外学习结构体,一般空调红外控制包不超过230byte
//通道固定含义: 0开机 1关机 2制冷 3制热 4除湿 5送风 6温度+ 7温度- 8风速 9自定义
typedef struct{  
    uint8_t enable; //是否有效 (0=空, 1=已学习)
    uint8_t cmd[256]; //学习到的红外码数据
}IR_LEARNING_t; //约257字节/通道, 10通道=2570字节
extern IRBUF_t IrBuf;

//本地规则引擎 - 触发类型(3bit, 8种)
typedef enum {
    TRIG_NONE        = 0, // 无触发(禁用)
    TRIG_TIME        = 1, // 按时间触发(每日定时, 时:分)
    TRIG_TEMP_ABOVE  = 2, // 环境温度 > 阈值
    TRIG_TEMP_BELOW  = 3, // 环境温度 < 阈值
    TRIG_POWER_ABOVE = 4, // 实时功率 > 阈值(单位:W)
    TRIG_RUNTIME     = 5, // 已停用，保留枚举值兼容旧配置
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
    // TRIG_RUNTIME:     已停用
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
    uint32_t energy_wh;                 // 累计电量，单位 0.1 kWh（保留旧字段名）
    uint32_t energy_watt_tenth_seconds; // 芯片累计但未满 0.1 kWh 的余数，单位 0.1 W*s
    uint32_t run_minutes;               // 保留字段：兼容既有 Flash 布局，不再累计
    uint32_t last_save_ts;              // 上次保存时间戳，秒
    uint16_t onoff_count;                // 开关机次数
    uint16_t fault_count;                // 计量故障次数
    uint16_t run_seconds_remainder;      // 保留字段，不再使用
    uint16_t today_run_minutes;          // 保留字段，不再使用
} DEV_METER_T;                          // 24字节

//设备结构体,存入DataFlash,掉电保存
typedef struct{
    uint16_t magicCode; //用于检测是否首次上电 0x55AA
    uint16_t nodeId; // 节点ID
    uint16_t channel;  //lora通道(0~32)？
    LoraStatus_t loraStatus; // lora状态
    uint32_t lastReportTime; // 上次上报时间戳
    uint32_t lastOnTime; // 上次空调开机时间，用于最短启停间隔
    uint32_t lastPowerChange; // 最近一次已提交开/关命令时间，用于跨复位保持最短启停间隔
    uint32_t loraFrequencyHz; // LoRa 当前频率，单位 Hz
    uint16_t gatewayId; //网关Id
    uint8_t scanCycle; //数据上报周期

    ActType_t irActType; // 动作类型(0~2) 0:红外控制 1:红外学习控制 //2:上报数据
    uint8_t irIdx; // 空调品牌索引，仅用于上位机目录显示
    uint16_t irType; // HXD039B 红外模块适配码
    uint8_t learnNum; //学习指令个数(0~10 MAX_IR_LEARNNUM)
    IR_LEARNING_t learnCode[MAX_IR_LEARNNUM];
    DEV_RULE_T    rules[MAX_RULES];       //本地规则引擎(定时/条件触发/计量,不上云, 160字节)
    DEV_METER_T   meter;                  // 计量数据（24字节，运行区轮转保存）
    //上报数据
    OnOff_t onOff; // 空调开关状态,0:关 1:开
    int16_t roomTempX10; // 环境温度，单位 0.1℃
    Mode_t ctlMode; // 空调运行模式
    uint16_t temSet; // 设定温度
    Wind_t wind; // 风速
    uint16_t runTime; // 已停用，保留结构布局
    uint16_t loadPower; // 负载功率,单位:W*10
    uint8_t mode; // 控制模式(0:本地 1:远程)
    // LoRa 多跳中继角色 (BLE写入, 掉电保存)
    uint8_t linkRole;       // tLinkRole, 默认 0=直连
    uint16_t parentRelayId; // 上级中继节点ID (仅 LINK_CHILD 有效, 其他为 0)
    /* 开发者可调的射频参数，仅占 4 字节；无动态分配，不进入轮询热路径。 */
    uint8_t loraRegisterSf;
    uint8_t loraRegisterBw;
    uint8_t loraListenSf;
    uint8_t loraListenBw;
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
#define LED_RED_PIN     GPIO_Pin_6
#define LED_GREEN_PIN   GPIO_Pin_15
#define LED_BLUE_PIN    GPIO_Pin_0
#define LED_WHITE_PIN   GPIO_Pin_16
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
#define BT_DEVICE_NAME                          "SplitAC" // 分体空调控制器广播基础名
// #define BT_DEFAULT_MAC_ADDR                     {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02} //BLE MAC 地址 默认由芯片地址随机生成

//uilt functions
extern void PrintHex(char *msg, uint8_t *buffer, uint16_t size);

//兼容普通节点板crc算法
extern void AddCrc(uint8_t *buf, uint16_t len);

extern int ChkCrc(uint8_t *buf, uint16_t len);

//红外函数
extern uint8_t Ir_ExecuteVerified(IR_CMD_t cmd);
extern uint8_t Ir_ConfiguredCommandSupported(IR_CMD_t cmd);
extern uint8_t Ir_ExecuteConfiguredVerified(IR_CMD_t cmd);
extern uint8_t Ir_StartMatch(void);
extern uint8_t Ir_StartLearning(uint8_t ch);
extern uint8_t Ir_SendLearnedVerified(uint8_t ch);
extern uint8_t Ir_CancelOperation(void);
extern uint8_t Ir_ResetLearned(uint8_t ch);
extern uint8_t Ir_ResetAllLearned(void);
extern uint16_t Ir_GetLearnedMask(void);
extern uint8_t Ir_PrepareConfigurationChange(void);
extern uint8_t Ir_TransmitRawAsync(const uint8_t *data, uint16_t len);
extern uint8_t Ir_GetQueueDepth(void);
extern uint8_t Ir_GetQueueHighWater(void);
extern void Ir_Pro(void);
extern uint8_t IrLearnChannel;

// 固定网关云端控制诊断计数（RAM 内饱和计数，不增加 Flash 擦写）
extern uint8_t Lora_BuildNodeReport(uint8_t *buf, uint8_t tag, uint8_t errorInfo);
extern uint8_t Lora_ExecuteNodeControl(const uint8_t *buf, uint8_t len,
                                      uint8_t allow_zero_gateway);
extern uint8_t Lora_BuildControlResult(uint8_t *buf, uint8_t tag, uint8_t result);

#define BITGET(val, bit)      (((val) >> (bit)) & 1)              // 获取 val 的第 bit 位（0 或 1）
#define BITSET(val, bit)      ((val) |= (1U << (bit)))            // 将 val 的第 bit 位置 1
#define BITCLR(val, bit)      ((val) &= ~(1U << (bit)))           // 将 val 的第 bit 位清 0
#define BITTOG(val, bit)   ((val) ^= (1U << (bit)))            // 将 val 的第 bit 位取反
extern void Lora_Pro(void);
extern uint8_t Relay_GetChildCount(void);
extern uint16_t Relay_GetChildBitmap(void);
extern void ADC_Pro(void);
extern void ADC_Init(void);
extern uint8_t ADC_IsValid(void);
extern void LED_Pro(void);
extern void Rule_Pro(void);
extern void Rule_DailyReset(void);
extern void Meter_Update(uint32_t dt_sec);
extern uint8_t Meter_ClearEnergy(void);
void LED_GREEN_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_RED_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_BLUE_BLINK(bool IsBlinking, uint32_t BlinkInterval);
void LED_WHITE_BLINK(bool IsBlinking, uint32_t BlinkInterval);
extern volatile uint32_t CurTick;
extern volatile uint32_t Timer_Lora;
#endif
