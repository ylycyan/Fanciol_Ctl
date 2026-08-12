#include "board.h"
#include "config_store_v2.h"
#include "protocol_v2.h"
#include "CH58x_common.h"
#include <stddef.h>
#include <string.h>

#define CFG_MAGIC       0x32434153UL
#define RUNTIME_MAGIC   0x34544E52UL
#define IR_MAGIC        0x32524953UL
#define LORA_PARAM_MAGIC 0x3250524CUL
#define CONNECTIVITY_MAGIC 0x324D4F43UL
#define RUNTIME_SLOTS   64u

typedef struct __attribute__((packed)) {
    uint16_t node_id;
    uint8_t channel;
    uint8_t link_role;
    uint16_t parent_relay_id;
    uint8_t work_mode;
    uint8_t ir_action_type;
    uint16_t ir_type;
    uint8_t ir_index;
    uint8_t reserved;
    DEV_RULE_T rules[MAX_RULES];
} persisted_config_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema_version;
    uint32_t generation;
    uint16_t payload_length;
    uint32_t crc32;
    persisted_config_v2_t payload;
} config_record_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    DEV_METER_T meter;
    uint32_t last_power_change;
    uint8_t on_off;
    uint8_t control_mode;
    uint8_t temperature;
    uint8_t wind;
    uint32_t crc32;
} runtime_record_v2_t;

typedef char runtime_backup_record_must_fit[
    (sizeof(runtime_record_v2_t) <= EEPROM_PAGE_SIZE) ? 1 : -1
];

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    uint8_t learn_num;
    uint8_t reserved[3];
} ir_record_header_v2_t;

#define IR_CODE_BYTES   ((uint16_t)sizeof(Dev.learnCode))
#define IR_CRC_OFFSET   ((uint32_t)sizeof(ir_record_header_v2_t) + IR_CODE_BYTES)
#define IR_RECORD_BYTES (IR_CRC_OFFSET + sizeof(uint32_t))
#define IR_CRC_CHUNK    64U

typedef char ir_record_must_fit_flash_block[
    (IR_RECORD_BYTES <= EEPROM_BLOCK_SIZE) ? 1 : -1
];

typedef struct {
    uint32_t generation;
    uint32_t crc32;
    uint8_t learn_num;
    uint8_t valid;
    uint8_t erased;
} ir_slot_info_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t register_sf;
    uint8_t register_bw;
    uint8_t listen_sf;
    uint8_t listen_bw;
    uint32_t crc32;
} lora_param_record_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t schema_version;
    uint8_t reserved;
    uint32_t generation;
    uint16_t payload_length;
    uint32_t crc32;
    connectivity_config_v2_t payload;
} connectivity_record_v2_t;

typedef char connectivity_record_must_fit_page[
    (sizeof(connectivity_record_v2_t) <= EEPROM_PAGE_SIZE) ? 1 : -1
];

static uint32_t config_revision;
static uint32_t config_slot;
static uint32_t runtime_generation;
static uint16_t runtime_next_slot;
static uint8_t runtime_backup_active;
static uint8_t runtime_write_locked;
static uint32_t config_fingerprint;
static config_v2_load_state_t config_load_state;
static uint32_t ir_fingerprint;
static uint32_t ir_generation;
static uint32_t ir_slot;
static uint8_t ir_write_locked;
static uint8_t lora_params_write_locked;
static uint8_t storage_startup_flags;
static connectivity_config_v2_t connectivity_config;
static uint32_t connectivity_generation;
static uint32_t connectivity_slot;

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t bit;
    for(i = 0; i < len; ++i) {
        crc ^= data[i];
        for(bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320UL : 0u);
    }
    return crc;
}

uint32_t ConfigV2_Crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL, data, len);
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t config_record_crc(const config_record_v2_t *record)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL,
                                (const uint8_t *)record,
                                (uint16_t)offsetof(config_record_v2_t, crc32));
    crc = crc32_update(crc,
                       (const uint8_t *)&record->payload,
                       sizeof(record->payload));
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t config_record_erased(const config_record_v2_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for(i = 0; i < sizeof(*record); ++i) {
        if(bytes[i] != 0xFFU) return 0U;
    }
    return 1U;
}

static void capture_config(persisted_config_v2_t *p)
{
    uint8_t i;
    memset(p, 0, sizeof(*p));
    p->node_id = Dev.nodeId; p->channel = (uint8_t)Dev.channel; p->link_role = Dev.linkRole;
    p->parent_relay_id = Dev.parentRelayId; p->work_mode = Dev.mode; p->ir_action_type = (uint8_t)Dev.irActType;
    p->ir_type = Dev.irType; p->ir_index = Dev.irIdx; memcpy(p->rules, Dev.rules, sizeof(p->rules));
    /* executed 是运行态，不能因规则触发而改写配置版本。 */
    for(i = 0; i < MAX_RULES; ++i) p->rules[i].ctrl.executed = 0;
}

static void apply_config(const persisted_config_v2_t *p)
{
    uint8_t i;
    Dev.nodeId = p->node_id; Dev.channel = p->channel; Dev.linkRole = p->link_role;
    Dev.parentRelayId = p->parent_relay_id; Dev.mode = p->work_mode; Dev.irActType = (ActType_t)p->ir_action_type;
    Dev.irType = p->ir_type; Dev.irIdx = p->ir_index; memcpy(Dev.rules, p->rules, sizeof(Dev.rules));
    for(i = 0; i < MAX_RULES; ++i) Dev.rules[i].ctrl.executed = 0;
}

static uint8_t valid_config_record(const config_record_v2_t *r)
{
    if(r->magic != CFG_MAGIC || r->schema_version != V2_CONFIG_SCHEMA_VERSION || r->payload_length != sizeof(r->payload)) return 0;
    return r->crc32 == config_record_crc(r);
}

