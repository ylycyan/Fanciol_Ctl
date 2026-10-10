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
#define SBP_DEVICE_RESET_EVT    0x0040
#define OTA_FLASH_VERIFY_EVT    0x0080
/* 非阻塞分片应答：每个 TMOS 事件只发一片，避免在 GATT 回调里忙等。 */
#define SBP_TX_FRAME_EVT        0x0100
/* 手机建立链路后必须在限定时间内完成一次有效协议请求，否则释放占用。 */
#define SBP_HANDSHAKE_TIMEOUT_EVT 0x0200

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
extern void Peripheral_RefreshDeviceName(void);
extern void Peripheral_RequestReset(void);

/*
 * Task Event Processor for the BLE Application
 */
extern uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events);

/*********************************************************************
*********************************************************************/
extern uint8_t peripheralCharNotify(uint8_t charIndex, uint8_t *pValue, uint16_t len);
#ifdef __cplusplus
}
#endif

#endif
