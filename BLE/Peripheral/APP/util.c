#include "board.h"
#include "timer.h"
#include "lora.h"
#include "gateway_lora_codec.h"
#include "hlw8110.h"
#include "lora_recovery.h"
#include "relay_child_table.h"
#include "ml307r.h"
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
#define RELAY_INNER_DOWN           0x01
#define RELAY_INNER_UP             0x02
#define RELAY_BROADCAST_CHILD_ID   GATEWAY_LORA_RELAY_BROADCAST
#define LORA_LOCAL_BUFFER_SIZE     GATEWAY_LORA_MAX_PACKET

#define RELAY_WAIT_TX_TIMEOUT      LORA_MS_TO_TICKS(1000)
#define RELAY_WAIT_GW_TIMEOUT      LORA_SEC_TO_TICKS(5)
#define RELAY_WAIT_CHILD_TIMEOUT   LORA_MS_TO_TICKS(1000)
#define RELAY_CHILD_OFFLINE_FACTOR 3

#define LORA_TAG_READ              0U
#define LORA_TAG_FANCOIL_CONTROL   1U
#define LORA_TAG_SPLITAC_CONTROL   3U

#if DevType != 20
#error "Fixed six-value LoRa mapping requires eDeviceFancoil DevType 20"
#endif

#define LORA_RADIO_PROFILE_UNKNOWN  0U
#define LORA_RADIO_PROFILE_REGISTER 1U
#define LORA_RADIO_PROFILE_WORK     2U

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

static relay_child_table_t childTable;

static relay_state_t relayState = RELAY_STATE_IDLE;
static uint16_t relayTimer = 0;
static uint16_t relayPendingChildId = 0;
static uint8_t relayPendingTag = 0;
static uint8_t relayPendingIsControl = 0;
static uint8_t relaySeq = 0;
static uint8_t relayPendingSeq = 0;
static uint8_t childLastInnerSeq = 0;
static lora_recovery_t loraRecovery;
static uint8_t loraRadioProfile = LORA_RADIO_PROFILE_UNKNOWN;
static uint8_t loraRadioSf = 0;
static uint8_t loraRadioBw = 0;

static void Relay_SendTargetFail(uint8_t *buf, uint8_t tag, uint16_t targetNodeId);

static uint32_t Lora_GetU32Le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t Lora_RegisterFrequencyHz(void)
{
    return GatewayLora_RegisterFrequencyHz((uint8_t)Dev.channel);
}

static uint32_t Lora_WorkFrequencyHz(void)
{
    return GatewayLora_WorkFrequencyHz((uint8_t)Dev.channel);
}

static uint8_t Lora_SwitchRegisterFreq(void)
{
    uint32_t frequency = Lora_RegisterFrequencyHz();
    if(loraRadioProfile == LORA_RADIO_PROFILE_REGISTER &&
       !Dev.errorCode.bit.lora && Dev.loraFrequencyHz == frequency &&
       loraRadioSf == Dev.loraRegisterSf && loraRadioBw == Dev.loraRegisterBw) return 1;
    Dev.loraFrequencyHz = frequency;
    if(Lora_Init(frequency, LORA_POWER, Dev.loraRegisterSf, Dev.loraRegisterBw) != 0U) {
        loraRadioProfile = LORA_RADIO_PROFILE_UNKNOWN;
        return 0;
    }
    loraRadioProfile = LORA_RADIO_PROFILE_REGISTER;
    loraRadioSf = Dev.loraRegisterSf;
    loraRadioBw = Dev.loraRegisterBw;
    LoraRecovery_MarkSucceeded(&loraRecovery, CurTick);
    return 1;
}

