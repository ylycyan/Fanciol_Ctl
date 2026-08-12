#ifndef SPLITAC_SERVICE_V2_H
#define SPLITAC_SERVICE_V2_H

#include <stdint.h>

void SplitAcV2_Init(void);
void SplitAcV2_ResetSession(void);
uint8_t SplitAcV2_IdentifyActive(void);
uint8_t SplitAcV2_MaintenanceActive(void);
uint8_t SplitAcControl_Execute(uint8_t control, uint16_t value);
uint8_t SplitAcV2_HandleFrame(const uint8_t *request, uint16_t request_len,
                              uint8_t *response, uint16_t capacity, uint16_t *response_len);

#endif
