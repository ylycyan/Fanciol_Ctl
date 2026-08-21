#include "ml307r.h"
#include "ml307r_codec.h"
#include "config_store.h"
#include "gateway_lora_codec.h"
#include "device_protocol.h"
#include "health.h"
#include "timer.h"
#include "time_utils.h"
#include "board.h"
#include "CH58x_common.h"
#include "ota_update.h"
#include <string.h>

#define ML307_RESET_PIN            GPIO_Pin_14
#define ML307_RX_RING_SIZE         256U
#define ML307_LINE_SIZE            512U
#define ML307_TX_SIZE              192U
#define ML307_RESET_LOW_MS         600U
#define ML307_BOOT_WAIT_MS         12000U
#define ML307_COMMAND_TIMEOUT_MS   3000U
#define ML307_MQTT_TIMEOUT_MS      15000U
#define ML307_SIGNAL_INTERVAL_MS   60000U
#define ML307_CLOCK_RETRY_MS       600000UL
#define ML307_CLOCK_INTERVAL_MS    21600000UL
#define ML307_AT_COMMAND_SIZE       64U
#define ML307_AT_RESPONSE_SIZE      128U
#define ML307_AT_TIMEOUT_MS         5000U
#define ML307_AT_RESULT_HOLD_MS     10000U
#define ML307_AT_RETRY_MS           700U
#define ML307_AT_RETRY_LIMIT        2U
#define ML307_SYNC_RETRY_LIMIT      4U
#define ML307_MQTT_FRAME_SIZE       GATEWAY_LORA_FANCOIL_REPORT_LENGTH
#define ML307_LORA_TAG_READ         0U
#define ML307_LORA_TAG_CONTROL      1U
#define ML307_DEVICE_ID_LENGTH      15U
#define ML307_MGMT_MAX_FRAME        160U
#define ML307_MGMT_TX_FRAME          40U
#define ML307_MGMT_MAGIC            0xC7U
#define ML307_MGMT_VERSION          0x01U

#define ML307_MGMT_RESULT               0x04U
#define ML307_MGMT_OTA_OFFER            0x05U
#define ML307_MGMT_OTA_ACTIVATE         0x06U
#define ML307_MGMT_OTA_CANCEL           0x07U
#define ML307_MGMT_OTA_STATUS           0x08U

#define ML307_HTTP_IDLE             0U
#define ML307_HTTP_CREATE           1U
#define ML307_HTTP_CACHE            2U
#define ML307_HTTP_ENCODING         3U
#define ML307_HTTP_HEADER           4U
#define ML307_HTTP_REQUEST          5U
#define ML307_HTTP_WAIT             6U
#define ML307_HTTP_READ             7U
#define ML307_HTTP_READ_DONE        8U
#define ML307_HTTP_DESTROY          9U

typedef struct {
    ml307_status_t status;
    uint32_t deadline_ms;
    uint32_t next_action_ms;
    uint32_t network_started_ms;
    uint32_t last_signal_ms;
    uint32_t next_clock_ms;
    uint16_t tx_length;
    uint16_t tx_offset;
    uint16_t line_length;
    uint8_t waiting;
    uint8_t response_seen;
    uint8_t report_requested;
    uint8_t pending_command_result;
    uint8_t pending_command_valid;
    uint8_t publishing_command_result;
    uint8_t publishing_command_valid;
    uint8_t network_command;
    uint8_t time_sync_attempts;
    uint8_t time_synced;
    uint8_t prompt_seen;
    uint8_t discard_line;
    uint8_t uart_enabled;
    uint8_t at_previous_phase;
    uint8_t at_result_read;
    uint8_t at_command_length;
    uint8_t at_retry_count;
    uint8_t mqtt_config_step;
    uint8_t subscribe_index;
    uint8_t recovery_level;
    uint8_t management_pending;
    uint8_t publishing_management;
    uint8_t management_length;
    uint8_t http_step;
    uint8_t http_id;
    uint8_t ota_activate_after_publish;
    uint16_t http_range_size;
    uint16_t http_range_received;
    uint32_t ota_write_offset;
    uint32_t at_started_ms;
    uint16_t at_start_tx_bytes;
    uint16_t at_start_rx_bytes;
    ml307_at_status_t at_status;
} ml307_context_t;

extern volatile uint32_t CurTick;

static ml307_context_t modem;
static volatile uint8_t rx_ring[ML307_RX_RING_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static char rx_line[ML307_LINE_SIZE];
static char tx_buffer[ML307_TX_SIZE];
static char device_id[ML307_DEVICE_ID_LENGTH + 1U];
static uint16_t device_jitter_ms;
static uint8_t management_frame[ML307_MGMT_TX_FRAME];

static void queue_ota_status(uint16_t transaction, uint8_t status);

static uint8_t reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static char hex_digit(uint8_t value)
{
    value &= 0x0FU;
    return (char)(value < 10U ? '0' + value : 'A' + value - 10U);
}

static void build_device_id(void)
{
    uint8_t uid[8] __attribute__((aligned(4)));
    uint8_t index;
    uint16_t crc;
    GET_UNIQUE_ID(uid);
    memcpy(device_id, "SAC", 3U);
    for(index = 0U; index < 6U; index++) {
        device_id[3U + index * 2U] = hex_digit(uid[index] >> 4);
        device_id[4U + index * 2U] = hex_digit(uid[index]);
    }
    device_id[ML307_DEVICE_ID_LENGTH] = '\0';
    crc = DeviceProtocol_Crc16(uid, 6U);
    device_jitter_ms = (uint16_t)(crc % 5000U);
}

static uint8_t at_phase_available(uint8_t phase)
{
    return (phase >= ML307_PHASE_AT_SYNC && phase <= ML307_PHASE_ONLINE) ||
           phase == ML307_PHASE_SIGNAL_QUERY || phase == ML307_PHASE_BACKOFF;
}

static uint8_t at_resume_phase(uint8_t phase)
{
    if(phase == ML307_PHASE_APN) return ML307_PHASE_SIM;
    if(phase == ML307_PHASE_SIGNAL_QUERY) return ML307_PHASE_ONLINE;
    return phase;
}

static uint16_t bounded_length(const char *text, uint16_t capacity)
{
    uint16_t length = 0U;
    while(length < capacity && text[length]) length++;
    return length;
}

static void status_phase(ml307_phase_t phase)
{
    modem.status.phase = (uint8_t)phase;
}

static void discard_at_session(void)
{
    if(modem.at_status.state == ML307_AT_RUNNING) {
        modem.at_status.state = ML307_AT_CANCELLED;
        modem.at_status.elapsed_ms = CurTick - modem.at_started_ms;
    }
    modem.at_result_read = 1U;
}

static void uart_disable(void)
{
    PFIC_DisableIRQ(UART1_IRQn);
    UART1_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT | RB_IER_THR_EMPTY);
    UART1_Reset();
    PFIC_ClearPendingIRQ(UART1_IRQn);
    modem.uart_enabled = 0U;
    modem.status.uart_active = 0U;
    modem.line_length = 0U;
    modem.discard_line = 0U;
    modem.tx_length = 0U;
    modem.tx_offset = 0U;
    rx_head = 0U;
    rx_tail = 0U;
    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bRXD1 | bTXD1, GPIO_ModeIN_Floating);
}

static void uart_enable(void)
{
    if(modem.uart_enabled) return;
    modem.line_length = 0U;
    modem.discard_line = 0U;
    rx_head = 0U;
    rx_tail = 0U;
    GPIOA_SetBits(bTXD1);
    /* The CH583-side pull-up keeps RX deterministic while a level shifter or
     * module carrier is starting. Bare 1.8 V modules still require external
     * level conversion, as documented for the hardware integration. */
    GPIOA_ModeCfg(bRXD1, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    UART1_INTCfg(DISABLE, RB_IER_LINE_STAT | RB_IER_THR_EMPTY);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY);
    PFIC_ClearPendingIRQ(UART1_IRQn);
    modem.uart_enabled = 1U;
    modem.status.uart_active = 1U;
    PFIC_EnableIRQ(UART1_IRQn);
}

static void reset_runtime_flags(void)
{
    modem.status.sim_ready = 0U;
    modem.status.network_registered = 0U;
    modem.status.mqtt_online = 0U;
    modem.waiting = 0U;
    modem.response_seen = 0U;
    modem.prompt_seen = 0U;
    modem.network_command = 0U;
    modem.time_sync_attempts = 0U;
    modem.time_synced = 0U;
    modem.mqtt_config_step = 0U;
    modem.subscribe_index = 0U;
    modem.tx_length = 0U;
    modem.tx_offset = 0U;
}

