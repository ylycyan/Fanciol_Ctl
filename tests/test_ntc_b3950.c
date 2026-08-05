#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ntc_b3950.h"

static void expect_temperature(uint16_t adc, int16_t expected)
{
    int16_t actual = 0;
    assert(NtcB3950_AdcToTempX10(adc, &actual));
    assert(actual == expected);
}

int main(void)
{
    int16_t previous = -401;
    int16_t actual;
    uint16_t adc;

    expect_temperature(6280U, -400);
    expect_temperature(4961U, 0);
    expect_temperature(3945U, 150);
    expect_temperature(3218U, 250);
    expect_temperature(2230U, 400);
    expect_temperature(631U, 850);

    assert(!NtcB3950_AdcToTempX10(6281U, &actual));
    assert(!NtcB3950_AdcToTempX10(630U, &actual));
    assert(!NtcB3950_AdcToTempX10(3218U, 0));

    /* ADC 值下降时温度必须单调上升，防止插值符号或边界回归。 */
    for(adc = 4095U; adc >= 631U; adc--) {
        assert(NtcB3950_AdcToTempX10(adc, &actual));
        assert(actual >= previous);
        previous = actual;
        if(adc == 631U) break;
    }

    puts("ntc_b3950 tests passed");
    return 0;
}
