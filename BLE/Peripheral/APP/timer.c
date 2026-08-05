#include "HAL.h"
#include "board.h"
#include "lora.h"
#include "include/flash.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "health_v2.h"
#include "splitac_service_v2.h"
#include "hlw8110.h"
#include "time_v2.h"
#include "config_store_v2.h"
static volatile uint8_t Flag_20ms = 0;
static volatile uint8_t Flag_100ms = 0;
static volatile uint8_t Flag_1s = 0;
volatile uint32_t CurTick = 0;  //??tick ,??10ms
static uint8_t rtcTimeValid = 0;
//??60M????????????? 131072/60000000*255=0.557056s?
void WWDG_Init(void){
    WWDG_SetCounter(0);//??
    WWDG_ClearFlag();//??????
    WWDG_ResetCfg(ENABLE);//???????
}

void WWDG_Refresh(void){
    WWDG_SetCounter(0);//??
}

// ***??!!! ???????????,????tmos?????RTC_InitTime()??????. 
void RTC_SetTimestamp(uint32_t timestamp)
{
    time_v2_fields_t fields;
    if ((timestamp < 1672531200u) || (timestamp > 2147483000u)) { //2023-01-01 00:00:00 ~ 2038-01-19 11:03:20
        PRINT("RTC_SetTimestamp: invalid timestamp %lu\r\n", timestamp);
        return;
    }
    if(!TimeV2_FromUnix(timestamp, &fields)) {
        PRINT("RTC_SetTimestamp: conversion failed %lu\r\n", timestamp);
        return;
    }

    //lse ?? ?HAL_TimeInit()??,???????,lse???????
    // LClk32K_Select(Clk32K_LSE);
    // R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    // R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    // R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;
    // R8_SAFE_ACCESS_SIG = 0;
    PRINT("set ts:%lu -> %04u-%02u-%02u %02u:%02u:%02u\r\n",
          timestamp, fields.year, fields.month, fields.day,
          fields.hour, fields.minute, fields.second);

    // 这里只校准 RTC。BLE/TMOS 只能在启动时初始化一次，运行中对时不得重置协议栈。
    sys_safe_access_enable();
    R8_CK32K_CONFIG |= RB_CLK_OSC32K_XT | RB_CLK_INT32K_PON | RB_CLK_XT32K_PON;
    sys_safe_access_disable();
    RTC_InitTime(fields.year, fields.month, fields.day,
                 fields.hour, fields.minute, fields.second);
    rtcTimeValid = 1;

}

void RTC_ProductInit(uint8_t resetReason, uint32_t retainedTimestamp)
{
    if(resetReason != RST_STATUS_RPOR &&
       retainedTimestamp >= 1672531200u && retainedTimestamp <= 2147483000u) {
        RTC_SetTimestamp(retainedTimestamp);
        rtcTimeValid = 1;
        PRINT("RTC retained after reset: %lu\r\n", retainedTimestamp);
        return;
    }

    /* 真正掉电后没有可信时钟，先给 RTC 安全基准，但禁止定时规则直到网关对时。 */
    RTC_SetTimestamp(1767225600u); /* 2026-01-01 00:00:00 */
    rtcTimeValid = 0;
    PRINT("RTC waiting for gateway time sync\r\n");
}

uint8_t RTC_IsTimeValid(void)
{
    return rtcTimeValid;
}

//???????
uint32_t Rtc_GetTimestamp(void){
    time_v2_fields_t fields;
    uint32_t timestamp;
    uint16_t year, mon, day, hour, min, sec;
    RTC_GetTime(&year, &mon, &day, &hour, &min, &sec);
    fields.year = year;
    fields.month = (uint8_t)mon;
    fields.day = (uint8_t)day;
    fields.hour = (uint8_t)hour;
    fields.minute = (uint8_t)min;
    fields.second = (uint8_t)sec;
    if(!TimeV2_ToUnix(&fields, &timestamp)) return LocalTimestamp;
    return timestamp;
}

//20ms????,????????LoRa
void Period_20ms(void){
    if(Flag_20ms){
        Flag_20ms = 0;
        Lora_Pro();
        HLW8110_Poll();
        HealthV2_Mark(HEALTH_V2_LORA);
    }
}

