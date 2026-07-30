#include "board.h"
#include "timer.h"
#include "lora.h"
#include <string.h>

volatile uint32_t Timer_Lora = 0; // Lora state timer, LORA_POLL_INTERVAL_MS/tick

#define LORA_CMD_DATA              0x01
#define LORA_CMD_FAIL              0x02
#define LORA_CMD_LOGIN             0x05
#define LORA_CMD_CONFIG            0x07
#define LORA_CMD_TIME_SYNC         0x0B
#define LORA_CMD_OPERATE           0x0D
#define LORA_CMD_ACK               0x0E
#define LORA_CMD_GROUP_OPERATE     0x0F
#define LORA_CMD_RELAY_INNER       0xA5

#define RELAY_INNER_DOWN           0x01
#define RELAY_INNER_UP             0x02
#define RELAY_BROADCAST_CHILD_ID   0xFFFF
#define LORA_LOCAL_BUFFER_SIZE     128

#define RELAY_WAIT_TX_TIMEOUT      LORA_MS_TO_TICKS(1000)
#define RELAY_WAIT_GW_TIMEOUT      LORA_SEC_TO_TICKS(5)
#define RELAY_WAIT_CHILD_TIMEOUT   LORA_MS_TO_TICKS(1000)
#define RELAY_CHILD_OFFLINE_FACTOR 3

typedef enum {
    RELAY_STATE_IDLE = 0,
    RELAY_STATE_TX_REG_TO_GW,
    RELAY_STATE_WAIT_GW_CONFIG,
    RELAY_STATE_TX_CONFIG_TO_CHILD,
    RELAY_STATE_TX_TO_CHILD,
    RELAY_STATE_WAIT_CHILD_RESPONSE,
    RELAY_STATE_TX_TO_GW,
    RELAY_STATE_TX_BROADCAST
} relay_state_t;

static child_info_t childTable[MAX_CHILD_NODES];
static uint8_t childSlotCount = 0;

static relay_state_t relayState = RELAY_STATE_IDLE;
static uint16_t relayTimer = 0;
static uint16_t relayPendingChildId = 0;
static uint8_t relayPendingTag = 0;
static uint8_t relayPendingIsControl = 0;
static uint8_t relaySeq = 0;
static uint8_t relayPendingSeq = 0;
static uint8_t childLastInnerSeq = 0;

static void Relay_SendTargetFail(uint8_t *buf, uint8_t tag, uint16_t targetNodeId);

static float Lora_RegisterFreq(void)
{
    return Dev.channel * 0.3f + 420.05f;
}

static float Lora_WorkFreq(void)
{
    if(Dev.channel <= 22) {
        return Dev.channel * 0.3f + 420.05f + 3.1375f;
    }
    return (Dev.channel - 23) * 0.3f + 420.1875f;
}

static void Lora_SwitchRegisterFreq(void)
{
    Dev.loraFrequency = Lora_RegisterFreq();
    Lora_Init(Dev.loraFrequency, LORA_POWER, Dev.loraRegisterSf, Dev.loraRegisterBw);
}

static void Lora_SwitchWorkFreq(void)
{
    Dev.loraFrequency = Lora_WorkFreq();
    Lora_Init(Dev.loraFrequency, LORA_POWER, Dev.loraListenSf, Dev.loraListenBw);
}

static void Relay_ResetState(void)
{
    relayState = RELAY_STATE_IDLE;
    relayTimer = 0;
    relayPendingChildId = 0;
    relayPendingTag = 0;
    relayPendingIsControl = 0;
    relayPendingSeq = 0;
}

static uint8_t Relay_NextSeq(void)
{
    relaySeq++;
    if(relaySeq == 0) relaySeq = 1;
    return relaySeq;
}

