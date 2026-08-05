#include "gateway_lora_codec.h"

#include <string.h>

#define GATEWAY_LORA_CMD_LOGIN        0x05U
#define GATEWAY_LORA_CMD_RELAY_INNER  0xA5U
#define GATEWAY_LORA_CMD_DATA         0x01U

static void GatewayLora_PutU16Le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

uint8_t GatewayLora_Checksum(const uint8_t *data, uint16_t length)
{
    uint8_t checksum = 0U;
    uint16_t i;

    if(data == 0) return 0U;
    for(i = 0; i < length; i++) checksum = (uint8_t)(checksum + data[i]);
    return (uint8_t)(checksum + 0xECU);
}

uint8_t GatewayLora_Validate(const uint8_t *packet, uint16_t length)
{
    if(packet == 0 || length <= 1U) return 0U;
    return GatewayLora_Checksum(packet, length - 1U) == packet[length - 1U];
}

uint32_t GatewayLora_RegisterFrequencyHz(uint8_t channel)
{
    /*
     * 固定网关使用 Radio(0~4) + Frequency(0~9) 两级编号。
     * 小程序对现场只暴露一个频道号，channel = Radio * 10 + Frequency。
     */
    if(channel > GATEWAY_LORA_MAX_CHANNEL) channel = 0U;
    return 420050000UL + (uint32_t)channel * 300000UL;
}

uint32_t GatewayLora_WorkFrequencyHz(uint8_t channel)
{
    uint32_t registerFrequency;

    if(channel > GATEWAY_LORA_MAX_CHANNEL) channel = 0U;
    registerFrequency = GatewayLora_RegisterFrequencyHz(channel);
    return channel <= 22U
        ? registerFrequency + 3137500UL
        : 420187500UL + (uint32_t)(channel - 23U) * 300000UL;
}

uint8_t GatewayLora_EncodeSmallFloatX10(int16_t valueX10, uint16_t *encoded)
{
    int16_t integer;
    int16_t remainder;
    uint16_t fraction;

    /* 固定网关 util.c:SfloatToUint16 的有效范围为 -128.9..127.9。 */
    if(encoded == 0 || valueX10 < -1289 || valueX10 > 1279) return 0U;
    integer = valueX10 / 10; /* C99 起有符号除法向 0 截断，与网关 float->int8 一致。 */
    remainder = (int16_t)(valueX10 - integer * 10);
    if(remainder < 0) remainder = (int16_t)-remainder;
    fraction = (uint16_t)(((uint16_t)remainder * 127U + 5U) / 10U);
    if(valueX10 < 0) fraction |= 0x80U;
    *encoded = ((uint16_t)(uint8_t)(int8_t)integer << 8) | fraction;
    return 1U;
}

int16_t GatewayLora_DecodeSmallFloatX10(uint16_t encoded)
{
    int16_t integer = (int8_t)(encoded >> 8);
    int16_t fractionX10 = (int16_t)((((encoded & 0x7FU) * 10U) + 63U) / 127U);
    return (encoded & 0x80U) != 0U
        ? (int16_t)(integer * 10 - fractionX10)
        : (int16_t)(integer * 10 + fractionX10);
}

uint16_t GatewayLora_MapFancoilStatus(uint16_t internalFaults,
                                      uint8_t offlineAutonomyActive)
{
    uint16_t cloudFaults = 0U;

    /* D0 通信模块故障。 */
    if((internalFaults & GATEWAY_FANCOIL_INTERNAL_FAULT_LORA) != 0U) {
        cloudFaults |= (uint16_t)(1U << 0);
    }
    /* D1 红外模块故障：匹配与学习共用一个云端故障位。 */
    if((internalFaults & (GATEWAY_FANCOIL_INTERNAL_FAULT_IR_LEARN |
                          GATEWAY_FANCOIL_INTERNAL_FAULT_IR_MATCH)) != 0U) {
        cloudFaults |= (uint16_t)(1U << 1);
    }
    /* D2 状态检测故障：HLW8110/功率状态检测不可用。 */
    if((internalFaults & GATEWAY_FANCOIL_INTERNAL_FAULT_METER) != 0U) {
        cloudFaults |= (uint16_t)(1U << 2);
    }
    /* D9 温度检测故障。 */
    if((internalFaults & GATEWAY_FANCOIL_INTERNAL_FAULT_TEMP_ADC) != 0U) {
        cloudFaults |= (uint16_t)(1U << 9);
    }
    /* D10 脱机运行状态。 */
    if(offlineAutonomyActive != 0U) cloudFaults |= (uint16_t)(1U << 10);
    /* D11 存储参数故障。 */
    if((internalFaults & GATEWAY_FANCOIL_INTERNAL_FAULT_STORAGE) != 0U) {
        cloudFaults |= (uint16_t)(1U << 11);
    }
    return cloudFaults;
}

