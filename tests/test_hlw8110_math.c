#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hlw8110_math.h"

int main(void)
{
    uint64_t energy_fraction = 0;
    uint16_t value = 0;
    const uint8_t unlock = 0xE5U;
    const uint8_t lock = 0xDCU;
    const uint8_t select_a = 0x5AU;
    const uint8_t read_hfconst[2] = {0x10U, 0x00U};

    /* 手册 11.2 节的 UART 黄金帧。 */
    assert(HLW8110_UartChecksum(0xEAU, &unlock, 1U) == 0x8BU);
    assert(HLW8110_UartChecksum(0xEAU, &lock, 1U) == 0x94U);
    assert(HLW8110_UartChecksum(0xEAU, &select_a, 1U) == 0x16U);
    assert(HLW8110_UartChecksum(0x02U, read_hfconst, 2U) == 0x48U);
    assert(HLW8110_UartChecksum(0x02U, 0, 1U) == 0U);

    /* 手册公式的整值向量，板级 K1=2、K2=1。 */
    assert(HLW8110_CalcCurrentMa(0x400000UL, 10000U, &value) == 1U);
    assert(value == 2500U);
    assert(HLW8110_CalcVoltageDv(0x200000UL, 44000U, &value) == 1U);
    assert(value == 2200U);
    assert(HLW8110_CalcPowerX10(0x40000000UL, 4000U) == 10000U);
    assert(HLW8110_CalcPowerX10(0xC0000000UL, 4000U) == 10000U);

    /* 交流 RMS 最高位表示零；非法系数和越界量程必须被拒绝。 */
    assert(HLW8110_CalcCurrentMa(0x800001UL, 10000U, &value) == 1U && value == 0U);
    assert(HLW8110_CalcVoltageDv(0x800001UL, 44000U, &value) == 1U && value == 0U);
    assert(HLW8110_CalcCurrentMa(1U, 0U, &value) == 0U);
    assert(HLW8110_CalcVoltageDv(0x7FFFFFUL, 65534U, &value) == 0U);
    assert(HLW8110_CalcPowerX10(1U, 1U) == 0U);

    /* 32768 个脉冲在此系数组合下恰好为 1kWh，即 36,000,000 个 0.1W*s。 */
    assert(HLW8110_CalcEnergyTenthWattSeconds(32768U, 0x8000U, 0x1000U,
                                              &energy_fraction) == 36000000ULL);
    assert(energy_fraction == 0U);
    assert(HLW8110_CalcEnergyTenthWattSeconds(1U, 0xFFFFU, 0x1000U,
                                              &energy_fraction) > 0U);

    puts("HLW8110 datasheet conversion vectors: PASS");
    return 0;
}
