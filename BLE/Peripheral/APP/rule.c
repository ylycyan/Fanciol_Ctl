/**
 * @file rule.c
 * @brief 本地智控策略组 + 计量
 * @author opencode
 * @date 2026-05-12
 *
 * 最多六组策略，每组独立日期、时段、温度范围和运行场景。
 * 相同日期的时段在保存时禁止重叠，因此热路径只运行一个活动组。
 *
 * 计量流程:
 *   1. 每秒调用 Meter_Update(1) 汇总芯片累计电量
 *   2. 取出 HLW8110 Energy_PA 寄存器累计的新增电量
 *   3. SaveDevInfo() 自动保存 meter 到 Flash
 */

#include "board.h"
#include "timer.h"
#include "gateway_lora_codec.h"
#include "hlw8110.h"
#include "config_store.h"
#include "ml307r.h"

extern t_dev Dev;
extern uint32_t LocalTimestamp;

static int8_t policy_active_index = -1;
static uint8_t policy_active_known;
/* 上电后保守等待一次保护间隔；运行中的保护不受任何对时影响。 */
static uint32_t power_change_ms;
static uint8_t pending_action, pending_index, pending_latch;
static uint8_t retry_pending;
static uint8_t retry_start;
static uint32_t retry_after_ms;
#define POLICY_EXIT_ACTION 4U
#define POLICY_TIMER_ACTION 5U
static uint8_t clock_known, timer_due;
static uint32_t last_minute, last_clock_tick, manual_until, manual_expiry_ms;
static uint16_t timer_last_day[MAX_POLICY_GROUPS];

/* ------------------------------------------------------------------ */
/*  内部工具函数                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 获取当前时间信息 (从RTC解析)
 */
static bool rule_get_time(uint16_t *hour, uint16_t *min, uint16_t *weekday,
                          uint16_t *calendar_day)
{
    uint16_t year, mon, day, h, m, sec;
    uint16_t ordinal, years;
    static const uint16_t month_offset[12] = {0,31,59,90,120,151,181,212,243,273,304,334};

    if(!RTC_GetWallTime(&year, &mon, &day, &h, &m, &sec)) return false;

    if (hour)   *hour   = h;
    if (min)    *min    = m;
    if(year < 2000u || year > 2038u || mon < 1u || mon > 12u || !day || day > 31u) return false;
    years = year - 2000u;
    ordinal = (uint16_t)(years * 365u + (years + 3u) / 4u + month_offset[mon - 1u] + day - 1u);
    if(mon > 2u && year % 4u == 0u) ordinal++;
    if(calendar_day) *calendar_day = ordinal;
    if(weekday) *weekday = (uint16_t)((ordinal + 6u) % 7u);
    return true;
}

/**
 * @brief 获取当前温度 (×10, 整数)
 */
static int16_t rule_get_temp_x10(void)
{
    return Dev.roomTempX10;
}

static uint8_t rule_action_mode(uint8_t action)
{
    switch(action) {
    case RULE_ACTION_COOL: return Mode_Cool;
    case RULE_ACTION_HEAT: return Mode_Heat;
    case RULE_ACTION_DRY:  return Mode_Dry;
    case RULE_ACTION_FAN:  return Mode_Fan;
    default: return Mode_Auto;
    }
}

static IR_CMD_t rule_action_command(uint8_t action)
{
    switch(action) {
    case RULE_ACTION_POWER_ON:  return IR_CMD_POWER_ON;
    case RULE_ACTION_POWER_OFF: return IR_CMD_POWER_OFF;
    case RULE_ACTION_COOL:      return IR_CMD_MODE_COOL;
    case RULE_ACTION_HEAT:      return IR_CMD_MODE_HEAT;
    case RULE_ACTION_DRY:       return IR_CMD_MODE_DRY;
    case RULE_ACTION_FAN:       return IR_CMD_MODE_FAN;
    default:                    return IR_CMD_POWER_OFF;
    }
}

/**
 * @brief UART 整组发送完成后发布状态；不是空调物理执行回执。
 */