uint8_t ConfigV2_ValidateCurrent(void)
{
    if(!Dev.nodeId || Dev.channel > 32 || Dev.linkRole > LINK_CHILD || Dev.mode > 1) return V2_STATUS_INVALID_ARG;
    if(Dev.linkRole == LINK_CHILD && (!Dev.parentRelayId || Dev.parentRelayId == Dev.nodeId)) return V2_STATUS_INVALID_ARG;
    if(Dev.irActType > ACT_TYPE_LEARN || (Dev.irIdx != 0xFFu && Dev.irIdx >= IR_BRAND_COUNT)) return V2_STATUS_INVALID_ARG;
    return V2_STATUS_OK;
}

void ConfigV2_FactoryDefaults(void)
{
    memset(&Dev, 0, sizeof(Dev));
    Dev.magicCode = MAGIC_CODE; Dev.nodeId = Default_DevId; Dev.channel = Default_Channel;
    Dev.linkRole = LINK_DIRECT; Dev.parentRelayId = 0; Dev.mode = 1; Dev.irActType = ACT_TYPE_IR;
    Dev.loraRegisterSf = LORA_SF_LISTEN; Dev.loraRegisterBw = LORA_BW_LISTEN;
    Dev.loraListenSf = LORA_SF_SCAN; Dev.loraListenBw = LORA_BW_SCAN;
    Dev.irIdx = 0xFF; Dev.errorCode.bit.irMatch = 1;
    /*
     * t_dev 的运行态枚举并非都以 0 表示安全默认值（PowerOn 恰好为 0）。
     * 恢复出厂和损坏回退后必须留下可直接继续运行的完整 RAM 状态，而不是
     * 依赖下一次重启补齐，否则会误报“已开机/0 ℃”并可能停在 LoRa 未初始化态。
     */
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 25U;
    Dev.wind = Wind_Auto;
    Dev.loraStatus = Status_Logining;
    config_revision = 0; config_slot = V2_CONFIG_SLOT_B;
}

uint8_t ConfigV2_Load(void)
{
    config_record_v2_t record;
    uint8_t read_a;
    uint8_t read_b;
    uint8_t valid_a;
    uint8_t valid_b;
    uint8_t erased_a;
    uint8_t erased_b;
    uint8_t repair_status;

    config_load_state = CONFIG_V2_LOAD_HEALTHY;
    storage_startup_flags = 0U;
    memset(&record, 0xFF, sizeof(record));
    read_a = EEPROM_READ(V2_CONFIG_SLOT_A, &record, sizeof(record)) == 0U;
    valid_a = read_a && valid_config_record(&record);
    erased_a = read_a && config_record_erased(&record);
    if(valid_a) {
        apply_config(&record.payload);
        config_revision = record.generation;
        config_slot = V2_CONFIG_SLOT_A;
        config_fingerprint = ConfigV2_Crc32((const uint8_t *)&record.payload,
                                            sizeof(record.payload));
    }

    /*
     * 逐槽读取，只在系统栈保留一份记录。CH583 RAM 很小，不能为了启动时
     * 的双槽选择把两份完整规则载荷同时压入主循环栈。
     */
    memset(&record, 0xFF, sizeof(record));
    read_b = EEPROM_READ(V2_CONFIG_SLOT_B, &record, sizeof(record)) == 0U;
    valid_b = read_b && valid_config_record(&record);
    erased_b = read_b && config_record_erased(&record);
    if(valid_b && (!valid_a ||
       (int32_t)(record.generation - config_revision) > 0)) {
        apply_config(&record.payload);
        config_revision = record.generation;
        config_slot = V2_CONFIG_SLOT_B;
        config_fingerprint = ConfigV2_Crc32((const uint8_t *)&record.payload,
                                            sizeof(record.payload));
    }

    if(!valid_a && !valid_b) {
        if(!read_a || !read_b) {
            config_load_state = CONFIG_V2_LOAD_IO_ERROR;
            storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        }
        else if(erased_a && erased_b)
            config_load_state = CONFIG_V2_LOAD_EMPTY;
        else
            config_load_state = CONFIG_V2_LOAD_CORRUPT;
        ConfigV2_FactoryDefaults();
        return (!read_a || !read_b) ? V2_STATUS_IO_ERROR : V2_STATUS_VERIFY_FAILED;
    }

    /*
     * EEPROM 读取失败时绝不写 Flash，避免把瞬时总线故障误判成坏槽并覆盖数据。
     * 两槽都可读且仅一槽有效时，用已验证载荷重建备用槽。
     */
    if(!read_a || !read_b) {
        config_load_state = CONFIG_V2_LOAD_IO_ERROR;
        storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        return V2_STATUS_OK;
    }
    if(valid_a && valid_b) {
        config_load_state = CONFIG_V2_LOAD_HEALTHY;
        return V2_STATUS_OK;
    }

    repair_status = ConfigV2_Commit(config_revision);
    if(repair_status == V2_STATUS_OK) {
        config_load_state = CONFIG_V2_LOAD_REPAIRED;
        storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
        PRINT("V2 config redundancy repaired, revision=%lu\r\n",
              (unsigned long)config_revision);
    } else {
        config_load_state = CONFIG_V2_LOAD_DEGRADED;
        storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        PRINT("V2 config redundancy repair failed: %u\r\n", repair_status);
    }
    return V2_STATUS_OK;
}

uint8_t ConfigV2_Commit(uint32_t expected_revision)
{
    config_record_v2_t r;
    uint32_t target;
    uint32_t committed_generation;
    uint32_t committed_crc;
    if(config_load_state == CONFIG_V2_LOAD_IO_ERROR) return V2_STATUS_IO_ERROR;
    if(expected_revision != config_revision) return V2_STATUS_CONFLICT;
    if(ConfigV2_ValidateCurrent() != V2_STATUS_OK) return V2_STATUS_INVALID_ARG;
    memset(&r, 0, sizeof(r)); capture_config(&r.payload);
    r.magic = CFG_MAGIC; r.schema_version = V2_CONFIG_SCHEMA_VERSION; r.generation = config_revision + 1u;
    r.payload_length = sizeof(r.payload);
    r.crc32 = config_record_crc(&r);
    committed_generation = r.generation;
    committed_crc = r.crc32;
    target = (config_slot == V2_CONFIG_SLOT_A) ? V2_CONFIG_SLOT_B : V2_CONFIG_SLOT_A;
    if(EEPROM_ERASE(target, EEPROM_BLOCK_SIZE) || EEPROM_WRITE(target, &r, sizeof(r))) return V2_STATUS_IO_ERROR;
    if(EEPROM_READ(target, &r, sizeof(r)) ||
       !valid_config_record(&r) ||
       r.generation != committed_generation ||
       r.crc32 != committed_crc) {
        return V2_STATUS_VERIFY_FAILED;
    }
    config_revision = committed_generation;
    config_slot = target;
    config_fingerprint = ConfigV2_Crc32((const uint8_t *)&r.payload,
                                        sizeof(r.payload));
    config_load_state = CONFIG_V2_LOAD_HEALTHY;
    return V2_STATUS_OK;
}