uint8_t GatewayLora_PackFancoilOperationStatus(uint8_t power,
                                               uint8_t mode,
                                               uint8_t fanSpeed)
{
    uint8_t operation;

    if(power == 0U) operation = 0U;
    else if(mode == 1U) operation = 1U; /* 制冷 */
    else if(mode == 4U) operation = 2U; /* 制热 */
    else if(mode == 3U) operation = 3U; /* 送风/新风 */
    else operation = 6U;                /* 自动、除湿等无法无损映射 */
    if(fanSpeed > 3U) fanSpeed = 0U;
    return (uint8_t)((operation << 4) | fanSpeed);
}

uint8_t GatewayLora_BuildFancoilReport(uint8_t *out,
                                      uint8_t tag,
                                      uint16_t nodeId,
                                      int8_t rssi,
                                      uint8_t errorInfo,
                                      const GatewayLoraFancoilState *state)
{
    if(out == 0 || state == 0 || nodeId == 0U) return 0U;

    out[0] = GATEWAY_LORA_CMD_DATA;
    out[1] = tag;
    GatewayLora_PutU16Le(out + 2, nodeId);
    out[4] = (uint8_t)rssi;
    out[5] = errorInfo;

    GatewayLora_PutU16Le(out + 6, state->set_temperature_sf);
    out[8] = state->operation_status;
    GatewayLora_PutU16Le(out + 9, state->room_temperature_sf);
    GatewayLora_PutU16Le(out + 11, state->fan_temperature_diff_sf);
    GatewayLora_PutU16Le(out + 13, state->valve_temperature_diff_sf);
    GatewayLora_PutU16Le(out + 15, state->status_code);
    out[17] = GatewayLora_Checksum(out, 17U);
    return GATEWAY_LORA_FANCOIL_REPORT_LENGTH;
}

uint8_t GatewayLora_BuildNodeLogin(uint8_t *out, uint16_t nodeId)
{
    if(out == 0 || nodeId == 0U) return 0U;
    out[0] = GATEWAY_LORA_CMD_LOGIN;
    out[1] = 0U;
    out[2] = (uint8_t)nodeId;
    out[3] = (uint8_t)(nodeId >> 8);
    out[4] = GatewayLora_Checksum(out, 4U);
    return 5U;
}

uint8_t GatewayLora_BuildChildLogin(uint8_t *out,
                                    uint16_t nodeId,
                                    uint16_t parentRelayId)
{
    if(out == 0 || nodeId == 0U || parentRelayId == 0U || nodeId == parentRelayId) return 0U;
    out[0] = GATEWAY_LORA_CMD_LOGIN;
    out[1] = 1U;
    out[2] = (uint8_t)nodeId;
    out[3] = (uint8_t)(nodeId >> 8);
    out[4] = (uint8_t)parentRelayId;
    out[5] = (uint8_t)(parentRelayId >> 8);
    out[6] = GatewayLora_Checksum(out, 6U);
    return 7U;
}

uint8_t GatewayLora_BuildRelayInner(uint8_t *out,
                                    uint8_t direction,
                                    uint16_t relayId,
                                    uint16_t childId,
                                    uint8_t sequence,
                                    const uint8_t *payload,
                                    uint8_t payloadLength)
{
    uint16_t packetLength = (uint16_t)payloadLength + 9U;

    if(out == 0 || payload == 0 || relayId == 0U || childId == 0U ||
       packetLength > GATEWAY_LORA_MAX_PACKET) {
        return 0U;
    }

    out[0] = GATEWAY_LORA_CMD_RELAY_INNER;
    out[1] = direction;
    out[2] = (uint8_t)relayId;
    out[3] = (uint8_t)(relayId >> 8);
    out[4] = (uint8_t)childId;
    out[5] = (uint8_t)(childId >> 8);
    out[6] = sequence;
    out[7] = payloadLength;
    memmove(out + 8, payload, payloadLength);
    out[8U + payloadLength] = GatewayLora_Checksum(out, (uint16_t)payloadLength + 8U);
    return (uint8_t)packetLength;
}

uint8_t GatewayLora_UnwrapRelayInnerInPlace(uint8_t *packet,
                                            uint8_t *length,
                                            uint8_t direction,
                                            uint16_t relayId,
                                            uint16_t childId,
                                            uint8_t *sequence)
{
    uint8_t payloadLength;
    uint16_t packetRelayId;
    uint16_t packetChildId;

    if(packet == 0 || length == 0 || *length < 9U ||
       packet[0] != GATEWAY_LORA_CMD_RELAY_INNER ||
       !GatewayLora_Validate(packet, *length) ||
       packet[1] != direction) {
        return 0U;
    }

    packetRelayId = (uint16_t)(packet[2] | ((uint16_t)packet[3] << 8));
    packetChildId = (uint16_t)(packet[4] | ((uint16_t)packet[5] << 8));
    payloadLength = packet[7];

    if(packetRelayId != relayId ||
       (packetChildId != childId && packetChildId != GATEWAY_LORA_RELAY_BROADCAST) ||
       (uint16_t)payloadLength + 9U != *length) {
        return 0U;
    }

    if(sequence != 0) *sequence = packet[6];
    memmove(packet, packet + 8, payloadLength);
    *length = payloadLength;
    return 1U;
}
