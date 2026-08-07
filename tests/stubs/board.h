#ifndef TEST_SPLITAC_BOARD_H
#define TEST_SPLITAC_BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "CH58x_common.h"

#define Default_DevId      0xBB01u
#define Default_Channel    5u
#define MAGIC_CODE         0x52ABu
#define IR_BRAND_COUNT     128u
#define MAX_IR_LEARNNUM    10u
#define MAX_RULES          10u
#define LORA_SF_LISTEN     9u
#define LORA_BW_LISTEN     4u
#define LORA_SF_SCAN       10u
#define LORA_BW_SCAN       5u
#define LORA_SF_MIN        5u
#define LORA_SF_MAX        12u

typedef enum {
    LINK_DIRECT = 0,
    LINK_RELAY = 1,
    LINK_CHILD = 2
} tLinkRole;

typedef enum {
    ACT_TYPE_IR = 0,
    ACT_TYPE_LEARN = 1
} ActType_t;

typedef enum {
    Mode_Auto = 0,
    Mode_Cool = 1,
    Mode_Dry = 2,
    Mode_Fan = 3,
    Mode_Heat = 4
} Mode_t;

typedef enum {
    Wind_Auto = 0,
    Wind_Low = 1,
    Wind_Mid = 2,
    Wind_High = 3
} Wind_t;

typedef enum {
    PowerOn = 0,
    PowerOff = 1
} OnOff_t;

typedef enum {
    Status_Uninit = 0,
    Status_Logining = 1,
    Status_CheckSend = 2,
    Status_RecvLogin = 3,
    Status_Connected = 4,
    Status_CheckData = 5
} LoraStatus_t;

typedef enum {
    TRIG_NONE = 0,
    TRIG_TIME = 1,
    TRIG_TEMP_ABOVE = 2,
    TRIG_TEMP_BELOW = 3,
    TRIG_POWER_ABOVE = 4,
    TRIG_RUNTIME = 5,
    TRIG_ENERGY = 6,
    TRIG_COMBINED = 7
} TrigType_t;

typedef enum {
    IR_CMD_POWER_OFF = 0x80,
    IR_CMD_POWER_ON = 0x81
} IR_CMD_t;

typedef struct {
    uint8_t enable : 1;
    uint8_t trig_type : 3;
    uint8_t executed : 1;
    uint8_t reserved : 3;
} RuleCtrl_t;

typedef struct {
    RuleCtrl_t ctrl;
    uint8_t flags;
    uint16_t trig_val;
    uint16_t trig_val2;
    uint16_t sched;
    union {
        uint8_t raw[8];
        struct {
            uint8_t onOff : 1;
            uint8_t mode : 3;
            uint8_t wind : 2;
            uint8_t sweep : 1;
            uint8_t sleep : 1;
            uint8_t temSet;
            uint8_t reserved[6];
        } ir;
        struct {
            uint8_t learnIdx;
            uint8_t reserved[7];
        } learn;
    } act;
} DEV_RULE_T;

typedef struct {
    uint32_t energy_wh;
    uint32_t energy_watt_tenth_seconds;
    uint32_t run_minutes;
    uint32_t last_save_ts;
    uint16_t onoff_count;
    uint16_t fault_count;
    uint16_t run_seconds_remainder;
    uint16_t today_run_minutes;
} DEV_METER_T;

typedef struct {
    uint8_t enable;
    uint8_t cmd[256];
} IR_LEARNING_t;

typedef struct {
    uint16_t magicCode;
    uint16_t nodeId;
    uint16_t channel;
    uint8_t linkRole;
    uint16_t parentRelayId;
    uint8_t mode;
    ActType_t irActType;
    uint8_t irIdx;
    uint16_t irType;
    DEV_RULE_T rules[MAX_RULES];
    DEV_METER_T meter;
    uint16_t runTime;
    uint8_t learnNum;
    IR_LEARNING_t learnCode[MAX_IR_LEARNNUM];
    uint8_t loraRegisterSf;
    uint8_t loraRegisterBw;
    uint8_t loraListenSf;
    uint8_t loraListenBw;
    OnOff_t onOff;
    int16_t roomTempX10;
    Mode_t ctlMode;
    uint8_t temSet;
    Wind_t wind;
    LoraStatus_t loraStatus;
    uint32_t lastOnTime;
    uint32_t lastPowerChange;
    uint16_t loadPower;
    uint8_t scanCycle;
    union {
        uint16_t u16Val;
        struct {
            uint16_t lora : 1;
            uint16_t irLearn : 1;
            uint16_t irMatch : 1;
            uint16_t ad : 1;
            uint16_t power : 1;
            uint16_t flash : 1;
        } bit;
    } errorCode;
} t_dev;

void Meter_Update(uint32_t dt_sec);
uint16_t Meter_GetTodayRunMinutes(void);

extern t_dev Dev;

#define BITGET(value, bit) (((value) >> (bit)) & 1u)

uint8_t ADC_IsValid(void);
uint8_t Ir_ExecuteVerified(IR_CMD_t cmd);
uint8_t Ir_SendLearnedVerified(uint8_t channel);
void SaveDevInfo(uint16_t delay);

#endif
