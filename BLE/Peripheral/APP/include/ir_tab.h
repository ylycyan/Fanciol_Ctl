#ifndef IR_TAB_H
#define IR_TAB_H

#include <stdbool.h>
#include <stdint.h>

#define IR_MODULE HXD039B
typedef struct {
  uint8_t num;
  char name[16];
  uint16_t cmd[200];
} t_arc;

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
  IR_CMD_FAN_FAST_COOL = 0x9c, // 快速制冷
  IR_CMD_FAN_FAST_HEAT = 0x9d, // 快速制热
  // sleep
  IR_CMD_SLEEP_MUTE_OFF = 0x9e, // 静音关
  IR_CMD_SLEEP_MUTE_ON = 0x9f,  // 静音开
  Ir_Illegal
} IR_CMD_t;

// 空调控制指令码表
// static const t_arc g_arc_info[] = {};


static const t_arc g_arc_info[] =
    {
        {.num = 24,
         .name = "美的(Midea)",
         .cmd = {1091, 1038, 1035, 1034, 1032, 1016, 833,  925,
                 1015, 930,  556,  663,  525,  521,  593,  526,
                 1106, 1109, 1110, 1111, 1112, 1116, 1120, 1130}},
        {.num = 29,
         .name = "东芝",
         .cmd = {1067, 1038, 911,  854,  994,  1027, 795,  1016, 1015, 695,
                 791,  790,  930,  663,  525,  521,  593,  1110, 1111, 1112,
                 1116, 1120, 1130, 1140, 1141, 1142, 1143, 1144, 1145}},

        {.num = 42, .name = "海尔(Haier)", .cmd = {1086, 1085, 1076, 1072, 1068,
                                                   1064, 988,  956,  1001, 613,
                                                   930,  923,  998,  999,  994,
                                                   972,  955,  878,  899,  844,
                                                   917,  903,  863,  670,  650,
                                                   627,  621,  554,  552,  555,
                                                   551,  522,  517,  523,  1123,
                                                   1124, 1132, 1134, 1135, 1138,
                                                   1139, 1140}}, /*海尔(Haier)@16(+
                                                                    93*/

        {.num = 19,
         .name = "格力(Gree)",
         .cmd = {830, 526, 1056, 912, 599, 1, 518, 668, 603, 602, 549, 542, 514,
                 513, 605, 604, 1108, 1116, 1119}}, /*格力(Gree)@18(+95*/


        {
            .num = 32,
            .name = "奥克斯(AUX)",
            .cmd = {960,  1089, 1002, 663,  995,  915,  890,  879,  868,
                    851,  867,  866,  853,  532,  533,  527,  536,  529,
                    530,  531,  535,  571,  1105, 1106, 1109, 1110, 1111,
                    1112, 1120, 1130, 1136, 1137}}, /*奥克斯(AUX)@12(+97*/


        {.num = 26,
         .name =
             "海信(Hisense)",
         .cmd =
             {1063, 1062, 1053, 1038, 782,  952,  1014, 1007, 940,
              924,  873,  846,  833,  927,  858,  842,  902,  518,
              570,  558,  831,  1141, 1142, 1143, 1144, 1145}}, /*海信(Hisense)@8(
                                                                   +68*/
        {.num = 13,
         .name = "开利(Carrier)",
         .cmd = {922, 1027, 513, 872, 856, 798, 525, 954, 916, 662, 655, 654,
                 1117}}, /*开利(Carrier)@7( +25*/


        {.num = 7,
         .name = "扬子(YAIR)",
         .cmd = {1002, 921, 832, 1098, 1105, 1106, 1109}}, /*扬子(YAIR)@4(+20 */

        {.num = 21, .name = "惠而浦", .cmd = {819, 798, 842, 971, 856, 626,
                                              818, 581, 935, 924, 832, 526,
                                              876, 846, 852, 851, 850, 839,
                                              512, 532, 648}}, /*惠而浦(Whirlpool)@4(17+
                                                                */

        {
            .num = 31,
            .name = "大金(Daikin)",
            .cmd = {1061, 1038, 1030, 600,  599,  1028, 1013, 1010,
                    9,    6,    908,  790,  791,  897,  881,  861,
                    845,  527,  834,  677,  678,  1105, 1106, 1109,
                    1110, 1111, 1112, 1120, 1130, 1136, 1137}}, /*大金(Daikin)@5(
                                                                   +97  */

        {.num = 15,
         .name = "伊莱克斯",
         .cmd = {960, 828, 1097, 541, 923, 920, 721, 887, 516, 628, 1132, 1135,
                 1138, 1139, 1140}}, /* 伊莱克斯(Electrolux)@13(+3 */

        {.num = 6,
         .name = "熊猫(Panda)",
         .cmd = {1001, 519, 800, 1097, 804, 811}}, /*熊猫(Panda)@13 */

        {.num = 21, .name = "乐华(ROWA)", .cmd = {819,  869, 605, 617,  600,
                                                  601,  597, 602, 800,  804,
                                                  1102, 612, 610, 582,  661,
                                                  667,  556, 545, 1122, 1139,
                                                  1140}}, /*乐华(ROWA)@1*/

        {.num = 30,
         .name = "富士通",
         .cmd =
             {1046, 902, 770, 952, 927, 3,    990,  991, 969, 966, 945,
              937,  813, 879, 670, 622, 623,  535,  626, 627, 612, 610,
              582,  661, 667, 556, 545, 1122, 1132, 1135}}, /*富士通(Fujitsu)@15
                                                             */

        {.num = 28,
         .name = "约克(York)",
         .cmd =
             {1079, 1070, 1066, 1050, 1029, 833, 590, 588, 849, 673,
              994,  935,  1014, 953,  908,  846, 532, 860, 609, 664,
              630,  631,  582,  661,  667,  556, 545, 1122}}, /*约克(York)@15 */
        {.num = 18,
         .name = "麦克维尔",
         .cmd = {1084, 1070, 963, 626, 3, 935, 908, 865, 664, 630, 631, 582,
                 661, 667, 556, 545, 1122, 1127}}, /*麦克维尔(McQuay)@15 */
        {.num = 24,
         .name = "夏普(SHARP)",
         .cmd = {1026, 1018, 1020, 1021, 1022, 1023, 1012, 752, 864,
                 777,  781,  782,  784,  619,  618,  523,  608, 1106,
                 1109, 1110, 1111, 1112, 1120, 1130}}, /* 夏普(SHARP)@788*/
        {.num = 2, .name = "小米(MI)", .cmd = {1052, 512}}, /*小米(MI)@11(+59 */
        {.num = 4,
         .name = "银燕(YINYAN)",
         .cmd = {1038, 1001, 1000, 1128}}, /*银燕(YINYAN)@11(+59 */
        {.num = 2,
         .name = "创维",
         .cmd = {993, 992}}, /*创维(Skyworth)@11(+59 */
        {.num = 2,
         .name = "深松",
         .cmd = {512, 958}}, /*深松(Pastamic)@11(+59 */
        {.num = 1,
         .name = "海林(HaiLin)",
         .cmd = {704}}, /*海林(HaiLin)@11(+59 */
        {.num = 1,
         .name = "MAVELL(MAVELL)",
         .cmd = {960}}, /*MAVELL(MAVELL)@11(+59 */
        {.num = 1,
         .name = "泰阳(Taiyang)",
         .cmd = {1041}}, /*泰阳(Taiyang)@11(+59 */
        {.num = 1, .name = "VINO(VINO)", .cmd = {1051}}, /*VINO(VINO)@11(+59 */
        {.num = 2,
         .name = "米家(MI JIA)",
         .cmd = {958, 1052}}, /*米家(MI JIA)@11(+59 */
};

#elif (IR_MODULE) // 其他红外模块定义
#error undefined ir module
#endif

void Check_IrBuf(void);
void Ir_cmd(IR_CMD_t cmd);
void IR_Init(void);
#endif
