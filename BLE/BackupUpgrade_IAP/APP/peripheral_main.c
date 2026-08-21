#include "CH58x_common.h"
#include "../../Peripheral/APP/include/ota_update.h"
#include <stddef.h>

static ota_metadata_t metadata;
static ota_metadata_t candidate;
static uint32_t metadata_slot;
static uint8_t copy_buffer[256] __attribute__((aligned(4)));

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
    uint8_t found = 0U;

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
    return found;
}

static uint8_t metadata_save(void)
{
    uint32_t target = metadata_slot == OTA_METADATA_A ? OTA_METADATA_B : OTA_METADATA_A;

    metadata.generation++;
    metadata.crc32 = metadata_crc(&metadata);
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_WRITE(target, &metadata, sizeof(metadata)) != 0U ||
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

static uint8_t install_image(void)
{
    uint32_t offset;
    uint32_t erase_size;

    /* Validate staging before touching the currently runnable image. */
    if(metadata.image_size == 0U || metadata.image_size > OTA_MAX_IMAGE_SIZE ||
       image_crc(OTA_STAGING_ADDRESS, metadata.image_size) != metadata.image_crc32)
        return 0U;

    erase_size = (metadata.image_size + EEPROM_BLOCK_SIZE - 1U) &
                 ~(EEPROM_BLOCK_SIZE - 1U);
    for(offset = 0U; offset < erase_size; offset += EEPROM_BLOCK_SIZE) {
        if(FLASH_ROM_ERASE(OTA_APP_ADDRESS + offset, EEPROM_BLOCK_SIZE) != 0U)
            return 0U;
    }
    for(offset = 0U; offset < metadata.image_size; offset += sizeof(copy_buffer)) {
        uint16_t length = (uint16_t)((metadata.image_size - offset) > sizeof(copy_buffer) ?
                                    sizeof(copy_buffer) : metadata.image_size - offset);
        FLASH_ROM_READ(OTA_STAGING_ADDRESS + offset, copy_buffer, length);
        if(FLASH_ROM_WRITE(OTA_APP_ADDRESS + offset, copy_buffer, length) != 0U ||
           FLASH_ROM_VERIFY(OTA_APP_ADDRESS + offset, copy_buffer, length) != 0U)
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
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    if(metadata_load() && metadata.state == OTA_STATE_INSTALLING) {
        if(install_image()) {
            metadata.current_version = metadata.update_version;
            metadata.state = OTA_STATE_IDLE;
            metadata.url_length = 0U;
            metadata.erased_bytes = 0U;
            metadata.downloaded_bytes = 0U;
            (void)metadata_save();
        } else if(image_crc(OTA_STAGING_ADDRESS, metadata.image_size) != metadata.image_crc32) {
            /* A bad staging image must not prevent the known application from booting. */
            metadata.state = OTA_STATE_IDLE;
            metadata.url_length = 0U;
            (void)metadata_save();
        } else {
            /* The running region may be partially copied. Retry from the intact staging image. */
            SYS_ResetExecute();
        }
    }

    jump_to_app();
    while(1) {}
}
