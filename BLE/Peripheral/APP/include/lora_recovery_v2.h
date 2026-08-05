#ifndef __LORA_RECOVERY_V2_H__
#define __LORA_RECOVERY_V2_H__

#include <stdint.h>

typedef struct {
    uint32_t next_attempt_ms;
    uint8_t consecutive_failures;
} lora_recovery_v2_t;

void LoraRecoveryV2_Init(lora_recovery_v2_t *ctx, uint32_t now_ms);
uint8_t LoraRecoveryV2_ShouldAttempt(const lora_recovery_v2_t *ctx, uint32_t now_ms);
void LoraRecoveryV2_MarkFailed(lora_recovery_v2_t *ctx, uint32_t now_ms);
void LoraRecoveryV2_MarkSucceeded(lora_recovery_v2_t *ctx, uint32_t now_ms);
uint32_t LoraRecoveryV2_CurrentDelayMs(const lora_recovery_v2_t *ctx);

#endif
