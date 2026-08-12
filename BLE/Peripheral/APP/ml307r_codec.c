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
                               ml307_publish_v2_t *publish)
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

static uint8_t key_equal(const char *key, uint16_t length, const char *expected)
{
    uint16_t expected_length = (uint16_t)strlen(expected);
    return length == expected_length && memcmp(key, expected, length) == 0;
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
                              ml307_clock_v2_t *clock)
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

uint8_t Ml307Codec_ParseCommand(const char *payload, uint16_t length,
                                ml307_command_v2_t *command)
{
    const char *cursor;
    const char *end;
    uint8_t fields = 0U;
    uint8_t first = 1U;

    if(!payload || !command || !length) return 0U;
    cursor = payload;
    end = payload + length;
    memset(command, 0, sizeof(*command));
    if(!consume(&cursor, end, '{')) return 0U;
    while(1) {
        const char *key;
        uint16_t key_length;
        uint32_t value;
        cursor = skip_space(cursor, end);
        if(cursor < end && *cursor == '}') { cursor++; break; }
        if(!first && !consume(&cursor, end, ',')) return 0U;
        if(!quoted(&cursor, end, &key, &key_length) ||
           !consume(&cursor, end, ':') || !parse_u32(&cursor, end, &value)) return 0U;
        if(key_equal(key, key_length, "id")) {
            if(fields & 0x01U) return 0U;
            command->command_id = value;
            fields |= 0x01U;
        } else if(key_equal(key, key_length, "op")) {
            if(fields & 0x02U) return 0U;
            if(value > 255U) return 0U;
            command->operation = (uint8_t)value;
            fields |= 0x02U;
        } else if(key_equal(key, key_length, "value")) {
            if(fields & 0x04U) return 0U;
            if(value > 65535U) return 0U;
            command->value = (uint16_t)value;
            fields |= 0x04U;
        } else return 0U;
        first = 0U;
    }
    cursor = skip_space(cursor, end);
    return cursor == end && fields == 0x07U && command->command_id != 0U &&
           command->operation >= 1U && command->operation <= 14U;
}

typedef struct {
    char *output;
    uint16_t capacity;
    uint16_t length;
    uint8_t failed;
} json_writer_t;

static void writer_char(json_writer_t *writer, char value)
{
    if(writer->output && writer->length + 1U < writer->capacity)
        writer->output[writer->length] = value;
    else if(writer->output) writer->failed = 1U;
    writer->length++;
}

static void writer_text(json_writer_t *writer, const char *value)
{
    while(*value) writer_char(writer, *value++);
}

static void writer_u32(json_writer_t *writer, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(value && count < sizeof(digits));
    while(count) writer_char(writer, digits[--count]);
}

static void writer_i32(json_writer_t *writer, int32_t value)
{
    if(value < 0) {
        writer_char(writer, '-');
        writer_u32(writer, (uint32_t)(-value));
    } else writer_u32(writer, (uint32_t)value);
}

static void writer_hex16(json_writer_t *writer, uint16_t value)
{
    int8_t shift;
    for(shift = 12; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)(value >> shift) & 0x0FU;
        writer_char(writer, digit < 10U ? (char)('0' + digit) :
                                          (char)('A' + digit - 10U));
    }
}

uint16_t Ml307Codec_BuildReport(const ml307_report_v2_t *report,
                                char *output, uint16_t capacity)
{
    json_writer_t writer;
    if(!report || (output && capacity == 0U)) return 0U;
    writer.output = output;
    writer.capacity = capacity;
    writer.length = 0U;
    writer.failed = 0U;
    writer_text(&writer, "{\"v\":1,\"id\":\""); writer_hex16(&writer, report->node_id);
    writer_text(&writer, "\",\"ts\":"); writer_u32(&writer, report->timestamp);
    writer_text(&writer, ",\"p\":"); writer_u32(&writer, report->power);
    writer_text(&writer, ",\"m\":"); writer_u32(&writer, report->mode);
    writer_text(&writer, ",\"t\":"); writer_u32(&writer, report->set_temp_x10);
    writer_text(&writer, ",\"r\":"); writer_i32(&writer, report->room_temp_x10);
    writer_text(&writer, ",\"f\":"); writer_u32(&writer, report->fan);
    writer_text(&writer, ",\"run\":"); writer_u32(&writer, report->run_minutes);
    writer_text(&writer, ",\"w\":"); writer_u32(&writer, report->power_w_x10);
    writer_text(&writer, ",\"e\":"); writer_u32(&writer, report->energy_wh);
    writer_text(&writer, ",\"er\":"); writer_u32(&writer, report->fault_code);
    if(report->has_command_result) {
        writer_text(&writer, ",\"c\":"); writer_u32(&writer, report->command_id);
        writer_text(&writer, ",\"x\":"); writer_u32(&writer, report->command_result);
    }
    writer_char(&writer, '}');
    if(output) {
        if(writer.failed || writer.length >= capacity) return 0U;
        output[writer.length] = '\0';
    }
    return writer.length;
}
