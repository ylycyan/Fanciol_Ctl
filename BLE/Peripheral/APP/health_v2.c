#include "health_v2.h"
#include "config_store_v2.h"
#include "CH58x_common.h"
#include <string.h>

#define HEALTH_MAGIC             0x324C5448UL
#define HEALTH_SLOTS             128u
#define HEALTH_BANK_COUNT        2u
#define HEALTH_SLOTS_PER_BANK    (HEALTH_SLOTS / HEALTH_BANK_COUNT)
#define HEALTH_BANK_SIZE         (V2_HEALTH_REGION_SIZE / HEALTH_BANK_COUNT)
#define HEALTH_FAST_DEADLINE_MS  500u
#define HEALTH_SLOW_DEADLINE_MS  2500u
#define HEALTH_REARM_DELAY_MS    30000u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    uint32_t timestamp;
    uint16_t fault_snapshot;
    uint8_t reset_reason;
    uint8_t consecutive_resets;
    uint8_t unhealthy_mask;
    uint8_t reserved[3];
    uint32_t crc32;
    uint8_t padding[4];
} health_record_v2_t;

typedef char health_records_must_fit_region[
    ((HEALTH_SLOTS * sizeof(health_record_v2_t)) == V2_HEALTH_REGION_SIZE) ? 1 : -1
];
typedef char health_banks_must_be_page_aligned[
    ((HEALTH_BANK_SIZE % EEPROM_PAGE_SIZE) == 0u) ? 1 : -1
];

extern uint32_t LocalTimestamp;
extern volatile uint32_t CurTick;

static uint32_t last_lora_ms;
static uint32_t last_ir_ms;
static uint32_t last_flash_ms;
static uint32_t last_periodic_ms;
static uint32_t last_ble_ms;
static uint32_t last_cellular_ms;
static uint32_t healthy_since_ms;
static uint32_t health_generation;
static uint16_t health_next_slot;
static uint8_t health_write_bank;
static uint8_t reported_mask;
static uint8_t consecutive_resets;
static uint8_t last_reset_reason;
static uint8_t last_unhealthy_mask;
static uint8_t fault_latched;
static uint8_t storage_error;
static uint8_t history_count;
static uint8_t recent_count;
static uint8_t recent_slots[HEALTH_V2_RECENT_LIMIT];

static uint32_t slot_address(uint16_t slot)
{
    return V2_HEALTH_PAGE + (uint32_t)slot * sizeof(health_record_v2_t);
}

static uint8_t valid(const health_record_v2_t *record)
{
    return record->magic == HEALTH_MAGIC &&
           record->crc32 == ConfigV2_Crc32((const uint8_t *)record,
                                           (uint16_t)(sizeof(*record) - 8u));
}

static uint8_t read_record(uint16_t slot, health_record_v2_t *record)
{
    if(slot >= HEALTH_SLOTS || !record) return 0;
    if(EEPROM_READ(slot_address(slot), record, sizeof(*record)) != 0u) {
        storage_error = 1u;
        return 0;
    }
    return 1;
}

static uint8_t erase_bank(uint8_t bank)
{
    uint32_t address;
    if(bank >= HEALTH_BANK_COUNT) return 0;
    address = V2_HEALTH_PAGE + (uint32_t)bank * HEALTH_BANK_SIZE;
    if(EEPROM_ERASE(address, HEALTH_BANK_SIZE) != 0u) {
        storage_error = 1u;
        return 0;
    }
    return 1;
}

static void rebuild_history_cache(void)
{
    health_record_v2_t record;
    uint32_t latest_generation = 0;
    uint16_t latest_slot = 0;
    uint16_t slot;
    uint16_t start;
    uint16_t end;
    int16_t cursor;
    uint8_t latest_found = 0;
    uint8_t latest_bank;
    uint8_t bank;

    history_count = 0;
    recent_count = 0;
    for(slot = 0; slot < HEALTH_SLOTS; ++slot) {
        if(!read_record(slot, &record) || !valid(&record)) continue;
        if(history_count < HEALTH_SLOTS) history_count++;
        if(!latest_found || (int32_t)(record.generation - latest_generation) > 0) {
            latest_found = 1;
            latest_generation = record.generation;
            latest_slot = slot;
        }
    }
    if(!latest_found) return;

    /*
     * 每个银行只顺序追加。最新银行倒序后再读取另一银行，即为时间倒序；
     * 仅缓存物理槽号，避免为诊断历史长期占用大量 RAM。
     */
    latest_bank = (uint8_t)(latest_slot / HEALTH_SLOTS_PER_BANK);
    start = (uint16_t)latest_bank * HEALTH_SLOTS_PER_BANK;
    for(cursor = (int16_t)latest_slot;
        cursor >= (int16_t)start && recent_count < HEALTH_V2_RECENT_LIMIT;
        --cursor) {
        if(read_record((uint16_t)cursor, &record) && valid(&record)) {
            recent_slots[recent_count++] = (uint8_t)cursor;
        }
    }

    bank = (uint8_t)(1u - latest_bank);
    start = (uint16_t)bank * HEALTH_SLOTS_PER_BANK;
    end = (uint16_t)(start + HEALTH_SLOTS_PER_BANK);
    for(cursor = (int16_t)(end - 1u);
        cursor >= (int16_t)start && recent_count < HEALTH_V2_RECENT_LIMIT;
        --cursor) {
        if(read_record((uint16_t)cursor, &record) && valid(&record)) {
            recent_slots[recent_count++] = (uint8_t)cursor;
        }
    }
}

