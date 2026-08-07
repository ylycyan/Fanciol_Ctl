#include "board.h"
#include "CH58x_common.h"
#include "config_store_v2.h"
#include "protocol_v2.h"

static volatile uint16_t Flash_Delay;
static volatile uint8_t Ir_Save_Pending;

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
    config_v2_load_state_t load_state = ConfigV2_GetLoadState();
    if(status != V2_STATUS_OK) {
        if(status == V2_STATUS_VERIFY_FAILED) {
            status = ConfigV2_InitializeDefaults(
                load_state == CONFIG_V2_LOAD_CORRUPT);
            if(status != V2_STATUS_OK) Dev.errorCode.bit.flash = 1;
        } else {
            /*
             * 读取失败时只在 RAM 中使用安全默认值，绝不擦写可能仍有效的双槽。
             * 下一次正常启动仍有机会恢复原配置。
             */
            ConfigV2_FactoryDefaults();
            Dev.errorCode.bit.flash = 1;
        }
    }
    load_state = ConfigV2_GetLoadState();
    if(load_state == CONFIG_V2_LOAD_REPAIRED ||
       load_state == CONFIG_V2_LOAD_DEFAULTS_RECOVERED ||
       load_state == CONFIG_V2_LOAD_DEGRADED ||
       load_state == CONFIG_V2_LOAD_IO_ERROR) {
        Dev.errorCode.bit.flash = 1;
    }
    /*
     * 先建立安全运行默认值，再让独立运行日志恢复最近一次已提交的空调状态。
     * 这样首次上电仍是关机，非掉电复位则不会向云端误报成默认关机/25 ℃。
     */
    Dev.magicCode = MAGIC_CODE;
    Dev.onOff = PowerOff;
    Dev.ctlMode = Mode_Auto;
    Dev.temSet = 25;
    Dev.wind = Wind_Auto;
    Dev.lastOnTime = 0;
    Dev.lastPowerChange = 0;
    Dev.lastReportTime = 0;
    Dev.loadPower = 0;
    (void)RuntimeV2_Load();
    (void)IrStoreV2_Load();
    (void)LoraParamsV2_Load();
    if(StorageV2_GetStartupFlags() != 0U) Dev.errorCode.bit.flash = 1;
    /* 状态机没有 Uninit 分支；直接进入注册态，下一次 20 ms 轮询即按配置初始化射频。 */
    Dev.loraStatus = Status_Logining;
    Timer_Lora = LORA_SEC_TO_TICKS(300);
    if(Dev.irActType == ACT_TYPE_IR) {
        Dev.errorCode.bit.irMatch =
            (Dev.irIdx >= IR_BRAND_COUNT || !Dev.irType || Dev.irType == 0xFFFFu) ? 1u : 0u;
    } else {
        /* 学习模式不依赖品牌内码，不能残留“内码未匹配”故障。 */
        Dev.errorCode.bit.irMatch = 0;
        Dev.errorCode.bit.irLearn = Dev.learnNum == 0U ? 1U : 0U;
    }
    PRINT("V2 config loaded: node=0x%04x channel=%u revision=%lu storage=%u radio=%u/%u,%u/%u\r\n",
          Dev.nodeId, Dev.channel, ConfigV2_GetRevision(), load_state,
          Dev.loraRegisterSf, Dev.loraRegisterBw,
          Dev.loraListenSf, Dev.loraListenBw);
}
