#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml307r_codec.h"

static void test_binary_publish_header(void)
{
    static const char header[] =
        "+MQTTURC: \"publish\",0,0,\"sub/ac/A26091234\",18,18,";
    ml307_publish_header_t publish;

    assert(Ml307Codec_ParsePublishHeader(header, (uint16_t)strlen(header), &publish) == ML307_CODEC_OK);
    assert(publish.topic_length == strlen("sub/ac/A26091234"));
    assert(memcmp(publish.topic, "sub/ac/A26091234", publish.topic_length) == 0);
    assert(publish.payload_length == 18U);
}

static void test_fragment_and_incomplete_header_are_rejected(void)
{
    static const char fragment[] =
        "+MQTTURC: \"publish\",0,0,\"cmd\",36,8,";
    static const char incomplete[] =
        "+MQTTURC: \"publish\",0,0,\"cmd\",18,18";
    ml307_publish_header_t publish;

    assert(Ml307Codec_ParsePublishHeader(fragment, (uint16_t)strlen(fragment), &publish) ==
           ML307_CODEC_FRAGMENTED);
    assert(Ml307Codec_ParsePublishHeader(incomplete, (uint16_t)strlen(incomplete), &publish) ==
           ML307_CODEC_INVALID);
}

static void test_network_clock_accepts_optional_timezone_metadata(void)
{
    static const char line[] = "+CCLK: \"26/08/11,10:24:30+32\"";
    ml307_clock_t clock;
    assert(Ml307Codec_ParseClock(line, (uint16_t)strlen(line), &clock));
    assert(clock.year == 2026U && clock.month == 8U && clock.day == 11U);
    assert(clock.hour == 10U && clock.minute == 24U && clock.second == 30U);
    static const char no_zone[] = "+CCLK: \"26/08/11,10:24:30\"";
    static const char bad_zone[] = "+CCLK: \"26/08/11,10:24:30+3\"";
    assert(Ml307Codec_ParseClock(no_zone, (uint16_t)strlen(no_zone), &clock));
    assert(clock.hour == 10U && clock.minute == 24U && clock.second == 30U);
    assert(!Ml307Codec_ParseClock(bad_zone, (uint16_t)strlen(bad_zone), &clock));
    assert(!Ml307Codec_ParseClock("+CCLK: \"26/13/11,10:24:30+32\"", 31U, &clock));
}

int main(void)
{
    test_binary_publish_header();
    test_fragment_and_incomplete_header_are_rejected();
    test_network_clock_accepts_optional_timezone_metadata();
    puts("ml307r_codec tests passed");
    return 0;
}