static void start_hardware_reset(void)
{
    discard_at_session();
    uart_disable();
    reset_runtime_flags();
    GPIOB_ResetBits(ML307_RESET_PIN);
    /* PB14 is wired directly to the 5 V carrier's RST input. Pull it low only;
     * release to high impedance so the carrier supplies its own logic level. */
    GPIOB_ModeCfg(ML307_RESET_PIN, GPIO_ModeOut_PP_5mA);
    if(modem.status.reset_count != 0xFFFFU) modem.status.reset_count++;
    modem.deadline_ms = CurTick + ML307_RESET_LOW_MS;
    status_phase(ML307_PHASE_RESETTING);
}

static void enter_backoff(ml307_error_t error)
{
    uint32_t delay;
    if(error == ML307_ERROR_MQTT || error == ML307_ERROR_BROKER_AUTH ||
       error == ML307_ERROR_SUBSCRIBE || error == ML307_ERROR_PUBLISH ||
       error == ML307_ERROR_TIMEOUT) {
        if(modem.recovery_level < 3U) modem.recovery_level++;
    } else modem.recovery_level = 3U;
    if(modem.recovery_level >= 3U) uart_disable();
    modem.status.mqtt_online = 0U;
    modem.status.last_error = (uint8_t)error;
    if(error == ML307_ERROR_TIMEOUT && modem.status.timeout_count != 0xFFFFU)
        modem.status.timeout_count++;
    if(modem.status.consecutive_failures < 255U) modem.status.consecutive_failures++;
    switch(modem.status.consecutive_failures) {
    case 1U: delay = 5000UL; break;
    case 2U: delay = 10000UL; break;
    case 3U: delay = 20000UL; break;
    case 4U: delay = 40000UL; break;
    case 5U: delay = 60000UL; break;
    case 6U: delay = 120000UL; break;
    case 7U: delay = 240000UL; break;
    default: delay = 300000UL; break;
    }
    if(delay > 300000UL) delay = 300000UL;
    if(delay < 300000UL) delay += device_jitter_ms % 1000U;
    modem.next_action_ms = CurTick + delay;
    modem.waiting = 0U;
    modem.report_requested = 1U;
    status_phase(ML307_PHASE_BACKOFF);
}

static uint8_t tx_append_char(uint16_t *length, char value)
{
    if(*length >= ML307_TX_SIZE) return 0U;
    tx_buffer[(*length)++] = value;
    return 1U;
}

static uint8_t tx_append_text(uint16_t *length, const char *text)
{
    while(*text) if(!tx_append_char(length, *text++)) return 0U;
    return 1U;
}

static uint8_t tx_append_counted(uint16_t *length, const char *text, uint16_t count)
{
    while(count--) if(!tx_append_char(length, *text++)) return 0U;
    return 1U;
}

static uint8_t tx_append_u32(uint16_t *length, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while(value && count < sizeof(digits));
    while(count) if(!tx_append_char(length, digits[--count])) return 0U;
    return 1U;
}

static uint8_t tx_append_topic(uint16_t *length, const char *suffix)
{
    const connectivity_config_t *config = Connectivity_Get();
    return tx_append_text(length, config->mqtt_topic_prefix) &&
           tx_append_char(length, '/') && tx_append_text(length, device_id) &&
           tx_append_text(length, suffix);
}

static uint8_t tx_start(uint16_t length)
{
    if(modem.tx_length != modem.tx_offset || length == 0U || length > ML307_TX_SIZE)
        return 0U;
    modem.tx_offset = 0U;
    modem.tx_length = length;
    return 1U;
}

static uint8_t tx_command(const char *command)
{
    uint16_t length = bounded_length(command, ML307_TX_SIZE);
    if(length == 0U || length >= ML307_TX_SIZE) return 0U;
    memcpy(tx_buffer, command, length);
    return tx_start(length);
}

static void tx_pump(void)
{
    while(modem.tx_offset < modem.tx_length && R8_UART1_TFC < UART_FIFO_SIZE) {
        R8_UART1_THR = (uint8_t)tx_buffer[modem.tx_offset++];
        modem.status.tx_bytes++;
    }
    if(modem.tx_offset >= modem.tx_length) {
        modem.tx_offset = 0U;
        modem.tx_length = 0U;
    }
}

static void transition_wait(ml307_phase_t phase, uint32_t timeout_ms)
{
    modem.waiting = 1U;
    modem.response_seen = 0U;
    modem.deadline_ms = CurTick + timeout_ms;
    status_phase(phase);
}

