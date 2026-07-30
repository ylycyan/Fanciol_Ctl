#ifndef SPLITAC_CONFIG_STORE_V2_H
#define SPLITAC_CONFIG_STORE_V2_H

#include <stdint.h>

#define V2_CONFIG_SCHEMA_VERSION 2u
#define V2_CONFIG_SLOT_A         0x0000u
#define V2_CONFIG_SLOT_B         0x1000u
#define V2_RESERVED_PAGE         0x2000u
#define V2_RUNTIME_PAGE          0x3000u
#define V2_IR_PAGE               0x4000u

uint32_t ConfigV2_Crc32(const uint8_t *data, uint16_t len);
uint8_t ConfigV2_Load(void);
uint8_t ConfigV2_Commit(uint32_t expected_revision);
uint8_t ConfigV2_CommitIfChanged(void);
uint8_t ConfigV2_ValidateCurrent(void);
uint32_t ConfigV2_GetRevision(void);
void ConfigV2_FactoryDefaults(void);
uint8_t RuntimeV2_Load(void);
uint8_t RuntimeV2_Append(void);
uint8_t IrStoreV2_Load(void);
uint8_t IrStoreV2_SaveIfChanged(void);
uint8_t LoraParamsV2_Load(void);
uint8_t LoraParamsV2_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw);
uint8_t LoraParamsV2_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw);

#endif
