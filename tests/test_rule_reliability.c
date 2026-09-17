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
static uint64_t meter_energy_delta;
static uint8_t meter_clear_calls;
static uint8_t runtime_append_calls;
static uint16_t rtc_hour = 12u;
static uint16_t rtc_minute;

void Ml307_RequestReport(void) {}

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
    *hour = rtc_hour;
    *minute = rtc_minute;
    *second = 0u;
    return 1u;
}

const HLW8110_Status_t *HLW8110_GetStatus(void)
{
    return &meter_status;
}

uint64_t HLW8110_TakeEnergyTenthWattSeconds(void)
{
    uint64_t value = meter_energy_delta;
    meter_energy_delta = 0U;
    return value;
}

void HLW8110_ClearEnergyAccumulator(void)
{
    meter_clear_calls++;
    meter_energy_delta = 0U;
}

uint8_t Runtime_Append(void)
{
    runtime_append_calls++;
    return 0U;
}

void Rule_Pro(void);
void Rule_DailyReset(void);

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

    /* 仅远程模式与链路状态无关，始终禁止本地规则。 */
    setup_temperature_power_on_rule();
    Dev.mode = 1U;
    ir_accept = 1U;
    ir_calls = 0U;
    Rule_Pro();
    assert(ir_calls == 0U);
    Dev.loraStatus = Status_Uninit;
    Rule_Pro();
    assert(ir_calls == 0U);

    /* 切换到混合模式后，本地规则立即恢复执行。 */
    Dev.mode = 0U;
    Rule_Pro();
    assert(ir_calls == 1U);
    assert(Dev.onOff == PowerOn);

    /* 温控策略只在定时窗口内运行，首次发现位于窗口外时执行关机。 */
    setup_temperature_power_on_rule();
    Rule_DailyReset();
    Dev.onOff = PowerOn;
    Dev.rules[1].ctrl.enable = 1u;
    Dev.rules[1].ctrl.trig_type = TRIG_TIME;
    Dev.rules[1].flags = 0x7Fu;
    Dev.rules[1].trig_val = 8u * 60u;
    Dev.rules[1].trig_val2 = 10u * 60u;
    Dev.rules[1].act.ir.onOff = 1u;
    Dev.rules[1].act.raw[2] = 5u;
    ir_calls = 0u;
    LocalTimestamp = 4000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_OFF);
    assert(Dev.onOff == PowerOff);

    /* 保持状态选项不在时段结束时发送关机。 */
    Rule_DailyReset();
    Dev.onOff = PowerOn;
    Dev.rules[1].act.raw[4] = 1u;
    ir_calls = 0u;
    Rule_Pro();
    assert(ir_calls == 0u);
    assert(Dev.onOff == PowerOn);

    /* 制热策略使用“低于下限开机”，且在有效时段内执行。 */
    memset(&Dev, 0, sizeof(Dev));
    Rule_DailyReset();
    Dev.mode = 0u;
    Dev.irActType = ACT_TYPE_IR;
    Dev.onOff = PowerOff;
    Dev.roomTempX10 = 180;
    Dev.rules[0].ctrl.enable = 1u;
    Dev.rules[0].ctrl.trig_type = TRIG_TIME;
    Dev.rules[0].flags = 0x7Fu;
    Dev.rules[0].trig_val = 11u * 60u;
    Dev.rules[0].trig_val2 = 13u * 60u;
    Dev.rules[0].act.ir.onOff = 1u;
    Dev.rules[0].act.raw[2] = 5u;
    Dev.rules[1].ctrl.enable = 1u;
    Dev.rules[1].ctrl.trig_type = TRIG_TEMP_BELOW;
    Dev.rules[1].trig_val = 200u;
    Dev.rules[1].trig_val2 = 5u;
    Dev.rules[1].act.ir.onOff = 1u;
    Dev.rules[1].act.raw[2] = 5u;
    ir_calls = 0u;
    LocalTimestamp = 5000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_ON);
    assert(Dev.onOff == PowerOn);

    /* 全天模式使用相同起止时间，任意时刻都应处于运行窗口。 */
    memset(&Dev, 0, sizeof(Dev));
    Rule_DailyReset();
    Dev.mode = 0u;
    Dev.irActType = ACT_TYPE_IR;
    Dev.onOff = PowerOff;
    Dev.rules[0].ctrl.enable = 1u;
    Dev.rules[0].ctrl.trig_type = TRIG_TIME;
    Dev.rules[0].flags = 0x7Fu;
    Dev.rules[0].trig_val = 0u;
    Dev.rules[0].trig_val2 = 0u;
    Dev.rules[0].act.ir.onOff = 1u;
    Dev.rules[0].act.raw[2] = 5u;
    RULE_TIME_START_ACTION(&Dev.rules[0]) = 1u;
    RULE_TIME_ACTION_TAG(&Dev.rules[0]) = RULE_TIME_ACTION_MARKER;
    ir_calls = 0u;
    LocalTimestamp = 6000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_ON);
    assert(Dev.onOff == PowerOn);

    /* 进入时段可以明确选择不操作，不得隐式开机。 */
    memset(&Dev, 0, sizeof(Dev));
    Rule_DailyReset();
    Dev.mode = 0u;
    Dev.irActType = ACT_TYPE_IR;
    Dev.onOff = PowerOff;
    Dev.rules[0].ctrl.enable = 1u;
    Dev.rules[0].ctrl.trig_type = TRIG_TIME;
    Dev.rules[0].flags = 0x7Fu;
    Dev.rules[0].trig_val = 11u * 60u;
    Dev.rules[0].trig_val2 = 13u * 60u;
    Dev.rules[0].act.raw[2] = 5u;
    RULE_TIME_START_ACTION(&Dev.rules[0]) = 0u;
    RULE_TIME_ACTION_TAG(&Dev.rules[0]) = RULE_TIME_ACTION_MARKER;
    ir_calls = 0u;
    LocalTimestamp = 7000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 0u);
    assert(Dev.onOff == PowerOff);

    /* 进入时段也可由用户明确指定关机。 */
    Rule_DailyReset();
    Dev.onOff = PowerOn;
    RULE_TIME_START_ACTION(&Dev.rules[0]) = 2u;
    ir_calls = 0u;
    LocalTimestamp = 8000u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_OFF);
    assert(Dev.onOff == PowerOff);

    /* 空调开机不使用软件时间估算电量。 */
    memset(&Dev, 0, sizeof(Dev));
    memset(&meter_status, 0, sizeof(meter_status));
    Dev.onOff = PowerOn;
    LocalTimestamp = 1785456000u + 12u * 3600u; /* 2026-07-31 12:00 UTC */
    Dev.meter.last_save_ts = LocalTimestamp - 60u;
    Meter_Update(3600u);
    assert(Dev.meter.energy_wh == 0u);

    /* 电量必须来自 HLW8110 寄存器增量，而不是 Dev.loadPower 软件积分。 */
    Dev.loadPower = 60000u;
    meter_energy_delta = 3600000u;
    Meter_Update(0u);
    assert(Dev.meter.energy_wh == 1u);
    Meter_Update(0u);
    assert(Dev.meter.energy_wh == 1u);

    /* Flash 定时保存边界：第 599 秒不保存，第 600 秒才安排一次保存。 */
    memset(&Dev, 0, sizeof(Dev));
    save_calls = 0u;
    LocalTimestamp = 20000u;
    Dev.meter.last_save_ts = LocalTimestamp - 599u;
    Meter_Update(0u);
    assert(save_calls == 0u);
    LocalTimestamp++;
    Meter_Update(0u);
    assert(save_calls == 1u);
    assert(Dev.meter.last_save_ts == LocalTimestamp);

    /* 清累计只影响电量，运行时长、次数等寿命统计必须保留。 */
    Dev.meter.energy_wh = 1234U;
    Dev.meter.energy_watt_tenth_seconds = 5678U;
    Dev.meter.onoff_count = 17U;
    meter_energy_delta = 900U;
    meter_clear_calls = 0U;
    runtime_append_calls = 0U;
    assert(Meter_ClearEnergy() == 0U);
    assert(Dev.meter.energy_wh == 0U);
    assert(Dev.meter.energy_watt_tenth_seconds == 0U);
    assert(Dev.meter.onoff_count == 17U);
    assert(meter_clear_calls == 1U);
    assert(runtime_append_calls == 1U);

    puts("rule IR submission reliability: PASS");
    return 0;
}
