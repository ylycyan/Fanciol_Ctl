#ifndef SPLITAC_HEALTH_V2_H
#define SPLITAC_HEALTH_V2_H

#include <stdint.h>

#define HEALTH_V2_LORA      0x01u
#define HEALTH_V2_IR        0x02u
#define HEALTH_V2_FLASH     0x04u
#define HEALTH_V2_PERIODIC  0x08u
#define HEALTH_V2_BLE_STACK 0x10u
#define HEALTH_V2_REQUIRED  0x1Fu

void HealthV2_Init(uint16_t fault_snapshot);
void HealthV2_Mark(uint8_t component);
uint8_t HealthV2_Tick100ms(void);
uint8_t HealthV2_ConsecutiveResets(void);
uint8_t HealthV2_LastResetReason(void);

#endif
