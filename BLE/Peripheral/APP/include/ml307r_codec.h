#ifndef SPLITAC_ML307R_CODEC_H
#define SPLITAC_ML307R_CODEC_H

#include <stdint.h>

typedef struct {
    const char *topic;
    uint8_t topic_length;
    const char *payload;
    uint16_t payload_length;
} ml307_publish_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int16_t timezone_quarters;
} ml307_clock_t;

enum {
    ML307_CODEC_NOT_PUBLISH = 0,
    ML307_CODEC_OK = 1,
    ML307_CODEC_INVALID = -1,
    ML307_CODEC_FRAGMENTED = -2
};

int8_t Ml307Codec_ParsePublish(const char *line, uint16_t length,
                               ml307_publish_t *publish);
uint8_t Ml307Codec_ParseClock(const char *line, uint16_t length,
                              ml307_clock_t *clock);
uint16_t Ml307Codec_HexEncode(const uint8_t *input, uint8_t length,
                              char *output, uint16_t capacity);
uint8_t Ml307Codec_HexDecode(const char *input, uint16_t length,
                             uint8_t *output, uint8_t capacity);

#endif
