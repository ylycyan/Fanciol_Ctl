#include "CH58x_common.h"
#include "board.h"
#include "ntc_b3950.h"
#define _DEBUG_AD 0
static uint8_t adcValid;
void ADC_Init(void){
    adcValid = 0;
    Dev.errorCode.bit.ad = 1;
}
uint8_t ADC_IsValid(void){ return adcValid; }
void ADC_Pro(void){
    static uint32_t lastSampStamp = 0;
    /*
     * RTC 可能被网关向前或向后校时，不能把无符号差值强转 int 后再 abs：
     * 大跨度校时会溢出。回拨时立即重新采样，正常情况下按间隔限频。
     */
    if(lastSampStamp != 0u && LocalTimestamp >= lastSampStamp &&
       (LocalTimestamp - lastSampStamp) < AD_INTERVAL){
        return;
    }
    lastSampStamp = LocalTimestamp;
    uint16_t caliVal,temp,maxVal = 0,minVal = 0xffff,i;
    uint32_t sum = 0;
    uint16_t meanValue;
    int16_t temperatureX10;
    #if _DEBUG_AD
    PRINT("adc start sampling ,@%ld\n",LocalTimestamp);
    #endif
    GPIOA_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    caliVal = ADC_DataCalib_Rough(); //获取内部校准值
    #if _DEBUG_AD
    PRINT("Calibration Val:%d\n",caliVal);
    #endif
    ADC_ChannelCfg(0);
    for(i = 0;i < 20;i++){
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
    meanValue = (uint16_t)(sum / 18U);
    if(!NtcB3950_AdcToTempX10(meanValue, &temperatureX10)){
        adcValid = 0;
        Dev.errorCode.bit.ad = 1;
        return;
    }
    Dev.roomTempX10 = temperatureX10;
    adcValid = 1;
    Dev.errorCode.bit.ad = 0;
    #if _DEBUG_AD
    PRINT("meanVal=%u, tempX10=%d\n", meanValue, temperatureX10);
    #endif
}
