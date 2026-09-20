/**
 * @file rule.c
 * @brief 本地规则引擎 - 定时/条件触发 + 计量
 * @author opencode
 * @date 2026-05-12
 *
 * 规则评估流程:
 *   1. 每秒调用 Rule_Pro() (从 Period_1s 调用)
 *   2. 遍历 rules[], 跳过 enable=0 的规则
 *   3. 按 trig_type 判断触发条件
 *   4. 触发时调用 Rule_Execute() 执行动作
 *   5. 条件触发支持回差和锁存
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

static uint8_t policy_window_known;
static uint8_t policy_window_active;

/* ------------------------------------------------------------------ */
/*  内部工具函数                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief 获取当前时间信息 (从RTC解析)
 */
static void rule_get_time(uint16_t *hour, uint16_t *min, uint16_t *weekday,
                          uint16_t *month)
{
    uint16_t year, mon, day, h, m, sec;
    static const uint8_t month_offset[12] = {0,3,2,5,0,3,5,1,4,6,2,4};

    if(!RTC_GetWallTime(&year, &mon, &day, &h, &m, &sec)) {
        year = 2020u;
        mon = 1u;
        day = 1u;
        h = 0u;
        m = 0u;
    }

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
}

/**
 * @brief 获取当前温度 (×10, 整数)
 */
static int16_t rule_get_temp_x10(void)
{
    return Dev.roomTempX10;
}

/**
 * @brief 执行空调控制动作
 */
static bool rule_exec_ir(const DEV_RULE_T *r)
{
    const uint8_t onOff = r->act.ir.onOff;
    uint8_t previous = Dev.onOff;

    /*
     * 规则每秒都会重试，不能先塞入通用队列再乐观修改运行状态：
     * 配置无效或红外忙碌时必须保持规则待执行。这里直接走已配置方案
     * 校验和空闲互斥，只有命令真正提交到 UART
     * 后才更新本机的期望状态；忙碌或配置错误均保持 executed=0。
     */
    if(!Ir_ExecuteVerified(onOff ? IR_CMD_POWER_ON : IR_CMD_POWER_OFF)) return false;

    Dev.onOff = onOff ? PowerOn : PowerOff;
    if(previous != Dev.onOff) {
        if(Dev.meter.onoff_count != 0xFFFFu) Dev.meter.onoff_count++;
        if(onOff) Dev.lastOnTime = LocalTimestamp;
    }
    return true;
}



/**
 * @brief 执行学习码动作
 */
static bool rule_exec_learn(const DEV_RULE_T *r)
{
    /* 规则持久化始终保存绝对开/关语义；学习模式只在执行时映射到通道 0/1。 */
    uint8_t idx = r->act.ir.onOff ? 0u : 1u;
    if (idx < Dev.learnNum && Dev.learnCode[idx].enable) {
        uint8_t previous = Dev.onOff;
        if(!Ir_SendLearnedVerified(idx)) return false;
        if(idx == 0u) Dev.onOff = PowerOn;
        else if(idx == 1u) Dev.onOff = PowerOff;
        if(previous != Dev.onOff && Dev.meter.onoff_count != 0xFFFFu) Dev.meter.onoff_count++;
        if(previous != Dev.onOff && Dev.onOff == PowerOn) Dev.lastOnTime = LocalTimestamp;
        return true;
    }
    return false;
}

/**
 * @brief 执行规则动作
 */
static uint16_t rule_minimum_interval(const DEV_RULE_T *r)
{
    return (uint16_t)r->act.raw[2] | ((uint16_t)r->act.raw[3] << 8);
}

static bool Rule_Execute(DEV_RULE_T *r)
{
    uint16_t minimum = rule_minimum_interval(r);
    uint8_t requested = r->act.ir.onOff ? PowerOn : PowerOff;
    bool changesPower = requested != Dev.onOff;
    bool executed;
    if(changesPower && Dev.lastPowerChange != 0u &&
       (uint32_t)(LocalTimestamp - Dev.lastPowerChange) < (uint32_t)minimum * 60u) {
        PRINT("[Rule] defer power change, minimum=%u min\r\n", minimum);
        return false;
    }

    switch (Dev.irActType) {
    case ACT_TYPE_IR:     executed = rule_exec_ir(r);     break;
    case ACT_TYPE_LEARN:  executed = rule_exec_learn(r);  break;
    default: return false;
    }

    if(!executed) return false;

    if(changesPower) {
        Dev.lastPowerChange = LocalTimestamp;
        /* 与计量共用运行日志；同一秒内的多个状态变化会合并成一次追加。 */
        SaveDevInfo(50u);
    }

    PRINT("[Rule] exec rule trig=%d act=%d\r\n",
          r->ctrl.trig_type, Dev.irActType);
    /* 本地智控改变了物理状态，4G 在线时立即上报；离线时由重连流程补报。 */
    Ml307_RequestReport();
    return true;
}

