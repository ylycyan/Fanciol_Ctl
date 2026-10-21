/**
 * @file led.c
 * @brief 产品状态灯（从左到右：绿=运行/红外、白=通信、蓝=BLE、红=硬件故障）
 */
#include "board.h"
#include "CONFIG.h"
#include "config_store.h"
#include "device_service.h"
#include "ml307r.h"
#include "hlw8110.h"

#define LED_IR_FLASH_MS 400U
#define LED_BOOT_CHECK_MS 5000U
static uint32_t irFlashStart;
static uint8_t irFlashing;
static uint8_t bootChecked;

static void led_write(uint32_t pin, uint8_t on)
{
    /* 低电平点亮。 */
    if(on) GPIOB_ResetBits(pin);
    else GPIOB_SetBits(pin);
}

void LED_NotifyIrTx(void)
{
    irFlashStart = CurTick;
    irFlashing = 1U;
}

void LED_Pro(void)
{
    const ml307_status_t *cell = Ml307_GetStatus();
    uint8_t loraEnabled = Connectivity_LoraEnabled();
    uint8_t cellularEnabled = Connectivity_CellularEnabled();
    uint8_t networkOnline = (loraEnabled && Dev.loraStatus >= Status_Connected) ||
                            (cellularEnabled && cell->mqtt_online);
    uint8_t bleState = 0U;
    uint8_t hardwareFault;
    uint8_t bleReady;

    bleReady = (GAPRole_GetParameter(GAPROLE_STATE, &bleState) == SUCCESS);

    /* 运行时常亮；红外发送短闪两次后恢复。无符号差值兼容计时回绕。 */
    if(irFlashing && (uint32_t)(CurTick - irFlashStart) >= LED_IR_FLASH_MS)
        irFlashing = 0U;
    led_write(LED_GREEN_PIN,
              (!irFlashing || DeviceService_IdentifyActive()) ? 1U :
              (uint8_t)((((CurTick - irFlashStart) / 100U) & 1U) == 0U));
    /* 白灯只在启用的 LoRa/MQTT 至少一条在线时亮，离线不闪。 */
    led_write(LED_WHITE_PIN, networkOnline);

    /* BLE设备定位期间仅蓝灯快闪，保留运行与联网指示。 */
    if(DeviceService_IdentifyActive()) {
        led_write(LED_BLUE_PIN, (uint8_t)((CurTick / 100U) & 1U));
    }

    /*
     * 蓝灯用于 BLE：未连接熄灭；连接成功常亮；设备定位（Identify）时快闪，
     * 由上面的分支处理。连接建立即代表链路已确认，不再持续闪烁。
     */
    bleState &= GAPROLE_STATE_ADV_MASK;
    bleReady = bleReady && (bleState == GAPROLE_ADVERTISING ||
                           bleState == GAPROLE_CONNECTED || bleState == GAPROLE_CONNECTED_ADV);
    if(!DeviceService_IdentifyActive())
        led_write(LED_BLUE_PIN,
                  (bleState == GAPROLE_CONNECTED || bleState == GAPROLE_CONNECTED_ADV) ?
                  1U : 0U);

    /* 自检期间持续亮红灯；未取得有效采样或 SIM READY 不能提前放行。
     * 不等待网关、运营商注册或 MQTT；未对时也不等于 RTC 硬件故障。
     * 只在本次启动检查一次，避免运行中重连或 tick 回绕重启自检。 */
    hardwareFault = Dev.errorCode.bit.flash || Dev.errorCode.bit.ad ||
                    Dev.errorCode.bit.power || Dev.errorCode.bit.rtc || Dev.errorCode.bit.ble ||
                    (loraEnabled && Dev.errorCode.bit.lora) ||
                    (cellularEnabled && Dev.errorCode.bit.cellular);
    if(!bootChecked && CurTick >= LED_BOOT_CHECK_MS && !hardwareFault &&
       bleReady && ADC_IsValid() && HLW8110_GetStatus()->valid &&
       (!cellularEnabled || cell->sim_ready))
        bootChecked = 1U;
    led_write(LED_RED_PIN, !bootChecked || hardwareFault);
}
