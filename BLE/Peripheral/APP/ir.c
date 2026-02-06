#include "board.h"
#include "time.h"
IRBUF_t IrBuf = {0};
void Check_IrBuf(void){ //
    static uint16_t lastRxLen = 0;
    if(!IrBuf.isFinish){ //未接收完成
        if(IrBuf.rxlen > 0){
            if(IrBuf.rxlen == lastRxLen){ //100ms没收到新数据,接收完毕
                IrBuf.isFinish = 1;
                lastRxLen = 0;
            }else{
                lastRxLen = IrBuf.rxlen; //未接收完毕
            }
        }
    }else{ //接收完成
        return;
    }
    if(!IrBuf.isFinish){ //未接收完成
        return;        
    }
    #if(IR_MODULE == HXD039B)
    if(IrBuf.type == IR_TYPE_MATCH){ //查表匹配
        //匹配失败 RX 返回：FF FF（匹配失败）；
        // 匹配超时 RX 返回：88 99 AA （二十秒自动超时）
        //匹配成功 RX 返回：03 3E （匹配到的索引号）
        if(IrBuf.rxlen == 2){
            if((IrBuf.rxbuf[0] == 0xFF) && (IrBuf.rxbuf[1] == 0xFF)){
                #if _IR_INFO_
                    PRINT("ir Matched Fialed.\r\n");
                #endif
                Dev.errorCode.bit.irLearn = 1;
            }else{ //匹配成功
                //查询 g_arc_info 表中是否存在对应编号
                uint16_t temp = (((uint16_t)IrBuf.rxbuf[0])<<8)|(IrBuf.rxbuf[1]);
                 #if _IR_INFO_
                    PRINT("\nir Matched:%d\r\n",temp);
                #endif
                for(int i,j = 0;i<(sizeof(g_arc_info)/sizeof(t_arc));i++){
                    for(j = 0;j<g_arc_info[i].num;j++){
                        if(g_arc_info[i].cmd[j] == temp){ //flash表中找到对应设备
                            Dev.irIdx = i;
                            Dev.irType = j;
                            Dev.errorCode.bit.irLearn = 0;
                            #if _IR_INFO_
                                PRINT("\n\t### ir Match Success ###\r\n");
                                PRINT("\t### match device:%s,index:%d\r\n",g_arc_info[i].name,g_arc_info[i].cmd[j]);
                            #endif
                            break;
                        }
                    }
                }
                Dev.errorCode.bit.irLearn = 1; //未匹配到对应品牌空调
            }
        }else if(IrBuf.rxlen == 3){  //20s自动超时返回
            if((IrBuf.rxbuf[0] == 0x88) && (IrBuf.rxbuf[1] == 0x99) && (IrBuf.rxbuf[2] == 0xAA)){
                #if _IR_INFO_
                PRINT("ir Matched timeout\r\n");
                #endif
                Dev.errorCode.bit.irLearn = 1;
            }
        }else{
            Dev.errorCode.bit.irLearn = 1;
            #if _IR_INFO_
            PrintHex("ir Unexpected rx",IrBuf.rxbuf,IrBuf.rxlen);
            #endif
        }
        
    }else if(IrBuf.type == IR_TYPE_LEARNing){ //按遥控器手动学习
        #if _IR_INFO_
            PrintHex("ir Learning rx",IrBuf.rxbuf,IrBuf.rxlen);
        #endif
        if(IrBuf.rxlen == 3){  //20s自动超时返回
            if((IrBuf.rxbuf[0] == 0x88) && (IrBuf.rxbuf[1] == 0x99) && (IrBuf.rxbuf[2] == 0xAA)){
                #if _IR_INFO_
                PRINT("ir Matched timeout\r\n");
                #endif
                Dev.errorCode.bit.irLearn = 1;
            }
        }else{

            //学习数据校验? 
            //只需要把通过发送 30 20 50 学习命令返回的 230 个字节学习数据第一个字节 00 改为 30 03 再发送给芯片即可控制设备
            //@todo 方法待优化
            Dev.learnCode[Dev.learnNum%MAX_IR_LEARNNUM].cmd[0] = 0x30;
            Dev.learnCode[Dev.learnNum%MAX_IR_LEARNNUM].cmd[1] = 0x03;
            memcpy(Dev.learnCode[Dev.learnNum%MAX_IR_LEARNNUM].cmd+2,IrBuf.rxbuf+1,229);
            Dev.learnCode[Dev.learnNum%MAX_IR_LEARNNUM].type = Dev.learnNum%MAX_IR_LEARNNUM;
            Dev.learnNum++;
            if(Dev.learnNum > MAX_IR_LEARNNUM){
                Dev.learnNum = MAX_IR_LEARNNUM;
            }
            #if _IR_INFO_
                PRINT("dev learnNum=%d\r\n",Dev.learnNum);
                // PrintHex("ir Learning rx",IrBuf.rxbuf,IrBuf.rxlen);
            #endif
            Dev.errorCode.bit.irLearn = 0;
        }
    }
    #elif (IR_MODULE == xx)
    #endif
}

