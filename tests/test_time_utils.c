#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "time_utils.h"

static void expect_timestamp(uint32_t timestamp,
                             uint16_t year,
                             uint8_t month,
                             uint8_t day,
                             uint8_t hour,
                             uint8_t minute,
                             uint8_t second)
{
    time_fields_t fields;
    uint32_t roundTrip = 0;

    assert(TimeUtil_FromUnix(timestamp, &fields));
    assert(fields.year == year);
    assert(fields.month == month);
    assert(fields.day == day);
    assert(fields.hour == hour);
    assert(fields.minute == minute);
    assert(fields.second == second);
    assert(TimeUtil_ToUnix(&fields, &roundTrip));
    assert(roundTrip == timestamp);
}

int main(void)
{
    time_fields_t invalid = {2023U, 2U, 29U, 0U, 0U, 0U};
    time_fields_t leap = {2024U, 2U, 29U, 0U, 0U, 0U};
    time_fields_t tooLate = {2100U, 1U, 1U, 0U, 0U, 0U};
    uint32_t timestamp;

    expect_timestamp(0UL, 1970U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(1672531200UL, 2023U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(1709164800UL, 2024U, 2U, 29U, 0U, 0U, 0U);
    expect_timestamp(1767225600UL, 2026U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(2147483000UL, 2038U, 1U, 19U, 3U, 3U, 20U);
    expect_timestamp(2147483648UL, 2038U, 1U, 19U, 3U, 14U, 8U);
    expect_timestamp(3981312000UL, 2096U, 2U, 29U, 0U, 0U, 0U);
    expect_timestamp(TIME_UTIL_MAX_TIMESTAMP, 2099U, 12U, 31U, 23U, 59U, 59U);

    assert(!TimeUtil_ToUnix(&invalid, &timestamp));
    assert(TimeUtil_ToUnix(&leap, &timestamp));
    assert(timestamp == 1709164800UL);
    assert(!TimeUtil_ToUnix(&tooLate, &timestamp));
    assert(!TimeUtil_FromUnix(TIME_UTIL_MAX_TIMESTAMP + 1UL, &invalid));
    assert(!TimeUtil_FromUnix(UINT32_MAX, &invalid));
    /* Exhaust every supported day at midnight and at its final second. */
    const uint8_t month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    timestamp=0;
    for(uint16_t year=1970;year<=2099;year++) {
        for(uint8_t month=1;month<=12;month++) {
            uint8_t days=month_days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
            for(uint8_t day=1;day<=days;day++) {
                expect_timestamp(timestamp,year,month,day,0,0,0);
                expect_timestamp(timestamp+86399UL,year,month,day,23,59,59);
                timestamp+=86400UL;
            }
        }
    }
    assert(timestamp==TIME_UTIL_MAX_TIMESTAMP+1UL);

    puts("time_utils tests passed");
    return 0;
}
