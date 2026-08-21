#ifndef SPLITAC_HEALTH_H
#define SPLITAC_HEALTH_H

#include <stdint.h>

#define HEALTH_LORA      0x01u
#define HEALTH_IR        0x02u
#define HEALTH_FLASH     0x04u
#define HEALTH_PERIODIC  0x08u
#define HEALTH_BLE_STACK 0x10u
#define HEALTH_CELLULAR  0x20u
#define HEALTH_REQUIRED  0x3Fu

void Health_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot);
void Health_Mark(uint8_t component);
uint8_t Health_Tick100ms(uint16_t fault_snapshot);
uint8_t Health_ConsecutiveResets(void);
uint8_t Health_LastResetReason(void);
uint8_t Health_LastUnhealthyMask(void);
uint8_t Health_StorageError(void);

#endif
