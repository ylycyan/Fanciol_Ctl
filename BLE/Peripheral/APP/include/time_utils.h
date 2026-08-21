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

/* 产品 RTC 的有效范围固定为 1970-01-01 至 2038-01-19。 */
uint8_t TimeUtil_FromUnix(uint32_t timestamp, time_fields_t *fields);
uint8_t TimeUtil_ToUnix(const time_fields_t *fields, uint32_t *timestamp);

#endif
