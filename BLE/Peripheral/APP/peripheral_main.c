/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2020/08/06
 * Description        : 外设从机应用主函数及任务系统初始化
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#include <stdint.h>
#include <string.h>
#include "b_cdc.h"
/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif
uint8_t TxBuff[100] = "This is uart3 test .\r\n";
/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   主循环
 *
 * @return  none
 */
__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
    static uint32_t count = 0;
    while(1)
    {   
        if(count++ % 10000 == 0){
            // snprintf((char *)TxBuff, sizeof(TxBuff), "This is uart3 test %d.\r\n", count++);
            // UART3_SendString(TxBuff, strlen((char *)TxBuff));
            // USB_Printf("USB CDC Debug Test %d\r\n", count);
        }
        TMOS_SystemProcess();
    }
}
uint8_t TestBuf[1024];
/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */
int main(void)
{
    uint16_t i;
    uint8_t  s;
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    GPIOA_SetBits(GPIO_Pin_5);
    GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);      // RXD-配置上拉输入
    GPIOA_ModeCfg(GPIO_Pin_5, GPIO_ModeOut_PP_5mA); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART3_DefInit();
    UART3_SendString("UART3 Test Start.\r\n", strlen("UART3 Test Start.\r\n"));

    #if 0 // 读写Data-Flash

    PRINT("EEPROM_READ...\n");
    EEPROM_READ(0, TestBuf, 500);
    for(i = 0; i < 500; i++)
    {
        PRINT("%02x ", TestBuf[i]);
    }
    PRINT("\n");

    s = EEPROM_ERASE(0, EEPROM_BLOCK_SIZE);
    PRINT("EEPROM_ERASE=%02x\n", s);
    PRINT("EEPROM_READ...\n");
    EEPROM_READ(0, TestBuf, 500);
    for(i = 0; i < 500; i++)
    {
        PRINT("%02x ", TestBuf[i]);
    }
    PRINT("\n");

    for(i = 0; i < 500; i++)
        TestBuf[i] = 0x0 + i;
    s = EEPROM_WRITE(0, TestBuf, 500);
    PRINT("EEPROM_WRITE=%02x\n", s);
    PRINT("EEPROM_READ...\n");
    EEPROM_READ(0, TestBuf, 500);
    for(i = 0; i < 500; i++)
    {
        PRINT("%02x ", TestBuf[i]);
    }
    PRINT("\n");

#endif

    // InitUSBDevice();

    PRINT("%s\n", VER_LIB);
    CH58X_BLEInit();
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    Main_Circulation();
}

/******************************** endfile @ main ******************************/
