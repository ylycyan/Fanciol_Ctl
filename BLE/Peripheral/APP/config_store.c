#include "board.h"
#include "config_store.h"
#include "device_protocol.h"
#include "CH58x_common.h"
#include <stddef.h>
#include <string.h>

#define CFG_MAGIC       0x31474643UL
#define RUNTIME_MAGIC   0x35544E52UL
#define IR_MAGIC        0x32524953UL
#define CONNECTIVITY_MAGIC 0x3154454EUL
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
} persisted_config_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t schema_version;
    uint32_t generation;
    uint16_t payload_length;
    uint32_t crc32;
    persisted_config_t payload;
} config_record_t;

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
} runtime_record_t;

typedef char runtime_backup_record_must_fit[
    (sizeof(runtime_record_t) <= EEPROM_PAGE_SIZE) ? 1 : -1
];

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    uint8_t learn_num;
    uint8_t reserved[3];
} ir_record_header_t;

#define IR_CODE_BYTES   ((uint16_t)sizeof(Dev.learnCode))
#define IR_CRC_OFFSET   ((uint32_t)sizeof(ir_record_header_t) + IR_CODE_BYTES)
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
} ir_slot_info_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t schema_version;
    uint8_t state;
    uint32_t generation;
    uint16_t payload_length;
    uint32_t crc32;
    connectivity_config_t payload;
} connectivity_record_t;

#define CONNECTIVITY_RECORD_ACTIVE    0xA1U

typedef char connectivity_record_must_fit_page[
    (sizeof(connectivity_record_t) <= EEPROM_PAGE_SIZE) ? 1 : -1
];

static uint32_t config_revision;
static uint32_t config_slot;
static uint32_t runtime_generation;
static uint16_t runtime_next_slot;
static uint8_t runtime_backup_active;
static uint8_t runtime_write_locked;
static uint32_t config_fingerprint;
static config_load_state_t config_load_state;
static uint32_t ir_fingerprint;
static uint32_t ir_generation;
static uint32_t ir_slot;
static uint8_t ir_write_locked;
static uint8_t lora_params_write_locked;
static uint8_t storage_startup_flags;
static connectivity_config_t connectivity_config;
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

uint32_t Config_Crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL, data, len);
    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t config_record_crc(const config_record_t *record)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL,
                                (const uint8_t *)record,
                                (uint16_t)offsetof(config_record_t, crc32));
    crc = crc32_update(crc,
                       (const uint8_t *)&record->payload,
                       sizeof(record->payload));
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t config_record_erased(const config_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for(i = 0; i < sizeof(*record); ++i) {
        if(bytes[i] != 0xFFU) return 0U;
    }
    return 1U;
}

static void capture_config(persisted_config_t *p)
{
    uint8_t i;
    memset(p, 0, sizeof(*p));
    p->node_id = Dev.nodeId; p->channel = (uint8_t)Dev.channel; p->link_role = Dev.linkRole;
    p->parent_relay_id = Dev.parentRelayId; p->work_mode = Dev.mode; p->ir_action_type = (uint8_t)Dev.irActType;
    p->ir_type = Dev.irType; p->ir_index = Dev.irIdx; memcpy(p->rules, Dev.rules, sizeof(p->rules));
    /* executed 是运行态，不能因规则触发而改写配置版本。 */
    for(i = 0; i < MAX_RULES; ++i) p->rules[i].ctrl.executed = 0;
}

static void apply_config(const persisted_config_t *p)
{
    uint8_t i;
    Dev.nodeId = p->node_id; Dev.channel = p->channel; Dev.linkRole = p->link_role;
    Dev.parentRelayId = p->parent_relay_id; Dev.mode = p->work_mode; Dev.irActType = (ActType_t)p->ir_action_type;
    Dev.irType = p->ir_type; Dev.irIdx = p->ir_index; memcpy(Dev.rules, p->rules, sizeof(Dev.rules));
    for(i = 0; i < MAX_RULES; ++i) Dev.rules[i].ctrl.executed = 0;
}

static uint8_t valid_config_record(const config_record_t *r)
{
    if(r->magic != CFG_MAGIC || r->schema_version != CONFIG_SCHEMA_VERSION || r->payload_length != sizeof(r->payload)) return 0;
    return r->crc32 == config_record_crc(r);
}

uint8_t Config_ValidateCurrent(void)
{
    if(!Dev.nodeId || Dev.channel > 32 || Dev.linkRole > LINK_CHILD || Dev.mode > 1) return DEVICE_STATUS_INVALID_ARG;
    if(Dev.linkRole == LINK_CHILD && (!Dev.parentRelayId || Dev.parentRelayId == Dev.nodeId)) return DEVICE_STATUS_INVALID_ARG;
    if(Dev.irActType > ACT_TYPE_LEARN || (Dev.irIdx != 0xFFu && Dev.irIdx >= IR_BRAND_COUNT)) return DEVICE_STATUS_INVALID_ARG;
    return DEVICE_STATUS_OK;
}

