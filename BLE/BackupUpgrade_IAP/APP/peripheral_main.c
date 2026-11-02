#include "CH58x_common.h"
#include "../../Peripheral/APP/include/ota_update.h"
#include <stddef.h>
#include <string.h>

static ota_metadata_t metadata;
static ota_metadata_t candidate;
static uint32_t metadata_slot;
static uint8_t copy_markers_ready;
static uint8_t copy_buffer[256] __attribute__((aligned(4)));

/* 固定地址描述符供运行应用在 OTA 前确认 Updater 与当前元数据协议兼容。 */
const uint32_t updater_info[4] __attribute__((section(".updater_info"), used)) = {
    OTA_UPDATER_MAGIC, OTA_UPDATER_SCHEMA, OTA_METADATA_MAGIC, 0U
};

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

static uint32_t metadata_crc(const ota_metadata_t *record)
{
    return crc32_update(0xFFFFFFFFUL, (const uint8_t *)record,
                        (uint16_t)offsetof(ota_metadata_t, crc32)) ^ 0xFFFFFFFFUL;
}

static uint8_t metadata_valid(const ota_metadata_t *record)
{
    return record->magic == OTA_METADATA_MAGIC && record->schema == 1U &&
           record->state <= OTA_STATE_INSTALLING &&
           record->url_length < OTA_URL_SIZE &&
           record->image_size <= OTA_MAX_IMAGE_SIZE &&
           record->crc32 == metadata_crc(record);
}

static uint8_t metadata_load(void)
{
    uint8_t found = 0U, corrupt = 0U, slot;
    copy_markers_ready = 0U;

    for(slot = 0U; slot < 2U; slot++) {
        uint32_t address = slot ? OTA_METADATA_B : OTA_METADATA_A;
        uint16_t offset;
        const uint8_t *bytes = (const uint8_t *)&candidate;
        /* 2 means unknown newest state: never boot a possibly partial app. */
        if(EEPROM_READ(address, &candidate, sizeof(candidate)) != 0U) return 2U;
        if(metadata_valid(&candidate)) {
            if(found && metadata.state == OTA_STATE_INSTALLING &&
               candidate.state == OTA_STATE_INSTALLING &&
               metadata.reserved == OTA_COPY_STARTED && candidate.reserved == OTA_COPY_STARTED &&
               metadata.image_size == candidate.image_size &&
               metadata.image_crc32 == candidate.image_crc32 &&
               metadata.update_version == candidate.update_version)
                copy_markers_ready = 1U;
            if(!found || (int32_t)(candidate.generation - metadata.generation) > 0) {
                metadata = candidate;
                metadata_slot = address;
                found = 1U;
            }
        } else {
            for(offset = 0U; offset < sizeof(candidate) && bytes[offset] == 0xFFU; offset++) {}
            if(offset != sizeof(candidate)) corrupt++;
        }
    }
    /* Empty factory records / one interrupted first write may boot normally.
     * Two unreadable generations require recovery, not an unchecked jump. */
    return !found && corrupt == 2U ? 2U : found;
}

static uint8_t metadata_save(void)
{
    uint32_t target = metadata_slot == OTA_METADATA_A ? OTA_METADATA_B : OTA_METADATA_A;

    metadata.generation++;
    metadata.crc32 = metadata_crc(&metadata);
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_READ(target, &candidate, 4U) != 0U || candidate.magic != 0xFFFFFFFFUL ||
       EEPROM_WRITE(target + 4U, (uint8_t *)&metadata + 4U, sizeof(metadata) - 4U) != 0U ||
       EEPROM_READ(target + 4U, (uint8_t *)&candidate + 4U, sizeof(candidate) - 4U) != 0U ||
       memcmp((const uint8_t *)&candidate + 4U, (const uint8_t *)&metadata + 4U, sizeof(metadata) - 4U) ||
       EEPROM_WRITE(target, &metadata, 4U) != 0U ||
       EEPROM_READ(target, &candidate, sizeof(candidate)) != 0U ||
       !metadata_valid(&candidate) || candidate.generation != metadata.generation)
        return 0U;
    metadata_slot = target;
    return 1U;
}

