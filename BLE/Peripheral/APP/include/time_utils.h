#ifndef SPLITAC_TIME_UTILS_H
#define SPLITAC_TIME_UTILS_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} time_fields_t;

/* Unsigned Unix seconds: 1970-01-01 through 2099-12-31 23:59:59 UTC. */
#define TIME_UTIL_MAX_TIMESTAMP 4102444799UL
uint8_t TimeUtil_FromUnix(uint32_t timestamp, time_fields_t *fields);
uint8_t TimeUtil_ToUnix(const time_fields_t *fields, uint32_t *timestamp);

#endif