uint8_t ConfigV2_CommitIfChanged(void)
{
    persisted_config_v2_t p;
    uint8_t status;
    capture_config(&p);
    if(ConfigV2_Crc32((const uint8_t *)&p, sizeof(p)) == config_fingerprint) {
        if(config_load_state != CONFIG_V2_LOAD_DEGRADED) return V2_STATUS_OK;
        status = ConfigV2_Commit(config_revision);
        if(status == V2_STATUS_OK && config_revision == 1U)
            status = ConfigV2_Commit(config_revision);
        return status;
    }
    return ConfigV2_Commit(config_revision);
}

uint32_t ConfigV2_GetRevision(void) { return config_revision; }

config_v2_load_state_t ConfigV2_GetLoadState(void)
{
    return config_load_state;
}

uint8_t ConfigV2_InitializeDefaults(uint8_t recovered_from_corruption)
{
    uint8_t status;

    ConfigV2_FactoryDefaults();
    config_load_state = CONFIG_V2_LOAD_EMPTY;
    status = ConfigV2_Commit(0U);
    if(status == V2_STATUS_OK) status = ConfigV2_Commit(ConfigV2_GetRevision());
    if(status != V2_STATUS_OK) {
        config_load_state = CONFIG_V2_LOAD_DEGRADED;
        storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        return status;
    }
    config_load_state = recovered_from_corruption
        ? CONFIG_V2_LOAD_DEFAULTS_RECOVERED
        : CONFIG_V2_LOAD_INITIALIZED;
    if(recovered_from_corruption)
        storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
    return V2_STATUS_OK;
}

static uint8_t valid_runtime_record(const runtime_record_v2_t *r)
{
    return r->magic == RUNTIME_MAGIC &&
           r->on_off <= (uint8_t)PowerOff &&
           r->control_mode <= (uint8_t)Mode_Heat &&
           r->temperature >= 16u && r->temperature <= 31u &&
           r->wind <= (uint8_t)Wind_High &&
           r->crc32 == ConfigV2_Crc32((const uint8_t *)r,
                                      (uint16_t)offsetof(runtime_record_v2_t,
                                                         crc32));
}

uint8_t RuntimeV2_Load(void)
{
    runtime_record_v2_t r;
    runtime_record_v2_t best;
    uint8_t found = 0;
    uint8_t read_error = 0U;
    uint8_t invalid_record = 0U;
    uint16_t i;
    runtime_next_slot = 0; runtime_generation = 0; runtime_backup_active = 0;
    runtime_write_locked = 0U;
    for(i = 0; i < RUNTIME_SLOTS; ++i) {
        memset(&r, 0xFF, sizeof(r));
        if(EEPROM_READ(V2_RUNTIME_PAGE + (uint32_t)i * sizeof(r),
                       &r, sizeof(r)) != 0U) {
            read_error = 1U;
            runtime_next_slot = (uint16_t)(i + 1U);
            continue;
        }
        if(r.magic == 0xFFFFFFFFUL) { runtime_next_slot = i; break; }
        if(valid_runtime_record(&r)) {
            if(!found || (int32_t)(r.generation - best.generation) > 0) { best = r; found = 1; }
        } else invalid_record = 1U;
        runtime_next_slot = (uint16_t)(i + 1u);
    }

    /*
     * 主日志轮转前先把最新记录写到健康页尾的独立 256 B 检查点。
     * 即使在“备份完成、主日志擦除、首条记录写入”的任一步骤掉电，
     * 启动时仍可从 generation 较新的有效记录恢复。
     */
    memset(&r, 0xFF, sizeof(r));
    if(EEPROM_READ(V2_RUNTIME_BACKUP_PAGE, &r, sizeof(r)) != 0U) {
        read_error = 1U;
    } else if(r.magic != 0xFFFFFFFFUL) {
        if(valid_runtime_record(&r)) {
            runtime_backup_active = 1;
            if(!found || (int32_t)(r.generation - best.generation) > 0) {
                best = r;
                found = 1;
            }
        } else invalid_record = 1U;
    }
    if(found) {
        Dev.meter = best.meter;
        Dev.runTime = best.meter.run_minutes > 0xFFFFUL
            ? 0xFFFFu
            : (uint16_t)best.meter.run_minutes;
        Dev.lastPowerChange = best.last_power_change;
        Dev.onOff = (OnOff_t)best.on_off;
        Dev.ctlMode = (Mode_t)best.control_mode;
        Dev.temSet = best.temperature;
        Dev.wind = (Wind_t)best.wind;
        Dev.lastOnTime = (Dev.onOff == PowerOn)
            ? best.last_power_change
            : 0u;
        runtime_generation = best.generation;
    }
    if(invalid_record) storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
    if(read_error) {
        /*
         * 任一页读取失败后无法证明下一写槽安全，本次启动禁止继续追加。
         * 瞬时故障也宁可少记一段计量，不覆盖尚未读出的更高代记录。
         */
        runtime_write_locked = 1U;
        storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        return V2_STATUS_IO_ERROR;
    }
    return found ? V2_STATUS_OK : V2_STATUS_VERIFY_FAILED;
}

