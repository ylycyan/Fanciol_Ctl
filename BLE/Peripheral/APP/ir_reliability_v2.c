#include "ir_reliability_v2.h"

#include <string.h>

static void SaturatingIncrement(uint16_t *value)
{
    if(*value != 0xFFFFU) (*value)++;
}

void IrReliabilityV2_Init(ir_reliability_v2_t *ctx)
{
    if(ctx != 0) memset(ctx, 0, sizeof(*ctx));
}

void IrReliabilityV2_RecordSubmitted(ir_reliability_v2_t *ctx)
{
    if(ctx != 0) SaturatingIncrement(&ctx->submitted_count);
}

void IrReliabilityV2_RecordBusyRejected(ir_reliability_v2_t *ctx)
{
    if(ctx != 0) SaturatingIncrement(&ctx->busy_rejected_count);
}

uint8_t IrReliabilityV2_ScheduleRepeat(ir_reliability_v2_t *ctx,
                                       uint8_t command,
                                       uint32_t now_ms)
{
    if(ctx == 0 || command == 0U || ctx->repeat_cmd != 0U) return 0U;
    ctx->repeat_cmd = command;
    ctx->repeat_due_ms = now_ms + IR_RELIABILITY_REPEAT_DELAY_MS;
    return 1U;
}

uint8_t IrReliabilityV2_TakeDueRepeat(ir_reliability_v2_t *ctx,
                                      uint32_t now_ms,
                                      uint8_t *command)
{
    if(ctx == 0 || command == 0 || ctx->repeat_cmd == 0U ||
       (int32_t)(now_ms - ctx->repeat_due_ms) < 0) {
        return 0U;
    }

    *command = ctx->repeat_cmd;
    ctx->repeat_cmd = 0U;
    SaturatingIncrement(&ctx->repeated_count);
    return 1U;
}

void IrReliabilityV2_CancelRepeat(ir_reliability_v2_t *ctx)
{
    if(ctx == 0) return;
    ctx->repeat_cmd = 0U;
    ctx->repeat_due_ms = 0U;
}
