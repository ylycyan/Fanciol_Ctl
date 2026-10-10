#include "HAL.h"

uint8_t RTC_SetTimestamp(uint32_t timestamp);
uint8_t RTC_SyncTimestamp(uint32_t timestamp);
uint32_t Rtc_GetTimestamp(void);
uint8_t RTC_GetWallTime(uint16_t *year, uint16_t *mon, uint16_t *day,
                        uint16_t *hour, uint16_t *min, uint16_t *sec);
void RTC_ProductInit(void);
uint8_t RTC_IsTimeValid(void);
void WWDG_Init(void);
void WWDG_Refresh(void);
void Period_20ms(void);
void Period_100ms(void);
void Period_1s(void);
