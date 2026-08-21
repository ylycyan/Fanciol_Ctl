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
#include "health.h"
#include "hlw8110.h"
#include "config_store.h"
#include "ml307r.h"
#include "ota_update.h"
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
    uint8_t ml307Initialized = 0U;
    while(1)
    {
        /* BLE owns the tightest deadline.  Give TMOS a scheduling point before
         * and between peripheral jobs so LoRa/Flash/4G cannot starve it. */
        TMOS_SystemProcess();
        Health_Mark(HEALTH_BLE_STACK);
        Period_20ms();
        TMOS_SystemProcess();
        Period_100ms();
        TMOS_SystemProcess();
        Period_1s();
        /*
         * HEAD 已验证的 BLE/TMOS 启动路径必须先获得调度。新增的蜂窝硬件
         * 只能在协议栈稳定运行后初始化；仅 LoRa 配置则不会触碰 UART1/PB14。
         */
        if(!ml307Initialized && CurTick >= 1000U) {
            Ml307_Init();
            ml307Initialized = 1U;
        }
        if(ml307Initialized) Ml307_Process();
        else Health_Mark(HEALTH_CELLULAR);
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
#ifdef DEBUG
    GPIOA_SetBits(bTXD2);
    GPIOA_ModeCfg(bRXD2, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD2, GPIO_ModeOut_PP_5mA);
    UART2_DefInit();
#endif
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
    // UART1 专用于 ML307R；调试构建将日志输出到 UART2 PA6/PA7。
#ifdef DEBUG
    GPIOA_SetBits(bTXD2);
    GPIOA_ModeCfg(bRXD2, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD2, GPIO_ModeOut_PP_5mA);
    UART2_DefInit();
#endif
    // InitUSBDevice(); //usb-cdc 已弃用，不再初始化
    PRINT("%s ,build in(%s:%s)\n", VER_LIB,__DATE__,__TIME__);
    PRINT("BLE cfg: heap=%u packet=%u count=%u links=%u/%u\r\n",
          BLE_MEMHEAP_SIZE, BLE_BUFF_MAX_LEN, BLE_BUFF_NUM,
          PERIPHERAL_MAX_CONNECTION, CENTRAL_MAX_CONNECTION);
    CH58X_BLEInit();
    HAL_Init();
    PRINT("BLE RTC clock: internal 32K RC\r\n");
    RTC_ProductInit(retainedResetReason, retainedTimestamp);
    LocalTimestamp = Rtc_GetTimestamp();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    LoadDevInfo();
    Ota_Init();
    Peripheral_RefreshDeviceName();
    ADC_Init();
    HLW8110_Init();
    /*
     * 复位状态必须使用时钟初始化后的第一份快照。BLE/HAL 初始化可能读取
     * 或清理相关寄存器，不能在健康模块内延迟重新读取。
     */
    Health_Init(retainedResetReason, Dev.errorCode.u16Val);
    /*
     * 单槽自愈或双槽损坏后成功重建属于“已恢复历史事件”：
     * 先写入启动健康快照，再清除当前故障；仍退化/读失败则保持告警。
     */
    if((Storage_GetStartupFlags() & STORAGE_STARTUP_RECOVERED) != 0U &&
       (Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED) == 0U) {
        Dev.errorCode.bit.flash = 0;
    }
    WWDG_Init();
    PRINT("IR catalog brands: %u\r\n", IR_BRAND_COUNT);
    //lora test    
    Main_Circulation();
}
#endif

/******************************** endfile @ main ******************************/
