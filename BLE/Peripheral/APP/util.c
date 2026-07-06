#include "board.h"
#include "timer.h"
#include "lora.h"

volatile uint16_t Timer_Lora = 0; //lora状态机时间计数,单位ms

// ===== 中继子节点管理 =====
#define RELAY_REG_FWD_TIMEOUT  50  // 注册转发超时 5s (50 * 100ms)

static child_info_t childTable[MAX_CHILD_NODES];
static uint8_t childCount = 0;

// 中继转发状态
static uint8_t relayRegFwdState = 0;  // 0=空闲, 1=注册转发中(等待网关回复)
static uint16_t pendingChildId = 0;
static uint16_t relayRegFwdTimer = 0;

// 中继子节点命令转发状态(非阻塞实现, 避免阻塞其他子节点)
static uint8_t relayFwdState = 0;       // 0=空闲, 1=已转发命令等待子节点响应
static uint16_t relayFwdTimer = 0;      // 等待计时(100ms单位)
static uint8_t relayFwdPendingLen = 0;  // 已转发命令的长度(暂存)

// 查找或添加子节点到内存表
static int8_t ChildTable_FindOrAdd(uint16_t nodeId) {
    for (uint8_t i = 0; i < childCount; i++) {
        if (childTable[i].nodeId == nodeId) return i;
    }
    if (childCount < MAX_CHILD_NODES) {
        childTable[childCount].nodeId = nodeId;
        childTable[childCount].online = 1;
        childTable[childCount].lastRssi = 0;
        childTable[childCount].lastSeenTs = LocalTimestamp;
        return childCount++;
    }
    return -1; // 表满
}

// 更新子节点状态
static void ChildTable_Update(uint16_t nodeId, uint8_t rssi) {
    int8_t idx = ChildTable_FindOrAdd(nodeId);
    if (idx >= 0) {
        childTable[idx].online = 1;
        childTable[idx].lastRssi = rssi;
        childTable[idx].lastSeenTs = LocalTimestamp;
    }
}

// 清除所有子节点的 online 标志(每个上报周期开始时调用)
static void ChildTable_ClearOnline(void) {
    for (uint8_t i = 0; i < childCount; i++) {
        childTable[i].online = 0;
    }
}

// 获取子节点位图
static uint16_t ChildTable_GetBitmap(void) {
    uint16_t bitmap = 0;
    for (uint8_t i = 0; i < childCount && i < 16; i++) {
        if (childTable[i].online) bitmap |= (1U << i);
    }
    return bitmap;
}

// 外部访问接口
uint8_t Relay_GetChildCount(void) {
    return childCount;
}

uint16_t Relay_GetChildBitmap(void) {
    return ChildTable_GetBitmap();
}

