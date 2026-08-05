#ifndef SPLITAC_CONFIG_STORE_V2_H
#define SPLITAC_CONFIG_STORE_V2_H

#include <stdint.h>

#define V2_CONFIG_SCHEMA_VERSION 3u
#define V2_CONFIG_SLOT_A         0x0000u
#define V2_CONFIG_SLOT_B         0x1000u
#define V2_RESERVED_PAGE         0x2000u
#define V2_RUNTIME_PAGE          0x3000u
#define V2_IR_PAGE               0x4000u
#define V2_HEALTH_PAGE           0x5000u
#define V2_HEALTH_REGION_SIZE    0x0E00u
#define V2_RUNTIME_BACKUP_PAGE   0x5E00u
#define V2_IR_SLOT_B             0x6000u

/*
 * 启动配置状态只占一个字节，用于区分“全新空白”“已自愈”和
 * “存储仍退化”。状态不会写入固定网关协议。
 */
typedef enum {
    CONFIG_V2_LOAD_HEALTHY = 0,
    CONFIG_V2_LOAD_EMPTY,
    CONFIG_V2_LOAD_REPAIRED,
    CONFIG_V2_LOAD_DEFAULTS_RECOVERED,
    CONFIG_V2_LOAD_DEGRADED,
    CONFIG_V2_LOAD_CORRUPT,
    CONFIG_V2_LOAD_IO_ERROR,
    CONFIG_V2_LOAD_INITIALIZED
} config_v2_load_state_t;

#define STORAGE_V2_STARTUP_RECOVERED 0x01U
#define STORAGE_V2_STARTUP_DEGRADED  0x02U

uint32_t ConfigV2_Crc32(const uint8_t *data, uint16_t len);
uint8_t ConfigV2_Load(void);
uint8_t ConfigV2_Commit(uint32_t expected_revision);
uint8_t ConfigV2_CommitIfChanged(void);
uint8_t ConfigV2_ValidateCurrent(void);
uint32_t ConfigV2_GetRevision(void);
config_v2_load_state_t ConfigV2_GetLoadState(void);
void ConfigV2_FactoryDefaults(void);
uint8_t ConfigV2_InitializeDefaults(uint8_t recovered_from_corruption);
uint8_t RuntimeV2_Load(void);
uint8_t RuntimeV2_Append(void);
uint8_t IrStoreV2_Load(void);
uint8_t IrStoreV2_SaveIfChanged(void);
uint8_t LoraParamsV2_Load(void);
uint8_t LoraParamsV2_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw);
uint8_t LoraParamsV2_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw);
uint8_t StorageV2_FactoryReset(void);
uint8_t StorageV2_GetStartupFlags(void);

#endif
