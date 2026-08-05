#ifndef NTC_B3950_H
#define NTC_B3950_H

#include <stdint.h>

/*
 * 10K/B3950 NTC，10K 上拉，3.3V 分压，CH583 ADC 0dB：
 * 将校准后的平均 ADC 原始值转换为 0.1℃。
 */
uint8_t NtcB3950_AdcToTempX10(uint16_t adcValue, int16_t *temperatureX10);

#endif
