#include "HAL.h"

void RTC_SetTimestamp(uint32_t timestamp);
uint32_t Rtc_GetTimestamp(void);
void RTC_ProductInit(uint8_t resetReason, uint32_t retainedTimestamp);
uint8_t RTC_IsTimeValid(void);
void WWDG_Init(void);
void WWDG_Refresh(void);
void Period_20ms(void);
void Period_100ms(void);
void Period_1s(void);
