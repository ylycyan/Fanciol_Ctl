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
    time_fields_t tooLate = {2038U, 1U, 20U, 0U, 0U, 0U};
    uint32_t timestamp;

    expect_timestamp(0UL, 1970U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(1672531200UL, 2023U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(1709164800UL, 2024U, 2U, 29U, 0U, 0U, 0U);
    expect_timestamp(1767225600UL, 2026U, 1U, 1U, 0U, 0U, 0U);
    expect_timestamp(2147483000UL, 2038U, 1U, 19U, 3U, 3U, 20U);

    assert(!TimeUtil_ToUnix(&invalid, &timestamp));
    assert(TimeUtil_ToUnix(&leap, &timestamp));
    assert(timestamp == 1709164800UL);
    assert(!TimeUtil_ToUnix(&tooLate, &timestamp));
    assert(!TimeUtil_FromUnix(2147483001UL, &invalid));

    puts("time_utils tests passed");
    return 0;
}
