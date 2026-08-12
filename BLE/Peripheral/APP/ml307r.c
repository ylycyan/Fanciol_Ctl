#include "ml307r.h"
#include "ml307r_codec.h"
#include "config_store_v2.h"
#include "protocol_v2.h"
#include "splitac_service_v2.h"
#include "health_v2.h"
#include "hlw8110.h"
#include "timer.h"
#include "time_v2.h"
#include "board.h"
#include "CH58x_common.h"
#include <string.h>

#define ML307_RESET_PIN            GPIO_Pin_22
#define ML307_RX_RING_SIZE         128U
#define ML307_LINE_SIZE            128U
#define ML307_TX_SIZE              176U
#define ML307_RESET_LOW_MS         350U
#define ML307_BOOT_WAIT_MS         6000U
#define ML307_COMMAND_TIMEOUT_MS   3000U
#define ML307_NETWORK_TIMEOUT_MS   120000U
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

typedef struct {
    ml307_status_v2_t status;
    uint32_t deadline_ms;
    uint32_t next_action_ms;
    uint32_t network_started_ms;
    uint32_t last_signal_ms;
    uint32_t next_clock_ms;
    uint32_t last_command_id;
    uint32_t pending_command_id;
    uint32_t publishing_command_id;
    ml307_command_v2_t queued_command;
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
    uint8_t last_command_result;
    uint8_t last_command_valid;
    uint8_t queued_command_valid;
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
    uint32_t at_started_ms;
    uint16_t at_start_tx_bytes;
    uint16_t at_start_rx_bytes;
    ml307_at_status_v2_t at_status;
} ml307_context_v2_t;

extern volatile uint32_t CurTick;

static ml307_context_v2_t modem;
static volatile uint8_t rx_ring[ML307_RX_RING_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static char rx_line[ML307_LINE_SIZE];
static char tx_buffer[ML307_TX_SIZE];

static uint8_t reached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
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

static void status_phase(ml307_phase_v2_t phase)
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
    modem.tx_length = 0U;
    modem.tx_offset = 0U;
}

static void start_hardware_reset(void)
{
    discard_at_session();
    uart_disable();
    reset_runtime_flags();
    GPIOB_ResetBits(ML307_RESET_PIN);
    /* RESET is a 1.8 V-domain, active-low input. Drive only the low level and
     * release it as a floating input to emulate the open-drain circuit
     * recommended by the module hardware guide. */
    GPIOB_ModeCfg(ML307_RESET_PIN, GPIO_ModeOut_PP_5mA);
    if(modem.status.reset_count != 0xFFFFU) modem.status.reset_count++;
    modem.deadline_ms = CurTick + ML307_RESET_LOW_MS;
    status_phase(ML307_PHASE_RESETTING);
}