static uint8_t BuildRelayInnerPacket(uint8_t *out,
                                     uint8_t direction,
                                     uint16_t relayId,
                                     uint16_t childId,
                                     uint8_t seq,
                                     const uint8_t *payload,
                                     uint8_t payloadLen)
{
    if((uint16_t)payloadLen + 9U > LORA_LOCAL_BUFFER_SIZE) return 0;

    out[0] = LORA_CMD_RELAY_INNER;
    out[1] = direction;
    out[2] = relayId & 0xFF;
    out[3] = (relayId >> 8) & 0xFF;
    out[4] = childId & 0xFF;
    out[5] = (childId >> 8) & 0xFF;
    out[6] = seq;
    out[7] = payloadLen;
    memmove(out + 8, payload, payloadLen);
    AddCrc(out, (uint16_t)payloadLen + 8U);
    return (uint8_t)(payloadLen + 9U);
}

static uint8_t RelayInner_UnwrapInPlace(uint8_t *buf,
                                        uint8_t *len,
                                        uint8_t direction,
                                        uint16_t relayId,
                                        uint16_t childId,
                                        uint8_t *seq)
{
    uint8_t payloadLen;
    uint16_t pktRelayId;
    uint16_t pktChildId;

    if(*len < 8 || buf[0] != LORA_CMD_RELAY_INNER) return 0;
    if(!ChkCrc(buf, *len)) return 0;
    if(buf[1] != direction) return 0;

    pktRelayId = (uint16_t)(buf[2] | (buf[3] << 8));
    pktChildId = (uint16_t)(buf[4] | (buf[5] << 8));
    payloadLen = buf[7];

    if(pktRelayId != relayId) return 0;
    if(pktChildId != childId && pktChildId != RELAY_BROADCAST_CHILD_ID) return 0;
    if((uint16_t)payloadLen + 9U != *len) return 0;

    if(seq != NULL) *seq = buf[6];
    memmove(buf, buf + 8, payloadLen);
    *len = payloadLen;
    return 1;
}

static uint8_t Relay_IsTxDoneOrTimeout(void)
{
    uint16_t irq = Lora_GetIrqStatus();
    if((irq & IRQ_TX_DONE) == IRQ_TX_DONE) {
        return 1;
    }
    if((irq & IRQ_RX_TX_TIMEOUT) == IRQ_RX_TX_TIMEOUT) {
        return 1;
    }
    return relayTimer >= RELAY_WAIT_TX_TIMEOUT;
}

static int8_t ChildTable_Find(uint16_t nodeId)
{
    for(uint8_t i = 0; i < childSlotCount; i++) {
        if(childTable[i].nodeId == nodeId) return (int8_t)i;
    }
    return -1;
}

static int8_t ChildTable_FindOrAdd(uint16_t nodeId)
{
    int8_t idx = ChildTable_Find(nodeId);
    if(idx >= 0) return idx;

    if(childSlotCount >= RELAY_MAX_ACTIVE_CHILD_NODES) {
        PRINT("Relay child table full, reject %04x\n", nodeId);
        return -1;
    }

    childTable[childSlotCount].nodeId = nodeId;
    childTable[childSlotCount].online = 1;
    childTable[childSlotCount].lastRssi = 0;
    childTable[childSlotCount].lastSeenTs = LocalTimestamp;
    return (int8_t)childSlotCount++;
}

static void ChildTable_Update(uint16_t nodeId, uint8_t rssi)
{
    if(nodeId == 0 || nodeId == Dev.nodeId) return;

    int8_t idx = ChildTable_FindOrAdd(nodeId);
    if(idx >= 0) {
        childTable[idx].online = 1;
        childTable[idx].lastRssi = rssi;
        childTable[idx].lastSeenTs = LocalTimestamp;
    }
}

static uint8_t ChildTable_IsOnline(uint8_t idx)
{
    uint32_t timeout = (uint32_t)Dev.scanCycle * RELAY_CHILD_OFFLINE_FACTOR;

    if(idx >= childSlotCount || !childTable[idx].online) return 0;
    if(timeout < 180) timeout = 180;
    if(LocalTimestamp >= childTable[idx].lastSeenTs &&
       (LocalTimestamp - childTable[idx].lastSeenTs) > timeout) {
        childTable[idx].online = 0;
        return 0;
    }
    return 1;
}

