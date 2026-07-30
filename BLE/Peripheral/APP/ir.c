#include "board.h"
#include "time.h"
#include "CONFIG.h"
#include "peripheral.h"
#include "gattprofile.h"
IRBUF_t IrBuf = {0};
uint8_t IrLearnChannel = 0; //当前正在学习的通道索引 (0-9)

#define IR_MATCH_OK 0x00
#define IR_MATCH_FAIL 0x01
#define IR_MATCH_TIMEOUT 0x02
#define IR_MATCH_ERROR 0x03
#define IR_CMD_QUEUE_SIZE 8

static uint8_t irCmdQueue[IR_CMD_QUEUE_SIZE];
static uint8_t irCmdQueueHead = 0;
static uint8_t irCmdQueueTail = 0;
static uint8_t irCmdQueueCount = 0;
static uint16_t irLastRxLen = 0;

static uint8_t Ir_DequeueCmd(uint8_t *cmd)
{
    if(irCmdQueueCount == 0) return 0;
    *cmd = irCmdQueue[irCmdQueueTail];
    irCmdQueueTail = (uint8_t)((irCmdQueueTail + 1) % IR_CMD_QUEUE_SIZE);
    irCmdQueueCount--;
    return 1;
}

void Ir_RequestCmd(IR_CMD_t cmd)
{
    if(cmd == 0) return;

    if(Dev.irPendingCmd == 0) {
        Dev.irPendingCmd = (uint8_t)cmd;
        return;
    }

    if(irCmdQueueCount >= IR_CMD_QUEUE_SIZE) {
        PRINT("IR cmd queue full, drop %02x\r\n", (uint8_t)cmd);
        return;
    }

    irCmdQueue[irCmdQueueHead] = (uint8_t)cmd;
    irCmdQueueHead = (uint8_t)((irCmdQueueHead + 1) % IR_CMD_QUEUE_SIZE);
    irCmdQueueCount++;
}

void Ir_Pro(void)
{
    uint8_t cmd;

    if(Dev.irPendingCmd == 0) return;
    if(!IrBuf.isFinish && IrBuf.rxlen > 0) return;

    cmd = Dev.irPendingCmd;
    Dev.irPendingCmd = 0;
    Ir_cmd((IR_CMD_t)cmd);

    if(Ir_DequeueCmd(&cmd)) {
        Dev.irPendingCmd = cmd;
    }
}

