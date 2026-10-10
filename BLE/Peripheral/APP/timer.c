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
#include "timer.h"
#include "config_store.h"
#include "ml307r.h"
#include "ota_update.h"
static volatile uint8_t Flag_20ms = 0;
static volatile uint8_t Flag_100ms = 0;
static volatile uint8_t Flag_1s = 0;
volatile uint32_t CurTick = 0;  //??tick ,??10ms
static uint8_t rtcTimeValid = 0;
static uint8_t rtcPolled = 0;
static uint32_t rtcPollTick, rtcChangeTick;
#define RTC_CE_PIN   GPIO_Pin_2
#define RTC_SCLK_PIN GPIO_Pin_18
#define RTC_IO_PIN   GPIO_Pin_19

/* DS1302 three-wire, LSB first. CE is idle low, not a reset pulse.
 * Release I/O before the command's last falling edge: DS1302 drives D0 there.
 * One burst takes about 220 us; never disable BLE interrupts for this bus. */
static void rtc_transfer(uint8_t command, uint8_t *data, uint8_t length)
{
    uint8_t i, bit, value;
    GPIOB_ResetBits(RTC_CE_PIN | RTC_SCLK_PIN | RTC_IO_PIN);
    GPIOB_ModeCfg(RTC_IO_PIN, GPIO_ModeOut_PP_5mA);
    mDelayuS(1);
    GPIOB_SetBits(RTC_CE_PIN);
    mDelayuS(4);
    for(bit = 0; bit < 8U; bit++) {
        if(command & (1U << bit)) GPIOB_SetBits(RTC_IO_PIN);
        else GPIOB_ResetBits(RTC_IO_PIN);
        mDelayuS(1);
        GPIOB_SetBits(RTC_SCLK_PIN);
        mDelayuS(1);
        if(bit == 7U && (command & 1U)) GPIOB_ModeCfg(RTC_IO_PIN, GPIO_ModeIN_PD);
        GPIOB_ResetBits(RTC_SCLK_PIN);
        mDelayuS(1);
    }
    for(i = 0; i < length; i++) {
        value = (command & 1U) ? 0U : data[i];
        for(bit = 0; bit < 8U; bit++) {
            if(command & 1U) {
                if(GPIOB_ReadPortPin(RTC_IO_PIN)) value |= (uint8_t)(1U << bit);
            } else {
                if(value & (1U << bit)) GPIOB_SetBits(RTC_IO_PIN);
                else GPIOB_ResetBits(RTC_IO_PIN);
            }
            mDelayuS(1);
            GPIOB_SetBits(RTC_SCLK_PIN);
            mDelayuS(1);
            GPIOB_ResetBits(RTC_SCLK_PIN);
            mDelayuS(1);
        }
        if(command & 1U) data[i] = value;
    }
    GPIOB_ResetBits(RTC_CE_PIN);
    mDelayuS(4);
    GPIOB_ModeCfg(RTC_IO_PIN, GPIO_ModeIN_PD);
}

static uint8_t rtc_bcd(uint8_t value)
{
    if((value & 15U) > 9U || (value >> 4) > 9U) return 0xffU;
    return (uint8_t)((value >> 4) * 10U + (value & 15U));
}

static uint8_t rtc_read_timestamp(uint32_t *timestamp)
{
    uint8_t data[8];
    time_fields_t fields;
    rtc_transfer(0xbfU, data, sizeof(data));
    /* WP readback distinguishes a disconnected bus from an unset calendar. */
    if(data[7] != 0x80U) { Dev.errorCode.bit.rtc = 1U; return 0U; }
    if(data[0] & 0x80U) return 0U; /* oscillator halted: needs synchronisation */
    fields.second = rtc_bcd(data[0]);
    fields.minute = rtc_bcd(data[1]);
    fields.hour = rtc_bcd(data[2] & 0x3fU);
    if(data[2] & 0x40U) return 0U;
    if(data[2] & 0x80U) {
        fields.hour = rtc_bcd(data[2] & 0x1fU);
        if(fields.hour < 1U || fields.hour > 12U) return 0U;
        fields.hour = fields.hour % 12U + ((data[2] & 0x20U) ? 12U : 0U);
    }
    fields.day = rtc_bcd(data[3]);
    fields.month = rtc_bcd(data[4]);
    fields.year = 2000U + rtc_bcd(data[6]);
    return data[5] >= 1U && data[5] <= 7U &&
           TimeUtil_ToUnix(&fields, timestamp) && *timestamp >= 1672531200U;
}
//主频60M，看门狗超时复位最长时间为 131072/60000000*255=0.557056s?
void WWDG_Init(void){
    WWDG_SetCounter(0);//喂狗
    WWDG_ClearFlag();//清除标志位
    WWDG_ResetCfg(ENABLE);//使能看门狗复位
}

void WWDG_Refresh(void){
    WWDG_SetCounter(0);//喂狗
}

