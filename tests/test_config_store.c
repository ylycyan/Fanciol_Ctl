#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "../BLE/Peripheral/APP/include/config_store.h"
#include "../BLE/Peripheral/APP/include/device_protocol.h"

#define DATAFLASH_SIZE 0x8000u

typedef enum {
    FAIL_NONE = 0,
    FAIL_READ,
    FAIL_ERASE,
    FAIL_WRITE
} fail_type_t;

typedef struct {
    fail_type_t type;
    uint32_t occurrence;
    uint32_t seen;
    uint8_t partial;
} failure_t;

t_dev Dev;
IRBUF_t IrBuf;
static uint8_t dataflash[DATAFLASH_SIZE];
static failure_t failure;
static uint8_t silent_commit_marker, fail_backup_cleanup;
static uint32_t backup_erase_calls;

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
    char mqtt_host[48];
    char apn[20];
    char device_id[10];
} connectivity_v4_payload_t;

typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;
    uint8_t schema_version;
    uint8_t state;
    uint32_t generation;
    uint16_t payload_length;
    uint32_t crc32;
    connectivity_v4_payload_t payload;
} connectivity_v4_record_t;

static void clear_failure(void)
{
    memset(&failure, 0, sizeof(failure));
}

static void fail_on(fail_type_t type, uint32_t occurrence, uint8_t partial)
{
    failure.type = type;
    failure.occurrence = occurrence;
    failure.seen = 0;
    failure.partial = partial;
}

static uint8_t should_fail(fail_type_t type)
{
    if(failure.type != type) return 0;
    failure.seen++;
    if(failure.seen != failure.occurrence) return 0;
    failure.type = FAIL_NONE;
    return 1;
}

uint32_t Test_EepromRead(uint32_t address, void *buffer, uint32_t length)
{
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    if(should_fail(FAIL_READ)) return 1;
    memcpy(buffer, dataflash + address, length);
    return 0;
}

uint32_t Test_EepromErase(uint32_t address, uint32_t length)
{
    if(fail_backup_cleanup && address == RUNTIME_BACKUP_PAGE && ++backup_erase_calls > 1U)
        return 1U;
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    assert((address % EEPROM_PAGE_SIZE) == 0u);
    assert((length % EEPROM_PAGE_SIZE) == 0u);
    if(should_fail(FAIL_ERASE)) {
        if(failure.partial) memset(dataflash + address, 0xFF, length / 2u);
        return 1;
    }
    memset(dataflash + address, 0xFF, length);
    return 0;
}

uint32_t Test_EepromWrite(uint32_t address, const void *buffer, uint32_t length)
{
    if(silent_commit_marker && length == 4U &&
       (address == CONFIG_SLOT_A || address == CONFIG_SLOT_B)) return 0U;
    uint32_t i;
    uint32_t written = length;
    const uint8_t *source = (const uint8_t *)buffer;
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    if(should_fail(FAIL_WRITE)) {
        if(!failure.partial) return 1;
        written = length / 2u;
    }
    for(i = 0; i < written; ++i) dataflash[address + i] &= source[i];
    return written == length ? 0u : 1u;
}

uint32_t SYS_GetLastResetSta(void)
{
    return 3u;
}

static void reset_flash(void)
{
    silent_commit_marker = fail_backup_cleanup = 0U;
    backup_erase_calls = 0U;
    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(&Dev, 0, sizeof(Dev));
    clear_failure();
}

static void create_initial_config(void)
{
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetLoadState() == CONFIG_LOAD_EMPTY);
    assert(Config_InitializeDefaults(0u) == DEVICE_STATUS_OK);
    assert(Config_GetLoadState() == CONFIG_LOAD_INITIALIZED);
    assert(Config_GetRevision() == 2u);
}