static uint8_t Lora_SwitchWorkFreq(void)
{
    uint32_t frequency = Lora_WorkFrequencyHz();
    if(loraRadioProfile == LORA_RADIO_PROFILE_WORK &&
       !Dev.errorCode.bit.lora && Dev.loraFrequencyHz == frequency &&
       loraRadioSf == Dev.loraListenSf && loraRadioBw == Dev.loraListenBw) return 1;
    Dev.loraFrequencyHz = frequency;
    if(Lora_Init(frequency, LORA_POWER, Dev.loraListenSf, Dev.loraListenBw) != 0U) {
        loraRadioProfile = LORA_RADIO_PROFILE_UNKNOWN;
        return 0;
    }
    loraRadioProfile = LORA_RADIO_PROFILE_WORK;
    loraRadioSf = Dev.loraListenSf;
    loraRadioBw = Dev.loraListenBw;
    LoraRecovery_MarkSucceeded(&loraRecovery, CurTick);
    return 1;
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
    return GatewayLora_BuildRelayInner(out, direction, relayId, childId,
                                       seq, payload, payloadLen);
}

static uint8_t RelayInner_UnwrapInPlace(uint8_t *buf,
                                        uint8_t *len,
                                        uint8_t direction,
                                        uint16_t relayId,
                                        uint16_t childId,
                                        uint8_t *seq)
{
    return GatewayLora_UnwrapRelayInnerInPlace(buf, len, direction,
                                               relayId, childId, seq);
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

static uint32_t ChildTable_TimeoutMs(void)
{
    uint32_t timeout_ms =
        (uint32_t)Dev.scanCycle * RELAY_CHILD_OFFLINE_FACTOR * 1000U;
    return timeout_ms < 180000U ? 180000U : timeout_ms;
}

static int8_t ChildTable_Find(uint16_t nodeId)
{
    return RelayChildTable_Find(&childTable, nodeId);
}

static int8_t ChildTable_Update(uint16_t nodeId, uint8_t rssi)
{
    if(nodeId == 0U || nodeId == Dev.nodeId) return -1;
    return RelayChildTable_Touch(&childTable, nodeId, rssi, CurTick,
                                   ChildTable_TimeoutMs());
}

static uint8_t ChildTable_IsOnline(uint8_t idx)
{
    return RelayChildTable_IsOnline(&childTable, idx, CurTick,
                                      ChildTable_TimeoutMs());
}

static uint16_t ChildTable_GetBitmap(void)
{
    return RelayChildTable_OnlineBitmap(&childTable, CurTick,
                                          ChildTable_TimeoutMs());
}

uint8_t Relay_GetChildCount(void)
{
    return RelayChildTable_OnlineCount(&childTable, CurTick,
                                         ChildTable_TimeoutMs());
}

uint16_t Relay_GetChildBitmap(void)
{
    return ChildTable_GetBitmap();
}

static uint8_t BuildLoginPacket(uint8_t *buf, uint16_t nodeId)
{
    return GatewayLora_BuildNodeLogin(buf, nodeId);
}

static uint8_t BuildChildLoginPacket(uint8_t *buf, uint16_t nodeId, uint16_t parentRelayId)
{
    return GatewayLora_BuildChildLogin(buf, nodeId, parentRelayId);
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
    GatewayLoraFancoilState state;
    const HLW8110_Status_t *meter = HLW8110_GetStatus();
    int16_t roomTemp = Dev.roomTempX10;
    int16_t setTemp = (int16_t)(Dev.temSet * 10U);

    if(Dev.temSet < 16U || Dev.temSet > 32U ||
       !GatewayLora_EncodeSmallFloatX10(setTemp, &state.set_temperature_sf)) {
        state.set_temperature_sf = 0U;
    }
    if(!GatewayLora_EncodeSmallFloatX10(roomTemp, &state.room_temperature_sf)) {
        state.room_temperature_sf = 0U;
    }
    state.power_setting = Dev.onOff == PowerOn ? 1U : 0U;
    state.work_mode_sf = (uint16_t)(uint8_t)Dev.ctlMode << 8;
    state.fan_speed_sf = (uint16_t)(uint8_t)Dev.wind << 8;
    state.run_feedback = meter->valid ? meter->current_ma : 0U;

    return GatewayLora_BuildFancoilReport(buf, tag, nodeId,
                                          Lora_GetRssi(), errorInfo, &state);
}

uint8_t Lora_BuildNodeReport(uint8_t *buf, uint8_t tag, uint8_t errorInfo)
{
    return BuildDataPacket(buf, tag, Dev.nodeId, errorInfo);
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
    if(ChildTable_Update(childId, (uint8_t)Lora_GetRssi()) < 0) {
        PRINT("Relay: reject child %04x, capacity=%d\n", childId, RELAY_MAX_ACTIVE_CHILD_NODES);
        Lora_Listening();
        return 1;
    }

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
    relayPendingIsControl = (tag == LORA_TAG_FANCOIL_CONTROL ||
                             tag == LORA_TAG_SPLITAC_CONTROL);
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

static uint8_t ExecuteGatewayControl(uint8_t op, uint16_t operateTag, uint32_t parameterValue)
{
    IR_CMD_t command;
    uint16_t value = 0U;

    /* 仅接受云端定义的原子操作；21/1~4 等联动指令不执行。 */
    switch(op) {
        case 21:
            if(operateTag != 0U) return 2;
            command = IR_CMD_POWER_ON;
            break;
        case 22:
            if(operateTag != 0U) return 2;
            command = IR_CMD_POWER_OFF;
            break;
        case 23:
            if(operateTag != 0U || parameterValue < 16U || parameterValue > 31U) return 2;
            value = (uint16_t)parameterValue;
            command = (IR_CMD_t)(IR_CMD_TEMP_16 + (value - 16U));
            break;
        case 24:
            if(operateTag > (uint16_t)Mode_Heat) return 2;
            value = operateTag;
            command = (IR_CMD_t)(IR_CMD_MODE_AUTO + value);
            break;
        case 25:
            if(operateTag > (uint16_t)Wind_High) return 2;
            value = operateTag;
            command = (IR_CMD_t)(IR_CMD_FAN_AUTO + value);
            break;
        case 26:
            if(operateTag != 0U || parameterValue < 1U || parameterValue > 0xFFFEU) return 2;
            value = (uint16_t)parameterValue;
            if(Dev.irActType != ACT_TYPE_IR || Dev.irIdx >= IR_BRAND_COUNT) return 2;
            if(!Ir_PrepareConfigurationChange()) return 1;
            Dev.irType = value;
            Dev.errorCode.bit.irMatch = 0;
            SaveDevInfo(50u);
            return 0;
        case 27:
            if(operateTag != 0U || parameterValue != 0U) return 2;
            return Meter_ClearEnergy() == 0U ? 0U : 1U;
        default:
            return 2;
    }

    /* 学习模式只映射具备明确语义的开关、模式键；绝对温度/风速明确拒绝。 */
    if(!Ir_ConfiguredCommandSupported(command)) return 2;
    /* 先确认红外配置有效且命令已提交，再更新对外状态，避免云端假成功。 */
    if(!Ir_ExecuteConfiguredVerified(command)) return 1;

    switch(op) {
        case 21:
            if(Dev.onOff != PowerOn) {
                if(Dev.meter.onoff_count != 0xFFFFU) Dev.meter.onoff_count++;
                Dev.lastPowerChange = LocalTimestamp;
            }
            Dev.onOff = PowerOn;
            Dev.lastOnTime = LocalTimestamp;
            break;
        case 22:
            if(Dev.onOff != PowerOff) {
                if(Dev.meter.onoff_count != 0xFFFFU) Dev.meter.onoff_count++;
                Dev.lastPowerChange = LocalTimestamp;
            }
            Dev.onOff = PowerOff;
            break;
        case 23:
            Dev.temSet = (uint8_t)value;
            break;
        case 24:
            Dev.ctlMode = (Mode_t)value;
            break;
        case 25:
            Dev.wind = (Wind_t)value;
            break;
        default:
            return 2;
    }
    /* 延迟并合并运行状态写入，不改变固定网关报文和 LoRa 轮询热路径。 */
    SaveDevInfo(50u);
    Ml307_RequestReport();
    return 0;
}

static uint8_t Lora_ParseGatewayControl(const uint8_t *buf,
                                        uint8_t len,
                                        uint8_t tag,
                                        uint8_t *operation,
                                        uint16_t *operateTag,
                                        uint32_t *parameterValue,
                                        uint32_t *token)
{
    const uint8_t *parameterPtr;

    if(tag == LORA_TAG_FANCOIL_CONTROL && len == 18U) {
        /* eDeviceFancoil: operate@6, operateTag@7, parameter@9, token@13。 */
        *operation = buf[6];
        *operateTag = (uint16_t)buf[7] | ((uint16_t)buf[8] << 8);
        parameterPtr = buf + 9;
        *token = Lora_GetU32Le(buf + 13);
    } else if(tag == LORA_TAG_SPLITAC_CONTROL && len == 22U) {
        /* 保留旧 tag=3 解析，便于现场切换云端类型时明确返回结果。 */
        *operation = buf[10];
        *operateTag = (uint16_t)buf[11] | ((uint16_t)buf[12] << 8);
        parameterPtr = buf + 13;
        *token = Lora_GetU32Le(buf + 17);
    } else {
        return 0;
    }

    *parameterValue = Lora_GetU32Le(parameterPtr);
    return 1;
}

uint8_t Lora_ExecuteNodeControl(const uint8_t *buf, uint8_t len,
                               uint8_t allow_zero_gateway)
{
    uint32_t parameterValue;
    uint32_t token;
    uint16_t operateTag;
    uint16_t targetNodeId;
    uint8_t operation;
    uint8_t result;
    uint8_t tag;

    if(!buf || len < 6U || !GatewayLora_Validate(buf, len) || buf[0] != LORA_CMD_OPERATE)
        return 0xFFU;
    tag = buf[1];
    targetNodeId = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    if((((uint16_t)buf[2] | ((uint16_t)buf[3] << 8)) != Dev.gatewayId &&
        !(allow_zero_gateway && buf[2] == 0U && buf[3] == 0U)) ||
       targetNodeId != Dev.nodeId ||
       (tag != LORA_TAG_FANCOIL_CONTROL && tag != LORA_TAG_SPLITAC_CONTROL) ||
       !Lora_ParseGatewayControl(buf, len, tag, &operation, &operateTag,
                                 &parameterValue, &token)) {
        return 0xFFU;
    }

    /* LoRa and MQTT both use command-as-action semantics. The fixed protocol
     * token is retained on the wire, but never suppresses an operation. */
    result = ExecuteGatewayControl(operation, operateTag, parameterValue);
    PRINT("Control: op=%d operateTag=%u token=%08lx result=%d\n",
          operation, operateTag, (unsigned long)token, result);
    return result;
}

uint8_t Lora_BuildControlResult(uint8_t *buf, uint8_t tag, uint8_t result)
{
    if(!buf) return 0U;
    if(tag == LORA_TAG_FANCOIL_CONTROL) {
        return result == 0U
            ? Lora_BuildNodeReport(buf, tag, 0U)
            : BuildFailPacket(buf, tag, Dev.nodeId);
    }
    if(tag == LORA_TAG_SPLITAC_CONTROL)
        return BuildGatewayAckPacket(buf, tag, Dev.nodeId, result);
    return 0U;
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

    if(tag == LORA_TAG_FANCOIL_CONTROL || tag == LORA_TAG_SPLITAC_CONTROL) {
        result = Lora_ExecuteNodeControl(buf, len, 0U);
        txLen = Lora_BuildControlResult(buf, tag, result);
    } else {
        txLen = Lora_BuildNodeReport(buf, tag, 0U);
    }

    Lora_TxNodeResponse(buf, txLen);
    Dev.lastReportTime = LocalTimestamp;
    Dev.loraStatus = Status_CheckData;
    Timer_Lora = 0;
}

void Lora_Pro(void)
{
    static uint8_t LoraBuf[LORA_LOCAL_BUFFER_SIZE] = {0};
    static uint16_t observedNodeId = 0;
    static uint16_t observedChannel = 0xFFFFu;
    static uint8_t observedRole = 0xFFu;
    static uint8_t observedConfigValid = 0;
    uint8_t len = 0;
    uint8_t cmd;
    uint8_t loraTag;
    uint16_t irq;
    uint16_t recvTimeout;
    uint16_t targetNodeId;
    int8_t targetChildIdx;

    Timer_Lora++;

    if(observedNodeId != Dev.nodeId || observedChannel != Dev.channel || observedRole != Dev.linkRole) {
        Relay_ResetState();
        RelayChildTable_Init(&childTable, RELAY_MAX_ACTIVE_CHILD_NODES);
        childLastInnerSeq = 0;
        observedNodeId = Dev.nodeId;
        observedChannel = Dev.channel;
        observedRole = Dev.linkRole;
        loraRadioProfile = LORA_RADIO_PROFILE_UNKNOWN;
        LoraRecovery_Init(&loraRecovery, CurTick);

        /*
         * 配置提交后不能继续沿用旧频道/旧身份的 Connected 会话。
         * 中继与直连都重新走注册频点；子节点则重新在工作频点向父中继登录。
         * 将计时器预置到登录门限，使现场保存配置后立即生效，无需重启设备。
        */
        Dev.loraStatus = Status_Logining;
        Timer_Lora = observedConfigValid ? LORA_SEC_TO_TICKS(13) : 0;
        observedConfigValid = 1;
        PRINT("LoRa config changed: node=%04x channel=%d role=%d, relogin\n",
              Dev.nodeId, Dev.channel, Dev.linkRole);
    }

    /*
     * BUSY/SPI 异常后禁止继续向故障模块发送命令；按 5/10/20/40/60 秒退避重置。
     * 失败时保持本地规则与 BLE 可用，避免缺失射频模块导致 MCU 周期性整机复位。
     */
    if(Dev.errorCode.bit.lora) {
        Relay_ResetState();
        loraRadioProfile = LORA_RADIO_PROFILE_UNKNOWN;
        Dev.loraStatus = Status_Logining;
        if(!LoraRecovery_ShouldAttempt(&loraRecovery, CurTick)) return;

        PRINT("LoRa recovery attempt\n");
        if((Dev.linkRole == LINK_CHILD ? Lora_SwitchWorkFreq() : Lora_SwitchRegisterFreq()) == 0U) {
            LoraRecovery_MarkFailed(&loraRecovery, CurTick);
            PRINT("LoRa recovery failed, retry in %lu ms\n",
                  (unsigned long)LoraRecovery_CurrentDelayMs(&loraRecovery));
            return;
        }

        Timer_Lora = LORA_SEC_TO_TICKS(13);
        PRINT("LoRa recovery succeeded, relogin\n");
        return;
    }

    /*
     * Dev.mode 表示本地规则/远程控制策略，不是 LoRa 电源开关。
     * 本地模式仍须注册、上报并接收对时；中继节点还必须持续为子节点转发。
     * 旧逻辑在 mode=0 时直接退出，会让中继自身和其全部子节点一起离线。
     */

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
            PRINT("LoRa node login: node=%04x role=%d @register freq\n",
                  Dev.nodeId, Dev.linkRole);
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
                uint32_t gatewayTimestamp = len >= 9 ? Lora_GetU32Le(LoraBuf + 4) : LocalTimestamp;
                if(len >= 9 &&
                   ((gatewayTimestamp > LocalTimestamp + 3) ||
                    (LocalTimestamp > gatewayTimestamp + 3))) {
                    PRINT("RTC update %ld -> %ld\n", LocalTimestamp, gatewayTimestamp);
                    LocalTimestamp = gatewayTimestamp;
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
                /* 中继本身也是一台空调控制器，自身 ID 的轮询优先本地处理。 */
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
            /*
             * 三个轮询周期没有收到网关命令，所有角色都重新注册。
             * 子节点不能永久停在 Connected：中继重启后其 RAM 子表为空，只有
             * 子节点重新发送登录包，才能重建转发表并恢复离线自治判定。
             */
            Dev.loraStatus = Status_Logining;
            Timer_Lora = LORA_SEC_TO_TICKS(30);
            PRINT("LoRa silent for 3 cycles, relogin role=%d\n", Dev.linkRole);
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
    if(buf != NULL) buf[len] = GatewayLora_Checksum(buf, len);
}

int ChkCrc(uint8_t *buf, uint16_t len)
{
    return GatewayLora_Validate(buf, len);
}