static uint16_t ChildTable_GetBitmap(void)
{
    uint16_t bitmap = 0;
    for(uint8_t i = 0; i < childSlotCount && i < 16; i++) {
        if(ChildTable_IsOnline(i)) bitmap |= (1U << i);
    }
    return bitmap;
}

uint8_t Relay_GetChildCount(void)
{
    uint8_t count = 0;
    for(uint8_t i = 0; i < childSlotCount; i++) {
        if(ChildTable_IsOnline(i)) count++;
    }
    return count;
}

uint16_t Relay_GetChildBitmap(void)
{
    return ChildTable_GetBitmap();
}

static uint8_t BuildLoginPacket(uint8_t *buf, uint16_t nodeId)
{
    buf[0] = LORA_CMD_LOGIN;
    buf[1] = 0;
    buf[2] = nodeId & 0xFF;
    buf[3] = (nodeId >> 8) & 0xFF;
    AddCrc(buf, 4);
    return 5;
}

static uint8_t BuildChildLoginPacket(uint8_t *buf, uint16_t nodeId, uint16_t parentRelayId)
{
    buf[0] = LORA_CMD_LOGIN;
    buf[1] = 1;
    buf[2] = nodeId & 0xFF;
    buf[3] = (nodeId >> 8) & 0xFF;
    buf[4] = parentRelayId & 0xFF;
    buf[5] = (parentRelayId >> 8) & 0xFF;
    AddCrc(buf, 6);
    return 7;
}

static uint8_t BuildFailPacket(uint8_t *buf, uint8_t tag, uint16_t nodeId)
{
    buf[0] = LORA_CMD_FAIL;
    buf[1] = tag;
    buf[2] = nodeId & 0xFF;
    buf[3] = (nodeId >> 8) & 0xFF;
    AddCrc(buf, 4);
    return 5;
}

static uint8_t BuildGatewayAckPacket(uint8_t *buf, uint8_t tag, uint16_t nodeId, uint8_t result)
{
    if(result != 0) {
        return BuildFailPacket(buf, tag, nodeId);
    }
    buf[0] = LORA_CMD_DATA;
    buf[1] = 8;
    buf[2] = nodeId & 0xFF;
    buf[3] = (nodeId >> 8) & 0xFF;
    AddCrc(buf, 4);
    return 5;
}

static uint8_t BuildDataPacket(uint8_t *buf, uint8_t tag, uint16_t nodeId, uint8_t errorInfo)
{
    uint8_t dataLen = 18;

    buf[0] = LORA_CMD_DATA;
    buf[1] = tag;
    buf[2] = nodeId & 0xFF;
    buf[3] = (nodeId >> 8) & 0xFF;
    buf[4] = (uint8_t)Lora_GetRssi();
    buf[5] = errorInfo;
    buf[6] = 0;
    buf[7] = Dev.temSet & 0xFF;
    buf[8] = 0;
    buf[9] = 0;
    buf[10] = (uint8_t)Dev.tem;
    buf[11] = 0;
    buf[12] = Dev.ctlMode;
    buf[13] = 0;
    buf[14] = Dev.wind;
    buf[15] = 0;
    buf[16] = 0;
    buf[17] = (Dev.linkRole == LINK_CHILD) ? 1 : 0;

    if(Dev.linkRole == LINK_RELAY) {
        uint16_t bitmap = ChildTable_GetBitmap();
        buf[18] = Relay_GetChildCount();
        buf[19] = bitmap & 0xFF;
        buf[20] = (bitmap >> 8) & 0xFF;
        dataLen = 21;
    }

    AddCrc(buf, dataLen);
    return dataLen + 1;
}

static uint8_t Lora_IsConfigForNode(uint8_t *buf, uint8_t len, uint16_t nodeId)
{
    return len >= 19 &&
           ChkCrc(buf, len) &&
           buf[0] == LORA_CMD_CONFIG &&
           ((uint16_t)(buf[8] | (buf[9] << 8)) == nodeId);
}

