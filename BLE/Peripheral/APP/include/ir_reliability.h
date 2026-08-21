#ifndef SPLITAC_IR_RELIABILITY_H
#define SPLITAC_IR_RELIABILITY_H

#include <stdint.h>

#define IR_RELIABILITY_REPEAT_DELAY_MS 180U

/*
 * 红外链路没有统一的执行回执。对“绝对状态”内部码，在首次串口提交后
 * 延时补发一次；相对温度键和学习码不使用该策略，避免一次操作执行两次。
 */
typedef struct {
    uint32_t repeat_due_ms;
    uint8_t repeat_cmd;
} ir_reliability_t;

void IrReliability_Init(ir_reliability_t *ctx);
uint8_t IrReliability_ScheduleRepeat(ir_reliability_t *ctx,
                                       uint8_t command,
                                       uint32_t now_ms);
uint8_t IrReliability_TakeDueRepeat(ir_reliability_t *ctx,
                                      uint32_t now_ms,
                                      uint8_t *command);
void IrReliability_CancelRepeat(ir_reliability_t *ctx);

#endif
