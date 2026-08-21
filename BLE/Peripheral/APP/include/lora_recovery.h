#ifndef SPLITAC_LORA_RECOVERY_H
#define SPLITAC_LORA_RECOVERY_H

#include <stdint.h>

typedef struct {
    uint32_t next_attempt_ms;
    uint8_t consecutive_failures;
} lora_recovery_t;

void LoraRecovery_Init(lora_recovery_t *ctx, uint32_t now_ms);
uint8_t LoraRecovery_ShouldAttempt(const lora_recovery_t *ctx, uint32_t now_ms);
void LoraRecovery_MarkFailed(lora_recovery_t *ctx, uint32_t now_ms);
void LoraRecovery_MarkSucceeded(lora_recovery_t *ctx, uint32_t now_ms);
uint32_t LoraRecovery_CurrentDelayMs(const lora_recovery_t *ctx);

#endif