static uint8_t find_latest(health_record_v2_t *best, uint16_t *latest_slot)
{
    health_record_v2_t record;
    uint32_t generation = 0;
    uint16_t slot;
    uint8_t found = 0;

    for(slot = 0; slot < HEALTH_SLOTS; ++slot) {
        if(!read_record(slot, &record) || !valid(&record)) continue;
        if(!found || (int32_t)(record.generation - generation) > 0) {
            *best = record;
            *latest_slot = slot;
            generation = record.generation;
            found = 1;
        }
    }
    return found;
}

static uint16_t find_free_after(uint16_t latest_slot)
{
    health_record_v2_t record;
    uint16_t bank_end;
    uint16_t slot;

    bank_end = (uint16_t)(((latest_slot / HEALTH_SLOTS_PER_BANK) + 1u) *
                          HEALTH_SLOTS_PER_BANK);
    for(slot = (uint16_t)(latest_slot + 1u); slot < bank_end; ++slot) {
        if(read_record(slot, &record) && record.magic == 0xFFFFFFFFUL) return slot;
    }
    return HEALTH_SLOTS;
}

static void remember_recent(uint16_t slot)
{
    uint8_t move_count = recent_count;
    if(move_count >= HEALTH_V2_RECENT_LIMIT) move_count = HEALTH_V2_RECENT_LIMIT - 1u;
    if(move_count) memmove(recent_slots + 1, recent_slots, move_count);
    recent_slots[0] = (uint8_t)slot;
    if(recent_count < HEALTH_V2_RECENT_LIMIT) recent_count++;
}

static uint8_t append_record(uint8_t reset_reason,
                             uint8_t unhealthy_mask,
                             uint16_t fault_snapshot)
{
    health_record_v2_t out;
    health_record_v2_t verify;
    uint16_t slot = health_next_slot;
    uint16_t bank_start = (uint16_t)health_write_bank * HEALTH_SLOTS_PER_BANK;
    uint16_t bank_end = (uint16_t)(bank_start + HEALTH_SLOTS_PER_BANK);

    /*
     * 故障路径绝不擦除 Flash。启动时会预先准备空银行；若本次启动内
     * 记录空间确实耗尽，则等待看门狗复位后再安全轮转。
     */
    if(slot < bank_start || slot >= bank_end) return 0;

    memset(&out, 0, sizeof(out));
    out.magic = HEALTH_MAGIC;
    out.generation = ++health_generation;
    out.timestamp = LocalTimestamp;
    out.fault_snapshot = fault_snapshot;
    out.reset_reason = reset_reason;
    out.consecutive_resets = consecutive_resets;
    out.unhealthy_mask = unhealthy_mask;
    out.crc32 = ConfigV2_Crc32((const uint8_t *)&out,
                               (uint16_t)(sizeof(out) - 8u));

    health_next_slot++;
    if(EEPROM_WRITE(slot_address(slot), &out, sizeof(out)) != 0u ||
       !read_record(slot, &verify) ||
       memcmp(&out, &verify, sizeof(out)) != 0 ||
       !valid(&verify)) {
        storage_error = 1u;
        return 0;
    }

    if(history_count < HEALTH_SLOTS) history_count++;
    remember_recent(slot);
    return 1;
}

static void prepare_write_bank(uint8_t found, uint16_t latest_slot)
{
    uint16_t next;

    if(!found) {
        if(!erase_bank(0u)) {
            health_next_slot = HEALTH_SLOTS;
            return;
        }
        health_write_bank = 0u;
        health_next_slot = 0u;
        return;
    }

    next = find_free_after(latest_slot);
    if(next < HEALTH_SLOTS) {
        health_write_bank = (uint8_t)(latest_slot / HEALTH_SLOTS_PER_BANK);
        health_next_slot = next;
        return;
    }

    /*
     * 当前银行仍完整保留最新记录，先擦另一银行再切换。任意时刻掉电，
     * 至少有一个银行含有效记录，不会在轮转边界丢失全部复位历史。
     */
    health_write_bank = (uint8_t)(1u - (latest_slot / HEALTH_SLOTS_PER_BANK));
    if(!erase_bank(health_write_bank)) {
        health_next_slot = HEALTH_SLOTS;
        return;
    }
    health_next_slot = (uint16_t)health_write_bank * HEALTH_SLOTS_PER_BANK;
}

static void prepare_fault_slot_after_boot(void)
{
    uint16_t bank_end = (uint16_t)(health_write_bank + 1u) *
                        HEALTH_SLOTS_PER_BANK;
    uint8_t next_bank;

    if(health_next_slot < bank_end) return;
    next_bank = (uint8_t)(1u - health_write_bank);
    if(!erase_bank(next_bank)) {
        health_next_slot = HEALTH_SLOTS;
        return;
    }
    health_write_bank = next_bank;
    health_next_slot = (uint16_t)next_bank * HEALTH_SLOTS_PER_BANK;
}

