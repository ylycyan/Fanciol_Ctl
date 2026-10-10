#ifndef SPLITAC_ML307R_CODEC_H
#define SPLITAC_ML307R_CODEC_H

#include <stdint.h>

typedef struct {
    const char *topic;
    uint8_t topic_length;
    uint16_t payload_length;
} ml307_publish_header_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} ml307_clock_t;

enum {
    ML307_CODEC_NOT_PUBLISH = 0,
    ML307_CODEC_OK = 1,
    ML307_CODEC_INVALID = -1,
    ML307_CODEC_FRAGMENTED = -2
};

int8_t Ml307Codec_ParsePublishHeader(const char *header, uint16_t length,
                                     ml307_publish_header_t *publish);
uint8_t Ml307Codec_ParseClock(const char *line, uint16_t length,
                              ml307_clock_t *clock);

#endif
