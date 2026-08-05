#include "lora_recovery_v2.h"

#define LORA_RECOVERY_FIRST_DELAY_MS  5000UL
#define LORA_RECOVERY_MAX_DELAY_MS   60000UL

void LoraRecoveryV2_Init(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return;
    ctx->next_attempt_ms = now_ms;
    ctx->consecutive_failures = 0;
}

uint8_t LoraRecoveryV2_ShouldAttempt(const lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return 0;
    /* 所有退避间隔远小于 2^31 ms，带符号差值可安全跨越 32 位回卷。 */
    return (int32_t)(now_ms - ctx->next_attempt_ms) >= 0;
}

uint32_t LoraRecoveryV2_CurrentDelayMs(const lora_recovery_v2_t *ctx)
{
    uint32_t delay = LORA_RECOVERY_FIRST_DELAY_MS;
    uint8_t shifts;

    if(ctx == 0 || ctx->consecutive_failures == 0) return 0;
    shifts = (uint8_t)(ctx->consecutive_failures - 1U);
    while(shifts > 0U && delay < LORA_RECOVERY_MAX_DELAY_MS) {
        delay <<= 1;
        shifts--;
    }
    if(delay > LORA_RECOVERY_MAX_DELAY_MS) delay = LORA_RECOVERY_MAX_DELAY_MS;
    return delay;
}

void LoraRecoveryV2_MarkFailed(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return;
    if(ctx->consecutive_failures != 0xFFU) ctx->consecutive_failures++;
    ctx->next_attempt_ms = now_ms + LoraRecoveryV2_CurrentDelayMs(ctx);
}

void LoraRecoveryV2_MarkSucceeded(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    LoraRecoveryV2_Init(ctx, now_ms);
}
