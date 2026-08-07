#include "board.h"
#include "time.h"
#include "CONFIG.h"
#include "peripheral.h"
#include "gattprofile.h"
#include "splitac_service_v2.h"
#include "ir_control_map.h"
#include "ir_reliability_v2.h"
IRBUF_t IrBuf = {0};
uint8_t IrLearnChannel = 0; //当前正在学习的通道索引 (0-9)

#define IR_LEARN_RESPONSE_LEN 230U
#define IR_LEARN_REPLAY_LEN 231U

static uint8_t irPipelineHighWater = 0;
static uint16_t irLastRxLen = 0;
static volatile uint16_t irTxLength = 0;
static volatile uint16_t irTxOffset = 0;
static volatile uint8_t irTxActive = 0;
static volatile uint8_t irTxWaitResponse = 0;
static uint32_t irOperationDeadline = 0;
static ir_reliability_v2_t irReliability;

static uint8_t Ir_SubmitInternalCommand(IR_CMD_t cmd, uint8_t allowRepeat);

static uint8_t Ir_IsControlPathIdle(void)
{
    return !irTxActive &&
           IrBuf.isFinish &&
           irReliability.repeat_cmd == 0U;
}

static uint8_t Ir_IsIdempotentCommand(IR_CMD_t cmd)
{
    /* 相对温度键重复发送会多加/减一度，其余内部码均表示绝对状态。 */
    return cmd != IR_CMD_TEMP_UP && cmd != IR_CMD_TEMP_DOWN;
}

static void Ir_TxFillFifo(void)
{
    while(irTxActive && irTxOffset < irTxLength &&
          R8_UART3_TFC < UART_FIFO_SIZE) {
        R8_UART3_THR = IrBuf.txbuf[irTxOffset++];
    }
}

static uint8_t Ir_TxStartCopy(const uint8_t *data,
                              uint16_t len,
                              uint8_t waitResponse)
{
    if(data == 0 || len == 0U || len > IRBUFSIZE || irTxActive) return 0U;

    if(data != IrBuf.txbuf) memcpy(IrBuf.txbuf, data, len);
    irTxLength = len;
    irTxOffset = 0U;
    irTxWaitResponse = waitResponse ? 1U : 0U;
    irTxActive = 1U;
    IrBuf.isFinish = 0U;
    irPipelineHighWater = 1U;

    /* 先填满 8 字节 FIFO，余下数据由 THR_EMPTY 中断继续发送。 */
    Ir_TxFillFifo();
    UART3_INTCfg(ENABLE, RB_IER_THR_EMPTY);
    return 1U;
}

static void Ir_TxAbort(void)
{
    UART3_INTCfg(DISABLE, RB_IER_THR_EMPTY);
    irTxActive = 0U;
    irTxLength = 0U;
    irTxOffset = 0U;
    irTxWaitResponse = 0U;
}

