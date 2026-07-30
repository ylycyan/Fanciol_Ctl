#include "board.h"
#include "config_store_v2.h"
#include "protocol_v2.h"
#include "CH58x_common.h"
#include <string.h>

#define CFG_MAGIC       0x32434153UL
#define RUNTIME_MAGIC   0x32544E52UL
#define IR_MAGIC        0x32524953UL
#define LORA_PARAM_MAGIC 0x3250524CUL
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
    uint16_t run_time;
    uint16_t reset_reason;
    uint32_t crc32;
    uint8_t padding[4];
} runtime_record_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t learn_num;
    uint8_t reserved[3];
    IR_LEARNING_t code[MAX_IR_LEARNNUM];
    uint32_t crc32;
} ir_record_v2_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t register_sf;
    uint8_t register_bw;
    uint8_t listen_sf;
    uint8_t listen_bw;
    uint32_t crc32;
} lora_param_record_v2_t;

static uint32_t config_revision;
static uint32_t config_slot;
static uint32_t runtime_generation;
static uint16_t runtime_next_slot;
static uint32_t config_fingerprint;
static uint32_t ir_fingerprint;

uint32_t ConfigV2_Crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t bit;
    for(i = 0; i < len; ++i) {
        crc ^= data[i];
        for(bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320UL : 0u);
    }
    return crc ^ 0xFFFFFFFFUL;
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
    return r->crc32 == ConfigV2_Crc32((const uint8_t *)&r->payload, sizeof(r->payload));
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
    config_revision = 0; config_slot = V2_CONFIG_SLOT_B;
}

uint8_t ConfigV2_Load(void)
{
    config_record_v2_t a;
    config_record_v2_t b;
    const config_record_v2_t *selected = 0;
    EEPROM_READ(V2_CONFIG_SLOT_A, &a, sizeof(a)); EEPROM_READ(V2_CONFIG_SLOT_B, &b, sizeof(b));
    if(valid_config_record(&a)) selected = &a;
    if(valid_config_record(&b) && (!selected || (int32_t)(b.generation - selected->generation) > 0)) selected = &b;
    if(!selected) { ConfigV2_FactoryDefaults(); return V2_STATUS_VERIFY_FAILED; }
    apply_config(&selected->payload); config_revision = selected->generation;
    config_slot = (selected == &a) ? V2_CONFIG_SLOT_A : V2_CONFIG_SLOT_B;
    config_fingerprint = ConfigV2_Crc32((const uint8_t *)&selected->payload, sizeof(selected->payload));
    return V2_STATUS_OK;
}

uint8_t ConfigV2_Commit(uint32_t expected_revision)
{
    config_record_v2_t r;
    config_record_v2_t verify;
    uint32_t target;
    if(expected_revision != config_revision) return V2_STATUS_CONFLICT;
    if(ConfigV2_ValidateCurrent() != V2_STATUS_OK) return V2_STATUS_INVALID_ARG;
    memset(&r, 0, sizeof(r)); capture_config(&r.payload);
    r.magic = CFG_MAGIC; r.schema_version = V2_CONFIG_SCHEMA_VERSION; r.generation = config_revision + 1u;
    r.payload_length = sizeof(r.payload); r.crc32 = ConfigV2_Crc32((const uint8_t *)&r.payload, sizeof(r.payload));
    target = (config_slot == V2_CONFIG_SLOT_A) ? V2_CONFIG_SLOT_B : V2_CONFIG_SLOT_A;
    if(EEPROM_ERASE(target, EEPROM_BLOCK_SIZE) || EEPROM_WRITE(target, &r, sizeof(r))) return V2_STATUS_IO_ERROR;
    if(EEPROM_READ(target, &verify, sizeof(verify)) || !valid_config_record(&verify) || verify.generation != r.generation) return V2_STATUS_VERIFY_FAILED;
    config_revision = r.generation; config_slot = target; config_fingerprint = r.crc32;
    return V2_STATUS_OK;
}

uint8_t ConfigV2_CommitIfChanged(void)
{
    persisted_config_v2_t p;
    capture_config(&p);
    if(ConfigV2_Crc32((const uint8_t *)&p, sizeof(p)) == config_fingerprint) return V2_STATUS_OK;
    return ConfigV2_Commit(config_revision);
}

uint32_t ConfigV2_GetRevision(void) { return config_revision; }

uint8_t RuntimeV2_Load(void)
{
    runtime_record_v2_t r;
    runtime_record_v2_t best;
    uint8_t found = 0;
    uint16_t i;
    runtime_next_slot = 0; runtime_generation = 0;
    for(i = 0; i < RUNTIME_SLOTS; ++i) {
        EEPROM_READ(V2_RUNTIME_PAGE + (uint32_t)i * sizeof(r), &r, sizeof(r));
        if(r.magic == 0xFFFFFFFFUL) { runtime_next_slot = i; break; }
        if(r.magic == RUNTIME_MAGIC && r.crc32 == ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 8u))) {
            if(!found || (int32_t)(r.generation - best.generation) > 0) { best = r; found = 1; }
        }
        runtime_next_slot = (uint16_t)(i + 1u);
    }
    if(found) { Dev.meter = best.meter; Dev.runTime = best.run_time; runtime_generation = best.generation; }
    return found ? V2_STATUS_OK : V2_STATUS_VERIFY_FAILED;
}