static uint8_t runtime_write_backup(runtime_record_v2_t *record)
{
    runtime_record_v2_t verify;
    if(EEPROM_ERASE(V2_RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) ||
       EEPROM_WRITE(V2_RUNTIME_BACKUP_PAGE, record, sizeof(*record)) ||
       EEPROM_READ(V2_RUNTIME_BACKUP_PAGE, &verify, sizeof(verify)) ||
       memcmp(record, &verify, sizeof(verify)) != 0 ||
       !valid_runtime_record(&verify)) {
        return V2_STATUS_VERIFY_FAILED;
    }
    runtime_backup_active = 1;
    return V2_STATUS_OK;
}

uint8_t RuntimeV2_Append(void)
{
    runtime_record_v2_t r;
    runtime_record_v2_t verify;
    uint32_t address;
    uint8_t rolling_over;

    if(runtime_write_locked) return V2_STATUS_IO_ERROR;

    memset(&r, 0, sizeof(r));
    r.magic = RUNTIME_MAGIC;
    r.generation = runtime_generation + 1u;
    r.meter = Dev.meter;
    r.last_power_change = Dev.lastPowerChange;
    r.on_off = (uint8_t)Dev.onOff;
    r.control_mode = (uint8_t)Dev.ctlMode;
    r.temperature = (uint8_t)Dev.temSet;
    r.wind = (uint8_t)Dev.wind;
    r.crc32 = ConfigV2_Crc32(
        (const uint8_t *)&r,
        (uint16_t)offsetof(runtime_record_v2_t, crc32));

    rolling_over = runtime_next_slot >= RUNTIME_SLOTS;
    if(rolling_over) {
        if(runtime_write_backup(&r) != V2_STATUS_OK) return V2_STATUS_VERIFY_FAILED;
    }
    if(runtime_next_slot >= RUNTIME_SLOTS) {
        if(EEPROM_ERASE(V2_RUNTIME_PAGE, EEPROM_BLOCK_SIZE)) return V2_STATUS_IO_ERROR;
        runtime_next_slot = 0;
    }
    address = V2_RUNTIME_PAGE + (uint32_t)runtime_next_slot * sizeof(r);
    if(EEPROM_WRITE(address, &r, sizeof(r))) { runtime_next_slot++; return V2_STATUS_IO_ERROR; }
    runtime_next_slot++;
    if(EEPROM_READ(address, &verify, sizeof(verify)) || memcmp(&r, &verify, sizeof(r)) != 0 ||
       verify.magic != RUNTIME_MAGIC ||
       verify.crc32 != ConfigV2_Crc32(
           (const uint8_t *)&verify,
           (uint16_t)offsetof(runtime_record_v2_t, crc32))) {
        return V2_STATUS_VERIFY_FAILED;
    }
    runtime_generation = r.generation;
    if(runtime_backup_active &&
       EEPROM_ERASE(V2_RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) == 0) {
        runtime_backup_active = 0;
    }
    return V2_STATUS_OK;
}

