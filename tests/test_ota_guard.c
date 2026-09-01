#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../BLE/Peripheral/APP/include/ota_guard.h"
#include "../BLE/Peripheral/APP/include/ota_update.h"
#include "../BLE/Peripheral/APP/include/device_protocol.h"

#define APP_B_START 0x00037000UL
#define IAP_START   0x0006D000UL
#define BLOCK_SIZE  4096UL

static uint8_t dataflash[0x8000];
static uint8_t codeflash[480 * 1024];

static uint32_t crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t index;
    uint8_t bit;
    for(index = 0; index < length; index++) {
        crc ^= data[index];
        for(bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t Config_Crc32(const uint8_t *data, uint16_t length) { return crc32(data, length); }
uint32_t Test_EepromRead(uint32_t address, void *buffer, uint32_t length) { memcpy(buffer, dataflash + address, length); return 0; }
uint32_t Test_EepromErase(uint32_t address, uint32_t length) { memset(dataflash + address, 0xFF, length); return 0; }
uint32_t Test_EepromWrite(uint32_t address, const void *buffer, uint32_t length) { memcpy(dataflash + address, buffer, length); return 0; }
uint32_t Test_FlashErase(uint32_t address, uint32_t length) { memset(codeflash + address, 0xFF, length); return 0; }
uint32_t Test_FlashWrite(uint32_t address, const void *buffer, uint32_t length) { memcpy(codeflash + address, buffer, length); return 0; }
uint32_t Test_FlashVerify(uint32_t address, const void *buffer, uint32_t length) { return memcmp(codeflash + address, buffer, length) != 0; }
void Test_FlashRead(uint32_t address, void *buffer, uint32_t length) { memcpy(buffer, codeflash + address, length); }
uint32_t SYS_GetLastResetSta(void) { return 0; }

static void test_partition_bounds(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);

    assert(OtaGuard_BeginErase(&guard, APP_B_START, 54, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    assert(guard.range_end == IAP_START);
    assert(guard.state == OTA_GUARD_ERASING);

    assert(!OtaGuard_BeginErase(&guard, APP_B_START, 55, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, APP_B_START, 0, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, APP_B_START + 1, 1, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, IAP_START, 1, BLOCK_SIZE,
                                APP_B_START, IAP_START));
}

static void test_order_and_contiguous_ranges(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 2, BLOCK_SIZE,
                               APP_B_START, IAP_START));

    assert(!OtaGuard_CanProgram(&guard, APP_B_START, 240));
    OtaGuard_EndErase(&guard, 1);
    assert(OtaGuard_CanProgram(&guard, APP_B_START, 240));
    assert(!OtaGuard_CanProgram(&guard, APP_B_START + 16, 240));
    OtaGuard_EndProgram(&guard, 240, 1);
    assert(OtaGuard_CanProgram(&guard, APP_B_START + 240, 32));
    OtaGuard_EndProgram(&guard, 32, 1);

    /* 新流程写完即可进入整镜像 CRC，无需再传一遍数据。 */
    assert(OtaGuard_CanFinish(&guard));
    assert(OtaGuard_CanFinish(&guard));
}

static void test_failed_operations_do_not_advance(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 1, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    OtaGuard_EndErase(&guard, 1);

    assert(OtaGuard_CanProgram(&guard, APP_B_START, 16));
    OtaGuard_EndProgram(&guard, 16, 0);
    assert(OtaGuard_CanProgram(&guard, APP_B_START, 16));
    OtaGuard_EndProgram(&guard, 16, 1);

    assert(OtaGuard_CanFinish(&guard));

    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 1, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    OtaGuard_EndErase(&guard, 0);
    assert(guard.state == OTA_GUARD_IDLE);
}

static void test_remote_download_install_and_cancel(void)
{
    uint8_t image[64];
    uint8_t status;
    uint8_t index;
    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(codeflash, 0xFF, sizeof(codeflash));
    for(index = 0; index < sizeof(image); index++) image[index] = (uint8_t)(index + 1U);

    Ota_Init();
    assert(Ota_BeginRemote(0x00021601UL, sizeof(image), crc32(image, sizeof(image)),
                           "http://fw/device.bin", 20) == DEVICE_STATUS_OK);
    assert(Ota_EraseStep() == DEVICE_STATUS_OK);
    assert(Ota_EraseStep() == DEVICE_STATUS_OK);
    assert(Ota_Write(0, image, sizeof(image)) == DEVICE_STATUS_OK);
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_VERIFYING);
    do { status = Ota_VerifyStep(); } while(status == DEVICE_STATUS_BUSY);
    assert(status == DEVICE_STATUS_OK);
    assert(Ota_Get()->state == OTA_STATE_READY);
    assert(Ota_MarkInstall() == DEVICE_STATUS_OK);
    assert(Ota_Get()->state == OTA_STATE_INSTALLING);
    assert(Ota_Cancel() == DEVICE_STATUS_CONFLICT);
}

static void test_interrupted_local_update_is_discarded(void)
{
    memset(dataflash, 0xFF, sizeof(dataflash));
    Ota_Init();
    assert(Ota_BeginLocal(0x00021601UL, 4096U, 0x12345678UL) == DEVICE_STATUS_OK);
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_IDLE);
}

static void test_remote_progress_checkpoint_uses_distance_not_alignment(void)
{
    uint8_t chunk[240];
    uint32_t offset = 0U;
    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(codeflash, 0xFF, sizeof(codeflash));
    memset(chunk, 0x5A, sizeof(chunk));

    Ota_Init();
    assert(Ota_BeginRemote(0x00021601UL, 20000U, 0x12345678UL,
                           "http://fw/device.bin", 20) == DEVICE_STATUS_OK);
    while(Ota_Get()->state == OTA_STATE_ERASING)
        assert(Ota_EraseStep() == DEVICE_STATUS_OK);
    while(offset < 16560U) {
        assert(Ota_Write(offset, chunk, sizeof(chunk)) == DEVICE_STATUS_OK);
        offset += sizeof(chunk);
    }
    assert(Ota_Get()->downloaded_bytes == 16560U);

    /* A reboot resumes at the latest >=16 KB checkpoint even though 240-byte
     * chunks never land exactly on a 16 KB boundary. */
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_DOWNLOADING);
    assert(Ota_Get()->downloaded_bytes == 16560U);
}

int main(void)
{
    test_partition_bounds();
    test_order_and_contiguous_ranges();
    test_failed_operations_do_not_advance();
    test_remote_download_install_and_cancel();
    test_interrupted_local_update_is_discarded();
    test_remote_progress_checkpoint_uses_distance_not_alignment();
    puts("OTA partition/session and staging metadata: PASS");
    return 0;
}
