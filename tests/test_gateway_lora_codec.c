#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gateway_lora_codec.h"

static void test_fancoil_type20_fixed_report(void)
{
    static const uint8_t expected[] = {
        0x01U, 0x00U, 0x34U, 0x12U, 0xD8U, 0x00U,
        0x00U, 0x1AU, 0x01U, 0x00U, 0x19U,
        0x00U, 0x01U, 0x00U, 0x03U, 0xD2U, 0x04U, 0x19U
    };
    GatewayLoraFancoilState state = {
        0x1A00U, 1U, 0x1900U, 0x0100U, 0x0300U, 1234U
    };
    uint8_t packet[GATEWAY_LORA_FANCOIL_REPORT_LENGTH];

    assert(GATEWAY_LORA_FANCOIL_VALUE_COUNT == 6U);
    assert(GATEWAY_LORA_FANCOIL_DATA_LENGTH == 11U);
    assert(GATEWAY_LORA_FANCOIL_REPORT_LENGTH == 18U);
    assert(GatewayLora_BuildFancoilReport(packet, 0U, 0x1234U,
                                          -40, 0U, &state) == sizeof(expected));
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    assert(GatewayLora_Validate(packet, sizeof(expected)));

    assert(GatewayLora_EncodeSmallFloatX10(-325, &state.room_temperature_sf));
    assert(GatewayLora_BuildFancoilReport(packet, 0U, 0x1234U,
                                          -40, 0U, &state) == sizeof(expected));
    assert(packet[9] == 0xC0U && packet[10] == 0xE0U);
    assert(GatewayLora_DecodeSmallFloatX10(state.room_temperature_sf) == -325);
    assert(!GatewayLora_EncodeSmallFloatX10(1280, &state.room_temperature_sf));
    assert(GatewayLora_BuildFancoilReport(packet, 0U, 0U,
                                          -40, 0U, &state) == 0U);
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
    test_fancoil_type20_fixed_report();
    test_relay_is_also_a_normal_gateway_node();
    test_channel_matches_fixed_gateway_radio_table();
    test_child_login_carries_parent_only_on_local_hop();
    test_relay_inner_round_trip_and_rejection();
    puts("gateway_lora_codec tests passed");
    return 0;
}
