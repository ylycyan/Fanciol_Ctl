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

#if defined(DEBUG) && DEBUG == Debug_UART1
volatile uint8_t DebugUartOutputMuted = 0U;
int __real__write(int fd, char *buf, int size);
int __wrap__write(int fd, char *buf, int size)
{
    if(DebugUartOutputMuted) return size;
    return __real__write(fd, buf, size);
}
#endif

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
        Period_20ms();
        TMOS_SystemProcess();
        Period_100ms();
        TMOS_SystemProcess();
        Period_1s();
        /*
         * HEAD 已验证的 BLE/TMOS 启动路径必须先获得调度。新增的蜂窝硬件
         * 只能在协议栈稳定运行后初始化；PB5 在此时按通信配置控制 4G 电源。
         */
        if(!ml307Initialized && CurTick >= 1000U) {
            Ml307_Init();
            ml307Initialized = 1U;
        }
        if(ml307Initialized) Ml307_Process();
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
    Ml307_EarlyPowerOff();
#ifdef DEBUG
#if DEBUG == Debug_UART1
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bRXD1, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
#else
    GPIOA_SetBits(bTXD2);
    GPIOA_ModeCfg(bRXD2, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD2, GPIO_ModeOut_PP_5mA);
    UART2_DefInit();
#endif
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
    Ml307_EarlyPowerOff();
    retainedResetReason = (uint8_t)SYS_GetLastResetSta();
    retainedTimestamp = Rtc_GetTimestamp();
    //timer0 init
    TMR0_TimerInit(FREQ_SYS / 100);         // TIM0 ?10ms???????
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);        //enable peripheral interrupt
    PFIC_EnableIRQ(TMR0_IRQn);                    //enable timer0 core interrupt
    Led_Init();
    IR_Init();
    // 调试串口由 DEBUG 选择；4G接管UART1前会先静音调试输出。
#ifdef DEBUG
#if DEBUG == Debug_UART1
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bRXD1, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
#else
    GPIOA_SetBits(bTXD2);
    GPIOA_ModeCfg(bRXD2, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD2, GPIO_ModeOut_PP_5mA);
    UART2_DefInit();
#endif
#endif
    PRINT("%s ,build in(%s:%s)\n", VER_LIB,__DATE__,__TIME__);
    PRINT("BLE cfg: heap=%u packet=%u count=%u links=%u/%u\r\n",
          BLE_MEMHEAP_SIZE, BLE_BUFF_MAX_LEN, BLE_BUFF_NUM,
          PERIPHERAL_MAX_CONNECTION, CENTRAL_MAX_CONNECTION);
    CH58X_BLEInit();
    HAL_Init();
#if(CLK_OSC32K == 0)
    PRINT("BLE RTC clock: external 32.768 kHz LSE\r\n");
#elif(CLK_OSC32K == 1)
    PRINT("BLE RTC clock: internal 32 kHz RC\r\n");
#else
    PRINT("BLE RTC clock: internal 32.768 kHz RC\r\n");
#endif
    RTC_ProductInit(retainedResetReason, retainedTimestamp);
    LocalTimestamp = Rtc_GetTimestamp();
    /*
     * DataFlash contains the production device ID used as the complete BLE
     * name.  Load it before the GAP role builds its first advertising packet;
     * updating advertising data after advertising has started is not reliable
     * on every phone/controller combination and can leave the cached SplitAC
     * fallback visible until the next power cycle.
     */
    LoadDevInfo();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    Ota_Init();
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
