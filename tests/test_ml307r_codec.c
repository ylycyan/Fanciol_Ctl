#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml307r_codec.h"

static void test_publish_urc_and_lora_frame(void)
{
    static const uint8_t frame[] = {
        0x0D, 0x01, 0x42, 0x0A, 0x34, 0x12, 0x15, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00, 0xCB
    };
    static const char line[] =
        "+MQTTURC: \"publish\",0,0,\"splitac/cmd\",36,36,0D01420A3412150000000000002A000000CB";
    ml307_publish_t publish;
    uint8_t decoded[18];

    assert(Ml307Codec_ParsePublish(line, (uint16_t)strlen(line), &publish) == ML307_CODEC_OK);
    assert(publish.topic_length == strlen("splitac/cmd"));
    assert(memcmp(publish.topic, "splitac/cmd", publish.topic_length) == 0);
    assert(Ml307Codec_HexDecode(publish.payload, publish.payload_length,
                                decoded, sizeof(decoded)) == sizeof(frame));
    assert(memcmp(decoded, frame, sizeof(frame)) == 0);
}

static void test_fragment_and_invalid_hex_are_rejected(void)
{
    static const char fragment[] =
        "+MQTTURC: \"publish\",0,0,\"cmd\",36,8,0D01420A";
    ml307_publish_t publish;
    uint8_t decoded[18];

    assert(Ml307Codec_ParsePublish(fragment, (uint16_t)strlen(fragment), &publish) ==
           ML307_CODEC_FRAGMENTED);
    assert(!Ml307Codec_HexDecode("0", 1U, decoded, sizeof(decoded)));
    assert(!Ml307Codec_HexDecode("0G", 2U, decoded, sizeof(decoded)));
    assert(!Ml307Codec_HexDecode("0x01", 4U, decoded, sizeof(decoded)));
    assert(!Ml307Codec_HexDecode("00 1", 4U, decoded, sizeof(decoded)));
    assert(!Ml307Codec_HexDecode("0011", 4U, decoded, 1U));
}

static void test_hex_round_trip(void)
{
    static const uint8_t frame[] = {
        0x01, 0x00, 0x34, 0x12, 0xB0, 0x00, 0x1A, 0x00, 0x01,
        0x1B, 0x00, 0x01, 0x00, 0x02, 0x00, 0x32, 0x00, 0x4E
    };
    char output[36];
    uint8_t decoded[18];
    uint16_t length;

    length = Ml307Codec_HexEncode(frame, sizeof(frame), output, sizeof(output));
    assert(length == sizeof(output));
    assert(memcmp(output, "01003412B0001A00011B000100020032004E", length) == 0);
    assert(Ml307Codec_HexDecode(output, length, decoded, sizeof(decoded)) == sizeof(decoded));
    assert(memcmp(decoded, frame, sizeof(frame)) == 0);
    assert(Ml307Codec_HexDecode("0d01420a3412150000000000002a000000cb", 36U,
                                decoded, sizeof(decoded)) == sizeof(decoded));
}

static void test_network_clock_parses_timezone_quarters(void)
{
    static const char line[] = "+CCLK: \"26/08/11,10:24:30+32\"";
    ml307_clock_t clock;
    assert(Ml307Codec_ParseClock(line, (uint16_t)strlen(line), &clock));
    assert(clock.year == 2026U && clock.month == 8U && clock.day == 11U);
    assert(clock.hour == 10U && clock.minute == 24U && clock.second == 30U);
    assert(clock.timezone_quarters == 32);
    assert(!Ml307Codec_ParseClock("+CCLK: \"26/13/11,10:24:30+32\"", 31U, &clock));
}

int main(void)
{
    test_publish_urc_and_lora_frame();
    test_fragment_and_invalid_hex_are_rejected();
    test_hex_round_trip();
    test_network_clock_parses_timezone_quarters();
    puts("ml307r_codec tests passed");
    return 0;
}
