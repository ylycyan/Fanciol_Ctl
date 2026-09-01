/**
 * @file ota_guard.c
 * @brief OTA 流程状态机保护（防越界/乱序写入）
 *
 * OTA 只按"擦除 → 顺序编程 → 完成"推进。完整镜像由
 * Ota_VerifyStep() 在切换前统一做 CRC32 校验。
 */
#include "ota_guard.h"
#include <string.h>

/**
 * @brief 重置 OTA 保护状态（断开/失败时调用）
 */
void OtaGuard_Reset(ota_guard_t *guard)
{
    if(guard != 0) memset(guard, 0, sizeof(*guard));
}

/**
 * @brief 开始擦除阶段：校验擦除范围（块对齐、位于镜像区间内、块数不越界）
 */
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
    guard->state = OTA_GUARD_ERASING;
    return 1;
}

/**
 * @brief 结束擦除阶段（全部擦除成功后进入 READY）
 */
void OtaGuard_EndErase(ota_guard_t *guard, uint8_t success)
{
    if(guard == 0 || guard->state != OTA_GUARD_ERASING) return;
    if(success) {
        guard->state = OTA_GUARD_READY;
    } else {
        OtaGuard_Reset(guard);
    }
}

/**
 * @brief 是否允许在该地址编程：必须严格顺序、处于 READY/PROGRAMMING、长度不越界
 */
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

/**
 * @brief 记录一段编程完成（推进 program_next）
 */
void OtaGuard_EndProgram(ota_guard_t *guard, uint16_t length, uint8_t success)
{
    if(guard == 0 || !success ||
       (guard->state != OTA_GUARD_READY && guard->state != OTA_GUARD_PROGRAMMING)) {
        return;
    }
    guard->program_next += length;
    guard->state = OTA_GUARD_PROGRAMMING;
}

/**
 * @brief 是否允许结束 OTA：必须完成连续编程且确有写入
 *
 * 调用方还会用 manifest 中的 image_size 检查 program_next，并在切换
 * 前执行整镜像 CRC32。因此不再要求客户端把整份固件重新上传一遍
 * 做逐块 VERIFY。
 */
uint8_t OtaGuard_CanFinish(const ota_guard_t *guard)
{
    return guard != 0 &&
           guard->state == OTA_GUARD_PROGRAMMING &&
           guard->program_next > guard->range_start;
}
