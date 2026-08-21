#include "ir_reliability.h"

#include <string.h>

void IrReliability_Init(ir_reliability_t *ctx)
{
    if(ctx != 0) memset(ctx, 0, sizeof(*ctx));
}

uint8_t IrReliability_ScheduleRepeat(ir_reliability_t *ctx,
                                       uint8_t command,
                                       uint32_t now_ms)
{
    if(ctx == 0 || command == 0U || ctx->repeat_cmd != 0U) return 0U;
    ctx->repeat_cmd = command;
    ctx->repeat_due_ms = now_ms + IR_RELIABILITY_REPEAT_DELAY_MS;
    return 1U;
}

uint8_t IrReliability_TakeDueRepeat(ir_reliability_t *ctx,
                                      uint32_t now_ms,
                                      uint8_t *command)
{
    if(ctx == 0 || command == 0 || ctx->repeat_cmd == 0U ||
       (int32_t)(now_ms - ctx->repeat_due_ms) < 0) {
        return 0U;
    }

    *command = ctx->repeat_cmd;
    ctx->repeat_cmd = 0U;
    return 1U;
}

void IrReliability_CancelRepeat(ir_reliability_t *ctx)
{
    if(ctx == 0) return;
    ctx->repeat_cmd = 0U;
    ctx->repeat_due_ms = 0U;
}