uint8_t RuntimeV2_Append(void)
{
    runtime_record_v2_t r;
    if(runtime_next_slot >= RUNTIME_SLOTS) {
        if(EEPROM_ERASE(V2_RUNTIME_PAGE, EEPROM_BLOCK_SIZE)) return V2_STATUS_IO_ERROR;
        runtime_next_slot = 0;
    }
    memset(&r, 0, sizeof(r)); r.magic = RUNTIME_MAGIC; r.generation = ++runtime_generation; r.meter = Dev.meter; r.run_time = Dev.runTime;
    r.reset_reason = SYS_GetLastResetSta(); r.crc32 = ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 8u));
    if(EEPROM_WRITE(V2_RUNTIME_PAGE + (uint32_t)runtime_next_slot * sizeof(r), &r, sizeof(r))) return V2_STATUS_IO_ERROR;
    runtime_next_slot++; return V2_STATUS_OK;
}

uint8_t IrStoreV2_Load(void)
{
    ir_record_v2_t r;
    EEPROM_READ(V2_IR_PAGE, &r, sizeof(r));
    if(r.magic != IR_MAGIC || r.crc32 != ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 4u))) return V2_STATUS_VERIFY_FAILED;
    Dev.learnNum = r.learn_num; memcpy(Dev.learnCode, r.code, sizeof(r.code)); ir_fingerprint = r.crc32; return V2_STATUS_OK;
}

uint8_t IrStoreV2_SaveIfChanged(void)
{
    ir_record_v2_t r;
    memset(&r, 0, sizeof(r)); r.magic = IR_MAGIC; r.learn_num = Dev.learnNum; memcpy(r.code, Dev.learnCode, sizeof(r.code));
    r.crc32 = ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 4u));
    if(r.crc32 == ir_fingerprint) return V2_STATUS_OK;
    if(EEPROM_ERASE(V2_IR_PAGE, EEPROM_BLOCK_SIZE) || EEPROM_WRITE(V2_IR_PAGE, &r, sizeof(r))) return V2_STATUS_IO_ERROR;
    ir_fingerprint = r.crc32; return V2_STATUS_OK;
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

uint8_t LoraParamsV2_Validate(uint8_t register_sf, uint8_t register_bw,
                              uint8_t listen_sf, uint8_t listen_bw)
{
    if(register_sf < LORA_SF_MIN || register_sf > LORA_SF_MAX ||
       listen_sf < LORA_SF_MIN || listen_sf > LORA_SF_MAX ||
       !valid_lora_bw(register_bw) || !valid_lora_bw(listen_bw)) return V2_STATUS_INVALID_ARG;
    return V2_STATUS_OK;
}

uint8_t LoraParamsV2_Load(void)
{
    lora_param_record_v2_t r;
    EEPROM_READ(V2_RESERVED_PAGE, &r, sizeof(r));
    if(r.magic != LORA_PARAM_MAGIC ||
       r.crc32 != ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 4u)) ||
       LoraParamsV2_Validate(r.register_sf, r.register_bw, r.listen_sf, r.listen_bw) != V2_STATUS_OK) {
        Dev.loraRegisterSf = LORA_SF_LISTEN; Dev.loraRegisterBw = LORA_BW_LISTEN;
        Dev.loraListenSf = LORA_SF_SCAN; Dev.loraListenBw = LORA_BW_SCAN;
        return V2_STATUS_VERIFY_FAILED;
    }
    Dev.loraRegisterSf = r.register_sf; Dev.loraRegisterBw = r.register_bw;
    Dev.loraListenSf = r.listen_sf; Dev.loraListenBw = r.listen_bw;
    return V2_STATUS_OK;
}

uint8_t LoraParamsV2_Save(uint8_t register_sf, uint8_t register_bw,
                          uint8_t listen_sf, uint8_t listen_bw)
{
    lora_param_record_v2_t r, verify;
    uint8_t status = LoraParamsV2_Validate(register_sf, register_bw, listen_sf, listen_bw);
    if(status != V2_STATUS_OK) return status;
    memset(&r, 0, sizeof(r)); r.magic = LORA_PARAM_MAGIC;
    r.register_sf = register_sf; r.register_bw = register_bw;
    r.listen_sf = listen_sf; r.listen_bw = listen_bw;
    r.crc32 = ConfigV2_Crc32((const uint8_t *)&r, (uint16_t)(sizeof(r) - 4u));
    if(EEPROM_ERASE(V2_RESERVED_PAGE, EEPROM_BLOCK_SIZE) ||
       EEPROM_WRITE(V2_RESERVED_PAGE, &r, sizeof(r)) ||
       EEPROM_READ(V2_RESERVED_PAGE, &verify, sizeof(verify)) ||
       memcmp(&r, &verify, sizeof(r)) != 0) return V2_STATUS_VERIFY_FAILED;
    Dev.loraRegisterSf = register_sf; Dev.loraRegisterBw = register_bw;
    Dev.loraListenSf = listen_sf; Dev.loraListenBw = listen_bw;
    return V2_STATUS_OK;
}
