#include "board.h"
#include "CH58x_common.h"
#include "config_store_v2.h"
#include "protocol_v2.h"

static volatile uint16_t Flash_Delay;
static volatile uint8_t Ir_Save_Pending;

/* Compatibility facade for existing modules. New data is partitioned by concern. */
int Flash_Erase(void) { return EEPROM_ERASE(V2_CONFIG_SLOT_A, EEPROM_BLOCK_SIZE); }
int Flash_Write(uint8_t *data, uint32_t len) { return EEPROM_WRITE(V2_CONFIG_SLOT_A, data, len); }
int Flash_Read(uint8_t *data, uint32_t len) { return EEPROM_READ(V2_CONFIG_SLOT_A, data, len); }

/* Legacy callers express delay in 10 ms ticks; Flash_Poll runs once per second. */
void SaveDevInfo(uint16_t delay) { Flash_Delay = delay ? (uint16_t)((delay + 99u) / 100u) : 1u; }
void SaveIrInfo(void) { Ir_Save_Pending = 1u; }

void Flash_Poll(void)
{
    uint8_t status = V2_STATUS_OK;
    uint8_t attempted = 0;
    if(Flash_Delay && !--Flash_Delay) {
        attempted = 1;
        status = ConfigV2_CommitIfChanged();
        if(status == V2_STATUS_OK) status = RuntimeV2_Append();
    }
    if(Ir_Save_Pending) {
        uint8_t ir_status;
        attempted = 1;
        ir_status = IrStoreV2_SaveIfChanged();
        if(ir_status == V2_STATUS_OK) Ir_Save_Pending = 0;
        else status = ir_status;
    }
    if(!attempted) return;
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
    LoraParamsV2_Load();
    Dev.magicCode = MAGIC_CODE;
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 25;
    Dev.wind = Wind_Auto;
    Dev.lastOnTime = 0;
    Dev.lastReportTime = 0;
    Dev.loadPower = 0;
    Dev.irPendingCmd = 0;
    /* 状态机没有 Uninit 分支；直接进入注册态，下一次 20 ms 轮询即按配置初始化射频。 */
    Dev.loraStatus = Status_Logining;
    Timer_Lora = LORA_SEC_TO_TICKS(300);
    if(Dev.irActType == ACT_TYPE_IR && (Dev.irIdx >= IR_BRAND_COUNT || !Dev.irType || Dev.irType == 0xFFFFu)) Dev.errorCode.bit.irMatch = 1;
    PRINT("V2 config loaded: node=0x%04x channel=%u revision=%lu radio=%u/%u,%u/%u\r\n",
          Dev.nodeId, Dev.channel, ConfigV2_GetRevision(), Dev.loraRegisterSf,
          Dev.loraRegisterBw, Dev.loraListenSf, Dev.loraListenBw);
}
