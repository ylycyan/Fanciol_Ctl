#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "lora_recovery.h"

int main(void)
{
    lora_recovery_t ctx;
    uint32_t now = 1000U;

    LoraRecovery_Init(&ctx, now);
    assert(LoraRecovery_ShouldAttempt(&ctx, now));

    LoraRecovery_MarkFailed(&ctx, now);
    assert(ctx.consecutive_failures == 1U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 5000U);
    assert(!LoraRecovery_ShouldAttempt(&ctx, 5999U));
    assert(LoraRecovery_ShouldAttempt(&ctx, 6000U));

    LoraRecovery_MarkFailed(&ctx, 6000U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 10000U);
    LoraRecovery_MarkFailed(&ctx, 16000U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 20000U);
    LoraRecovery_MarkFailed(&ctx, 36000U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 40000U);
    LoraRecovery_MarkFailed(&ctx, 76000U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 60000U);
    LoraRecovery_MarkFailed(&ctx, 136000U);
    assert(LoraRecovery_CurrentDelayMs(&ctx) == 60000U);

    LoraRecovery_MarkSucceeded(&ctx, 200000U);
    assert(ctx.consecutive_failures == 0U);
    assert(LoraRecovery_ShouldAttempt(&ctx, 200000U));

    /* 验证毫秒时钟回卷后的到期判断。 */
    LoraRecovery_Init(&ctx, UINT32_MAX - 1000U);
    LoraRecovery_MarkFailed(&ctx, UINT32_MAX - 1000U);
    assert(!LoraRecovery_ShouldAttempt(&ctx, 3000U));
    assert(LoraRecovery_ShouldAttempt(&ctx, 4000U));

    puts("LoRa recovery backoff: PASS");
    return 0;
}
