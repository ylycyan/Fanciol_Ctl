/********************************** (C) COPYRIGHT *******************************
 * File Name          : peripheral.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef PERIPHERAL_H
#define PERIPHERAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */

/*********************************************************************
 * CONSTANTS
 */

// Peripheral Task Events
#define SBP_START_DEVICE_EVT    0x0001
#define SBP_PERIODIC_EVT        0x0002
#define SBP_READ_RSSI_EVT       0x0004
#define SBP_PARAM_UPDATE_EVT    0x0008
#define SBP_PHY_UPDATE_EVT      0x0010
#define OTA_FLASH_ERASE_EVT     0x0020

// 蓝牙通讯协议定义
// 帧结构: LEN(1) + CMD(1) + PAYLOAD(N) + CRC(1)

// 指令类型
typedef enum {
    BT_CMD_WRITE      = 0x01, // 写属性 (上位机 -> 设备)
    BT_CMD_READ       = 0x02, // 读属性 (上位机 -> 设备)
    BT_CMD_NOTIFY     = 0x03, // 属性通知/应答 (设备 -> 上位机)
    BT_CMD_ACK        = 0x04, // 操作成功应答 (设备 -> 上位机)
    BT_CMD_ERROR      = 0x05, // 错误应答 (设备 -> 上位机)
    BT_CMD_ACTION     = 0x06  // 执行动作 (上位机 -> 设备)
} BT_CMD_Type;

// 属性ID定义 (PID)
typedef enum {
    PID_SWITCH        = 0x01, // 开关 (u8: 0关 1开)
    PID_MODE          = 0x02, // 模式 (u8: 0自动 1制冷 2除湿 3送风 4制热)
    PID_TEMP_SET      = 0x03, // 设定温度 (u16: 放大10倍)
    PID_TEMP_ROOM     = 0x04, // 环境温度 (s16: 放大10倍)
    PID_FAN_SPEED     = 0x05, // 风速 (u8: 0自动 1低 2中 3高)
    PID_LOCK          = 0x06, // 锁定状态 (u8)
    PID_ERROR         = 0x07, // 故障码 (u16)
    
    PID_LORA_CFG      = 0x10,
    PID_IR_CFG        = 0x11,
    PID_DEV_INFO      = 0x12,
    PID_SYS_PARAMS    = 0x13,
    PID_SYS_CTRL      = 0x14,
    PID_ALL_STATE     = 0xF0
} BT_PID_Type;

// 动作ID定义 (ActionID)
typedef enum {
    ACT_RESET         = 0x01, // 复位设备
    ACT_IR_MATCH      = 0x02, // 红外匹配
    ACT_IR_LEARN      = 0x03, // 红外学习
    ACT_IR_SEND       = 0x04, // 红外透传发送
    ACT_SAVE_PARAMS   = 0x05, // 保存参数到Flash
} BT_Action_Type;

/*********************************************************************
 * MACROS
 */
typedef struct
{
    uint16_t connHandle; // Connection handle of current connection
    uint16_t connInterval;
    uint16_t connSlaveLatency;
    uint16_t connTimeout;
} peripheralConnItem_t;

/*********************************************************************
 * FUNCTIONS
 */

/*
 * Task Initialization for the BLE Application
 */
extern void Peripheral_Init(void);

/*
 * Task Event Processor for the BLE Application
 */
extern uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events);

/*********************************************************************
*********************************************************************/
extern void peripheralCharNotify(uint8_t charIndex, uint8_t *pValue, uint16_t len);
#ifdef __cplusplus
}
#endif

#endif
