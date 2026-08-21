#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH58x_common.h"
#include "../BLE/Peripheral/APP/include/config_store.h"
#include "../BLE/Peripheral/APP/include/health.h"

#define DATAFLASH_SIZE 0x8000u

static uint8_t dataflash[DATAFLASH_SIZE];
static uint32_t fail_write_at;
static uint32_t fail_erase_at;
static uint32_t write_count;
static uint32_t erase_count;

uint32_t LocalTimestamp;
volatile uint32_t CurTick;

uint32_t Config_Crc32(const uint8_t *data, uint16_t len)
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

static void mark_all_healthy(void)
{
    Health_Mark(HEALTH_REQUIRED);
}

static void test_uses_earliest_reset_snapshot(void)
{
    reset_fixture();
    Health_Init(0x37u, 0x1234u);
    assert(Health_LastResetReason() == 0x37u);
    assert(Health_ConsecutiveResets() == 0u);
    assert(!Health_StorageError());
}

static void test_watchdog_fault_snapshot_and_rearm(void)
{
    reset_fixture();
    Health_Init(1u, 0u);
    CurTick = 600u;
    assert(!Health_Tick100ms(0x55AAu));
    assert((Health_LastUnhealthyMask() & (HEALTH_LORA |
                                         HEALTH_IR |
                                         HEALTH_BLE_STACK)) != 0u);

    /* 同一故障不重复写；连续健康 30 秒后才允许记录下一次故障。 */
    {
        uint32_t writes_after_fault = write_count;
    CurTick = 700u;
    assert(!Health_Tick100ms(0xAAAAu));
        assert(write_count == writes_after_fault);
    }
    mark_all_healthy();
    assert(Health_Tick100ms(0u));
    CurTick += 30000u;
    mark_all_healthy();
    assert(Health_Tick100ms(0u));
    CurTick += 600u;
    assert(!Health_Tick100ms(0xBEEFu));
    assert(!Health_StorageError());
}

static void test_watchdog_reset_counter_persists(void)
{
    reset_fixture();
    Health_Init(RST_STATUS_WTR, 0u);
    assert(Health_ConsecutiveResets() == 1u);
    LocalTimestamp++;
    Health_Init(RST_STATUS_WTR, 0u);
    assert(Health_ConsecutiveResets() == 2u);
    LocalTimestamp++;
    Health_Init(1u, 0u);
    assert(Health_ConsecutiveResets() == 0u);
}

static void test_alternate_slot_write_failure_keeps_previous_record(void)
{
    reset_fixture();
    Health_Init(RST_STATUS_WTR, 1u);
    fail_write_at = write_count + 1u;
    Health_Init(RST_STATUS_WTR, 2u);
    assert(Health_StorageError());
    fail_write_at = 0u;
    Health_Init(RST_STATUS_WTR, 3u);
    assert(!Health_StorageError());
    assert(Health_ConsecutiveResets() == 2u);
}

static void test_storage_failure_is_visible(void)
{
    reset_fixture();
    fail_write_at = 1u;
    Health_Init(1u, 0u);
    assert(Health_StorageError());

    reset_fixture();
    fail_erase_at = 1u;
    Health_Init(1u, 0u);
    assert(Health_StorageError());
}

int main(void)
{
    test_uses_earliest_reset_snapshot();
    test_watchdog_fault_snapshot_and_rearm();
    test_watchdog_reset_counter_persists();
    test_alternate_slot_write_failure_keeps_previous_record();
    test_storage_failure_is_visible();
    puts("health tests passed");
    return 0;
}
