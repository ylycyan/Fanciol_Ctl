/**
 * @file ir_control_map.c
 * @brief 红外命令 → 学习码通道映射
 *
 * 学习模式共 10 个固定通道，语义约定：
 *   0开机 1关机 2制冷 3制热 4除湿 5送风 6温度+ 7温度- 8风速 9自定义
 * 绝对温度、自动模式、绝对风速等无法由单个学习键保证一致性，明确返回 -1。
 */
#include "ir_control_map.h"

/**
 * @brief 将红外命令映射到学习码通道
 * @return 通道号 0~9；不支持的命令返回 -1
 */
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
