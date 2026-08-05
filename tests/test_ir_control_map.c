#include <assert.h>
#include <stdio.h>
#include "ir_control_map.h"

int main(void)
{
    assert(IrControl_LearnedChannel(IR_CMD_POWER_ON) == 0);
    assert(IrControl_LearnedChannel(IR_CMD_POWER_OFF) == 1);
    assert(IrControl_LearnedChannel(IR_CMD_MODE_COOL) == 2);
    assert(IrControl_LearnedChannel(IR_CMD_MODE_HEAT) == 3);
    assert(IrControl_LearnedChannel(IR_CMD_MODE_DRY) == 4);
    assert(IrControl_LearnedChannel(IR_CMD_MODE_FAN) == 5);
    assert(IrControl_LearnedChannel(IR_CMD_TEMP_UP) == 6);
    assert(IrControl_LearnedChannel(IR_CMD_TEMP_DOWN) == 7);

    assert(IrControl_LearnedChannel(IR_CMD_MODE_AUTO) == -1);
    assert(IrControl_LearnedChannel(IR_CMD_TEMP_24) == -1);
    assert(IrControl_LearnedChannel(IR_CMD_FAN_HIGH) == -1);
    assert(IrControl_LearnedChannel(IR_CMD_SLEEP_ON) == -1);

    puts("IR learned-channel mapping: PASS");
    return 0;
}
