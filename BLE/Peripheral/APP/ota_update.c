#include "ota_update.h"
#include "config_store.h"
#include "device_protocol.h"
#include "CH58x_common.h"
#include <stddef.h>
#include <string.h>

static ota_metadata_t metadata;
static uint32_t metadata_slot;
static uint32_t write_offset;
static uint32_t verify_offset;
static uint32_t verify_crc;
static uint8_t resume_erase_required;
static uint8_t write_buffer[244] __attribute__((aligned(4)));
static uint8_t verify_buffer[64] __attribute__((aligned(4)));

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint16_t length)
{
    uint16_t index;
    uint8_t bit;
    for(index = 0U; index < length; index++) {
        crc ^= data[index];
        for(bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}

static uint8_t metadata_valid(const ota_metadata_t *record)
{
    return record->magic == OTA_METADATA_MAGIC && record->schema == 1U &&
           record->state <= OTA_STATE_INSTALLING &&
           record->url_length < OTA_URL_SIZE &&
           record->image_size <= OTA_MAX_IMAGE_SIZE &&
           record->downloaded_bytes <= record->image_size &&
           record->erased_bytes <= record->image_size &&
           record->crc32 == Config_Crc32((const uint8_t *)record,
                                            (uint16_t)offsetof(ota_metadata_t, crc32));
}

static uint8_t metadata_save(void)
{
    ota_metadata_t readback;
    uint32_t target = metadata_slot == OTA_METADATA_A ? OTA_METADATA_B : OTA_METADATA_A;

    metadata.magic = OTA_METADATA_MAGIC;
    metadata.schema = 1U;
    metadata.generation++;
    metadata.crc32 = Config_Crc32((const uint8_t *)&metadata,
                                    (uint16_t)offsetof(ota_metadata_t, crc32));
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_WRITE(target, &metadata, sizeof(metadata)) != 0U ||
       EEPROM_READ(target, &readback, sizeof(readback)) != 0U ||
       !metadata_valid(&readback) || readback.generation != metadata.generation)
        return DEVICE_STATUS_IO_ERROR;

    metadata_slot = target;
    return DEVICE_STATUS_OK;
}

void Ota_Init(void)
{
    ota_metadata_t candidate;
    uint8_t found = 0U;

    memset(&metadata, 0, sizeof(metadata));
    if(EEPROM_READ(OTA_METADATA_A, &candidate, sizeof(candidate)) == 0U &&
       metadata_valid(&candidate)) {
        metadata = candidate;
        metadata_slot = OTA_METADATA_A;
        found = 1U;
    }
    if(EEPROM_READ(OTA_METADATA_B, &candidate, sizeof(candidate)) == 0U &&
       metadata_valid(&candidate) &&
       (!found || (int32_t)(candidate.generation - metadata.generation) > 0)) {
        metadata = candidate;
        metadata_slot = OTA_METADATA_B;
        found = 1U;
    }

    if(!found) {
        memset(&metadata, 0, sizeof(metadata));
        metadata.current_version = FIRMWARE_BUILD_VERSION;
        metadata_slot = OTA_METADATA_B;
        (void)metadata_save();
    }

    /* The running image is the authoritative version.  This also keeps the
     * metadata correct after a wired factory/recovery flash. */
    if(metadata.current_version != FIRMWARE_BUILD_VERSION) {
        metadata.current_version = FIRMWARE_BUILD_VERSION;
        if(metadata.state != OTA_STATE_IDLE &&
           metadata.state != OTA_STATE_INSTALLING &&
           metadata.update_version <= metadata.current_version) {
            metadata.state = OTA_STATE_IDLE;
            metadata.url_length = 0U;
            metadata.erased_bytes = 0U;
            metadata.downloaded_bytes = 0U;
        }
        (void)metadata_save();
    }

    write_offset = metadata.downloaded_bytes;
    verify_offset = 0U;
    verify_crc = 0xFFFFFFFFUL;
    resume_erase_required = 0U;

    if(metadata.state == OTA_STATE_DOWNLOADING) {
        if(metadata.url_length == 0U) {
            /* BLE transfers have no persistent source URL to resume from. */
            metadata.state = OTA_STATE_IDLE;
            metadata.downloaded_bytes = 0U;
            metadata.erased_bytes = 0U;
            (void)metadata_save();
        } else if(metadata.downloaded_bytes == metadata.image_size) {
            /* Power may fail after the last committed block and before the
             * state changes to VERIFYING.  Continue with a full CRC pass. */
            metadata.state = OTA_STATE_VERIFYING;
            (void)metadata_save();
        } else {
            /* downloaded_bytes is committed only at a 4 KB boundary.  The
             * following block may contain a partial write from before reset,
             * so erase just that block before resuming from the checkpoint. */
            resume_erase_required = 1U;
        }
    }
}

const ota_metadata_t *Ota_Get(void)
{
    return &metadata;
}

uint32_t Ota_StagingAddress(void)
{
    return OTA_STAGING_ADDRESS;
}

uint8_t Ota_BeginRemote(uint32_t version, uint32_t image_size, uint32_t image_crc32,
                        const char *url, uint8_t url_length)
{
    if(!url || url_length == 0U || url_length >= OTA_URL_SIZE ||
       image_size == 0U || image_size > OTA_MAX_IMAGE_SIZE ||
       version <= metadata.current_version)
        return DEVICE_STATUS_INVALID_ARG;

    if(metadata.state != OTA_STATE_IDLE) {
        /* Reissuing the identical offer explicitly resumes a paused transfer.
         * A different file still requires CANCEL first, preventing two images
         * from being mixed in the staging region. */
        if((metadata.state == OTA_STATE_ERASING ||
            metadata.state == OTA_STATE_DOWNLOADING) &&
           metadata.update_version == version && metadata.image_size == image_size &&
           metadata.image_crc32 == image_crc32 && metadata.url_length == url_length &&
           memcmp(metadata.url, url, url_length) == 0)
            return DEVICE_STATUS_OK;
        return DEVICE_STATUS_BUSY;
    }

    memset(metadata.url, 0, sizeof(metadata.url));
    memcpy(metadata.url, url, url_length);
    metadata.url_length = url_length;
    metadata.update_version = version;
    metadata.image_size = image_size;
    metadata.image_crc32 = image_crc32;
    metadata.erased_bytes = 0U;
    metadata.downloaded_bytes = 0U;
    metadata.state = OTA_STATE_ERASING;
    write_offset = 0U;
    resume_erase_required = 0U;
    return metadata_save();
}

uint8_t Ota_BeginLocal(uint32_t version, uint32_t image_size, uint32_t image_crc32)
{
    if(metadata.state != OTA_STATE_IDLE) return DEVICE_STATUS_BUSY;
    if(version <= metadata.current_version || image_size == 0U ||
       image_size > OTA_MAX_IMAGE_SIZE) return DEVICE_STATUS_INVALID_ARG;

    metadata.url_length = 0U;
    metadata.update_version = version;
    metadata.image_size = image_size;
    metadata.image_crc32 = image_crc32;
    metadata.erased_bytes = image_size;
    metadata.downloaded_bytes = 0U;
    metadata.state = OTA_STATE_DOWNLOADING;
    write_offset = 0U;
    resume_erase_required = 0U;
    return metadata_save();
}

uint8_t Ota_FinishLocal(void)
{
    if(metadata.state != OTA_STATE_DOWNLOADING) return DEVICE_STATUS_CONFLICT;
    metadata.downloaded_bytes = metadata.image_size;
    write_offset = metadata.image_size;
    if(metadata_save() != DEVICE_STATUS_OK) return DEVICE_STATUS_IO_ERROR;
    return Ota_BeginVerify();
}

uint8_t Ota_EraseStep(void)
{
    uint32_t remaining;

    if(metadata.state != OTA_STATE_ERASING) return DEVICE_STATUS_CONFLICT;
    remaining = metadata.image_size - metadata.erased_bytes;
    if(remaining != 0U) {
        if(FLASH_ROM_ERASE(OTA_STAGING_ADDRESS + metadata.erased_bytes,
                           EEPROM_BLOCK_SIZE) != 0U)
            return DEVICE_STATUS_IO_ERROR;
        metadata.erased_bytes += remaining > EEPROM_BLOCK_SIZE ? EEPROM_BLOCK_SIZE : remaining;
        return metadata_save();
    }

    metadata.state = OTA_STATE_DOWNLOADING;
    write_offset = metadata.downloaded_bytes;
    resume_erase_required = 0U;
    return metadata_save();
}

uint8_t Ota_Write(uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint32_t end;
    uint16_t program_length;

    if(metadata.state != OTA_STATE_DOWNLOADING || !data || length == 0U ||
       length > 240U || offset != write_offset) return DEVICE_STATUS_CONFLICT;
    end = offset + length;
    if(end < offset || end > metadata.image_size) return DEVICE_STATUS_INVALID_ARG;
    /* ISP583 programs whole dwords and requires the source buffer to be RAM
     * and 4-byte aligned.  Keep one fixed aligned scratch buffer so the
     * remote HTTP path and the local BLE path obey the same rule.  The final
     * partial dword is padded with erased-state bytes; the image CRC covers
     * only the requested image_size bytes. */
    program_length = (uint16_t)((length + 3U) & (uint16_t)~3U);
    if(program_length > sizeof(write_buffer)) return DEVICE_STATUS_INVALID_ARG;
    memcpy(write_buffer, data, length);
    if(program_length > length)
        memset(write_buffer + length, 0xFF, program_length - length);
    if(FLASH_ROM_WRITE(OTA_STAGING_ADDRESS + offset, write_buffer, program_length) != 0U)
        return DEVICE_STATUS_IO_ERROR;

    write_offset = end;
    if((write_offset % EEPROM_BLOCK_SIZE) == 0U || write_offset == metadata.image_size) {
        metadata.downloaded_bytes = write_offset;
        return metadata_save();
    }
    return DEVICE_STATUS_OK;
}

uint8_t Ota_RewindDownload(void)
{
    if(metadata.state != OTA_STATE_DOWNLOADING) return DEVICE_STATUS_CONFLICT;
    if(write_offset == metadata.downloaded_bytes && !resume_erase_required)
        return DEVICE_STATUS_OK;
    if(metadata.downloaded_bytes >= metadata.image_size ||
       (metadata.downloaded_bytes % EEPROM_BLOCK_SIZE) != 0U)
        return DEVICE_STATUS_CONFLICT;
    if(FLASH_ROM_ERASE(OTA_STAGING_ADDRESS + metadata.downloaded_bytes,
                       EEPROM_BLOCK_SIZE) != 0U)
        return DEVICE_STATUS_IO_ERROR;
    write_offset = metadata.downloaded_bytes;
    resume_erase_required = 0U;
    return DEVICE_STATUS_OK;
}

uint8_t Ota_BeginVerify(void)
{
    if(metadata.state != OTA_STATE_DOWNLOADING || write_offset != metadata.image_size)
        return DEVICE_STATUS_CONFLICT;
    metadata.state = OTA_STATE_VERIFYING;
    verify_offset = 0U;
    verify_crc = 0xFFFFFFFFUL;
    return metadata_save();
}

uint8_t Ota_VerifyStep(void)
{
    uint16_t length;

    if(metadata.state != OTA_STATE_VERIFYING) return DEVICE_STATUS_CONFLICT;
    if(verify_offset < metadata.image_size) {
        length = (uint16_t)((metadata.image_size - verify_offset) > sizeof(verify_buffer) ?
                           sizeof(verify_buffer) : metadata.image_size - verify_offset);
        FLASH_ROM_READ(OTA_STAGING_ADDRESS + verify_offset, verify_buffer, length);
        verify_crc = crc32_update(verify_crc, verify_buffer, length);
        verify_offset += length;
        return DEVICE_STATUS_BUSY;
    }

    if((verify_crc ^ 0xFFFFFFFFUL) != metadata.image_crc32) {
        metadata.state = OTA_STATE_IDLE;
        metadata.url_length = 0U;
        (void)metadata_save();
        return DEVICE_STATUS_VERIFY_FAILED;
    }
    metadata.state = OTA_STATE_READY;
    return metadata_save();
}

uint8_t Ota_MarkInstall(void)
{
    if(metadata.state != OTA_STATE_READY) return DEVICE_STATUS_CONFLICT;
    metadata.state = OTA_STATE_INSTALLING;
    return metadata_save();
}

uint8_t Ota_Cancel(void)
{
    if(metadata.state == OTA_STATE_INSTALLING) return DEVICE_STATUS_CONFLICT;
    metadata.state = OTA_STATE_IDLE;
    metadata.url_length = 0U;
    metadata.erased_bytes = 0U;
    metadata.downloaded_bytes = 0U;
    resume_erase_required = 0U;
    return metadata_save();
}
