#ifndef SPLITAC_OTA_UPDATE_H
#define SPLITAC_OTA_UPDATE_H

#include <stdint.h>

/* The linker and flash partition remain unchanged.  The second application
 * region is staging storage only; it is never executed directly. */
#define OTA_APP_ADDRESS           0x00001000UL
#define OTA_STAGING_ADDRESS       0x00037000UL
#define OTA_APP_SIZE              (216UL * 1024UL)
#define OTA_MAX_IMAGE_SIZE        (208UL * 1024UL)
#define OTA_METADATA_A            0x00007000UL
#define OTA_METADATA_B            0x00007100UL
#define OTA_URL_SIZE              128U
#define OTA_METADATA_MAGIC        0x3154414FUL

#ifndef FIRMWARE_BUILD_VERSION
#define FIRMWARE_BUILD_VERSION    0x0002160CUL
#endif

#ifndef HARDWARE_BUILD_VERSION
#define HARDWARE_BUILD_VERSION    "HW1.0"
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_READY,
    OTA_STATE_INSTALLING
} ota_state_t;

typedef struct __attribute__((packed)) {
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