static uint32_t ir_fingerprint_current(uint8_t learn_num)
{
    uint8_t metadata[4] = {learn_num, 0U, 0U, 0U};
    uint32_t crc = crc32_update(0xFFFFFFFFUL, metadata, sizeof(metadata));
    crc = crc32_update(crc, (const uint8_t *)Dev.learnCode, IR_CODE_BYTES);
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t ir_validate_slot(uint32_t slot,
                                ir_slot_info_v2_t *info,
                                uint8_t *io_error)
{
    ir_record_header_v2_t header;
    uint8_t chunk[IR_CRC_CHUNK];
    uint32_t stored_crc;
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0U;

    memset(info, 0, sizeof(*info));
    if(EEPROM_READ(slot, &header, sizeof(header))) {
        if(io_error) *io_error = 1U;
        return 0U;
    }
    if(header.magic == 0xFFFFFFFFUL) {
        info->erased = 1U;
        return 0U;
    }
    if(header.magic != IR_MAGIC ||
       header.learn_num > MAX_IR_LEARNNUM) {
        return 0U;
    }

    crc = crc32_update(crc, (const uint8_t *)&header, sizeof(header));
    while(offset < IR_CODE_BYTES) {
        uint16_t remaining = (uint16_t)(IR_CODE_BYTES - offset);
        uint16_t length = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        if(EEPROM_READ(slot + sizeof(header) + offset, chunk, length)) {
            if(io_error) *io_error = 1U;
            return 0U;
        }
        crc = crc32_update(crc, chunk, length);
        offset += length;
    }
    crc ^= 0xFFFFFFFFUL;
    if(EEPROM_READ(slot + IR_CRC_OFFSET, &stored_crc, sizeof(stored_crc))) {
        if(io_error) *io_error = 1U;
        return 0U;
    }
    if(crc != stored_crc) {
        return 0U;
    }

    info->generation = header.generation;
    info->crc32 = stored_crc;
    info->learn_num = header.learn_num;
    info->valid = 1U;
    return 1U;
}

static uint8_t ir_load_slot(uint32_t slot,
                            const ir_slot_info_v2_t *expected,
                            uint8_t *io_error)
{
    ir_record_header_v2_t header;
    uint32_t stored_crc;
    uint32_t crc;

    if(!expected->valid) return 0U;
    if(EEPROM_READ(slot, &header, sizeof(header)) ||
       EEPROM_READ(slot + sizeof(header), Dev.learnCode, IR_CODE_BYTES) ||
       EEPROM_READ(slot + IR_CRC_OFFSET, &stored_crc, sizeof(stored_crc))) {
        if(io_error) *io_error = 1U;
        return 0U;
    }

    crc = crc32_update(0xFFFFFFFFUL, (const uint8_t *)&header, sizeof(header));
    crc = crc32_update(crc, (const uint8_t *)Dev.learnCode, IR_CODE_BYTES) ^ 0xFFFFFFFFUL;
    if(header.magic != IR_MAGIC ||
       header.learn_num > MAX_IR_LEARNNUM ||
       header.generation != expected->generation ||
       stored_crc != expected->crc32 ||
       crc != stored_crc) {
        return 0U;
    }

    Dev.learnNum = header.learn_num;
    ir_fingerprint = ir_fingerprint_current(header.learn_num);
    ir_generation = header.generation;
    ir_slot = slot;
    return 1U;
}

uint8_t IrStoreV2_Load(void)
{
    ir_slot_info_v2_t slot_a;
    ir_slot_info_v2_t slot_b;
    uint8_t io_error = 0U;
    uint8_t repair_status;
    uint32_t selected_fingerprint;
    uint8_t valid_a = ir_validate_slot(V2_IR_PAGE, &slot_a, &io_error);
    uint8_t valid_b = ir_validate_slot(V2_IR_SLOT_B, &slot_b, &io_error);
    uint8_t loaded = 0U;

    ir_write_locked = 0U;

    if(valid_b && (!valid_a || (int32_t)(slot_b.generation - slot_a.generation) > 0)) {
        loaded = ir_load_slot(V2_IR_SLOT_B, &slot_b, &io_error);
        if(!loaded && valid_a) loaded = ir_load_slot(V2_IR_PAGE, &slot_a, &io_error);
    } else if(valid_a) {
        loaded = ir_load_slot(V2_IR_PAGE, &slot_a, &io_error);
        if(!loaded && valid_b) loaded = ir_load_slot(V2_IR_SLOT_B, &slot_b, &io_error);
    }

    if(loaded) {
        if(io_error) {
            ir_write_locked = 1U;
            storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
            return V2_STATUS_IO_ERROR;
        }
        if(valid_a != valid_b) {
            selected_fingerprint = ir_fingerprint;
            ir_fingerprint ^= 1U; /* 强制把已验证数据写入失效的备用槽。 */
            repair_status = IrStoreV2_SaveIfChanged();
            if(repair_status == V2_STATUS_OK) {
                storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
            } else {
                ir_fingerprint = selected_fingerprint;
                storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
            }
        }
        return V2_STATUS_OK;
    }

    Dev.learnNum = 0;
    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    ir_fingerprint = 0;
    ir_generation = 0;
    ir_slot = V2_IR_SLOT_B;
    if(io_error) {
        ir_write_locked = 1U;
        storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
        return V2_STATUS_IO_ERROR;
    }
    if(!slot_a.erased || !slot_b.erased)
        storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
    return V2_STATUS_VERIFY_FAILED;
}

uint8_t IrStoreV2_SaveIfChanged(void)
{
    ir_record_header_v2_t header;
    ir_slot_info_v2_t verify;
    uint32_t target;
    uint32_t fingerprint;
    uint32_t crc;

    if(ir_write_locked) return V2_STATUS_IO_ERROR;
    if(Dev.learnNum > MAX_IR_LEARNNUM) return V2_STATUS_INVALID_ARG;
    fingerprint = ir_fingerprint_current(Dev.learnNum);
    if(fingerprint == ir_fingerprint) return V2_STATUS_OK;

    memset(&header, 0, sizeof(header));
    header.magic = IR_MAGIC;
    header.generation = ir_generation + 1U;
    header.learn_num = Dev.learnNum;
    crc = crc32_update(0xFFFFFFFFUL, (const uint8_t *)&header, sizeof(header));
    crc = crc32_update(crc, (const uint8_t *)Dev.learnCode, IR_CODE_BYTES) ^ 0xFFFFFFFFUL;

    target = (ir_slot == V2_IR_PAGE) ? V2_IR_SLOT_B : V2_IR_PAGE;
    /*
     * CRC 最后写入：任一阶段掉电都会使新槽无效，旧槽仍可回退。
     * 学习码直接从 Dev 分段写入，不在 512 B 系统栈上复制约 2.6 KB 记录。
     */
    if(EEPROM_ERASE(target, EEPROM_BLOCK_SIZE) ||
       EEPROM_WRITE(target, &header, sizeof(header)) ||
       EEPROM_WRITE(target + sizeof(header), Dev.learnCode, IR_CODE_BYTES) ||
       EEPROM_WRITE(target + IR_CRC_OFFSET, &crc, sizeof(crc))) {
        return V2_STATUS_IO_ERROR;
    }
    if(!ir_validate_slot(target, &verify, 0) ||
       verify.generation != header.generation ||
       verify.crc32 != crc ||
       verify.learn_num != header.learn_num) {
        return V2_STATUS_VERIFY_FAILED;
    }

    ir_fingerprint = fingerprint;
    ir_generation = header.generation;
    ir_slot = target;
    return V2_STATUS_OK;
}

static uint8_t valid_lora_bw(uint8_t bw)
{
    switch(bw) {
    case 0u: case 1u: case 2u: case 3u: case 4u:
    case 5u: case 6u: case 8u: case 9u: case 10u:
        return 1u;
    default:
        return 0u;
    }
}

static void connectivity_defaults(connectivity_config_v2_t *config)
{
    memset(config, 0, sizeof(*config));
    config->transport_mask = CONNECTIVITY_V2_LORA;
    config->lora_register_sf = LORA_SF_LISTEN;
    config->lora_register_bw = LORA_BW_LISTEN;
    config->lora_listen_sf = LORA_SF_SCAN;
    config->lora_listen_bw = LORA_BW_SCAN;
    config->mqtt_port = 1883U;
    config->mqtt_keepalive_sec = 60U;
    config->report_interval_sec = 60U;
    config->mqtt_qos = 0U;
}

static uint8_t connectivity_string_valid(const char *value, uint16_t capacity)
{
    uint16_t i;
    if(!value || !capacity || value[capacity - 1U] != '\0') return 0U;
    for(i = 0U; value[i] != '\0'; ++i) {
        uint8_t ch = (uint8_t)value[i];
        if(ch < 0x20U || ch > 0x7EU || ch == '"' || ch == '\\') return 0U;
    }
    return 1U;
}

static uint8_t connectivity_record_erased(const connectivity_record_v2_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for(i = 0U; i < sizeof(*record); ++i) if(bytes[i] != 0xFFU) return 0U;
    return 1U;
}

static uint32_t connectivity_crc(const connectivity_record_v2_t *record)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL, (const uint8_t *)record,
                                (uint16_t)offsetof(connectivity_record_v2_t, crc32));
    crc = crc32_update(crc, (const uint8_t *)&record->payload,
                       sizeof(record->payload));
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t connectivity_record_valid(const connectivity_record_v2_t *record)
{
    return record->magic == CONNECTIVITY_MAGIC &&
           record->schema_version == CONNECTIVITY_V2_SCHEMA &&
           record->payload_length == sizeof(record->payload) &&
           record->crc32 == connectivity_crc(record) &&
           ConnectivityV2_Validate(&record->payload) == V2_STATUS_OK;
}