void Config_FactoryDefaults(void)
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
    config_revision = 0; config_slot = CONFIG_SLOT_B;
}

uint8_t Config_Load(void)
{
    config_record_t record;
    uint8_t read_a;
    uint8_t read_b;
    uint8_t valid_a;
    uint8_t valid_b;
    uint8_t erased_a;
    uint8_t erased_b;
    uint8_t repair_status;

    config_load_state = CONFIG_LOAD_HEALTHY;
    storage_startup_flags = 0U;
    memset(&record, 0xFF, sizeof(record));
    read_a = EEPROM_READ(CONFIG_SLOT_A, &record, sizeof(record)) == 0U;
    valid_a = read_a && valid_config_record(&record);
    erased_a = read_a && config_record_erased(&record);
    if(valid_a) {
        apply_config(&record.payload);
        config_revision = record.generation;
        config_slot = CONFIG_SLOT_A;
        config_fingerprint = Config_Crc32((const uint8_t *)&record.payload,
                                            sizeof(record.payload));
    }

    /*
     * 逐槽读取，只在系统栈保留一份记录。CH583 RAM 很小，不能为了启动时
     * 的双槽选择把两份完整规则载荷同时压入主循环栈。
     */
    memset(&record, 0xFF, sizeof(record));
    read_b = EEPROM_READ(CONFIG_SLOT_B, &record, sizeof(record)) == 0U;
    valid_b = read_b && valid_config_record(&record);
    erased_b = read_b && config_record_erased(&record);
    if(valid_b && (!valid_a ||
       (int32_t)(record.generation - config_revision) > 0)) {
        apply_config(&record.payload);
        config_revision = record.generation;
        config_slot = CONFIG_SLOT_B;
        config_fingerprint = Config_Crc32((const uint8_t *)&record.payload,
                                            sizeof(record.payload));
    }

    if(!valid_a && !valid_b) {
        if(!read_a || !read_b) {
            config_load_state = CONFIG_LOAD_IO_ERROR;
            storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        }
        else if(erased_a && erased_b)
            config_load_state = CONFIG_LOAD_EMPTY;
        else
            config_load_state = CONFIG_LOAD_CORRUPT;
        Config_FactoryDefaults();
        return (!read_a || !read_b) ? DEVICE_STATUS_IO_ERROR : DEVICE_STATUS_VERIFY_FAILED;
    }

    /*
     * EEPROM 读取失败时绝不写 Flash，避免把瞬时总线故障误判成坏槽并覆盖数据。
     * 两槽都可读且仅一槽有效时，用已验证载荷重建备用槽。
     */
    if(!read_a || !read_b) {
        config_load_state = CONFIG_LOAD_IO_ERROR;
        storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        return DEVICE_STATUS_OK;
    }
    if(valid_a && valid_b) {
        config_load_state = CONFIG_LOAD_HEALTHY;
        return DEVICE_STATUS_OK;
    }

    repair_status = Config_Commit();
    if(repair_status == DEVICE_STATUS_OK) {
        config_load_state = CONFIG_LOAD_REPAIRED;
        storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
        PRINT("Configuration redundancy repaired, revision=%lu\r\n",
              (unsigned long)config_revision);
    } else {
        config_load_state = CONFIG_LOAD_DEGRADED;
        storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        PRINT("Configuration redundancy repair failed: %u\r\n", repair_status);
    }
    return DEVICE_STATUS_OK;
}

uint8_t Config_Commit(void)
{
    config_record_t r;
    uint32_t target;
    uint32_t committed_generation;
    uint32_t committed_crc;
    if(config_load_state == CONFIG_LOAD_IO_ERROR) return DEVICE_STATUS_IO_ERROR;
    if(Config_ValidateCurrent() != DEVICE_STATUS_OK) return DEVICE_STATUS_INVALID_ARG;
    memset(&r, 0, sizeof(r)); capture_config(&r.payload);
    r.magic = CFG_MAGIC; r.schema_version = CONFIG_SCHEMA_VERSION; r.generation = config_revision + 1u;
    r.payload_length = sizeof(r.payload);
    r.crc32 = config_record_crc(&r);
    committed_generation = r.generation;
    committed_crc = r.crc32;
    target = (config_slot == CONFIG_SLOT_A) ? CONFIG_SLOT_B : CONFIG_SLOT_A;
    if(EEPROM_ERASE(target, EEPROM_BLOCK_SIZE) || EEPROM_WRITE(target, &r, sizeof(r))) return DEVICE_STATUS_IO_ERROR;
    if(EEPROM_READ(target, &r, sizeof(r)) ||
       !valid_config_record(&r) ||
       r.generation != committed_generation ||
       r.crc32 != committed_crc) {
        return DEVICE_STATUS_VERIFY_FAILED;
    }
    config_revision = committed_generation;
    config_slot = target;
    config_fingerprint = Config_Crc32((const uint8_t *)&r.payload,
                                        sizeof(r.payload));
    config_load_state = CONFIG_LOAD_HEALTHY;
    return DEVICE_STATUS_OK;
}