static void enter_backoff(ml307_error_v2_t error)
{
    uint32_t delay;
    uart_disable();
    modem.status.mqtt_online = 0U;
    modem.status.last_error = (uint8_t)error;
    if(error == ML307_ERROR_TIMEOUT && modem.status.timeout_count != 0xFFFFU)
        modem.status.timeout_count++;
    if(modem.status.consecutive_failures < 255U) modem.status.consecutive_failures++;
    delay = 5000UL << (modem.status.consecutive_failures > 7U ? 6U :
                       modem.status.consecutive_failures - 1U);
    if(delay > 300000UL) delay = 300000UL;
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

static uint8_t tx_append_hex16(uint16_t *length, uint16_t value)
{
    int8_t shift;
    for(shift = 12; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)(value >> shift) & 0x0FU;
        if(!tx_append_char(length, (char)(digit < 10U ? '0' + digit : 'A' + digit - 10U)))
            return 0U;
    }
    return 1U;
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

static void transition_wait(ml307_phase_v2_t phase, uint32_t timeout_ms)
{
    modem.waiting = 1U;
    modem.response_seen = 0U;
    modem.deadline_ms = CurTick + timeout_ms;
    status_phase(phase);
}

static uint8_t send_apn(void)
{
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    uint16_t length = 0U;
    if(!config->apn[0]) return 0U;
    if(!tx_append_text(&length, "AT+CGDCONT=1,\"IP\",\"") ||
       !tx_append_text(&length, config->apn) ||
       !tx_append_text(&length, "\"\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_APN, ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_config(void)
{
    uint16_t length = 0U;
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    if(!tx_append_text(&length, "AT+MQTTCFG=\"keepalive\",0,") ||
       !tx_append_u32(&length, config->mqtt_keepalive_sec) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_MQTT_CONFIG, ML307_COMMAND_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_connect(void)
{
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MQTTCONN=0,\"") ||
       !tx_append_text(&length, config->mqtt_host) ||
       !tx_append_text(&length, "\",") || !tx_append_u32(&length, config->mqtt_port) ||
       !tx_append_text(&length, ",\"") ||
       !(config->mqtt_client_id[0] ? tx_append_text(&length, config->mqtt_client_id) :
                                    (tx_append_text(&length, "splitac-") && tx_append_hex16(&length, Dev.nodeId))) ||
       !tx_append_text(&length, "\",\"") || !tx_append_text(&length, config->mqtt_username) ||
       !tx_append_text(&length, "\",\"") || !tx_append_text(&length, config->mqtt_password) ||
       !tx_append_text(&length, "\"\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_MQTT_CONNECT, ML307_MQTT_TIMEOUT_MS);
    return 1U;
}

static uint8_t send_mqtt_subscribe(void)
{
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    uint16_t length = 0U;
    if(!tx_append_text(&length, "AT+MQTTSUB=0,\"") ||
       !tx_append_text(&length, config->subscribe_topic) ||
       !tx_append_text(&length, "\",") || !tx_append_u32(&length, config->mqtt_qos) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    transition_wait(ML307_PHASE_MQTT_SUBSCRIBE, ML307_MQTT_TIMEOUT_MS);
    return 1U;
}

static void fill_report(ml307_report_v2_t *report)
{
    const HLW8110_Status_t *meter = HLW8110_GetStatus();
    memset(report, 0, sizeof(*report));
    report->node_id = Dev.nodeId;
    report->timestamp = LocalTimestamp;
    report->power = Dev.onOff == PowerOn ? 1U : 0U;
    report->mode = (uint8_t)Dev.ctlMode;
    report->set_temp_x10 = (uint16_t)(Dev.temSet * 10U);
    report->room_temp_x10 = Dev.roomTempX10;
    report->fan = (uint8_t)Dev.wind;
    report->run_minutes = Dev.meter.run_minutes;
    report->power_w_x10 = meter->power_w_x10;
    report->energy_wh = Dev.meter.energy_wh;
    report->fault_code = Dev.errorCode.u16Val;
    report->has_command_result = modem.publishing_command_valid;
    report->command_id = modem.publishing_command_id;
    report->command_result = modem.publishing_command_result;
}

static uint8_t start_publish(void)
{
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    ml307_report_v2_t report;
    uint16_t payload_length;
    uint16_t length = 0U;

    modem.publishing_command_valid = modem.pending_command_valid;
    modem.publishing_command_id = modem.pending_command_id;
    modem.publishing_command_result = modem.pending_command_result;
    fill_report(&report);
    payload_length = Ml307Codec_BuildReport(&report, 0, 0U);
    if(payload_length == 0U || payload_length >= ML307_TX_SIZE) return 0U;
    if(!tx_append_text(&length, "AT+MQTTPUB=0,\"") ||
       !tx_append_text(&length, config->publish_topic) ||
       !tx_append_text(&length, "\",") || !tx_append_u32(&length, config->mqtt_qos) ||
       !tx_append_text(&length, ",0,0,") || !tx_append_u32(&length, payload_length) ||
       !tx_append_text(&length, "\r\n") || !tx_start(length)) return 0U;
    modem.report_requested = 0U;
    modem.prompt_seen = 0U;
    transition_wait(ML307_PHASE_PUBLISH, 5000U);
    return 1U;
}

static uint8_t send_publish_payload(void)
{
    ml307_report_v2_t report;
    uint16_t length;
    fill_report(&report);
    length = Ml307Codec_BuildReport(&report, tx_buffer, sizeof(tx_buffer));
    if(!length || !tx_start(length)) return 0U;
    modem.prompt_seen = 1U;
    modem.deadline_ms = CurTick + 10000U;
    return 1U;
}

static uint8_t topic_matches(const ml307_publish_v2_t *publish)
{
    const char *expected = ConnectivityV2_Get()->subscribe_topic;
    uint16_t length = bounded_length(expected, CONNECTIVITY_TOPIC_SIZE);
    return length == publish->topic_length &&
           memcmp(expected, publish->topic, length) == 0;
}

static void execute_command(const ml307_command_v2_t *command)
{
    uint8_t result;
    if(modem.last_command_valid && command->command_id == modem.last_command_id) {
        if(modem.status.command_duplicate_count != 0xFFFFU)
            modem.status.command_duplicate_count++;
        result = modem.last_command_result;
    } else {
        /* MQTT retries must remain idempotent: relative temperature and field
         * learning channels are deliberately unavailable over the broker. */
        result = command->operation >= 13U ? V2_STATUS_NOT_SUPPORTED :
                 SplitAcControl_Execute(command->operation, command->value);
        modem.last_command_id = command->command_id;
        modem.last_command_result = result;
        modem.last_command_valid = 1U;
        if(result == V2_STATUS_OK) {
            if(modem.status.command_executed_count != 0xFFFFU)
                modem.status.command_executed_count++;
        } else if(modem.status.command_rejected_count != 0xFFFFU)
            modem.status.command_rejected_count++;
    }
    modem.pending_command_id = command->command_id;
    modem.pending_command_result = result;
    modem.pending_command_valid = 1U;
    modem.report_requested = 1U;
}

static void handle_downlink(const ml307_publish_v2_t *publish)
{
    ml307_command_v2_t command;
    if(!topic_matches(publish)) return;
    if(!Ml307Codec_ParseCommand(publish->payload, publish->payload_length, &command)) {
        if(modem.status.command_rejected_count != 0xFFFFU)
            modem.status.command_rejected_count++;
        modem.status.last_error = ML307_ERROR_COMMAND;
        return;
    }
    /* The module acknowledges broker delivery independently from our result
     * publish, so keep one bounded waiting command instead of assuming that a
     * busy-time command will be redelivered. */
    if(modem.pending_command_valid &&
       (!modem.last_command_valid || command.command_id != modem.last_command_id)) {
        if(!modem.queued_command_valid) {
            modem.queued_command = command;
            modem.queued_command_valid = 1U;
        } else if(command.command_id == modem.queued_command.command_id) {
            if(modem.status.command_duplicate_count != 0xFFFFU)
                modem.status.command_duplicate_count++;
        } else {
            if(modem.status.command_rejected_count != 0xFFFFU)
                modem.status.command_rejected_count++;
            modem.status.last_error = ML307_ERROR_COMMAND;
        }
        return;
    }
    execute_command(&command);
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

static void parse_network_clock(const char *line, uint16_t length)
{
    ml307_clock_v2_t clock;
    time_v2_fields_t fields;
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
    if(!TimeV2_ToUnix(&fields, &local_timestamp)) return;
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
    modem.status.last_report_ms = CurTick;
    if(modem.publishing_command_valid && modem.pending_command_valid &&
       modem.publishing_command_id == modem.pending_command_id)
        modem.pending_command_valid = 0U;
    modem.publishing_command_valid = 0U;
    modem.waiting = 0U;
    status_phase(ML307_PHASE_ONLINE);
    if(!modem.pending_command_valid && modem.queued_command_valid) {
        ml307_command_v2_t command = modem.queued_command;
        modem.queued_command_valid = 0U;
        execute_command(&command);
    }
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

static void complete_mqtt_connection(void)
{
    modem.waiting = 0U;
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

static void at_finish(ml307_at_state_v2_t state)
{
    modem.at_status.state = (uint8_t)state;
    modem.at_status.elapsed_ms = CurTick - modem.at_started_ms;
    status_phase((ml307_phase_v2_t)modem.at_previous_phase);
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
    ml307_publish_v2_t publish;
    int8_t publish_status;
    uint8_t phase = modem.status.phase;
    (void)length;

    if(at_process_line(line, length)) return;

    publish_status = Ml307Codec_ParsePublish(line, length, &publish);
    if(publish_status == ML307_CODEC_OK) {
        handle_downlink(&publish);
        return;
    }
    if(publish_status == ML307_CODEC_FRAGMENTED) {
        modem.status.last_error = ML307_ERROR_COMMAND;
        return;
    }
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
        } else enter_backoff(ML307_ERROR_MQTT);
        return;
    }
    if(strstr(line, "+MQTTURC: \"timeout\",0,") != 0) {
        enter_backoff(ML307_ERROR_MQTT);
        return;
    }
    if(strstr(line, "+MQTTURC: \"suback\",0,") != 0) {
        uint16_t result;
        /* suback fields: connect_id, mid, result, qos. */
        if(!mqtt_urc_number(line, 2U, &result) || result != 0U) {
            enter_backoff(ML307_ERROR_MQTT);
            return;
        }
        modem.status.mqtt_online = 1U;
        modem.status.last_connected_ms = CurTick;
        modem.status.consecutive_failures = 0U;
        modem.status.last_error = ML307_ERROR_NONE;
        modem.report_requested = 1U;
        modem.waiting = 0U;
        status_phase(ML307_PHASE_ONLINE);
        return;
    }
    if(strstr(line, "+MQTTURC: \"puback\",0,") != 0) {
        if(phase == ML307_PHASE_PUBLISH && modem.prompt_seen &&
           ConnectivityV2_Get()->mqtt_qos == 1U)
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
        else if(ConnectivityV2_Get()->apn[0]) {
            if(!send_apn()) enter_backoff(ML307_ERROR_CONFIG);
        } else {
            modem.network_started_ms = CurTick;
            modem.next_action_ms = CurTick;
            status_phase(ML307_PHASE_NETWORK);
        }
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
        modem.next_action_ms = CurTick;
        status_phase(ML307_PHASE_MQTT_CONNECT);
        break;
    case ML307_PHASE_PUBLISH:
        if(modem.prompt_seen) {
            if(ConnectivityV2_Get()->mqtt_qos == 0U) complete_publish();
            else {
                /* QoS 1 只有收到 puback 才算完成；OK 仅表示命令已处理。 */
                modem.response_seen = 1U;
                modem.waiting = 1U;
            }
        }
        break;
    case ML307_PHASE_SIGNAL_QUERY:
        if(modem.network_command == 2U) modem.last_signal_ms = CurTick;
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
    modem.status.signal_rssi = -127;
    modem.at_status.state = ML307_AT_IDLE;
    rx_head = 0U;
    rx_tail = 0U;
    if(!ConnectivityV2_CellularEnabled()) {
        status_phase(ML307_PHASE_DISABLED);
        PRINT("ML307 init: configured=0, hardware untouched\r\n");
        return;
    }
    GPIOB_SetBits(ML307_RESET_PIN);
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
    modem.last_command_valid = 0U;
    modem.queued_command_valid = 0U;
    modem.report_requested = 0U;
    if(!ConnectivityV2_CellularEnabled()) {
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
    if(ConnectivityV2_CellularEnabled()) start_hardware_reset();
}

void Ml307_RequestReport(void)
{
    modem.report_requested = 1U;
}

uint8_t Ml307_IsOnline(void)
{
    return ConnectivityV2_CellularEnabled() && modem.status.mqtt_online;
}

const ml307_status_v2_t *Ml307_GetStatus(void)
{
    modem.status.uart_active = modem.uart_enabled;
    modem.status.waiting = modem.waiting;
    modem.status.retry_remaining_ms =
        modem.status.phase == ML307_PHASE_BACKOFF && !reached(CurTick, modem.next_action_ms)
        ? modem.next_action_ms - CurTick : 0U;
    return &modem.status;
}

uint8_t Ml307_AtStart(const uint8_t *command, uint8_t length)
{
    uint8_t index;
    if(!command || length < 2U || length > ML307_AT_COMMAND_SIZE) return V2_STATUS_INVALID_ARG;
    if(!ConnectivityV2_CellularEnabled()) return V2_STATUS_CONFLICT;
    if(modem.at_status.state == ML307_AT_RUNNING || modem.tx_length != 0U)
        return V2_STATUS_BUSY;
    if(!at_phase_available(modem.status.phase)) return V2_STATUS_BUSY;
    if(modem.status.phase == ML307_PHASE_BACKOFF && !modem.uart_enabled) uart_enable();
    if(!modem.uart_enabled) return V2_STATUS_CONFLICT;
    if((command[0] != 'A' && command[0] != 'a') ||
       (command[1] != 'T' && command[1] != 't')) return V2_STATUS_INVALID_ARG;
    for(index = 0U; index < length; index++)
        if(command[index] < 0x20U || command[index] > 0x7EU)
            return V2_STATUS_INVALID_ARG;

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
    if(!tx_start((uint16_t)length + 2U)) return V2_STATUS_BUSY;
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
    return V2_STATUS_OK;
}

uint8_t Ml307_AtCancel(void)
{
    if(modem.at_status.state != ML307_AT_RUNNING) return V2_STATUS_INVALID_ARG;
    at_finish(ML307_AT_CANCELLED);
    return V2_STATUS_OK;
}

const ml307_at_status_v2_t *Ml307_AtGetStatus(void)
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
    const connectivity_config_v2_t *config = ConnectivityV2_Get();
    HealthV2_Mark(HEALTH_V2_CELLULAR);

    if(!ConnectivityV2_CellularEnabled()) {
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

    switch((ml307_phase_v2_t)modem.status.phase) {
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
        if((uint32_t)(now - modem.network_started_ms) >= ML307_NETWORK_TIMEOUT_MS)
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
        if(!modem.waiting && reached(now, modem.next_action_ms) && !send_mqtt_config())
            enter_backoff(ML307_ERROR_CONFIG);
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
        if(modem.report_requested ||
           (uint32_t)(now - modem.status.last_report_ms) >= (uint32_t)config->report_interval_sec * 1000UL) {
            if(!start_publish()) enter_backoff(ML307_ERROR_CONFIG);
        } else if(reached(now, modem.next_clock_ms) && tx_command("AT+CCLK?\r\n")) {
            modem.network_command = 1U;
            modem.next_clock_ms = now + ML307_CLOCK_RETRY_MS;
            transition_wait(ML307_PHASE_SIGNAL_QUERY, ML307_COMMAND_TIMEOUT_MS);
        } else if((uint32_t)(now - modem.last_signal_ms) >= ML307_SIGNAL_INTERVAL_MS &&
                  tx_command("AT+CSQ\r\n")) {
            modem.network_command = 2U;
            transition_wait(ML307_PHASE_SIGNAL_QUERY, ML307_COMMAND_TIMEOUT_MS);
        }
        break;
    case ML307_PHASE_BACKOFF:
        if(reached(now, modem.next_action_ms)) start_hardware_reset();
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