void Rule_IrCompleted(uint8_t success)
{
    DEV_RULE_T *r;
    uint8_t action = pending_action;
    uint8_t previous = Dev.onOff;
    uint8_t mode;
    if(action == RULE_ACTION_NONE) return;
    pending_action = RULE_ACTION_NONE;
    r = &Dev.rules[pending_index];
    Ml307_RecordRule(pending_index, pending_latch, action, success);
    if(!success) {
        /* 学习码可能是增减/切换键，发送结果不确定时不能自动重发。 */
        if(pending_latch == POLICY_TIMER_ACTION) return; /* uncertain send: do not replay */
        if(Dev.irActType == ACT_TYPE_LEARN) {
            if(pending_latch != POLICY_EXIT_ACTION) r->ctrl.reserved |= pending_latch;
            return;
        }
        if(pending_latch == 0U) retry_start = 1U;
        if(pending_latch == POLICY_EXIT_ACTION) policy_active_index = (int8_t)pending_index;
        retry_after_ms = CurTick + 5000U;
        retry_pending = 1U;
        return;
    }
    mode = rule_action_mode(action);
    Dev.onOff = action == RULE_ACTION_POWER_OFF ? PowerOff : PowerOn;
    if(action >= RULE_ACTION_COOL) {
        Dev.ctlMode = (Mode_t)mode;
        if(Dev.irActType == ACT_TYPE_IR) {
            Dev.temSet = POLICY_TARGET_TEMP(r);
            Dev.wind = (Wind_t)POLICY_FAN(r);
        }
    }
    if(previous != Dev.onOff) {
        if(Dev.meter.onoff_count != 0xFFFFu) Dev.meter.onoff_count++;
        if(Dev.onOff == PowerOn) Dev.lastOnTime = LocalTimestamp;
    }
    if(pending_latch < POLICY_EXIT_ACTION) r->ctrl.reserved |= pending_latch;
    if(previous != Dev.onOff || action >= RULE_ACTION_COOL) {
        Rule_RecordPowerChange();
        SaveDevInfo(50u);
    }
    Ml307_RequestReport();
}

/**
 * @brief 执行规则动作
 */
void Rule_RecordPowerChange(void)
{
    Dev.lastPowerChange = LocalTimestamp;
    power_change_ms = CurTick;
}

static bool Rule_Execute(DEV_RULE_T *r, uint8_t action, uint8_t latch)
{
    uint16_t minimum = POLICY_MIN_INTERVAL(r);
    uint8_t requested = action == RULE_ACTION_POWER_OFF ? PowerOff : PowerOn;
    bool changesPower = requested != Dev.onOff;
    bool accepted;
    if(pending_action != RULE_ACTION_NONE || action < RULE_ACTION_POWER_ON ||
       action > RULE_ACTION_MAX) return false;
    /* An explicit timed shutdown must not be lost merely because the unit
     * started recently. Restart/temperature actions retain their protection. */
    if(changesPower && !(latch == POLICY_TIMER_ACTION && action == RULE_ACTION_POWER_OFF) &&
       (uint32_t)(CurTick - power_change_ms) < (uint32_t)minimum * 60000u) {
        PRINT("[Rule] defer power change, minimum=%u min\r\n", minimum);
        return false;
    }

    pending_index = (uint8_t)(r - Dev.rules);
    pending_action = action;
    pending_latch = latch;
    if(action >= RULE_ACTION_COOL && Dev.irActType == ACT_TYPE_IR) {
        accepted = Ir_ExecuteConfiguredProfileVerified(rule_action_mode(action),
                         POLICY_TARGET_TEMP(r), POLICY_FAN(r)) != 0U;
    } else if(Dev.irActType == ACT_TYPE_IR) {
        accepted = Ir_ExecuteVerified(rule_action_command(action)) != 0U;
    } else if(Dev.irActType == ACT_TYPE_LEARN) {
        accepted = Ir_ExecuteConfiguredVerified(rule_action_command(action)) != 0U;
    } else accepted = false;
    if(!accepted) {
        pending_action = RULE_ACTION_NONE;
        return false;
    }
    return true;
}

static bool policy_date_matches(const DEV_RULE_T *r, const RULE_SCHEDULE_T *s, int32_t day)
{
    if(day < 0 || day > (int32_t)RULE_LAST_DAY) return false;
    if(s->date_mode >= 2u && (day < s->start_day || day > s->end_day)) return false;
    return s->date_mode == 2u || BITGET(r->flags, (day + 6) % 7);
}

static bool policy_window_matches(const DEV_RULE_T *r, const RULE_SCHEDULE_T *s,
                                  uint16_t hour, uint16_t min, uint16_t day)
{
    uint16_t now_min;
    now_min = (uint16_t)(hour * 60u + min);

    /* 跨午夜时，结束段仍归属于前一天的计划。 */
    if (r->trig_val > r->trig_val2 && now_min < r->trig_val2) {
        if(!day) return false;
        day--;
    }
    if (!policy_date_matches(r, s, day)) return false;
    if (r->trig_val == r->trig_val2) return true; /* 全天 */
    if (r->trig_val < r->trig_val2) return now_min >= r->trig_val && now_min < r->trig_val2;
    return now_min >= r->trig_val || now_min < r->trig_val2;
}

