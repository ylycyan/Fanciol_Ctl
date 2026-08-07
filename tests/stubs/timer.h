#ifndef TEST_SPLITAC_TIMER_H
#define TEST_SPLITAC_TIMER_H

#include <stdint.h>

uint8_t RTC_IsTimeValid(void);
uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *month, uint16_t *day,
                        uint16_t *hour, uint16_t *minute, uint16_t *second);

#endif
