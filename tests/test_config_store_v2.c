#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "../BLE/Peripheral/APP/include/config_store_v2.h"
#include "../BLE/Peripheral/APP/include/protocol_v2.h"

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
static uint8_t dataflash[DATAFLASH_SIZE];
static failure_t failure;

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
    memset(dataflash, 0xFF, sizeof(dataflash));
    memset(&Dev, 0, sizeof(Dev));
    clear_failure();
}

static void create_initial_config(void)
{
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(Dev.nodeId == Default_DevId);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_EMPTY);
    assert(ConfigV2_InitializeDefaults(0u) == V2_STATUS_OK);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_INITIALIZED);
    assert(ConfigV2_GetRevision() == 2u);
}

static void test_config_slot_recovery(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    create_initial_config();
    memcpy(snapshot, dataflash, sizeof(snapshot));

    Dev.nodeId = 0x2345u;
    fail_on(FAIL_WRITE, 1, 1);
    assert(ConfigV2_Commit(2u) == V2_STATUS_IO_ERROR);
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(ConfigV2_GetRevision() == 3u);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_REPAIRED);

    memcpy(dataflash, snapshot, sizeof(snapshot));
    assert(ConfigV2_Load() == V2_STATUS_OK);
    Dev.nodeId = 0x2345u;
    fail_on(FAIL_READ, 1, 0);
    assert(ConfigV2_Commit(2u) == V2_STATUS_VERIFY_FAILED);
    clear_failure();
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(Dev.nodeId == 0x2345u);
    assert(ConfigV2_GetRevision() == 3u);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_HEALTHY);

    /*
     * generation 也受 CRC 保护；最新槽头损坏时回退上一代并立即重建冗余，
     * 不能只校验 payload 后误选一个被翻转成“大版本”的记录。
     */
    dataflash[V2_CONFIG_SLOT_A + 6u] ^= 0x40u;
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(ConfigV2_GetRevision() == 3u);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_REPAIRED);
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_HEALTHY);

    /* 两槽同时损坏才恢复 RAM 默认值，并明确留下 CORRUPT 状态。 */
    dataflash[V2_CONFIG_SLOT_A + 20u] ^= 0xA5u;
    dataflash[V2_CONFIG_SLOT_B + 20u] ^= 0x5Au;
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(Dev.nodeId == Default_DevId);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_CORRUPT);
    assert(ConfigV2_InitializeDefaults(1u) == V2_STATUS_OK);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_DEFAULTS_RECOVERED);
    assert(ConfigV2_GetRevision() == 2u);
}

static void test_config_read_error_never_overwrites_flash(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    create_initial_config();
    Dev.nodeId = 0x4567u;
    assert(ConfigV2_Commit(2u) == V2_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    /* A 是最新槽；读取 A 失败时可临时使用 B，但绝不能擦写任何一个槽。 */
    fail_on(FAIL_READ, 1u, 0u);
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_IO_ERROR);
    assert(StorageV2_GetStartupFlags() == STORAGE_V2_STARTUP_DEGRADED);
    assert(Dev.nodeId == Default_DevId);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);

    clear_failure();
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_HEALTHY);
    assert(Dev.nodeId == 0x4567u);
    assert(ConfigV2_GetRevision() == 3u);

    /* 全空白且读取失败时同样只使用 RAM 默认值，不得“初始化”并覆盖现场数据。 */
    reset_flash();
    memcpy(snapshot, dataflash, sizeof(snapshot));
    fail_on(FAIL_READ, 1u, 0u);
    assert(ConfigV2_Load() == V2_STATUS_IO_ERROR);
    assert(ConfigV2_GetLoadState() == CONFIG_V2_LOAD_IO_ERROR);
    assert(StorageV2_GetStartupFlags() == STORAGE_V2_STARTUP_DEGRADED);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void fill_runtime_page(uint8_t *snapshot)
{
    uint32_t i;
    reset_flash();
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED); /* 同时清零启动存储标志。 */
    assert(RuntimeV2_Load() == V2_STATUS_VERIFY_FAILED);
    for(i = 1; i <= 64u; ++i) {
        Dev.meter.run_minutes = i;
        Dev.runTime = (uint16_t)i;
        Dev.lastPowerChange = 1000u + i;
        Dev.onOff = (i & 1u) ? PowerOn : PowerOff;
        Dev.ctlMode = (Mode_t)(i % 5u);
        Dev.temSet = (uint8_t)(16u + (i % 16u));
        Dev.wind = (Wind_t)(i % 4u);
        assert(RuntimeV2_Append() == V2_STATUS_OK);
    }
    memcpy(snapshot, dataflash, DATAFLASH_SIZE);
}