uint8_t Config_CommitIfChanged(void)
{
    persisted_config_t p;
    uint8_t status;
    capture_config(&p);
    if(Config_Crc32((const uint8_t *)&p, sizeof(p)) == config_fingerprint) {
        if(config_load_state != CONFIG_LOAD_DEGRADED) return DEVICE_STATUS_OK;
        status = Config_Commit();
        if(status == DEVICE_STATUS_OK && config_revision == 1U)
            status = Config_Commit();
        return status;
    }
    return Config_Commit();
}

uint32_t Config_GetRevision(void) { return config_revision; }

config_load_state_t Config_GetLoadState(void)
{
    return config_load_state;
}

uint8_t Config_InitializeDefaults(uint8_t recovered_from_corruption)
{
    uint8_t status;

    Config_FactoryDefaults();
    config_load_state = CONFIG_LOAD_EMPTY;
    status = Config_Commit();
    if(status == DEVICE_STATUS_OK) status = Config_Commit();
    if(status != DEVICE_STATUS_OK) {
        config_load_state = CONFIG_LOAD_DEGRADED;
        storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        return status;
    }
    config_load_state = recovered_from_corruption
        ? CONFIG_LOAD_DEFAULTS_RECOVERED
        : CONFIG_LOAD_INITIALIZED;
    if(recovered_from_corruption)
        storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
    return DEVICE_STATUS_OK;
}

static uint8_t valid_runtime_record(const runtime_record_t *r)
{
    return r->magic == RUNTIME_MAGIC &&
           r->on_off <= (uint8_t)PowerOff &&
           r->control_mode <= (uint8_t)Mode_Heat &&
           r->temperature >= 16u && r->temperature <= 31u &&
           r->wind <= (uint8_t)Wind_High &&
           r->crc32 == Config_Crc32((const uint8_t *)r,
                                      (uint16_t)offsetof(runtime_record_t,
                                                         crc32));
}

