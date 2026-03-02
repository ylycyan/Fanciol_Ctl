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
t_dev Dev;
uint32_t LocalTimestamp;

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
    while(1)
    {   
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
    TMR0_TimerInit(FREQ_SYS / 100);         // TIM0 ?10msùùùù???
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);        //enable peripheral interrupt
    PFIC_EnableIRQ(TMR0_IRQn);                    //enable timer0 core interrupt
    //wwdg init
    WWDG_Init();
    Led_Init();
    IR_Init();
    //debug init
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    #if 1
    PRINT("Lora Initing ...\n");
    if(Lora_Init(421.34f,20,8,0xa) != 0){
        PRINT("Lora Init Failed! Halting.\n");
    }else{
        PRINT("Lora Init OK.\n");
    }
#endif 

    #if 0 // Data-Flash ??

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

    // InitUSBDevice(); //usb-cdc¥Æø⁄µ˜ ‘ 
    PRINT("%s ,build in(%s:%s)\n", VER_LIB,__DATE__,__TIME__);
    CH58X_BLEInit();
    HAL_Init();
    //lse test
    sys_safe_access_enable();
    R8_CK32K_CONFIG |= RB_CLK_OSC32K_XT | RB_CLK_INT32K_PON | RB_CLK_XT32K_PON;
    sys_safe_access_disable();
    RTC_SetTimestamp(1767240000);
    LoadDevInfo();
    

    int arc_num = sizeof(g_arc_info)/sizeof(t_arc);
    const t_arc *i_ptr = g_arc_info;
    for(i = 0;i<arc_num;i++){
        PRINT("name[%s],ir_num = %d\r\n",i_ptr[i].name,i_ptr[i].num);
    }
    //lora test    
    Main_Circulation();
}

/******************************** endfile @ main ******************************/