static uint16_t Lora_PacketNodeId(uint8_t *buf, uint8_t len)
{
    if(len < 4) return 0;
    if(buf[0] == LORA_CMD_LOGIN) return (uint16_t)(buf[2] | (buf[3] << 8));
    if(buf[0] == LORA_CMD_DATA) return (uint16_t)(buf[2] | (buf[3] << 8));
    if(buf[0] == LORA_CMD_ACK) return (uint16_t)(buf[2] | (buf[3] << 8));
    if(buf[0] == LORA_CMD_FAIL) return (uint16_t)(buf[2] | (buf[3] << 8));
    return 0;
}

static uint8_t Relay_ForwardChildLogin(uint8_t *rxBuf, uint8_t rxLen)
{
    uint8_t loginBuf[5];
    uint16_t childId;
    uint16_t parentRelayId;

    if(Dev.linkRole != LINK_RELAY || rxLen < 7 || rxBuf[0] != LORA_CMD_LOGIN) return 0;
    if(!ChkCrc(rxBuf, rxLen)) return 0;
    if(rxBuf[1] != 1) return 0;

    childId = (uint16_t)(rxBuf[2] | (rxBuf[3] << 8));
    parentRelayId = (uint16_t)(rxBuf[4] | (rxBuf[5] << 8));
    if(parentRelayId != Dev.nodeId) {
        Lora_Listening();
        return 1;
    }

    if(childId == 0 || childId == Dev.nodeId) {
        Lora_Listening();
        return 1;
    }
    if(ChildTable_Find(childId) < 0 && childSlotCount >= RELAY_MAX_ACTIVE_CHILD_NODES) {
        PRINT("Relay: reject child %04x, capacity=%d\n", childId, RELAY_MAX_ACTIVE_CHILD_NODES);
        Lora_Listening();
        return 1;
    }

    ChildTable_Update(childId, (uint8_t)Lora_GetRssi());
    relayPendingChildId = childId;
    BuildLoginPacket(loginBuf, childId);

    PRINT("Relay: child %04x login -> gw\n", childId);
    Lora_SwitchRegisterFreq();
    Lora_Tx(loginBuf, sizeof(loginBuf));
    relayState = RELAY_STATE_TX_REG_TO_GW;
    relayTimer = 0;
    return 1;
}

static void Relay_ForwardGatewayPacket(uint8_t *packet, uint8_t len, uint16_t targetNodeId, uint8_t tag)
{
    uint8_t wrapped[LORA_LOCAL_BUFFER_SIZE];
    uint8_t wrappedLen;

    relayPendingChildId = targetNodeId;
    relayPendingTag = tag;
    relayPendingIsControl = (tag == 1);
    relayPendingSeq = Relay_NextSeq();
    wrappedLen = BuildRelayInnerPacket(wrapped,
                                       RELAY_INNER_DOWN,
                                       Dev.nodeId,
                                       targetNodeId,
                                       relayPendingSeq,
                                       packet,
                                       len);
    if(wrappedLen == 0) {
        Relay_SendTargetFail(packet, tag, targetNodeId);
        return;
    }

    relayState = RELAY_STATE_TX_TO_CHILD;
    relayTimer = 0;
    PRINT("Relay: gw cmd %02x -> child %04x\n", packet[0], targetNodeId);
    Lora_Tx(wrapped, wrappedLen);
}

static void Relay_ForwardBroadcast(uint8_t *packet, uint8_t len)
{
    uint8_t wrapped[LORA_LOCAL_BUFFER_SIZE];
    uint8_t wrappedLen;

    wrappedLen = BuildRelayInnerPacket(wrapped,
                                       RELAY_INNER_DOWN,
                                       Dev.nodeId,
                                       RELAY_BROADCAST_CHILD_ID,
                                       Relay_NextSeq(),
                                       packet,
                                       len);
    if(wrappedLen == 0) {
        Lora_Listening();
        Relay_ResetState();
        return;
    }

    relayState = RELAY_STATE_TX_BROADCAST;
    relayTimer = 0;
    PRINT("Relay: broadcast cmd %02x\n", packet[0]);
    Lora_Tx(wrapped, wrappedLen);
}

