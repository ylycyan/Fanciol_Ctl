#include "HAL.h"
#include <time.h>
#include "board.h"
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

// ***慎用!!! 尽量在初始化程序中调用,若在沁恒tmos任务中调用RTC_InitTime()可能导致异常. 
void RTC_SetTimestamp(time_t timestamp)
{
    struct tm * tm_p;
    if ((timestamp < 1672531200) || (timestamp > 2147483000)) { //2023-01-01 00:00:00 ~ 2038-01-19 11:03:20
    	return;
    }
    LClk32K_Select(Clk32K_LSE);
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    R8_CK32K_CONFIG |= RB_CLK_XT32K_PON;    //给外部32K上电
    R8_SAFE_ACCESS_SIG = 0;
    tm_p = localtime(&timestamp);
    RTC_InitTime(tm_p->tm_year,tm_p->tm_mon,tm_p->tm_mday,tm_p->tm_hour,tm_p->tm_min,tm_p->tm_sec);   
}

//获取当前时间戳
uint32_t Rtc_GetTimestamp(void){
    struct tm t = {0};
    RTC_GetTime((uint16_t*)&t.tm_year,(uint16_t*)&t.tm_mon,(uint16_t*)&t.tm_mday,(uint16_t*)&t.tm_hour,(uint16_t*)&t.tm_min,(uint16_t*)&t.tm_sec);
    return mktime(&t);
}

//100ms事件处理,主循环中轮询处理
void Period_100ms(void){
    if(Flag_100ms){
        //100ms事件处理
        Flag_100ms = 0;
        WWDG_Refresh(); //喂狗
        LED1_TOGGLE();
    }
}

//1s事件处理,主循环中轮询处理
void Period_1s(void){
    if(Flag_1s){
        //1s事件处理
        Flag_1s = 0;
        LED0_TOGGLE();
        #if 1 //test
        PRINT("timestamp:%d\r\n",Rtc_GetTimestamp());
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
