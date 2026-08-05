#ifndef SPLITAC_IR_CONTROL_MAP_H
#define SPLITAC_IR_CONTROL_MAP_H

#include <stdint.h>
#include "ir_tab.h"

/*
 * 将标准空调命令映射到现场约定的学习通道。
 * 返回 -1 表示该命令没有可靠的学习码语义，不能假装成绝对控制。
 */
int8_t IrControl_LearnedChannel(IR_CMD_t command);

#endif
