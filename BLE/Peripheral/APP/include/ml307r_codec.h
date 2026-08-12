#ifndef SPLITAC_ML307R_CODEC_H
#define SPLITAC_ML307R_CODEC_H

#include <stdint.h>

typedef struct {
    uint32_t command_id;
    uint8_t operation;
    uint16_t value;
} ml307_command_v2_t;

typedef struct {
    const char *topic;
    uint8_t topic_length;
    const char *payload;
    uint16_t payload_length;
} ml307_publish_v2_t;

typedef struct {
    uint16_t node_id;
    uint32_t timestamp;
    int16_t room_temp_x10;
    uint32_t run_minutes;
    uint32_t energy_wh;
    uint32_t command_id;
    uint16_t set_temp_x10;
    uint16_t power_w_x10;
    uint16_t fault_code;
    uint8_t power;
    uint8_t mode;
    uint8_t fan;
    uint8_t command_result;
    uint8_t has_command_result;
} ml307_report_v2_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int16_t timezone_quarters;
} ml307_clock_v2_t;

enum {
    ML307_CODEC_NOT_PUBLISH = 0,
    ML307_CODEC_OK = 1,
    ML307_CODEC_INVALID = -1,
    ML307_CODEC_FRAGMENTED = -2
};

int8_t Ml307Codec_ParsePublish(const char *line, uint16_t length,
                               ml307_publish_v2_t *publish);
uint8_t Ml307Codec_ParseCommand(const char *payload, uint16_t length,
                                ml307_command_v2_t *command);
uint8_t Ml307Codec_ParseClock(const char *line, uint16_t length,
                              ml307_clock_v2_t *clock);
uint16_t Ml307Codec_BuildReport(const ml307_report_v2_t *report,
                                char *output, uint16_t capacity);

#endif
