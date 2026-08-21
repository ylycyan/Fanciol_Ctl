#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ir_reliability.h"

static void test_repeat_is_delayed_and_only_taken_once(void)
{
    ir_reliability_t ctx;
    uint8_t command = 0U;

    IrReliability_Init(&ctx);
    assert(IrReliability_ScheduleRepeat(&ctx, 0x81U, 1000U));
    assert(!IrReliability_ScheduleRepeat(&ctx, 0x80U, 1000U));
    assert(!IrReliability_TakeDueRepeat(&ctx, 1179U, &command));
    assert(IrReliability_TakeDueRepeat(&ctx, 1180U, &command));
    assert(command == 0x81U);
    assert(!IrReliability_TakeDueRepeat(&ctx, 2000U, &command));
}

static void test_deadline_is_safe_across_tick_wrap(void)
{
    ir_reliability_t ctx;
    uint8_t command = 0U;

    IrReliability_Init(&ctx);
    assert(IrReliability_ScheduleRepeat(&ctx, 0xA2U, UINT32_MAX - 99U));
    assert(!IrReliability_TakeDueRepeat(&ctx, 79U, &command));
    assert(IrReliability_TakeDueRepeat(&ctx, 80U, &command));
    assert(command == 0xA2U);
}

static void test_cancel_clears_pending(void)
{
    ir_reliability_t ctx;

    IrReliability_Init(&ctx);
    assert(IrReliability_ScheduleRepeat(&ctx, 0x54U, 0U));
    IrReliability_CancelRepeat(&ctx);
    assert(ctx.repeat_cmd == 0U);
}

int main(void)
{
    test_repeat_is_delayed_and_only_taken_once();
    test_deadline_is_safe_across_tick_wrap();
    test_cancel_clears_pending();
    puts("ir_reliability tests passed");
    return 0;
}