static void test_config_slot_recovery(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    create_initial_config();
    memcpy(snapshot, dataflash, sizeof(snapshot));

    Dev.nodeId = 0x2345u;
    fail_on(FAIL_WRITE, 1, 1);
    assert(Config_Commit() == DEVICE_STATUS_IO_ERROR);
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetRevision() == 3u);
    assert(Config_GetLoadState() == CONFIG_LOAD_REPAIRED);

    memcpy(dataflash, snapshot, sizeof(snapshot));
    assert(Config_Load() == DEVICE_STATUS_OK);
    Dev.nodeId = 0x2345u;
    fail_on(FAIL_READ, 1, 0);
    assert(Config_Commit() == DEVICE_STATUS_VERIFY_FAILED);
    clear_failure();
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetRevision() == 3u);
    assert(Config_GetLoadState() == CONFIG_LOAD_REPAIRED);

    Dev.nodeId = 0x2345u;
    assert(Config_Commit() == DEVICE_STATUS_OK);

    /*
     * generation 也受 CRC 保护；最新槽头损坏时回退上一代并立即重建冗余，
     * 不能只校验 payload 后误选一个被翻转成“大版本”的记录。
     */
    dataflash[CONFIG_SLOT_B + 6u] ^= 0x40u;
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetRevision() == 4u);
    assert(Config_GetLoadState() == CONFIG_LOAD_REPAIRED);
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Config_GetLoadState() == CONFIG_LOAD_HEALTHY);

    /* 两槽同时损坏才恢复 RAM 默认值，并明确留下 CORRUPT 状态。 */
    dataflash[CONFIG_SLOT_A + 20u] ^= 0xA5u;
    dataflash[CONFIG_SLOT_B + 20u] ^= 0x5Au;
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetLoadState() == CONFIG_LOAD_CORRUPT);
    assert(Config_InitializeDefaults(1u) == DEVICE_STATUS_OK);
    assert(Config_GetLoadState() == CONFIG_LOAD_DEFAULTS_RECOVERED);
    assert(Config_GetRevision() == 2u);
}

static void test_config_read_error_never_overwrites_flash(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    create_initial_config();
    Dev.nodeId = 0x4567u;
    assert(Config_Commit() == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    /* A 是最新槽；读取 A 失败时可临时使用 B，但绝不能擦写任何一个槽。 */
    fail_on(FAIL_READ, 1u, 0u);
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Config_GetLoadState() == CONFIG_LOAD_IO_ERROR);
    assert(Storage_GetStartupFlags() == STORAGE_STARTUP_DEGRADED);
    assert(Dev.nodeId == Default_DevId);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);

    clear_failure();
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Config_GetLoadState() == CONFIG_LOAD_HEALTHY);
    assert(Dev.nodeId == 0x4567u);
    assert(Config_GetRevision() == 3u);

    /* 全空白且读取失败时同样只使用 RAM 默认值，不得“初始化”并覆盖现场数据。 */
    reset_flash();
    memcpy(snapshot, dataflash, sizeof(snapshot));
    fail_on(FAIL_READ, 1u, 0u);
    assert(Config_Load() == DEVICE_STATUS_IO_ERROR);
    assert(Config_GetLoadState() == CONFIG_LOAD_IO_ERROR);
    assert(Storage_GetStartupFlags() == STORAGE_STARTUP_DEGRADED);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void test_verified_commit_keeps_rejected_values_out_of_flash(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];
    connectivity_config_t next;
    uint32_t stage;
    reset_flash(); create_initial_config();
    memcpy(snapshot, dataflash, sizeof(snapshot));
    for(stage = 1U; stage <= 4U; stage++) {
        memcpy(dataflash, snapshot, sizeof(snapshot)); clear_failure();
        assert(Config_Load() == DEVICE_STATUS_OK);
        Dev.nodeId = 0x4321U;
        fail_on(FAIL_READ, stage, 0U);
        assert(Config_Commit() == DEVICE_STATUS_VERIFY_FAILED);
        clear_failure(); assert(Config_Load() == DEVICE_STATUS_OK);
        assert(Dev.nodeId == Default_DevId);
    }
    memcpy(dataflash, snapshot, sizeof(snapshot)); clear_failure();
    assert(Config_Load() == DEVICE_STATUS_OK); Dev.nodeId = 0x4321U;
    fail_on(FAIL_WRITE, 2U, 1U); /* 有效标记写一半不能发布新值。 */
    assert(Config_Commit() == DEVICE_STATUS_IO_ERROR);
    clear_failure(); assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);

    reset_flash(); assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    next = *Connectivity_Get(); assert(Connectivity_Save(&next) == DEVICE_STATUS_OK);
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));
    for(stage = 1U; stage <= 6U; stage++) {
        memcpy(dataflash, snapshot, sizeof(snapshot)); clear_failure();
        assert(Connectivity_Load() == DEVICE_STATUS_OK);
        next = *Connectivity_Get(); next.report_interval_sec = 600U;
        fail_on(FAIL_READ, stage, 0U);
        assert(Connectivity_Save(&next) == DEVICE_STATUS_VERIFY_FAILED);
        clear_failure(); assert(Connectivity_Load() == DEVICE_STATUS_OK);
        assert(Connectivity_Get()->report_interval_sec == 60U);
    }
}

static void fill_runtime_page(uint8_t *snapshot)
{
    uint32_t i;
    reset_flash();
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED); /* 同时清零启动存储标志。 */
    assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
    for(i = 1; i <= 64u; ++i) {
        Dev.meter.fault_count = (uint16_t)i;
        Dev.lastPowerChange = 1000u + i;
        Dev.onOff = (i & 1u) ? PowerOn : PowerOff;
        Dev.ctlMode = (Mode_t)(i % 5u);
        Dev.temSet = (uint8_t)(16u + (i % 16u));
        Dev.wind = (Wind_t)(i % 4u);
        assert(Runtime_Append() == DEVICE_STATUS_OK);
    }
    memcpy(snapshot, dataflash, DATAFLASH_SIZE);
}

