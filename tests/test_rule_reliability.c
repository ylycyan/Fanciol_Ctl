#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hlw8110.h"

t_dev Dev;
uint32_t LocalTimestamp;
volatile uint32_t CurTick;

static uint8_t ir_accept;
static uint8_t ir_complete = 1U;
static uint8_t ir_calls;
static uint8_t save_calls;
static IR_CMD_t last_ir_cmd;
static HLW8110_Status_t meter_status;
static uint64_t meter_energy_delta;
static uint8_t meter_clear_calls;
static uint8_t runtime_append_calls;
static uint16_t rtc_hour = 12u;
static uint16_t rtc_minute;
static uint16_t rtc_day = 31u;
static uint8_t rtc_valid = 1u;
static uint8_t rtc_read_ok = 1u;

void Ml307_RequestReport(void) {}

uint8_t ADC_IsValid(void)
{
    return 1u;
}

uint8_t Ir_ExecuteVerified(IR_CMD_t cmd)
{
    ir_calls++;
    last_ir_cmd = cmd;
    if(ir_accept && ir_complete) Rule_IrCompleted(1U);
    return ir_accept;
}

uint8_t Ir_ExecuteConfiguredVerified(IR_CMD_t cmd)
{
    return Ir_ExecuteVerified(cmd);
}

uint8_t Ir_ExecuteConfiguredProfileVerified(uint8_t mode, uint8_t temperature, uint8_t wind)
{
    (void)temperature;
    (void)wind;
    ir_calls++;
    last_ir_cmd = (IR_CMD_t)(IR_CMD_MODE_AUTO + mode);
    if(ir_accept && ir_complete) Rule_IrCompleted(1U);
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
    return rtc_valid;
}

uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *month, uint16_t *day,
                        uint16_t *hour, uint16_t *minute, uint16_t *second)
{
    if(!rtc_read_ok) return 0u;
    *year = 2026u;
    *month = 7u;
    *day = rtc_day;
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
void Rule_Reset(void);

static void setup_policy(uint16_t start, uint16_t end, uint8_t start_action,
                         uint8_t end_off, uint8_t low_action, uint8_t high_action)
{
    DEV_RULE_T *rule;
    memset(&Dev, 0, sizeof(Dev));
    Dev.mode = 0u;
    Dev.loraStatus = Status_Connected;
    Dev.irActType = ACT_TYPE_IR;
    Dev.onOff = PowerOff;
    Dev.roomTempX10 = 300;
    LocalTimestamp = 1000u;
    CurTick += 3600000u;
    Rule_Reset();

    rule = &Dev.rules[0];
    rule->ctrl.enable = 1u;
    rule->ctrl.trig_type = TRIG_COMBINED;
    rule->flags = 0x7Fu;
    rule->trig_val = start;
    rule->trig_val2 = end;
    rule->sched = 260u;
    rule->act.raw[0] = (uint8_t)280u;
    rule->act.raw[1] = (uint8_t)(280u >> 8);
    rule->act.raw[2] = 1u;
    rule->act.raw[3] = 0u;
    rule->act.raw[4] = (uint8_t)(high_action | (uint8_t)(low_action << 4));
    rule->act.raw[5] = (uint8_t)(start_action | (uint8_t)(end_off << 4));
    rule->act.raw[6] = 26u;
    rule->act.raw[7] = Wind_Mid;
}

int main(void)
{
    DEV_RULE_T *rule;

    /* 上电后不能用旧墙上时间认定保护期已过，应保守等待一次间隔。 */
    setup_policy(0u, 0u, RULE_ACTION_POWER_ON, 0u, 0u, 0u);
    CurTick = 0u;
    ir_accept = 1u;
    ir_calls = 0u;
    Rule_Pro();
    assert(ir_calls == 0u);
    CurTick = 60000u;
    Rule_Pro();
    assert(ir_calls == 1u);

    /* 未校时、读取失败不能启动规则；已进入时段也不能误发离开时段关机。 */
    setup_policy(0u, 0u, RULE_ACTION_POWER_ON, 1u, 0u, 0u);
    ir_accept = 1u;
    ir_calls = 0u;
    Rule_Reset();
    rtc_valid = 0u;
    Rule_Pro();
    assert(ir_calls == 0u);
    rtc_valid = 1u;
    rtc_read_ok = 0u;
    Rule_Pro();
    assert(ir_calls == 0u);
    rtc_read_ok = 1u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(Dev.onOff == PowerOn);
    rtc_read_ok = 0u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(Dev.onOff == PowerOn);
    rtc_read_ok = 1u;

    setup_policy(0u, 0u, 0u, 0u, RULE_ACTION_POWER_OFF, RULE_ACTION_COOL);
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
    assert(last_ir_cmd == IR_CMD_MODE_COOL);
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
    assert((rule->ctrl.reserved & POLICY_LATCH_HIGH) != 0u);
    assert(Dev.onOff == PowerOn);
    assert(Dev.meter.onoff_count == 1u);
    assert(Dev.lastOnTime == LocalTimestamp);
    assert(Dev.lastPowerChange == LocalTimestamp);
    assert(save_calls == 1u);

    /* 仅远程模式与链路状态无关，始终禁止本地规则。 */
    setup_policy(0u, 0u, 0u, 0u, RULE_ACTION_POWER_OFF, RULE_ACTION_COOL);
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

    /* 策略只在自己的时间窗口内运行，离开窗口按配置关机。 */
    setup_policy(8u * 60u, 10u * 60u, 0u, 1u, RULE_ACTION_POWER_OFF, RULE_ACTION_COOL);
    Rule_Reset();
    Dev.onOff = PowerOn;
    Dev.roomTempX10 = 270;
    ir_calls = 0u;
    LocalTimestamp = 4000u;
    rtc_hour = 9u;
    Rule_Pro();
    assert(ir_calls == 0u);
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_OFF);
    assert(Dev.onOff == PowerOff);

    /* 保持状态选项不在时段结束时发送关机。 */
    Rule_Reset();
    Dev.onOff = PowerOn;
    Dev.rules[0].act.raw[5] &= 0x0Fu;
    ir_calls = 0u;
    rtc_hour = 9u;
    Rule_Pro();
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 0u);
    assert(Dev.onOff == PowerOn);

    /* 制热策略使用“低于下限制热”，并应用目标温度和风速。 */
    setup_policy(11u * 60u, 13u * 60u, 0u, 0u, RULE_ACTION_HEAT, RULE_ACTION_POWER_OFF);
    Rule_Reset();
    Dev.roomTempX10 = 180;
    ir_calls = 0u;
    LocalTimestamp = 5000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_MODE_HEAT);
    assert(Dev.onOff == PowerOn);
    assert(Dev.ctlMode == Mode_Heat);
    assert(Dev.temSet == 26u);
    assert(Dev.wind == Wind_Mid);

    /* 全天策略进入时可执行指定动作。 */
    setup_policy(0u, 0u, RULE_ACTION_POWER_ON, 0u, 0u, 0u);
    Rule_Reset();
    ir_calls = 0u;
    LocalTimestamp = 6000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_ON);
    assert(Dev.onOff == PowerOn);

    /* 进入时段选择不操作时不得隐式开机。 */
    setup_policy(11u * 60u, 13u * 60u, 0u, 0u, RULE_ACTION_POWER_OFF, RULE_ACTION_COOL);
    Rule_Reset();
    Dev.roomTempX10 = 270;
    ir_calls = 0u;
    LocalTimestamp = 7000u;
    rtc_hour = 12u;
    Rule_Pro();
    assert(ir_calls == 0u);
    assert(Dev.onOff == PowerOff);

    /* 进入时段也可明确指定关机。 */
    Rule_Reset();
    Dev.onOff = PowerOn;
    Dev.rules[0].act.raw[5] = RULE_ACTION_POWER_OFF;
    ir_calls = 0u;
    LocalTimestamp = 8000u;
    Rule_Pro();
    assert(ir_calls == 1u);
    assert(last_ir_cmd == IR_CMD_POWER_OFF);
    assert(Dev.onOff == PowerOff);

    /* 跨午夜保持同一活动组，不能重复进入动作或清除温控锁存。 */
    setup_policy(22u * 60u, 6u * 60u, RULE_ACTION_POWER_ON, 1u, 0u, 0u);
    rtc_hour = 23u;
    rtc_minute = 59u;
    ir_calls = 0u;
    Rule_Pro();
    assert(ir_calls == 1u);
    rtc_day = 1u;
    rtc_hour = 0u;
    rtc_minute = 0u;
    CurTick += 60000u;
    Rule_Pro();
    Rule_Pro();
    assert(ir_calls == 1u);
    rtc_hour = 6u;
    Rule_Pro();
    assert(ir_calls == 2u && last_ir_cmd == IR_CMD_POWER_OFF);
    rtc_day = 31u;

    /* 对时向前/向后不能绕过启停保护；单调计时跨 32 位回卷仍正确。 */
    setup_policy(0u, 0u, RULE_ACTION_POWER_OFF, 0u, 0u, 0u);
    Dev.onOff = PowerOn;
    CurTick = 0xFFFFFF00u;
    Rule_RecordPowerChange();
    ir_calls = 0u;
    CurTick += 59000u;
    LocalTimestamp -= 60u;
    Rule_Pro();
    assert(ir_calls == 0u);
    LocalTimestamp += 36000u;
    Rule_Pro();
    assert(ir_calls == 0u);
    CurTick += 1000u;
    Rule_Pro();
    assert(ir_calls == 1u);

    /* 周一跨夜与周二清晨冲突，周一清晨不冲突；周末回卷也要校验。 */
    setup_policy(1320u, 360u, RULE_ACTION_POWER_ON, 0u, 0u, 0u);
    Dev.rules[0].flags = 2u;
    Dev.rules[1] = Dev.rules[0];
    Dev.rules[1].flags = 4u;
    Dev.rules[1].trig_val = 300u;
    Dev.rules[1].trig_val2 = 420u;
    assert(Rule_WindowsOverlap(&Dev.rules[0], &Dev.rules[1]));
    assert(Rule_WindowsOverlap(&Dev.rules[1], &Dev.rules[0]));
    Dev.rules[1].flags = 2u;
    assert(!Rule_WindowsOverlap(&Dev.rules[0], &Dev.rules[1]));
    Dev.rules[0].flags = 64u;
    Dev.rules[1].flags = 1u;
    assert(Rule_WindowsOverlap(&Dev.rules[0], &Dev.rules[1]));
    Dev.rules[1].trig_val = 360u;
    assert(!Rule_WindowsOverlap(&Dev.rules[0], &Dev.rules[1]));

    /* 空调开机不使用软件时间估算电量。 */
    CurTick = 0u;
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

    /* Flash 定时保存使用单调时钟：未对时也必须每 10 分钟保存。 */
    memset(&Dev, 0, sizeof(Dev));
    save_calls = 0u;
    LocalTimestamp = 20000u;
    CurTick = 599999u;
    Meter_Update(0u);
    assert(save_calls == 0u);
    CurTick = 600000u;
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

    /* 提交不是发送完成；失败后五秒重试，且不提前计数、保存或锁存。 */
    setup_policy(0,0,0,0,RULE_ACTION_POWER_OFF,RULE_ACTION_COOL);
    CurTick+=60000;ir_accept=1;ir_complete=0;ir_calls=save_calls=0;
    Rule_Pro(); assert(ir_calls==1 && Dev.onOff==PowerOff && save_calls==0);
    assert(!(Dev.rules[0].ctrl.reserved & POLICY_LATCH_HIGH));
    Rule_Pro();assert(ir_calls==1);
    Rule_IrCompleted(0);CurTick+=4999;Rule_Pro();assert(ir_calls==1);
    CurTick++;Rule_Pro();assert(ir_calls==2 && Dev.onOff==PowerOff);
    Rule_IrCompleted(1);
    assert(Dev.onOff==PowerOn && save_calls==1 && Dev.meter.onoff_count==1);
    assert(Dev.rules[0].ctrl.reserved & POLICY_LATCH_HIGH);
    Rule_Pro();assert(ir_calls==2);
    /* 配置重置后的迟到完成不能修改新的策略或设备状态。 */
    setup_policy(0,0,RULE_ACTION_POWER_ON,0,0,0);
    CurTick+=60000;ir_calls=0;Rule_Pro();assert(ir_calls==1);
    Rule_Reset();Rule_IrCompleted(1);assert(Dev.onOff==PowerOff);
    /* 窗口退出的关机失败不能丢掉旧组的待关机操作。 */
    ir_complete=1;
    setup_policy(8*60,10*60,RULE_ACTION_POWER_ON,1,0,0);
    CurTick+=60000;rtc_hour=9;ir_calls=0;Rule_Pro();assert(Dev.onOff==PowerOn);
    CurTick+=60000;rtc_hour=10;ir_complete=0;Rule_Pro();assert(ir_calls==2);
    Rule_IrCompleted(0);CurTick+=5000;Rule_Pro();assert(ir_calls==3);
    Rule_IrCompleted(1);assert(Dev.onOff==PowerOff);
    /* 进入场景部分失败后窗口过期，不能因软件仍为关闭而漏掉关机。 */
    setup_policy(8*60,10*60,RULE_ACTION_COOL,1,0,0);
    CurTick+=60000;rtc_hour=9;ir_calls=0;Rule_Pro();assert(ir_calls==1);
    Rule_IrCompleted(0);CurTick+=5000;rtc_hour=10;Rule_Pro();
    assert(ir_calls==2 && last_ir_cmd==IR_CMD_POWER_OFF);
    Rule_IrCompleted(1);
    /* 重试退避使用单调时间，跨 uint32 回绕也不能提前重发。 */
    setup_policy(0,0,0,0,RULE_ACTION_POWER_OFF,RULE_ACTION_COOL);
    rtc_hour=12;CurTick=0xFFFFFFF0U;ir_calls=0;
    Rule_Pro();assert(ir_calls==1);Rule_IrCompleted(0);
    CurTick+=4999;Rule_Pro();assert(ir_calls==1);
    CurTick++;Rule_Pro();assert(ir_calls==2);Rule_IrCompleted(1);
    /* 学习码不能盲目重复执行可能的切换/增减键。 */
    setup_policy(0,0,0,0,RULE_ACTION_POWER_OFF,RULE_ACTION_COOL);
    Dev.irActType=ACT_TYPE_LEARN;CurTick+=60000;ir_calls=0;
    Rule_Pro();assert(ir_calls==1);Rule_IrCompleted(0);
    CurTick+=30000;Rule_Pro();assert(ir_calls==1 && Dev.onOff==PowerOff);
    ir_complete=1;
    puts("rule IR submission reliability: PASS");
    return 0;
}