/* Only the external calendar is adjusted. Internal RTC belongs to BLE/TMOS. */
uint8_t RTC_SetTimestamp(uint32_t timestamp)
{
    time_fields_t fields;
    uint32_t verified;
    uint8_t data[8], control = 0U;
    uint8_t values[7], i;
    if(timestamp < 1672531200U || !TimeUtil_FromUnix(timestamp, &fields)) return 0U;
    values[0] = fields.second; values[1] = fields.minute; values[2] = fields.hour;
    values[3] = fields.day; values[4] = fields.month;
    values[5] = (uint8_t)((timestamp / 86400U + 4U) % 7U + 1U);
    values[6] = (uint8_t)(fields.year - 2000U);
    for(i = 0U; i < 7U; i++) data[i] = (values[i] / 10U << 4) | (values[i] % 10U);
    data[7] = 0x80U; /* restore write protection in the same clock burst */
    rtc_transfer(0x8eU, &control, 1U);
    rtc_transfer(0x90U, &control, 1U); /* never charge a primary backup battery */
    rtc_transfer(0xbeU, data, sizeof(data));
    rtcTimeValid = rtc_read_timestamp(&verified) && verified >= timestamp && verified <= timestamp + 1U;
    Dev.errorCode.bit.rtc = !rtcTimeValid;
    if(!rtcTimeValid) { PRINT("External RTC write/readback failed\r\n"); return 0U; }
    LocalTimestamp = verified;
    rtcPolled = 1U;
    rtcPollTick = rtcChangeTick = CurTick;
    PRINT("External RTC synced: %lu\r\n", (unsigned long)verified);
    return 1U;
}

void RTC_ProductInit(void)
{
    uint8_t control = 0U;
    GPIOB_ResetBits(RTC_CE_PIN | RTC_SCLK_PIN);
    GPIOB_ModeCfg(RTC_CE_PIN | RTC_SCLK_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(RTC_IO_PIN, GPIO_ModeIN_PD);
    rtcTimeValid = rtcPolled = 0U;
    LocalTimestamp = 0U;
    rtcChangeTick = CurTick;
    Dev.errorCode.bit.rtc = 0U;
    rtc_transfer(0x8eU, &control, 1U);
    rtc_transfer(0x90U, &control, 1U);
    control = 0x80U;
    rtc_transfer(0x8eU, &control, 1U);
    Rtc_GetTimestamp();
    PRINT("External RTC PB2/PB18/PB19: valid=%u fault=%u time=%lu\r\n",
          rtcTimeValid, Dev.errorCode.bit.rtc, (unsigned long)LocalTimestamp);
}

uint8_t RTC_IsTimeValid(void)
{
    return rtcTimeValid;
}

/* Network clocks may carry a wrong timezone or stale calendar. Keep a valid
 * battery-backed clock authoritative; explicit BLE synchronisation can still
 * correct any date via RTC_SetTimestamp. All timestamps remain Unix UTC. */
uint8_t RTC_SyncTimestamp(uint32_t timestamp)
{
    uint32_t current = Rtc_GetTimestamp();
    uint32_t difference;
    if(timestamp < 1672531200U || timestamp > 2147483000U) return 0U;
    if(rtcTimeValid) {
        difference = timestamp > current ? timestamp - current : current - timestamp;
        if(difference > 300U) {
            PRINT("Network RTC sync rejected: current=%lu received=%lu\r\n",
                  (unsigned long)current, (unsigned long)timestamp);
            return 0U;
        }
        if(difference <= 3U) return 1U;
    }
    return RTC_SetTimestamp(timestamp);
}

//获取RTC时间戳
uint32_t Rtc_GetTimestamp(void){
    uint32_t timestamp;
    if(rtcPolled && (uint32_t)(CurTick - rtcPollTick) < 1000U) return LocalTimestamp;
    rtcPolled = 1U;
    rtcPollTick = CurTick;
    if(!rtc_read_timestamp(&timestamp)) {
        if(rtcTimeValid) Dev.errorCode.bit.rtc = 1U;
        rtcTimeValid = 0U;
        return LocalTimestamp;
    }
    if(timestamp != LocalTimestamp) rtcChangeTick = CurTick;
    if((uint32_t)(CurTick - rtcChangeTick) >= 3000U) {
        Dev.errorCode.bit.rtc = 1U; /* plausible calendar but oscillator not ticking */
        rtcTimeValid = 0U;
        return LocalTimestamp;
    }
    LocalTimestamp = timestamp;
    rtcTimeValid = 1U;
    Dev.errorCode.bit.rtc = 0U;
    return timestamp;
}

uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *mon, uint16_t *day,
                        uint16_t *hour, uint16_t *min, uint16_t *sec)
{
    time_fields_t fields;
    uint32_t timestamp = Rtc_GetTimestamp();
    /* External RTC keeps UTC; weekly policies use site time (UTC+8). */
    if(!rtcTimeValid || timestamp > 2147483000UL - 8UL * 3600UL ||
       !TimeUtil_FromUnix(timestamp + 8UL * 3600UL, &fields)) return 0;
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
         * 每分钟输出一条机器可解析的健康心跳
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
                PRINT("#HEALTH up=%lu rev=%lu reset=%u fault=%04x lora=%u loraTick=%lu cell=%u/%u cellFail=%u irQ=%u/%u meterErr=%u meterFail=%u/%u/%02x sensor=%02x shtErr=%u\r\n",
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
                      meter->last_error_register,
                      ADC_GetSensorStatus(),
                      ADC_GetSht40Errors());
            }
        }
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