static void Relay_SendChildTimeout(uint8_t *buf)
{
    uint8_t len;
    len = BuildFailPacket(buf, relayPendingTag, relayPendingChildId);
    PRINT("Relay: child %04x timeout -> gw\n", relayPendingChildId);
    Lora_Tx(buf, len);
    relayState = RELAY_STATE_TX_TO_GW;
    relayTimer = 0;
}

static void Relay_SendTargetFail(uint8_t *buf, uint8_t tag, uint16_t targetNodeId)
{
    uint8_t len = BuildFailPacket(buf, tag, targetNodeId);
    relayState = RELAY_STATE_TX_TO_GW;
    relayTimer = 0;
    PRINT("Relay: child %04x unavailable -> gw\n", targetNodeId);
    Lora_Tx(buf, len);
}

static uint8_t Relay_ProcessState(uint8_t *buf, uint8_t *len)
{
    uint8_t wrapped[LORA_LOCAL_BUFFER_SIZE];
    uint8_t wrappedLen;
    uint8_t respSeq;
    uint16_t respNodeId;

    if(relayState == RELAY_STATE_IDLE) return 0;

    relayTimer++;

    switch(relayState) {
        case RELAY_STATE_TX_REG_TO_GW:
            if(Relay_IsTxDoneOrTimeout()) {
                Lora_Listening();
                relayState = RELAY_STATE_WAIT_GW_CONFIG;
                relayTimer = 0;
            }
            return 1;

        case RELAY_STATE_WAIT_GW_CONFIG:
            Lora_CheckData(buf, len);
            if(*len > 0 && Lora_IsConfigForNode(buf, *len, relayPendingChildId)) {
                ChildTable_Update(relayPendingChildId, (uint8_t)Lora_GetRssi());
                wrappedLen = BuildRelayInnerPacket(wrapped,
                                                   RELAY_INNER_DOWN,
                                                   Dev.nodeId,
                                                   relayPendingChildId,
                                                   Relay_NextSeq(),
                                                   buf,
                                                   *len);
                if(wrappedLen == 0) {
                    Lora_SwitchWorkFreq();
                    Lora_Listening();
                    Relay_ResetState();
                    return 1;
                }
                Lora_SwitchWorkFreq();
                Lora_Tx(wrapped, wrappedLen);
                relayState = RELAY_STATE_TX_CONFIG_TO_CHILD;
                relayTimer = 0;
                PRINT("Relay: gw config -> child %04x\n", relayPendingChildId);
                return 1;
            }
            if(relayTimer >= RELAY_WAIT_GW_TIMEOUT) {
                PRINT("Relay: gw config timeout for %04x\n", relayPendingChildId);
                Lora_SwitchWorkFreq();
                Lora_Listening();
                Relay_ResetState();
            }
            return 1;

        case RELAY_STATE_TX_CONFIG_TO_CHILD:
        case RELAY_STATE_TX_BROADCAST:
            if(Relay_IsTxDoneOrTimeout()) {
                Lora_Listening();
                Relay_ResetState();
            }
            return 1;

        case RELAY_STATE_TX_TO_CHILD:
            if(Relay_IsTxDoneOrTimeout()) {
                Lora_Listening();
                relayState = RELAY_STATE_WAIT_CHILD_RESPONSE;
                relayTimer = 0;
            }
            return 1;

        case RELAY_STATE_WAIT_CHILD_RESPONSE:
            Lora_CheckData(buf, len);
            if(*len > 0 &&
               RelayInner_UnwrapInPlace(buf, len, RELAY_INNER_UP, Dev.nodeId, relayPendingChildId, &respSeq) &&
               respSeq == relayPendingSeq &&
               ChkCrc(buf, *len)) {
                respNodeId = Lora_PacketNodeId(buf, *len);
                if(respNodeId == relayPendingChildId) {
                    ChildTable_Update(respNodeId, (uint8_t)Lora_GetRssi());
                    Lora_Tx(buf, *len);
                    relayState = RELAY_STATE_TX_TO_GW;
                    relayTimer = 0;
                    PRINT("Relay: child %04x response -> gw\n", respNodeId);
                    return 1;
                }
            }
            if(relayTimer >= RELAY_WAIT_CHILD_TIMEOUT) {
                Relay_SendChildTimeout(buf);
            }
            return 1;

        case RELAY_STATE_TX_TO_GW:
            if(Relay_IsTxDoneOrTimeout()) {
                Lora_Listening();
                Relay_ResetState();
            }
            return 1;

        default:
            Relay_ResetState();
            Lora_Listening();
            return 1;
    }
}

