#include "health_v2.h"
#include "config_store_v2.h"
#include "CH58x_common.h"
#include <string.h>

#define HEALTH_PAGE 0x5000u
#define HEALTH_MAGIC 0x324C5448UL
#define HEALTH_SLOTS 128u

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

extern uint32_t LocalTimestamp;
extern volatile uint32_t CurTick;
static uint32_t last_lora_ms;
static uint32_t last_ir_ms;
static uint32_t last_flash_ms;
static uint32_t last_periodic_ms;
static uint32_t last_ble_ms;
static uint8_t reported_mask;
static uint8_t consecutive_resets;
static uint8_t last_reset_reason;

#define HEALTH_FAST_DEADLINE_MS  500u
#define HEALTH_SLOW_DEADLINE_MS 2500u

static uint8_t valid(const health_record_v2_t *r)
{
    return r->magic==HEALTH_MAGIC && r->crc32==ConfigV2_Crc32((const uint8_t*)r,(uint16_t)(sizeof(*r)-8u));
}

void HealthV2_Init(uint16_t fault_snapshot)
{
    health_record_v2_t r,best,out;
    uint16_t i,next=0;uint8_t found=0;uint32_t generation=0;
    last_reset_reason=(uint8_t)SYS_GetLastResetSta();
    for(i=0;i<HEALTH_SLOTS;i++){
        EEPROM_READ(HEALTH_PAGE+(uint32_t)i*sizeof(r),&r,sizeof(r));
        if(r.magic==0xFFFFFFFFUL){next=i;break;}
        if(valid(&r)&&(!found||(int32_t)(r.generation-generation)>0)){best=r;generation=r.generation;found=1;}
        next=(uint16_t)(i+1u);
    }
    consecutive_resets=(last_reset_reason==RST_STATUS_WTR||last_reset_reason==RST_STATUS_LRM1)?(uint8_t)((found&&best.consecutive_resets<255u)?best.consecutive_resets+1u:1u):0u;
    if(next>=HEALTH_SLOTS){EEPROM_ERASE(HEALTH_PAGE,EEPROM_BLOCK_SIZE);next=0;}
    memset(&out,0,sizeof(out));out.magic=HEALTH_MAGIC;out.generation=generation+1u;out.timestamp=LocalTimestamp;out.fault_snapshot=fault_snapshot;
    out.reset_reason=last_reset_reason;out.consecutive_resets=consecutive_resets;out.unhealthy_mask=found?best.unhealthy_mask:0;
    out.crc32=ConfigV2_Crc32((const uint8_t*)&out,(uint16_t)(sizeof(out)-8u));EEPROM_WRITE(HEALTH_PAGE+(uint32_t)next*sizeof(out),&out,sizeof(out));
    last_lora_ms=CurTick;last_ir_ms=CurTick;last_flash_ms=CurTick;last_periodic_ms=CurTick;last_ble_ms=CurTick;
    reported_mask=0;
    PRINT("Reset reason=%u, consecutive watchdog resets=%u\r\n",last_reset_reason,consecutive_resets);
}

void HealthV2_Mark(uint8_t component)
{
    uint32_t now=CurTick;
    if(component&HEALTH_V2_LORA)last_lora_ms=now;
    if(component&HEALTH_V2_IR)last_ir_ms=now;
    if(component&HEALTH_V2_FLASH)last_flash_ms=now;
    if(component&HEALTH_V2_PERIODIC)last_periodic_ms=now;
    if(component&HEALTH_V2_BLE_STACK)last_ble_ms=now;
}

uint8_t HealthV2_Tick100ms(void)
{
    uint32_t now=CurTick;
    uint8_t mask=0;
    if((uint32_t)(now-last_lora_ms)>HEALTH_FAST_DEADLINE_MS)mask|=HEALTH_V2_LORA;
    if((uint32_t)(now-last_ir_ms)>HEALTH_FAST_DEADLINE_MS)mask|=HEALTH_V2_IR;
    if((uint32_t)(now-last_flash_ms)>HEALTH_SLOW_DEADLINE_MS)mask|=HEALTH_V2_FLASH;
    if((uint32_t)(now-last_periodic_ms)>HEALTH_SLOW_DEADLINE_MS)mask|=HEALTH_V2_PERIODIC;
    if((uint32_t)(now-last_ble_ms)>HEALTH_FAST_DEADLINE_MS)mask|=HEALTH_V2_BLE_STACK;
    if(mask&&mask!=reported_mask){PRINT("Health timeout mask=0x%02x, watchdog refresh stopped\r\n",mask);reported_mask=mask;}
    if(!mask)reported_mask=0;
    return mask==0;
}

uint8_t HealthV2_ConsecutiveResets(void){return consecutive_resets;}
uint8_t HealthV2_LastResetReason(void){return last_reset_reason;}
