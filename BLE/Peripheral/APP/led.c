/**
 * @file led.c
 * @brief 产品状态灯（从左到右：绿=系统/红外、白=通信、蓝=BLE、红=硬件故障）
 */
#include "board.h"
#include "CONFIG.h"
#include "config_store.h"
#include "device_service.h"
#include "ml307r.h"

#define LED_HEARTBEAT_PERIOD_MS 2000U
#define LED_HEARTBEAT_ON_MS      100U
#define LED_LINK_BLINK_HALF_MS   500U
#define LED_IR_FLASH_MS          400U

static uint32_t irFlashStart;
static uint32_t irFlashUntil;

static void led_write(uint32_t pin, uint8_t on)
{
    /* 四盏灯均为低电平点亮。 */
    if(on) GPIOB_ResetBits(pin);
    else GPIOB_SetBits(pin);
}

static uint8_t heartbeat_pulse(void)
{
    return (CurTick % LED_HEARTBEAT_PERIOD_MS) < LED_HEARTBEAT_ON_MS;
}

void LED_NotifyIrTx(void)
{
    irFlashStart = CurTick;
    irFlashUntil = CurTick + LED_IR_FLASH_MS;
}

void LED_Pro(void)
{
    const ml307_status_t *cell = Ml307_GetStatus();
    uint8_t loraEnabled = Connectivity_LoraEnabled();
    uint8_t cellularEnabled = Connectivity_CellularEnabled();
    uint8_t networkEnabled = loraEnabled || cellularEnabled;
    uint8_t networkOnline = (loraEnabled && Dev.loraStatus >= Status_Connected) ||
                            (cellularEnabled && cell->mqtt_online);
    uint8_t bleState = 0U;
    uint8_t irFlashing = (int32_t)(irFlashUntil - CurTick) > 0;
    uint8_t hardwareFault;

    GAPRole_GetParameter(GAPROLE_STATE, &bleState);

    /* BLE设备定位期间只快闪蓝灯。 */
    if(DeviceService_IdentifyActive()) {
        led_write(LED_GREEN_PIN, 0U);
        led_write(LED_WHITE_PIN, 0U);
        led_write(LED_BLUE_PIN, (uint8_t)((CurTick / 100U) & 1U));
        led_write(LED_RED_PIN, 0U);
        return;
    }

    /* 绿灯正常每 2 秒短亮一次；红外发送时以 100 ms 节奏闪两次。 */
    led_write(LED_GREEN_PIN,
              irFlashing ?
              (uint8_t)((((CurTick - irFlashStart) / 100U) & 1U) == 0U) :
              heartbeat_pulse());

    /* LoRa/MQTT 共用白灯：在线常亮，重连时 500 ms 明灭，未启用时熄灭。 */
    led_write(LED_WHITE_PIN,
              networkEnabled ?
              (networkOnline ? 1U : (uint8_t)((CurTick / LED_LINK_BLINK_HALF_MS) & 1U)) :
              0U);

    /* 蓝灯用于 BLE 调试：连接期间持续快闪，广播或未连接时熄灭。 */
    bleState &= GAPROLE_STATE_ADV_MASK;
    led_write(LED_BLUE_PIN,
              (bleState == GAPROLE_CONNECTED || bleState == GAPROLE_CONNECTED_ADV) ?
              (uint8_t)((CurTick / 100U) & 1U) : 0U);

    /* 普通网络重连由白灯表示；红灯只表示需要排查的硬件级故障。 */
    hardwareFault = Dev.errorCode.bit.flash || Dev.errorCode.bit.ad ||
                    Dev.errorCode.bit.power ||
                    (loraEnabled && Dev.errorCode.bit.lora) ||
                    (cellularEnabled &&
                     (cell->last_error == ML307_ERROR_MODEM ||
                      cell->last_error == ML307_ERROR_RX_OVERFLOW ||
                      cell->last_error == ML307_ERROR_UART));
    led_write(LED_RED_PIN, hardwareFault);
}
