#ifndef SPLITAC_HEALTH_H
#define SPLITAC_HEALTH_H

#include <stdint.h>

/* Snapshot at startup only; zero means the external RTC was not valid. */
void Health_Init(uint8_t boot_reset_reason, uint32_t startup_timestamp);
uint8_t Health_ConsecutiveResets(void);
uint8_t Health_LastResetReason(void);
uint32_t Health_BootTimestamp(void);
uint8_t Health_LastUnhealthyMask(void);
uint8_t Health_StorageError(void);

#endif