void Ir_Pro(void)
{
    uint8_t cmd;

    /*
     * 匹配、学习、原始透传以及正在发送的长帧拥有红外模块独占权。
     * 旧逻辑在“尚未收到第一个字节”时会误发规则命令并覆盖 IrBuf.type。
     */
    if(irTxActive) return;
    if(!IrBuf.isFinish) {
        if(irOperationDeadline != 0U &&
           (int32_t)(CurTick - irOperationDeadline) >= 0) {
            if(IrBuf.type == IR_TYPE_MATCH) Dev.errorCode.bit.irMatch = 1;
            if(IrBuf.type == IR_TYPE_LEARNing) Dev.errorCode.bit.irLearn = 1;
            PRINT("IR operation timeout: type=%u\r\n", IrBuf.type);
            IrBuf.isFinish = 1U;
            IrBuf.rxlen = 0U;
            irLastRxLen = 0U;
            irOperationDeadline = 0U;
        }
        return;
    }

    if(irReliability.repeat_cmd != 0U) {
        if(IrReliabilityV2_TakeDueRepeat(&irReliability, CurTick, &cmd)) {
            (void)Ir_SubmitInternalCommand((IR_CMD_t)cmd, 0U);
        }
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
                irOperationDeadline = 0U;
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
    if(state == GAPROLE_CONNECTED && SplitAcV2_MaintenanceActive()){ //仅开发者维护窗口开放原始透传
        peripheralCharNotify(SIMPLEPROFILE_CHAR2, IrBuf.rxbuf, IrBuf.rxlen);
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
                Dev.errorCode.bit.irMatch = 1;
            }else{ //匹配成功
                // HXD039B 匹配返回码即模块内部码，直接作为 Dev.irType 使用
                Dev.irType = (((uint16_t)IrBuf.rxbuf[0])<<8)|(IrBuf.rxbuf[1]);
                Dev.errorCode.bit.irMatch = 0;
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
            }
        }else{
            Dev.errorCode.bit.irMatch = 1;
            #if _IR_INFO_
            PrintHex("ir Unexpected rx",IrBuf.rxbuf,IrBuf.rxlen);
            #endif
        }
    }else if(IrBuf.type == IR_TYPE_LEARNing){ //按遥控器手动学习
        #if _IR_INFO_
            PrintHex("ir Learning rx",IrBuf.rxbuf,IrBuf.rxlen);
        #endif
        uint8_t ch = IrLearnChannel;
        if(IrBuf.rxlen == 3){  //20s自动超时返回
            if((IrBuf.rxbuf[0] == 0x88) && (IrBuf.rxbuf[1] == 0x99) && (IrBuf.rxbuf[2] == 0xAA)){
                #if _IR_INFO_
                PRINT("ir Learn timeout\r\n");
                #endif
                Dev.errorCode.bit.irLearn = 1;
            }
        }else if(IrBuf.rxlen == IR_LEARN_RESPONSE_LEN && ch < MAX_IR_LEARNNUM){
            //学习数据: 首字节00改为30 03，后面229字节拷贝
            Dev.learnCode[ch].cmd[0] = 0x30;
            Dev.learnCode[ch].cmd[1] = 0x03;
            memcpy(Dev.learnCode[ch].cmd + 2, IrBuf.rxbuf + 1, IR_LEARN_RESPONSE_LEN - 1U);
            Dev.learnCode[ch].enable = 1;
            if(ch >= Dev.learnNum){
                Dev.learnNum = ch + 1;
                if(Dev.learnNum > MAX_IR_LEARNNUM) Dev.learnNum = MAX_IR_LEARNNUM;
            }
            Dev.errorCode.bit.irLearn = 0;
            SaveIrInfo();
            #if _IR_INFO_
                PRINT("ir Learn ch[%d] ok, learnNum=%d\r\n", ch, Dev.learnNum);
            #endif
        }else{
            Dev.errorCode.bit.irLearn = 1;
            PRINT("ir Learn invalid length:%d ch:%d\r\n", IrBuf.rxlen, ch);
        }
        //发送学习结果通知: PID_IR_LEARN + channel + status
    }
    #elif (IR_MODULE == xx)
    #endif
}

void IR_Init(void){ //uart3
    IrBuf.isFinish = 1U;
    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.rxlen = 0U;
    irLastRxLen = 0U;
    irTxActive = 0U;
    irTxLength = 0U;
    irTxOffset = 0U;
    irTxWaitResponse = 0U;
    irOperationDeadline = 0U;
    irPipelineHighWater = 0U;
    IrReliabilityV2_Init(&irReliability);
    GPIOPinRemap(ENABLE,RB_PIN_UART3);
    GPIOB_SetBits(bTXD3_);
    GPIOB_ModeCfg(bRXD3_, GPIO_ModeIN_PU);      // RXD-配置上拉输入
    GPIOB_ModeCfg(bTXD3_, GPIO_ModeOut_PP_5mA); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART3_DefInit();
    UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART3_IRQn);
}

/*
 * 保持中断入口尽量短，主体放在普通代码区，避免占用 CH583 紧张的
 * 32 KB RAM（.highcode 会在启动时复制到 RAM）。
 */
static void __attribute__((noinline)) Ir_HandleTxFifoEmpty(void)
{
    if(!irTxActive) {
        UART3_INTCfg(DISABLE, RB_IER_THR_EMPTY);
        return;
    }
    Ir_TxFillFifo();
    /*
     * 必须等待最后一批字节真正离开 FIFO 后再释放互斥，不能在
     * 刚写入最后一批数据时提前允许下一条红外命令进入。
     */
    if(irTxOffset >= irTxLength && R8_UART3_TFC == 0U) {
        uint8_t waitResponse = irTxWaitResponse;
        Ir_TxAbort();
        if(!waitResponse) IrBuf.isFinish = 1U;
    }
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
                uint8_t value = R8_UART3_RBR;
                if(IrBuf.rxlen < (IRBUFSIZE - 1U)) {
                    IrBuf.rxbuf[IrBuf.rxlen++] = value;
                }
            }
            // IrBuf.rxlen = (IrBuf.rxlen & (IRBUFSIZE-1)) + 1 ;
            break;
        case UART_II_THR_EMPTY: // 发送 FIFO 空，继续发送剩余字节
            Ir_HandleTxFifoEmpty();
            break;
        case UART_II_MODEM_CHG: // 只支持串口0
            break;
        default:
            break;
     }
}

