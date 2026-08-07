#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hlw8110.h"

t_dev Dev;
uint32_t LocalTimestamp;

static uint8_t ir_accept;
static uint8_t ir_calls;
static uint8_t save_calls;
static IR_CMD_t last_ir_cmd;
static HLW8110_Status_t meter_status;

uint8_t ADC_IsValid(void)
{
    return 1u;
}

uint8_t Ir_ExecuteVerified(IR_CMD_t cmd)
{
    ir_calls++;
    last_ir_cmd = cmd;
    return ir_accept;
}

uint8_t Ir_SendLearnedVerified(uint8_t channel)
{
    (void)channel;
    return 0u;
}

void SaveDevInfo(uint16_t delay)
{
    assert(delay == 0u || delay == 50u);
    save_calls++;
}

uint8_t RTC_IsTimeValid(void)
{
    return 1u;
}

uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *month, uint16_t *day,
                        uint16_t *hour, uint16_t *minute, uint16_t *second)
{
    *year = 2026u;
    *month = 7u;
    *day = 31u;
    *hour = 12u;
    *minute = 0u;
    *second = 0u;
    return 1u;
}

const HLW8110_Status_t *HLW8110_GetStatus(void)
{
    return &meter_status;
}

void Rule_Pro(void);

static void setup_temperature_power_on_rule(void)
{
    DEV_RULE_T *rule;
    memset(&Dev, 0, sizeof(Dev));
    Dev.mode = 0u;
    Dev.loraStatus = Status_Connected;
    Dev.irActType = ACT_TYPE_IR;
    Dev.onOff = PowerOff;
    Dev.roomTempX10 = 300;
    LocalTimestamp = 1000u;

    rule = &Dev.rules[0];
    rule->ctrl.enable = 1u;
    rule->ctrl.trig_type = TRIG_TEMP_ABOVE;
    rule->trig_val = 250u;
    rule->trig_val2 = 10u;
    rule->act.ir.onOff = 1u;
    rule->act.raw[2] = 1u;
    rule->act.raw[3] = 0u;
}

int main(void)
{
    DEV_RULE_T *rule;

    setup_temperature_power_on_rule();
    rule = &Dev.rules[0];

    /*
     * 配置无效、红外忙碌或发送入口拒绝时，规则必须保持待执行，
     * 不能提前修改开关状态、计量次数或最短启停时间。
     */
    ir_accept = 0u;
    ir_calls = 0u;
    save_calls = 0u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_ON);
    assert(rule->ctrl.executed == 0u);
    assert(Dev.onOff == PowerOff);
    assert(Dev.meter.onoff_count == 0u);
    assert(Dev.lastOnTime == 0u);
    assert(Dev.lastPowerChange == 0u);
    assert(save_calls == 0u);

    /* 下一秒红外空闲后应自然重试，并且只在成功提交时更新状态。 */
    ir_accept = 1u;
    LocalTimestamp++;
    Rule_Pro();
    assert(ir_calls == 2u);
    assert(rule->ctrl.executed == 1u);
    assert(Dev.onOff == PowerOn);
    assert(Dev.meter.onoff_count == 1u);
    assert(Dev.lastOnTime == LocalTimestamp);
    assert(Dev.lastPowerChange == LocalTimestamp);
    assert(save_calls == 1u);

    /* 云端 v2 是当天运行分钟；终身累计仍单独保留给本地规则。 */
    memset(&Dev, 0, sizeof(Dev));
    memset(&meter_status, 0, sizeof(meter_status));
    Dev.onOff = PowerOn;
    Dev.meter.run_seconds_remainder = 59u;
    Dev.meter.run_minutes = 1000u;
    Dev.meter.today_run_minutes = 120u;
    LocalTimestamp = 1785456000u + 12u * 3600u; /* 2026-07-31 12:00 UTC */
    Dev.meter.last_save_ts = LocalTimestamp - 60u;
    Meter_Update(1u);
    assert(Dev.meter.run_minutes == 1001u);
    assert(Meter_GetTodayRunMinutes() == 121u);

    LocalTimestamp = 1785542400u; /* 次日 00:00 UTC */
    Dev.meter.run_seconds_remainder = 0u;
    Meter_Update(60u);
    assert(Dev.meter.run_minutes == 1002u);
    assert(Meter_GetTodayRunMinutes() == 1u);

    Dev.meter.today_run_minutes = 1440u;
    Meter_Update(60u);
    assert(Meter_GetTodayRunMinutes() == 1440u);

    puts("rule IR submission reliability: PASS");
    return 0;
}
