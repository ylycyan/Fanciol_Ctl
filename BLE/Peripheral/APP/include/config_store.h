#ifndef SPLITAC_CONFIG_STORE_H
#define SPLITAC_CONFIG_STORE_H

#include <stdint.h>

#define CONFIG_SCHEMA_VERSION 3u
#define CONFIG_SLOT_A         0x0000u
#define CONFIG_SLOT_B         0x1000u
#define STORAGE_RESERVED_PAGE         0x2000u
#define CONNECTIVITY_SLOT_A   STORAGE_RESERVED_PAGE
#define CONNECTIVITY_SLOT_B   (STORAGE_RESERVED_PAGE + 0x0100u)
#define DEVICE_PROFILE_SLOT_A (STORAGE_RESERVED_PAGE + 0x0200u)
#define DEVICE_PROFILE_SLOT_B (STORAGE_RESERVED_PAGE + 0x0300u)
#define RUNTIME_PAGE          0x3000u
#define IR_STORAGE_PAGE               0x4000u
#define HEALTH_STORAGE_PAGE           0x5000u
#define HEALTH_STORAGE_REGION_SIZE    0x0E00u
#define RUNTIME_BACKUP_PAGE   0x5E00u
#define IR_STORAGE_SLOT_B             0x6000u

/*
 * 启动配置状态只占一个字节，用于区分“全新空白”“已自愈”和
 * “存储仍退化”。状态不会写入固定网关协议。
 */
typedef enum {
    CONFIG_LOAD_HEALTHY = 0,
    CONFIG_LOAD_EMPTY,
    CONFIG_LOAD_REPAIRED,
    CONFIG_LOAD_DEFAULTS_RECOVERED,
    CONFIG_LOAD_DEGRADED,
    CONFIG_LOAD_CORRUPT,
    CONFIG_LOAD_IO_ERROR,
    CONFIG_LOAD_INITIALIZED
} config_load_state_t;

#define STORAGE_STARTUP_RECOVERED 0x01U
#define STORAGE_STARTUP_DEGRADED  0x02U

#define CONNECTIVITY_LORA          0x01U
#define CONNECTIVITY_CELLULAR      0x02U
#define CONNECTIVITY_SCHEMA        3U
#define DEVICE_PROFILE_NAME_MAX    15U

/* 数组长度包含结尾 NUL；上位机可写入的最大字符数需减一。 */
#define CONNECTIVITY_HOST_SIZE        48U
#define CONNECTIVITY_APN_SIZE         20U
#define CONNECTIVITY_CLIENT_ID_SIZE   32U
#define CONNECTIVITY_TOPIC_SIZE       32U

#define CONNECTIVITY_PDP_IPV4         0U
#define CONNECTIVITY_PDP_IPV4V6       1U

typedef struct __attribute__((packed)) {
    uint8_t transport_mask;
    uint8_t lora_register_sf;
    uint8_t lora_register_bw;
    uint8_t lora_listen_sf;
    uint8_t lora_listen_bw;
    uint8_t cellular_pdp_type;
    uint8_t mqtt_qos;
    uint8_t mqtt_clean_session;
    uint16_t mqtt_port;
    uint16_t mqtt_keepalive_sec;
    uint16_t report_interval_sec;
    uint16_t network_timeout_sec;
    char mqtt_host[CONNECTIVITY_HOST_SIZE];
    char apn[CONNECTIVITY_APN_SIZE];
    char mqtt_client_id[CONNECTIVITY_CLIENT_ID_SIZE];
    char mqtt_topic_prefix[CONNECTIVITY_TOPIC_SIZE];
} connectivity_config_t;

uint32_t Config_Crc32(const uint8_t *data, uint16_t len);
uint8_t Config_Load(void);
uint8_t Config_Commit(uint32_t expected_revision);
uint8_t Config_CommitIfChanged(void);
uint8_t Config_ValidateCurrent(void);
uint32_t Config_GetRevision(void);
config_load_state_t Config_GetLoadState(void);
void Config_FactoryDefaults(void);
uint8_t Config_InitializeDefaults(uint8_t recovered_from_corruption);
uint8_t Runtime_Load(void);
uint8_t Runtime_Append(void);
uint8_t IrStore_Load(void);
uint8_t IrStore_SaveIfChanged(void);
uint8_t LoraParams_Load(void);
uint8_t LoraParams_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw);
uint8_t LoraParams_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw);
uint8_t Connectivity_Load(void);
uint8_t Connectivity_Save(const connectivity_config_t *config);
uint8_t Connectivity_Validate(const connectivity_config_t *config);
const connectivity_config_t *Connectivity_Get(void);
uint32_t Connectivity_GetGeneration(void);
uint8_t Connectivity_Encode(uint8_t *payload, uint16_t capacity, uint16_t *length);
uint8_t Connectivity_Decode(const uint8_t *payload, uint16_t length,
                              connectivity_config_t *config);
uint8_t Connectivity_LoraEnabled(void);
uint8_t Connectivity_CellularEnabled(void);
uint8_t DeviceProfile_Load(void);
uint8_t DeviceProfile_SaveName(const char *name, uint8_t length);
const char *DeviceProfile_GetName(void);
void DeviceProfile_FactoryDefaults(void);
uint8_t Storage_FactoryReset(void);
uint8_t Storage_GetStartupFlags(void);

#endif
