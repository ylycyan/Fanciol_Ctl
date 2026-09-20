/**
 * @file hlw8110_math.c
 * @brief HLW8110 原始采样值 → 物理量换算（整数运算，无浮点）
 *
 * 换算系数来自芯片标定：K1/K2 为功率/电压分压与采样网络的比例常数，
 * 所有除法采用先乘后除 + 半值舍入，避免溢出且结果与浮点公式一致。
 */
#include "hlw8110_math.h"

#define HLW_K1_NUM              2ULL
#define HLW_K2_NUM              1ULL
#define HLW_POWER_NOISE_X10     20ULL
/*
 * 换算分母保持“编译期常量”形式。若写成 uint64_t 变量，RV32 上会退化成
 * __udivdi3/__umoddi3 调用（约 2.7 KB Flash，且明显更慢）。常量形式下
 * 2 的幂次除法被优化为移位；电压分母含因子 10，单独用右移拆分处理。
 */
#define HLW_POWER_DEN           (HLW_K1_NUM * HLW_K2_NUM * (1ULL << 31)) /* 2^32 */
#define HLW_CURRENT_DEN         (HLW_K1_NUM * (1ULL << 23))              /* 2^24 */
#define HLW_VOLTAGE_DEN         (HLW_K2_NUM * (1ULL << 22) * 10ULL)      /* 10*2^22 */
/* 功率/电流分母为 2 的幂：显式用移位，确保不生成 64 位除法库调用。 */
#define HLW_POWER_SHIFT         32U
#define HLW_CURRENT_SHIFT       24U
/* 电压分母含非 2 的幂因子 10：先按 2^22 拆分，再做 32 位除法。 */
#define HLW_VOLTAGE_SHIFT       22U
#define HLW_VOLTAGE_FACTOR      10U
typedef char hlw_power_den_must_match[
    (HLW_POWER_DEN == (1ULL << HLW_POWER_SHIFT)) ? 1 : -1
];
typedef char hlw_current_den_must_match[
    (HLW_CURRENT_DEN == (1ULL << HLW_CURRENT_SHIFT)) ? 1 : -1
];
typedef char hlw_voltage_den_must_match[
    (HLW_VOLTAGE_DEN == ((1ULL << HLW_VOLTAGE_SHIFT) * HLW_VOLTAGE_FACTOR)) ? 1 : -1
];
/*
 * kWh -> 0.1 W*s 需要乘 36,000,000。与能量公式分母共同约去 256 后：
 *   36,000,000 / (K1*K2*2^29*4096)
 * = 140,625 / (K1*K2*2^33)
 * 先做商和余数可保证最坏 24 位脉冲值也不会使 uint64_t 中间结果溢出。
 */
#define HLW_ENERGY_SCALE_NUM    140625ULL
#define HLW_ENERGY_SCALE_DEN    (HLW_K1_NUM * HLW_K2_NUM * (1ULL << 33))

/**
 * @brief 计算 HLW8110 帧校验和（帧头 0xA5 + 命令 + 数据，取反）
 */
uint8_t HLW8110_UartChecksum(uint8_t command, const uint8_t *data, uint8_t length)
{
    uint8_t sum = (uint8_t)(0xA5U + command);
    uint8_t i;

    if(length > 0U && data == 0) return 0U;
    for(i = 0; i < length; ++i) sum = (uint8_t)(sum + data[i]);
    return (uint8_t)(~sum);
}

/**
 * @brief 功率原始值（有符号 32 位）→ 功率 ×10（单位 0.1W）
 *
 * 取绝对值后按标定系数换算，低于 2.0W 视为噪声清零。
 */
