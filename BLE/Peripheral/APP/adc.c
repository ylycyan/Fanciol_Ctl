#include "CH58x_common.h"
#include "board.h"
#include <math.h>
void ADC_Init(void){

}
void ADC_Pro(void){
    uint16_t adVal[20] = {0},caliVal,temp,maxVal,minVal,i;
    uint32_t sum = 0;
    float fVal,meanVal,sum_diff,stdDev;
    GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_Floating);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    caliVal = ADC_DataCalib_Rough(); //获取内部校准值
    PRINT("Calibration Val:%d\n",caliVal);
    ADC_ChannelCfg(11);
    for(i = 0;i < 20;i++){
        adVal[i] = ADC_ExcutSingleConver() + caliVal;
        temp = adVal[i];
        if(temp > maxVal) maxVal = temp;
        if(temp < minVal) minVal = temp;
        sum += temp;
    }
    meanVal = sum / 20; //计算20次采样平均值
    for(i = 0;i < 20;i++){
        sum_diff += ((adVal[i] - meanVal) * (adVal[i] - meanVal));
    }
    stdDev = sqrt(sum_diff / (20 - 1)); //标准方差
    sum -= (maxVal + minVal); //去掉最大最小值
    PRINT("meanVal1 = %f,stdDev = %f,meanVal2 = %f \n",meanVal,stdDev,sum/20.0f);
}