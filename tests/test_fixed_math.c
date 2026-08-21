#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "fixed_math.h"

static void test_ieee754_integer_decoder(void)
{
    uint8_t value = 0xFFU;

    assert(FixedMath_DecodeUnsignedInteger(0x00000000UL, 0U, 4U, &value));
    assert(value == 0U);
    assert(FixedMath_DecodeUnsignedInteger(0x80000000UL, 0U, 4U, &value));
    assert(value == 0U);
    assert(FixedMath_DecodeUnsignedInteger(0x40800000UL, 0U, 4U, &value));
    assert(value == 4U);
    assert(FixedMath_DecodeUnsignedInteger(0x41800000UL, 16U, 31U, &value));
    assert(value == 16U);
    assert(FixedMath_DecodeUnsignedInteger(0x41F80000UL, 16U, 31U, &value));
    assert(value == 31U);

    assert(!FixedMath_DecodeUnsignedInteger(0x41840000UL, 16U, 31U, &value)); /* 16.5 */
    assert(!FixedMath_DecodeUnsignedInteger(0xC1800000UL, 16U, 31U, &value)); /* -16 */
    assert(!FixedMath_DecodeUnsignedInteger(0x7F800000UL, 0U, 31U, &value));  /* +Inf */
    assert(!FixedMath_DecodeUnsignedInteger(0x7FC00000UL, 0U, 31U, &value));  /* NaN */
    assert(!FixedMath_DecodeUnsignedInteger(0x42000000UL, 16U, 31U, &value)); /* 32 */
    assert(!FixedMath_DecodeUnsignedInteger(0x3F000000UL, 0U, 31U, &value));  /* 0.5 */
    assert(!FixedMath_DecodeUnsignedInteger(0x3F800000UL, 2U, 4U, &value));
    assert(!FixedMath_DecodeUnsignedInteger(0x3F800000UL, 0U, 4U, 0));
}

static void test_frequency_conversion(void)
{
    static const uint32_t frequencies[] = {
        420050000UL, 423187500UL, 426787500UL, 429650000UL
    };
    uint8_t i;

    for(i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++) {
        uint32_t expected = (uint32_t)(((uint64_t)frequencies[i] * 33554432ULL) /
                                       32000000ULL);
        assert(FixedMath_FrequencyHzToPll(frequencies[i]) == expected);
    }
}

int main(void)
{
    test_ieee754_integer_decoder();
    test_frequency_conversion();
    puts("fixed_math tests passed");
    return 0;
}
