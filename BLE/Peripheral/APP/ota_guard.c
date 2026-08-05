#include "ota_guard.h"
#include <string.h>

void OtaGuard_Reset(ota_guard_t *guard)
{
    if(guard != 0) memset(guard, 0, sizeof(*guard));
}

uint8_t OtaGuard_BeginErase(ota_guard_t *guard,
                            uint32_t start,
                            uint32_t block_count,
                            uint32_t block_size,
                            uint32_t image_start,
                            uint32_t image_end)
{
    uint32_t available;
    uint32_t length;

    if(guard == 0 || block_size == 0u || block_count == 0u ||
       image_start >= image_end || start < image_start || start >= image_end ||
       (start % block_size) != 0u) {
        return 0;
    }

    available = image_end - start;
    if(block_count > (available / block_size)) return 0;
    length = block_count * block_size;
    if(length == 0u || length > available) return 0;

    guard->range_start = start;
    guard->range_end = start + length;
    guard->program_next = start;
    guard->verify_next = start;
    guard->state = OTA_GUARD_ERASING;
    return 1;
}

void OtaGuard_EndErase(ota_guard_t *guard, uint8_t success)
{
    if(guard == 0 || guard->state != OTA_GUARD_ERASING) return;
    if(success) {
        guard->state = OTA_GUARD_READY;
    } else {
        OtaGuard_Reset(guard);
    }
}

uint8_t OtaGuard_CanProgram(const ota_guard_t *guard, uint32_t address, uint16_t length)
{
    if(guard == 0 || length == 0u ||
       (guard->state != OTA_GUARD_READY && guard->state != OTA_GUARD_PROGRAMMING) ||
       address != guard->program_next || address < guard->range_start ||
       address >= guard->range_end) {
        return 0;
    }
    return (uint32_t)length <= (guard->range_end - address);
}

void OtaGuard_EndProgram(ota_guard_t *guard, uint16_t length, uint8_t success)
{
    if(guard == 0 || !success ||
       (guard->state != OTA_GUARD_READY && guard->state != OTA_GUARD_PROGRAMMING)) {
        return;
    }
    guard->program_next += length;
    guard->state = OTA_GUARD_PROGRAMMING;
}

uint8_t OtaGuard_CanVerify(const ota_guard_t *guard, uint32_t address, uint16_t length)
{
    if(guard == 0 || length == 0u ||
       (guard->state != OTA_GUARD_PROGRAMMING && guard->state != OTA_GUARD_VERIFYING) ||
       address != guard->verify_next || address < guard->range_start ||
       address >= guard->program_next) {
        return 0;
    }
    return (uint32_t)length <= (guard->program_next - address);
}

void OtaGuard_EndVerify(ota_guard_t *guard, uint16_t length, uint8_t success)
{
    if(guard == 0 || !success ||
       (guard->state != OTA_GUARD_PROGRAMMING && guard->state != OTA_GUARD_VERIFYING)) {
        return;
    }
    guard->verify_next += length;
    guard->state = (guard->verify_next == guard->program_next)
        ? OTA_GUARD_VERIFIED
        : OTA_GUARD_VERIFYING;
}

uint8_t OtaGuard_CanFinish(const ota_guard_t *guard)
{
    return guard != 0 &&
           guard->state == OTA_GUARD_VERIFIED &&
           guard->program_next > guard->range_start &&
           guard->verify_next == guard->program_next;
}
