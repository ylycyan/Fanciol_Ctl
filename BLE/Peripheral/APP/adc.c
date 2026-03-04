#include "CH58x_common.h"
#include "board.h"
#include <math.h>
#define _DEBUG_AD 1
const float Rp=10000.0; //10K
const float T2 = (273.15+25.0);;//T2
const float Bx = 3950.0;//B
const float Ka = 273.15;
void ADC_Init(void){

}
void ADC_Pro(void){
    static u_int32_t lastSampStamp = 0;
    if(abs((int)(lastSampStamp - LocalTimestamp)) < AD_INTERVAL){
        return;
    }
    WWDG_SetCounter(0);//喂狗
    lastSampStamp = LocalTimestamp;
    uint16_t caliVal,temp,maxVal,minVal,i;
    uint32_t sum = 0;
    float vol,res,tem;
    #if _DEBUG_AD
    PRINT("adc start sampling ,@%d\n",LocalTimestamp);
    #endif
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    caliVal = ADC_DataCalib_Rough(); //获取内部校准值
    #if _DEBUG_AD
    PRINT("Calibration Val:%d\n",caliVal);
    #endif
    ADC_ChannelCfg(11);
    for(i = 0;i < 20;i++){
        WWDG_SetCounter(0);//喂狗
        temp =  ADC_ExcutSingleConver() + caliVal;
        if(temp > maxVal) maxVal = temp;
        if(temp < minVal) minVal = temp;
        sum += temp;
    }
    sum -= (maxVal + minVal); //去掉最大最小值
    // ch583x 内部倍率
    // -12dB(1/4 倍)	(ADC/512-3)*Vref	5*Vref	    -0.2V ～ VIO33+0.2V	2.9V ～ VIO33
    // -6dB(1/2 倍)	    (ADC/1024-1)*Vref	3*Vref	    -0.2V ～ 3.15V	    1.9V ～ 3V
    // 0db(1倍)	        (ADC/2048)*Vref	    2*Vref	    0V ～ 2.1V	        0V ～ 2V
    // 6db(2倍)	        (ADC/4096+0.5)*Vref	1.5*Vref	0.525V ～ 1.575V	0.6V ～ 1.5V
    vol = ((sum/18.0f)*1.050)/2048;// 0db： v = val*1.05/2048
    if(vol <= 0.001f){
        return;
    }
    res = 10000 * vol / (3.3 - vol);
    tem = res/Rp;
	tem = log(tem);//ln(Rt/Rp)
	tem/=Bx;//ln(Rt/Rp)/B
	tem+=(1/T2);
	tem = 1/(tem);
	tem-=Ka;
    Dev.tem = tem;
    #if _DEBUG_AD
    PRINT("meanVal = %d ,vol = %d,res = %d,tem = %d\n",sum,(int)(vol*100),(int)res,(int)(tem*100));
    #endif
}