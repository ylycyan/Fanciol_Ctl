/**
 * @file time_utils.c
 * @brief Unix 时间戳 ↔ 公历日期互转（纯整数，无浮点/无 mktime）
 *
 * 有效范围 1970-01-01 ~ 2038-01-19（uint32 时间戳上限附近），
 * 超出范围返回 0 以保护 RTC 软件偏移的 int32 表示。
 */
#include "time_utils.h"

static const uint8_t monthDays[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/**
 * @brief 判断闰年（公历规则：4 年一闰、百年不闰、四百年再闰）
 */
static uint8_t TimeUtil_IsLeapYear(uint16_t year)
{
    if((year % 4U) != 0U) return 0;
    if((year % 100U) != 0U) return 1;
    return (year % 400U) == 0U;
}

/**
 * @brief 某年某月的天数（2 月按闰年调整）
 */
static uint8_t TimeUtil_DaysInMonth(uint16_t year, uint8_t month)
{
    uint8_t days;

    if(month < 1U || month > 12U) return 0;
    days = monthDays[month - 1U];
    if(month == 2U && TimeUtil_IsLeapYear(year)) days++;
    return days;
}

/**
 * @brief Unix 时间戳 → 公历字段（逐年级减，简单可靠）
 * @retval 1 成功；0 时间戳超范围或指针为空
 */
uint8_t TimeUtil_FromUnix(uint32_t timestamp, time_fields_t *fields)
{
    uint32_t days;
    uint32_t secondsOfDay;
    uint16_t year = 1970U;
    uint8_t month = 1U;

    if(fields == 0 || timestamp > 2147483000UL) return 0;

    days = timestamp / 86400UL;
    secondsOfDay = timestamp % 86400UL;
    for(;;) {
        uint16_t daysInYear = TimeUtil_IsLeapYear(year) ? 366U : 365U;
        if(days < daysInYear) break;
        days -= daysInYear;
        year++;
    }

    for(;;) {
        uint8_t daysInMonth = TimeUtil_DaysInMonth(year, month);
        if(days < daysInMonth) break;
        days -= daysInMonth;
        month++;
    }

    fields->year = year;
    fields->month = month;
    fields->day = (uint8_t)(days + 1U);
    fields->hour = (uint8_t)(secondsOfDay / 3600UL);
    secondsOfDay %= 3600UL;
    fields->minute = (uint8_t)(secondsOfDay / 60UL);
    fields->second = (uint8_t)(secondsOfDay % 60UL);
    return 1;
}

/**
 * @brief 公历字段 → Unix 时间戳（含 1970 前闰年修正）
 * @retval 1 成功；0 字段非法或结果超范围
 */
uint8_t TimeUtil_ToUnix(const time_fields_t *fields, uint32_t *timestamp)
{
    uint32_t days;
    uint32_t previousYear;
    uint8_t month;
    uint8_t daysInMonth;

    if(fields == 0 || timestamp == 0) return 0;
    if(fields->year < 1970U || fields->year > 2038U ||
       fields->month < 1U || fields->month > 12U ||
       fields->hour > 23U || fields->minute > 59U || fields->second > 59U) {
        return 0;
    }

    daysInMonth = TimeUtil_DaysInMonth(fields->year, fields->month);
    if(fields->day < 1U || fields->day > daysInMonth) return 0;

    /* 自 1970-01-01 起的天数：年 ×365 + 闰日修正 + 月天数 + 日 */
    previousYear = (uint32_t)fields->year - 1UL;
    days = 365UL * ((uint32_t)fields->year - 1970UL);
    days += previousYear / 4UL - previousYear / 100UL + previousYear / 400UL;
    days -= 1969UL / 4UL - 1969UL / 100UL + 1969UL / 400UL;

    for(month = 1U; month < fields->month; month++) {
        days += TimeUtil_DaysInMonth(fields->year, month);
    }
    days += (uint32_t)fields->day - 1UL;

    if(days > 24855UL) return 0;
    *timestamp = days * 86400UL +
                 (uint32_t)fields->hour * 3600UL +
                 (uint32_t)fields->minute * 60UL +
                 fields->second;
    if(*timestamp > 2147483000UL) return 0;
    return 1;
}
