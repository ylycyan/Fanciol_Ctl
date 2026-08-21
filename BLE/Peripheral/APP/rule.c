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
 *   1. 每秒调用 Meter_Update(1) 累加运行时间
 *   2. 空调开机时累计电量 (power * dt)
 *   3. SaveDevInfo() 自动保存 meter 到 Flash
 */

#include "board.h"
#include "timer.h"
#include "gateway_lora_codec.h"
#include "hlw8110.h"
#include "config_store.h"
#include "ml307r.h"

#define METER_DAY_MAX_RUN_MINUTES 1440U

extern t_dev Dev;
extern uint32_t LocalTimestamp;

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
 * @brief 获取当前功率 (W)
 */
static uint16_t rule_get_power(void)
{
    return Dev.loadPower; /* 已经是 W*10 单位, 直接用 */
}

/**
 * @brief 获取累计运行时间 (分钟)
 */
static uint32_t rule_get_runtime(void)
{
    return Dev.meter.run_minutes;
}

/**
 * @brief 获取累计电量 (0.1kWh)
 */
static uint32_t rule_get_energy(void)
{
    return Dev.meter.energy_wh;
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
    // case ACT_REPORT: rule_exec_report();  break;
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
    return true;
}

static bool Rule_ExecuteInverse(DEV_RULE_T *r)
{
    r->act.ir.onOff = r->act.ir.onOff ? 0u : 1u;
    bool result = Rule_Execute(r);
    r->act.ir.onOff = r->act.ir.onOff ? 0u : 1u;
    return result;
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

    /* 星期匹配: flags 的 bit0=周日, bit1=周一 ... bit6=周六 */
    if (!BITGET(r->flags, weekday)) {
        return false;
    }

    /* 月份匹配: sched 全0 或对应 bit 为1 */
    if (r->sched != 0 && !BITGET(r->sched, month - 1)) {
        return false;
    }

    /* 时间窗口匹配 */
    if (r->trig_val2 == 0xFFFF) {
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

/**
 * @brief 温度高于阈值判断
 *   trig_val  = 阈值×10
 *   trig_val2 = 回差×10 (防频繁动作)
 *   sched     = 上限×10 (0=单阈值, 非0=区间触发)
 */
/**
 * @brief 功率高于阈值判断
 *   trig_val  = 阈值 (W*10, 直接与 loadPower 比较)
 *   trig_val2 = 回差
 *   sched     = 上限 (0=单阈值, 非0=区间)
 */
static bool trig_check_power_above(const DEV_RULE_T *r)
{
    uint16_t power = rule_get_power();
    uint16_t thresh = r->trig_val;

    if (r->sched != 0) {
        return (power >= thresh && power <= r->sched);
    }

    return (power > thresh);
}

/**
 * @brief 累计运行时间超过阈值
 *   trig_val = 阈值 (分钟)
 */
static bool trig_check_runtime(const DEV_RULE_T *r)
{
    return (rule_get_runtime() > r->trig_val);
}

/**
 * @brief 累计电量超过阈值
 *   trig_val = 阈值 (0.1kWh)
 */
static bool trig_check_energy(const DEV_RULE_T *r)
{
    return (rule_get_energy() > r->trig_val);
}

/**
 * @brief 组合触发: 时间窗口 AND 条件(温度)
 *   trig_val  = 起始时间(分钟)
 *   trig_val2 = 停止时间(分钟)
 *   flags     = 星期调度
 *   sched     = 月份调度
 *   act.ac.temSet 复用为温度阈值 (×10, 高字节存阈值, 低字节存回差)
 *   注: 组合触发的条件部分使用 ac.temSet 作为温度阈值(简化设计)
 */
static bool trig_check_combined(const DEV_RULE_T *r)
{
    /* 先检查时间窗口 */
    if (!trig_check_time(r)) {
        return false;
    }

    /* 再检查温度条件 (ac.temSet * 10 作为阈值) */
    int16_t temp = rule_get_temp_x10();
    int16_t thresh = (int16_t)r->act.ir.temSet * 10;

    return (temp > thresh);
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
    bool triggered;
    int16_t temp;

    /* 远程模式只要任一已选链路在线就由云端接管；本地模式始终自治。 */
    if (BITGET(Dev.mode, 0) &&
        ((Connectivity_LoraEnabled() && Dev.loraStatus >= Status_Connected) ||
         Ml307_IsOnline())) return;
    for (i = 0; i < MAX_RULES; i++) {
        DEV_RULE_T *r = &Dev.rules[i];

        /* 跳过未启用的规则 */
        if (!r->ctrl.enable) {
            continue;
        }

        switch ((TrigType_t)r->ctrl.trig_type) {
        case TRIG_TIME:
            triggered = trig_check_time(r);
            if(triggered && !r->ctrl.executed && Rule_Execute(r)) r->ctrl.executed = 1;
            if(!triggered && r->ctrl.executed && r->trig_val2 != 0u &&
               r->trig_val2 != 0xFFFFu && Rule_ExecuteInverse(r)) r->ctrl.executed = 0;
            break;
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
        case TRIG_POWER_ABOVE:
            if(!HLW8110_GetStatus()->valid) break;
            triggered = trig_check_power_above(r);
            if(triggered && !r->ctrl.executed && Rule_Execute(r)) r->ctrl.executed=1;
            break;
        case TRIG_RUNTIME: triggered = trig_check_runtime(r); if(triggered && !r->ctrl.executed && Rule_Execute(r)) r->ctrl.executed=1; break;
        case TRIG_ENERGY: triggered = trig_check_energy(r); if(triggered && !r->ctrl.executed && Rule_Execute(r)) r->ctrl.executed=1; break;
        case TRIG_COMBINED: if(!ADC_IsValid()) break; triggered = trig_check_combined(r); if(triggered && !r->ctrl.executed && Rule_Execute(r)) r->ctrl.executed=1; break;
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
}


/* ------------------------------------------------------------------ */
/*  计量模块                                                           */
/* ------------------------------------------------------------------ */

#define METER_SAVE_INTERVAL         3600UL
#define METER_TENTH_KWH_DIVISOR  3600000UL /* (W*10)*s -> 0.1 kWh */
#define METER_SECONDS_PER_DAY       86400UL
#define METER_DAY_UNINITIALIZED     0xFFFFFFFFUL

static uint32_t meterDayStart = METER_DAY_UNINITIALIZED;

static void meter_sync_day(void)
{
    uint32_t dayStart;

    /* 掉电后的安全基准时间不可信，等 LoRa 或 4G 对时后再判断是否跨日。 */
    if(!RTC_IsTimeValid()) return;
    if(meterDayStart != METER_DAY_UNINITIALIZED &&
       LocalTimestamp >= meterDayStart &&
       LocalTimestamp < meterDayStart + METER_SECONDS_PER_DAY) return;

    dayStart = LocalTimestamp - (LocalTimestamp % METER_SECONDS_PER_DAY);
    if(meterDayStart == METER_DAY_UNINITIALIZED) {
        if(Dev.meter.last_save_ts < dayStart ||
           Dev.meter.last_save_ts >= dayStart + METER_SECONDS_PER_DAY) {
            Dev.meter.today_run_minutes = 0U;
        }
    } else {
        Dev.meter.today_run_minutes = 0U;
    }
    meterDayStart = dayStart;
}

uint16_t Meter_GetTodayRunMinutes(void)
{
    meter_sync_day();
    if(Dev.meter.today_run_minutes > METER_DAY_MAX_RUN_MINUTES) {
        Dev.meter.today_run_minutes = METER_DAY_MAX_RUN_MINUTES;
    }
    return Dev.meter.today_run_minutes;
}

/**
 * @brief 计量数据更新 (每秒调用一次)
 * @param dt_sec 时间增量 (秒), 通常为1
 *
 * 空调运行时累计:
 *   - 运行时间 +1 分钟 (每60秒)
 *   - 电量 += (功率 * dt) / 3600 (单位: 0.1kWh)
 */
void Meter_Update(uint32_t dt_sec)
{
    uint64_t accumulated;
    uint32_t increments;

    meter_sync_day();
    if(Dev.meter.today_run_minutes > METER_DAY_MAX_RUN_MINUTES) {
        Dev.meter.today_run_minutes = METER_DAY_MAX_RUN_MINUTES;
    }

    if(Dev.onOff == PowerOn) {
        uint32_t seconds = (uint32_t)Dev.meter.run_seconds_remainder + dt_sec;
        uint32_t runMinuteIncrements = seconds / 60UL;
        Dev.meter.run_minutes += runMinuteIncrements;
        if(runMinuteIncrements >= (uint32_t)METER_DAY_MAX_RUN_MINUTES -
                                  Dev.meter.today_run_minutes) {
            Dev.meter.today_run_minutes = METER_DAY_MAX_RUN_MINUTES;
        } else {
            Dev.meter.today_run_minutes = (uint16_t)(Dev.meter.today_run_minutes +
                                                      runMinuteIncrements);
        }
        Dev.meter.run_seconds_remainder = (uint16_t)(seconds % 60UL);
        Dev.runTime = Dev.meter.run_minutes > 0xFFFFUL ? 0xFFFFU : (uint16_t)Dev.meter.run_minutes;
    }

    /* 按实测功率累计，红外状态与空调物理状态不一致时也不会漏计。 */
    if(HLW8110_GetStatus()->valid && Dev.loadPower != 0U) {
        accumulated = (uint64_t)Dev.meter.energy_watt_tenth_seconds + (uint64_t)Dev.loadPower * dt_sec;
        increments = (uint32_t)(accumulated / METER_TENTH_KWH_DIVISOR);
        Dev.meter.energy_watt_tenth_seconds = (uint32_t)(accumulated % METER_TENTH_KWH_DIVISOR);
        if(0xFFFFFFFFUL - Dev.meter.energy_wh < increments) Dev.meter.energy_wh = 0xFFFFFFFFUL;
        else Dev.meter.energy_wh += increments;
    }

    /* RTC 对时可能回拨，避免无符号下溢导致连续擦写。 */
    if(!RTC_IsTimeValid()) {
        /* 未对时阶段不以 2026-01-01 的安全基准覆盖真实保存时间。 */
    } else if(Dev.meter.last_save_ts == 0U || LocalTimestamp < Dev.meter.last_save_ts) {
        Dev.meter.last_save_ts = LocalTimestamp;
    } else if(LocalTimestamp - Dev.meter.last_save_ts >= METER_SAVE_INTERVAL) {
        Dev.meter.last_save_ts = LocalTimestamp;
        SaveDevInfo(0);
    }
}
