#include "ir_control_map.h"

int8_t IrControl_LearnedChannel(IR_CMD_t command)
{
    switch(command) {
    case IR_CMD_POWER_ON:  return 0; /* 开机 */
    case IR_CMD_POWER_OFF: return 1; /* 关机 */
    case IR_CMD_MODE_COOL: return 2; /* 制冷 */
    case IR_CMD_MODE_HEAT: return 3; /* 制热 */
    case IR_CMD_MODE_DRY:  return 4; /* 除湿 */
    case IR_CMD_MODE_FAN:  return 5; /* 送风 */
    case IR_CMD_TEMP_UP:   return 6; /* 温度加 */
    case IR_CMD_TEMP_DOWN: return 7; /* 温度减 */
    default:
        /*
         * 自动模式、绝对温度、绝对风速和高级开关无法由单个学习键
         * 保证语义一致，必须明确返回不支持。
         */
        return -1;
    }
}
