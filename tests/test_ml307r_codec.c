#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml307r_codec.h"

static void test_publish_urc_and_command(void)
{
    static const char line[] =
        "+MQTTURC: \"publish\",0,0,\"splitac/cmd\",26,26,{\"id\":42,\"op\":1,\"value\":1}";
    ml307_publish_v2_t publish;
    ml307_command_v2_t command;

    assert(Ml307Codec_ParsePublish(line, (uint16_t)strlen(line), &publish) == ML307_CODEC_OK);
    assert(publish.topic_length == strlen("splitac/cmd"));
    assert(memcmp(publish.topic, "splitac/cmd", publish.topic_length) == 0);
    assert(Ml307Codec_ParseCommand(publish.payload, publish.payload_length, &command));
    assert(command.command_id == 42U);
    assert(command.operation == 1U);
    assert(command.value == 1U);
}

static void test_fragment_and_invalid_command_are_rejected(void)
{
    static const char fragment[] =
        "+MQTTURC: \"publish\",0,0,\"cmd\",20,8,{\"id\":1}";
    ml307_publish_v2_t publish;
    ml307_command_v2_t command;

    assert(Ml307Codec_ParsePublish(fragment, (uint16_t)strlen(fragment), &publish) ==
           ML307_CODEC_FRAGMENTED);
    {
        static const char zero_id[] = "{\"id\":0,\"op\":1,\"value\":1}";
        static const char bad_op[] = "{\"id\":1,\"op\":15,\"value\":1}";
        static const char duplicate_id[] = "{\"id\":1,\"op\":1,\"value\":1,\"id\":2}";
        static const char unknown_key[] = "{\"id\":1,\"op\":1,\"value\":1,\"extra\":0}";
        assert(!Ml307Codec_ParseCommand(zero_id, (uint16_t)strlen(zero_id), &command));
        assert(!Ml307Codec_ParseCommand(bad_op, (uint16_t)strlen(bad_op), &command));
        assert(!Ml307Codec_ParseCommand(duplicate_id, (uint16_t)strlen(duplicate_id), &command));
        assert(!Ml307Codec_ParseCommand(unknown_key, (uint16_t)strlen(unknown_key), &command));
    }
}

static void test_report_is_compact_and_contains_result(void)
{
    ml307_report_v2_t report = {0};
    char output[176];
    uint16_t length;

    report.node_id = 0xFFFFU;
    report.timestamp = 0xFFFFFFFFUL;
    report.room_temp_x10 = INT16_MIN;
    report.run_minutes = 0xFFFFFFFFUL;
    report.energy_wh = 0xFFFFFFFFUL;
    report.command_id = 0xFFFFFFFFUL;
    report.set_temp_x10 = 65535U;
    report.power_w_x10 = 65535U;
    report.fault_code = 65535U;
    report.power = 1U;
    report.mode = 255U;
    report.fan = 255U;
    report.command_result = 8U;
    report.has_command_result = 1U;
    length = Ml307Codec_BuildReport(&report, output, sizeof(output));
    assert(length > 0U && length < sizeof(output));
    assert(strstr(output, "\"id\":\"FFFF\"") != 0);
    assert(strstr(output, "\"c\":4294967295,\"x\":8") != 0);
    assert(length == Ml307Codec_BuildReport(&report, 0, 0U));
}

static void test_network_clock_parses_timezone_quarters(void)
{
    static const char line[] = "+CCLK: \"26/08/11,10:24:30+32\"";
    ml307_clock_v2_t clock;
    assert(Ml307Codec_ParseClock(line, (uint16_t)strlen(line), &clock));
    assert(clock.year == 2026U && clock.month == 8U && clock.day == 11U);
    assert(clock.hour == 10U && clock.minute == 24U && clock.second == 30U);
    assert(clock.timezone_quarters == 32);
    assert(!Ml307Codec_ParseClock("+CCLK: \"26/13/11,10:24:30+32\"", 31U, &clock));
}

int main(void)
{
    test_publish_urc_and_command();
    test_fragment_and_invalid_command_are_rejected();
    test_report_is_compact_and_contains_result();
    test_network_clock_parses_timezone_quarters();
    puts("ml307r_codec tests passed");
    return 0;
}