uint8_t Runtime_Load(void)
{
    runtime_record_t r;
    runtime_record_t best;
    uint8_t found = 0;
    uint8_t read_error = 0U;
    uint8_t invalid_record = 0U;
    uint16_t i;
    runtime_next_slot = 0; runtime_generation = 0; runtime_backup_active = 0;
    runtime_write_locked = 0U;
    for(i = 0; i < RUNTIME_SLOTS; ++i) {
        memset(&r, 0xFF, sizeof(r));
        if(EEPROM_READ(RUNTIME_PAGE + (uint32_t)i * sizeof(r),
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
    if(EEPROM_READ(RUNTIME_BACKUP_PAGE, &r, sizeof(r)) != 0U) {
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
    if(invalid_record) storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
    if(read_error) {
        /*
         * 任一页读取失败后无法证明下一写槽安全，本次启动禁止继续追加。
         * 瞬时故障也宁可少记一段计量，不覆盖尚未读出的更高代记录。
         */
        runtime_write_locked = 1U;
        storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        return DEVICE_STATUS_IO_ERROR;
    }
    return found ? DEVICE_STATUS_OK : DEVICE_STATUS_VERIFY_FAILED;
}

static uint8_t runtime_write_backup(runtime_record_t *record)
{
    runtime_record_t verify;
    if(EEPROM_ERASE(RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) ||
       EEPROM_WRITE(RUNTIME_BACKUP_PAGE, record, sizeof(*record)) ||
       EEPROM_READ(RUNTIME_BACKUP_PAGE, &verify, sizeof(verify)) ||
       memcmp(record, &verify, sizeof(verify)) != 0 ||
       !valid_runtime_record(&verify)) {
        return DEVICE_STATUS_VERIFY_FAILED;
    }
    runtime_backup_active = 1;
    return DEVICE_STATUS_OK;
}

uint8_t Runtime_Append(void)
{
    runtime_record_t r;
    runtime_record_t verify;
    uint32_t address;
    uint8_t rolling_over;

    if(runtime_write_locked) return DEVICE_STATUS_IO_ERROR;

    memset(&r, 0, sizeof(r));
    r.magic = RUNTIME_MAGIC;
    r.generation = runtime_generation + 1u;
    r.meter = Dev.meter;
    r.last_power_change = Dev.lastPowerChange;
    r.on_off = (uint8_t)Dev.onOff;
    r.control_mode = (uint8_t)Dev.ctlMode;
    r.temperature = (uint8_t)Dev.temSet;
    r.wind = (uint8_t)Dev.wind;
    r.crc32 = Config_Crc32(
        (const uint8_t *)&r,
        (uint16_t)offsetof(runtime_record_t, crc32));

    rolling_over = runtime_next_slot >= RUNTIME_SLOTS;
    if(rolling_over) {
        if(runtime_write_backup(&r) != DEVICE_STATUS_OK) return DEVICE_STATUS_VERIFY_FAILED;
    }
    if(runtime_next_slot >= RUNTIME_SLOTS) {
        if(EEPROM_ERASE(RUNTIME_PAGE, EEPROM_BLOCK_SIZE)) return DEVICE_STATUS_IO_ERROR;
        runtime_next_slot = 0;
    }
    address = RUNTIME_PAGE + (uint32_t)runtime_next_slot * sizeof(r);
    if(EEPROM_WRITE(address, &r, sizeof(r))) { runtime_next_slot++; return DEVICE_STATUS_IO_ERROR; }
    runtime_next_slot++;
    if(EEPROM_READ(address, &verify, sizeof(verify)) || memcmp(&r, &verify, sizeof(r)) != 0 ||
       verify.magic != RUNTIME_MAGIC ||
       verify.crc32 != Config_Crc32(
           (const uint8_t *)&verify,
           (uint16_t)offsetof(runtime_record_t, crc32))) {
        return DEVICE_STATUS_VERIFY_FAILED;
    }
    runtime_generation = r.generation;
    if(runtime_backup_active &&
       EEPROM_ERASE(RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) == 0) {
        runtime_backup_active = 0;
    }
    return DEVICE_STATUS_OK;
}

static uint32_t ir_fingerprint_current(uint8_t learn_num)
{
    uint8_t metadata[4] = {learn_num, 0U, 0U, 0U};
    uint32_t crc = crc32_update(0xFFFFFFFFUL, metadata, sizeof(metadata));
    crc = crc32_update(crc, (const uint8_t *)Dev.learnCode, IR_CODE_BYTES);
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t ir_validate_slot(uint32_t slot,
                                ir_slot_info_t *info,
                                uint8_t *io_error)
{
    ir_record_header_t header;
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
                            const ir_slot_info_t *expected,
                            uint8_t *io_error)
{
    ir_record_header_t header;
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

uint8_t IrStore_Load(void)
{
    ir_slot_info_t slot_a;
    ir_slot_info_t slot_b;
    uint8_t io_error = 0U;
    uint8_t repair_status;
    uint32_t selected_fingerprint;
    uint8_t valid_a = ir_validate_slot(IR_STORAGE_PAGE, &slot_a, &io_error);
    uint8_t valid_b = ir_validate_slot(IR_STORAGE_SLOT_B, &slot_b, &io_error);
    uint8_t loaded = 0U;

    ir_write_locked = 0U;

    if(valid_b && (!valid_a || (int32_t)(slot_b.generation - slot_a.generation) > 0)) {
        loaded = ir_load_slot(IR_STORAGE_SLOT_B, &slot_b, &io_error);
        if(!loaded && valid_a) loaded = ir_load_slot(IR_STORAGE_PAGE, &slot_a, &io_error);
    } else if(valid_a) {
        loaded = ir_load_slot(IR_STORAGE_PAGE, &slot_a, &io_error);
        if(!loaded && valid_b) loaded = ir_load_slot(IR_STORAGE_SLOT_B, &slot_b, &io_error);
    }

    if(loaded) {
        if(io_error) {
            ir_write_locked = 1U;
            storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
            return DEVICE_STATUS_IO_ERROR;
        }
        if(valid_a != valid_b) {
            selected_fingerprint = ir_fingerprint;
            ir_fingerprint ^= 1U; /* 强制把已验证数据写入失效的备用槽。 */
            repair_status = IrStore_SaveIfChanged();
            if(repair_status == DEVICE_STATUS_OK) {
                storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
            } else {
                ir_fingerprint = selected_fingerprint;
                storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
            }
        }
        return DEVICE_STATUS_OK;
    }

    Dev.learnNum = 0;
    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    ir_fingerprint = 0;
    ir_generation = 0;
    ir_slot = IR_STORAGE_SLOT_B;
    if(io_error) {
        ir_write_locked = 1U;
        storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
        return DEVICE_STATUS_IO_ERROR;
    }
    if(!slot_a.erased || !slot_b.erased)
        storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
    return DEVICE_STATUS_VERIFY_FAILED;
}

uint8_t IrStore_SaveIfChanged(void)
{
    ir_record_header_t header;
    ir_slot_info_t verify;
    uint32_t target;
    uint32_t fingerprint;
    uint32_t crc;

    if(ir_write_locked) return DEVICE_STATUS_IO_ERROR;
    if(Dev.learnNum > MAX_IR_LEARNNUM) return DEVICE_STATUS_INVALID_ARG;
    fingerprint = ir_fingerprint_current(Dev.learnNum);
    if(fingerprint == ir_fingerprint) return DEVICE_STATUS_OK;

    memset(&header, 0, sizeof(header));
    header.magic = IR_MAGIC;
    header.generation = ir_generation + 1U;
    header.learn_num = Dev.learnNum;
    crc = crc32_update(0xFFFFFFFFUL, (const uint8_t *)&header, sizeof(header));
    crc = crc32_update(crc, (const uint8_t *)Dev.learnCode, IR_CODE_BYTES) ^ 0xFFFFFFFFUL;

    target = (ir_slot == IR_STORAGE_PAGE) ? IR_STORAGE_SLOT_B : IR_STORAGE_PAGE;
    /*
     * CRC 最后写入：任一阶段掉电都会使新槽无效，旧槽仍可回退。
     * 学习码直接从 Dev 分段写入，不在 512 B 系统栈上复制约 2.6 KB 记录。
     */
    if(EEPROM_ERASE(target, EEPROM_BLOCK_SIZE) ||
       EEPROM_WRITE(target, &header, sizeof(header)) ||
       EEPROM_WRITE(target + sizeof(header), Dev.learnCode, IR_CODE_BYTES) ||
       EEPROM_WRITE(target + IR_CRC_OFFSET, &crc, sizeof(crc))) {
        return DEVICE_STATUS_IO_ERROR;
    }
    if(!ir_validate_slot(target, &verify, 0) ||
       verify.generation != header.generation ||
       verify.crc32 != crc ||
       verify.learn_num != header.learn_num) {
        return DEVICE_STATUS_VERIFY_FAILED;
    }

    ir_fingerprint = fingerprint;
    ir_generation = header.generation;
    ir_slot = target;
    return DEVICE_STATUS_OK;
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

static void connectivity_defaults(connectivity_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->transport_mask = CONNECTIVITY_LORA;
    config->lora_register_sf = LORA_SF_LISTEN;
    config->lora_register_bw = LORA_BW_LISTEN;
    config->lora_listen_sf = LORA_SF_SCAN;
    config->lora_listen_bw = LORA_BW_SCAN;
    config->mqtt_port = 1883U;
    config->mqtt_keepalive_sec = 60U;
    config->report_interval_sec = 60U;
    config->network_timeout_sec = 120U;
    config->mqtt_clean_session = 1U;
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

uint8_t DeviceUid_Valid(const char *uid)
{
    uint8_t index;
    if(!uid || uid[0] != 'A' || uid[DEVICE_UID_LENGTH] != '\0') return 0U;
    for(index = 1U; index < 5U; ++index) {
        if(uid[index] < '0' || uid[index] > '9') return 0U;
    }
    if((uid[3] != '0' && uid[3] != '1') ||
       (uid[3] == '0' && uid[4] == '0') ||
       (uid[3] == '1' && (uid[4] < '0' || uid[4] > '2'))) return 0U;
    for(index = 5U; index < DEVICE_UID_LENGTH; ++index) {
        if(!((uid[index] >= '0' && uid[index] <= '9') ||
             (uid[index] >= 'A' && uid[index] <= 'F'))) return 0U;
    }
    if(memcmp(uid + 5U, "0000", 4U) == 0) return 0U;
    return 1U;
}

const char *DeviceUid_Get(void)
{
    static const char hex[] = "0123456789ABCDEF";
    char *uid = connectivity_config.device_id;
    if(!DeviceUid_Valid(uid)) return uid;
    uid[5] = hex[(Dev.nodeId >> 12) & 0x0FU];
    uid[6] = hex[(Dev.nodeId >> 8) & 0x0FU];
    uid[7] = hex[(Dev.nodeId >> 4) & 0x0FU];
    uid[8] = hex[Dev.nodeId & 0x0FU];
    return uid;
}

static uint8_t connectivity_record_erased(const connectivity_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for(i = 0U; i < sizeof(*record); ++i) if(bytes[i] != 0xFFU) return 0U;
    return 1U;
}

static uint32_t connectivity_crc(const connectivity_record_t *record)
{
    uint32_t crc = crc32_update(0xFFFFFFFFUL, (const uint8_t *)record,
                                (uint16_t)offsetof(connectivity_record_t, crc32));
    crc = crc32_update(crc, (const uint8_t *)&record->payload,
                       sizeof(record->payload));
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t connectivity_record_valid(const connectivity_record_t *record)
{
    return record->magic == CONNECTIVITY_MAGIC &&
           record->schema_version == CONNECTIVITY_SCHEMA &&
           record->state == CONNECTIVITY_RECORD_ACTIVE &&
           record->payload_length == sizeof(record->payload) &&
           record->crc32 == connectivity_crc(record) &&
           Connectivity_Validate(&record->payload) == DEVICE_STATUS_OK;
}

uint8_t LoraParams_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw)
{
    if(register_sf < LORA_SF_MIN || register_sf > LORA_SF_MAX ||
       listen_sf < LORA_SF_MIN || listen_sf > LORA_SF_MAX ||
       !valid_lora_bw(register_bw) || !valid_lora_bw(listen_bw)) return DEVICE_STATUS_INVALID_ARG;
    return DEVICE_STATUS_OK;
}

uint8_t Connectivity_Validate(const connectivity_config_t *config)
{
    if(!config || config->transport_mask == 0U ||
       (config->transport_mask & (uint8_t)~(CONNECTIVITY_LORA | CONNECTIVITY_CELLULAR)) != 0U)
        return DEVICE_STATUS_INVALID_ARG;
    if(LoraParams_Validate(config->lora_register_sf,
                             config->lora_register_bw,
                             config->lora_listen_sf,
                             config->lora_listen_bw) != DEVICE_STATUS_OK)
        return DEVICE_STATUS_INVALID_ARG;
    if(config->cellular_pdp_type > CONNECTIVITY_PDP_IPV4V6 || config->mqtt_qos > 1U ||
       config->mqtt_clean_session > 1U || config->mqtt_port == 0U ||
       config->mqtt_keepalive_sec < 15U ||
       config->mqtt_keepalive_sec > 3600U || config->report_interval_sec < 10U ||
       config->report_interval_sec > 3600U || config->network_timeout_sec < 30U ||
       config->network_timeout_sec > 600U)
        return DEVICE_STATUS_INVALID_ARG;
    if(!connectivity_string_valid(config->mqtt_host, sizeof(config->mqtt_host)) ||
       !connectivity_string_valid(config->apn, sizeof(config->apn)) ||
       !connectivity_string_valid(config->device_id, sizeof(config->device_id)))
        return DEVICE_STATUS_INVALID_ARG;
    if((config->transport_mask & CONNECTIVITY_CELLULAR) != 0U &&
       !DeviceUid_Valid(config->device_id)) return DEVICE_STATUS_INVALID_ARG;
    return DEVICE_STATUS_OK;
}

static void connectivity_apply(const connectivity_config_t *config)
{
    memcpy(&connectivity_config, config, sizeof(connectivity_config));
    Dev.loraRegisterSf = config->lora_register_sf;
    Dev.loraRegisterBw = config->lora_register_bw;
    Dev.loraListenSf = config->lora_listen_sf;
    Dev.loraListenBw = config->lora_listen_bw;
}

static uint8_t connectivity_write_record(uint32_t target, uint8_t state,
                                         uint32_t generation,
                                         const connectivity_config_t *config)
{
    connectivity_record_t record;
    uint32_t expected_crc;
    memset(&record, 0, sizeof(record));
    record.magic = CONNECTIVITY_MAGIC;
    record.schema_version = CONNECTIVITY_SCHEMA;
    record.state = state;
    record.generation = generation;
    record.payload_length = sizeof(record.payload);
    memcpy(&record.payload, config, sizeof(record.payload));
    record.crc32 = connectivity_crc(&record);
    expected_crc = record.crc32;
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_WRITE(target, &record, sizeof(record)) != 0U ||
       EEPROM_READ(target, &record, sizeof(record)) != 0U)
        return DEVICE_STATUS_IO_ERROR;
    if(!connectivity_record_valid(&record) || record.state != state ||
       record.generation != generation || record.crc32 != expected_crc)
        return DEVICE_STATUS_VERIFY_FAILED;
    return DEVICE_STATUS_OK;
}

uint8_t Connectivity_Load(void)
{
    connectivity_record_t record;
    uint8_t first_valid;
    uint8_t second_valid;
    uint8_t first_erased;
    uint8_t second_erased;
    uint8_t selected_valid = 0U;

    lora_params_write_locked = 0U;
    memset(&record, 0xFF, sizeof(record));
    if(EEPROM_READ(CONNECTIVITY_SLOT_A, &record, sizeof(record)) != 0U)
        goto connectivity_read_error;
    first_valid = connectivity_record_valid(&record) &&
                  record.state == CONNECTIVITY_RECORD_ACTIVE;
    first_erased = connectivity_record_erased(&record);
    if(first_valid) {
        connectivity_generation = record.generation;
        connectivity_slot = CONNECTIVITY_SLOT_A;
        connectivity_apply(&record.payload);
        selected_valid = 1U;
    }

    /* Reuse one page-sized scratch record to keep the boot task stack bounded. */
    memset(&record, 0xFF, sizeof(record));
    if(EEPROM_READ(CONNECTIVITY_SLOT_B, &record, sizeof(record)) != 0U)
        goto connectivity_read_error;
    second_valid = connectivity_record_valid(&record) &&
                   record.state == CONNECTIVITY_RECORD_ACTIVE;
    second_erased = connectivity_record_erased(&record);
    if(second_valid && (!selected_valid ||
       (int32_t)(record.generation - connectivity_generation) > 0)) {
        connectivity_generation = record.generation;
        connectivity_slot = CONNECTIVITY_SLOT_B;
        connectivity_apply(&record.payload);
        selected_valid = 1U;
    }
    if(selected_valid) {
        if(first_valid != second_valid) {
            uint32_t target = connectivity_slot == CONNECTIVITY_SLOT_A ?
                              CONNECTIVITY_SLOT_B : CONNECTIVITY_SLOT_A;
            uint32_t expected_crc;
            memset(&record, 0, sizeof(record));
            record.magic = CONNECTIVITY_MAGIC;
            record.schema_version = CONNECTIVITY_SCHEMA;
            record.state = CONNECTIVITY_RECORD_ACTIVE;
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
                storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
            } else {
                connectivity_generation = record.generation;
                connectivity_slot = target;
                /* A blank peer slot is normal after the very first commit.
                 * Rebuild its redundancy silently; only a non-erased invalid
                 * record represents corruption worth exposing as recovered. */
                if(!((first_valid && second_erased) || (second_valid && first_erased)))
                    storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
            }
        }
        return DEVICE_STATUS_OK;
    }

    connectivity_defaults(&connectivity_config);
    connectivity_generation = 0U;
    connectivity_slot = CONNECTIVITY_SLOT_B;
    connectivity_apply(&connectivity_config);
    if(!first_erased || !second_erased)
        storage_startup_flags |= STORAGE_STARTUP_RECOVERED;
    return DEVICE_STATUS_VERIFY_FAILED;

connectivity_read_error:
    connectivity_defaults(&connectivity_config);
    connectivity_apply(&connectivity_config);
    connectivity_generation = 0U;
    connectivity_slot = CONNECTIVITY_SLOT_B;
    lora_params_write_locked = 1U;
    storage_startup_flags |= STORAGE_STARTUP_DEGRADED;
    return DEVICE_STATUS_IO_ERROR;
}

uint8_t Connectivity_Save(const connectivity_config_t *config)
{
    uint32_t target;
    uint8_t status = Connectivity_Validate(config);
    if(lora_params_write_locked) return DEVICE_STATUS_IO_ERROR;
    if(status != DEVICE_STATUS_OK) return status;
    if(memcmp(config, &connectivity_config, sizeof(*config)) == 0 && connectivity_generation != 0U)
        return DEVICE_STATUS_OK;
    target = connectivity_slot == CONNECTIVITY_SLOT_A ?
             CONNECTIVITY_SLOT_B : CONNECTIVITY_SLOT_A;
    status = connectivity_write_record(target, CONNECTIVITY_RECORD_ACTIVE,
                                       connectivity_generation + 1U, config);
    if(status != DEVICE_STATUS_OK) return status;
    connectivity_generation++;
    connectivity_slot = target;
    connectivity_apply(config);
    return DEVICE_STATUS_OK;
}

const connectivity_config_t *Connectivity_Get(void)
{
    return &connectivity_config;
}

uint8_t Connectivity_LoraEnabled(void)
{
    return (connectivity_config.transport_mask & CONNECTIVITY_LORA) != 0U;
}

uint8_t Connectivity_CellularEnabled(void)
{
    return (connectivity_config.transport_mask & CONNECTIVITY_CELLULAR) != 0U;
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

uint8_t Connectivity_Encode(uint8_t *payload, uint16_t capacity, uint16_t *length)
{
    const connectivity_config_t *config = &connectivity_config;
    uint16_t offset = 17U;
    if(!payload || !length || capacity < offset) return DEVICE_STATUS_INVALID_ARG;
    payload[0] = CONNECTIVITY_SCHEMA;
    payload[1] = config->transport_mask;
    payload[2] = config->lora_register_sf;
    payload[3] = config->lora_register_bw;
    payload[4] = config->lora_listen_sf;
    payload[5] = config->lora_listen_bw;
    payload[6] = config->cellular_pdp_type;
    payload[7] = config->mqtt_qos;
    payload[8] = config->mqtt_clean_session;
    payload[9] = (uint8_t)config->mqtt_port;
    payload[10] = (uint8_t)(config->mqtt_port >> 8);
    payload[11] = (uint8_t)config->mqtt_keepalive_sec;
    payload[12] = (uint8_t)(config->mqtt_keepalive_sec >> 8);
    payload[13] = (uint8_t)config->report_interval_sec;
    payload[14] = (uint8_t)(config->report_interval_sec >> 8);
    payload[15] = (uint8_t)config->network_timeout_sec;
    payload[16] = (uint8_t)(config->network_timeout_sec >> 8);
    if(!wire_put_string(payload, capacity, &offset, config->mqtt_host, sizeof(config->mqtt_host)) ||
       !wire_put_string(payload, capacity, &offset, config->apn, sizeof(config->apn)))
        return DEVICE_STATUS_INVALID_ARG;
    *length = offset;
    return DEVICE_STATUS_OK;
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

uint8_t Connectivity_Decode(const uint8_t *payload, uint16_t length,
                              connectivity_config_t *config)
{
    uint16_t offset = 17U;
    if(!payload || !config || length < offset || payload[0] != CONNECTIVITY_SCHEMA)
        return DEVICE_STATUS_INVALID_ARG;
    (void)DeviceUid_Get();
    memcpy(config, &connectivity_config, sizeof(*config));
    config->transport_mask = payload[1];
    config->lora_register_sf = payload[2];
    config->lora_register_bw = payload[3];
    config->lora_listen_sf = payload[4];
    config->lora_listen_bw = payload[5];
    config->cellular_pdp_type = payload[6];
    config->mqtt_qos = payload[7];
    config->mqtt_clean_session = payload[8];
    config->mqtt_port = (uint16_t)payload[9] | ((uint16_t)payload[10] << 8);
    config->mqtt_keepalive_sec = (uint16_t)payload[11] | ((uint16_t)payload[12] << 8);
    config->report_interval_sec = (uint16_t)payload[13] | ((uint16_t)payload[14] << 8);
    config->network_timeout_sec = (uint16_t)payload[15] | ((uint16_t)payload[16] << 8);
    if(!wire_get_string(payload, length, &offset, config->mqtt_host, sizeof(config->mqtt_host)) ||
       !wire_get_string(payload, length, &offset, config->apn, sizeof(config->apn)) ||
       offset != length) return DEVICE_STATUS_INVALID_ARG;
    return Connectivity_Validate(config);
}

uint8_t LoraParams_Load(void)
{
    return Connectivity_Load();
}

uint8_t LoraParams_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw)
{
    connectivity_config_t next;
    uint8_t status = LoraParams_Validate(register_sf, register_bw, listen_sf, listen_bw);
    if(lora_params_write_locked) return DEVICE_STATUS_IO_ERROR;
    if(status != DEVICE_STATUS_OK) return status;
    memcpy(&next, &connectivity_config, sizeof(next));
    next.lora_register_sf = register_sf;
    next.lora_register_bw = register_bw;
    next.lora_listen_sf = listen_sf;
    next.lora_listen_bw = listen_bw;
    return Connectivity_Save(&next);
}

uint8_t Storage_FactoryReset(void)
{
    char device_id[DEVICE_UID_LENGTH + 1U];
    memset(device_id, 0, sizeof(device_id));
    if(DeviceUid_Valid(connectivity_config.device_id))
        memcpy(device_id, connectivity_config.device_id, sizeof(device_id));
    /*
     * 先清运行分区，最后清配置双槽并提交默认值。任一步失败都返回错误，
     * 避免小程序显示“恢复成功”但旧计量/学习码仍在。健康与复位历史保留，
     * 便于现场追溯恢复出厂前后的异常原因。
     */
    if(EEPROM_ERASE(STORAGE_RESERVED_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(RUNTIME_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE) ||
       EEPROM_ERASE(IR_STORAGE_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(IR_STORAGE_SLOT_B, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(CONFIG_SLOT_A, EEPROM_BLOCK_SIZE) ||
       EEPROM_ERASE(CONFIG_SLOT_B, EEPROM_BLOCK_SIZE)) {
        return DEVICE_STATUS_IO_ERROR;
    }

    runtime_generation = 0;
    runtime_next_slot = 0;
    runtime_backup_active = 0;
    runtime_write_locked = 0U;
    ir_fingerprint = 0;
    ir_generation = 0;
    ir_slot = IR_STORAGE_SLOT_B;
    ir_write_locked = 0U;
    lora_params_write_locked = 0U;
    connectivity_generation = 0U;
    connectivity_slot = CONNECTIVITY_SLOT_B;
    connectivity_defaults(&connectivity_config);
    memcpy(connectivity_config.device_id, device_id, sizeof(device_id));
    storage_startup_flags = 0U;
    if(Connectivity_Save(&connectivity_config) != DEVICE_STATUS_OK)
        return DEVICE_STATUS_IO_ERROR;
    return Config_InitializeDefaults(0U);
}

uint8_t Storage_GetStartupFlags(void)
{
    return storage_startup_flags;
}