uint8_t LoraParamsV2_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw)
{
    if(register_sf < LORA_SF_MIN || register_sf > LORA_SF_MAX ||
       listen_sf < LORA_SF_MIN || listen_sf > LORA_SF_MAX ||
       !valid_lora_bw(register_bw) || !valid_lora_bw(listen_bw)) return V2_STATUS_INVALID_ARG;
    return V2_STATUS_OK;
}

uint8_t ConnectivityV2_Validate(const connectivity_config_v2_t *config)
{
    if(!config || config->transport_mask == 0U ||
       (config->transport_mask & (uint8_t)~(CONNECTIVITY_V2_LORA | CONNECTIVITY_V2_CELLULAR)) != 0U)
        return V2_STATUS_INVALID_ARG;
    if(LoraParamsV2_Validate(config->lora_register_sf,
                             config->lora_register_bw,
                             config->lora_listen_sf,
                             config->lora_listen_bw) != V2_STATUS_OK)
        return V2_STATUS_INVALID_ARG;
    if(config->mqtt_port == 0U || config->mqtt_keepalive_sec < 60U ||
       config->mqtt_keepalive_sec > 3600U || config->report_interval_sec < 10U ||
       config->report_interval_sec > 3600U || config->mqtt_qos > 1U)
        return V2_STATUS_INVALID_ARG;
    if(!connectivity_string_valid(config->mqtt_host, sizeof(config->mqtt_host)) ||
       !connectivity_string_valid(config->mqtt_client_id, sizeof(config->mqtt_client_id)) ||
       !connectivity_string_valid(config->mqtt_username, sizeof(config->mqtt_username)) ||
       !connectivity_string_valid(config->mqtt_password, sizeof(config->mqtt_password)) ||
       !connectivity_string_valid(config->publish_topic, sizeof(config->publish_topic)) ||
       !connectivity_string_valid(config->subscribe_topic, sizeof(config->subscribe_topic)) ||
       !connectivity_string_valid(config->apn, sizeof(config->apn)))
        return V2_STATUS_INVALID_ARG;
    if((config->transport_mask & CONNECTIVITY_V2_CELLULAR) != 0U &&
       (!config->mqtt_host[0] || !config->publish_topic[0] || !config->subscribe_topic[0]))
        return V2_STATUS_INVALID_ARG;
    return V2_STATUS_OK;
}

static void connectivity_apply(const connectivity_config_v2_t *config)
{
    memcpy(&connectivity_config, config, sizeof(connectivity_config));
    Dev.loraRegisterSf = config->lora_register_sf;
    Dev.loraRegisterBw = config->lora_register_bw;
    Dev.loraListenSf = config->lora_listen_sf;
    Dev.loraListenBw = config->lora_listen_bw;
}

uint8_t ConnectivityV2_Load(void)
{
    connectivity_record_v2_t record;
    lora_param_record_v2_t legacy;
    uint8_t first_valid;
    uint8_t second_valid;
    uint8_t first_erased;
    uint8_t second_erased;
    uint8_t selected_valid = 0U;

    lora_params_write_locked = 0U;
    memset(&record, 0xFF, sizeof(record));
    if(EEPROM_READ(V2_CONNECTIVITY_SLOT_A, &record, sizeof(record)) != 0U)
        goto connectivity_read_error;
    memcpy(&legacy, &record, sizeof(legacy));
    first_valid = connectivity_record_valid(&record);
    first_erased = connectivity_record_erased(&record);
    if(first_valid) {
        connectivity_generation = record.generation;
        connectivity_slot = V2_CONNECTIVITY_SLOT_A;
        connectivity_apply(&record.payload);
        selected_valid = 1U;
    }

    /* Reuse one page-sized scratch record to keep the boot task stack bounded. */
    memset(&record, 0xFF, sizeof(record));
    if(EEPROM_READ(V2_CONNECTIVITY_SLOT_B, &record, sizeof(record)) != 0U)
        goto connectivity_read_error;
    second_valid = connectivity_record_valid(&record);
    second_erased = connectivity_record_erased(&record);
    if(second_valid && (!selected_valid ||
       (int32_t)(record.generation - connectivity_generation) > 0)) {
        connectivity_generation = record.generation;
        connectivity_slot = V2_CONNECTIVITY_SLOT_B;
        connectivity_apply(&record.payload);
        selected_valid = 1U;
    }
    if(selected_valid) {
        if(first_valid != second_valid) {
            uint32_t target = connectivity_slot == V2_CONNECTIVITY_SLOT_A ?
                              V2_CONNECTIVITY_SLOT_B : V2_CONNECTIVITY_SLOT_A;
            uint32_t expected_crc;
            memset(&record, 0, sizeof(record));
            record.magic = CONNECTIVITY_MAGIC;
            record.schema_version = CONNECTIVITY_V2_SCHEMA;
            record.generation = connectivity_generation + 1U;
            record.payload_length = sizeof(record.payload);
            memcpy(&record.payload, &connectivity_config, sizeof(record.payload));
            record.crc32 = connectivity_crc(&record);
            expected_crc = record.crc32;
            if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
               EEPROM_WRITE(target, &record, sizeof(record)) != 0U ||
               EEPROM_READ(target, &record, sizeof(record)) != 0U ||
               !connectivity_record_valid(&record) || record.crc32 != expected_crc) {
                lora_params_write_locked = 1U;
                storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
            } else {
                connectivity_generation = record.generation;
                connectivity_slot = target;
                /* A blank peer slot is normal after the very first commit.
                 * Rebuild its redundancy silently; only a non-erased invalid
                 * record represents corruption worth exposing as recovered. */
                if(!((first_valid && second_erased) || (second_valid && first_erased)))
                    storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
            }
        }
        return V2_STATUS_OK;
    }

    /* 兼容迁移旧版 0x2000 单槽 LoRa 参数记录。 */
    connectivity_defaults(&connectivity_config);
    connectivity_generation = 0U;
    connectivity_slot = V2_CONNECTIVITY_SLOT_B;
    if(legacy.magic == LORA_PARAM_MAGIC &&
       legacy.crc32 == ConfigV2_Crc32((const uint8_t *)&legacy,
                                      (uint16_t)(sizeof(legacy) - 4U)) &&
       LoraParamsV2_Validate(legacy.register_sf, legacy.register_bw,
                             legacy.listen_sf, legacy.listen_bw) == V2_STATUS_OK) {
        connectivity_config.lora_register_sf = legacy.register_sf;
        connectivity_config.lora_register_bw = legacy.register_bw;
        connectivity_config.lora_listen_sf = legacy.listen_sf;
        connectivity_config.lora_listen_bw = legacy.listen_bw;
        if(legacy.register_sf == 10U && legacy.register_bw == 0x04U &&
           legacy.listen_sf == 8U && legacy.listen_bw == 0x0AU) {
            connectivity_config.lora_register_sf = LORA_SF_LISTEN;
            connectivity_config.lora_register_bw = LORA_BW_LISTEN;
            connectivity_config.lora_listen_sf = LORA_SF_SCAN;
            connectivity_config.lora_listen_bw = LORA_BW_SCAN;
        }
        connectivity_apply(&connectivity_config);
        storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
        return ConnectivityV2_Save(&connectivity_config);
    }

    connectivity_apply(&connectivity_config);
    if(!first_erased || !second_erased)
        storage_startup_flags |= STORAGE_V2_STARTUP_RECOVERED;
    return V2_STATUS_VERIFY_FAILED;