static uint8_t ExecuteGatewayControl(uint8_t op, float param)
{
    switch(op) {
        case 21:
            Dev.onOff = PowerOn;
            Ir_RequestCmd(IR_CMD_POWER_ON);
            return 0;
        case 22:
            Dev.onOff = PowerOff;
            Ir_RequestCmd(IR_CMD_POWER_OFF);
            return 0;
        case 23: {
            uint8_t temp = (uint8_t)param;
            if(temp < 16) temp = 16;
            if(temp > 31) temp = 31;
            Dev.temSet = temp;
            Ir_RequestCmd((IR_CMD_t)(IR_CMD_TEMP_16 + (temp - 16)));
            return 0;
        }
        case 24: {
            uint8_t mode = (uint8_t)param;
            Dev.ctlMode = (Mode_t)mode;
            if(mode == Mode_Auto) Ir_RequestCmd(IR_CMD_MODE_AUTO);
            else if(mode == Mode_Cool) Ir_RequestCmd(IR_CMD_MODE_COOL);
            else if(mode == Mode_Dry) Ir_RequestCmd(IR_CMD_MODE_DRY);
            else if(mode == Mode_Fan) Ir_RequestCmd(IR_CMD_MODE_FAN);
            else if(mode == Mode_Heat) Ir_RequestCmd(IR_CMD_MODE_HEAT);
            else return 2;
            return 0;
        }
        case 25: {
            uint8_t wind = (uint8_t)param;
            Dev.wind = (Wind_t)wind;
            if(wind == Wind_Auto) Ir_RequestCmd(IR_CMD_FAN_AUTO);
            else if(wind == Wind_Low) Ir_RequestCmd(IR_CMD_FAN_LOW);
            else if(wind == Wind_Mid) Ir_RequestCmd(IR_CMD_FAN_MID);
            else if(wind == Wind_High) Ir_RequestCmd(IR_CMD_FAN_HIGH);
            else return 2;
            return 0;
        }
        default:
            return 2;
    }
}

static void Lora_TxNodeResponse(uint8_t *buf, uint8_t len)
{
    uint8_t wrapped[LORA_LOCAL_BUFFER_SIZE];
    uint8_t wrappedLen;

    if(Dev.linkRole == LINK_CHILD) {
        wrappedLen = BuildRelayInnerPacket(wrapped,
                                           RELAY_INNER_UP,
                                           Dev.parentRelayId,
                                           Dev.nodeId,
                                           childLastInnerSeq,
                                           buf,
                                           len);
        if(wrappedLen == 0) return;
        Lora_Tx(wrapped, wrappedLen);
        return;
    }

    Lora_Tx(buf, len);
}

static void Lora_HandleSelfCommand(uint8_t *buf, uint8_t len, uint8_t tag)
{
    uint8_t txLen;
    uint8_t result;
    float operateParameter = 0.0f;

    if(tag == 1) {
        if(len >= 13) {
            operateParameter = *(float*)(buf + 9);
        }
        result = ExecuteGatewayControl(buf[6], operateParameter);
        txLen = BuildGatewayAckPacket(buf, tag, Dev.nodeId, result);
    } else {
        txLen = BuildDataPacket(buf, tag, Dev.nodeId, 0);
    }

    Lora_TxNodeResponse(buf, txLen);
    Dev.loraStatus = Status_CheckData;
    Timer_Lora = 0;
}

