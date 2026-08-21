/**
 * @file fixed_math.c
 * @brief 定点数学工具：协议参数解码与频率换算（避免浮点/64 位除法）
 */
#include "fixed_math.h"

/**
 * @brief 解码 IEEE754 单精度比特位为受限整数
 *
 * 仅接受"整数值可精确表示"的正数（尾数低位必须全 0），
 * 用于解析网关下发的整数参数（如温度设定），拒绝负数/小数/NaN/Inf。
 */
uint8_t FixedMath_DecodeUnsignedInteger(uint32_t ieee754Bits,
                                          uint8_t minValue,
                                          uint8_t maxValue,
                                          uint8_t *value)
{
    uint32_t exponent;
    uint32_t mantissa;
    uint32_t integerValue;
    uint32_t shift;

    if(value == 0 || minValue > maxValue) return 0;

    /* +0 和 -0 都按整数 0 处理，其余负数全部拒绝。 */
    if((ieee754Bits & 0x7FFFFFFFUL) == 0U) {
        integerValue = 0U;
    } else {
        if((ieee754Bits & 0x80000000UL) != 0U) return 0;

        exponent = (ieee754Bits >> 23) & 0xFFU;
        if(exponent == 0U || exponent == 0xFFU || exponent < 127U) return 0;

        exponent -= 127U;
        /* 当前协议的参数最大为 31；限制到 8 位可避免移位溢出。 */
        if(exponent > 7U) return 0;

        mantissa = 0x00800000UL | (ieee754Bits & 0x007FFFFFUL);
        shift = 23U - exponent;
        if((mantissa & ((1UL << shift) - 1UL)) != 0U) return 0;
        integerValue = mantissa >> shift;
    }

    if(integerValue < minValue || integerValue > maxValue) return 0;
    *value = (uint8_t)integerValue;
    return 1;
}

/**
 * @brief 射频频率 (Hz) → SX126x PLL 分频值
 *
 * SX126x 频率寄存器 = freq / (32MHz / 2^25) = freq × 2^25 / 32MHz。
 * 分解为商余避免 64 位除法溢出：2^25/32e6 = 131072/125000 = 1 + 6072/125000。
 */
uint32_t FixedMath_FrequencyHzToPll(uint32_t frequencyHz)
{
    uint32_t quotient = frequencyHz / 125000UL;
    uint32_t remainder = frequencyHz % 125000UL;

    /*
     * 2^25 / 32,000,000 = 131072 / 125000 = 1 + 6072 / 125000。
     * 先做商余分解，避免 64 位除法，也避免 frequencyHz * 6072 溢出。
     */
    return frequencyHz +
           quotient * 6072UL +
           (remainder * 6072UL) / 125000UL;
}
