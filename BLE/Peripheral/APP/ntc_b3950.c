/**
 * @file ntc_b3950.c
 * @brief NTC 热敏电阻（B=3950）ADC 值 → 温度查表换算
 *
 * 表由产品原公式离线生成：R25=10K、B=3950、VCC=3.3V、Vref=1.05V，
 * 每 5℃ 一个点，覆盖 -40℃~85℃；运行时查表 + 整数线性插值，无浮点。
 */
#include "ntc_b3950.h"

#define NTC_MIN_TEMP_X10   (-400)
#define NTC_TABLE_STEP_X10 50

/*
 * 由产品原公式离线生成：R25=10K、B=3950、VCC=3.3V、Vref=1.05V。
 * 每 5℃ 一个点，运行时只做查表和整数线性插值。
 */
static const uint16_t ntcAdcTable[] = {
    6280, 6216, 6130, 6019, 5879, 5704, 5493, 5245, 4961,
    4645, 4303, 3945, 3580, 3218, 2868, 2537, 2230, 1951,
    1700, 1477, 1282, 1111, 963, 836, 725, 631
};

/**
 * @brief ADC 采样值 → 温度（×10，单位 0.1℃）
 *
 * @param adcValue 滤波后的 ADC 原始值
 * @param temperatureX10 输出温度
 * @retval 1 成功；0 越界（超出 -40~85℃ 范围）
 */
uint8_t NtcB3950_AdcToTempX10(uint16_t adcValue, int16_t *temperatureX10)
{
    uint8_t i;

    if(temperatureX10 == 0) return 0;
    if(adcValue > ntcAdcTable[0] ||
       adcValue < ntcAdcTable[(sizeof(ntcAdcTable) / sizeof(ntcAdcTable[0])) - 1U]) {
        return 0;
    }

    /* 找到 adcValue 所在区间 [coldAdc, warmAdc]，按比例线性插值温度 */
    for(i = 0; i + 1U < (sizeof(ntcAdcTable) / sizeof(ntcAdcTable[0])); i++) {
        uint16_t coldAdc = ntcAdcTable[i];
        uint16_t warmAdc = ntcAdcTable[i + 1U];

        if(adcValue <= coldAdc && adcValue >= warmAdc) {
            uint16_t span = (uint16_t)(coldAdc - warmAdc);
            uint16_t offset = (uint16_t)(coldAdc - adcValue);
            int16_t base = (int16_t)(NTC_MIN_TEMP_X10 + (int16_t)i * NTC_TABLE_STEP_X10);
            uint16_t interpolated = (uint16_t)(((uint32_t)offset * NTC_TABLE_STEP_X10 +
                                                span / 2U) / span);
            *temperatureX10 = (int16_t)(base + (int16_t)interpolated);
            return 1;
        }
    }

    return 0;
}