static void test_runtime_read_error_is_visible_and_non_destructive(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
    Dev.meter.fault_count = 17U;
    Dev.lastPowerChange = 123456U;
    Dev.onOff = PowerOn;
    Dev.ctlMode = Mode_Cool;
    Dev.temSet = 23U;
    Dev.wind = Wind_High;
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    memset(&Dev.meter, 0, sizeof(Dev.meter));
    Dev.lastPowerChange = 0U;
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 25U;
    Dev.wind = Wind_Auto;
    /* 有效记录及下一空槽可读，随后读取失败也必须锁定写入。 */
    fail_on(FAIL_READ, 3U, 0U);
    assert(Runtime_Load() == DEVICE_STATUS_IO_ERROR);
    assert(Dev.meter.fault_count == 17U);
    assert(Dev.lastPowerChange == 123456U);
    assert(Dev.onOff == PowerOn);
    assert(Dev.ctlMode == Mode_Cool);
    assert(Dev.temSet == 23U);
    assert(Dev.wind == Wind_High);
    assert((Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    Dev.meter.fault_count = 18U;
    assert(Runtime_Append() == DEVICE_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void restore_runtime_snapshot(const uint8_t *snapshot)
{
    memcpy(dataflash, snapshot, DATAFLASH_SIZE);
    memset(&Dev, 0, sizeof(Dev));
    clear_failure();
    assert(Runtime_Load() == DEVICE_STATUS_OK);
    assert(Dev.meter.fault_count == 64u);
    assert(Dev.lastPowerChange == 1064u);
    assert(Dev.onOff == PowerOff);
    assert(Dev.ctlMode == (Mode_t)(64u % 5u));
    assert(Dev.temSet == (uint8_t)(16u + (64u % 16u)));
    assert(Dev.wind == Wind_Auto);
    Dev.meter.fault_count = 65u;
    Dev.lastPowerChange = 1065u;
    Dev.onOff = PowerOn;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 17u;
    Dev.wind = Wind_Low;
}

static void assert_runtime_recovers_64_or_65(uint32_t expected)
{
    memset(&Dev, 0, sizeof(Dev));
    clear_failure();
    assert(Runtime_Load() == DEVICE_STATUS_OK);
    assert(Dev.meter.fault_count == expected);
    assert(Dev.lastPowerChange == 1000u + expected);
    assert(Dev.onOff == ((expected & 1u) ? PowerOn : PowerOff));
    assert(Dev.ctlMode == (Mode_t)(expected % 5u));
    assert(Dev.temSet == (uint8_t)(16u + (expected % 16u)));
    assert(Dev.wind == (Wind_t)(expected % 4u));
}

static void test_runtime_rollover_power_loss(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];
    fill_runtime_page(snapshot);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_WRITE, 1, 1); /* 检查点写一半，主日志未擦。 */
    assert(Runtime_Append() == DEVICE_STATUS_VERIFY_FAILED);
    assert_runtime_recovers_64_or_65(64u);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_ERASE, 2, 1); /* 检查点完成，主日志擦除中掉电。 */
    assert(Runtime_Append() == DEVICE_STATUS_OK); /* 本次值已持久化到检查点。 */
    assert(Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED);
    assert_runtime_recovers_64_or_65(65u);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_WRITE, 3, 1); /* 主日志首条候选写一半，检查点仍完整。 */
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    assert(Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED);
    assert_runtime_recovers_64_or_65(65u);

    restore_runtime_snapshot(snapshot);
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    assert_runtime_recovers_64_or_65(65u);
    assert(dataflash[RUNTIME_BACKUP_PAGE] == 0xFFu);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_ERASE, 2U, 1U);
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash + RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE);
    Dev.meter.fault_count = 66U;
    fail_on(FAIL_WRITE, 1U, 1U); /* 重试主日志，不能先擦掉唯一有效检查点。 */
    assert(Runtime_Append() == DEVICE_STATUS_IO_ERROR);
    assert(!memcmp(snapshot, dataflash + RUNTIME_BACKUP_PAGE, EEPROM_PAGE_SIZE));
    assert_runtime_recovers_64_or_65(65U);
}