void HealthV2_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot)
{
    health_record_v2_t best = {0};
    uint16_t latest_slot = 0;
    uint8_t found;

    storage_error = 0;
    found = find_latest(&best, &latest_slot);
    last_reset_reason = boot_reset_reason;
    consecutive_resets =
        (last_reset_reason == RST_STATUS_WTR ||
         last_reset_reason == RST_STATUS_LRM1)
            ? (uint8_t)((found && best.consecutive_resets < 255u)
                            ? best.consecutive_resets + 1u
                            : 1u)
            : 0u;
    last_unhealthy_mask = found ? best.unhealthy_mask : 0u;
    health_generation = found ? best.generation : 0u;

    prepare_write_bank(found, latest_slot);
    rebuild_history_cache();
    append_record(last_reset_reason, last_unhealthy_mask, fault_snapshot);
    prepare_fault_slot_after_boot();
    rebuild_history_cache();

    last_lora_ms = CurTick;
    last_ir_ms = CurTick;
    last_flash_ms = CurTick;
    last_periodic_ms = CurTick;
    last_ble_ms = CurTick;
    last_cellular_ms = CurTick;
    healthy_since_ms = 0;
    reported_mask = 0;
    fault_latched = 0;
    PRINT("Reset reason=%u, consecutive watchdog resets=%u\r\n",
          last_reset_reason, consecutive_resets);
}

void HealthV2_Mark(uint8_t component)
{
    uint32_t now = CurTick;
    if(component & HEALTH_V2_LORA) last_lora_ms = now;
    if(component & HEALTH_V2_IR) last_ir_ms = now;
    if(component & HEALTH_V2_FLASH) last_flash_ms = now;
    if(component & HEALTH_V2_PERIODIC) last_periodic_ms = now;
    if(component & HEALTH_V2_BLE_STACK) last_ble_ms = now;
    if(component & HEALTH_V2_CELLULAR) last_cellular_ms = now;
}

uint8_t HealthV2_Tick100ms(uint16_t fault_snapshot)
{
    uint32_t now = CurTick;
    uint8_t mask = 0;

    if((uint32_t)(now - last_lora_ms) > HEALTH_FAST_DEADLINE_MS)
        mask |= HEALTH_V2_LORA;
    if((uint32_t)(now - last_ir_ms) > HEALTH_FAST_DEADLINE_MS)
        mask |= HEALTH_V2_IR;
    if((uint32_t)(now - last_flash_ms) > HEALTH_SLOW_DEADLINE_MS)
        mask |= HEALTH_V2_FLASH;
    if((uint32_t)(now - last_periodic_ms) > HEALTH_SLOW_DEADLINE_MS)
        mask |= HEALTH_V2_PERIODIC;
    if((uint32_t)(now - last_ble_ms) > HEALTH_FAST_DEADLINE_MS)
        mask |= HEALTH_V2_BLE_STACK;
    if((uint32_t)(now - last_cellular_ms) > HEALTH_FAST_DEADLINE_MS)
        mask |= HEALTH_V2_CELLULAR;

    if(mask) {
        healthy_since_ms = 0;
        if(mask != reported_mask) {
            PRINT("Health timeout mask=0x%02x, watchdog refresh stopped\r\n",
                  mask);
            reported_mask = mask;
        }
        if(!fault_latched) {
            last_unhealthy_mask = mask;
            append_record(0u, mask, fault_snapshot);
            fault_latched = 1u;
        }
    } else {
        reported_mask = 0;
        if(fault_latched) {
            if(!healthy_since_ms) healthy_since_ms = now;
            else if((uint32_t)(now - healthy_since_ms) >=
                    HEALTH_REARM_DELAY_MS) {
                fault_latched = 0;
                healthy_since_ms = 0;
            }
        }
    }
    return mask == 0u;
}

uint8_t HealthV2_ConsecutiveResets(void)
{
    return consecutive_resets;
}

uint8_t HealthV2_LastResetReason(void)
{
    return last_reset_reason;
}

uint8_t HealthV2_LastUnhealthyMask(void)
{
    return last_unhealthy_mask;
}

uint8_t HealthV2_StorageError(void)
{
    return storage_error;
}

uint8_t HealthV2_HistoryCount(void)
{
    return history_count;
}

uint8_t HealthV2_ReadRecent(uint8_t index, health_event_v2_t *event)
{
    health_record_v2_t record;
    if(!event || index >= recent_count) return 0;
    if(!read_record(recent_slots[index], &record) || !valid(&record)) {
        storage_error = 1u;
        return 0;
    }
    event->generation = record.generation;
    event->timestamp = record.timestamp;
    event->fault_snapshot = record.fault_snapshot;
    event->reset_reason = record.reset_reason;
    event->consecutive_resets = record.consecutive_resets;
    event->unhealthy_mask = record.unhealthy_mask;
    return 1;
}