connectivity_read_error:
    connectivity_defaults(&connectivity_config);
    connectivity_apply(&connectivity_config);
    connectivity_generation = 0U;
    connectivity_slot = V2_CONNECTIVITY_SLOT_B;
    lora_params_write_locked = 1U;
    storage_startup_flags |= STORAGE_V2_STARTUP_DEGRADED;
    return V2_STATUS_IO_ERROR;
}

uint8_t ConnectivityV2_Save(const connectivity_config_v2_t *config)
{
    connectivity_record_v2_t record;
    uint32_t target;
    uint32_t expected_crc;
    uint8_t status = ConnectivityV2_Validate(config);
    if(lora_params_write_locked) return V2_STATUS_IO_ERROR;
    if(status != V2_STATUS_OK) return status;
    if(memcmp(config, &connectivity_config, sizeof(*config)) == 0 && connectivity_generation != 0U)
        return V2_STATUS_OK;

    memset(&record, 0, sizeof(record));
    record.magic = CONNECTIVITY_MAGIC;
    record.schema_version = CONNECTIVITY_V2_SCHEMA;
    record.generation = connectivity_generation + 1U;
    record.payload_length = sizeof(record.payload);
    memcpy(&record.payload, config, sizeof(record.payload));
    record.crc32 = connectivity_crc(&record);
    expected_crc = record.crc32;
    target = connectivity_slot == V2_CONNECTIVITY_SLOT_A ?
             V2_CONNECTIVITY_SLOT_B : V2_CONNECTIVITY_SLOT_A;
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_WRITE(target, &record, sizeof(record)) != 0U ||
       EEPROM_READ(target, &record, sizeof(record)) != 0U)
        return V2_STATUS_IO_ERROR;
    if(!connectivity_record_valid(&record) || record.crc32 != expected_crc)
        return V2_STATUS_VERIFY_FAILED;

    connectivity_generation = record.generation;
    connectivity_slot = target;
    connectivity_apply(&record.payload);
    return V2_STATUS_OK;
}

const connectivity_config_v2_t *ConnectivityV2_Get(void)
{
    return &connectivity_config;
}

uint32_t ConnectivityV2_GetGeneration(void)
{
    return connectivity_generation;
}

uint8_t ConnectivityV2_LoraEnabled(void)
{
    return (connectivity_config.transport_mask & CONNECTIVITY_V2_LORA) != 0U;
}

uint8_t ConnectivityV2_CellularEnabled(void)
{
    return (connectivity_config.transport_mask & CONNECTIVITY_V2_CELLULAR) != 0U;
}

static uint8_t wire_put_string(uint8_t *payload, uint16_t capacity, uint16_t *offset,
                               const char *value, uint16_t value_capacity)
{
    uint16_t length = 0U;
    while(length < value_capacity && value[length]) length++;
    if(length >= value_capacity || length > 255U ||
       (uint16_t)(*offset + 1U + length) > capacity) return 0U;
    payload[(*offset)++] = (uint8_t)length;
    memcpy(payload + *offset, value, length);
    *offset = (uint16_t)(*offset + length);
    return 1U;
}

