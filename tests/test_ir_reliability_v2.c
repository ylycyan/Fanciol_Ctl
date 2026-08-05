#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ir_reliability_v2.h"

static void test_repeat_is_delayed_and_only_taken_once(void)
{
    ir_reliability_v2_t ctx;
    uint8_t command = 0U;

    IrReliabilityV2_Init(&ctx);
    assert(IrReliabilityV2_ScheduleRepeat(&ctx, 0x81U, 1000U));
    assert(!IrReliabilityV2_ScheduleRepeat(&ctx, 0x80U, 1000U));
    assert(!IrReliabilityV2_TakeDueRepeat(&ctx, 1179U, &command));
    assert(IrReliabilityV2_TakeDueRepeat(&ctx, 1180U, &command));
    assert(command == 0x81U);
    assert(ctx.repeated_count == 1U);
    assert(!IrReliabilityV2_TakeDueRepeat(&ctx, 2000U, &command));
}

static void test_deadline_is_safe_across_tick_wrap(void)
{
    ir_reliability_v2_t ctx;
    uint8_t command = 0U;

    IrReliabilityV2_Init(&ctx);
    assert(IrReliabilityV2_ScheduleRepeat(&ctx, 0xA2U, UINT32_MAX - 99U));
    assert(!IrReliabilityV2_TakeDueRepeat(&ctx, 79U, &command));
    assert(IrReliabilityV2_TakeDueRepeat(&ctx, 80U, &command));
    assert(command == 0xA2U);
}

static void test_counters_saturate_and_cancel_clears_pending(void)
{
    ir_reliability_v2_t ctx;

    IrReliabilityV2_Init(&ctx);
    ctx.submitted_count = 0xFFFFU;
    ctx.busy_rejected_count = 0xFFFFU;
    IrReliabilityV2_RecordSubmitted(&ctx);
    IrReliabilityV2_RecordBusyRejected(&ctx);
    assert(ctx.submitted_count == 0xFFFFU);
    assert(ctx.busy_rejected_count == 0xFFFFU);

    assert(IrReliabilityV2_ScheduleRepeat(&ctx, 0x54U, 0U));
    IrReliabilityV2_CancelRepeat(&ctx);
    assert(ctx.repeat_cmd == 0U);
}

int main(void)
{
    test_repeat_is_delayed_and_only_taken_once();
    test_deadline_is_safe_across_tick_wrap();
    test_counters_saturate_and_cancel_clears_pending();
    puts("ir_reliability_v2 tests passed");
    return 0;
}
