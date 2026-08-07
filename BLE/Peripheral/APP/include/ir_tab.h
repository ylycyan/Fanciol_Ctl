#ifndef IR_TAB_H
#define IR_TAB_H

#include <stdbool.h>
#include <stdint.h>

#define IR_MODULE HXD039B
#define IR_BRAND_COUNT 82u

#if (IR_MODULE == HXD039B)

// 空外模块空调控制指令码
typedef enum {
  // power
  IR_CMD_POWER_OFF = 0x80, // 关闭空调
  IR_CMD_POWER_ON = 0x81,  // 开启空调
  // mode
  IR_CMD_MODE_AUTO = 0xa1, // 模式自动
  IR_CMD_MODE_COOL = 0xa2, // 模式制冷
  IR_CMD_MODE_DRY = 0xa3,  // 模式抽湿
  IR_CMD_MODE_FAN = 0xa4,  // 模式送风
  IR_CMD_MODE_HEAT = 0xa5, // 模式制热
  // temp
  IR_CMD_TEMP_16 = 0x40, // 16°
  IR_CMD_TEMP_17 = 0x41, // 17°
  IR_CMD_TEMP_18 = 0x42, // 18°
  IR_CMD_TEMP_19 = 0x43, // 19°
  IR_CMD_TEMP_20 = 0x44, // 20°
  IR_CMD_TEMP_21 = 0x45, // 21°
  IR_CMD_TEMP_22 = 0x46, // 22°
  IR_CMD_TEMP_23 = 0x47, // 23°
  IR_CMD_TEMP_24 = 0x48, // 24°
  IR_CMD_TEMP_25 = 0x49, // 25°
  IR_CMD_TEMP_26 = 0x4a, // 26°
  IR_CMD_TEMP_27 = 0x4b, // 27°
  IR_CMD_TEMP_28 = 0x4c, // 28°
  IR_CMD_TEMP_29 = 0x4d, // 29°
  IR_CMD_TEMP_30 = 0x4e, // 30°
  IR_CMD_TEMP_31 = 0x4f, // 31°
  // fan speed
  IR_CMD_FAN_AUTO = 0x51, // 自动风速
  IR_CMD_FAN_LOW = 0x52,  // 风速低
  IR_CMD_FAN_MID = 0x53,  // 风速中
  IR_CMD_FAN_HIGH = 0x54, // 风速高
  // wind direction
  IR_CMD_WIND_UP = 0x61,   // 风向向上(上下摆风)
  IR_CMD_WIND_MID = 0x62,  // 风向中
  IR_CMD_WIND_DOWN = 0x63, // 风向向下(上下停摆)
  // wind direction auto
  IR_CMD_WIND_AUTO_OFF = 0x70, // 自动风向关闭
  IR_CMD_WIND_AUTO_ON = 0x71,  // 自动风向打开
  // sleep
  IR_CMD_SLEEP_OFF = 0xb0, // 睡眠关
  IR_CMD_SLEEP_ON = 0xb1,  // 睡眠开
  // aux heat
  IR_CMD_AUX_HEAT_OFF = 0xc0, // 辅热关
  IR_CMD_AUX_HEAT_ON = 0xc1,  // 辅热开
  // light
  IR_CMD_LIGHT_OFF = 0xd0,        // 灯光关
  IR_CMD_LIGHT_ON = 0xd1,         // 灯光开
  IR_CMD_SLEEP_ENERGY_OFF = 0xe0, // 节能关
  IR_CMD_SLEEP_ENERGY_ON = 0xe1,  // 节能开
  IR_CMD_TEMP_DOWN = 0x96,        // 温度减
  IR_CMD_TEMP_UP = 0x97,          // 温度加
  // fan speed
  IR_CMD_FAST_COOL = 0x9c, // 快速制冷
  IR_CMD_FAST_HEAT = 0x9d, // 快速制热
  // sleep
  IR_CMD_MUTE_OFF = 0x9e, // 静音关
  IR_CMD_MUTE_ON = 0x9f,  // 静音开
    Ir_Illegal
} IR_CMD_t;

#elif (IR_MODULE) // 其他红外模块定义
#error undefined ir module
#endif

void Check_IrBuf(void);
void IR_Init(void);
#endif
