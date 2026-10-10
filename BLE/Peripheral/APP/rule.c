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

/* ------------------------------------------------------------------ */
/*  内部工具函数                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 获取当前时间信息 (从RTC解析)
 */
static bool rule_get_time(uint16_t *hour, uint16_t *min, uint16_t *weekday,
                          uint16_t *month)
{
    uint16_t year, mon, day, h, m, sec;
    static const uint8_t month_offset[12] = {0,3,2,5,0,3,5,1,4,6,2,4};

    if(!RTC_GetWallTime(&year, &mon, &day, &h, &m, &sec)) return false;

    if (hour)   *hour   = h;
    if (min)    *min    = m;
    if (month)  *month  = mon;

    if (weekday) {
        /* Sakamoto 算法：0=周日，避免规则热路径调用较重且依赖时区的 mktime。 */
        uint16_t y = year;
        if(mon < 3u) y--;
        *weekday = (uint16_t)((y + y / 4u - y / 100u + y / 400u +
                              month_offset[mon - 1u] + day) % 7u);
    }
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
 * @brief 提交一个本地智控动作。内部码可执行完整场景；学习模式只执行已学习的模式键。
 */
static bool rule_exec_action(const DEV_RULE_T *r, uint8_t action)
{
    uint8_t previous = Dev.onOff;
    uint8_t mode;
    bool accepted;

    if(action < RULE_ACTION_POWER_ON || action > RULE_ACTION_MAX) return false;
    mode = rule_action_mode(action);

    if(action >= RULE_ACTION_COOL && Dev.irActType == ACT_TYPE_IR) {
        accepted = Ir_ExecuteConfiguredProfileVerified(mode, POLICY_TARGET_TEMP(r), POLICY_FAN(r)) != 0u;
    } else if(Dev.irActType == ACT_TYPE_IR) {
        accepted = Ir_ExecuteVerified(rule_action_command(action)) != 0u;
    } else if(Dev.irActType == ACT_TYPE_LEARN) {
        accepted = Ir_ExecuteConfiguredVerified(rule_action_command(action)) != 0u;
    } else {
        return false;
    }
    if(!accepted) return false;

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
    return true;
}

/**
 * @brief 执行规则动作
 */
void Rule_RecordPowerChange(void)
{
    Dev.lastPowerChange = LocalTimestamp;
    power_change_ms = CurTick;
}

static bool Rule_Execute(DEV_RULE_T *r, uint8_t action)
{
    uint16_t minimum = POLICY_MIN_INTERVAL(r);
    uint8_t requested = action == RULE_ACTION_POWER_OFF ? PowerOff : PowerOn;
    bool changesPower = requested != Dev.onOff;
    bool executed;
    if(changesPower && (uint32_t)(CurTick - power_change_ms) < (uint32_t)minimum * 60000u) {
        PRINT("[Rule] defer power change, minimum=%u min\r\n", minimum);
        return false;
    }

    executed = rule_exec_action(r, action);

    if(!executed) return false;

    if(changesPower || action >= RULE_ACTION_COOL) {
        Rule_RecordPowerChange();
        /* 与计量共用运行日志；同一秒内的多个状态变化会合并成一次追加。 */
        SaveDevInfo(50u);
    }

    PRINT("[Rule] exec trig=%d action=%d\r\n", r->ctrl.trig_type, action);
    /* 本地智控改变了物理状态，4G 在线时立即上报；离线时由重连流程补报。 */
    Ml307_RequestReport();
    return true;
}

static bool policy_window_matches(const DEV_RULE_T *r, uint16_t hour, uint16_t min,
                                  uint16_t weekday)
{
    uint16_t now_min;
    now_min = (uint16_t)(hour * 60u + min);

    /* 跨午夜时，结束段仍归属于前一天的计划。 */
    if (r->trig_val > r->trig_val2 && now_min < r->trig_val2) {
        weekday = (uint16_t)((weekday + 6u) % 7u);
    }
    if (!BITGET(r->flags, weekday)) return false;
    if (r->trig_val == r->trig_val2) return true; /* 全天 */
    if (r->trig_val < r->trig_val2) return now_min >= r->trig_val && now_min < r->trig_val2;
    return now_min >= r->trig_val || now_min < r->trig_val2;
}

/* 只在保存时使用：按完整周展开窗口，检查跨日及周六到周日的重叠。 */
uint8_t Rule_WindowsOverlap(const DEV_RULE_T *a, const DEV_RULE_T *b)
{
    uint8_t day_a, day_b;
    for(day_a = 0u; day_a < 7u; day_a++) {
        int32_t start_a, end_a;
        if(!BITGET(a->flags, day_a)) continue;
        start_a = (int32_t)day_a * 1440 + (a->trig_val == a->trig_val2 ? 0 : a->trig_val);
        end_a = (int32_t)day_a * 1440 + (a->trig_val == a->trig_val2 ? 1440 : a->trig_val2);
        if(a->trig_val > a->trig_val2) end_a += 1440;
        for(day_b = 0u; day_b < 7u; day_b++) {
            int32_t start_b, end_b;
            if(!BITGET(b->flags, day_b)) continue;
            start_b = (int32_t)day_b * 1440 + (b->trig_val == b->trig_val2 ? 0 : b->trig_val);
            end_b = (int32_t)day_b * 1440 + (b->trig_val == b->trig_val2 ? 1440 : b->trig_val2);
            if(b->trig_val > b->trig_val2) end_b += 1440;
            if((start_a < end_b && start_b < end_a) ||
               (start_a < end_b + 10080 && start_b + 10080 < end_a) ||
               (start_a < end_b - 10080 && start_b - 10080 < end_a)) return 1u;
        }
    }
    return 0u;
}

/* ------------------------------------------------------------------ */
/*  公共接口                                                           */
/* ------------------------------------------------------------------ */


/**
 * @brief 规则引擎主循环 (每秒调用一次)
 *
 * 最多六个互不重叠的策略组；每次只运行当前时间窗口对应的一组。
 */
void Rule_Pro(void)
{
    uint8_t i;
    int8_t selected = -1;
    int16_t temp;
    uint16_t hour, minute, weekday;
    DEV_RULE_T *r;

    /* 仅远程控制模式不执行本地规则；混合模式不依赖网络状态。 */
    if (Dev.mode != 0u) {
        policy_active_known = 0u;
        policy_active_index = -1;
        return;
    }

    /* 时间不可用时不执行进入、退出或温控动作，也不使用虚构的默认日期。 */
    if(!RTC_IsTimeValid() || !rule_get_time(&hour, &minute, &weekday, NULL)) return;
    for(i = 0u; i < MAX_POLICY_GROUPS; ++i) {
        r = &Dev.rules[i];
        if(r->ctrl.enable && r->ctrl.trig_type == TRIG_COMBINED && policy_window_matches(r, hour, minute, weekday)) {
            selected = (int8_t)i;
            break;
        }
    }

    if(!policy_active_known || selected != policy_active_index) {
        if(policy_active_known && policy_active_index >= 0) {
            r = &Dev.rules[(uint8_t)policy_active_index];
            if(POLICY_END_OFF(r) && Dev.onOff != PowerOff) {
                if(!Rule_Execute(r, RULE_ACTION_POWER_OFF)) return;
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
            if(!Rule_Execute(r, POLICY_START_ACTION(r))) policy_active_index = -1;
            return;
        }
    }

    if(selected < 0 || !ADC_IsValid()) return;
    r = &Dev.rules[(uint8_t)selected];
    temp = rule_get_temp_x10();

    if((r->ctrl.reserved & POLICY_LATCH_HIGH) && temp <= (int16_t)POLICY_UPPER_X10(r) - 5)
        r->ctrl.reserved &= (uint8_t)~POLICY_LATCH_HIGH;
    if((r->ctrl.reserved & POLICY_LATCH_LOW) && temp >= (int16_t)r->sched + 5)
        r->ctrl.reserved &= (uint8_t)~POLICY_LATCH_LOW;

    if(temp > (int16_t)POLICY_UPPER_X10(r) && POLICY_HIGH_ACTION(r) != RULE_ACTION_NONE &&
       !(r->ctrl.reserved & POLICY_LATCH_HIGH) && Rule_Execute(r, POLICY_HIGH_ACTION(r))) {
        r->ctrl.reserved |= POLICY_LATCH_HIGH;
        return;
    }
    if(temp < (int16_t)r->sched && POLICY_LOW_ACTION(r) != RULE_ACTION_NONE &&
       !(r->ctrl.reserved & POLICY_LATCH_LOW) && Rule_Execute(r, POLICY_LOW_ACTION(r))) {
        r->ctrl.reserved |= POLICY_LATCH_LOW;
    }
}

/**
 * @brief 成功修改策略或工作模式后重新选择活动组；跨午夜不能清理运行锁存。
 */
void Rule_Reset(void)
{
    uint8_t i;
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

    HLW8110_ClearEnergyAccumulator();
    Dev.meter.energy_wh = 0U;
    Dev.meter.energy_watt_tenth_seconds = 0U;
    if(RTC_IsTimeValid()) Dev.meter.last_save_ts = LocalTimestamp;

    /* 清零是低频维护操作，立即追加一条记录后再向调用端返回结果。 */
    status = Runtime_Append();
    if(status == 0U) Ml307_RequestReport();
    return status;
}
