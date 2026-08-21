#include "health.h"
#include "config_store.h"
#include "CH58x_common.h"

#define HEALTH_MAGIC             0x314C5448UL
#define HEALTH_SLOT_A            HEALTH_STORAGE_PAGE
#define HEALTH_SLOT_B            (HEALTH_STORAGE_PAGE + EEPROM_PAGE_SIZE)
#define HEALTH_FAST_DEADLINE_MS  500U
#define HEALTH_SLOW_DEADLINE_MS  2500U
#define HEALTH_REARM_DELAY_MS    30000UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t generation;
    uint16_t fault_snapshot;
    uint8_t reset_reason;
    uint8_t consecutive_resets;
    uint8_t unhealthy_mask;
    uint8_t reserved[3];
    uint32_t crc32;
} health_record_t;

extern volatile uint32_t CurTick;

static uint32_t last_lora_ms;
static uint32_t last_ir_ms;
static uint32_t last_flash_ms;
static uint32_t last_periodic_ms;
static uint32_t last_ble_ms;
static uint32_t last_cellular_ms;
static uint32_t healthy_since_ms;
static uint32_t record_generation;
static uint32_t active_slot;
static uint8_t reported_mask;
static uint8_t consecutive_resets;
static uint8_t last_reset_reason;
static uint8_t last_unhealthy_mask;
static uint8_t fault_latched;
static uint8_t storage_error;

static uint8_t record_valid(const health_record_t *record)
{
    return record->magic == HEALTH_MAGIC &&
           record->crc32 == Config_Crc32((const uint8_t *)record,
                                          (uint16_t)(sizeof(*record) - sizeof(record->crc32)));
}

static uint8_t read_record(uint32_t address, health_record_t *record)
{
    if(EEPROM_READ(address, record, sizeof(*record)) != 0U) {
        storage_error = 1U;
        return 0U;
    }
    return record_valid(record);
}

static void save_record(uint8_t reset_reason, uint8_t unhealthy_mask,
                        uint16_t fault_snapshot)
{
    health_record_t record;
    health_record_t verify;
    uint32_t target = active_slot == HEALTH_SLOT_A ? HEALTH_SLOT_B : HEALTH_SLOT_A;

    record.magic = HEALTH_MAGIC;
    record.generation = ++record_generation;
    record.fault_snapshot = fault_snapshot;
    record.reset_reason = reset_reason;
    record.consecutive_resets = consecutive_resets;
    record.unhealthy_mask = unhealthy_mask;
    record.reserved[0] = 0U;
    record.reserved[1] = 0U;
    record.reserved[2] = 0U;
    record.crc32 = Config_Crc32((const uint8_t *)&record,
                                (uint16_t)(sizeof(record) - sizeof(record.crc32)));
    if(EEPROM_ERASE(target, EEPROM_PAGE_SIZE) != 0U ||
       EEPROM_WRITE(target, &record, sizeof(record)) != 0U ||
       EEPROM_READ(target, &verify, sizeof(verify)) != 0U ||
       !record_valid(&verify) || verify.generation != record.generation) {
        storage_error = 1U;
        return;
    }
    active_slot = target;
}

void Health_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot)
{
    health_record_t first;
    health_record_t second;
    health_record_t latest;
    uint8_t first_valid;
    uint8_t second_valid;

    storage_error = 0U;
    first_valid = read_record(HEALTH_SLOT_A, &first);
    second_valid = read_record(HEALTH_SLOT_B, &second);
    if(first_valid && (!second_valid || (int32_t)(first.generation - second.generation) > 0)) {
        latest = first;
        active_slot = HEALTH_SLOT_A;
    } else if(second_valid) {
        latest = second;
        active_slot = HEALTH_SLOT_B;
    } else {
        latest.generation = 0U;
        latest.consecutive_resets = 0U;
        latest.unhealthy_mask = 0U;
        active_slot = HEALTH_SLOT_B;
    }

    record_generation = latest.generation;
    last_reset_reason = boot_reset_reason;
    consecutive_resets =
        (boot_reset_reason == RST_STATUS_WTR || boot_reset_reason == RST_STATUS_LRM1)
        ? (uint8_t)(latest.consecutive_resets < 255U ? latest.consecutive_resets + 1U : 255U)
        : 0U;
    last_unhealthy_mask = latest.unhealthy_mask;
    save_record(last_reset_reason, last_unhealthy_mask, fault_snapshot);

    last_lora_ms = CurTick;
    last_ir_ms = CurTick;
    last_flash_ms = CurTick;
    last_periodic_ms = CurTick;
    last_ble_ms = CurTick;
    last_cellular_ms = CurTick;
    healthy_since_ms = 0U;
    reported_mask = 0U;
    fault_latched = 0U;
    PRINT("Reset reason=%u, consecutive watchdog resets=%u\r\n",
          last_reset_reason, consecutive_resets);
}

void Health_Mark(uint8_t component)
{
    uint32_t now = CurTick;
    if(component & HEALTH_LORA) last_lora_ms = now;
    if(component & HEALTH_IR) last_ir_ms = now;
    if(component & HEALTH_FLASH) last_flash_ms = now;
    if(component & HEALTH_PERIODIC) last_periodic_ms = now;
    if(component & HEALTH_BLE_STACK) last_ble_ms = now;
    if(component & HEALTH_CELLULAR) last_cellular_ms = now;
}

uint8_t Health_Tick100ms(uint16_t fault_snapshot)
{
    uint32_t now = CurTick;
    uint8_t mask = 0U;

    if((uint32_t)(now - last_lora_ms) > HEALTH_FAST_DEADLINE_MS) mask |= HEALTH_LORA;
    if((uint32_t)(now - last_ir_ms) > HEALTH_FAST_DEADLINE_MS) mask |= HEALTH_IR;
    if((uint32_t)(now - last_flash_ms) > HEALTH_SLOW_DEADLINE_MS) mask |= HEALTH_FLASH;
    if((uint32_t)(now - last_periodic_ms) > HEALTH_SLOW_DEADLINE_MS) mask |= HEALTH_PERIODIC;
    if((uint32_t)(now - last_ble_ms) > HEALTH_FAST_DEADLINE_MS) mask |= HEALTH_BLE_STACK;
    if((uint32_t)(now - last_cellular_ms) > HEALTH_FAST_DEADLINE_MS) mask |= HEALTH_CELLULAR;

    if(mask != 0U) {
        healthy_since_ms = 0U;
        if(mask != reported_mask) {
            PRINT("Health timeout mask=0x%02x, watchdog refresh stopped\r\n", mask);
            reported_mask = mask;
        }
        if(!fault_latched) {
            last_unhealthy_mask = mask;
            save_record(0U, mask, fault_snapshot);
            fault_latched = 1U;
        }
    } else {
        reported_mask = 0U;
        if(fault_latched) {
            if(healthy_since_ms == 0U) healthy_since_ms = now;
            else if((uint32_t)(now - healthy_since_ms) >= HEALTH_REARM_DELAY_MS) {
                fault_latched = 0U;
                healthy_since_ms = 0U;
            }
        }
    }
    return mask == 0U;
}

uint8_t Health_ConsecutiveResets(void) { return consecutive_resets; }
uint8_t Health_LastResetReason(void) { return last_reset_reason; }
uint8_t Health_LastUnhealthyMask(void) { return last_unhealthy_mask; }
uint8_t Health_StorageError(void) { return storage_error; }