static uint8_t send_apn(void)
{
    const connectivity_config_t *config = Connectivity_Get();
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+CGDCONT=1,\"") ||
       !tx_append_text(&length, config->cellular_pdp_type == CONNECTIVITY_PDP_IPV4V6 ?
                                "IPV4V6" : "IP") ||
       !tx_append_text(&length, "\",\"") ||
       !tx_append_text(&length, config->apn) ||
       !tx_append_text(&length, "\"\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_APN, ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_config(void)
{
    uint16_t length = 0U;
    const connectivity_config_t *config = Connectivity_Get();
    if(modem.mqtt_config_step == 0U) {
        /* A CH583-only restart can leave client 0 alive if the modem reset did
         * not complete. Disconnect is best-effort; "not connected" is OK. */
        if(!tx_append_text(&length, "AT+MQTTDISC=0\r\n") ||
           !tx_start(length)) return 0U;
    } else if(modem.mqtt_config_step == 1U) {
        if(!tx_append_text(&length, "AT+MQTTCFG=\"keepalive\",0,") ||
           !tx_append_u32(&length, config->mqtt_keepalive_sec) ||
           !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    } else {
        if(!tx_append_text(&length, "AT+MQTTCFG=\"clean\",0,") ||
           !tx_append_u32(&length, config->mqtt_clean_session) ||
           !tx_append_text(&length, "\r\n") ||
           !tx_start(length)) return 0U;
    }
    transition_wait(ML307_PHASE_MQTT_CONFIG, ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_connect(void)
{
    const connectivity_config_t *config = Connectivity_Get();
    const char *client_id = config->mqtt_client_id[0] ? config->mqtt_client_id : device_id;
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MQTTCONN=0,\"") ||
       !tx_append_text(&length, config->mqtt_host) ||
       !tx_append_text(&length, "\",") || !tx_append_u32(&length, config->mqtt_port) ||
       !tx_append_text(&length, ",\"") || !tx_append_text(&length, client_id) ||
       !tx_append_text(&length, "\",\"\",\"\"\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_MQTT_CONNECT, ML307_MQTT_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_subscribe(void)
{
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MQTTSUB=0,\"") ||
       !tx_append_topic(&length, modem.subscribe_index == 0U ? "/d" : "/m/d") ||
       !tx_append_text(&length, "\",") || !tx_append_u32(&length, modem.subscribe_index) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_MQTT_SUBSCRIBE, ML307_MQTT_TIMEOUT_MS);
    return 1U;
}

static uint8_t http_url_parts(uint16_t *origin_length, const char **path)
{
    const ota_metadata_t *ota = Ota_Get();
    const char *slash;
    if(ota->url_length < 8U || memcmp(ota->url, "http://", 7U) != 0)
        return 0U;
    slash = strchr(ota->url + 7, '/');
    *origin_length = slash ? (uint16_t)(slash - ota->url) : ota->url_length;
    *path = slash ? slash : "/";
    return *origin_length < ota->url_length || !slash;
}

static void http_wait(uint32_t timeout_ms)
{
    modem.waiting = 1U;
    modem.deadline_ms = CurTick + timeout_ms;
}

static uint8_t send_http_create(void)
{
    const ota_metadata_t *ota = Ota_Get();
    const char *path;
    uint16_t origin_length;
    uint16_t length = 0U;
    if(!http_url_parts(&origin_length, &path) ||
       !tx_append_text(&length, "AT+MHTTPCREATE=\"") ||
       !tx_append_counted(&length, ota->url, origin_length) ||
       !tx_append_text(&length, "\"\r\n") || !tx_start(length)) return 0U;
    (void)path;
    http_wait(ML307_MQTT_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_http_setting(const char *name, uint8_t first, uint8_t second)
{
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MHTTPCFG=\"") ||
       !tx_append_text(&length, name) || !tx_append_text(&length, "\",") ||
       !tx_append_u32(&length, modem.http_id) || !tx_append_text(&length, ",") ||
       !tx_append_u32(&length, first) ||
       (second != 0xFFU && (!tx_append_text(&length, ",") ||
                            !tx_append_u32(&length, second))) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    http_wait(ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_http_header(void)
{
    const ota_metadata_t *ota = Ota_Get();
    uint32_t start;
    uint32_t end;
    uint32_t remaining;
    uint16_t length = 0U;

    modem.http_range_received = 0U;
    modem.ota_write_offset = ota->downloaded_bytes;
    remaining = ota->image_size - modem.ota_write_offset;
    modem.http_range_size = (uint16_t)(remaining > EEPROM_BLOCK_SIZE ?
                                       EEPROM_BLOCK_SIZE : remaining);
    start = modem.ota_write_offset;
    end = start + modem.http_range_size - 1U;
    if(!tx_append_text(&length, "AT+MHTTPHEADER=") ||
       !tx_append_u32(&length, modem.http_id) ||
       !tx_append_text(&length, ",0,0,\"Range: bytes=") ||
       !tx_append_u32(&length, start) || !tx_append_char(&length, '-') ||
       !tx_append_u32(&length, end) || !tx_append_text(&length, "\"\r\n") ||
       !tx_start(length)) return 0U;
    http_wait(ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_http_request(void)
{
    const ota_metadata_t *ota = Ota_Get();
    const char *path;
    uint16_t origin_length;
    uint16_t length = 0U;
    uint16_t path_length;
    if(!http_url_parts(&origin_length, &path)) return 0U;
    path_length = path[0] == '/' && path[1] == '\0' ? 1U :
                  (uint16_t)(ota->url_length - origin_length);
    if(!tx_append_text(&length, "AT+MHTTPREQUEST=") ||
       !tx_append_u32(&length, modem.http_id) ||
       !tx_append_text(&length, ",1,0,\"") ||
       !tx_append_counted(&length, path, path_length) ||
       !tx_append_text(&length, "\"\r\n") || !tx_start(length)) return 0U;
    http_wait(60000UL);
    return 1U;
}

static uint8_t send_http_read(void)
{
    uint16_t remaining = (uint16_t)(modem.http_range_size - modem.http_range_received);
    uint16_t amount = remaining > 240U ? 240U : remaining;
    uint16_t length = 0U;
    if(!amount || !tx_append_text(&length, "AT+MHTTPREAD=") ||
       !tx_append_u32(&length, modem.http_id) ||
       !tx_append_text(&length, ",1,") || !tx_append_u32(&length, amount) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    http_wait(ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_http_destroy(void)
{
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MHTTPDESTROY=") ||
       !tx_append_u32(&length, modem.http_id) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    http_wait(ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t build_publish_frame(uint8_t *frame)
{
    if(modem.publishing_command_valid)
        return Lora_BuildControlResult(frame, ML307_LORA_TAG_CONTROL,
                                       modem.publishing_command_result);
    return Lora_BuildNodeReport(frame, ML307_LORA_TAG_READ, 0U);
}

static uint8_t start_publish(void)
{
    uint8_t frame[ML307_MQTT_FRAME_SIZE];
    uint8_t frame_length;
    uint16_t payload_length;
    uint16_t length = 0U;

    modem.publishing_management = modem.management_pending;
    if(modem.publishing_management) frame_length = modem.management_length;
    else {
        modem.publishing_command_valid = modem.pending_command_valid;
        modem.publishing_command_result = modem.pending_command_result;
        modem.pending_command_valid = 0U;
        frame_length = build_publish_frame(frame);
        if(frame_length == 0U) return 0U;
    }
    payload_length = (uint16_t)frame_length * 2U;
    if(!tx_append_text(&length, "AT+MQTTPUB=0,\"") ||
       !tx_append_topic(&length, modem.publishing_management ? "/m/u" : "/u") ||
       !tx_append_text(&length, "\",") ||
       !tx_append_u32(&length, modem.publishing_management ? 1U :
                      Connectivity_Get()->mqtt_qos) ||
       !tx_append_text(&length, ",0,0,") || !tx_append_u32(&length, payload_length) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    if(!modem.publishing_management) modem.report_requested = 0U;
    modem.prompt_seen = 0U;
    transition_wait(ML307_PHASE_PUBLISH, 5000U);
    return 1U;
}

static uint8_t send_publish_payload(void)
{
    uint8_t frame[ML307_MQTT_FRAME_SIZE];
    uint8_t frame_length;
    uint16_t length;
    if(modem.publishing_management) {
        frame_length = modem.management_length;
        length = Ml307Codec_HexEncode(management_frame, frame_length,
                                      tx_buffer, sizeof(tx_buffer));
    } else {
        frame_length = build_publish_frame(frame);
        length = Ml307Codec_HexEncode(frame, frame_length, tx_buffer, sizeof(tx_buffer));
    }
    if(!length || !tx_start(length)) return 0U;
    modem.prompt_seen = 1U;
    modem.deadline_ms = CurTick + 10000U;
    return 1U;
}

static uint8_t topic_matches(const ml307_publish_t *publish, const char *suffix)
{
    const connectivity_config_t *config = Connectivity_Get();
    uint16_t prefix_length = bounded_length(config->mqtt_topic_prefix,
                                             sizeof(config->mqtt_topic_prefix));
    uint16_t suffix_length = bounded_length(suffix, 8U);
    uint16_t length = prefix_length + 1U + ML307_DEVICE_ID_LENGTH + suffix_length;
    return length == publish->topic_length &&
           memcmp(publish->topic, config->mqtt_topic_prefix, prefix_length) == 0 &&
           publish->topic[prefix_length] == '/' &&
           memcmp(publish->topic + prefix_length + 1U, device_id,
                  ML307_DEVICE_ID_LENGTH) == 0 &&
           memcmp(publish->topic + prefix_length + 1U + ML307_DEVICE_ID_LENGTH,
                  suffix, suffix_length) == 0;
}

static uint16_t frame_u16(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t frame_u32(const uint8_t *value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static void frame_put16(uint8_t *value, uint16_t number)
{
    value[0] = (uint8_t)number;
    value[1] = (uint8_t)(number >> 8);
}

static void frame_put32(uint8_t *value, uint32_t number)
{
    value[0] = (uint8_t)number;
    value[1] = (uint8_t)(number >> 8);
    value[2] = (uint8_t)(number >> 16);
    value[3] = (uint8_t)(number >> 24);
}

static void queue_management_frame(uint8_t type, uint16_t transaction,
                                   const uint8_t *payload, uint8_t payload_length)
{
    uint16_t crc;
    uint16_t total = (uint16_t)payload_length + 10U;
    if(total > sizeof(management_frame)) return;
    management_frame[0] = ML307_MGMT_MAGIC;
    management_frame[1] = ML307_MGMT_VERSION;
    management_frame[2] = type;
    management_frame[3] = 0U;
    frame_put16(management_frame + 4, transaction);
    frame_put16(management_frame + 6, payload_length);
    if(payload_length) memcpy(management_frame + 8, payload, payload_length);
    crc = DeviceProtocol_Crc16(management_frame, (uint16_t)payload_length + 8U);
    frame_put16(management_frame + 8U + payload_length, crc);
    modem.management_length = (uint8_t)total;
    modem.management_pending = 1U;
}

static void queue_management_result(uint8_t request_type, uint16_t transaction,
                                    uint8_t status)
{
    uint8_t payload[2];
    payload[0] = request_type;
    payload[1] = status;
    queue_management_frame(ML307_MGMT_RESULT, transaction, payload, sizeof(payload));
}

static void handle_management(const ml307_publish_t *publish)
{
    uint8_t frame[ML307_MGMT_MAX_FRAME];
    uint8_t length;
    uint8_t type;
    uint8_t status = DEVICE_STATUS_INVALID_ARG;
    uint16_t transaction;
    uint16_t payload_length;

    if(!topic_matches(publish, "/m/d")) return;
    length = Ml307Codec_HexDecode(publish->payload, publish->payload_length,
                                  frame, sizeof(frame));
    if(length < 10U || frame[0] != ML307_MGMT_MAGIC ||
       frame[1] != ML307_MGMT_VERSION) return;
    type = frame[2];
    transaction = frame_u16(frame + 4);
    payload_length = frame_u16(frame + 6);
    if(payload_length > ML307_MGMT_MAX_FRAME - 10U ||
       length != payload_length + 10U ||
       frame_u16(frame + 8U + payload_length) !=
       DeviceProtocol_Crc16(frame, (uint16_t)payload_length + 8U)) return;
    switch(type) {
    case ML307_MGMT_OTA_OFFER:
        /* version:u32, size:u32, crc32:u32, urlLength:u8, URL */
        if(payload_length >= 13U && frame[20] == payload_length - 13U) {
            status = Ota_BeginRemote(frame_u32(frame + 8), frame_u32(frame + 12),
                                     frame_u32(frame + 16),
                                     (const char *)(frame + 21), frame[20]);
        }
        break;
    case ML307_MGMT_OTA_ACTIVATE:
        if(payload_length == 4U && frame_u32(frame + 8) == Ota_Get()->update_version) {
            status = Ota_MarkInstall();
            if(status == DEVICE_STATUS_OK) modem.ota_activate_after_publish = 1U;
        }
        break;
    case ML307_MGMT_OTA_CANCEL:
        if(payload_length == 0U) {
            status = Ota_Cancel();
            if(status == DEVICE_STATUS_OK) modem.http_step = ML307_HTTP_IDLE;
        }
        break;
    case ML307_MGMT_OTA_STATUS:
        if(payload_length != 0U) break;
        queue_ota_status(transaction, DEVICE_STATUS_OK);
        return;
    default:
        status = DEVICE_STATUS_NOT_SUPPORTED;
        break;
    }
    queue_management_result(type, transaction, status);
}

static void execute_command(const uint8_t *frame, uint8_t length)
{
    uint8_t result = Lora_ExecuteNodeControl(frame, length, 1U);
    if(result == 0xFFU) {
        if(modem.status.command_rejected_count != 0xFFFFU)
            modem.status.command_rejected_count++;
        modem.status.last_error = ML307_ERROR_COMMAND;
        return;
    }
    if(result == 0U) {
        if(modem.status.command_executed_count != 0xFFFFU)
            modem.status.command_executed_count++;
    } else if(modem.status.command_rejected_count != 0xFFFFU) {
        modem.status.command_rejected_count++;
    }
    modem.pending_command_result = result;
    modem.pending_command_valid = 1U;
    modem.report_requested = 1U;
}

static void handle_downlink(const ml307_publish_t *publish)
{
    uint8_t frame[ML307_MQTT_FRAME_SIZE];
    uint8_t frame_length;
    if(!topic_matches(publish, "/d")) return;
    frame_length = Ml307Codec_HexDecode(publish->payload, publish->payload_length,
                                        frame, sizeof(frame));
    if(frame_length != ML307_MQTT_FRAME_SIZE || frame[0] != 0x0DU ||
       frame[1] != ML307_LORA_TAG_CONTROL ||
       frame_u16(frame + 2) != 0U || frame_u16(frame + 4) != Dev.nodeId ||
       !GatewayLora_Validate(frame, frame_length)) {
        if(modem.status.command_rejected_count != 0xFFFFU)
            modem.status.command_rejected_count++;
        modem.status.last_error = ML307_ERROR_COMMAND;
        return;
    }
    /* MQTT command-as-action: every valid frame executes immediately. */
    execute_command(frame, frame_length);
}

static uint8_t parse_cereg(const char *line)
{
    const char *cursor = strchr(line, ':');
    uint8_t first = 0xFFU;
    uint8_t status;
    if(!cursor) return 0U;
    cursor++;
    while(*cursor == ' ') cursor++;
    if(*cursor < '0' || *cursor > '9') return 0U;
    first = (uint8_t)(*cursor++ - '0');
    while(*cursor == ' ') cursor++;
    status = first;
    if(*cursor == ',') {
        cursor++;
        while(*cursor == ' ') cursor++;
        if(*cursor < '0' || *cursor > '9') return 0U;
        status = (uint8_t)(*cursor - '0');
    }
    return status == 1U || status == 5U;
}

static void parse_signal(const char *line)
{
    const char *cursor = strchr(line, ':');
    uint16_t raw = 0U;
    if(!cursor) return;
    cursor++;
    while(*cursor == ' ') cursor++;
    while(*cursor >= '0' && *cursor <= '9') {
        raw = (uint16_t)(raw * 10U + (uint8_t)(*cursor - '0'));
        cursor++;
    }
    modem.status.signal_rssi = raw <= 31U ? (int8_t)(-113 + (int16_t)raw * 2) : -127;
}

static void parse_extended_signal(const char *line)
{
    const char *cursor = strchr(line, ':');
    uint16_t values[6] = {0};
    uint8_t index;
    if(!cursor) return;
    cursor++;
    for(index = 0U; index < 6U; index++) {
        uint8_t digits = 0U;
        while(*cursor == ' ') cursor++;
        while(*cursor >= '0' && *cursor <= '9') {
            values[index] = (uint16_t)(values[index] * 10U + (uint8_t)(*cursor++ - '0'));
            digits = 1U;
        }
        if(!digits || (index < 5U && *cursor++ != ',')) return;
    }
    modem.status.signal_rssi = values[0] <= 63U ?
        (int8_t)(-111 + (int16_t)values[0]) : -127;
    modem.status.rsrq_db_x10 = values[4] <= 34U ?
        (int16_t)(-195 + (int16_t)values[4] * 5) : -32768;
    modem.status.rsrp_dbm = values[5] <= 97U ?
        (int8_t)(-140 + (int16_t)values[5]) : -127;
}

static void parse_network_clock(const char *line, uint16_t length)
{
    ml307_clock_t clock;
    time_fields_t fields;
    uint32_t local_timestamp;
    uint32_t utc_timestamp;
    int32_t offset;
    if(!Ml307Codec_ParseClock(line, length, &clock)) return;
    fields.year = clock.year;
    fields.month = clock.month;
    fields.day = clock.day;
    fields.hour = clock.hour;
    fields.minute = clock.minute;
    fields.second = clock.second;
    if(!TimeUtil_ToUnix(&fields, &local_timestamp)) return;
    offset = (int32_t)clock.timezone_quarters * 900L;
    if(offset >= 0) {
        if(local_timestamp < (uint32_t)offset) return;
        utc_timestamp = local_timestamp - (uint32_t)offset;
    } else {
        uint32_t magnitude = (uint32_t)(-offset);
        if(local_timestamp > 0xFFFFFFFFUL - magnitude) return;
        utc_timestamp = local_timestamp + magnitude;
    }
    if(utc_timestamp < 1672531200UL || utc_timestamp > 2147483000UL) return;
    RTC_SetTimestamp(utc_timestamp);
    modem.time_synced = RTC_IsTimeValid();
    if(modem.time_synced) {
        LocalTimestamp = Rtc_GetTimestamp();
        modem.next_clock_ms = CurTick + ML307_CLOCK_INTERVAL_MS;
    }
}

static void complete_publish(void)
{
    if(modem.status.publish_count != 0xFFFFU) modem.status.publish_count++;
    if(modem.publishing_management) {
        modem.management_pending = 0U;
        modem.publishing_management = 0U;
        modem.waiting = 0U;
        if(modem.ota_activate_after_publish) {
            modem.ota_activate_after_publish = 0U;
            SYS_DisableAllIrq(NULL);
            mDelaymS(10);
            SYS_ResetExecute();
        } else status_phase(ML307_PHASE_ONLINE);
        return;
    }
    modem.status.last_report_ms = CurTick;
    modem.publishing_command_valid = 0U;
    modem.waiting = 0U;
    status_phase(ML307_PHASE_ONLINE);
}

static uint8_t mqtt_urc_number(const char *line, uint8_t index, uint16_t *value)
{
    const char *cursor = strstr(line, "\",");
    uint8_t current = 0U;
    if(!cursor || !value) return 0U;
    cursor += 2;
    for(;;) {
        uint32_t parsed = 0U;
        uint8_t digits = 0U;
        while(*cursor == ' ') cursor++;
        while(*cursor >= '0' && *cursor <= '9') {
            parsed = parsed * 10U + (uint8_t)(*cursor++ - '0');
            if(parsed > 0xFFFFU) return 0U;
            digits = 1U;
        }
        if(!digits) return 0U;
        if(current++ == index) {
            *value = (uint16_t)parsed;
            return 1U;
        }
        while(*cursor == ' ') cursor++;
        if(*cursor++ != ',') return 0U;
    }
}

static void queue_ota_status(uint16_t transaction, uint8_t status)
{
    const ota_metadata_t *ota = Ota_Get();
    uint8_t payload[24];
    payload[0] = status;
    payload[1] = ota->state;
    frame_put32(payload + 2, ota->current_version);
    frame_put32(payload + 6, ota->update_version);
    frame_put32(payload + 10, ota->image_size);
    frame_put32(payload + 14, ota->downloaded_bytes);
    frame_put32(payload + 18, ota->image_crc32);
    frame_put16(payload + 22, (uint16_t)(ota->erased_bytes / EEPROM_BLOCK_SIZE));
    queue_management_frame(ML307_MGMT_OTA_STATUS, transaction, payload, sizeof(payload));
}

static void http_fail(ml307_error_t error)
{
    modem.http_step = ML307_HTTP_IDLE;
    modem.waiting = 0U;
    enter_backoff(error);
}

static uint8_t parse_http_read(char *line, uint16_t length)
{
    char *comma = line;
    char *previous = 0;
    char *data;
    uint16_t data_length = 0U;
    uint8_t index;
    uint8_t decoded[240];
    uint8_t decoded_length;
    for(index = 0U; index < 4U; index++) {
        previous = comma;
        comma = strchr(comma, ',');
        if(!comma) return 0U;
        comma++;
    }
    data = comma;
    comma = previous;
    while(comma < data - 1 && *comma >= '0' && *comma <= '9')
        data_length = (uint16_t)(data_length * 10U + (uint8_t)(*comma++ - '0'));
    if(data_length == 0U || data_length > sizeof(decoded) ||
       (uint16_t)(line + length - data) != data_length * 2U) return 0U;
    decoded_length = Ml307Codec_HexDecode(data, data_length * 2U,
                                          decoded, sizeof(decoded));
    if(decoded_length != data_length ||
       modem.http_range_received + data_length > modem.http_range_size) return 0U;
    if(Ota_Write(modem.ota_write_offset + modem.http_range_received,
                 decoded, data_length) != DEVICE_STATUS_OK) return 0U;
    modem.http_range_received = (uint16_t)(modem.http_range_received + data_length);
    if(modem.http_range_received == modem.http_range_size)
        modem.http_step = ML307_HTTP_READ_DONE;
    return 1U;
}

static uint8_t http_process_line(char *line, uint16_t length)
{
    uint16_t code;
    uint16_t content_length;
    uint8_t status;
    if(modem.http_step == ML307_HTTP_IDLE) return 0U;
    if(strstr(line, "+MHTTPCREATE:") != 0) {
        const char *cursor = strchr(line, ':');
        if(cursor) {
            while(*++cursor == ' ') {}
            if(*cursor >= '0' && *cursor <= '3') modem.http_id = (uint8_t)(*cursor - '0');
        }
        return 1U;
    }
    if(strstr(line, "+MHTTPURC: \"err\",") != 0) {
        uint16_t error_code = 0U;
        mqtt_urc_number(line, 1U, &error_code);
        http_fail(error_code == 1U ? ML307_ERROR_DNS : ML307_ERROR_HTTP);
        return 1U;
    }
    if(strstr(line, "+MHTTPURC: \"recv\",") != 0) {
        if(!mqtt_urc_number(line, 1U, &code) ||
           !mqtt_urc_number(line, 3U, &content_length) || code != 206U ||
           content_length != modem.http_range_size) {
            http_fail(ML307_ERROR_HTTP);
        } else {
            modem.waiting = 0U;
            modem.http_step = ML307_HTTP_READ;
        }
        return 1U;
    }
    if(strstr(line, "+MHTTPREAD:") != 0) {
        if(!parse_http_read(line, length)) http_fail(ML307_ERROR_OTA);
        return 1U;
    }
    if(strcmp(line, "ERROR") == 0 || strstr(line, "+CME ERROR:") != 0) {
        if(modem.http_step == ML307_HTTP_DESTROY) {
            modem.waiting = 0U;
            modem.http_step = ML307_HTTP_IDLE;
            return 1U;
        }
        http_fail(ML307_ERROR_HTTP);
        return 1U;
    }
    if(strcmp(line, "OK") != 0) return 1U;
    modem.waiting = 0U;
    switch(modem.http_step) {
    case ML307_HTTP_CREATE: modem.http_step = ML307_HTTP_CACHE; break;
    case ML307_HTTP_CACHE: modem.http_step = ML307_HTTP_ENCODING; break;
    case ML307_HTTP_ENCODING: modem.http_step = ML307_HTTP_HEADER; break;
    case ML307_HTTP_HEADER: modem.http_step = ML307_HTTP_REQUEST; break;
    case ML307_HTTP_REQUEST:
        modem.http_step = ML307_HTTP_WAIT;
        modem.waiting = 1U;
        modem.deadline_ms = CurTick + 60000UL;
        break;
    case ML307_HTTP_READ_DONE:
        if(modem.ota_write_offset + modem.http_range_received >= Ota_Get()->image_size) {
            status = Ota_BeginVerify();
            if(status == DEVICE_STATUS_OK) modem.http_step = ML307_HTTP_DESTROY;
            else http_fail(ML307_ERROR_OTA);
        } else modem.http_step = ML307_HTTP_HEADER;
        break;
    case ML307_HTTP_DESTROY:
        modem.http_step = ML307_HTTP_IDLE;
        break;
    default:
        break;
    }
    return 1U;
}

static void complete_mqtt_connection(void)
{
    modem.waiting = 0U;
    modem.subscribe_index = 0U;
    modem.next_action_ms = CurTick;
    status_phase(ML307_PHASE_MQTT_SUBSCRIBE);
}

static void at_append_line(const char *line, uint16_t length)
{
    uint16_t available;
    uint16_t copy_length;
    if(modem.at_status.response_length &&
       modem.at_status.response_length < ML307_AT_RESPONSE_SIZE) {
        tx_buffer[modem.at_status.response_length++] = '\n';
    }
    available = (uint16_t)(ML307_AT_RESPONSE_SIZE - modem.at_status.response_length);
    copy_length = length < available ? length : available;
    if(copy_length) {
        memcpy(tx_buffer + modem.at_status.response_length, line, copy_length);
        modem.at_status.response_length = (uint8_t)(modem.at_status.response_length + copy_length);
    }
    if(copy_length != length) modem.at_status.truncated = 1U;
}

static void at_finish(ml307_at_state_t state)
{
    modem.at_status.state = (uint8_t)state;
    modem.at_status.elapsed_ms = CurTick - modem.at_started_ms;
    status_phase((ml307_phase_t)modem.at_previous_phase);
    if(modem.at_previous_phase == ML307_PHASE_BACKOFF) uart_disable();
    else modem.next_action_ms = CurTick;
}

static uint8_t at_process_line(const char *line, uint16_t length)
{
    if(modem.at_status.state != ML307_AT_RUNNING) return 0U;
    at_append_line(line, length);
    if(strcmp(line, "OK") == 0) at_finish(ML307_AT_OK);
    else if(strcmp(line, "ERROR") == 0 || strstr(line, "+CME ERROR:") != 0 ||
            strstr(line, "+CMS ERROR:") != 0) at_finish(ML307_AT_ERROR);
    return 1U;
}

static void process_line(char *line, uint16_t length)
{
    ml307_publish_t publish;
    int8_t publish_status;
    uint8_t phase = modem.status.phase;
    if(at_process_line(line, length)) return;

    publish_status = Ml307Codec_ParsePublish(line, length, &publish);
    if(publish_status == ML307_CODEC_OK) {
        if(topic_matches(&publish, "/m/d")) handle_management(&publish);
        else handle_downlink(&publish);
        return;
    }
    if(publish_status == ML307_CODEC_FRAGMENTED) {
        modem.status.last_error = ML307_ERROR_COMMAND;
        return;
    }
    if(http_process_line(line, length)) return;
    if(strstr(line, "+MQTTURC: \"conn\",0,") != 0) {
        uint16_t result = 0xFFFFU;
        if(mqtt_urc_number(line, 1U, &result) && result == 0U) {
            if(phase == ML307_PHASE_MQTT_CONNECT) {
                modem.response_seen |= 0x02U;
                if((modem.response_seen & 0x03U) == 0x03U)
                    complete_mqtt_connection();
            }
        } else if(result == 1U) {
            /* State 1 means that the module is already reconnecting. Give its
             * internal MQTT client one bounded window before escalating to a
             * hardware reset; immediate resets can otherwise form a loop on
             * a briefly unstable cellular network. */
            modem.status.mqtt_online = 0U;
            modem.waiting = 1U;
            /* Auto reconnect has no new AT command OK; count the existing
             * connection request as accepted and wait only for conn state 0. */
            modem.response_seen = 0x01U;
            modem.deadline_ms = CurTick + ML307_MQTT_TIMEOUT_MS;
            status_phase(ML307_PHASE_MQTT_CONNECT);
        } else if(result == 2U && phase == ML307_PHASE_MQTT_CONFIG) {
            /* Expected asynchronous acknowledgement of AT+MQTTDISC=0. */
        } else enter_backoff(ML307_ERROR_MQTT);
        return;
    }
    if(strstr(line, "+MQTTURC: \"timeout\",0,") != 0) {
        enter_backoff(ML307_ERROR_MQTT);
        return;
    }
    if(strstr(line, "+MQTTURC: \"suback\",0,") != 0) {
        uint16_t result;
        /* SUBACK code 0/1/2 is the granted QoS; 128 means rejected. */
        if(!mqtt_urc_number(line, 2U, &result) || result > 2U) {
            enter_backoff(ML307_ERROR_MQTT);
            return;
        }
        modem.waiting = 0U;
        if(modem.subscribe_index == 0U) {
            modem.subscribe_index = 1U;
            modem.next_action_ms = CurTick;
        } else {
            modem.status.mqtt_online = 1U;
            modem.status.last_connected_ms = CurTick;
            modem.status.consecutive_failures = 0U;
            modem.status.last_error = ML307_ERROR_NONE;
            modem.recovery_level = 0U;
            modem.report_requested = 1U;
            modem.next_action_ms = CurTick + device_jitter_ms;
            status_phase(ML307_PHASE_ONLINE);
        }
        return;
    }
    if(strstr(line, "+MQTTURC: \"puback\",0,") != 0) {
        if(phase == ML307_PHASE_PUBLISH && modem.prompt_seen)
            complete_publish();
        return;
    }
    if(strstr(line, "+CPIN:") != 0 && strstr(line, "READY") != 0)
        modem.status.sim_ready = 1U;
    if(strstr(line, "+CEREG:") != 0) {
        modem.status.network_registered = parse_cereg(line);
        if(!modem.status.network_registered &&
           (phase == ML307_PHASE_ONLINE || phase == ML307_PHASE_PUBLISH ||
            phase == ML307_PHASE_SIGNAL_QUERY)) {
            enter_backoff(ML307_ERROR_NETWORK);
            return;
        }
    }
    if(strstr(line, "+CSQ:") != 0) parse_signal(line);
    if(strstr(line, "+CESQ:") != 0) parse_extended_signal(line);
    if(strstr(line, "+CCLK:") != 0) parse_network_clock(line, length);
    if(strcmp(line, "ERROR") == 0 || strstr(line, "+CME ERROR:") != 0) {
        if(phase == ML307_PHASE_SIM) enter_backoff(ML307_ERROR_SIM);
        else if(phase == ML307_PHASE_NETWORK && modem.network_command == 1U) {
            modem.waiting = 0U;
            modem.next_action_ms = CurTick + 1000U;
        }
        else if(phase == ML307_PHASE_NETWORK) enter_backoff(ML307_ERROR_NETWORK);
        else if(phase == ML307_PHASE_SIGNAL_QUERY) {
            if(modem.network_command == 2U) modem.last_signal_ms = CurTick;
            modem.waiting = 0U;
            status_phase(ML307_PHASE_ONLINE);
        }
        else if(phase == ML307_PHASE_MQTT_CONFIG && modem.mqtt_config_step == 0U) {
            /* No existing client is the normal cold-start result. */
            modem.waiting = 0U;
            modem.mqtt_config_step = 1U;
            modem.next_action_ms = CurTick + 100U;
        }
        else if(phase >= ML307_PHASE_MQTT_CONFIG && phase <= ML307_PHASE_PUBLISH)
            enter_backoff(ML307_ERROR_MQTT);
        else enter_backoff(ML307_ERROR_MODEM);
        return;
    }
    if(strcmp(line, "OK") != 0) return;

    if(phase == ML307_PHASE_MQTT_CONNECT) {
        /* OK and conn URC are asynchronous and either may arrive first. */
        modem.response_seen |= 0x01U;
        if((modem.response_seen & 0x03U) == 0x03U)
            complete_mqtt_connection();
        else modem.waiting = 1U;
        return;
    }
    if(phase == ML307_PHASE_MQTT_SUBSCRIBE) {
        /* 命令 OK 只表示已受理，必须继续等待 conn/suback URC。 */
        modem.response_seen = 1U;
        modem.waiting = 1U;
        return;
    }
    modem.waiting = 0U;
    switch(phase) {
    case ML307_PHASE_AT_SYNC:
        /* AT auto-baud probing shares this byte with the later clock retry
         * counter.  Once the modem has answered, start clock retries from
         * zero so repeated AT probes cannot suppress network time sync. */
        modem.time_sync_attempts = 0U;
        if(tx_command("ATE0\r\n")) transition_wait(ML307_PHASE_SIM, ML307_COMMAND_TIMEOUT_MS);
        break;
    case ML307_PHASE_SIM:
        if(!modem.status.sim_ready) modem.next_action_ms = CurTick + 2000U;
        else if(!send_apn()) enter_backoff(ML307_ERROR_CONFIG);
        break;
    case ML307_PHASE_APN:
        modem.network_started_ms = CurTick;
        modem.next_action_ms = CurTick;
        status_phase(ML307_PHASE_NETWORK);
        break;
    case ML307_PHASE_NETWORK:
        modem.next_action_ms = CurTick +
            (modem.status.network_registered ? 1000U : 2000U);
        break;
    case ML307_PHASE_MQTT_CONFIG:
        if(modem.mqtt_config_step < 2U) {
            modem.mqtt_config_step++;
            /* Give the disconnect URC time to arrive before reusing client 0. */
            modem.next_action_ms = CurTick +
                (modem.mqtt_config_step == 1U ? 200U : 0U);
        } else {
            modem.next_action_ms = CurTick;
            status_phase(ML307_PHASE_MQTT_CONNECT);
        }
        break;
    case ML307_PHASE_PUBLISH:
        if(modem.prompt_seen) {
            if(modem.publishing_management || Connectivity_Get()->mqtt_qos == 1U) {
                /* QoS 1 只有收到 puback 才算完成；OK 仅表示命令已处理。 */
                modem.response_seen = 1U;
                modem.waiting = 1U;
            } else complete_publish();
        }
        break;
    case ML307_PHASE_SIGNAL_QUERY:
        if(modem.network_command == 2U || modem.network_command == 6U)
            modem.last_signal_ms = CurTick;
        status_phase(ML307_PHASE_ONLINE);
        break;
    default:
        break;
    }
}

static void consume_uart(void)
{
    uint16_t budget = 192U;
    while(rx_tail != rx_head && budget--) {
        char value = (char)rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1U) % ML307_RX_RING_SIZE);
        if(value == '>' && modem.status.phase == ML307_PHASE_PUBLISH &&
           !modem.prompt_seen) {
            modem.line_length = 0U;
            modem.discard_line = 0U;
            if(!send_publish_payload()) enter_backoff(ML307_ERROR_MQTT);
            continue;
        }
        if(modem.prompt_seen && modem.line_length == 0U && value == ' ') continue;
        if(value == '\r') continue;
        if(value == '\n') {
            if(!modem.discard_line && modem.line_length) {
                rx_line[modem.line_length] = '\0';
                process_line(rx_line, modem.line_length);
            }
            modem.line_length = 0U;
            modem.discard_line = 0U;
            continue;
        }
        if(modem.discard_line) continue;
        if(modem.line_length + 1U >= sizeof(rx_line)) {
            modem.discard_line = 1U;
            if(modem.status.rx_overflow_count != 0xFFFFU)
                modem.status.rx_overflow_count++;
            modem.status.last_error = ML307_ERROR_RX_OVERFLOW;
        } else rx_line[modem.line_length++] = value;
    }
}

void Ml307_Init(void)
{
    memset(&modem, 0, sizeof(modem));
    build_device_id();
    modem.status.signal_rssi = -127;
    modem.status.rsrp_dbm = -127;
    modem.status.rsrq_db_x10 = -32768;
    modem.at_status.state = ML307_AT_IDLE;
    rx_head = 0U;
    rx_tail = 0U;
    if(!Connectivity_CellularEnabled()) {
        status_phase(ML307_PHASE_DISABLED);
        PRINT("ML307 init: configured=0, hardware untouched\r\n");
        return;
    }
    GPIOB_ModeCfg(ML307_RESET_PIN, GPIO_ModeIN_Floating);
    uart_disable();
    PRINT("ML307 init: configured=1, UART1 deferred\r\n");
    start_hardware_reset();
}

void Ml307_ApplyConfiguration(void)
{
    discard_at_session();
    modem.pending_command_valid = 0U;
    modem.publishing_command_valid = 0U;
    modem.report_requested = 0U;
    if(!Connectivity_CellularEnabled()) {
        reset_runtime_flags();
        uart_disable();
        GPIOB_ModeCfg(ML307_RESET_PIN, GPIO_ModeIN_Floating);
        status_phase(ML307_PHASE_DISABLED);
        return;
    }
    start_hardware_reset();
}

void Ml307_Restart(void)
{
    if(Connectivity_CellularEnabled()) start_hardware_reset();
}

void Ml307_RequestReport(void)
{
    modem.report_requested = 1U;
}

uint8_t Ml307_IsOnline(void)
{
    return Connectivity_CellularEnabled() && modem.status.mqtt_online;
}

const ml307_status_t *Ml307_GetStatus(void)
{
    modem.status.uart_active = modem.uart_enabled;
    modem.status.waiting = modem.waiting;
    modem.status.retry_remaining_ms =
        modem.status.phase == ML307_PHASE_BACKOFF && !reached(CurTick, modem.next_action_ms)
        ? modem.next_action_ms - CurTick : 0U;
    return &modem.status;
}

const char *Ml307_GetDeviceId(void)
{
    return device_id;
}

uint8_t Ml307_AtStart(const uint8_t *command, uint8_t length)
{
    uint8_t index;
    if(!command || length < 2U || length > ML307_AT_COMMAND_SIZE) return DEVICE_STATUS_INVALID_ARG;
    if(!Connectivity_CellularEnabled()) return DEVICE_STATUS_CONFLICT;
    if(Ota_Get()->state != OTA_STATE_IDLE) return DEVICE_STATUS_BUSY;
    if(modem.at_status.state == ML307_AT_RUNNING || modem.tx_length != 0U)
        return DEVICE_STATUS_BUSY;
    if(!at_phase_available(modem.status.phase)) return DEVICE_STATUS_BUSY;
    if(modem.status.phase == ML307_PHASE_BACKOFF && !modem.uart_enabled) uart_enable();
    if(!modem.uart_enabled) return DEVICE_STATUS_CONFLICT;
    if((command[0] != 'A' && command[0] != 'a') ||
       (command[1] != 'T' && command[1] != 't')) return DEVICE_STATUS_INVALID_ARG;
    for(index = 0U; index < length; index++)
        if(command[index] < 0x20U || command[index] > 0x7EU)
            return DEVICE_STATUS_INVALID_ARG;

    /* Developer AT is allowed to take over an automatic command wait.  The
     * previous command has already left the TX FIFO, so discard only its
     * unfinished receive state and resume that phase after the manual result. */
    modem.at_previous_phase = at_resume_phase(modem.status.phase);
    modem.waiting = 0U;
    modem.response_seen = 0U;
    modem.prompt_seen = 0U;
    modem.line_length = 0U;
    modem.discard_line = 0U;
    rx_tail = rx_head;
    memcpy(tx_buffer, command, length);
    tx_buffer[length] = '\r';
    tx_buffer[length + 1U] = '\n';
    if(!tx_start((uint16_t)length + 2U)) return DEVICE_STATUS_BUSY;
    modem.at_started_ms = CurTick;
    modem.at_start_tx_bytes = (uint16_t)modem.status.tx_bytes;
    modem.at_start_rx_bytes = (uint16_t)modem.status.rx_bytes;
    modem.at_command_length = (uint8_t)(length + 2U);
    modem.at_retry_count = 0U;
    modem.at_status.generation++;
    if(!modem.at_status.generation) modem.at_status.generation = 1U;
    modem.at_status.elapsed_ms = 0U;
    modem.at_status.state = ML307_AT_RUNNING;
    modem.at_status.truncated = 0U;
    modem.at_status.response_length = 0U;
    modem.at_result_read = 0U;
    return DEVICE_STATUS_OK;
}

uint8_t Ml307_AtCancel(void)
{
    if(modem.at_status.state != ML307_AT_RUNNING) return DEVICE_STATUS_INVALID_ARG;
    at_finish(ML307_AT_CANCELLED);
    return DEVICE_STATUS_OK;
}

const ml307_at_status_t *Ml307_AtGetStatus(void)
{
    if(modem.at_status.state == ML307_AT_RUNNING)
        modem.at_status.elapsed_ms = CurTick - modem.at_started_ms;
    return &modem.at_status;
}

uint16_t Ml307_AtGetTxDelta(void)
{
    return (uint16_t)((uint16_t)modem.status.tx_bytes - modem.at_start_tx_bytes);
}

uint16_t Ml307_AtGetRxDelta(void)
{
    return (uint16_t)((uint16_t)modem.status.rx_bytes - modem.at_start_rx_bytes);
}

uint8_t Ml307_AtCopyResponse(uint8_t *output, uint8_t capacity)
{
    uint8_t length = modem.at_status.response_length < capacity
        ? modem.at_status.response_length : capacity;
    if(output && length) memcpy(output, tx_buffer, length);
    if(modem.at_status.state != ML307_AT_RUNNING) modem.at_result_read = 1U;
    return length;
}

void Ml307_Process(void)
{
    uint32_t now = CurTick;
    const connectivity_config_t *config = Connectivity_Get();
    Health_Mark(HEALTH_CELLULAR);

    if(!Connectivity_CellularEnabled()) {
        if(modem.status.phase != ML307_PHASE_DISABLED) Ml307_ApplyConfiguration();
        return;
    }
    if(modem.uart_enabled) {
        tx_pump();
        consume_uart();
        tx_pump();
    }
    if(modem.at_status.state == ML307_AT_RUNNING) {
        if(modem.at_status.response_length == 0U &&
           modem.at_retry_count < ML307_AT_RETRY_LIMIT &&
           modem.tx_length == 0U &&
           reached(now, modem.at_started_ms + (uint32_t)(modem.at_retry_count + 1U) * ML307_AT_RETRY_MS)) {
            if(tx_start(modem.at_command_length)) {
                modem.at_retry_count++;
            }
        }
        if(reached(now, modem.at_started_ms + ML307_AT_TIMEOUT_MS))
            at_finish(ML307_AT_TIMEOUT);
        return;
    }
    if(modem.at_status.state != ML307_AT_IDLE && !modem.at_result_read &&
       !reached(now, modem.at_started_ms + ML307_AT_TIMEOUT_MS + ML307_AT_RESULT_HOLD_MS))
        return;
    if(modem.http_step != ML307_HTTP_IDLE) {
        if(modem.waiting && reached(now, modem.deadline_ms)) {
            http_fail(ML307_ERROR_HTTP);
            return;
        }
        if(modem.tx_length == 0U && !modem.waiting) {
            uint8_t sent = 1U;
            switch(modem.http_step) {
            case ML307_HTTP_CREATE: sent = send_http_create(); break;
            case ML307_HTTP_CACHE: sent = send_http_setting("cached", 1U, 0xFFU); break;
            case ML307_HTTP_ENCODING: sent = send_http_setting("encoding", 0U, 1U); break;
            case ML307_HTTP_HEADER: sent = send_http_header(); break;
            case ML307_HTTP_REQUEST: sent = send_http_request(); break;
            case ML307_HTTP_READ: sent = send_http_read(); break;
            case ML307_HTTP_DESTROY: sent = send_http_destroy(); break;
            default: break;
            }
            if(!sent) http_fail(ML307_ERROR_HTTP);
        }
        return;
    }
    if(modem.status.phase == ML307_PHASE_ONLINE && !modem.management_pending) {
        uint8_t ota_state = Ota_Get()->state;
        if(ota_state == OTA_STATE_ERASING) {
            uint8_t result = Ota_EraseStep();
            if(result != DEVICE_STATUS_OK) {
                modem.status.last_error = ML307_ERROR_OTA;
                Ota_Cancel();
                queue_ota_status(0U, result);
            }
            return;
        }
        if(ota_state == OTA_STATE_DOWNLOADING) {
            modem.http_step = ML307_HTTP_CREATE;
            modem.http_id = 0U;
            return;
        }
        if(ota_state == OTA_STATE_VERIFYING) {
            uint8_t result = Ota_VerifyStep();
            if(result == DEVICE_STATUS_OK || result == DEVICE_STATUS_VERIFY_FAILED ||
               result == DEVICE_STATUS_IO_ERROR) {
                if(result != DEVICE_STATUS_OK) modem.status.last_error = ML307_ERROR_OTA;
                queue_ota_status(0U, result);
            }
            return;
        }
    }
    if(modem.waiting && reached(now, modem.deadline_ms)) {
        if(modem.status.phase == ML307_PHASE_AT_SYNC &&
           modem.time_sync_attempts < ML307_SYNC_RETRY_LIMIT) {
            modem.waiting = 0U;
            modem.next_action_ms = now + 200U;
            return;
        }
        if(modem.status.phase == ML307_PHASE_NETWORK && modem.network_command == 1U) {
            modem.waiting = 0U;
            modem.next_action_ms = now + 1000U;
            return;
        }
        if(modem.status.phase == ML307_PHASE_SIGNAL_QUERY) {
            if(modem.network_command == 2U) modem.last_signal_ms = now;
            modem.waiting = 0U;
            status_phase(ML307_PHASE_ONLINE);
            return;
        }
        enter_backoff(ML307_ERROR_TIMEOUT);
        return;
    }
    if(modem.tx_length != 0U) return;

    switch((ml307_phase_t)modem.status.phase) {
    case ML307_PHASE_DISABLED:
        start_hardware_reset();
        break;
    case ML307_PHASE_RESETTING:
        if(reached(now, modem.deadline_ms)) {
            GPIOB_ModeCfg(ML307_RESET_PIN, GPIO_ModeIN_Floating);
            modem.next_action_ms = now + ML307_BOOT_WAIT_MS;
            status_phase(ML307_PHASE_BOOTING);
        }
        break;
    case ML307_PHASE_BOOTING:
        if(reached(now, modem.next_action_ms)) {
            uart_enable();
            if(tx_command("AT\r\n")) {
                modem.time_sync_attempts = 1U;
                transition_wait(ML307_PHASE_AT_SYNC, ML307_COMMAND_TIMEOUT_MS);
            }
        }
        break;
    case ML307_PHASE_AT_SYNC:
        if(!modem.waiting && reached(now, modem.next_action_ms) && tx_command("AT\r\n")) {
            modem.time_sync_attempts++;
            transition_wait(ML307_PHASE_AT_SYNC, ML307_COMMAND_TIMEOUT_MS);
        }
        break;
    case ML307_PHASE_SIM:
        if(!modem.waiting && reached(now, modem.next_action_ms) && tx_command("AT+CPIN?\r\n"))
            transition_wait(ML307_PHASE_SIM, ML307_COMMAND_TIMEOUT_MS);
        break;
    case ML307_PHASE_NETWORK:
        if((uint32_t)(now - modem.network_started_ms) >=
           (uint32_t)config->network_timeout_sec * 1000UL)
            enter_backoff(ML307_ERROR_NETWORK);
        else if(!modem.waiting && reached(now, modem.next_action_ms)) {
            if(modem.status.network_registered &&
               (modem.time_synced || modem.time_sync_attempts >= 3U)) {
                if(!modem.time_synced)
                    modem.next_clock_ms = now + ML307_CLOCK_RETRY_MS;
                modem.next_action_ms = now;
                status_phase(ML307_PHASE_MQTT_CONFIG);
            } else if(modem.status.network_registered) {
                if(tx_command("AT+CCLK?\r\n")) {
                    modem.network_command = 1U;
                    modem.time_sync_attempts++;
                    transition_wait(ML307_PHASE_NETWORK, ML307_COMMAND_TIMEOUT_MS);
                }
            } else if(tx_command("AT+CEREG?\r\n")) {
                modem.network_command = 0U;
                transition_wait(ML307_PHASE_NETWORK, ML307_COMMAND_TIMEOUT_MS);
            }
        }
        break;
    case ML307_PHASE_MQTT_CONFIG:
        if(!config->mqtt_host[0]) {
            modem.status.last_error = ML307_ERROR_CONFIG;
            modem.recovery_level = 0U;
            modem.next_action_ms = now + 300000UL;
            status_phase(ML307_PHASE_BACKOFF);
        } else if(!modem.waiting && reached(now, modem.next_action_ms) && !send_mqtt_config()) {
            enter_backoff(ML307_ERROR_CONFIG);
        }
        break;
    case ML307_PHASE_MQTT_CONNECT:
        if(!modem.waiting && reached(now, modem.next_action_ms) && !send_mqtt_connect())
            enter_backoff(ML307_ERROR_CONFIG);
        break;
    case ML307_PHASE_MQTT_SUBSCRIBE:
        if(!modem.waiting && reached(now, modem.next_action_ms) && !send_mqtt_subscribe())
            enter_backoff(ML307_ERROR_CONFIG);
        break;
    case ML307_PHASE_ONLINE:
        if(modem.management_pending ||
           (modem.report_requested && reached(now, modem.next_action_ms)) ||
           (uint32_t)(now - modem.status.last_report_ms) >= (uint32_t)config->report_interval_sec * 1000UL) {
            if(!start_publish()) enter_backoff(ML307_ERROR_CONFIG);
        } else if(reached(now, modem.next_clock_ms) && tx_command("AT+CCLK?\r\n")) {
            modem.network_command = 1U;
            modem.next_clock_ms = now + ML307_CLOCK_RETRY_MS;
            transition_wait(ML307_PHASE_SIGNAL_QUERY, ML307_COMMAND_TIMEOUT_MS);
        } else if((uint32_t)(now - modem.last_signal_ms) >= ML307_SIGNAL_INTERVAL_MS &&
                  tx_command("AT+CESQ\r\n")) {
            modem.network_command = 6U;
            transition_wait(ML307_PHASE_SIGNAL_QUERY, ML307_COMMAND_TIMEOUT_MS);
        }
        break;
    case ML307_PHASE_BACKOFF:
        if(reached(now, modem.next_action_ms)) {
            if(modem.recovery_level == 0U) {
                /* Missing Broker configuration is not a hardware fault.  Keep
                 * UART1 alive for the local AT console until configuration is saved. */
                modem.next_action_ms = now + 300000UL;
            } else if(modem.recovery_level == 1U) {
                if(!modem.uart_enabled) uart_enable();
                modem.mqtt_config_step = 0U;
                modem.subscribe_index = 0U;
                modem.next_action_ms = now;
                status_phase(ML307_PHASE_MQTT_CONFIG);
            } else if(modem.recovery_level == 2U) {
                if(!modem.uart_enabled) uart_enable();
                modem.network_started_ms = now;
                modem.next_action_ms = now;
                modem.status.network_registered = 0U;
                status_phase(ML307_PHASE_NETWORK);
            } else start_hardware_reset();
        }
        break;
    default:
        break;
    }
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
    if(!modem.uart_enabled) {
        UART1_Reset();
        PFIC_DisableIRQ(UART1_IRQn);
        return;
    }
    switch(UART1_GetITFlag()) {
    case UART_II_LINE_STAT:
        UART1_GetLinSTA();
        break;
    case UART_II_RECV_RDY:
    case UART_II_RECV_TOUT:
        while(R8_UART1_RFC) {
            uint8_t next = (uint8_t)((rx_head + 1U) % ML307_RX_RING_SIZE);
            uint8_t value = R8_UART1_RBR;
            modem.status.rx_bytes++;
            if(next == rx_tail) {
                if(modem.status.rx_overflow_count != 0xFFFFU)
                    modem.status.rx_overflow_count++;
                modem.status.last_error = ML307_ERROR_RX_OVERFLOW;
            } else {
                rx_ring[rx_head] = value;
                rx_head = next;
            }
        }
        break;
    case UART_II_THR_EMPTY:
        UART1_INTCfg(DISABLE, RB_IER_THR_EMPTY);
        break;
    default:
        break;
    }
}
