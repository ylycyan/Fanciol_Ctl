#ifndef __HLW8110_MATH_H__
#define __HLW8110_MATH_H__

#include <stdint.h>

/*
 * 板级固定参数：
 * - 电流采样电阻 2 mOhm，K1=2
 * - 电压分压网络 1K/1M，K2=1
 *
 * 返回单位与 BLE/LoRa 状态模型一致：
 * power=0.1 W，current=mA，voltage=0.1 V。
 */
uint16_t HLW8110_CalcPowerX10(uint32_t raw, uint16_t coefficient);
uint8_t HLW8110_CalcCurrentMa(uint32_t raw, uint16_t coefficient, uint16_t *result);
uint8_t HLW8110_CalcVoltageDv(uint32_t raw, uint16_t coefficient, uint16_t *result);
uint64_t HLW8110_CalcEnergyTenthWattSeconds(uint32_t pulses, uint16_t energy_coefficient,
                                           uint16_t hfconst, uint64_t *fraction);
uint8_t HLW8110_UartChecksum(uint8_t command, const uint8_t *data, uint8_t length);

#endif