//检测红外模块接收缓冲区数据
//蓝牙连接状态下，可通过FFE2直接透传测试
void Check_IrBuf(void){ //
    uint8_t state;
    if(!IrBuf.isFinish){ //未接收完成
        if(IrBuf.rxlen > 0){
            if(IrBuf.rxlen == irLastRxLen){ //100ms没收到新数据,接收完毕
                IrBuf.isFinish = 1;
                irLastRxLen = 0;
            }else{
                irLastRxLen = IrBuf.rxlen; //未接收完毕
                return;   
            }
        }else{
            return; 
        }
    }else{ //接收完成
        return;
    }
    PrintHex("uart3 rx",IrBuf.rxbuf,IrBuf.rxlen);
    GAPRole_GetParameter(GAPROLE_STATE,&state);
    if(state == GAPROLE_CONNECTED){ //蓝牙已连接
        peripheralCharNotify(SIMPLEPROFILE_CHAR2, IrBuf.rxbuf, IrBuf.rxlen);
    }
    #if(IR_MODULE == HXD039B)
    if(IrBuf.type == IR_TYPE_MATCH){ //查表匹配
        //匹配失败 RX 返回：FF FF（匹配失败）；
        // 匹配超时 RX 返回：88 99 AA （二十秒自动超时）
        //匹配成功 RX 返回：03 3E （匹配到的索引号）
        uint8_t status = IR_MATCH_ERROR;
        uint16_t irType = 0xFFFF;
        if(IrBuf.rxlen == 2){
            if((IrBuf.rxbuf[0] == 0xFF) && (IrBuf.rxbuf[1] == 0xFF)){
                #if _IR_INFO_
                    PRINT("ir Matched Fialed.\r\n");
                #endif
                Dev.errorCode.bit.irMatch = 1;
                status = IR_MATCH_FAIL;
            }else{ //匹配成功
                //查询 g_arc_info 表中是否存在对应编号
                irType = (((uint16_t)IrBuf.rxbuf[0])<<8)|(IrBuf.rxbuf[1]);
                Dev.irType = irType;
                Dev.errorCode.bit.irMatch = 0;
                status = IR_MATCH_OK;
                 #if _IR_INFO_
                    PRINT("\nir Matched:%d\r\n",Dev.irType);
                #endif
            }
        }else if(IrBuf.rxlen == 3){  //20s自动超时返回
            if((IrBuf.rxbuf[0] == 0x88) && (IrBuf.rxbuf[1] == 0x99) && (IrBuf.rxbuf[2] == 0xAA)){
                #if _IR_INFO_
                PRINT("ir Matched timeout\r\n");
                #endif
                Dev.errorCode.bit.irMatch = 1;
                status = IR_MATCH_TIMEOUT;
            }
        }else{
            Dev.errorCode.bit.irMatch = 1;
            #if _IR_INFO_
            PrintHex("ir Unexpected rx",IrBuf.rxbuf,IrBuf.rxlen);
            #endif
        }
        uint8_t payload[5];
        payload[0] = PID_IR_MATCH;
        payload[1] = status;
        payload[2] = irType & 0xFF;
        payload[3] = (irType >> 8) & 0xFF;
        payload[4] = Dev.irIdx;
        SendBtResponse(BT_CMD_NOTIFY, payload, 5);
        
    }else if(IrBuf.type == IR_TYPE_LEARNing){ //按遥控器手动学习
        #if _IR_INFO_
            PrintHex("ir Learning rx",IrBuf.rxbuf,IrBuf.rxlen);
        #endif
        uint8_t status = IR_MATCH_ERROR;
        uint8_t ch = IrLearnChannel;
        if(IrBuf.rxlen == 3){  //20s自动超时返回
            if((IrBuf.rxbuf[0] == 0x88) && (IrBuf.rxbuf[1] == 0x99) && (IrBuf.rxbuf[2] == 0xAA)){
                #if _IR_INFO_
                PRINT("ir Learn timeout\r\n");
                #endif
                Dev.errorCode.bit.irLearn = 1;
                status = IR_MATCH_TIMEOUT;
            }
        }else if(IrBuf.rxlen > 0 && ch < MAX_IR_LEARNNUM){
            //学习数据: 首字节00改为30 03，后面229字节拷贝
            Dev.learnCode[ch].cmd[0] = 0x30;
            Dev.learnCode[ch].cmd[1] = 0x03;
            memcpy(Dev.learnCode[ch].cmd + 2, IrBuf.rxbuf + 1, 229);
            Dev.learnCode[ch].enable = 1;
            if(ch >= Dev.learnNum){
                Dev.learnNum = ch + 1;
                if(Dev.learnNum > MAX_IR_LEARNNUM) Dev.learnNum = MAX_IR_LEARNNUM;
            }
            Dev.errorCode.bit.irLearn = 0;
            status = IR_MATCH_OK;
            SaveIrInfo();
            #if _IR_INFO_
                PRINT("ir Learn ch[%d] ok, learnNum=%d\r\n", ch, Dev.learnNum);
            #endif
        }
        //发送学习结果通知: PID_IR_LEARN + channel + status
        uint8_t payload[3];
        payload[0] = PID_IR_LEARN;
        payload[1] = ch;
        payload[2] = status;
        SendBtResponse(BT_CMD_NOTIFY, payload, 3);
    }
    #elif (IR_MODULE == xx)
    #endif
}

void IR_Init(void){ //uart3
    GPIOPinRemap(ENABLE,RB_PIN_UART3);
    GPIOB_SetBits(bTXD3_);
    GPIOB_ModeCfg(bRXD3_, GPIO_ModeIN_PU);      // RXD-配置上拉输入
    GPIOB_ModeCfg(bTXD3_, GPIO_ModeOut_PP_5mA); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART3_DefInit();
    UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART3_IRQn);
}