static uint8_t Ir_SubmitInternalCommand(IR_CMD_t cmd, uint8_t allowRepeat)
{
    #if(IR_MODULE == HXD039B)
    if(Dev.errorCode.bit.irMatch ||
       Dev.irIdx >= IR_BRAND_COUNT ||
       !Dev.irType ||
       Dev.irType == 0xFFFFu) {
        PRINT("IR profile invalid: idx=%d type=%04x\r\n", Dev.irIdx, Dev.irType);
        Dev.errorCode.bit.irMatch = 1;
        return 0U;
    }

    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.rxlen = 0U;
    irLastRxLen = 0U;
    IrBuf.txbuf[0] = 0x30U;
    IrBuf.txbuf[1] = 0x06U;
    /* HXD039B 匹配结果就是模块内部码，品牌索引仅作为现场显示元数据。 */
    IrBuf.txbuf[2] = (uint8_t)(Dev.irType >> 8);
    IrBuf.txbuf[3] = (uint8_t)Dev.irType;
    IrBuf.txbuf[4] = (uint8_t)cmd;
    if(!Ir_TxStartCopy(IrBuf.txbuf, 5U, 0U)) return 0U;

    IrReliabilityV2_RecordSubmitted(&irReliability);
    if(allowRepeat && Ir_IsIdempotentCommand(cmd)) {
        (void)IrReliabilityV2_ScheduleRepeat(&irReliability,
                                              (uint8_t)cmd,
                                              CurTick);
    }
    #if _IR_INFO_
    PrintHex("ir tx", IrBuf.txbuf, 5U);
    #endif
    return 1U;
    #elif (IR_MODULE == xx)
    (void)cmd;
    (void)allowRepeat;
    return 0U;
    #endif
}

uint8_t Ir_ExecuteVerified(IR_CMD_t cmd)
{
    if(Dev.irActType != ACT_TYPE_IR ||
       Dev.errorCode.bit.irMatch ||
       Dev.irIdx >= IR_BRAND_COUNT ||
       !Dev.irType ||
       Dev.irType == 0xFFFFu) return 0;
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    /*
     * 不同 HXD039B 固件对普通控制命令没有一致回执，因此这里的成功只表示
     * 配置有效且命令已提交到 UART；现场实施仍由用户确认空调真实响应。
     */
    return Ir_SubmitInternalCommand(cmd, 1U);
}

uint8_t Ir_ConfiguredCommandSupported(IR_CMD_t cmd)
{
    int8_t channel;

    if(Dev.irActType == ACT_TYPE_IR) {
        return !Dev.errorCode.bit.irMatch &&
               Dev.irIdx < IR_BRAND_COUNT &&
               Dev.irType != 0u &&
               Dev.irType != 0xFFFFu;
    }
    if(Dev.irActType != ACT_TYPE_LEARN) return 0;

    channel = IrControl_LearnedChannel(cmd);
    return channel >= 0 &&
           channel < MAX_IR_LEARNNUM &&
           Dev.learnCode[(uint8_t)channel].enable;
}

uint8_t Ir_ExecuteConfiguredVerified(IR_CMD_t cmd)
{
    int8_t channel;

    if(!Ir_ConfiguredCommandSupported(cmd)) return 0;
    if(Dev.irActType == ACT_TYPE_IR) return Ir_ExecuteVerified(cmd);

    channel = IrControl_LearnedChannel(cmd);
    return Ir_SendLearnedVerified((uint8_t)channel);
}

uint8_t Ir_StartMatch(void)
{
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    Dev.errorCode.bit.irMatch = 0;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    IrBuf.type = IR_TYPE_MATCH;
    IrBuf.txbuf[0] = 0x30;
    IrBuf.txbuf[1] = 0x70;
    IrBuf.txbuf[2] = 0xA0;
    if(!Ir_TxStartCopy(IrBuf.txbuf, 3U, 1U)) {
        IrBuf.type = IR_TYPE_NORMAL;
        IrBuf.isFinish = 1U;
        return 0;
    }
    irOperationDeadline = CurTick + 25000U;
    IrReliabilityV2_RecordSubmitted(&irReliability);
    return 1;
}