/* 只在保存时使用：按完整周展开窗口，检查跨日及周六到周日的重叠。 */
uint8_t Rule_SchedulesOverlap(const DEV_RULE_T *a, const RULE_SCHEDULE_T *sa,
                             const DEV_RULE_T *b, const RULE_SCHEDULE_T *sb)
{
    int32_t a0 = sa->date_mode >= 2u ? sa->start_day : 0;
    int32_t a1 = sa->date_mode >= 2u ? sa->end_day : RULE_LAST_DAY;
    int32_t b0 = sb->date_mode >= 2u ? sb->start_day : 0;
    int32_t b1 = sb->date_mode >= 2u ? sb->end_day : RULE_LAST_DAY;
    int32_t as = a->ctrl.trig_type == TRIG_TIME || a->trig_val != a->trig_val2 ? a->trig_val : 0;
    int32_t bs = b->ctrl.trig_type == TRIG_TIME || b->trig_val != b->trig_val2 ? b->trig_val : 0;
    int32_t ae = a->ctrl.trig_type == TRIG_TIME ? as + 1 : a->trig_val == a->trig_val2 ? 1440 : a->trig_val2 + (a->trig_val > a->trig_val2 ? 1440 : 0);
    int32_t be = b->ctrl.trig_type == TRIG_TIME ? bs + 1 : b->trig_val == b->trig_val2 ? 1440 : b->trig_val2 + (b->trig_val > b->trig_val2 ? 1440 : 0);
    int8_t shift;
    if(a->ctrl.trig_type != TRIG_TIME && POLICY_END_OFF(a) && b->ctrl.trig_type == TRIG_TIME) ae++;
    if(b->ctrl.trig_type != TRIG_TIME && POLICY_END_OFF(b) && a->ctrl.trig_type == TRIG_TIME) be++;
    /* Date ranges intersect first; weekly masks repeat after seven days.
     * At most 21 comparisons, even for a multi-year schedule. */
    for(shift = -1; shift <= 1; shift++) {
        int32_t day, first = a0 > b0 - shift ? a0 : b0 - shift;
        int32_t last = a1 < b1 - shift ? a1 : b1 - shift;
        if(last > first + 6) last = first + 6;
        for(day = first; day <= last; day++) {
            if(policy_date_matches(a, sa, day) && policy_date_matches(b, sb, day + shift) &&
               as < be + shift * 1440 && bs + shift * 1440 < ae) return 1u;
        }
    }
    return 0u;
}

uint8_t Rule_WindowsOverlap(const DEV_RULE_T *a, const DEV_RULE_T *b)
{
    const RULE_SCHEDULE_T weekly = {0};
    return Rule_SchedulesOverlap(a, &weekly, b, &weekly);
}

void Rule_ClockChanged(void)
{
    clock_known = 0u;
    timer_due = 0u;
}

/* Manual/remote actions win until the current temperature window ends.
 * No Flash writes and no effect on later independent timer tasks. */
