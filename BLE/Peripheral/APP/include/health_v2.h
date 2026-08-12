#ifndef SPLITAC_HEALTH_V2_H
#define SPLITAC_HEALTH_V2_H

#include <stdint.h>

#define HEALTH_V2_LORA      0x01u
#define HEALTH_V2_IR        0x02u
#define HEALTH_V2_FLASH     0x04u
#define HEALTH_V2_PERIODIC  0x08u
#define HEALTH_V2_BLE_STACK 0x10u
#define HEALTH_V2_CELLULAR  0x20u
#define HEALTH_V2_REQUIRED  0x3Fu
#define HEALTH_V2_RECENT_LIMIT 32u

typedef struct {
    uint32_t generation;
    uint32_t timestamp;
    uint16_t fault_snapshot;
    uint8_t reset_reason;
    uint8_t consecutive_resets;
    uint8_t unhealthy_mask;
} health_event_v2_t;

void HealthV2_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot);
void HealthV2_Mark(uint8_t component);
uint8_t HealthV2_Tick100ms(uint16_t fault_snapshot);
uint8_t HealthV2_ConsecutiveResets(void);
uint8_t HealthV2_LastResetReason(void);
uint8_t HealthV2_LastUnhealthyMask(void);
uint8_t HealthV2_StorageError(void);
uint8_t HealthV2_HistoryCount(void);
uint8_t HealthV2_ReadRecent(uint8_t index, health_event_v2_t *event);

#endif
