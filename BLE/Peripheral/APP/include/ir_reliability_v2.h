#ifndef SPLITAC_IR_RELIABILITY_V2_H
#define SPLITAC_IR_RELIABILITY_V2_H

#include <stdint.h>

#define IR_RELIABILITY_REPEAT_DELAY_MS 180U

/*
 * 红外链路没有统一的执行回执。对“绝对状态”内部码，在首次串口提交后
 * 延时补发一次；相对温度键和学习码不使用该策略，避免一次操作执行两次。
 * 计数只保存在 RAM，用于现场诊断，不增加 Flash 擦写。
 */
typedef struct {
    uint32_t repeat_due_ms;
    uint16_t submitted_count;
    uint16_t repeated_count;
    uint16_t busy_rejected_count;
    uint8_t repeat_cmd;
} ir_reliability_v2_t;

void IrReliabilityV2_Init(ir_reliability_v2_t *ctx);
void IrReliabilityV2_RecordSubmitted(ir_reliability_v2_t *ctx);
void IrReliabilityV2_RecordBusyRejected(ir_reliability_v2_t *ctx);
uint8_t IrReliabilityV2_ScheduleRepeat(ir_reliability_v2_t *ctx,
                                       uint8_t command,
                                       uint32_t now_ms);
uint8_t IrReliabilityV2_TakeDueRepeat(ir_reliability_v2_t *ctx,
                                      uint32_t now_ms,
                                      uint8_t *command);
void IrReliabilityV2_CancelRepeat(ir_reliability_v2_t *ctx);

#endif
