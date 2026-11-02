#ifndef SPLITAC_OTA_UPDATE_H
#define SPLITAC_OTA_UPDATE_H

#include <stdint.h>
#include "product_version.h"

/* The linker and flash partition remain unchanged.  The second application
 * region is staging storage only; it is never executed directly. */
#define OTA_APP_ADDRESS           0x00001000UL
#define OTA_STAGING_ADDRESS       0x00037000UL
#define OTA_APP_SIZE              (216UL * 1024UL)
#define OTA_MAX_IMAGE_SIZE        (208UL * 1024UL)
#define OTA_MIN_IMAGE_SIZE        64UL
#define OTA_METADATA_A            0x00007000UL
#define OTA_METADATA_B            0x00007100UL
#define OTA_URL_SIZE              128U
#define OTA_METADATA_MAGIC        0x3154414FUL
#define OTA_BOOT_ENTRY_WORD       0x0006D06FUL
#define OTA_UPDATER_INFO_ADDRESS  0x0006FFF0UL
#define OTA_UPDATER_MAGIC         0x31504455UL
#define OTA_UPDATER_SCHEMA        1U
#define OTA_COPY_STARTED          1U /* Existing metadata.reserved: app may be partial. */

/* Existing CH583 startup layout; no new package header or version restriction.
 * This rejects obvious non-APP data, not arbitrary faulty application logic. */
static inline uint8_t Ota_ImageHeaderValid(const uint32_t *header, uint32_t size)
{
    uint32_t entry = header[0], offset;
    uint8_t i;
    if(size < OTA_MIN_IMAGE_SIZE || size > OTA_MAX_IMAGE_SIZE ||
       (entry & 0xFFFU) != 0x06FU || (entry & 0x80000000UL) ||
       header[1] != 0U || header[2] != 0U || header[5] != 0xF3F9BDA9UL) return 0U;
    offset = ((entry >> 21) & 0x3FFU) << 1;
    offset |= ((entry >> 20) & 1U) << 11;
    offset |= ((entry >> 12) & 0xFFU) << 12;
    if(offset < 24U || offset > size - 4U) return 0U;
    for(i = 3U; i <= 4U; i++) {
        uint32_t handler = header[i];
        if((handler & 1U) ||
           !((handler >= 0x20000000UL && handler < 0x20008000UL) ||
             (handler >= OTA_APP_ADDRESS + 24U && handler < OTA_APP_ADDRESS + size))) return 0U;
    }
    return 1U;
}

#ifndef FIRMWARE_BUILD_VERSION
#error "FIRMWARE_BUILD_VERSION must be derived from product_version.h by the build script"
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY,
    OTA_STATE_INSTALLING
} ota_state_t;

/* packed 固定落盘布局；aligned(4) 满足 DataFlash 缓冲区对齐要求，不改尺寸。 */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;
    uint8_t schema;
    uint8_t state;
    uint8_t url_length;
    uint8_t reserved;
    uint32_t generation;
    uint32_t current_version;
    uint32_t update_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t erased_bytes;
    uint32_t downloaded_bytes;
    char url[OTA_URL_SIZE];
    uint32_t crc32;
} ota_metadata_t;

void Ota_Init(void);
const ota_metadata_t *Ota_Get(void);
uint32_t Ota_StagingAddress(void);
uint32_t Ota_GetWriteOffset(void);
uint8_t Ota_BootloaderReady(void);
uint8_t Ota_BeginRemote(uint32_t version, uint32_t image_size, uint32_t image_crc32,
                        const char *url, uint8_t url_length);
uint8_t Ota_BeginLocal(uint32_t version, uint32_t image_size, uint32_t image_crc32);
uint8_t Ota_FinishLocal(void);
uint8_t Ota_EraseStep(void);
uint8_t Ota_Write(uint32_t offset, const uint8_t *data, uint16_t length);
uint8_t Ota_RewindDownload(void);
uint8_t Ota_BeginVerify(void);
uint8_t Ota_VerifyStep(void);
uint8_t Ota_MarkInstall(void);
uint8_t Ota_Cancel(void);

#endif
