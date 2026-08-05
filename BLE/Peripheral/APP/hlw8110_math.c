#include "hlw8110_math.h"

#define HLW_K1_NUM              2ULL
#define HLW_K2_NUM              1ULL
#define HLW_POWER_NOISE_X10     20ULL

uint8_t HLW8110_UartChecksum(uint8_t command, const uint8_t *data, uint8_t length)
{
    uint8_t sum = (uint8_t)(0xA5U + command);
    uint8_t i;

    if(length > 0U && data == 0) return 0U;
    for(i = 0; i < length; ++i) sum = (uint8_t)(sum + data[i]);
    return (uint8_t)(~sum);
}

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
