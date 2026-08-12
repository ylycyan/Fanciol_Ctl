/**
 * @file lora_recovery_v2.c
 * @brief LoRa 射频故障退避重连调度（指数退避，纯 RAM 状态）
 *
 * 退避序列：5s → 10s → 20s → 40s → 60s（封顶），
 * 连续失败计数饱和不归零；成功一次即重置。
 */
#include "lora_recovery_v2.h"

#define LORA_RECOVERY_FIRST_DELAY_MS  5000UL
#define LORA_RECOVERY_MAX_DELAY_MS   60000UL

/**
 * @brief 初始化/重置退避状态（成功后也应调用）
 */
void LoraRecoveryV2_Init(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return;
    ctx->next_attempt_ms = now_ms;
    ctx->consecutive_failures = 0;
}

/**
 * @brief 判断当前是否到达下一次重试时间
 */
uint8_t LoraRecoveryV2_ShouldAttempt(const lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return 0;
    /* 所有退避间隔远小于 2^31 ms，带符号差值可安全跨越 32 位回卷。 */
    return (int32_t)(now_ms - ctx->next_attempt_ms) >= 0;
}

/**
 * @brief 计算当前退避延迟（基于连续失败次数指数增长）
 */
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

/**
 * @brief 记录一次失败：累计计数并按当前退避延迟安排下次尝试
 */
void LoraRecoveryV2_MarkFailed(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    if(ctx == 0) return;
    if(ctx->consecutive_failures != 0xFFU) ctx->consecutive_failures++;
    ctx->next_attempt_ms = now_ms + LoraRecoveryV2_CurrentDelayMs(ctx);
}

/**
 * @brief 记录一次成功：重置退避
 */
void LoraRecoveryV2_MarkSucceeded(lora_recovery_v2_t *ctx, uint32_t now_ms)
{
    LoraRecoveryV2_Init(ctx, now_ms);
}