static void test_runtime_uncommitted_slot_does_not_hide_later_records(void)
{
    for(uint32_t stage = 0U; stage <= 2U; stage++) {
        reset_flash(); assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
        Dev.onOff = PowerOff; Dev.temSet = 25U; Dev.meter.energy_wh = 123U;
        /* 不写入任何字节、仅空标记读取失败、候选写完后读回失败。 */
        fail_on(stage ? FAIL_READ : FAIL_WRITE, stage ? stage : 1U, 0U);
        assert(Runtime_Append() != DEVICE_STATUS_OK);
        clear_failure(); Dev.meter.energy_wh = 456U;
        assert(Runtime_Append() == DEVICE_STATUS_OK);
        assert(Runtime_Load() == DEVICE_STATUS_OK);
        assert(Dev.meter.energy_wh == 456U);
    }
}

static void test_ir_double_slot_recovery(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];
    uint32_t write_stage;

    reset_flash();
    assert(IrStore_Load() == DEVICE_STATUS_VERIFY_FAILED);
    Dev.learnNum = 1;
    Dev.learnCode[0].enable = 1;
    Dev.learnCode[0].cmd[0] = 0x30u;
    assert(IrStore_SaveIfChanged() == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    /* 新槽的头、学习码正文或最后 CRC 任一写入阶段掉电，都必须回退旧槽。 */
    for(write_stage = 1U; write_stage <= 3U; write_stage++) {
        memcpy(dataflash, snapshot, sizeof(snapshot));
        memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
        clear_failure();
        assert(IrStore_Load() == DEVICE_STATUS_OK);
        assert(Dev.learnCode[0].cmd[0] == 0x30u);

        Dev.learnCode[0].cmd[0] = 0x31u;
        fail_on(FAIL_WRITE, write_stage, 1);
        assert(IrStore_SaveIfChanged() == DEVICE_STATUS_IO_ERROR);
        memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
        clear_failure();
        assert(IrStore_Load() == DEVICE_STATUS_OK);
        assert(Dev.learnCode[0].cmd[0] == 0x30u);
    }
}

