#include "board.h"
#include "lora.h"
void Lora_Process(void){
    static uint16_t Timer_Lora = 0; //lora状态机时间计数,单位ms
    static uint8_t LoraBuf[64] = {0};
    uint16_t irq = 0;
    uint8_t len,cmd,LoraTag; 
    float operateParameter;
    Timer_Lora ++; //100ms 只作粗略估计,不考虑tx\其他程序运行时间,实际影响可忽略不记
    if(!BITGET(Dev.mode,1)){ //bit1 lora不启用
        return;
    }
       //
        if (Timer_Lora >= 1800) //180s 
        {
            Timer_Lora = 0;
            Dev.loraStatus = 1; 
        }
        if(Dev.loraStatus == 1){ //还未注册：发送注册数据包
            if(Timer_Lora < (100 + ((Dev.nodeId*100)%100))) // 根据Dev.nodeId计算每次轮询时间,每(10+[0~3])s 轮询注册一次
            {
                return; //未到轮询时间
            } 
            //发送注册请求  eCmdLogin(1)Tag(1)Dev.nodeId(2)Crc(1)
            *(LoraBuf) = 5;  //cmdLogin
            *(LoraBuf + 1) = 0;  //tag
            *(uint16_t*)(LoraBuf + 2) = Dev.nodeId;  //Dev.nodeId
            AddCrc(LoraBuf,4);
            //lora频率切换为注册频段
            Lora_Init(Dev.channel * 0.3f + 420.05f,22,10,4);  //注册频率
            Lora_Tx(LoraBuf,5);
            Dev.loraStatus = 2;
            Timer_Lora = 0; //切换至发送检测状态,重新超时计数
        }else if(Dev.loraStatus == 2){
            irq = Lora_GetIrqStatus();
            if (irq == IRQ_TX_DONE) { //发送完成：RFLR_IRQFLAGS_TXDONE
                Lora_Listening(); //保持在注册通道(频率不变)接收网关反馈
                Dev.loraStatus = 3; //发送完成，等待网关反馈
                Timer_Lora = 0; //接收时重新计算超时时间
            } else if ((irq > 0) || (Timer_Lora > 10)) { //检测到非预期的Lora中断(出错?)或者超时，说明注册失败，后续重试
                Dev.loraStatus = 1; //切换至未注册状态
                Timer_Lora = 0; 
            } //else //发送中(未检测到发送完成中断，且未发送超时)，继续等待
        }else if(Dev.loraStatus == 3){  //注册请求发送成功，等待注册反馈
            Lora_CheckData(LoraBuf,&len);
            if(len > 0){
                // cmdConfig(1)Tag(1)Timestamp(4)Dev.gatewayId(2)Dev.nodeId(2)LoraPower(1)Dev.nodeIdx(2)DataCycle(2)Dev.scanCycle(2)DeviceType(1)DeviceTag(1)Crc(1)
                if(ChkCrc(LoraBuf,len) == 0){ //接收到数据,但crc校验错误
                    if(Timer_Lora >= 200){ // 接收超时1s,重新尝试注册 1
                        Timer_Lora = 0;
                        Dev.loraStatus = 1;
                        return;
                    }
                }//收到注册反馈
                if((*LoraBuf == 7) && (*(uint16_t*)(LoraBuf+8) == Dev.nodeId)){
                    // Dev.scanCycle = *(uint16_t *)(LoraBuf + 15);
                    // Dev.scanCycle = 0x3c;
                    Dev.scanCycle = (LoraBuf[16]<<8)|(LoraBuf[15]);
                    if(Dev.scanCycle < 60){ // 采集周期限定 1~3min
                        Dev.scanCycle = 60;
                    }else if(Dev.scanCycle > 180){
                        Dev.scanCycle = 180;
                    }
                    Dev.gatewayId = (LoraBuf[7]<<8)|(LoraBuf[6]);
                    //注册成功,切换至接收指令状态
                    //lora频率切换为工作频段
                    if(Dev.channel <= 22){
                        Dev.loraFrequency = Dev.loraFrequency + 3.1375f;
                    }else{
                        Dev.loraFrequency = (Dev.channel - 23) * 0.3f + 420.1875f;
                    }
                    Lora_Init(Dev.loraFrequency,22,8,0xA);  //切换至监听频率
                    Lora_Listening();  //监听网关指令  
                    Dev.loraStatus = 4;
                    Timer_Lora = 0;
                }
            }else{ //未收到反馈
                if(Timer_Lora >= 20){ // 接收超时2s,重新尝试注册 2
                    Timer_Lora = 0;
                    Dev.loraStatus = 1;
                    return;
                }
            }
        }else if(Dev.loraStatus == 4){ //监听网关指令
            Lora_CheckData(LoraBuf,&len);
            if(len > 0){
                if(ChkCrc(LoraBuf,len) == 1){
                    cmd = LoraBuf[0];
                    LoraTag = LoraBuf[1];
                    if(cmd == 0x0f && *(uint16_t*)(LoraBuf + 2) == Dev.gatewayId ){ //群控下发指令，无需上报结果
                        Lora_Listening();
                    }else{
                        if((*(uint16_t*)(LoraBuf + 2) != Dev.gatewayId) || (*(uint16_t*)(LoraBuf + 4) != Dev.nodeId)){
                            Lora_Listening(); //
                        }
                        else if(cmd == 13){ //针对节点的命令
                            if(LoraTag == 1){// 网关下发指令: eCmdData1(1)Tag(1-1)Dev.gatewayId(2)Dev.nodeId(2)Operate(1)OperateTag(2)OperateParameter(4)OperateToken(4)Crc(1)
                                //operate 6(u8) , operateTag 7(u16) , operateParameter 9(float) 
                                operateParameter = *(float*)(LoraBuf + 9);
                                if(LoraBuf[6] == 21){  //开机
                                    if(LoraBuf[7] == 0){  //0:默认模式开机  1:制冷模式开机  2:制热模式开机 3:指定的温度制冷开机  4:指定的温度制热开机 
                                    }
                                }else if(LoraBuf[6] == 22){ //关机 
                                    if(LoraBuf[7] == 0){ 
                                    }
                                }else if(LoraBuf[6] == 23){ //设定温度
                                   
                                }else if(LoraBuf[6] == 24){ //制冷/制热设定
                                    
                                }else if(LoraBuf[6] == 25){ //风机转速设定
                                    
                                }else if(LoraBuf[6] == 26){ //温度锁定
                                   
                                }else if(LoraBuf[6] == 27){ //管制设定-模式锁定
                                    
                                }else if(LoraBuf[6] == 28){ //温度补偿设定
                                   
                                }else if(LoraBuf[6] == 29){  //温度锁定上/下限设置
                                    
                                }

    
                            } else { // // 网关要求节点上报数据: eCmdData1(1)Tag(1-0)Dev.gatewayId(2)Dev.nodeId(2)Timestamp(4)Crc(1)
                                LoraTag = 0;
                            }
                            // 上报数据反馈： Cmd(1:13)Tag(1-1)Dev.nodeId(2)Rssi(1)ErrorInfo(1)TempSet(2)OnOff(1)Temp(2)Mode(2)Wend(2)LockMode(2)Crc(1)
                            // Beep = ~Beep;
                            // 上报数据
                            // Beep = ~Beep;
                            // TempSetting(u16-sfloat)OpStatus(u8)EnvironmentTemp(u16-sfloat)FanTempDiff(u16-sfloat)ValveTempDiff(u16-sfloat),StatusCode(u16) //OpStatus:(高4位)0-关 1-制冷 2-制热 3-新风 4-制冷+新风 5-制热+新风 6-未知 (低4位)FanSpeed:0-自动 1-低速 2-中速 3-高速
                            LoraBuf[0] = 1;  //ecmdOk
                            LoraBuf[1] = LoraTag;  //tag
                            LoraBuf[2] = Dev.nodeId&0xff;  //
                            LoraBuf[3] = (Dev.nodeId>>8)&0xff;
                            LoraBuf[4] = (uint8_t)Lora_GetRssi();
                            LoraBuf[5] = 0; //errorInfo
                            LoraBuf[6] = 0;  
                            LoraBuf[7] = Dev.temSet;  //设定温度  u16
                            LoraBuf[8] = 0;  //OpStatus u8
                            LoraBuf[9] = 0;			
                            LoraBuf[10] = Dev.tem;// 环境温度 u16
                            LoraBuf[11] = 0;  //制冷制热 u16
                            LoraBuf[12] = Dev.ctlMode;
                            LoraBuf[13] = 0;
                            LoraBuf[14] = Dev.wind; //风速度
                            LoraBuf[15] = 0; // status code = 0: 标识本地面板控制被禁用
                            LoraBuf[16] = 0;

                            AddCrc(LoraBuf,17);
                            Lora_Tx(LoraBuf,18);
                            Dev.loraStatus = 5; // 通过Lora发送了数据，后续检测发送完成
                            Timer_Lora = 0; //清空计时器
                        } else{ //异常指令
                            Lora_Listening();
                        }
                    }
                } else{ //Crc 错误,继续保持监听,不更新TimerLora超时检测
                    Lora_Listening();
                }
            } //未收到反馈
            if(Timer_Lora >= 1800){ //超过三个采集周期未收到反馈，切换到注册频率重新注册
                Dev.loraStatus = 1;
                Timer_Lora = 300;
                return;
            }
        }else if(Dev.loraStatus == 5){ //发送指令响应到网关,等待发送完成
            irq = Lora_GetIrqStatus();       
            if(irq == IRQ_TX_DONE || (Timer_Lora > 15)){
                Dev.loraStatus = 4;
                Timer_Lora = 0;
                Lora_Listening();
            }
        }
}