void Rule_ManualOverride(void)
{
    uint16_t hour, minute, day;
    uint8_t i;
    if(!RTC_IsTimeValid() || !rule_get_time(&hour, &minute, NULL, &day)) return;
    for(i = 0u; i < MAX_POLICY_GROUPS; i++) {
        DEV_RULE_T *r = &Dev.rules[i];
        if(r->ctrl.enable && r->ctrl.trig_type == TRIG_COMBINED &&
           policy_window_matches(r, &Dev.ruleSchedules[i], hour, minute, day)) {
            manual_until = (uint32_t)day * 1440u;
            if(r->trig_val == r->trig_val2) manual_until += 1440u;
            else {
                manual_until += r->trig_val2;
                if(r->trig_val > r->trig_val2 && hour * 60u + minute >= r->trig_val) manual_until += 1440u;
            }
            /* Backward clock corrections must not prolong a manual hold forever. */
            manual_expiry_ms = CurTick + (manual_until - ((uint32_t)day * 1440u + hour * 60u + minute)) * 60000u;
            policy_active_known = 1u;
            policy_active_index = -1;
            retry_pending = retry_start = 0u;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  公共接口                                                           */
/* ------------------------------------------------------------------ */


/**
 * @brief 规则引擎主循环 (每秒调用一次)
 *
 * 最多六条互不冲突的定时或温控策略；每秒固定扫描，不动态分配。
 */
void Rule_Pro(void)
{
    uint8_t i;
    int8_t selected = -1;
    int16_t temp;
    uint16_t hour, minute, day;
    uint32_t now;
    DEV_RULE_T *r;

    /* 仅远程控制模式不执行本地规则；混合模式不依赖网络状态。 */
    if (Dev.mode != 0u) {
        policy_active_known = 0u;
        policy_active_index = -1;
        Rule_ClockChanged();
        return;
    }

    /* 时间不可用时不执行进入、退出或温控动作，也不使用虚构的默认日期。 */
    if(!RTC_IsTimeValid() || !rule_get_time(&hour, &minute, NULL, &day)) { Rule_ClockChanged(); return; }
    now = (uint32_t)day * 1440u + hour * 60u + minute;
    if(!clock_known || now != last_minute) {
        timer_due = 0u;
        if(clock_known && now == last_minute + 1u && (uint32_t)(CurTick - last_clock_tick) < 90000u) {
            for(i = 0u; i < MAX_POLICY_GROUPS; i++) {
                r = &Dev.rules[i];
                if(r->ctrl.enable && r->ctrl.trig_type == TRIG_TIME &&
                   (timer_last_day[i] == 0xFFFFu || day > timer_last_day[i]) &&
                   r->trig_val == hour * 60u + minute && policy_date_matches(r, &Dev.ruleSchedules[i], day))
                    timer_due |= (uint8_t)(1u << i);
            }
        }
        last_minute = now;
        clock_known = 1u;
    }
    last_clock_tick = CurTick;
    if(pending_action != RULE_ACTION_NONE) return;
    for(i = 0u; i < MAX_POLICY_GROUPS; i++) {
        if(timer_due & (1u << i)) {
            if(Rule_Execute(&Dev.rules[i], POLICY_START_ACTION(&Dev.rules[i]), POLICY_TIMER_ACTION)) {
                timer_due &= (uint8_t)~(1u << i);
                timer_last_day[i] = day;
            }
            return;
        }
    }
    if(manual_until && now < manual_until && (int32_t)(CurTick - manual_expiry_ms) < 0) return;
    manual_until = 0u;
    if(retry_pending) {
        if((int32_t)(CurTick - retry_after_ms) < 0) return;
        retry_pending = 0U;
    }
    for(i = 0u; i < MAX_POLICY_GROUPS; ++i) {
        r = &Dev.rules[i];
        if(r->ctrl.enable && r->ctrl.trig_type == TRIG_COMBINED && policy_window_matches(r, &Dev.ruleSchedules[i], hour, minute, day)) {
            selected = (int8_t)i;
            break;
        }
    }

    if(retry_start) {
        /* 同一窗口重试进入动作；窗口已结束则先执行原组的退出动作。 */
        if(selected == policy_active_index) policy_active_index = -1;
        retry_start = 0U;
    }
    if(!policy_active_known || selected != policy_active_index) {
        if(policy_active_known && policy_active_index >= 0) {
            r = &Dev.rules[(uint8_t)policy_active_index];
            /* 即使场景部分发送失败、软件仍显示关闭，退出也发送绝对关机。 */
            if(POLICY_END_OFF(r)) {
                if(!Rule_Execute(r, RULE_ACTION_POWER_OFF, POLICY_EXIT_ACTION)) return;
                policy_active_index = -1;
                return;
            }
            r->ctrl.reserved = 0u;
        }
        policy_active_known = 1u;
        policy_active_index = selected;
        if(selected < 0) return;
        r = &Dev.rules[(uint8_t)selected];
        r->ctrl.reserved = 0u;
        if(POLICY_START_ACTION(r) != RULE_ACTION_NONE) {
            if(!Rule_Execute(r, POLICY_START_ACTION(r), 0U)) policy_active_index = -1;
            return;
        }
    }

    if(selected < 0 || !ADC_IsValid()) return;
    r = &Dev.rules[(uint8_t)selected];
    temp = rule_get_temp_x10();

    i = Dev.ruleSchedules[(uint8_t)selected].hysteresis_x10;
    if(!i) i = 5u;
    if((r->ctrl.reserved & POLICY_LATCH_HIGH) && temp <= (int16_t)POLICY_UPPER_X10(r) - i)
        r->ctrl.reserved &= (uint8_t)~POLICY_LATCH_HIGH;
    if((r->ctrl.reserved & POLICY_LATCH_LOW) && temp >= (int16_t)r->sched + i)
        r->ctrl.reserved &= (uint8_t)~POLICY_LATCH_LOW;

    if(temp > (int16_t)POLICY_UPPER_X10(r) && POLICY_HIGH_ACTION(r) != RULE_ACTION_NONE &&
       !(r->ctrl.reserved & POLICY_LATCH_HIGH) && Rule_Execute(r, POLICY_HIGH_ACTION(r), POLICY_LATCH_HIGH)) {
        return;
    }
    if(temp < (int16_t)r->sched && POLICY_LOW_ACTION(r) != RULE_ACTION_NONE &&
       !(r->ctrl.reserved & POLICY_LATCH_LOW))
        (void)Rule_Execute(r, POLICY_LOW_ACTION(r), POLICY_LATCH_LOW);
}

/**
 * @brief 成功修改策略或工作模式后重新选择活动组；跨午夜不能清理运行锁存。
 */
void Rule_Reset(void)
{
    uint8_t i;
    pending_action = RULE_ACTION_NONE;
    retry_pending = 0U;
    retry_start = 0U;
    Rule_ClockChanged();
    manual_until = 0u;
    for(i = 0u; i < MAX_POLICY_GROUPS; i++) timer_last_day[i] = 0xFFFFu;
    for (i = 0; i < MAX_RULES; i++) {
        Dev.rules[i].ctrl.executed = 0;
        Dev.rules[i].ctrl.reserved = 0;
    }
    policy_active_known = 0u;
    policy_active_index = -1;
}


/* ------------------------------------------------------------------ */
/*  计量模块                                                           */
/* ------------------------------------------------------------------ */

#define METER_SAVE_INTERVAL_MS    600000UL /* 每 10 分钟持久化一次运行数据 */
#define METER_TENTH_KWH_DIVISOR  3600000UL /* (W*10)*s -> 0.1 kWh */
static uint32_t meter_next_save_ms;
static uint8_t meter_save_armed;
/**
 * @brief 计量数据更新 (每秒调用一次)
 * @param dt_sec 时间增量 (秒), 通常为1
 */
void Meter_Update(uint32_t dt_sec)
{
    uint64_t energy_delta;
    uint32_t accumulated;
    uint32_t increments;

    (void)dt_sec;

    /* 芯片内部完成积分；MCU只汇总寄存器增量，不再用功率×时间估算。 */
    energy_delta = HLW8110_TakeEnergyTenthWattSeconds();
    if(energy_delta != 0U) {
        /*
         * 余数每次都被归一化到 < 3600000。把增量夹在 32 位剩余空间内即可用
         * 32 位除法（RV32 上 64 位除法会链接 __udivdi3/__umoddi3，约 2.7 KB
         * Flash）。0.1W*s 级别的每秒增量离 4.29e9 还差多个数量级，饱和无副作用。
         */
        uint32_t room = 0xFFFFFFFFUL - Dev.meter.energy_watt_tenth_seconds;
        if(energy_delta > (uint64_t)room) energy_delta = room;
        accumulated = Dev.meter.energy_watt_tenth_seconds + (uint32_t)energy_delta;
        increments = accumulated / METER_TENTH_KWH_DIVISOR;
        Dev.meter.energy_watt_tenth_seconds = accumulated % METER_TENTH_KWH_DIVISOR;
        if(increments > (0xFFFFFFFFUL - Dev.meter.energy_wh)) Dev.meter.energy_wh = 0xFFFFFFFFUL;
        else Dev.meter.energy_wh += increments;
    }

    /*
     * 落盘周期使用单调运行时钟，不依赖网关/NTP 对时。这样设备长期离线时，
     * 掉电也只会损失最多 10 分钟的累计值；RTC 仅作为可读的保存时间记录。
     */
    if(!meter_save_armed) {
        meter_next_save_ms = CurTick + METER_SAVE_INTERVAL_MS;
        meter_save_armed = 1U;
    } else if((int32_t)(CurTick - meter_next_save_ms) >= 0) {
        meter_next_save_ms = CurTick + METER_SAVE_INTERVAL_MS;
        if(RTC_IsTimeValid()) Dev.meter.last_save_ts = LocalTimestamp;
        SaveDevInfo(0);
    }
}

uint8_t Meter_ClearEnergy(void)
{
    uint8_t status;
    DEV_METER_T previous = Dev.meter;

    Dev.meter.energy_wh = 0U;
    Dev.meter.energy_watt_tenth_seconds = 0U;
    if(RTC_IsTimeValid()) Dev.meter.last_save_ts = LocalTimestamp;

    /* 清零是低频维护操作，立即追加一条记录后再向调用端返回结果。 */
    status = Runtime_Append();
    if(status != 0U) {
        Dev.meter = previous;
        Dev.errorCode.bit.flash = 1U;
        return status;
    }
    HLW8110_ClearEnergyAccumulator();
    Ml307_RequestReport();
    return status;
}
