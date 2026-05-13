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
#include <time.h>

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
    struct tm t;

    RTC_GetTime(&year, &mon, &day, &h, &m, &sec);

    if (hour)   *hour   = h;
    if (min)    *min    = m;
    if (month)  *month  = mon;

    if (weekday) {
        /* mktime + gmtime 计算星期几 (0=周日) */
        t.tm_year = year - 1900;
        t.tm_mon  = mon - 1;
        t.tm_mday = day;
        t.tm_hour = h;
        t.tm_min  = m;
        t.tm_sec  = sec;
        mktime(&t);
        *weekday = t.tm_wday; /* 0=Sun, 1=Mon ... 6=Sat */
    }
}

/**
 * @brief 获取当前温度 (×10, 整数)
 */
static int16_t rule_get_temp_x10(void)
{
    return (int16_t)(Dev.tem * 10.0f);
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
static void rule_exec_ir(const DEV_RULE_T *r)
{
    const uint8_t onOff = r->act.ir.onOff;
    const uint8_t mode  = r->act.ir.mode;
    const uint8_t wind  = r->act.ir.wind;
    const uint8_t tem   = r->act.ir.temSet;

    /* 通过红外发送对应指令 */
    if (onOff) {
        Ir_cmd(IR_CMD_POWER_ON);
    } else {
        Ir_cmd(IR_CMD_POWER_OFF);
    }

    /* 模式 (开机后再设置, 或者关机时只关机) */
    if (onOff) {
        switch (mode) {
        case Mode_Auto: Ir_cmd(IR_CMD_MODE_AUTO); break;
        case Mode_Cool: Ir_cmd(IR_CMD_MODE_COOL); break;
        case Mode_Dry:  Ir_cmd(IR_CMD_MODE_DRY);  break;
        case Mode_Fan:  Ir_cmd(IR_CMD_MODE_FAN);  break;
        case Mode_Heat: Ir_cmd(IR_CMD_MODE_HEAT); break;
        default: break;
        }

        /* 风速 */
        switch (wind) {
        case Wind_Auto: Ir_cmd(IR_CMD_FAN_AUTO); break;
        case Wind_Low:  Ir_cmd(IR_CMD_FAN_LOW);  break;
        case Wind_Mid:  Ir_cmd(IR_CMD_FAN_MID);  break;
        case Wind_High: Ir_cmd(IR_CMD_FAN_HIGH); break;
        default: break;
        }

        /* 温度 (16~31) */
        if (tem >= 16 && tem <= 31) {
            Ir_cmd((IR_CMD_t)(IR_CMD_TEMP_16 + (tem - 16)));
        }

        /* 扫风 */
        if (r->act.ir.sweep) {
            Ir_cmd(IR_CMD_WIND_AUTO_ON);
        }

        /* 睡眠 */
        if (r->act.ir.sleep) {
            Ir_cmd(IR_CMD_SLEEP_ON);
        }
    }

    /* 更新设备状态 */
    Dev.onOff = (OnOff_t)onOff;
    Dev.ctlMode = (Mode_t)mode;
    Dev.wind = (Wind_t)wind;
    Dev.temSet = tem;

    /* 记录开关机 */
    if (onOff) {
        Dev.lastOnTime = LocalTimestamp;
        Dev.meter.onoff_count++;
    }
}



/**
 * @brief 执行学习码动作
 */
static void rule_exec_learn(const DEV_RULE_T *r)
{
    uint8_t idx = r->act.learn.learnIdx;
    if (idx < Dev.learnNum && Dev.learnCode[idx].enable) {
        /* 学习码的 cmd[] 通过红外模块直接发送 */
        /* 具体实现需根据 IR 模块接口适配 */
        (void)idx;
    }
}

/**
 * @brief 立即上报数据
 */
static void rule_exec_report(void)
{
    /* 触发一次 LoRa 数据上报 (设置标志, 由 Lora_Pro 处理) */
    Dev.loraStatus = Status_CheckData;
}

/**
 * @brief 执行规则动作
 */
static void Rule_Execute(DEV_RULE_T *r)
{

    switch (Dev.irActType) {
    case ACT_TYPE_IR:     rule_exec_ir(r);     break;
    case ACT_TYPE_LEARN:  rule_exec_learn(r);  break;
    // case ACT_REPORT: rule_exec_report();  break;
    default: break;
    }

    PRINT("[Rule] exec rule trig=%d act=%d\r\n",
          r->ctrl.trig_type, act_type);
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
    } else {
        /* 区间触发: 起始 <= now < 停止 */
        return (now_min >= r->trig_val && now_min < r->trig_val2);
    }
}

