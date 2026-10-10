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
static uint8_t fail_metadata;

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
uint32_t Test_EepromRead(uint32_t address, void *buffer, uint32_t length) { if(fail_metadata==3)return 1; memcpy(buffer, dataflash + address, length); return 0; }
uint32_t Test_EepromErase(uint32_t address, uint32_t length) { if(fail_metadata==1)return 1; memset(dataflash + address, 0xFF, length); return 0; }
uint32_t Test_EepromWrite(uint32_t address, const void *buffer, uint32_t length) { if(fail_metadata==2)return 1; memcpy(dataflash + address, buffer, length); return 0; }
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
    assert(Ota_BeginRemote(FIRMWARE_BUILD_VERSION + 1U, sizeof(image), crc32(image, sizeof(image)),
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
    /* Updater 已返回应用但上一启动的元数据提交失败，可在应用启动时清理。 */
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_IDLE);
    assert(Ota_Get()->current_version == FIRMWARE_BUILD_VERSION);
}

static void test_interrupted_local_update_is_discarded(void)
{
    memset(dataflash, 0xFF, sizeof(dataflash));
    Ota_Init();
    assert(Ota_BeginLocal(FIRMWARE_BUILD_VERSION + 1U, 4096U, 0x12345678UL) == DEVICE_STATUS_OK);
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_IDLE);
}

static void test_same_or_older_version_is_allowed(void)
{
    memset(dataflash, 0xFF, sizeof(dataflash));
    Ota_Init();
    assert(Ota_BeginLocal(1U, 4096U, 0x12345678UL) == DEVICE_STATUS_OK);
    assert(Ota_Cancel() == DEVICE_STATUS_OK);
    assert(Ota_BeginRemote(1U, 4096U, 0x12345678UL,
                           "http://fw/device.bin", 20) == DEVICE_STATUS_OK);
}

static void test_local_update_accepts_a_partial_final_dword(void)
{
    uint8_t image[65];
    uint8_t status;
    uint8_t index;

    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(codeflash, 0xFF, sizeof(codeflash));
    for(index = 0U; index < sizeof(image); index++) image[index] = (uint8_t)(index ^ 0xA5U);

    Ota_Init();
    assert(Ota_BeginLocal(FIRMWARE_BUILD_VERSION + 1U, sizeof(image), crc32(image, sizeof(image))) == DEVICE_STATUS_OK);
    assert(Ota_Write(0U, image, sizeof(image)) == DEVICE_STATUS_OK);
    assert(Ota_FinishLocal() == DEVICE_STATUS_OK);
    do { status = Ota_VerifyStep(); } while(status == DEVICE_STATUS_BUSY);
    assert(status == DEVICE_STATUS_OK);
    assert(Ota_Get()->state == OTA_STATE_READY);
}

static void write_remote_block(uint32_t *offset, uint8_t *chunk)
{
    uint16_t length;
    uint32_t end = *offset + BLOCK_SIZE;
    while(*offset < end) {
        length = (uint16_t)(end - *offset);
        if(length > 240U) length = 240U;
        assert(Ota_Write(*offset, chunk, length) == DEVICE_STATUS_OK);
        *offset += length;
    }
}

static void test_remote_progress_uses_live_offset_and_aligned_checkpoint(void)
{
    uint8_t chunk[240];
    uint32_t offset = 0U;
    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(codeflash, 0xFF, sizeof(codeflash));
    memset(chunk, 0x5A, sizeof(chunk));

    Ota_Init();
    assert(Ota_BeginRemote(FIRMWARE_BUILD_VERSION + 1U, 20000U, 0x12345678UL,
                           "http://fw/device.bin", 20) == DEVICE_STATUS_OK);
    while(Ota_Get()->state == OTA_STATE_ERASING)
        assert(Ota_EraseStep() == DEVICE_STATUS_OK);
    write_remote_block(&offset, chunk);
    assert(Ota_GetWriteOffset() == BLOCK_SIZE);
    assert(Ota_Get()->downloaded_bytes == 0U);
    write_remote_block(&offset, chunk);
    write_remote_block(&offset, chunk);
    write_remote_block(&offset, chunk);
    assert(Ota_GetWriteOffset() == 4U * BLOCK_SIZE);
    assert(Ota_Get()->downloaded_bytes == 4U * BLOCK_SIZE);

    /* A reboot resumes at the aligned 16 KB checkpoint, so only the following
     * partially written erase block ever needs to be discarded. */
    Ota_Init();
    assert(Ota_Get()->state == OTA_STATE_DOWNLOADING);
    assert(Ota_Get()->downloaded_bytes == 4U * BLOCK_SIZE);
    assert(Ota_RewindDownload() == DEVICE_STATUS_OK);
    assert(Ota_GetWriteOffset() == 4U * BLOCK_SIZE);
}

int main(void)
{
    test_partition_bounds();
    test_order_and_contiguous_ranges();
    test_failed_operations_do_not_advance();
    test_remote_download_install_and_cancel();
    test_interrupted_local_update_is_discarded();
    test_same_or_older_version_is_allowed();
    test_local_update_accepts_a_partial_final_dword();
    test_remote_progress_uses_live_offset_and_aligned_checkpoint();
    /* 每个元数据 IO 阶段失败都应恢复完整状态，取消和重试不需要重启。 */
    for(uint8_t stage=1;stage<=3;stage++) {
        uint8_t image[16]={1,2,3};
        memset(dataflash,0xFF,sizeof(dataflash)); Ota_Init();
        assert(Ota_BeginLocal(1,sizeof(image),crc32(image,sizeof(image)))==DEVICE_STATUS_OK);
        assert(Ota_Write(0,image,sizeof(image))==DEVICE_STATUS_OK);
        assert(Ota_FinishLocal()==DEVICE_STATUS_OK);
        while(Ota_VerifyStep()==DEVICE_STATUS_BUSY) {}
        assert(Ota_Get()->state==OTA_STATE_READY);
        ota_metadata_t before=*Ota_Get();
        fail_metadata=stage;
        assert(Ota_MarkInstall()==DEVICE_STATUS_IO_ERROR);
        assert(!memcmp(&before,Ota_Get(),sizeof(before)));
        fail_metadata=0;
        assert(Ota_Cancel()==DEVICE_STATUS_OK);
        assert(Ota_BeginLocal(2,sizeof(image),0)==DEVICE_STATUS_OK);
        fail_metadata=stage;
        assert(Ota_Cancel()==DEVICE_STATUS_IO_ERROR);
        assert(Ota_Get()->state==OTA_STATE_DOWNLOADING);
        fail_metadata=0;
        assert(Ota_Cancel()==DEVICE_STATUS_OK);
    }
    /* 错误本地请求不能先取消一个有效远程会话。 */
    assert(Ota_BeginRemote(1,4096,0,"http://fw/bin",13)==DEVICE_STATUS_OK);
    assert(Ota_BeginLocal(1,0,0)==DEVICE_STATUS_INVALID_ARG);
    assert(Ota_Get()->state==OTA_STATE_ERASING && Ota_Get()->url_length==13);
    puts("OTA partition/session and staging metadata: PASS");
    return 0;
}
