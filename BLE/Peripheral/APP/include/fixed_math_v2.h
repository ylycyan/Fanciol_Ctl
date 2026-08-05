#ifndef FIXED_MATH_V2_H
#define FIXED_MATH_V2_H

#include <stdint.h>

/*
 * 将网关协议中的 IEEE754 单精度参数解码为无符号整数。
 * 协议字节保持不变，但 MCU 不执行任何浮点运算。
 */
uint8_t FixedMathV2_DecodeUnsignedInteger(uint32_t ieee754Bits,
                                          uint8_t minValue,
                                          uint8_t maxValue,
                                          uint8_t *value);

/* SX126x: F_rf = PLL * 32 MHz / 2^25。 */
uint32_t FixedMathV2_FrequencyHzToPll(uint32_t frequencyHz);

#endif
