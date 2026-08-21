#ifndef SPLITAC_OTA_GUARD_H
#define SPLITAC_OTA_GUARD_H

#include <stdint.h>

typedef enum {
    OTA_GUARD_IDLE = 0,
    OTA_GUARD_ERASING,
    OTA_GUARD_READY,
    OTA_GUARD_PROGRAMMING,
    OTA_GUARD_VERIFYING,
    OTA_GUARD_VERIFIED
} ota_guard_state_t;

/*
 * OTA 只保留必要的顺序和范围状态，避免异常/乱序 BLE 包写到暂存区之外。
 * 结构体无动态分配，CH583 常驻 RAM 开销不超过 20 字节。
 */
typedef struct {
    uint32_t range_start;
    uint32_t range_end;
    uint32_t program_next;
    uint32_t verify_next;
    uint8_t state;
} ota_guard_t;

void OtaGuard_Reset(ota_guard_t *guard);
uint8_t OtaGuard_BeginErase(ota_guard_t *guard,
                            uint32_t start,
                            uint32_t block_count,
                            uint32_t block_size,
                            uint32_t image_start,
                            uint32_t image_end);
void OtaGuard_EndErase(ota_guard_t *guard, uint8_t success);
uint8_t OtaGuard_CanProgram(const ota_guard_t *guard, uint32_t address, uint16_t length);
void OtaGuard_EndProgram(ota_guard_t *guard, uint16_t length, uint8_t success);
uint8_t OtaGuard_CanVerify(const ota_guard_t *guard, uint32_t address, uint16_t length);
void OtaGuard_EndVerify(ota_guard_t *guard, uint16_t length, uint8_t success);
uint8_t OtaGuard_CanFinish(const ota_guard_t *guard);

#endif