__INTERRUPT
__HIGH_CODE
void UART3_IRQHandler(void){
    switch( UART3_GetITFlag() ){
        case UART_II_LINE_STAT:        // 线路状态错误
            UART3_GetLinSTA();
            break;
        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            while(R8_UART3_RFC) {
                IrBuf.rxbuf[IrBuf.rxlen & (IRBUFSIZE-1)] = R8_UART3_RBR; //环形接收数据，避免溢出
                IrBuf.rxlen += 1;
            }
            // IrBuf.rxlen = (IrBuf.rxlen & (IRBUFSIZE-1)) + 1 ;
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
    IrBuf.isFinish = 0;
    IrBuf.type = IR_TYPE_NORMAL;
    #if(IR_MODULE == HXD039B)
        //构造cmd包 30 06+(2B)+(1B)
        if(Dev.irIdx >= IR_BRAND_COUNT || !Dev.irType || Dev.irType == 0xFFFFu){
            PRINT("Error: file:%s,line:%d,invalid ir config idx:%d type:%d.\r\n",__FILE__,__LINE__,Dev.irIdx,Dev.irType);
            Dev.errorCode.bit.irMatch = 1;
            return;
        }
        IrBuf.txbuf[0] = 0x30;
        IrBuf.txbuf[1] = 0x06;
        // HXD039B matching returns the module code directly. irIdx is retained
        // as user-facing brand metadata and is not an index into a flash table.
        IrBuf.txbuf[2] = Dev.irType>>8;
        IrBuf.txbuf[3] = Dev.irType&0xff;
        IrBuf.txbuf[4] = cmd;
        IrBuf.rxlen = 0;
        UART3_SendString(IrBuf.txbuf,5);
        #if _IR_INFO_
            PrintHex("ir tx",IrBuf.txbuf,5);
        #endif
        /* 普通命令的模块回执在不同 HXD039B 版本上不一致，且结果未被业务使用。
         * UART 提交后立即返回，避免阻塞 20 ms LoRa 轮询和 BLE 协议栈。 */
    #elif (IR_MODULE == xx)
    #endif
    IrBuf.isFinish = 1;
}

uint8_t Ir_ExecuteVerified(IR_CMD_t cmd)
{
    if(Dev.errorCode.bit.irMatch) return 0;
    Ir_cmd(cmd);
    // HXD039B firmware variants do not consistently acknowledge normal
    // commands. Validate the selected profile and UART submission here; the
    // commissioning UI performs the authoritative physical-response check.
    return Dev.errorCode.bit.irMatch ? 0 : 1;
}

uint8_t Ir_StartMatch(void)
{
    if(!IrBuf.isFinish && IrBuf.type != IR_TYPE_NORMAL) return 0;
    Dev.errorCode.bit.irMatch = 0;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    IrBuf.isFinish = 0;
    IrBuf.type = IR_TYPE_MATCH;
    IrBuf.txbuf[0] = 0x30;
    IrBuf.txbuf[1] = 0x70;
    IrBuf.txbuf[2] = 0xA0;
    UART3_SendString(IrBuf.txbuf, 3);
    return 1;
}

uint8_t Ir_StartLearning(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM || (!IrBuf.isFinish && IrBuf.type != IR_TYPE_NORMAL)) return 0;
    Dev.errorCode.bit.irLearn = 0;
    IrLearnChannel = ch;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    IrBuf.isFinish = 0;
    IrBuf.type = IR_TYPE_LEARNing;
    IrBuf.txbuf[0] = 0x30;
    IrBuf.txbuf[1] = 0x20;
    IrBuf.txbuf[2] = 0x50;
    UART3_SendString(IrBuf.txbuf, 3);
    return 1;
}

uint8_t Ir_SendLearnedVerified(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM || !Dev.learnCode[ch].enable) return 0;
    Ir_LearnSend(ch);
    // Some HXD039B revisions do not acknowledge replay. Successful UART
    // submission is followed by a physical confirmation in the host UI.
    return 1;
}

uint8_t Ir_CancelOperation(void)
{
    if(IrBuf.type != IR_TYPE_MATCH && IrBuf.type != IR_TYPE_LEARNing) return 1;
    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.isFinish = 1;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    Dev.errorCode.bit.irMatch = 0;
    Dev.errorCode.bit.irLearn = 0;
    return 1;
}

static void Ir_RecalculateLearnNum(void)
{
    uint8_t i;
    Dev.learnNum = 0;
    for(i = 0; i < MAX_IR_LEARNNUM; ++i) {
        if(Dev.learnCode[i].enable) Dev.learnNum = (uint8_t)(i + 1u);
    }
}

uint8_t Ir_ResetLearned(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM || (!IrBuf.isFinish && IrBuf.type != IR_TYPE_NORMAL)) return 0;
    memset(&Dev.learnCode[ch], 0, sizeof(Dev.learnCode[ch]));
    Ir_RecalculateLearnNum();
    SaveIrInfo();
    return 1;
}

uint8_t Ir_ResetAllLearned(void)
{
    if(!IrBuf.isFinish && IrBuf.type != IR_TYPE_NORMAL) return 0;
    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    Dev.learnNum = 0;
    SaveIrInfo();
    return 1;
}

uint16_t Ir_GetLearnedMask(void)
{
    uint8_t i;
    uint16_t mask = 0;
    for(i = 0; i < MAX_IR_LEARNNUM; ++i) {
        if(Dev.learnCode[i].enable) mask |= (uint16_t)(1u << i);
    }
    return mask;
}

//发送学习到的红外码
//ch: 通道索引 (0-9)
void Ir_LearnSend(uint8_t ch){
    if(ch >= MAX_IR_LEARNNUM || !Dev.learnCode[ch].enable){
        PRINT("Error: learnCode[%d] not ready\r\n", ch);
        return;
    }
    int16_t timeout = 100, temp = 0;
    IrBuf.isFinish = 0;
    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    //直接发送学习到的码: cmd[0]=0x30, cmd[1]=0x03, 后面是229字节数据
    UART3_SendString(Dev.learnCode[ch].cmd, 231);
    #if _IR_INFO_
        PrintHex("ir learn send", Dev.learnCode[ch].cmd, 10);
    #endif
    while(timeout > 0){
        if(IrBuf.rxlen > 0){
            if(IrBuf.rxlen == temp){
                break;
            }else{
                temp = IrBuf.rxlen;
            }
        }
        DelayMs(20);
        timeout -= 20;
    }
    IrBuf.isFinish = 1;
}

