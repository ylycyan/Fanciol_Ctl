#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH58x_common.h"
#include "../BLE/Peripheral/APP/include/config_store_v2.h"
#include "../BLE/Peripheral/APP/include/health_v2.h"

#define DATAFLASH_SIZE 0x8000u

static uint8_t dataflash[DATAFLASH_SIZE];
static uint32_t fail_write_at;
static uint32_t fail_erase_at;
static uint32_t write_count;
static uint32_t erase_count;

uint32_t LocalTimestamp;
volatile uint32_t CurTick;

uint32_t ConfigV2_Crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t bit;
    for(i = 0; i < len; ++i) {
        crc ^= data[i];
        for(bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320UL : 0u);
        }
    }
    return ~crc;
}

uint32_t Test_EepromRead(uint32_t address, void *buffer, uint32_t length)
{
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    memcpy(buffer, dataflash + address, length);
    return 0;
}

uint32_t Test_EepromErase(uint32_t address, uint32_t length)
{
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    assert((address % EEPROM_PAGE_SIZE) == 0u);
    assert((length % EEPROM_PAGE_SIZE) == 0u);
    erase_count++;
    if(fail_erase_at && erase_count == fail_erase_at) return 1;
    memset(dataflash + address, 0xFF, length);
    return 0;
}

uint32_t Test_EepromWrite(uint32_t address, const void *buffer, uint32_t length)
{
    uint32_t i;
    const uint8_t *source = (const uint8_t *)buffer;
    assert(address <= DATAFLASH_SIZE);
    assert(length <= DATAFLASH_SIZE - address);
    write_count++;
    if(fail_write_at && write_count == fail_write_at) return 1;
    for(i = 0; i < length; ++i) dataflash[address + i] &= source[i];
    return 0;
}

uint32_t SYS_GetLastResetSta(void)
{
    /* 健康模块不应再依赖这个延迟读取值。 */
    return 0xEEu;
}

static void reset_fixture(void)
{
    memset(dataflash, 0xFF, sizeof(dataflash));
    fail_write_at = 0;
    fail_erase_at = 0;
    write_count = 0;
    erase_count = 0;
    LocalTimestamp = 1700000000u;
    CurTick = 0;
}

static health_event_v2_t recent(uint8_t index)
{
    health_event_v2_t event;
    memset(&event, 0, sizeof(event));
    assert(HealthV2_ReadRecent(index, &event));
    return event;
}

static void mark_all_healthy(void)
{
    HealthV2_Mark(HEALTH_V2_REQUIRED);
}

static void test_uses_earliest_reset_snapshot(void)
{
    health_event_v2_t event;
    reset_fixture();
    HealthV2_Init(0x37u, 0x1234u);
    assert(HealthV2_LastResetReason() == 0x37u);
    assert(HealthV2_ConsecutiveResets() == 0u);
    assert(HealthV2_HistoryCount() == 1u);
    event = recent(0);
    assert(event.reset_reason == 0x37u);
    assert(event.fault_snapshot == 0x1234u);
    assert(event.timestamp == 1700000000u);
}

static void test_watchdog_fault_snapshot_and_rearm(void)
{
    health_event_v2_t event;
    reset_fixture();
    HealthV2_Init(1u, 0u);
    CurTick = 600u;
    assert(!HealthV2_Tick100ms(0x55AAu));
    assert(HealthV2_HistoryCount() == 2u);
    event = recent(0);
    assert(event.reset_reason == 0u);
    assert(event.fault_snapshot == 0x55AAu);
    assert((event.unhealthy_mask & (HEALTH_V2_LORA |
                                    HEALTH_V2_IR |
                                    HEALTH_V2_BLE_STACK)) != 0u);

    /* 同一故障不重复写；连续健康 30 秒后才允许记录下一次故障。 */
    CurTick = 700u;
    assert(!HealthV2_Tick100ms(0xAAAAu));
    assert(HealthV2_HistoryCount() == 2u);
    mark_all_healthy();
    assert(HealthV2_Tick100ms(0u));
    CurTick += 30000u;
    mark_all_healthy();
    assert(HealthV2_Tick100ms(0u));
    CurTick += 600u;
    assert(!HealthV2_Tick100ms(0xBEEFu));
    assert(HealthV2_HistoryCount() == 3u);
    assert(recent(0).fault_snapshot == 0xBEEFu);
}