static void test_ir_single_slot_self_heals_and_read_error_never_writes(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(IrStore_Load() == DEVICE_STATUS_VERIFY_FAILED);
    Dev.learnNum = 1U;
    Dev.learnCode[0].enable = 1U;
    Dev.learnCode[0].cmd[0] = 0x5AU;
    assert(IrStore_SaveIfChanged() == DEVICE_STATUS_OK);

    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    Dev.learnNum = 0U;
    assert(IrStore_Load() == DEVICE_STATUS_OK);
    assert(Dev.learnCode[0].cmd[0] == 0x5AU);
    assert((Storage_GetStartupFlags() & STORAGE_STARTUP_RECOVERED) != 0U);

    /* 自愈后两槽都有效；单槽读取报错时使用另一槽，但不做任何擦写。 */
    memcpy(snapshot, dataflash, sizeof(snapshot));
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED); /* 清零启动标志。 */
    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    Dev.learnNum = 0U;
    fail_on(FAIL_READ, 1U, 0U);
    assert(IrStore_Load() == DEVICE_STATUS_IO_ERROR);
    assert(Dev.learnCode[0].cmd[0] == 0x5AU);
    assert((Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    Dev.learnCode[0].cmd[0] = 0x5BU;
    assert(IrStore_SaveIfChanged() == DEVICE_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void test_lora_parameter_read_error_is_visible_and_non_destructive(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(Config_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(LoraParams_Save(11U, 2U, 9U, 3U) == DEVICE_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    fail_on(FAIL_READ, 1U, 0U);
    assert(LoraParams_Load() == DEVICE_STATUS_IO_ERROR);
    assert(Dev.loraRegisterSf == LORA_SF_LISTEN);
    assert(Dev.loraListenSf == LORA_SF_SCAN);
    assert((Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    assert(LoraParams_Save(11U, 2U, 9U, 3U) == DEVICE_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void test_factory_reset_reports_partial_failure(void)
{
    connectivity_config_t connectivity;
    reset_flash();
    create_initial_config();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    memcpy(&connectivity, Connectivity_Get(), sizeof(connectivity));
    strcpy(connectivity.device_id, "A26094567");
    assert(Connectivity_Save(&connectivity) == DEVICE_STATUS_OK);
    assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(IrStore_Load() == DEVICE_STATUS_VERIFY_FAILED);
    Dev.nodeId = 0x4567u;
    assert(Config_Commit() == DEVICE_STATUS_OK);
    Dev.meter.fault_count = 88u;
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    Dev.learnNum = 1;
    Dev.learnCode[0].enable = 1;
    Dev.learnCode[0].cmd[0] = 0x30u;
    assert(IrStore_SaveIfChanged() == DEVICE_STATUS_OK);

    /* Runtime, IR and config erase failures all remain hardware faults until
     * the corresponding storage is recovered, not just an operation error. */
    for(uint8_t erase = 1U; erase <= 7U; erase += 2U) {
        fail_on(FAIL_ERASE, erase, 0);
        assert(Storage_FactoryReset() == DEVICE_STATUS_IO_ERROR);
        assert(Dev.errorCode.bit.flash);
        clear_failure();
    }

    clear_failure();
    assert(Storage_FactoryReset() == DEVICE_STATUS_OK);
    assert(!Dev.errorCode.bit.flash);
    assert(Config_Load() == DEVICE_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(Config_GetRevision() == 2u);
    assert(Dev.onOff == PowerOff);
    assert(Dev.ctlMode == Mode_Auto);
    assert(Dev.temSet == 25u);
    assert(Dev.wind == Wind_Auto);
    assert(Dev.lastPowerChange == 0u);
    assert(Dev.loraStatus == Status_Logining);
    assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(IrStore_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(LoraParams_Load() == DEVICE_STATUS_OK);
    assert(Connectivity_LoraEnabled());
    assert(!Connectivity_CellularEnabled());
    assert(strcmp(DeviceUid_Get(), "A2609BB01") == 0);
}

static void test_connectivity_round_trip(void)
{
    connectivity_config_t next;
    connectivity_config_t decoded;
    uint8_t wire[240];
    uint16_t length = 0U;

    reset_flash();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    memcpy(&next, Connectivity_Get(), sizeof(next));
    next.transport_mask = CONNECTIVITY_LORA | CONNECTIVITY_CELLULAR;
    next.mqtt_port = 1883U;
    next.mqtt_keepalive_sec = 120U;
    next.report_interval_sec = 30U;
    next.network_timeout_sec = 180U;
    next.cellular_pdp_type = CONNECTIVITY_PDP_IPV4V6;
    next.mqtt_qos = 1U;
    Dev.nodeId = 0x1002U;
    strcpy(next.mqtt_host, "broker.example.com");
    strcpy(next.apn, "iot");
    strcpy(next.device_id, "A26091002");
    assert(Connectivity_Save(&next) == DEVICE_STATUS_OK);
    assert(Connectivity_LoraEnabled());
    assert(Connectivity_CellularEnabled());
    assert(DeviceUid_Valid(DeviceUid_Get()));
    assert(strcmp(DeviceUid_Get(), "A26091002") == 0);
    Dev.nodeId = 0xABCDU;
    assert(strcmp(DeviceUid_Get(), "A2609ABCD") == 0);
    Dev.nodeId = 0x1002U;

    assert(Connectivity_Encode(wire, sizeof(wire), &length) == DEVICE_STATUS_OK);
    assert(length < sizeof(wire));
    memset(&decoded, 0, sizeof(decoded));
    assert(Connectivity_Decode(wire, length, &decoded) == DEVICE_STATUS_OK);
    assert(memcmp(&decoded, &next, sizeof(next)) == 0);

    wire[length++] = 0U; /* trailing bytes are rejected */
    assert(Connectivity_Decode(wire, length, &decoded) == DEVICE_STATUS_INVALID_ARG);
}

static void test_connectivity_v4_migrates_to_current_mqtt_defaults(void)
{
    connectivity_v4_record_t record;
    uint8_t crc_input[offsetof(connectivity_v4_record_t, crc32) +
                      sizeof(connectivity_v4_payload_t)];

    reset_flash();
    memset(&record, 0, sizeof(record));
    record.magic = 0x3154454EUL;
    record.schema_version = 4U;
    record.state = 0xA1U;
    record.generation = 7U;
    record.payload_length = sizeof(record.payload);
    record.payload.transport_mask = CONNECTIVITY_LORA | CONNECTIVITY_CELLULAR;
    record.payload.lora_register_sf = 9U;
    record.payload.lora_register_bw = 4U;
    record.payload.lora_listen_sf = 10U;
    record.payload.lora_listen_bw = 5U;
    record.payload.cellular_pdp_type = CONNECTIVITY_PDP_IPV4;
    record.payload.mqtt_port = 1883U;
    record.payload.mqtt_keepalive_sec = 90U;
    record.payload.report_interval_sec = 45U;
    record.payload.network_timeout_sec = 180U;
    strcpy(record.payload.mqtt_host, "106.15.11.119");
    strcpy(record.payload.apn, "CMIOT");
    strcpy(record.payload.device_id, "A26091002");
    memcpy(crc_input, &record, offsetof(connectivity_v4_record_t, crc32));
    memcpy(crc_input + offsetof(connectivity_v4_record_t, crc32),
           &record.payload, sizeof(record.payload));
    record.crc32 = Config_Crc32(crc_input, sizeof(crc_input));
    memcpy(dataflash + CONNECTIVITY_SLOT_A, &record, sizeof(record));

    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(strcmp(Connectivity_Get()->mqtt_host, "118.178.128.26") == 0);
    assert(strcmp(Connectivity_Get()->mqtt_username, "ecac") == 0);
    assert(strcmp(Connectivity_Get()->mqtt_password, "ecac2026") == 0);
    assert(strcmp(Connectivity_Get()->publish_topic, "pub/ac/{uid}") == 0);
    assert(strcmp(Connectivity_Get()->subscribe_topic, "sub/ac/{uid}") == 0);
    assert(strcmp(Connectivity_Get()->apn, "CMIOT") == 0);
    assert(strcmp(Connectivity_Get()->device_id, "A26091002") == 0);
    Dev.nodeId = 0x1002U;
    assert(strcmp(DeviceUid_Get(), "A26091002") == 0);
    assert(Connectivity_Get()->report_interval_sec == 45U);
    assert(Connectivity_Get()->network_timeout_sec == 180U);
    assert(dataflash[CONNECTIVITY_SLOT_B + 4U] == CONNECTIVITY_SCHEMA);
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
}

static void test_connectivity_v5_migration_preserves_both_old_pages(void)
{
    connectivity_config_t config;
    uint8_t old[256], pages[512], input[242];
    uint32_t magic = 0x3154454EUL, generation = 9U, crc;
    uint16_t length = offsetof(connectivity_config_t, management_topic);
    unsigned stage;
    reset_flash();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    config = *Connectivity_Get(); config.transport_mask = CONNECTIVITY_CELLULAR;
    strcpy(config.device_id, "FE1234"); strcpy(config.mqtt_host, "private.broker");
    strcpy(config.mqtt_username, "custom"); strcpy(config.mqtt_password, "secret");
    strcpy(config.publish_topic, "custom/u/{uid}"); strcpy(config.subscribe_topic, "custom/d/{uid}");
    memset(old, 0xFF, sizeof(old));
    memcpy(old, &magic, 4); old[4] = 5; old[5] = 0xA1;
    memcpy(old + 6, &generation, 4); memcpy(old + 10, &length, 2);
    memcpy(old + 16, &config, length);
    memcpy(input, old, 12); memcpy(input + 12, &config, length);
    crc = Config_Crc32(input, sizeof(input)); memcpy(old + 12, &crc, 4);
    memcpy(dataflash + 0x2100, old, sizeof(old));
    memcpy(pages, dataflash + CONNECTIVITY_SLOT_A, sizeof(pages));
    for(stage = 1; stage <= 2; stage++) {
        memset(dataflash + CONNECTIVITY_SLOT_B, 0xFF, 512); clear_failure();
        fail_on(FAIL_WRITE, stage, 1U);
        assert(Connectivity_Load() == DEVICE_STATUS_OK);
        assert(!memcmp(pages, dataflash + CONNECTIVITY_SLOT_A, sizeof(pages)));
        assert(strcmp(Connectivity_Get()->mqtt_host, "private.broker") == 0);
        assert(Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED);
    }
    for(stage = 1; stage <= 11; stage++) {
        memset(dataflash + CONNECTIVITY_SLOT_B, 0xFF, 512); clear_failure();
        fail_on(FAIL_READ, stage, 0U);
        assert(Connectivity_Load() != DEVICE_STATUS_VERIFY_FAILED);
        assert(!memcmp(pages, dataflash + CONNECTIVITY_SLOT_A, sizeof(pages)));
    }
    clear_failure();
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(!memcmp(pages, dataflash + CONNECTIVITY_SLOT_A, sizeof(pages)));
    assert(strcmp(Connectivity_Get()->mqtt_password, "secret") == 0);
    assert(strcmp(Connectivity_Get()->management_topic, "mag/ac/{uid}") == 0);
    assert(strcmp(Connectivity_Get()->publish_topic, "custom/u/{uid}") == 0);
    assert(dataflash[CONNECTIVITY_SLOT_B + 4] == 6);
    assert(Connectivity_Load() == DEVICE_STATUS_OK); /* now the new A peer may be rebuilt */
    config = *Connectivity_Get(); strcpy(config.management_topic, config.subscribe_topic);
    assert(Connectivity_Save(&config) == DEVICE_STATUS_INVALID_ARG);
    config = *Connectivity_Get(); strcpy(config.management_topic, "custom/m/{uid}");
    assert(Connectivity_Save(&config) == DEVICE_STATUS_OK);
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(strcmp(Connectivity_Get()->management_topic, "custom/m/{uid}") == 0);
}

static void test_connectivity_power_loss_keeps_previous_slot(void)
{
    connectivity_config_t next;

    reset_flash();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    memcpy(&next, Connectivity_Get(), sizeof(next));
    next.transport_mask |= CONNECTIVITY_CELLULAR;
    strcpy(next.mqtt_host, "broker.example.com");
    strcpy(next.device_id, "A26091003");
    assert(Connectivity_Save(&next) == DEVICE_STATUS_OK);

    next.mqtt_port = 1884U;
    fail_on(FAIL_WRITE, 1U, 1U);
    assert(Connectivity_Save(&next) == DEVICE_STATUS_IO_ERROR);
    clear_failure();
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(Connectivity_Get()->mqtt_port == 1883U);
    assert(strcmp(Connectivity_Get()->mqtt_host, "broker.example.com") == 0);
}

static void test_device_uid_bounds_are_required_for_cellular(void)
{
    connectivity_config_t next;
    reset_flash();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    memcpy(&next, Connectivity_Get(), sizeof(next));
    next.transport_mask |= CONNECTIVITY_CELLULAR;
    strcpy(next.mqtt_host, "broker.example.com");
    assert(Connectivity_Save(&next) == DEVICE_STATUS_INVALID_ARG);
    strcpy(next.device_id, "A26131001");
    assert(Connectivity_Save(&next) == DEVICE_STATUS_OK);
    strcpy(next.device_id, "A26091001");
    assert(Connectivity_Save(&next) == DEVICE_STATUS_OK);
}

static void test_custom_device_uid_prefix_recovery(void)
{
    const uint8_t prefix[] = {'B', 'A', 'F'};
    reset_flash();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    Dev.nodeId = 0x1421U;
    assert(DeviceUid_SavePrefix(prefix, sizeof(prefix)) == DEVICE_STATUS_OK);
    assert(strcmp(DeviceUid_Get(), "BAF1421") == 0);
    assert(DeviceUid_Valid(DeviceUid_Get()));
    Dev.nodeId = 0x00AFU;
    assert(strcmp(DeviceUid_Get(), "BAF00AF") == 0);
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(strcmp(DeviceUid_Get(), "BAF00AF") == 0);
    fail_on(FAIL_WRITE, 1, 1);
    assert(DeviceUid_SavePrefix((const uint8_t *)"AA", 2) != DEVICE_STATUS_OK);
    clear_failure();
    assert(Connectivity_Load() == DEVICE_STATUS_OK);
    assert(strcmp(DeviceUid_Get(), "BAF00AF") == 0);
    assert(DeviceUid_SavePrefix(0, 0) == DEVICE_STATUS_OK);
    assert(strcmp(DeviceUid_Get(), "00AF") == 0);
    assert(DeviceUid_SavePrefix((const uint8_t *)"123456", 6) == DEVICE_STATUS_INVALID_ARG);
    assert(DeviceUid_SavePrefix((const uint8_t *)"A\0B", 3) == DEVICE_STATUS_INVALID_ARG);
    assert(DeviceUid_SavePrefix((const uint8_t *)"A2613", 5) == DEVICE_STATUS_OK);
    assert(strcmp(DeviceUid_Get(), "A261300AF") == 0);
}

static void test_final_commit_marker_is_read_back(void)
{
    reset_flash(); create_initial_config();
    uint32_t revision = Config_GetRevision();
    Dev.nodeId = 0x1234U;
    silent_commit_marker = 1U;
    assert(Config_Commit() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Config_GetRevision() == revision && Dev.errorCode.bit.flash);
    silent_commit_marker = 0U;
    assert(Config_Load() == DEVICE_STATUS_OK && Dev.nodeId == Default_DevId);
    Dev.nodeId = 0x1234U;
    assert(Config_Commit() == DEVICE_STATUS_OK);
    assert(Config_Load() == DEVICE_STATUS_OK && Dev.nodeId == 0x1234U);
}

static void test_stale_runtime_backup_never_replaces_newer_main_log(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];
    fill_runtime_page(snapshot); restore_runtime_snapshot(snapshot);
    fail_backup_cleanup = 1U;
    for(uint16_t n = 65U; n <= 128U; n++) {
        Dev.meter.energy_wh = n;
        assert(Runtime_Append() == DEVICE_STATUS_OK);
        assert(Dev.errorCode.bit.flash);
    }
    assert(Storage_GetStartupFlags() & STORAGE_STARTUP_DEGRADED);
    assert(Runtime_Load() == DEVICE_STATUS_OK && Dev.meter.energy_wh == 128U);
    memcpy(snapshot, dataflash, sizeof(snapshot));
    Dev.meter.energy_wh = 129U;
    /* Failed backup refresh must leave the latest primary log untouched. */
    assert(Runtime_Append() != DEVICE_STATUS_OK);
    assert(!memcmp(snapshot + RUNTIME_PAGE, dataflash + RUNTIME_PAGE, EEPROM_BLOCK_SIZE));
    assert(Runtime_Load() == DEVICE_STATUS_OK && Dev.meter.energy_wh == 128U);
    fail_backup_cleanup = 0U;
    Dev.meter.energy_wh = 129U;
    fail_on(FAIL_WRITE, 3U, 1U); /* Fresh checkpoint committed, then main copy interrupted. */
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    clear_failure();
    assert(Runtime_Load() == DEVICE_STATUS_OK && Dev.meter.energy_wh == 129U);
    Dev.meter.energy_wh = 130U;
    assert(Runtime_Append() == DEVICE_STATUS_OK);
    assert(!Dev.errorCode.bit.flash);
}

static void test_connectivity_flash_fault_is_owned_until_its_recovery(void)
{
    reset_flash(); create_initial_config();
    assert(Connectivity_Load() == DEVICE_STATUS_VERIFY_FAILED);
    connectivity_config_t next = *Connectivity_Get();
    fail_on(FAIL_WRITE, 1U, 0U);
    assert(Connectivity_Save(&next) != DEVICE_STATUS_OK && Dev.errorCode.bit.flash);
    /* A normal periodic runtime save cannot clear a failed connectivity write. */
    Dev.onOff = PowerOff; Dev.temSet = 25U;
    assert(Runtime_Load() == DEVICE_STATUS_VERIFY_FAILED);
    assert(Runtime_Append() == DEVICE_STATUS_OK && Dev.errorCode.bit.flash);
    Storage_ReportFault(STORAGE_FAULT_OTA_METADATA, 1U);
    assert(Connectivity_Save(&next) == DEVICE_STATUS_OK && Dev.errorCode.bit.flash);
    Storage_ReportFault(STORAGE_FAULT_OTA_METADATA, 0U);
    assert(!Dev.errorCode.bit.flash);
    next.mqtt_port = 0U;
    assert(Connectivity_Save(&next) == DEVICE_STATUS_INVALID_ARG && !Dev.errorCode.bit.flash);
}

int main(void)
{
    test_final_commit_marker_is_read_back();
    test_stale_runtime_backup_never_replaces_newer_main_log();
    test_connectivity_flash_fault_is_owned_until_its_recovery();
    /* Extension remains atomic with the main config; legacy record loads too. */
    reset_flash();create_initial_config();
    Dev.rules[0].ctrl.enable=1;Dev.rules[0].ctrl.trig_type=TRIG_TIME;
    Dev.ruleSchedules[0]=(RULE_SCHEDULE_T){9708,9708,2,10};
    assert(Config_Commit()==DEVICE_STATUS_OK);
    memset(Dev.ruleSchedules,0,sizeof(Dev.ruleSchedules));
    assert(Config_Load()==DEVICE_STATUS_OK);
    assert(Dev.ruleSchedules[0].start_day==9708 && Dev.ruleSchedules[0].hysteresis_x10==10);
    {
        uint32_t crc;
        uint8_t legacy[188];
        memcpy(legacy,dataflash+CONFIG_SLOT_B,sizeof(legacy));
        legacy[4]=4;legacy[5]=0;legacy[10]=172;legacy[11]=0;
        legacy[16]=0x78;legacy[17]=0x56;
        uint8_t material[184];memcpy(material,legacy,12);memcpy(material+12,legacy+16,172);
        crc=Config_Crc32(material,sizeof(material));memcpy(legacy+12,&crc,4);
        memset(dataflash,0xFF,0x2000);memcpy(dataflash,legacy,sizeof(legacy));
        assert(Config_Load()==DEVICE_STATUS_OK);
        assert(Dev.nodeId==0x5678);
        assert(Dev.ruleSchedules[0].date_mode==0 && Dev.ruleSchedules[0].hysteresis_x10==0);
    }
    test_verified_commit_keeps_rejected_values_out_of_flash();
    test_runtime_uncommitted_slot_does_not_hide_later_records();
    test_config_slot_recovery();
    test_config_read_error_never_overwrites_flash();
    test_runtime_rollover_power_loss();
    test_runtime_read_error_is_visible_and_non_destructive();
    test_ir_double_slot_recovery();
    test_ir_single_slot_self_heals_and_read_error_never_writes();
    test_lora_parameter_read_error_is_visible_and_non_destructive();
    test_factory_reset_reports_partial_failure();
    test_connectivity_round_trip();
    test_connectivity_v4_migrates_to_current_mqtt_defaults();
    test_connectivity_v5_migration_preserves_both_old_pages();
    test_connectivity_power_loss_keeps_previous_slot();
    test_device_uid_bounds_are_required_for_cellular();
    test_custom_device_uid_prefix_recovery();
    puts("Config/runtime/IR/connectivity power-loss recovery: PASS");
    return 0;
}