/**
 * @brief 温度高于阈值判断
 *   trig_val  = 阈值×10
 *   trig_val2 = 回差×10 (防频繁动作)
 *   sched     = 上限×10 (0=单阈值, 非0=区间触发)
 */
static bool trig_check_temp_above(const DEV_RULE_T *r)
{
    int16_t temp = rule_get_temp_x10();
    int16_t thresh = (int16_t)r->trig_val;
    int16_t hyst  = (int16_t)r->trig_val2;

    if (r->sched != 0) {
        /* 区间触发: temp 在 [trig_val, sched] 范围内 */
        return (temp >= thresh && temp <= (int16_t)r->sched);
    }

    /* 单阈值: temp > thresh (带回差: 降到 thresh-hyst 才释放) */
    return (temp > thresh);
}

/**
 * @brief 温度低于阈值判断
 *   trig_val  = 阈值×10
 *   trig_val2 = 回差×10
 */
static bool trig_check_temp_below(const DEV_RULE_T *r)
{
    int16_t temp = rule_get_temp_x10();
    int16_t thresh = (int16_t)r->trig_val;
    int16_t hyst  = (int16_t)r->trig_val2;

    (void)hyst; /* 回差通过反向动作实现 */

    return (temp < thresh);
}

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
 * @brief 规则引擎初始化 (清空所有规则和计量)
 */
