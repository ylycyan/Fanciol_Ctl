#include "board.h"
#include "CH58x_common.h"
#include "config_store_v2.h"
#include "protocol_v2.h"

static volatile uint16_t Flash_Delay;

/* Compatibility facade for existing modules. New data is partitioned by concern. */
int Flash_Erase(void) { return EEPROM_ERASE(V2_CONFIG_SLOT_A, EEPROM_BLOCK_SIZE); }
int Flash_Write(uint8_t *data, uint32_t len) { return EEPROM_WRITE(V2_CONFIG_SLOT_A, data, len); }
int Flash_Read(uint8_t *data, uint32_t len) { return EEPROM_READ(V2_CONFIG_SLOT_A, data, len); }

/* Legacy callers express delay in 10 ms ticks; Flash_Poll runs once per second. */
void SaveDevInfo(uint16_t delay) { Flash_Delay = delay ? (uint16_t)((delay + 99u) / 100u) : 1u; }

void Flash_Poll(void)
{
    uint8_t status;
    if(!Flash_Delay || --Flash_Delay) return;
    status = ConfigV2_CommitIfChanged();
    if(status == V2_STATUS_OK) status = IrStoreV2_SaveIfChanged();
    if(status == V2_STATUS_OK) status = RuntimeV2_Append();
    if(status != V2_STATUS_OK) {
        Dev.errorCode.bit.flash = 1;
        PRINT("V2 flash save failed: %u\r\n", status);
    } else {
        Dev.errorCode.bit.flash = 0;
        PRINT("V2 flash save complete, revision=%lu\r\n", ConfigV2_GetRevision());
    }
}

void LoadDevInfo(void)
{
    uint8_t status = ConfigV2_Load();
    if(status != V2_STATUS_OK) {
        ConfigV2_FactoryDefaults();
        if(ConfigV2_Commit(0) != V2_STATUS_OK) Dev.errorCode.bit.flash = 1;
    }
    RuntimeV2_Load();
    IrStoreV2_Load();
    Dev.magicCode = MAGIC_CODE;
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.wind = Wind_Auto;
    Dev.lastOnTime = 0;
    Dev.lastReportTime = 0;
    Dev.loadPower = 0;
    Dev.irPendingCmd = 0;
    Dev.loraStatus = Status_Uninit;
    Timer_Lora = LORA_SEC_TO_TICKS(300);
    if(Dev.irActType == ACT_TYPE_IR && (Dev.irIdx >= IR_BRAND_COUNT || !Dev.irType || Dev.irType == 0xFFFFu)) Dev.errorCode.bit.irMatch = 1;
    PRINT("V2 config loaded: node=%04x channel=%u revision=%lu\r\n", Dev.nodeId, Dev.channel, ConfigV2_GetRevision());
}
