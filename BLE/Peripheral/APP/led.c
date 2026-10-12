/**
 * @file led.c
 * @brief 产品状态灯（从左到右：绿=运行/红外、白=通信、蓝=BLE、红=硬件故障）
 */
#include "board.h"
#include "CONFIG.h"
#include "config_store.h"
#include "device_service.h"
#include "ml307r.h"

#define LED_IR_FLASH_MS 400U
static uint32_t irFlashStart;
static uint8_t irFlashing;

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

    GAPRole_GetParameter(GAPROLE_STATE, &bleState);

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
        led_write(LED_RED_PIN, 0U);
        return;
    }

    /*
     * 蓝灯用于 BLE：未连接熄灭；连接成功常亮；设备定位（Identify）时快闪，
     * 由上面的分支处理。连接建立即代表链路已确认，不再持续闪烁。
     */
    bleState &= GAPROLE_STATE_ADV_MASK;
    led_write(LED_BLUE_PIN,
              (bleState == GAPROLE_CONNECTED || bleState == GAPROLE_CONNECTED_ADV) ?
              1U : 0U);

    /* 红灯只表示需要排查的硬件级故障，不表示普通网络重连。 */
    hardwareFault = Dev.errorCode.bit.flash || Dev.errorCode.bit.ad ||
                    Dev.errorCode.bit.power || Dev.errorCode.bit.rtc ||
                    (loraEnabled && Dev.errorCode.bit.lora) ||
                    (cellularEnabled &&
                     (cell->last_error == ML307_ERROR_MODEM ||
                      cell->last_error == ML307_ERROR_RX_OVERFLOW ||
                      cell->last_error == ML307_ERROR_UART));
    led_write(LED_RED_PIN, hardwareFault);
}