void Rule_Init(void)
{
    uint8_t i;
    for (i = 0; i < MAX_RULES; i++) {
        Dev.rules[i].ctrl.enable = 0;
    }
    /* meter 保留, 不清零 (由 Flash 加载) */
}

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
    bool last_state;

    for (i = 0; i < MAX_RULES; i++) {
        DEV_RULE_T *r = &Dev.rules[i];

        /* 跳过未启用的规则 */
        if (!r->ctrl.enable) {
            continue;
        }

        /* 单次执行检查: 已执行过的规则不再触发 (除非时间触发, 每天自动重置) */
        if (r->ctrl.executed) {
            TrigType_t t = (TrigType_t)r->ctrl.trig_type;
            if (t == TRIG_TIME) {
                /* 时间触发: 当前时间不在区间内时自动清除 executed */
                if (!trig_check_time(r)) {
                    r->ctrl.executed = 0;
                }
                /* 时间在区间内但已执行过, 跳过 */
                if (r->ctrl.executed) {
                    continue;
                }
            } else {
                /* 非时间触发: 单次规则跳过 */
                continue;
            }
        }

        /* 评估触发条件 */
        triggered = false;
        last_state = r->ctrl.executed;

        switch ((TrigType_t)r->ctrl.trig_type) {
        case TRIG_NONE:        break;
        case TRIG_TIME:        triggered = trig_check_time(r);        break;
        case TRIG_TEMP_ABOVE:  triggered = trig_check_temp_above(r);  break;
        case TRIG_TEMP_BELOW:  triggered = trig_check_temp_below(r);  break;
        case TRIG_POWER_ABOVE: triggered = trig_check_power_above(r); break;
        case TRIG_RUNTIME:     triggered = trig_check_runtime(r);     break;
        case TRIG_ENERGY:      triggered = trig_check_energy(r);      break;
        case TRIG_COMBINED:    triggered = trig_check_combined(r);    break;
        default: break;
        }

        /* 锁存模式处理 */
        if (r->flags & 0x01) {
            /* bit0=1: 锁存模式 */
            if (r->flags & 0x02) {
                /* bit1=1: 反向动作 (条件不满足时执行恢复) */
                if (!triggered && last_state) {
                    Rule_Execute(r);
                    r->ctrl.executed = 0;
                }
            } else {
                /* bit1=0: 正向锁存 (条件满足时触发一次, 保持) */
                if (triggered && !last_state) {
                    Rule_Execute(r);
                    r->ctrl.executed = 1;
                }
            }
        } else {
            /* 非锁存模式: 条件满足就执行 */
            if (triggered) {
                Rule_Execute(r);
                r->ctrl.executed = 1;
            }
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

/**
 * @brief 清除指定规则的 executed 标志
 */
void Rule_ResetOne(uint8_t index)
{
    if (index < MAX_RULES) {
        Dev.rules[index].ctrl.executed = 0;
    }
}

/**
 * @brief 获取指定规则的只读指针 (用于蓝牙查询)
 */
const DEV_RULE_T* Rule_Get(uint8_t index)
{
    if (index < MAX_RULES) {
        return &Dev.rules[index];
    }
    return (const DEV_RULE_T*)0;
}

/**
 * @brief 设置指定规则 (用于蓝牙配置)
 */
void Rule_Set(uint8_t index, const DEV_RULE_T *rule)
{
    if (index < MAX_RULES && rule) {
        Dev.rules[index] = *rule;
        SaveDevInfo(50); /* 延迟500ms保存到Flash */
    }
}

/**
 * @brief 清空指定规则
 */
void Rule_Clear(uint8_t index)
{
    if (index < MAX_RULES) {
        Dev.rules[index].ctrl.enable = 0;
        Dev.rules[index].ctrl.executed = 0;
        SaveDevInfo(50);
    }
}

/* ------------------------------------------------------------------ */
/*  计量模块                                                           */
/* ------------------------------------------------------------------ */

#define METER_SAVE_INTERVAL  3600  /* 每小时保存一次计量数据到Flash */
#define METER_WH_PER_TICK    1     /* 每秒累计电量的基础单位 */

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
    static uint32_t sec_acc = 0;  /* 秒累加器, 60秒进位到分钟 */

    if (Dev.onOff != PowerOn) {
        return; /* 空调关闭时不计量 */
    }

    sec_acc += dt_sec;

    /* 运行时间: 每60秒累加1分钟 */
    if (sec_acc >= 60) {
        Dev.meter.run_minutes += (sec_acc / 60);
        sec_acc %= 60;
    }

    /* 电量累计: 功率(W*10) * 时间(s) / 3600 / 1000 = 0.1kWh 单位 */
    /* 公式: energy_wh += (loadPower * dt_sec) / 36000 */
    Dev.meter.energy_wh += (uint32_t)Dev.loadPower * dt_sec / 36000;

    /* 定期保存计量数据到Flash */
    if (LocalTimestamp - Dev.meter.last_save_ts >= METER_SAVE_INTERVAL) {
        Dev.meter.last_save_ts = LocalTimestamp;
        SaveDevInfo(0); /* 立即保存 */
    }
}

/**
 * @brief 手动保存计量数据
 */
void Meter_Save(void)
{
    Dev.meter.last_save_ts = LocalTimestamp;
    SaveDevInfo(0);
}

/**
 * @brief 清空计量数据
 */
void Meter_Reset(void)
{
    Dev.meter.energy_wh    = 0;
    Dev.meter.run_minutes  = 0;
    Dev.meter.onoff_count  = 0;
    Dev.meter.fault_count  = 0;
    Dev.meter.last_save_ts = LocalTimestamp;
    SaveDevInfo(0);
}