void IR_Init(void){ //uart1
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);      // RXD-配置上拉输入
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART1_DefInit();
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);
}

void UART1_IRQHandler(void){
    switch( UART1_GetITFlag() ){
        case UART_II_LINE_STAT:        // 线路状态错误
            UART1_GetLinSTA();
            break;
        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            while(R8_UART1_RFC) {
                IrBuf.rxbuf[IrBuf.rxlen & (IRBUFSIZE-1)] = R8_UART1_RBR; //环形接收数据，避免溢出
                IrBuf.rxlen += 1;
            }
            IrBuf.rxlen = (IrBuf.rxlen & (IRBUFSIZE-1)) + 1 ;
            break;
        case UART_II_THR_EMPTY: // 发送缓存区空，可继续发送
            break;
        case UART_II_MODEM_CHG: // 只支持串口0
            break;
        default:
            break;
     }
}

//普通指令查表 发送+接收
void Ir_cmd(IR_CMD_t cmd){
    if(Dev.errorCode.bit.irMatch){
        PRINT("Error: file:%s,line:%d,unMatched ir device .",__FILE__,__LINE__);
        return;
    }
    int16_t timeout = 100,temp = 0; //100ms超时
    IrBuf.isFinish = 0;
    IrBuf.type = IR_TYPE_NORMAL;
    #if(IR_MODULE == HXD039B)
        //构造cmd包 30 06+(2B)+(1B)
        IrBuf.txbuf[0] = 0x30;
        IrBuf.txbuf[1] = 0x06;
        IrBuf.txbuf[2] = g_arc_info[Dev.irIdx].cmd[Dev.irType]>>8;
        IrBuf.txbuf[3] = g_arc_info[Dev.irIdx].cmd[Dev.irType]&0xff;
        IrBuf.txbuf[4] = cmd;
        IrBuf.rxlen = temp = 0;
        UART1_SendString(IrBuf.txbuf,5);
        #if _IR_INFO_
            PrintHex("ir tx",IrBuf.txbuf,5);
        #endif
        while(timeout > 0){
            if(IrBuf.rxlen > 0){
                if(IrBuf.rxlen == temp){ //连续20ms无接收数据，认为接收完毕
                    break;
                }else{
                    temp = IrBuf.rxlen;
                }
            }
            DelayMs(20);
            timeout -= 20;
        }
        if(IrBuf.rxlen > 0){
            #if _IR_INFO_
                PrintHex("ir rx",IrBuf.rxbuf,IrBuf.rxlen);
            #endif
        }else{
            #if _IR_INFO_
                PRINT("ir recv timeout\r\n");;
            #endif
        }
    #elif (IR_MODULE == xx)
    #endif
    IrBuf.isFinish = 1;
}