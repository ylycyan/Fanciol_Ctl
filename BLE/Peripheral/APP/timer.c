#include "HAL.h"
#include <time.h>
#include "board.h"
#include "lora.h"
#include "include/flash.h"
static uint8_t Flag_100ms = 0;
static uint8_t Flag_1s = 0;

//主频60M，看门狗超时复位最长时间为 131072/60000000*255=0.557056s。
void WWDG_Init(void){
    WWDG_SetCounter(0);//喂狗
    WWDG_ClearFlag();//清除中断标志
    WWDG_ResetCfg(ENABLE);//使能看门狗复位
}

void WWDG_Refresh(void){
    WWDG_SetCounter(0);//喂狗
}

static uint8_t is_leap_year(uint16_t y){
    if((y % 4) != 0) return 0;
    if((y % 100) != 0) return 1;
    if((y % 400) != 0) return 0;
    return 1;
}

// ***慎用!!! 尽量在初始化程序中调用,若在沁恒tmos任务中调用RTC_InitTime()可能导致异常. 
void RTC_SetTimestamp(uint32_t timestamp)
{
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days;
    uint32_t sod;
    uint16_t year;
    uint16_t mon;
    uint16_t day;
    uint16_t hour;
    uint16_t min;
    uint16_t sec;
    if ((timestamp < 1672531200u) || (timestamp > 2147483000u)) { //2023-01-01 00:00:00 ~ 2038-01-19 11:03:20
        PRINT("RTC_SetTimestamp: invalid timestamp %u\r\n", timestamp);
        return;
    }
    days = timestamp / 86400u;
    sod = timestamp % 86400u;
    year = 1970;
    for(;;){
        uint16_t dy = is_leap_year(year) ? 366u : 365u;
        if(days >= dy){
            days -= dy;
            year++;
        }else{
            break;
        }
    }
    mon = 1;
    for(;;){
        uint8_t dim = mdays[mon - 1];
        if(mon == 2 && is_leap_year(year)){
            dim++;
        }
        if(days >= dim){
            days -= dim;
            mon++;
        }else{
            break;
        }
    }
    day = (uint16_t)(days + 1u);
    hour = (uint16_t)(sod / 3600u);
    sod %= 3600u;
    min = (uint16_t)(sod / 60u);
    sec = (uint16_t)(sod % 60u);

    //lse 使能 与HAL_TimeInit()冲突,会导致异常重启,lse是否工作待确认
    // LClk32K_Select(Clk32K_LSE);
    // R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    // R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    // R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;
    // R8_SAFE_ACCESS_SIG = 0;
    PRINT("set ts:%u -> %04u-%02u-%02u %02u:%02u:%02u\r\n",
          timestamp, year, mon, day, hour, min, sec);
    RTC_InitTime(year, mon, day, hour, min, sec);
}

//获取当前时间戳
uint32_t Rtc_GetTimestamp(void){
    struct tm t = {0};
    uint16_t year, mon, day, hour, min, sec;
    RTC_GetTime(&year, &mon, &day, &hour, &min, &sec);
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    return mktime(&t);
}

//100ms事件处理,主循环中轮询处理
void Period_100ms(void){
    if(Flag_100ms){
        //100ms事件处理
        Flag_100ms = 0;
        WWDG_Refresh(); //喂狗
        Check_IrBuf();
        LED1_TOGGLE();
        Lora_Process();
    }
}

//1s事件处理,主循环中轮询处理
void Period_1s(void){
    if(Flag_1s){
        //1s事件处理
        Flag_1s = 0;
        LED0_TOGGLE();
        WWDG_Refresh(); //喂狗
        Flash_Poll();
        #if 1 //test only
        PRINT("timestamp:%d\r\n",Rtc_GetTimestamp());
        // Lora_Tx((uint8_t*)"123456789ABCD",10,1500);
        #endif 
    }
}

__INTERRUPT                                   //interrupt flag
__HIGH_CODE                                   //put in ram
void TMR0_IRQHandler(void)  {                 //timer0 每10ms中断
    static uint32_t tick = 0;
    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END)){    //check flag
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);  //clear flag
        tick++;
        if(tick % 10 == 0){ //100ms
            Flag_100ms = 1;
        }
        if(tick % 100 == 0){ //1s
            Flag_1s = 1;
        }
    }
}
