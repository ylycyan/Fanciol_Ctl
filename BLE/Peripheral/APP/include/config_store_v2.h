#ifndef SPLITAC_CONFIG_STORE_V2_H
#define SPLITAC_CONFIG_STORE_V2_H

#include <stdint.h>

#define V2_CONFIG_SCHEMA_VERSION 3u
#define V2_CONFIG_SLOT_A         0x0000u
#define V2_CONFIG_SLOT_B         0x1000u
#define V2_RESERVED_PAGE         0x2000u
#define V2_CONNECTIVITY_SLOT_A   V2_RESERVED_PAGE
#define V2_CONNECTIVITY_SLOT_B   (V2_RESERVED_PAGE + 0x0100u)
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

#define CONNECTIVITY_V2_LORA          0x01U
#define CONNECTIVITY_V2_CELLULAR      0x02U
#define CONNECTIVITY_V2_SCHEMA        1U

/* 数组长度包含结尾 NUL；上位机可写入的最大字符数需减一。 */
#define CONNECTIVITY_HOST_SIZE        40U
#define CONNECTIVITY_CLIENT_ID_SIZE   28U
#define CONNECTIVITY_USERNAME_SIZE    24U
#define CONNECTIVITY_PASSWORD_SIZE    40U
#define CONNECTIVITY_TOPIC_SIZE       32U
#define CONNECTIVITY_APN_SIZE         20U

typedef struct __attribute__((packed)) {
    uint8_t transport_mask;
    uint8_t lora_register_sf;
    uint8_t lora_register_bw;
    uint8_t lora_listen_sf;
    uint8_t lora_listen_bw;
    uint16_t mqtt_port;
    uint16_t mqtt_keepalive_sec;
    uint16_t report_interval_sec;
    uint8_t mqtt_qos;
    char mqtt_host[CONNECTIVITY_HOST_SIZE];
    char mqtt_client_id[CONNECTIVITY_CLIENT_ID_SIZE];
    char mqtt_username[CONNECTIVITY_USERNAME_SIZE];
    char mqtt_password[CONNECTIVITY_PASSWORD_SIZE];
    char publish_topic[CONNECTIVITY_TOPIC_SIZE];
    char subscribe_topic[CONNECTIVITY_TOPIC_SIZE];
    char apn[CONNECTIVITY_APN_SIZE];
} connectivity_config_v2_t;

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
uint8_t ConnectivityV2_Load(void);
uint8_t ConnectivityV2_Save(const connectivity_config_v2_t *config);
uint8_t ConnectivityV2_Validate(const connectivity_config_v2_t *config);
const connectivity_config_v2_t *ConnectivityV2_Get(void);
uint32_t ConnectivityV2_GetGeneration(void);
uint8_t ConnectivityV2_Encode(uint8_t *payload, uint16_t capacity, uint16_t *length);
uint8_t ConnectivityV2_Decode(const uint8_t *payload, uint16_t length,
                              connectivity_config_v2_t *config);
uint8_t ConnectivityV2_LoraEnabled(void);
uint8_t ConnectivityV2_CellularEnabled(void);
uint8_t StorageV2_FactoryReset(void);
uint8_t StorageV2_GetStartupFlags(void);

#endif
