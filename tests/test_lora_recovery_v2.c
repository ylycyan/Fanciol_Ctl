#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "lora_recovery_v2.h"

int main(void)
{
    lora_recovery_v2_t ctx;
    uint32_t now = 1000U;

    LoraRecoveryV2_Init(&ctx, now);
    assert(LoraRecoveryV2_ShouldAttempt(&ctx, now));

    LoraRecoveryV2_MarkFailed(&ctx, now);
    assert(ctx.consecutive_failures == 1U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 5000U);
    assert(!LoraRecoveryV2_ShouldAttempt(&ctx, 5999U));
    assert(LoraRecoveryV2_ShouldAttempt(&ctx, 6000U));

    LoraRecoveryV2_MarkFailed(&ctx, 6000U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 10000U);
    LoraRecoveryV2_MarkFailed(&ctx, 16000U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 20000U);
    LoraRecoveryV2_MarkFailed(&ctx, 36000U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 40000U);
    LoraRecoveryV2_MarkFailed(&ctx, 76000U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 60000U);
    LoraRecoveryV2_MarkFailed(&ctx, 136000U);
    assert(LoraRecoveryV2_CurrentDelayMs(&ctx) == 60000U);

    LoraRecoveryV2_MarkSucceeded(&ctx, 200000U);
    assert(ctx.consecutive_failures == 0U);
    assert(LoraRecoveryV2_ShouldAttempt(&ctx, 200000U));

    /* 验证毫秒时钟回卷后的到期判断。 */
    LoraRecoveryV2_Init(&ctx, UINT32_MAX - 1000U);
    LoraRecoveryV2_MarkFailed(&ctx, UINT32_MAX - 1000U);
    assert(!LoraRecoveryV2_ShouldAttempt(&ctx, 3000U));
    assert(LoraRecoveryV2_ShouldAttempt(&ctx, 4000U));

    puts("LoRa recovery backoff: PASS");
    return 0;
}
