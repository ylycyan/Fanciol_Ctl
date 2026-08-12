/**
 * @file gateway_lora_codec.c
 * @brief 固定网关 LoRa 协议编解码
 *
 * 帧格式：`[CMD][Payload...][CRC]`，CRC = (所有前置字节之和 + 0xEC) & 0xFF。
 * 命令类型：LOGIN(0x05) 登录、DATA(0x01) 数据、RELAY_INNER(0xA5) 中继内层封装。
 * 频率规划：注册频点 420.05MHz + channel×0.3MHz；
 *          工作频点 channel<=22 为注册频点+3.1375MHz，否则 420.1875MHz + (channel-23)×0.3MHz。
 */
#include "gateway_lora_codec.h"

#include <string.h>

#define GATEWAY_LORA_CMD_LOGIN        0x05U
#define GATEWAY_LORA_CMD_RELAY_INNER  0xA5U
#define GATEWAY_LORA_CMD_DATA         0x01U

/**
 * @brief 写小端 16 位
 */
static void GatewayLora_PutU16Le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

/**
 * @brief 计算协议校验和：前 length 字节求和 + 0xEC
 */
uint8_t GatewayLora_Checksum(const uint8_t *data, uint16_t length)
{
    uint8_t checksum = 0U;
    uint16_t i;

    if(data == 0) return 0U;
    for(i = 0; i < length; i++) checksum = (uint8_t)(checksum + data[i]);
    return (uint8_t)(checksum + 0xECU);
}

/**
 * @brief 校验整包：末字节须等于前 length-1 字节的校验和
 */
uint8_t GatewayLora_Validate(const uint8_t *packet, uint16_t length)
{
    if(packet == 0 || length <= 1U) return 0U;
    return GatewayLora_Checksum(packet, length - 1U) == packet[length - 1U];
}

/**
 * @brief 计算注册频点：420.05MHz + channel × 0.3MHz（channel 越界时按 0 处理）
 */
uint32_t GatewayLora_RegisterFrequencyHz(uint8_t channel)
{
    /*
     * 固定网关使用 Radio(0~4) + Frequency(0~9) 两级编号。
     * 小程序对现场只暴露一个频道号，channel = Radio * 10 + Frequency。
     */
    if(channel > GATEWAY_LORA_MAX_CHANNEL) channel = 0U;
    return 420050000UL + (uint32_t)channel * 300000UL;
}

/**
 * @brief 计算工作频点（channel<=22 偏移 +3.1375MHz，否则按独立频段）
 */
uint32_t GatewayLora_WorkFrequencyHz(uint8_t channel)
{
    uint32_t registerFrequency;

    if(channel > GATEWAY_LORA_MAX_CHANNEL) channel = 0U;
    registerFrequency = GatewayLora_RegisterFrequencyHz(channel);
    return channel <= 22U
        ? registerFrequency + 3137500UL
        : 420187500UL + (uint32_t)(channel - 23U) * 300000UL;
}

/**
 * @brief 将 0.1℃ 温度编码为网关 small-float（-128.9℃~127.9℃）
 *
 * 高字节为有符号整数部分，低字节低 7 位为分数（×127），bit7 为符号位。
 */
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

/**
 * @brief 将网关 small-float 解码为 0.1℃ 温度
 */
int16_t GatewayLora_DecodeSmallFloatX10(uint16_t encoded)
{
    int16_t integer = (int8_t)(encoded >> 8);
    int16_t fractionX10 = (int16_t)((((encoded & 0x7FU) * 10U) + 63U) / 127U);
    return (encoded & 0x80U) != 0U
        ? (int16_t)(integer * 10 - fractionX10)
        : (int16_t)(integer * 10 + fractionX10);
}

/**
 * @brief 构建风机盘管状态上报帧（eDeviceFancoil 类型，固定 18 字节）
 *
 * 布局：CMD(1) + TAG(1) + NodeId(2) + RSSI(1) + errorInfo(1) + 6 个值(11 字节) + CRC(1)
 * 6 个值依次为：设定温度(sf) / 开关(u8) / 环境温度(sf) / 模式(sf) / 风速(sf) / 负载电流 mA(u16)
 */
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
    out[8] = state->power_setting;
    GatewayLora_PutU16Le(out + 9, state->room_temperature_sf);
    GatewayLora_PutU16Le(out + 11, state->work_mode_sf);
    GatewayLora_PutU16Le(out + 13, state->fan_speed_sf);
    GatewayLora_PutU16Le(out + 15, state->run_feedback);
    out[17] = GatewayLora_Checksum(out, 17U);
    return GATEWAY_LORA_FANCOIL_REPORT_LENGTH;
}

/**
 * @brief 构建普通节点登录帧（5 字节）：CMD + 角色(0=普通) + NodeId + CRC
 */
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

/**
 * @brief 构建子节点登录帧（7 字节）：带 role=1 与父中继 ID
 */
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

/**
 * @brief 构建中继内层封装帧（下行方向）
 *
 * 结构：CMD(0xA5) + direction + RelayId(2) + ChildId(2) + seq(1) + payloadLen(1) + payload + CRC
 * 返回总长度；超出 GATEWAY_LORA_MAX_PACKET 时返回 0。
 */
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

/**
 * @brief 解包中继内层帧（原地）：校验后把 payload 前移到包首
 *
 * 校验：CMD/方向/中继ID/子节点ID（或广播地址）/长度一致性/CRC。
 * 成功后 *length 变为 payload 长度，payload 位于 packet 起始处。
 */
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