uint16_t HLW8110_CalcPowerX10(uint32_t raw, uint16_t coefficient)
{
    int32_t signed_raw = (int32_t)raw;
    uint32_t magnitude;
    /*
     * 分母 = K1*K2*2^31 = 2^32（编译期常量）。写成变量会让 RV32 生成
     * __udivdi3 调用（约 1 KB Flash 且很慢）；常量形式直接降为移位。
     */
    uint64_t numerator;

    /* 0xFFFF 是免校准芯片的合法出厂系数，只有 0 表示系数无效。 */
    if(coefficient == 0U) return 0U;
    magnitude = signed_raw < 0 ? (uint32_t)(-(int64_t)signed_raw) : (uint32_t)signed_raw;
    numerator = (uint64_t)magnitude * coefficient * 10ULL;
    {
        uint64_t value = (numerator + (1ULL << (HLW_POWER_SHIFT - 1U))) >> HLW_POWER_SHIFT;
        if(value < HLW_POWER_NOISE_X10) value = 0U;
        return value > 0xFFFFULL ? 0xFFFFU : (uint16_t)value;
    }
}

/**
 * @brief 电流有效值原始值 → 电流（mA）
 * @retval 0 系数无效或结果溢出；1 成功（含"零值"标志位情况）
 */
uint8_t HLW8110_CalcCurrentMa(uint32_t raw, uint16_t coefficient, uint16_t *result)
{
    uint64_t numerator;
    uint64_t value;

    if(!result || coefficient == 0U) return 0U;
    /* 手册：交流有效值最高位为 1 时表示零值。 */
    if((raw & 0x800000UL) != 0U) {
        *result = 0U;
        return 1U;
    }
    numerator = (uint64_t)raw * coefficient;
    value = (numerator + (1ULL << (HLW_CURRENT_SHIFT - 1U))) >> HLW_CURRENT_SHIFT;
    if(value > 0xFFFFULL) return 0U;
    *result = (uint16_t)value;
    return 1U;
}

/**
 * @brief 电压有效值原始值 → 电压（0.1V）
 * @retval 0 系数无效或超出 300.0V；1 成功
 */
uint8_t HLW8110_CalcVoltageDv(uint32_t raw, uint16_t coefficient, uint16_t *result)
{
    uint32_t scaled;
    uint32_t value;

    if(!result || coefficient == 0U) return 0U;
    /* 手册：交流有效值最高位为 1 时表示零值。 */
    if((raw & 0x800000UL) != 0U) {
        *result = 0U;
        return 1U;
    }
    /*
     * 手册公式输出单位为 10 mV，再除以 10 转换为 0.1 V。
     * 分母 = 10*2^22：先右移 2^22（保留半值舍入），再 32 位除以 10。
     * floor((n + 5*2^22)/(10*2^22)) == floor(((n>>22)+5)/10)。
     * 右移后商不超过 2^26，可安全降到 32 位除法，避免 64 位除法库调用。 */
    scaled = (uint32_t)(((uint64_t)raw * coefficient) >> HLW_VOLTAGE_SHIFT);
    value = (scaled + HLW_VOLTAGE_FACTOR / 2U) / HLW_VOLTAGE_FACTOR;
    if(value > 3000U) return 0U;
    *result = (uint16_t)value;
    return 1U;
}

/**
 * @brief Energy_PA 脉冲数转换为 0.1 W*s
 *
 * fraction 保存不足 0.1 W*s 的定点余数，避免频繁读取少量脉冲时反复截断。
 * EnergyAC 的出厂默认值 0xFFFF 是合法系数，不能按“全 FF 通信错误”拒绝。
 */
uint64_t HLW8110_CalcEnergyTenthWattSeconds(uint32_t pulses, uint16_t energy_coefficient,
                                           uint16_t hfconst, uint64_t *fraction)
{
    uint64_t pulse_product;
    uint64_t quotient;
    uint64_t remainder;
    uint64_t scaled_remainder;
    uint64_t carry;

    if(!fraction || pulses == 0U || energy_coefficient == 0U || hfconst == 0U) return 0U;

    pulse_product = (uint64_t)(pulses & 0xFFFFFFUL) * energy_coefficient * hfconst;
    quotient = pulse_product / HLW_ENERGY_SCALE_DEN;
    remainder = pulse_product % HLW_ENERGY_SCALE_DEN;
    scaled_remainder = remainder * HLW_ENERGY_SCALE_NUM + *fraction;
    carry = scaled_remainder / HLW_ENERGY_SCALE_DEN;
    *fraction = scaled_remainder % HLW_ENERGY_SCALE_DEN;
    return quotient * HLW_ENERGY_SCALE_NUM + carry;
}