uint8_t ConnectivityV2_Encode(uint8_t *payload, uint16_t capacity, uint16_t *length)
{
    const connectivity_config_v2_t *config = &connectivity_config;
    uint16_t offset = 17U;
    if(!payload || !length || capacity < offset) return V2_STATUS_INVALID_ARG;
    payload[0] = CONNECTIVITY_V2_SCHEMA;
    payload[1] = config->transport_mask;
    payload[2] = config->lora_register_sf;
    payload[3] = config->lora_register_bw;
    payload[4] = config->lora_listen_sf;
    payload[5] = config->lora_listen_bw;
    payload[6] = config->mqtt_qos;
    payload[7] = (uint8_t)config->mqtt_port;
    payload[8] = (uint8_t)(config->mqtt_port >> 8);
    payload[9] = (uint8_t)config->mqtt_keepalive_sec;
    payload[10] = (uint8_t)(config->mqtt_keepalive_sec >> 8);
    payload[11] = (uint8_t)config->report_interval_sec;
    payload[12] = (uint8_t)(config->report_interval_sec >> 8);
    payload[13] = (uint8_t)connectivity_generation;
    payload[14] = (uint8_t)(connectivity_generation >> 8);
    payload[15] = (uint8_t)(connectivity_generation >> 16);
    payload[16] = (uint8_t)(connectivity_generation >> 24);
    if(!wire_put_string(payload, capacity, &offset, config->mqtt_host, sizeof(config->mqtt_host)) ||
       !wire_put_string(payload, capacity, &offset, config->mqtt_client_id, sizeof(config->mqtt_client_id)) ||
       !wire_put_string(payload, capacity, &offset, config->mqtt_username, sizeof(config->mqtt_username)) ||
       !wire_put_string(payload, capacity, &offset, config->mqtt_password, sizeof(config->mqtt_password)) ||
       !wire_put_string(payload, capacity, &offset, config->publish_topic, sizeof(config->publish_topic)) ||
       !wire_put_string(payload, capacity, &offset, config->subscribe_topic, sizeof(config->subscribe_topic)) ||
       !wire_put_string(payload, capacity, &offset, config->apn, sizeof(config->apn)))
        return V2_STATUS_INVALID_ARG;
    *length = offset;
    return V2_STATUS_OK;
}

static uint8_t wire_get_string(const uint8_t *payload, uint16_t length, uint16_t *offset,
                               char *value, uint16_t capacity)
{
    uint16_t string_length;
    if(*offset >= length) return 0U;
    string_length = payload[(*offset)++];
    if(string_length >= capacity || (uint16_t)(*offset + string_length) > length) return 0U;
    memcpy(value, payload + *offset, string_length);
    value[string_length] = '\0';
    *offset = (uint16_t)(*offset + string_length);
    return 1U;
}

uint8_t ConnectivityV2_Decode(const uint8_t *payload, uint16_t length,
                              connectivity_config_v2_t *config)
{
    uint16_t offset = 17U;
    uint32_t expected_generation;
    if(!payload || !config || length < offset || payload[0] != CONNECTIVITY_V2_SCHEMA)
        return V2_STATUS_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    config->transport_mask = payload[1];
    config->lora_register_sf = payload[2];
    config->lora_register_bw = payload[3];
    config->lora_listen_sf = payload[4];
    config->lora_listen_bw = payload[5];
    config->mqtt_qos = payload[6];
    config->mqtt_port = (uint16_t)payload[7] | ((uint16_t)payload[8] << 8);
    config->mqtt_keepalive_sec = (uint16_t)payload[9] | ((uint16_t)payload[10] << 8);
    config->report_interval_sec = (uint16_t)payload[11] | ((uint16_t)payload[12] << 8);
    expected_generation = (uint32_t)payload[13] | ((uint32_t)payload[14] << 8) |
                          ((uint32_t)payload[15] << 16) | ((uint32_t)payload[16] << 24);
    if(expected_generation != connectivity_generation) return V2_STATUS_CONFLICT;
    if(!wire_get_string(payload, length, &offset, config->mqtt_host, sizeof(config->mqtt_host)) ||
       !wire_get_string(payload, length, &offset, config->mqtt_client_id, sizeof(config->mqtt_client_id)) ||
       !wire_get_string(payload, length, &offset, config->mqtt_username, sizeof(config->mqtt_username)) ||
       !wire_get_string(payload, length, &offset, config->mqtt_password, sizeof(config->mqtt_password)) ||
       !wire_get_string(payload, length, &offset, config->publish_topic, sizeof(config->publish_topic)) ||
       !wire_get_string(payload, length, &offset, config->subscribe_topic, sizeof(config->subscribe_topic)) ||
       !wire_get_string(payload, length, &offset, config->apn, sizeof(config->apn)) ||
       offset != length) return V2_STATUS_INVALID_ARG;
    return ConnectivityV2_Validate(config);
}

uint8_t LoraParamsV2_Load(void)
{
    return ConnectivityV2_Load();
}

uint8_t LoraParamsV2_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw)
{
    connectivity_config_v2_t next;
    uint8_t status = LoraParamsV2_Validate(register_sf, register_bw, listen_sf, listen_bw);
    if(lora_params_write_locked) return V2_STATUS_IO_ERROR;
    if(status != V2_STATUS_OK) return status;
    memcpy(&next, &connectivity_config, sizeof(next));
    next.lora_register_sf = register_sf;
    next.lora_register_bw = register_bw;
    next.lora_listen_sf = listen_sf;
    next.lora_listen_bw = listen_bw;
    return ConnectivityV2_Save(&next);
}

uint8_t StorageV2_FactoryReset(void)
{
    /*
     * 先清运行分区，最后清配置双槽并提交默认值。任一步失败都返回错误，
     * 避免小程序显示“恢复成功”但旧计量/学习码仍在。健康与复位历史保留，
     * 便于现场追溯恢复出厂前后的异常原因。
     */
    if(EEPROM_ERASE(V2_RESERVED_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(V2_RUNTIME_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(V2_RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) ||
       EEPROM_ERASE(V2_IR_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(V2_IR_SLOT_B, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(V2_CONFIG_SLOT_A, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(V2_CONFIG_SLOT_B, EEPROM_BLOCK_SIZE)) {
        return V2_STATUS_IO_ERROR;
    }

    runtime_generation = 0;
    runtime_next_slot = 0;
    runtime_backup_active = 0;
    runtime_write_locked = 0U;
    ir_fingerprint = 0;
    ir_generation = 0;
    ir_slot = V2_IR_SLOT_B;
    ir_write_locked = 0U;
    lora_params_write_locked = 0U;
    connectivity_generation = 0U;
    connectivity_slot = V2_CONNECTIVITY_SLOT_B;
    connectivity_defaults(&connectivity_config);
    storage_startup_flags = 0U;
    if(ConnectivityV2_Save(&connectivity_config) != V2_STATUS_OK)
        return V2_STATUS_IO_ERROR;
    return ConfigV2_InitializeDefaults(0U);
}

uint8_t StorageV2_GetStartupFlags(void)
{
    return storage_startup_flags;
}