//100ms????,????????
void Period_100ms(void){
    if(Flag_100ms){
        //100ms????
        Flag_100ms = 0;
        Check_IrBuf();
        Ir_Pro();
        HealthV2_Mark(HEALTH_V2_IR);
        if(HealthV2_Tick100ms(Dev.errorCode.u16Val)) WWDG_Refresh();
        LED_Pro();
        // LED_GREEN(LocalTimestamp % 2);
        if(SplitAcV2_IdentifyActive()){
            LED_GREEN_BLINK(FALSE, 0);
            LED_BLUE_BLINK(FALSE, 0);
            LED_WHITE_BLINK(TRUE, 100);
        }else{
            LED_GREEN_BLINK(TRUE, 1000);
            LED_BLUE_BLINK(TRUE, 500);
            LED_WHITE_BLINK(TRUE, 200);
        }
        if((Dev.loraStatus >= 4) && (Timer_Lora < LORA_SEC_TO_TICKS(Dev.scanCycle))){
            LED_RED_BLINK(TRUE,300);
        }else{
            LED_RED_BLINK(TRUE,3000);
        }
    }
}

//1s????,????????
void Period_1s(void){
    if(Flag_1s){
        //1s????
        Flag_1s = 0;

        /* RTC 只有秒级精度，每秒换算一次即可，避免在 60 MHz MCU 上每 100 ms 调用 mktime。 */
        LocalTimestamp = Rtc_GetTimestamp();
        Flash_Poll();
        HealthV2_Mark(HEALTH_V2_FLASH | HEALTH_V2_PERIODIC);
        ADC_Pro();
        Rule_Pro();       //规则引擎: 每秒评估一次触发条件
        Meter_Update(1);  //计量更新: 累计运行时间和电量

        /*
         * 每分钟输出一条机器可解析的健康心跳，供 7 天实验室工具判断
         * 重启、配置漂移、队列滞留、控制成功率和外设恢复情况。
         * 单行输出不会进入网关协议，也不增加 Flash 擦写。
         */
        {
            static uint8_t health_log_seconds = 0U;
            if(++health_log_seconds >= 60U) {
                const HLW8110_Status_t *meter = HLW8110_GetStatus();
                health_log_seconds = 0U;
                PRINT("#HEALTH up=%lu rev=%lu reset=%u fault=%04x lora=%u loraTick=%lu irQ=%u/%u irTx=%u/%u/%u meterErr=%u meterFail=%u/%u/%02x\r\n",
                      (unsigned long)(CurTick / 1000U),
                      (unsigned long)ConfigV2_GetRevision(),
                      HealthV2_ConsecutiveResets(),
                      Dev.errorCode.u16Val,
                      Dev.loraStatus,
                      (unsigned long)Timer_Lora,
                      Ir_GetQueueDepth(),
                      Ir_GetQueueHighWater(),
                      Ir_GetSubmittedCount(),
                      Ir_GetRepeatedCount(),
                      Ir_GetBusyRejectedCount(),
                      meter->communication_errors,
                      meter->last_error_reason,
                      meter->last_error_state,
                      meter->last_error_register);
            }
        }

        // 每天00:00重置规则的executed标志
        {
            static uint8_t last_day = 0;
            uint16_t y, m, d, h, mi, s;
            RTC_GetTime(&y, &m, &d, &h, &mi, &s);
            if (!RTC_IsTimeValid()) {
                last_day = 0;
            } else if (last_day == 0) {
                last_day = d;
            } else if (d != last_day) {
                Rule_DailyReset();
                last_day = d;
            }
        }

        #if 0 //test only
        // FREQ_SYS
        PRINT("#curtick:%d , @timestamp:%d\r\n",CurTick,LocalTimestamp);
        // Lora_Tx((uint8_t*)"123456789ABCD",10,1500);
        #endif 
    }
}

__INTERRUPT                                   //interrupt flag
__HIGH_CODE                                   //put in ram
void TMR0_IRQHandler(void)  {                 //timer0 ?10ms??
    static uint32_t tick = 0;
    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END)){    //check flag
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);  //clear flag
        tick++;
        /*
         * SysTick->CNT 在 60 MHz 下约 71.6 秒回卷，不能直接换算为毫秒时钟；
         * 否则健康监督器会在每次回卷时误判全部任务超时并触发看门狗复位。
         */
        CurTick += 10U;
        if(tick % 2 == 0){ //20ms
            Flag_20ms = 1;
        }
        if(tick % 10 == 0){ //100ms
            Flag_100ms = 1;
        }
        if(tick % 100 == 0){ //1s
            Flag_1s = 1;
        }
    }
}
