#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gateway_lora_codec.h"

static void test_splitac_unified_report(void)
{
    GatewayLoraSplitAcState state = {
        0x1234U, 1U, 1U, 3U,
        250, 500U, 260U,
        2200U, 2500U, 5500U, 123U,
        0x0000U, 0x0000U
    };
    int16_t sf = 0;
    float f;
    uint8_t packet[GATEWAY_LORA_REPORT_LENGTH];

    assert(GATEWAY_LORA_REPORT_LENGTH == 38U);
    assert(GATEWAY_LORA_REPORT_DATA_LENGTH == 31U);
    assert(GatewayLora_BuildSplitAcReport(packet, 0U, 0x1234U, -40, 0U, &state) == 38U);
    assert(GatewayLora_Validate(packet, sizeof(packet)));

    assert(packet[0] == 0x01U && packet[1] == 0x00U);
    assert(packet[2] == 0x34U && packet[3] == 0x12U);
    assert(packet[4] == 0xD8U && packet[5] == 0x00U);
    assert(packet[6] == 0x34U && packet[7] == 0x12U);          /* 红外码 */
    assert(packet[8] == 0x01U && packet[9] == 0x01U && packet[10] == 0x03U);
    assert(packet[11] == 0x00U && packet[12] == 0x19U);        /* 温度 25.0 sf */
    assert(packet[13] == 0x00U && packet[14] == 0x32U);        /* 湿度 50.0 sf */
    assert(packet[15] == 0x00U && packet[16] == 0x1AU);        /* 设定 26.0 sf */
    memcpy(&f, packet + 17, 4); assert(f == 220.0f);           /* 电压 V */
    memcpy(&f, packet + 21, 4); assert(f == 2.5f);             /* 电流 A */
    memcpy(&f, packet + 25, 4); assert(f == 550.0f);           /* 功率 W */
    memcpy(&f, packet + 29, 4); assert(f > 12.29f && f < 12.31f); /* 电量 kWh */
    assert(packet[33] == 0x00U && packet[34] == 0x00U);
    assert(packet[35] == 0x00U && packet[36] == 0x00U);

    /* small-float 编解码边界 */
    assert(GatewayLora_EncodeSmallFloatX10(-325, (uint16_t *)&sf));
    assert(GatewayLora_DecodeSmallFloatX10((uint16_t)sf) == -325);
    assert(!GatewayLora_EncodeSmallFloatX10(1280, (uint16_t *)&sf));
    assert(GatewayLora_BuildSplitAcReport(packet, 0U, 0U, -40, 0U, &state) == 0U);
}

static void test_relay_is_also_a_normal_gateway_node(void)
{
    static const uint8_t expected[] = {0x05U, 0x00U, 0x01U, 0xBBU, 0xADU};
    uint8_t packet[16];

    assert(GatewayLora_BuildNodeLogin(packet, 0xBB01U) == sizeof(expected));
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    assert(GatewayLora_Validate(packet, sizeof(expected)));
}

static void test_channel_matches_fixed_gateway_radio_table(void)
{
    assert(GATEWAY_LORA_REGISTER_SF == 9U);
    assert(GATEWAY_LORA_REGISTER_BW == 0x04U);
    assert(GATEWAY_LORA_WORK_SF == 10U);
    assert(GATEWAY_LORA_WORK_BW == 0x05U);
    assert(GatewayLora_RegisterFrequencyHz(0U) == 420050000UL);
    assert(GatewayLora_WorkFrequencyHz(0U) == 423187500UL);
    assert(GatewayLora_RegisterFrequencyHz(9U) == 422750000UL);
    assert(GatewayLora_WorkFrequencyHz(9U) == 425887500UL);
    assert(GatewayLora_RegisterFrequencyHz(22U) == 426650000UL);
    assert(GatewayLora_WorkFrequencyHz(22U) == 429787500UL);
    assert(GatewayLora_RegisterFrequencyHz(23U) == 426950000UL);
    assert(GatewayLora_WorkFrequencyHz(23U) == 420187500UL);
    assert(GatewayLora_RegisterFrequencyHz(32U) == 429650000UL);
    assert(GatewayLora_WorkFrequencyHz(32U) == 422887500UL);

    /* 非法频道只作为防御性回退，正常配置层会在保存前拒绝。 */
    assert(GatewayLora_RegisterFrequencyHz(33U) == 420050000UL);
    assert(GatewayLora_WorkFrequencyHz(33U) == 423187500UL);
}

static void test_child_login_carries_parent_only_on_local_hop(void)
{
    static const uint8_t expected[] = {
        0x05U, 0x01U, 0x34U, 0x12U, 0x01U, 0xBBU, 0xF4U
    };
    uint8_t packet[16];

    assert(GatewayLora_BuildChildLogin(packet, 0x1234U, 0xBB01U) == sizeof(expected));
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    assert(GatewayLora_Validate(packet, sizeof(expected)));
    assert(!GatewayLora_BuildChildLogin(packet, 0xBB01U, 0xBB01U));
}

static void test_relay_inner_round_trip_and_rejection(void)
{
    static const uint8_t payload[] = {0x05U, 0x00U, 0x34U, 0x12U, 0x37U};
    uint8_t packet[GATEWAY_LORA_MAX_PACKET];
    uint8_t original[GATEWAY_LORA_MAX_PACKET];
    uint8_t length;
    uint8_t sequence = 0U;

    length = GatewayLora_BuildRelayInner(packet, 1U, 0xBB01U, 0x1234U,
                                         7U, payload, sizeof(payload));
    assert(length == sizeof(payload) + 9U);
    memcpy(original, packet, length);
    assert(GatewayLora_UnwrapRelayInnerInPlace(packet, &length, 1U,
                                               0xBB01U, 0x1234U, &sequence));
    assert(sequence == 7U);
    assert(length == sizeof(payload));
    assert(memcmp(packet, payload, sizeof(payload)) == 0);

    memcpy(packet, original, sizeof(payload) + 9U);
    length = sizeof(payload) + 9U;
    packet[length - 1U] ^= 0x01U;
    assert(!GatewayLora_UnwrapRelayInnerInPlace(packet, &length, 1U,
                                                0xBB01U, 0x1234U, &sequence));

    memcpy(packet, original, sizeof(payload) + 9U);
    length = sizeof(payload) + 9U;
    assert(!GatewayLora_UnwrapRelayInnerInPlace(packet, &length, 1U,
                                                0xBB01U, 0x5678U, &sequence));
}

int main(void)
{
    /* MQTT 管理帧也使用固定网关的一字节累加校验，不使用 BLE CRC16。 */
    static const uint8_t management[] = {0xC7,0x01,0x08,0x01,0x34,0x12,0x02,0x00,0x05,0x06,0x10};
    assert(GatewayLora_Validate(management, sizeof(management)));
    assert(GatewayLora_Checksum(management, sizeof(management) - 1U) == 0x10U);
    test_splitac_unified_report();
    test_relay_is_also_a_normal_gateway_node();
    test_channel_matches_fixed_gateway_radio_table();
    test_child_login_carries_parent_only_on_local_hop();
    test_relay_inner_round_trip_and_rejection();
    puts("gateway_lora_codec tests passed");
    return 0;
}