static void test_runtime_read_error_is_visible_and_non_destructive(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(RuntimeV2_Load() == V2_STATUS_VERIFY_FAILED);
    Dev.meter.run_minutes = 17U;
    Dev.runTime = 17U;
    Dev.lastPowerChange = 123456U;
    Dev.onOff = PowerOn;
    Dev.ctlMode = Mode_Cool;
    Dev.temSet = 23U;
    Dev.wind = Wind_High;
    assert(RuntimeV2_Append() == V2_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    memset(&Dev.meter, 0, sizeof(Dev.meter));
    Dev.runTime = 0U;
    Dev.lastPowerChange = 0U;
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 25U;
    Dev.wind = Wind_Auto;
    /* 读取主日志有效记录、随后空槽成功，第三次读取检查点失败。 */
    fail_on(FAIL_READ, 3U, 0U);
    assert(RuntimeV2_Load() == V2_STATUS_IO_ERROR);
    assert(Dev.meter.run_minutes == 17U);
    assert(Dev.runTime == 17U);
    assert(Dev.lastPowerChange == 123456U);
    assert(Dev.onOff == PowerOn);
    assert(Dev.ctlMode == Mode_Cool);
    assert(Dev.temSet == 23U);
    assert(Dev.wind == Wind_High);
    assert((StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    Dev.meter.run_minutes = 18U;
    assert(RuntimeV2_Append() == V2_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void restore_runtime_snapshot(const uint8_t *snapshot)
{
    memcpy(dataflash, snapshot, DATAFLASH_SIZE);
    memset(&Dev, 0, sizeof(Dev));
    clear_failure();
    assert(RuntimeV2_Load() == V2_STATUS_OK);
    assert(Dev.meter.run_minutes == 64u);
    assert(Dev.lastPowerChange == 1064u);
    assert(Dev.onOff == PowerOff);
    assert(Dev.ctlMode == (Mode_t)(64u % 5u));
    assert(Dev.temSet == (uint8_t)(16u + (64u % 16u)));
    assert(Dev.wind == Wind_Auto);
    Dev.meter.run_minutes = 65u;
    Dev.runTime = 65u;
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
    assert(RuntimeV2_Load() == V2_STATUS_OK);
    assert(Dev.meter.run_minutes == expected);
    assert(Dev.runTime == expected);
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
    assert(RuntimeV2_Append() == V2_STATUS_VERIFY_FAILED);
    assert_runtime_recovers_64_or_65(64u);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_ERASE, 2, 1); /* 检查点完成，主日志擦除中掉电。 */
    assert(RuntimeV2_Append() == V2_STATUS_IO_ERROR);
    assert_runtime_recovers_64_or_65(65u);

    restore_runtime_snapshot(snapshot);
    fail_on(FAIL_WRITE, 2, 1); /* 主日志首条写一半，检查点仍完整。 */
    assert(RuntimeV2_Append() == V2_STATUS_IO_ERROR);
    assert_runtime_recovers_64_or_65(65u);

    restore_runtime_snapshot(snapshot);
    assert(RuntimeV2_Append() == V2_STATUS_OK);
    assert_runtime_recovers_64_or_65(65u);
    assert(dataflash[V2_RUNTIME_BACKUP_PAGE] == 0xFFu);
}

static void test_ir_double_slot_recovery(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];
    uint32_t write_stage;

    reset_flash();
    assert(IrStoreV2_Load() == V2_STATUS_VERIFY_FAILED);
    Dev.learnNum = 1;
    Dev.learnCode[0].enable = 1;
    Dev.learnCode[0].cmd[0] = 0x30u;
    assert(IrStoreV2_SaveIfChanged() == V2_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    /* 新槽的头、学习码正文或最后 CRC 任一写入阶段掉电，都必须回退旧槽。 */
    for(write_stage = 1U; write_stage <= 3U; write_stage++) {
        memcpy(dataflash, snapshot, sizeof(snapshot));
        memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
        clear_failure();
        assert(IrStoreV2_Load() == V2_STATUS_OK);
        assert(Dev.learnCode[0].cmd[0] == 0x30u);

        Dev.learnCode[0].cmd[0] = 0x31u;
        fail_on(FAIL_WRITE, write_stage, 1);
        assert(IrStoreV2_SaveIfChanged() == V2_STATUS_IO_ERROR);
        memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
        clear_failure();
        assert(IrStoreV2_Load() == V2_STATUS_OK);
        assert(Dev.learnCode[0].cmd[0] == 0x30u);
    }
}

static void test_ir_single_slot_self_heals_and_read_error_never_writes(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(IrStoreV2_Load() == V2_STATUS_VERIFY_FAILED);
    Dev.learnNum = 1U;
    Dev.learnCode[0].enable = 1U;
    Dev.learnCode[0].cmd[0] = 0x5AU;
    assert(IrStoreV2_SaveIfChanged() == V2_STATUS_OK);

    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    Dev.learnNum = 0U;
    assert(IrStoreV2_Load() == V2_STATUS_OK);
    assert(Dev.learnCode[0].cmd[0] == 0x5AU);
    assert((StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_RECOVERED) != 0U);

    /* 自愈后两槽都有效；单槽读取报错时使用另一槽，但不做任何擦写。 */
    memcpy(snapshot, dataflash, sizeof(snapshot));
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED); /* 清零启动标志。 */
    memset(Dev.learnCode, 0, sizeof(Dev.learnCode));
    Dev.learnNum = 0U;
    fail_on(FAIL_READ, 1U, 0U);
    assert(IrStoreV2_Load() == V2_STATUS_IO_ERROR);
    assert(Dev.learnCode[0].cmd[0] == 0x5AU);
    assert((StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    Dev.learnCode[0].cmd[0] = 0x5BU;
    assert(IrStoreV2_SaveIfChanged() == V2_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void test_lora_parameter_read_error_is_visible_and_non_destructive(void)
{
    uint8_t snapshot[DATAFLASH_SIZE];

    reset_flash();
    assert(ConfigV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(LoraParamsV2_Save(11U, 2U, 9U, 3U) == V2_STATUS_OK);
    memcpy(snapshot, dataflash, sizeof(snapshot));

    fail_on(FAIL_READ, 1U, 0U);
    assert(LoraParamsV2_Load() == V2_STATUS_IO_ERROR);
    assert(Dev.loraRegisterSf == LORA_SF_LISTEN);
    assert(Dev.loraListenSf == LORA_SF_SCAN);
    assert((StorageV2_GetStartupFlags() & STORAGE_V2_STARTUP_DEGRADED) != 0U);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
    clear_failure();
    assert(LoraParamsV2_Save(11U, 2U, 9U, 3U) == V2_STATUS_IO_ERROR);
    assert(memcmp(snapshot, dataflash, sizeof(snapshot)) == 0);
}

static void test_factory_reset_reports_partial_failure(void)
{
    reset_flash();
    create_initial_config();
    assert(RuntimeV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(IrStoreV2_Load() == V2_STATUS_VERIFY_FAILED);
    Dev.nodeId = 0x4567u;
    assert(ConfigV2_Commit(2u) == V2_STATUS_OK);
    Dev.meter.run_minutes = 88u;
    Dev.runTime = 88u;
    assert(RuntimeV2_Append() == V2_STATUS_OK);
    Dev.learnNum = 1;
    Dev.learnCode[0].enable = 1;
    Dev.learnCode[0].cmd[0] = 0x30u;
    assert(IrStoreV2_SaveIfChanged() == V2_STATUS_OK);

    fail_on(FAIL_ERASE, 3, 0);
    assert(StorageV2_FactoryReset() == V2_STATUS_IO_ERROR);

    clear_failure();
    assert(StorageV2_FactoryReset() == V2_STATUS_OK);
    assert(ConfigV2_Load() == V2_STATUS_OK);
    assert(Dev.nodeId == Default_DevId);
    assert(ConfigV2_GetRevision() == 2u);
    assert(Dev.onOff == PowerOff);
    assert(Dev.ctlMode == Mode_Auto);
    assert(Dev.temSet == 25u);
    assert(Dev.wind == Wind_Auto);
    assert(Dev.lastPowerChange == 0u);
    assert(Dev.loraStatus == Status_Logining);
    assert(RuntimeV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(IrStoreV2_Load() == V2_STATUS_VERIFY_FAILED);
    assert(LoraParamsV2_Load() == V2_STATUS_VERIFY_FAILED);
}

static void test_legacy_lora_defaults_migrate_without_overwriting_custom_profile(void)
{
    reset_flash();
    assert(LoraParamsV2_Save(10u, 4u, 8u, 10u) == V2_STATUS_OK);
    memset(&Dev, 0, sizeof(Dev));
    assert(LoraParamsV2_Load() == V2_STATUS_OK);
    assert(Dev.loraRegisterSf == LORA_SF_LISTEN);
    assert(Dev.loraRegisterBw == LORA_BW_LISTEN);
    assert(Dev.loraListenSf == LORA_SF_SCAN);
    assert(Dev.loraListenBw == LORA_BW_SCAN);

    assert(LoraParamsV2_Save(11u, 2u, 9u, 3u) == V2_STATUS_OK);
    memset(&Dev, 0, sizeof(Dev));
    assert(LoraParamsV2_Load() == V2_STATUS_OK);
    assert(Dev.loraRegisterSf == 11u);
    assert(Dev.loraRegisterBw == 2u);
    assert(Dev.loraListenSf == 9u);
    assert(Dev.loraListenBw == 3u);
}

int main(void)
{
    test_config_slot_recovery();
    test_config_read_error_never_overwrites_flash();
    test_runtime_rollover_power_loss();
    test_runtime_read_error_is_visible_and_non_destructive();
    test_ir_double_slot_recovery();
    test_ir_single_slot_self_heals_and_read_error_never_writes();
    test_lora_parameter_read_error_is_visible_and_non_destructive();
    test_factory_reset_reports_partial_failure();
    test_legacy_lora_defaults_migrate_without_overwriting_custom_profile();
    puts("Config/runtime/IR power-loss recovery: PASS");
    return 0;
}
