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
#include "lora.h"
#include "board.h"
#include "timer.h"
#include "health_v2.h"
#include "hlw8110.h"
#include "config_store_v2.h"
/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
t_dev Dev;
uint32_t LocalTimestamp;

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
        Period_20ms();
        Period_100ms();
        Period_1s();
        TMOS_SystemProcess();
        HealthV2_Mark(HEALTH_V2_BLE_STACK);
    }
}
/*********************************************************************
 * @fn      main
 *
 * @brief   ??????
 *
 * @return  none
 */
#if defined(BLE_BASELINE_DIAGNOSTIC)
int main(void)
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    PRINT("BLE baseline %s ,build in(%s:%s)\r\n", VER_LIB, __DATE__, __TIME__);

    CH58X_BLEInit();
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();

    while(1)
    {
        TMOS_SystemProcess();
    }
}
#else
int main(void)
{
    uint8_t retainedResetReason;
    uint32_t retainedTimestamp;
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    retainedResetReason = (uint8_t)SYS_GetLastResetSta();
    retainedTimestamp = Rtc_GetTimestamp();
    //timer0 init
    TMR0_TimerInit(FREQ_SYS / 100);         // TIM0 ?10ms???????
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);        //enable peripheral interrupt
    PFIC_EnableIRQ(TMR0_IRQn);                    //enable timer0 core interrupt
    Led_Init();
    IR_Init();
    //debug init
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    // InitUSBDevice(); //usb-cdc 已弃用，不再初始化
    PRINT("%s ,build in(%s:%s)\n", VER_LIB,__DATE__,__TIME__);
    CH58X_BLEInit();
    HAL_Init();
    PRINT("BLE RTC clock: internal 32K RC\r\n");
    RTC_ProductInit(retainedResetReason, retainedTimestamp);
    LocalTimestamp = Rtc_GetTimestamp();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    LoadDevInfo();
    ADC_Init();
    HLW8110_Init();
    /*
     * 复位状态必须使用时钟初始化后的第一份快照。BLE/HAL 初始化可能读取
     * 或清理相关寄存器，不能在健康模块内延迟重新读取。
     */
    HealthV2_Init(retainedResetReason, Dev.errorCode.u16Val);
    /*
     * 单槽自愈或双槽损坏后成功重建属于“已恢复历史事件”：
     * 先写入启动健康快照，再清除当前故障；仍退化/读失败则保持告警。
     */
    if((StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_RECOVERED) != 0U &&
       (StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_DEGRADED) == 0U) {
        Dev.errorCode.bit.flash = 0;
    }
    WWDG_Init();
    PRINT("IR catalog brands: %u\r\n", IR_BRAND_COUNT);
    //lora test    
    Main_Circulation();
}
#endif

/******************************** endfile @ main ******************************/
