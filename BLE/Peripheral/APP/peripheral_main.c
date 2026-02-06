/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2020/08/06
 * Description        : ????????????????????????????
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* ???????? */
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "b_cdc.h"
#include "lora.h"
#include "board.h"
#include "timer.h"
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
 * @brief   ?????
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
        // if(count++ % 100000 == 0){
        //     snprintf((char*)TxBuff,sizeof(TxBuff),"this is lora %d\r\n",count++);
        //     Lora_Tx(TxBuff,20,1000);
            
        // }
        Period_100ms();
        Period_1s();
        TMOS_SystemProcess();
    }
}
uint8_t TestBuf[1024];
/*********************************************************************
 * @fn      main
 *
 * @brief   ??????
 *
 * @return  none
 */
int main(void)
{
    uint16_t i;
    uint8_t  s;
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    //timer0 init
    TMR0_TimerInit(FREQ_SYS / 100);         // 设置定时时间 10ms
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);        //enable peripheral interrupt
    PFIC_EnableIRQ(TMR0_IRQn);                    //enable timer0 core interrupt
    //wwdg init
    WWDG_Init();
    Led_Init();
    RTC_SetTimestamp(1767240000);
    //debug init
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    int arc_num = sizeof(g_arc_info)/sizeof(t_arc);
    const t_arc *i_ptr = g_arc_info;
    for(i = 0;i<arc_num;i++){
        PRINT("name[%s],ir_num = %d\r\n",i_ptr[i].name,i_ptr[i].num);
    }
    #if 0
    PRINT("Lora Initing ...\n");
    if(Lora_Init(420.1f,20,7,4) != 0){
        PRINT("Lora Init Failed! Halting.\n");
    }else{
        PRINT("Lora Init OK.\n");
    }
#endif 

    #if 0 // ??дData-Flash

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

    //lora test


    Main_Circulation();
}

/******************************** endfile @ main ******************************/