static uint32_t image_crc(uint32_t address, uint32_t size)
{
    uint32_t offset = 0U;
    uint32_t crc = 0xFFFFFFFFUL;

    while(offset < size) {
        uint16_t length = (uint16_t)((size - offset) > sizeof(copy_buffer) ?
                                    sizeof(copy_buffer) : size - offset);
        FLASH_ROM_READ(address + offset, copy_buffer, length);
        crc = crc32_update(crc, copy_buffer, length);
        offset += length;
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t staging_image_valid(void)
{
    FLASH_ROM_READ(OTA_STAGING_ADDRESS, copy_buffer, 24U);
    return Ota_ImageHeaderValid((const uint32_t *)copy_buffer, metadata.image_size) &&
           image_crc(OTA_STAGING_ADDRESS, metadata.image_size) == metadata.image_crc32;
}

static uint8_t install_image(void)
{
    uint32_t offset;
    uint32_t erase_size;

    /* Validate staging before touching the currently runnable image. */
    if(!staging_image_valid())
        return 0U;
    /* 上次复制已完成但元数据提交失败时，不再擦写相同应用。 */
    if(image_crc(OTA_APP_ADDRESS, metadata.image_size) == metadata.image_crc32)
        return 1U;

    /* Both generations must say "possibly partial" before the first erase.
     * A reset between these commits is safe: the application is still intact.
     * Verified markers survive retries, avoiding journal wear on a Flash fault. */
    if(!copy_markers_ready) {
        metadata.reserved = OTA_COPY_STARTED;
        if(!metadata_save() || !metadata_save()) return 0U;
        copy_markers_ready = 1U;
    }

    erase_size = (metadata.image_size + EEPROM_BLOCK_SIZE - 1U) &
                 ~(EEPROM_BLOCK_SIZE - 1U);
    for(offset = 0U; offset < erase_size; offset += EEPROM_BLOCK_SIZE) {
        if(FLASH_ROM_ERASE(OTA_APP_ADDRESS + offset, EEPROM_BLOCK_SIZE) != 0U)
            return 0U;
    }
    for(offset = 0U; offset < metadata.image_size; offset += sizeof(copy_buffer)) {
        uint16_t length = (uint16_t)((metadata.image_size - offset) > sizeof(copy_buffer) ?
                                    sizeof(copy_buffer) : metadata.image_size - offset);
        uint16_t program_length = (uint16_t)((length + 3U) & ~3U);
        memset(copy_buffer, 0xFF, sizeof(copy_buffer));
        FLASH_ROM_READ(OTA_STAGING_ADDRESS + offset, copy_buffer, length);
        if(FLASH_ROM_WRITE(OTA_APP_ADDRESS + offset, copy_buffer, program_length) != 0U)
            return 0U;
    }
    return image_crc(OTA_APP_ADDRESS, metadata.image_size) == metadata.image_crc32;
}

static void jump_to_app(void)
{
    ((void (*)(void))((int *)OTA_APP_ADDRESS))();
}

int main(void)
{
    uint8_t loaded = 2U, attempt;
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    for(attempt = 0U; attempt < 3U; attempt++) {
        loaded = metadata_load();
        if(loaded != 2U) break;
        mDelaymS(1);
    }
    if(loaded == 2U) goto restart;
    if(loaded && metadata.state == OTA_STATE_INSTALLING) {
        if(install_image()) {
            metadata.current_version = metadata.update_version;
            metadata.state = OTA_STATE_IDLE;
            metadata.url_length = 0U;
            metadata.erased_bytes = 0U;
            metadata.downloaded_bytes = 0U;
            metadata.reserved = 0U;
            if(!metadata_save()) goto restart;
        } else if(!staging_image_valid()) {
            if(metadata.reserved == OTA_COPY_STARTED) {
                if(image_crc(OTA_APP_ADDRESS, metadata.image_size) != metadata.image_crc32)
                    goto restart; /* Neither region is verified runnable. Keep the journal. */
                metadata.current_version = metadata.update_version;
            }
            /* Before the first erase, a rejected staging file leaves the old app intact. */
            metadata.state = OTA_STATE_IDLE;
            metadata.url_length = 0U;
            metadata.reserved = 0U;
            if(!metadata_save()) goto restart;
        } else {
            /* The running region may be partially copied. Retry from the intact staging image. */
            goto restart;
        }
    }

    jump_to_app();
restart:
    SYS_ResetExecute();
    while(1) {}
}