void Lora_Pro(void)
{
    static uint8_t LoraBuf[LORA_LOCAL_BUFFER_SIZE] = {0};
    uint8_t len = 0;
    uint8_t cmd;
    uint8_t loraTag;
    uint16_t irq;
    uint16_t recvTimeout;
    uint16_t targetNodeId;
    int8_t targetChildIdx;

    Timer_Lora++;

    if(!BITGET(Dev.mode, 0)) {
        return;
    }

    if(Dev.linkRole != LINK_RELAY && relayState != RELAY_STATE_IDLE) {
        Relay_ResetState();
    }

    if(Relay_ProcessState(LoraBuf, &len)) {
        return;
    }

    if(Dev.loraStatus == Status_Logining) {
        if(Timer_Lora < (LORA_SEC_TO_TICKS(10) + ((Dev.nodeId * 17U) % LORA_SEC_TO_TICKS(3)))) {
            return;
        }

        if(Dev.linkRole == LINK_CHILD) {
            len = BuildChildLoginPacket(LoraBuf, Dev.nodeId, Dev.parentRelayId);
            Lora_SwitchWorkFreq();
            PRINT("Relay child login @work freq\n");
        } else {
            len = BuildLoginPacket(LoraBuf, Dev.nodeId);
            Lora_SwitchRegisterFreq();
        }
        Lora_Tx(LoraBuf, len);
        Dev.loraStatus = Status_CheckSend;
        Timer_Lora = 0;
        return;
    }

    if(Dev.loraStatus == Status_CheckSend) {
        irq = Lora_GetIrqStatus();
        if((irq & IRQ_TX_DONE) == IRQ_TX_DONE) {
            Lora_Listening();
            Dev.loraStatus = Status_RecvLogin;
            Timer_Lora = 0;
        } else if((irq & IRQ_RX_TX_TIMEOUT) || Timer_Lora > RELAY_WAIT_TX_TIMEOUT) {
            Dev.loraStatus = Status_Logining;
            Timer_Lora = 0;
            PRINT("Login tx timeout @%ld\n", LocalTimestamp);
        }
        return;
    }

    if(Dev.loraStatus == Status_RecvLogin) {
        Lora_CheckData(LoraBuf, &len);
        if(len > 0 && Dev.linkRole == LINK_CHILD) {
            if(!RelayInner_UnwrapInPlace(LoraBuf, &len, RELAY_INNER_DOWN, Dev.parentRelayId, Dev.nodeId, &childLastInnerSeq)) {
                Lora_Listening();
                return;
            }
        }
        if(len > 0 && Lora_IsConfigForNode(LoraBuf, len, Dev.nodeId)) {
            Dev.scanCycle = (LoraBuf[16] << 8) | LoraBuf[15];
            if(Dev.scanCycle < 60) Dev.scanCycle = 60;
            if(Dev.scanCycle > 180) Dev.scanCycle = 180;
            if(Dev.linkRole == LINK_CHILD && Dev.scanCycle < 90) Dev.scanCycle = 90;
            Dev.gatewayId = (LoraBuf[7] << 8) | LoraBuf[6];
            Lora_SwitchWorkFreq();
            Lora_Listening();
            Dev.loraStatus = Status_Connected;
            Timer_Lora = LORA_SEC_TO_TICKS(Dev.scanCycle);
            PRINT("Login gw:%04x scan:%d role:%d @%ld\n",
                  Dev.gatewayId, Dev.scanCycle, Dev.linkRole, LocalTimestamp);
            return;
        }

        recvTimeout = (Dev.linkRole == LINK_CHILD) ? RELAY_WAIT_GW_TIMEOUT : LORA_SEC_TO_TICKS(2);
        if(Timer_Lora >= recvTimeout) {
            Dev.loraStatus = Status_Logining;
            Timer_Lora = 0;
            PRINT("Login rx timeout @%ld\n", LocalTimestamp);
        }
        return;
    }

    if(Dev.loraStatus == Status_Connected) {
        Lora_CheckData(LoraBuf, &len);
        if(len > 0) {
            if(Dev.linkRole == LINK_CHILD) {
                if(!RelayInner_UnwrapInPlace(LoraBuf, &len, RELAY_INNER_DOWN, Dev.parentRelayId, Dev.nodeId, &childLastInnerSeq)) {
                    Lora_Listening();
                    return;
                }
            }

            if(!ChkCrc(LoraBuf, len)) {
                Lora_Listening();
                return;
            }

            cmd = LoraBuf[0];
            loraTag = LoraBuf[1];

            if(Relay_ForwardChildLogin(LoraBuf, len)) {
                return;
            }

            if(len >= 4 && ((uint16_t)(LoraBuf[2] | (LoraBuf[3] << 8)) != Dev.gatewayId)) {
                Lora_Listening();
                return;
            }

            if(cmd == LORA_CMD_TIME_SYNC) {
                if(len >= 9 &&
                   ((*(uint32_t*)(LoraBuf + 4) > LocalTimestamp + 3) ||
                    (LocalTimestamp > *(uint32_t*)(LoraBuf + 4) + 3))) {
                    PRINT("RTC update %ld -> %ld\n", LocalTimestamp, *(uint32_t*)(LoraBuf + 4));
                    LocalTimestamp = *(uint32_t*)(LoraBuf + 4);
                    RTC_SetTimestamp(LocalTimestamp);
                }
                if(Dev.linkRole == LINK_RELAY) Relay_ForwardBroadcast(LoraBuf, len);
                else Lora_Listening();
                return;
            }

            if(cmd == LORA_CMD_GROUP_OPERATE) {
                if(Dev.linkRole == LINK_RELAY) Relay_ForwardBroadcast(LoraBuf, len);
                else Lora_Listening();
                return;
            }

            if(cmd == LORA_CMD_OPERATE && len >= 6) {
                targetNodeId = (uint16_t)(LoraBuf[4] | (LoraBuf[5] << 8));
                if(targetNodeId == Dev.nodeId) {
                    Lora_HandleSelfCommand(LoraBuf, len, loraTag);
                } else if(Dev.linkRole == LINK_RELAY) {
                    targetChildIdx = ChildTable_Find(targetNodeId);
                    if(targetChildIdx < 0) {
                        Lora_Listening();
                    } else if(!ChildTable_IsOnline((uint8_t)targetChildIdx)) {
                        Relay_SendTargetFail(LoraBuf, loraTag, targetNodeId);
                    } else {
                        Relay_ForwardGatewayPacket(LoraBuf, len, targetNodeId, loraTag);
                    }
                } else {
                    Lora_Listening();
                }
                return;
            }

            Lora_Listening();
        }

        if(Timer_Lora >= LORA_SEC_TO_TICKS((uint32_t)Dev.scanCycle * 3U)) {
            if(Dev.linkRole == LINK_CHILD) {
                Timer_Lora = LORA_SEC_TO_TICKS(Dev.scanCycle);
                PRINT("Relay child: keep waiting\n");
            } else {
                Dev.loraStatus = Status_Logining;
                Timer_Lora = LORA_SEC_TO_TICKS(30);
            }
        }
        return;
    }

    if(Dev.loraStatus == Status_CheckData) {
        irq = Lora_GetIrqStatus();
        if((irq & IRQ_TX_DONE) == IRQ_TX_DONE || Timer_Lora > RELAY_WAIT_TX_TIMEOUT) {
            Dev.loraStatus = Status_Connected;
            Timer_Lora = 0;
            Lora_Listening();
        }
    }
}

void PrintHex(char *msg, uint8_t *buffer, uint16_t size)
{
    if(buffer == NULL) return;
    if(msg != NULL) PRINT("%s(%d bytes): ", msg, size);
    for(uint16_t i = 0; i < size; i++) {
        PRINT("%02x ", buffer[i]);
    }
    PRINT("\n");
}

void AddCrc(uint8_t *buf, uint16_t len)
{
    uint8_t crcValue = 0;
    for(uint16_t i = 0; i < len; i++) {
        crcValue = (uint8_t)(crcValue + buf[i]);
    }
    buf[len] = (uint8_t)(crcValue + 0xEC);
}

int ChkCrc(uint8_t *buf, uint16_t len)
{
    uint8_t crcValue = 0;
    if(len <= 1) return 0;
    for(uint16_t i = 0; i < len - 1; i++) {
        crcValue = (uint8_t)(crcValue + buf[i]);
    }
    crcValue = (uint8_t)(crcValue + 0xEC);
    return crcValue == buf[len - 1];
}