static void test_watchdog_reset_counter_persists(void)
{
    reset_fixture();
    HealthV2_Init(RST_STATUS_WTR, 0u);
    assert(HealthV2_ConsecutiveResets() == 1u);
    LocalTimestamp++;
    HealthV2_Init(RST_STATUS_WTR, 0u);
    assert(HealthV2_ConsecutiveResets() == 2u);
    LocalTimestamp++;
    HealthV2_Init(1u, 0u);
    assert(HealthV2_ConsecutiveResets() == 0u);
}

static void test_dual_bank_rollover_keeps_recent_history(void)
{
    uint16_t reboot;
    health_event_v2_t newest;
    health_event_v2_t previous;
    reset_fixture();
    for(reboot = 0; reboot < 140u; ++reboot) {
        LocalTimestamp = 1700000000u + reboot;
        HealthV2_Init(1u, reboot);
        assert(!HealthV2_StorageError());
    }
    /*
     * 轮转会回收较旧银行，但永远不会像整区擦除那样瞬间丢光历史。
     * BLE 只缓存最近 32 条，足够现场导出且 RAM 开销固定。
     */
    assert(HealthV2_HistoryCount() >= 64u);
    newest = recent(0);
    previous = recent(1);
    assert(newest.fault_snapshot == 139u);
    assert(previous.fault_snapshot == 138u);
    assert(newest.generation > previous.generation);
}

static void test_bank_switch_power_loss_keeps_last_valid_record(void)
{
    uint16_t reboot;
    reset_fixture();

    /* 填满第一银行，模拟切换后第一条记录写入失败。 */
    for(reboot = 0; reboot < 64u; ++reboot) {
        LocalTimestamp = 1700000000u + reboot;
        HealthV2_Init(1u, reboot);
    }
    fail_write_at = write_count + 1u;
    HealthV2_Init(1u, 64u);
    assert(HealthV2_StorageError());
    fail_write_at = 0u;
    HealthV2_Init(1u, 65u);
    assert(!HealthV2_StorageError());
    assert(recent(0).fault_snapshot == 65u);
    assert(recent(1).fault_snapshot == 63u);

    /*
     * 填满第二银行，模拟擦除旧银行时掉电。最新银行没有被擦，
     * 下次启动应继续从 generation 最大的记录恢复。
     */
    for(reboot = 66u; reboot < 128u; ++reboot) {
        LocalTimestamp = 1700000000u + reboot;
        HealthV2_Init(1u, reboot);
    }
    fail_erase_at = erase_count + 1u;
    HealthV2_Init(1u, 128u);
    assert(HealthV2_StorageError());
    assert(recent(0).fault_snapshot == 128u);
    fail_erase_at = 0u;
    HealthV2_Init(1u, 129u);
    assert(!HealthV2_StorageError());
    assert(recent(0).fault_snapshot == 129u);
    assert(recent(1).fault_snapshot == 128u);
}

static void test_storage_failure_is_visible(void)
{
    reset_fixture();
    fail_write_at = 1u;
    HealthV2_Init(1u, 0u);
    assert(HealthV2_StorageError());

    reset_fixture();
    memset(dataflash + V2_HEALTH_PAGE, 0x00, V2_HEALTH_REGION_SIZE);
    fail_erase_at = 1u;
    HealthV2_Init(1u, 0u);
    assert(HealthV2_StorageError());
}

int main(void)
{
    test_uses_earliest_reset_snapshot();
    test_watchdog_fault_snapshot_and_rearm();
    test_watchdog_reset_counter_persists();
    test_dual_bank_rollover_keeps_recent_history();
    test_bank_switch_power_loss_keeps_last_valid_record();
    test_storage_failure_is_visible();
    puts("health_v2 tests passed");
    return 0;
}