void Lora_Pro(void){
    static uint8_t LoraBuf[64] = {0};
    uint16_t irq = 0;
    uint8_t len,cmd,LoraTag;
    float operateParameter;
    Timer_Lora ++; //100ms

    if(!BITGET(Dev.mode,0)){ //bit0 lora不启用
        return;
    }

    // ===== 中继注册转发子状态（在主状态机之前处理）=====
    if(relayRegFwdState == 1){
        relayRegFwdTimer++;
        Lora_CheckData(LoraBuf, &len);
        if(len > 0 && ChkCrc(LoraBuf, len)){
            // 收到网关回复(注册频率)，检查是否是给pendingChildId的cmdConfig
            if(LoraBuf[0] == 7 && *(uint16_t*)(LoraBuf+8) == pendingChildId){
                // 切回工作频率，转发给子节点
                Lora_Init(Dev.loraFrequency, 22, LORA_SF_SCAN, LORA_BW_SCAN);
                Lora_Tx(LoraBuf, len);
                PRINT("Relay: fwd config to child %04x\n", pendingChildId);
                relayRegFwdState = 0;
                Dev.loraStatus = 4;
                Timer_Lora = Dev.scanCycle * 10;
                return;
            }
        }
        if(relayRegFwdTimer >= RELAY_REG_FWD_TIMEOUT){
            // 超时，切回工作频率
            Lora_Init(Dev.loraFrequency, 22, LORA_SF_SCAN, LORA_BW_SCAN);
            PRINT("Relay: reg fwd timeout for %04x\n", pendingChildId);
            relayRegFwdState = 0;
            Dev.loraStatus = 4;
            Timer_Lora = Dev.scanCycle * 10;
            Lora_Listening();
            return;
        }
        return; // 转发中，不进入主状态机
    }

    // ===== 主状态机 =====
    if(Dev.loraStatus == 1){ //还未注册：发送注册数据包
        if(Timer_Lora < (100 + ((Dev.nodeId*100)%30))){
            return;
        }
        if(Dev.linkRole == LINK_CHILD){
            // 中继子节点：在工作频率发注册包
            // [len=7][cmd=5][tag=0][nodeId(2)][hopCount=1][CRC]
            Dev.loraFrequency = Dev.channel * 0.3f + 420.05f;
            if(Dev.channel <= 22){
                Dev.loraFrequency += 3.1375f;
            }else{
                Dev.loraFrequency = (Dev.channel - 23) * 0.3f + 420.1875f;
            }
            LoraBuf[0] = 7;
            LoraBuf[1] = 5;  //cmdLogin
            LoraBuf[2] = 0;  //tag
            *(uint16_t*)(LoraBuf + 3) = Dev.nodeId;
            LoraBuf[5] = 1;  //hopCount=1
            AddCrc(LoraBuf, 6); //CRC放在buf[6]
            Lora_Init(Dev.loraFrequency, 22, LORA_SF_SCAN, LORA_BW_SCAN);
            PRINT("Relay child reg @work freq\n");
            Lora_Tx(LoraBuf, 7);
        }else{
            // 直连节点 或 中继节点自身: 在注册频率发5字节注册包
            *(LoraBuf) = 5;
            *(LoraBuf + 1) = 0;
            *(uint16_t*)(LoraBuf + 2) = Dev.nodeId;
            AddCrc(LoraBuf,4);
            Dev.loraFrequency = Dev.channel * 0.3f + 420.05f;
            Lora_Init(Dev.loraFrequency,22,LORA_SF_LISTEN,LORA_BW_LISTEN);
            Lora_Tx(LoraBuf,5);
        }
        Dev.loraStatus = 2;
        Timer_Lora = 0;

    }else if(Dev.loraStatus == 2){
        irq = Lora_GetIrqStatus();
        if (irq == IRQ_TX_DONE) {
            Lora_Listening();
            Dev.loraStatus = 3;
            Timer_Lora = 0;
        } else if ((irq > 0) || (Timer_Lora > 10)) {
            Dev.loraStatus = 1;
            Timer_Lora = 0;
            PRINT("Login tx error @%ld.\n",LocalTimestamp);
        }

    }else if(Dev.loraStatus == 3){ //等待注册反馈
        Lora_CheckData(LoraBuf,&len);
        if(len > 0){
            if(ChkCrc(LoraBuf,len) == 0){
                // CRC错误，超时后重试
                uint16_t recvTimeout = (Dev.linkRole == LINK_CHILD) ? 50 : 20; // 中继子5s, 直连2s
                if(Timer_Lora >= recvTimeout){
                    Timer_Lora = 0;
                    Dev.loraStatus = 1;
                    return;
                }
            }
            if((*LoraBuf == 7) && (*(uint16_t*)(LoraBuf+8) == Dev.nodeId)){
                Dev.scanCycle = (LoraBuf[16]<<8)|(LoraBuf[15]);
                if(Dev.scanCycle < 60){
                    Dev.scanCycle = 60;
                }else if(Dev.scanCycle > 180){
                    Dev.scanCycle = 180;
                }
                // 中继子节点: scanCycle强制下限90s(补偿多跳延迟)
                if(Dev.linkRole == LINK_CHILD && Dev.scanCycle < 90){
                    Dev.scanCycle = 90;
                }
                Dev.gatewayId = (LoraBuf[7]<<8)|(LoraBuf[6]);
                // 切换至工作频段
                if(Dev.channel <= 22){
                    Dev.loraFrequency = Dev.loraFrequency + 3.1375f;
                }else{
                    Dev.loraFrequency = (Dev.channel - 23) * 0.3f + 420.1875f;
                }
                PRINT("Login to %04x ,dataScycle:%d role=%d @%ld.\n",
                      Dev.gatewayId, Dev.scanCycle, Dev.linkRole, LocalTimestamp);
                Lora_Init(Dev.loraFrequency,22,LORA_SF_SCAN,LORA_BW_SCAN);
                Lora_Listening();
                Dev.loraStatus = 4;
                Timer_Lora = Dev.scanCycle * 10;
            }
        }else{
            uint16_t recvTimeout = (Dev.linkRole == LINK_CHILD) ? 50 : 20;
            if(Timer_Lora >= recvTimeout){
                Timer_Lora = 0;
                Dev.loraStatus = 1;
                PRINT("Login rx timeout @%ld.\n",LocalTimestamp);
                return;
            }
        }

    }else if(Dev.loraStatus == 4){ //监听网关指令
        Lora_CheckData(LoraBuf,&len);
        if(len > 0){
            if(ChkCrc(LoraBuf,len) == 1){
                cmd = LoraBuf[0];
                LoraTag = LoraBuf[1];

                // === 中继转发: 收到子节点注册包(工作频率) ===
                if(cmd == 5 && Dev.linkRole == LINK_RELAY){
                    uint16_t childNodeId = *(uint16_t*)(LoraBuf + 3);
                    // childNodeId == 0 表示已经过中继转发的回环包, 忽略
                    if(childNodeId == 0 || childNodeId == Dev.nodeId){
                        // 忽略(防止误处理)
                    }else if(LoraBuf[5] == 0){
                        // hopCount=0 表示这是直连节点的包(非子节点), 不应出现在工作频率, 忽略
                    }else if(relayFwdState == 1){
                        // 已经在等待某个子节点响应, 暂不处理新的注册请求
                    }else{
                        // 子节点发来的注册包(7字节)，转为5字节发到注册频率
                        PRINT("Relay: child %04x reg, fwd to reg freq\n", childNodeId);
                        pendingChildId = childNodeId;
                        // 构造标准5字节注册包
                        uint8_t regBuf[5];
                        regBuf[0] = 5;
                        regBuf[1] = 5;
                        regBuf[2] = 0;
                        *(uint16_t*)(regBuf + 3) = childNodeId;
                        AddCrc(regBuf, 4);
                        // 切到注册频率，转发
                        float regFreq = Dev.channel * 0.3f + 420.05f;
                        Lora_Init(regFreq, 22, LORA_SF_LISTEN, LORA_BW_LISTEN);
                        Lora_Tx(regBuf, 5);
                        // 进入注册转发等待状态
                        relayRegFwdState = 1;
                        relayRegFwdTimer = 0;
                        Dev.loraStatus = 4; // 保持Status4，由relayRegFwdState管理
                        return;
                    }
                }

                // === 中继转发: 收到子节点ACK(工作频率) ===
                // ACK包: [cmd=0x0E(1)][Tag(1)][nodeId(2)][execResult(1)][CRC(1)]
                // 不含gatewayId, 应在 gatewayId 校验前处理
                if(cmd == 0x0E && Dev.linkRole == LINK_RELAY){
                    uint16_t ackNodeId = *(uint16_t*)(LoraBuf + 2);
                    if(ackNodeId != Dev.nodeId){
                        // 子节点的ACK，转发给网关
                        PRINT("Relay: fwd ACK from %04x\n", ackNodeId);
                        Lora_Tx(LoraBuf, len);
                        Dev.loraStatus = 5;
                        Timer_Lora = 0;
                        return;
                    }
                }

                // 网关指令处理
                if(*(uint16_t*)(LoraBuf + 2) != Dev.gatewayId){
                    Lora_Listening();
                    return;
                }
                if(cmd == 0x0B){ //群控对时
                    if((*(uint32_t*)(LoraBuf+4) > LocalTimestamp + 3) || (LocalTimestamp > *(uint32_t*)(LoraBuf+4) + 3)){
                        PRINT("RTC update %ld -> %ld \n",LocalTimestamp,*(uint32_t*)(LoraBuf+4));
                        LocalTimestamp = *(uint32_t*)(LoraBuf+4);
                        RTC_SetTimestamp(LocalTimestamp);
                    }
                    // 中继: 群控对时也转发给子节点
                    if(Dev.linkRole == LINK_RELAY){
                        Lora_Tx(LoraBuf, len);
                        Dev.loraStatus = 5;
                        Timer_Lora = 0;
                    }else{
                        Lora_Listening();
                    }
                    return;
                }else if(cmd == 0x0f){ //群控下发指令
                    if(Dev.linkRole == LINK_RELAY){
                        Lora_Tx(LoraBuf, len);
                        Dev.loraStatus = 5;
                        Timer_Lora = 0;
                    }else{
                        Lora_Listening();
                    }
                }else{
                    uint16_t targetNodeId = *(uint16_t*)(LoraBuf + 4);
                    if(targetNodeId != Dev.nodeId){
                        // 目标不是自己
                        if(Dev.linkRole == LINK_RELAY){
                            // === 中继转发: 数据/控制指令 → 子节点 (非阻塞) ===
                            PRINT("Relay: fwd cmd %02x to %04x\n", cmd, targetNodeId);
                            relayFwdPendingLen = len;
                            relayFwdState = 1;
                            relayFwdTimer = 0;
                            Lora_Tx(LoraBuf, len);  // 转发给子节点
                            // 不进入Status5, 保持Status4, 下一轮Lora_Pro继续轮询
                            return;
                        }else{
                            Lora_Listening();
                        }
                    }else if(cmd == 13){ //针对本节点的命令
                        if(LoraTag == 1){ //网关下发控制指令
                            operateParameter = *(float*)(LoraBuf + 9);
                            if(LoraBuf[6] == 21){  //开机
                            }else if(LoraBuf[6] == 22){ //关机
                            }else if(LoraBuf[6] == 23){ //设定温度
                            }else if(LoraBuf[6] == 24){ //制冷/制热设定
                            }else if(LoraBuf[6] == 25){ //风机转速设定
                            }else if(LoraBuf[6] == 26){ //温度锁定
                            }else if(LoraBuf[6] == 27){ //管制设定-模式锁定
                            }else if(LoraBuf[6] == 28){ //温度补偿设定
                            }else if(LoraBuf[6] == 29){ //温度锁定上/下限设置
                            }
                            // 发送ACK (cmd=0x0E)
                            // 注: 当前firmware的IR控制为占位实现, ACK 暂不发送
                        } else {
                            LoraTag = 0;
                        }
                        // 上报数据（含hopCount，中继节点额外含childCount+childBitmap）
                        LoraBuf[0] = 1;  //ecmdOk
                        LoraBuf[1] = LoraTag;
                        LoraBuf[2] = Dev.nodeId&0xff;
                        LoraBuf[3] = (Dev.nodeId>>8)&0xff;
                        LoraBuf[4] = (uint8_t)Lora_GetRssi();
                        LoraBuf[5] = 0; //errorInfo
                        LoraBuf[6] = 0;
                        LoraBuf[7] = Dev.temSet;
                        LoraBuf[8] = 0; //OpStatus
                        LoraBuf[9] = 0;
                        LoraBuf[10] = Dev.tem;
                        LoraBuf[11] = 0;
                        LoraBuf[12] = Dev.ctlMode;
                        LoraBuf[13] = 0;
                        LoraBuf[14] = Dev.wind;
                        LoraBuf[15] = 0; // status code
                        LoraBuf[16] = 0;
                        LoraBuf[17] = (Dev.linkRole == LINK_CHILD) ? 1 : 0; // hopCount
                        uint8_t dataLen = 18; // 数据长度(不含CRC)
                        if(Dev.linkRole == LINK_RELAY){
                            LoraBuf[18] = childCount;
                            uint16_t bitmap = ChildTable_GetBitmap();
                            LoraBuf[19] = bitmap & 0xFF;
                            dataLen = 20;
                        }
                        AddCrc(LoraBuf, dataLen);
                        Lora_Tx(LoraBuf, dataLen + 1);
                        PRINT("Data to Gw:%04x @%ld\n",Dev.gatewayId,LocalTimestamp);
                        Dev.loraStatus = 5;
                        Timer_Lora = 0;
                    } else{
                        Lora_Listening();
                    }
                }
            } else{ //CRC错误
                Lora_Listening();
            }
        }
        // === 中继转发子状态处理: 等待子节点响应 ===
        if(Dev.linkRole == LINK_RELAY && relayFwdState == 1){
            relayFwdTimer++;
            // 收子节点响应 - 用 Lora_CheckData 安全获取
            uint16_t childIrq = Lora_GetIrqStatus();
            if(childIrq == IRQ_RX_DONE){
                // 收到子节点响应, 从 LoraBuf 读取
                Lora_CheckData(LoraBuf, &len);
                if(len > 0){
                    Lora_Listening(); // 重新进入监听
                    Lora_Tx(LoraBuf, len); // 转发给网关
                    uint16_t respNodeId = *(uint16_t*)(LoraBuf + 2);
                    ChildTable_Update(respNodeId, 0);
                    PRINT("Relay: fwd response from %04x\n", respNodeId);
                    relayFwdState = 0;
                    Dev.loraStatus = 5;
                    Timer_Lora = 0;
                    return;
                }
            }
            // 超时3s
            if(relayFwdTimer >= 30){
                PRINT("Relay: child response timeout\n");
                relayFwdState = 0;
                Lora_Listening();
            }
        }
        // 超时检测: 中继子节点不降级，普通节点重新注册
        if(Timer_Lora >= (Dev.scanCycle * 10 * 3)){
            if(Dev.linkRole == LINK_CHILD){
                // 中继子节点: 不降级，保持等待，只重置计时器
                Timer_Lora = Dev.scanCycle * 10;
                PRINT("Relay child: no cmd, keep waiting\n");
            }else{
                Dev.loraStatus = 1;
                Timer_Lora = 300;
                return;
            }
        }

    }else if(Dev.loraStatus == 5){ //发送完成检测
        irq = Lora_GetIrqStatus();
        if(irq == IRQ_TX_DONE || (Timer_Lora > 15)){
            Dev.loraStatus = 4;
            Timer_Lora = 0;
            Lora_Listening();
        }
    }
}



//uilt functions
void PrintHex(char *msg, uint8_t *buffer, uint16_t size){
    uint16_t i;
	if (buffer == NULL) {
		return;
	}
    if (msg != NULL) {
		PRINT("%s(%d bytes): ", msg, size);
	}
    for (i = 0; i < size; i++) {
		PRINT("%02x ", buffer[i]);
	}
	PRINT("\n");
}

//兼容普通节点板crc算法
void AddCrc(uint8_t *buf, uint16_t len) {
	uint8_t crcValue =0;
	uint16_t i;
	for (i=0; i<len; i++) {
		crcValue = crcValue + buf[i];
	}
	crcValue = crcValue + 0xec;
	*(buf + len) = crcValue;
	return;
}

int ChkCrc(uint8_t *buf, uint16_t len) {
	uint8_t crcValue =0;
	uint16_t i;
	if (len <= 1) {
		return 0;
	}
	for (i=0; i<len-1; i++) {
		crcValue = crcValue + buf[i];
	}
	crcValue = crcValue + 0xec;
	if (crcValue == buf[len - 1]) {
		return 1;
	} else {
		return 0;
	}
}