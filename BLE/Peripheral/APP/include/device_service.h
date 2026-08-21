#ifndef SPLITAC_DEVICE_SERVICE_H
#define SPLITAC_DEVICE_SERVICE_H

#include <stdint.h>

void DeviceService_Init(void);
void DeviceService_ResetSession(void);
uint8_t DeviceService_IdentifyActive(void);
uint8_t DeviceService_MaintenanceActive(void);
uint8_t SplitAcControl_Execute(uint8_t control, uint16_t value);
uint8_t DeviceService_HandleFrame(const uint8_t *request, uint16_t request_len,
                              uint8_t *response, uint16_t capacity, uint16_t *response_len);

#endif
