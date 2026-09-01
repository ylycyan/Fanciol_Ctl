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
    uint64_t denominator = HLW_K1_NUM * HLW_K2_NUM * (1ULL << 31);
    uint64_t value;

    if(coefficient == 0U || coefficient == 0xFFFFU) return 0U;
    magnitude = signed_raw < 0 ? (uint32_t)(-(int64_t)signed_raw) : (uint32_t)signed_raw;
    value = ((uint64_t)magnitude * coefficient * 10ULL + denominator / 2ULL) / denominator;
    if(value < HLW_POWER_NOISE_X10) value = 0U;
    return value > 0xFFFFULL ? 0xFFFFU : (uint16_t)value;
}

/**
 * @brief 电流有效值原始值 → 电流（mA）
 * @retval 0 系数无效或结果溢出；1 成功（含"零值"标志位情况）
 */
uint8_t HLW8110_CalcCurrentMa(uint32_t raw, uint16_t coefficient, uint16_t *result)
{
    uint64_t denominator;
    uint64_t value;

    if(!result || coefficient == 0U || coefficient == 0xFFFFU) return 0U;
    /* 手册：交流有效值最高位为 1 时表示零值。 */
    if((raw & 0x800000UL) != 0U) {
        *result = 0U;
        return 1U;
    }
    denominator = HLW_K1_NUM * (1ULL << 23);
    value = ((uint64_t)raw * coefficient + denominator / 2ULL) / denominator;
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
    uint64_t denominator;
    uint64_t value;

    if(!result || coefficient == 0U || coefficient == 0xFFFFU) return 0U;
    /* 手册：交流有效值最高位为 1 时表示零值。 */
    if((raw & 0x800000UL) != 0U) {
        *result = 0U;
        return 1U;
    }
    /* 手册公式输出单位为 10 mV，再除以 10 转换为 0.1 V。 */
    denominator = HLW_K2_NUM * (1ULL << 22) * 10ULL;
    value = ((uint64_t)raw * coefficient + denominator / 2ULL) / denominator;
    if(value > 3000ULL) return 0U;
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