uint8_t Ir_StartLearning(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM) return 0;
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    Dev.errorCode.bit.irLearn = 0;
    IrLearnChannel = ch;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    IrBuf.type = IR_TYPE_LEARNing;
    IrBuf.txbuf[0] = 0x30;
    IrBuf.txbuf[1] = 0x20;
    IrBuf.txbuf[2] = 0x50;
    if(!Ir_TxStartCopy(IrBuf.txbuf, 3U, 1U)) {
        IrBuf.type = IR_TYPE_NORMAL;
        IrBuf.isFinish = 1U;
        return 0;
    }
    irOperationDeadline = CurTick + 25000U;
    IrReliabilityV2_RecordSubmitted(&irReliability);
    return 1;
}

static uint8_t Ir_SubmitLearned(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM || !Dev.learnCode[ch].enable) return 0U;
    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.rxlen = 0U;
    irLastRxLen = 0U;
    if(!Ir_TxStartCopy(Dev.learnCode[ch].cmd, IR_LEARN_REPLAY_LEN, 0U)) {
        return 0U;
    }
    IrReliabilityV2_RecordSubmitted(&irReliability);
    #if _IR_INFO_
    PrintHex("ir learn send", IrBuf.txbuf, 10U);
    #endif
    return 1U;
}

uint8_t Ir_SendLearnedVerified(uint8_t ch)
{
    if(ch >= MAX_IR_LEARNNUM || !Dev.learnCode[ch].enable) return 0;
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    /* 学习码可能是切换/增减键，只发送一次，并由现场人员确认真实响应。 */
    return Ir_SubmitLearned(ch);
}

uint8_t Ir_CancelOperation(void)
{
    if(IrBuf.type != IR_TYPE_MATCH &&
       IrBuf.type != IR_TYPE_LEARNing &&
       IrBuf.type != IR_TYPE_RAW) {
        IrReliabilityV2_CancelRepeat(&irReliability);
        return 1;
    }
    Ir_TxAbort();
    IrReliabilityV2_CancelRepeat(&irReliability);
    IrBuf.type = IR_TYPE_NORMAL;
    IrBuf.isFinish = 1;
    IrBuf.rxlen = 0;
    irLastRxLen = 0;
    irOperationDeadline = 0U;
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
    if(ch >= MAX_IR_LEARNNUM || !Ir_IsControlPathIdle()) return 0;
    IrReliabilityV2_CancelRepeat(&irReliability);
    memset(&Dev.learnCode[ch], 0, sizeof(Dev.learnCode[ch]));
    Ir_RecalculateLearnNum();
    SaveIrInfo();
    return 1;
}

uint8_t Ir_ResetAllLearned(void)
{
    if(!Ir_IsControlPathIdle()) return 0;
    IrReliabilityV2_CancelRepeat(&irReliability);
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

uint8_t Ir_PrepareConfigurationChange(void)
{
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0U;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    return 1U;
}

uint8_t Ir_TransmitRawAsync(const uint8_t *data, uint16_t len)
{
    if(!Ir_IsControlPathIdle()) {
        IrReliabilityV2_RecordBusyRejected(&irReliability);
        return 0U;
    }
    IrReliabilityV2_CancelRepeat(&irReliability);
    IrBuf.type = IR_TYPE_RAW;
    IrBuf.rxlen = 0U;
    irLastRxLen = 0U;
    if(!Ir_TxStartCopy(data, len, 1U)) {
        IrBuf.type = IR_TYPE_NORMAL;
        IrBuf.isFinish = 1U;
        return 0U;
    }
    irOperationDeadline = CurTick + 3000U;
    IrReliabilityV2_RecordSubmitted(&irReliability);
    return 1U;
}

uint16_t Ir_GetSubmittedCount(void) { return irReliability.submitted_count; }
uint16_t Ir_GetRepeatedCount(void) { return irReliability.repeated_count; }
uint16_t Ir_GetBusyRejectedCount(void) { return irReliability.busy_rejected_count; }
uint8_t Ir_GetQueueDepth(void)
{
    return (irTxActive || !IrBuf.isFinish || irReliability.repeat_cmd != 0U)
        ? 1U
        : 0U;
}
uint8_t Ir_GetQueueHighWater(void) { return irPipelineHighWater; }
