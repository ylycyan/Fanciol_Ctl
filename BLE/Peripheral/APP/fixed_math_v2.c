#include "fixed_math_v2.h"

uint8_t FixedMathV2_DecodeUnsignedInteger(uint32_t ieee754Bits,
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

uint32_t FixedMathV2_FrequencyHzToPll(uint32_t frequencyHz)
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
