#include "HAL.h"
#include "board.h"
#include "lora.h"
#include "include/flash.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "health.h"
#include "device_service.h"
#include "hlw8110.h"
#include "time_utils.h"
#include "config_store.h"
#include "ml307r.h"
#include "ota_update.h"
static volatile uint8_t Flag_20ms = 0;
static volatile uint8_t Flag_100ms = 0;
static volatile uint8_t Flag_1s = 0;
volatile uint32_t CurTick = 0;  //??tick ,??10ms
static uint8_t rtcTimeValid = 0;
static int32_t rtcUnixOffset = 0;

static uint8_t rtc_read_hardware_timestamp(uint32_t *timestamp)
{
    time_fields_t fields;
    uint16_t year, mon, day, hour, min, sec;

    if(timestamp == 0) return 0;
    RTC_GetTime(&year, &mon, &day, &hour, &min, &sec);
    fields.year = year;
    fields.month = (uint8_t)mon;
    fields.day = (uint8_t)day;
    fields.hour = (uint8_t)hour;
    fields.minute = (uint8_t)min;
    fields.second = (uint8_t)sec;
    return TimeUtil_ToUnix(&fields, timestamp);
}
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
    time_fields_t fields;
    uint32_t hardwareTimestamp;
    uint32_t delta;
    if ((timestamp < 1672531200u) || (timestamp > 2147483000u)) { //2023-01-01 00:00:00 ~ 2038-01-19 11:03:20
        PRINT("RTC_SetTimestamp: invalid timestamp %lu\r\n", timestamp);
        return;
    }
    if(!TimeUtil_FromUnix(timestamp, &fields)) {
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
    if(!rtc_read_hardware_timestamp(&hardwareTimestamp)) {
        PRINT("RTC_SetTimestamp: hardware time invalid\r\n");
        return;
    }

    /*
     * BLE/TMOS uses the hardware RTC counter as its scheduler time base.
     * Keep that counter monotonic and represent wall-clock synchronisation as
     * a software offset. Reinitialising either RTC or BLE here breaks active
     * connections and duplicates protocol-stack tasks.
     */
    if(timestamp >= hardwareTimestamp) {
        delta = timestamp - hardwareTimestamp;
        if(delta > 0x7fffffffUL) {
            PRINT("RTC_SetTimestamp: offset out of range\r\n");
            return;
        }
        rtcUnixOffset = (int32_t)delta;
    } else {
        delta = hardwareTimestamp - timestamp;
        if(delta > 0x7fffffffUL) {
            PRINT("RTC_SetTimestamp: offset out of range\r\n");
            return;
        }
        rtcUnixOffset = -(int32_t)delta;
    }
    LocalTimestamp = timestamp;
    rtcTimeValid = 1;
    PRINT("RTC software offset=%ld\r\n", (long)rtcUnixOffset);
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

    /* 真正掉电后没有可信时钟，先给 RTC 安全基准，等待 LoRa 或 4G 对时。 */
    RTC_SetTimestamp(1767225600u); /* 2026-01-01 00:00:00 */
    rtcTimeValid = 0;
    PRINT("RTC waiting for remote time sync\r\n");
}

uint8_t RTC_IsTimeValid(void)
{
    return rtcTimeValid;
}

//???????
uint32_t Rtc_GetTimestamp(void){
    uint32_t hardwareTimestamp;
    uint32_t magnitude;

    if(!rtc_read_hardware_timestamp(&hardwareTimestamp)) return LocalTimestamp;
    if(rtcUnixOffset >= 0) {
        magnitude = (uint32_t)rtcUnixOffset;
        if(hardwareTimestamp > (0xffffffffUL - magnitude)) return LocalTimestamp;
        return hardwareTimestamp + magnitude;
    }
    magnitude = (uint32_t)(-rtcUnixOffset);
    if(hardwareTimestamp < magnitude) return LocalTimestamp;
    return hardwareTimestamp - magnitude;
}

uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *mon, uint16_t *day,
                        uint16_t *hour, uint16_t *min, uint16_t *sec)
{
    time_fields_t fields;
    if(!TimeUtil_FromUnix(Rtc_GetTimestamp(), &fields)) return 0;
    if(year) *year = fields.year;
    if(mon) *mon = fields.month;
    if(day) *day = fields.day;
    if(hour) *hour = fields.hour;
    if(min) *min = fields.minute;
    if(sec) *sec = fields.second;
    return 1;
}

//20ms????,????????LoRa
void Period_20ms(void){
    if(Flag_20ms){
        Flag_20ms = 0;
        if(Connectivity_LoraEnabled()) Lora_Pro();
        else {
            Dev.errorCode.bit.lora = 0U;
            Dev.loraStatus = Status_Logining;
        }
        HLW8110_Poll();
    }
}

//100ms????,????????
void Period_100ms(void){
    if(Flag_100ms){
        //100ms????
        Flag_100ms = 0;
        Check_IrBuf();
        Ir_Pro();
        WWDG_Refresh();
        LED_Pro();
        // LED_GREEN(LocalTimestamp % 2);
        if(DeviceService_IdentifyActive()){
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
        ADC_Pro();
        Rule_Pro();       //规则引擎: 每秒评估一次触发条件
        Meter_Update(1);  //计量更新: 汇总 HLW8110 累计电量
        /*
         * 每分钟输出一条机器可解析的健康心跳，供 7 天实验室工具判断
         * 重启、配置漂移、队列滞留、控制成功率和外设恢复情况。
         * 单行输出不会进入网关协议，也不增加 Flash 擦写。
         */
#ifdef DEBUG
        {
            static uint8_t health_log_seconds = 0U;
            if(++health_log_seconds >= 60U) {
                const HLW8110_Status_t *meter = HLW8110_GetStatus();
                health_log_seconds = 0U;
                const ml307_status_t *cellular = Ml307_GetStatus();
                PRINT("#HEALTH up=%lu rev=%lu reset=%u fault=%04x lora=%u loraTick=%lu cell=%u/%u cellFail=%u irQ=%u/%u meterErr=%u meterFail=%u/%u/%02x\r\n",
                      (unsigned long)(CurTick / 1000U),
                      (unsigned long)Config_GetRevision(),
                      Health_ConsecutiveResets(),
                      Dev.errorCode.u16Val,
                      Dev.loraStatus,
                      (unsigned long)Timer_Lora,
                      cellular->phase,
                      cellular->mqtt_online,
                      cellular->consecutive_failures,
                      Ir_GetQueueDepth(),
                      Ir_GetQueueHighWater(),
                      meter->communication_errors,
                      meter->last_error_reason,
                      meter->last_error_state,
                      meter->last_error_register);
            }
        }
#endif

        // 每天00:00重置规则的executed标志
        {
            static uint8_t last_day = 0;
            uint16_t y, m, d, h, mi, s;
            if (!RTC_IsTimeValid() ||
                !RTC_GetWallTime(&y, &m, &d, &h, &mi, &s)) {
                last_day = 0;
            } else if (last_day == 0) {
                last_day = d;
            } else if (d != last_day) {
                Rule_DailyReset();
                last_day = d;
            }
        }
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
