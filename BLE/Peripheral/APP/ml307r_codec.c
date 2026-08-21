#include "ml307r_codec.h"
#include <string.h>

static const char *skip_space(const char *cursor, const char *end)
{
    while(cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                           *cursor == '\r' || *cursor == '\n')) cursor++;
    return cursor;
}

static uint8_t parse_u32(const char **cursor, const char *end, uint32_t *value)
{
    uint32_t result = 0U;
    uint8_t digits = 0U;
    const char *p = skip_space(*cursor, end);
    while(p < end && *p >= '0' && *p <= '9') {
        uint8_t digit = (uint8_t)(*p - '0');
        if(result > 429496729U ||
           (result == 429496729U && digit > 5U)) return 0U;
        result = result * 10U + digit;
        p++;
        digits = 1U;
    }
    if(!digits) return 0U;
    *cursor = skip_space(p, end);
    *value = result;
    return 1U;
}

static uint8_t consume(const char **cursor, const char *end, char expected)
{
    const char *p = skip_space(*cursor, end);
    if(p >= end || *p != expected) return 0U;
    *cursor = p + 1;
    return 1U;
}

static uint8_t quoted(const char **cursor, const char *end,
                      const char **value, uint16_t *length)
{
    const char *start;
    const char *p = skip_space(*cursor, end);
    if(p >= end || *p++ != '"') return 0U;
    start = p;
    while(p < end && *p != '"') {
        if(*p == '\\' || (uint8_t)*p < 0x20U) return 0U;
        p++;
    }
    if(p >= end || (uint32_t)(p - start) > 65535U) return 0U;
    *value = start;
    *length = (uint16_t)(p - start);
    *cursor = p + 1;
    return 1U;
}

int8_t Ml307Codec_ParsePublish(const char *line, uint16_t length,
                               ml307_publish_t *publish)
{
    static const char prefix[] = "+MQTTURC:";
    const char *cursor;
    const char *end;
    const char *kind;
    uint16_t kind_length;
    uint16_t topic_length;
    uint32_t number;
    uint32_t total_length;
    uint32_t chunk_length;

    if(!line || !publish || length < sizeof(prefix) - 1U) return ML307_CODEC_INVALID;
    end = line + length;
    while(end > line && (end[-1] == '\r' || end[-1] == '\n')) end--;
    if((uint16_t)(end - line) < sizeof(prefix) - 1U ||
       memcmp(line, prefix, sizeof(prefix) - 1U) != 0) return ML307_CODEC_NOT_PUBLISH;
    cursor = line + sizeof(prefix) - 1U;
    if(!quoted(&cursor, end, &kind, &kind_length)) return ML307_CODEC_INVALID;
    if(kind_length != 7U || memcmp(kind, "publish", 7U) != 0)
        return ML307_CODEC_NOT_PUBLISH;
    if(!consume(&cursor, end, ',') || !parse_u32(&cursor, end, &number) || number != 0U ||
       !consume(&cursor, end, ',') || !parse_u32(&cursor, end, &number) ||
       !consume(&cursor, end, ',') ||
       !quoted(&cursor, end, &publish->topic, &topic_length) || topic_length > 255U ||
       !consume(&cursor, end, ',') || !parse_u32(&cursor, end, &total_length) ||
       !consume(&cursor, end, ',') || !parse_u32(&cursor, end, &chunk_length) ||
       !consume(&cursor, end, ',')) return ML307_CODEC_INVALID;

    cursor = skip_space(cursor, end);
    publish->payload = cursor;
    publish->payload_length = (uint16_t)(end - cursor);
    publish->topic_length = (uint8_t)topic_length;
    if(chunk_length != publish->payload_length || total_length < chunk_length)
        return ML307_CODEC_INVALID;
    if(total_length != chunk_length) return ML307_CODEC_FRAGMENTED;
    return ML307_CODEC_OK;
}

static uint8_t two_digits(const char **cursor, const char *end, uint8_t *value)
{
    const char *p = *cursor;
    if(p + 2 > end || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
        return 0U;
    *value = (uint8_t)((uint8_t)(p[0] - '0') * 10U + (uint8_t)(p[1] - '0'));
    *cursor = p + 2;
    return 1U;
}

uint8_t Ml307Codec_ParseClock(const char *line, uint16_t length,
                              ml307_clock_t *clock)
{
    static const char prefix[] = "+CCLK:";
    const char *cursor;
    const char *end;
    uint8_t year;
    uint8_t timezone;
    int16_t sign;
    if(!line || !clock || length < sizeof(prefix)) return 0U;
    cursor = line;
    end = line + length;
    if(memcmp(cursor, prefix, sizeof(prefix) - 1U) != 0) return 0U;
    cursor = skip_space(cursor + sizeof(prefix) - 1U, end);
    if(cursor >= end || *cursor++ != '"' ||
       !two_digits(&cursor, end, &year) || !consume(&cursor, end, '/') ||
       !two_digits(&cursor, end, &clock->month) || !consume(&cursor, end, '/') ||
       !two_digits(&cursor, end, &clock->day) || !consume(&cursor, end, ',') ||
       !two_digits(&cursor, end, &clock->hour) || !consume(&cursor, end, ':') ||
       !two_digits(&cursor, end, &clock->minute) || !consume(&cursor, end, ':') ||
       !two_digits(&cursor, end, &clock->second)) return 0U;
    if(cursor >= end || (*cursor != '+' && *cursor != '-')) return 0U;
    sign = *cursor++ == '+' ? 1 : -1;
    if(!two_digits(&cursor, end, &timezone) || timezone > 96U ||
       cursor >= end || *cursor++ != '"' || skip_space(cursor, end) != end)
        return 0U;
    clock->year = (uint16_t)(2000U + year);
    clock->timezone_quarters = (int16_t)(sign * timezone);
    if(clock->month < 1U || clock->month > 12U || clock->day < 1U || clock->day > 31U ||
       clock->hour > 23U || clock->minute > 59U || clock->second > 59U)
        return 0U;
    return 1U;
}

static int8_t hex_nibble(char value)
{
    if(value >= '0' && value <= '9') return (int8_t)(value - '0');
    if(value >= 'A' && value <= 'F') return (int8_t)(value - 'A' + 10);
    if(value >= 'a' && value <= 'f') return (int8_t)(value - 'a' + 10);
    return -1;
}

uint16_t Ml307Codec_HexEncode(const uint8_t *input, uint8_t length,
                              char *output, uint16_t capacity)
{
    static const char digits[] = "0123456789ABCDEF";
    uint16_t encoded_length = (uint16_t)length * 2U;
    uint8_t i;
    if(!input || !output || !length || capacity < encoded_length) return 0U;
    for(i = 0U; i < length; i++) {
        output[(uint16_t)i * 2U] = digits[input[i] >> 4];
        output[(uint16_t)i * 2U + 1U] = digits[input[i] & 0x0FU];
    }
    return encoded_length;
}

uint8_t Ml307Codec_HexDecode(const char *input, uint16_t length,
                             uint8_t *output, uint8_t capacity)
{
    uint16_t decoded_length;
    uint16_t i;
    if(!input || !output || !length || (length & 1U)) return 0U;
    decoded_length = length / 2U;
    if(decoded_length > capacity || decoded_length > 255U) return 0U;
    for(i = 0U; i < decoded_length; i++) {
        int8_t high = hex_nibble(input[i * 2U]);
        int8_t low = hex_nibble(input[i * 2U + 1U]);
        if(high < 0 || low < 0) return 0U;
        output[i] = (uint8_t)(((uint8_t)high << 4) | (uint8_t)low);
    }
    return (uint8_t)decoded_length;
}
