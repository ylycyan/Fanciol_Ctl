#include "HAL.h"
#include <time.h>

void RTC_SetTimestamp(uint32_t timestamp);
uint32_t Rtc_GetTimestamp(void);
void WWDG_Init(void);
void WWDG_Refresh(void);
void Period_100ms(void);
void Period_1s(void);