static bool Rule_ExecutePower(DEV_RULE_T *r, uint8_t on)
{
    uint8_t saved = r->act.ir.onOff;
    r->act.ir.onOff = on ? 1u : 0u;
    bool result = Rule_Execute(r);
    r->act.ir.onOff = saved;
    return result;
}

static uint8_t rule_time_start_action(const DEV_RULE_T *r)
{
    if (RULE_TIME_ACTION_TAG(r) == RULE_TIME_ACTION_MARKER &&
        RULE_TIME_START_ACTION(r) <= 2u) return RULE_TIME_START_ACTION(r);
    /* 非当前格式不猜测动作，避免损坏数据导致空调意外开机。 */
    return 0u;
}

/* ------------------------------------------------------------------ */
/*  触发条件判断                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief 时间触发判断
 *   trig_val  = 起始分钟
 *   trig_val2 = 停止分钟 (0=不停止, 0xFFFF=单点)
 *   flags     = 星期调度 bit[0~6]=周日~周六
 *   sched     = 月份调度 bit[0~11]=1~12月, 0=每月
 */
static bool trig_check_time(const DEV_RULE_T *r)
{
    uint16_t hour, min, weekday, month;
    if(!RTC_IsTimeValid()) return false;
    rule_get_time(&hour, &min, &weekday, &month);

    uint16_t now_min = hour * 60 + min;

    /* 跨午夜时，结束段仍归属于前一天的计划。 */
    if (r->trig_val > r->trig_val2 && r->trig_val2 != 0u &&
        r->trig_val2 != 0xFFFFu && now_min < r->trig_val2) {
        weekday = (uint16_t)((weekday + 6u) % 7u);
    }

    /* 星期匹配: flags 的 bit0=周日, bit1=周一 ... bit6=周六 */
    if (!BITGET(r->flags, weekday)) {
        return false;
    }

    /* 月份匹配: sched 全0 或对应 bit 为1 */
    if (r->sched != 0 && !BITGET(r->sched, month - 1)) {
        return false;
    }

    /* 时间窗口匹配 */
    if (r->trig_val == r->trig_val2) {
        return true;
    } else if (r->trig_val2 == 0xFFFF) {
        /* 单点触发: 精确到分钟 */
        return (now_min == r->trig_val);
    } else if (r->trig_val2 == 0) {
        /* 无停止时间: 只要 >= 起始时间就触发 (每天一次) */
        return (now_min == r->trig_val);
    } else if(r->trig_val < r->trig_val2) {
        return (now_min >= r->trig_val && now_min < r->trig_val2);
    } else {
        /* 跨午夜窗口，例如 22:00-06:00。 */
        return (now_min >= r->trig_val || now_min < r->trig_val2);
    }
}

/* ------------------------------------------------------------------ */
/*  公共接口                                                           */
/* ------------------------------------------------------------------ */


/**
 * @brief 规则引擎主循环 (每秒调用一次)
 *
 * 遍历所有规则, 评估触发条件, 满足则执行动作.
 * 条件触发支持:
 *   - 锁存模式 (flags.bit0=1): 触发后保持, 直到条件不满足且 bit1=1 时执行恢复动作
 *   - 单次模式 (executed=1): 触发一次后不再触发, 直到手动清除 executed
 */
