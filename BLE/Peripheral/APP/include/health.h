#ifndef SPLITAC_HEALTH_H
#define SPLITAC_HEALTH_H

#include <stdint.h>

void Health_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot);
uint8_t Health_ConsecutiveResets(void);
uint8_t Health_LastResetReason(void);
uint8_t Health_LastUnhealthyMask(void);
uint8_t Health_StorageError(void);

#endif