void Rule_Pro(void)
{
    uint8_t i;
    bool hasTime = false, hasTemperature = false, windowActive = false;
    bool enteringWindow;
    int16_t temp;
    DEV_RULE_T *shutdownRule = NULL;

    /* 仅远程控制模式不执行本地规则；混合模式不依赖网络状态。 */
    if (Dev.mode != 0u) return;

    /* 时间规则组成运行窗口；温控规则只在窗口内参与控制。 */
    for (i = 0; i < MAX_RULES; i++) {
        DEV_RULE_T *r = &Dev.rules[i];
        if (!r->ctrl.enable) continue;
        if (r->ctrl.trig_type == TRIG_TIME) {
            hasTime = true;
            if (!r->act.raw[4] && shutdownRule == NULL) shutdownRule = r;
        } else if (r->ctrl.trig_type == TRIG_TEMP_ABOVE ||
                   r->ctrl.trig_type == TRIG_TEMP_BELOW) {
            hasTemperature = true;
        }
    }

    if (!hasTime && !hasTemperature) {
        policy_window_known = 0u;
        policy_window_active = 0u;
        return;
    }

    if (hasTime) {
        if (!RTC_IsTimeValid()) return;
        for (i = 0; i < MAX_RULES; i++) {
            DEV_RULE_T *r = &Dev.rules[i];
            if (r->ctrl.enable && r->ctrl.trig_type == TRIG_TIME && trig_check_time(r)) {
                windowActive = true;
            }
        }

        enteringWindow = !policy_window_known || (!policy_window_active && windowActive);
        if (!windowActive) {
            if ((!policy_window_known || policy_window_active) && shutdownRule != NULL &&
                Dev.onOff != PowerOff && !Rule_ExecutePower(shutdownRule, 0u)) return;
            for (i = 0; i < MAX_RULES; i++) {
                if (Dev.rules[i].ctrl.trig_type == TRIG_TIME ||
                    Dev.rules[i].ctrl.trig_type == TRIG_TEMP_ABOVE ||
                    Dev.rules[i].ctrl.trig_type == TRIG_TEMP_BELOW) {
                    Dev.rules[i].ctrl.executed = 0u;
                }
            }
            policy_window_known = 1u;
            policy_window_active = 0u;
            return;
        }

        if (enteringWindow) {
            for (i = 0; i < MAX_RULES; i++) {
                DEV_RULE_T *r = &Dev.rules[i];
                if (r->ctrl.enable && r->ctrl.trig_type == TRIG_TIME && trig_check_time(r)) {
                    uint8_t action = rule_time_start_action(r);
                    if (action != 0u && Dev.onOff != (action == 1u ? PowerOn : PowerOff) &&
                        !Rule_ExecutePower(r, action == 1u)) return;
                    break;
                }
            }
        }

        for (i = 0; i < MAX_RULES; i++) {
            DEV_RULE_T *r = &Dev.rules[i];
            if (!r->ctrl.enable) continue;
            if (r->ctrl.trig_type == TRIG_TIME) r->ctrl.executed = trig_check_time(r) ? 1u : 0u;
            else if (enteringWindow && (r->ctrl.trig_type == TRIG_TEMP_ABOVE ||
                                        r->ctrl.trig_type == TRIG_TEMP_BELOW)) r->ctrl.executed = 0u;
        }
        policy_window_known = 1u;
        policy_window_active = 1u;
        if (!hasTemperature) return;
    } else {
        policy_window_known = 0u;
        policy_window_active = 0u;
    }

    for (i = 0; i < MAX_RULES; i++) {
        DEV_RULE_T *r = &Dev.rules[i];

        /* 跳过未启用的规则 */
        if (!r->ctrl.enable) {
            continue;
        }

        switch ((TrigType_t)r->ctrl.trig_type) {
        case TRIG_TEMP_ABOVE:
            if(!ADC_IsValid()) break;
            temp = rule_get_temp_x10();
            if(!r->ctrl.executed && temp > (int16_t)r->trig_val && Rule_Execute(r)) r->ctrl.executed = 1;
            if(r->ctrl.executed && temp <= (int16_t)r->trig_val - (int16_t)r->trig_val2) r->ctrl.executed = 0;
            break;
        case TRIG_TEMP_BELOW:
            if(!ADC_IsValid()) break;
            temp = rule_get_temp_x10();
            if(!r->ctrl.executed && temp < (int16_t)r->trig_val && Rule_Execute(r)) r->ctrl.executed = 1;
            if(r->ctrl.executed && temp >= (int16_t)r->trig_val + (int16_t)r->trig_val2) r->ctrl.executed = 0;
            break;
        default: break;
        }
    }
}

/**
 * @brief 清除所有规则的 executed 标志 (每天0点调用)
 */
void Rule_DailyReset(void)
{
    uint8_t i;
    for (i = 0; i < MAX_RULES; i++) {
        Dev.rules[i].ctrl.executed = 0;
    }
    policy_window_known = 0u;
    policy_window_active = 0u;
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
