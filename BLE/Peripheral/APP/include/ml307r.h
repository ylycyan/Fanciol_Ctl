#ifndef SPLITAC_ML307R_H
#define SPLITAC_ML307R_H

#include <stdint.h>

typedef enum {
    ML307_PHASE_DISABLED = 0,
    ML307_PHASE_RESETTING,
    ML307_PHASE_BOOTING,
    ML307_PHASE_AT_SYNC,
    ML307_PHASE_SIM,
    ML307_PHASE_APN,
    ML307_PHASE_NETWORK,
    ML307_PHASE_MQTT_CONFIG,
    ML307_PHASE_MQTT_CONNECT,
    ML307_PHASE_MQTT_SUBSCRIBE,
    ML307_PHASE_ONLINE,
    ML307_PHASE_PUBLISH,
    ML307_PHASE_SIGNAL_QUERY,
    ML307_PHASE_BACKOFF
} ml307_phase_t;

typedef enum {
    ML307_ERROR_NONE = 0,
    ML307_ERROR_CONFIG,
    ML307_ERROR_TIMEOUT,
    ML307_ERROR_MODEM,
    ML307_ERROR_SIM,
    ML307_ERROR_NETWORK,
    ML307_ERROR_MQTT,
    ML307_ERROR_RX_OVERFLOW,
    ML307_ERROR_COMMAND,
    ML307_ERROR_APN,
    ML307_ERROR_DNS,
    ML307_ERROR_BROKER_AUTH,
    ML307_ERROR_SUBSCRIBE,
    ML307_ERROR_PUBLISH,
    ML307_ERROR_UART,
    ML307_ERROR_HTTP,
    ML307_ERROR_OTA
} ml307_error_t;

typedef enum {
    ML307_AT_IDLE = 0,
    ML307_AT_RUNNING,
    ML307_AT_OK,
    ML307_AT_ERROR,
    ML307_AT_TIMEOUT,
    ML307_AT_CANCELLED
} ml307_at_state_t;

typedef struct {
    uint32_t elapsed_ms;
    uint16_t generation;
    uint8_t state;
    uint8_t truncated;
    uint8_t response_length;
} ml307_at_status_t;

typedef struct {
    uint32_t last_connected_ms;
    uint32_t last_report_ms;
    uint16_t reset_count;
    uint16_t publish_count;
    uint16_t command_executed_count;
    uint16_t command_rejected_count;
    uint16_t rx_overflow_count;
    uint16_t timeout_count;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t retry_remaining_ms;
    int8_t signal_rssi;
    int8_t rsrp_dbm;
    int16_t rsrq_db_x10;
    uint8_t phase;
    uint8_t sim_ready;
    uint8_t network_registered;
    uint8_t mqtt_online;
    uint8_t last_error;
    uint8_t consecutive_failures;
    uint8_t uart_active;
    uint8_t waiting;
} ml307_status_t;

void Ml307_Init(void);
void Ml307_Process(void);
void Ml307_ApplyConfiguration(void);
void Ml307_RequestReport(void);
void Ml307_Restart(void);
uint8_t Ml307_IsOnline(void);
const ml307_status_t *Ml307_GetStatus(void);
const char *Ml307_GetDeviceId(void);
uint8_t Ml307_AtStart(const uint8_t *command, uint8_t length);
uint8_t Ml307_AtCancel(void);
const ml307_at_status_t *Ml307_AtGetStatus(void);
uint8_t Ml307_AtCopyResponse(uint8_t *output, uint8_t capacity);
uint16_t Ml307_AtGetTxDelta(void);
uint16_t Ml307_AtGetRxDelta(void);

#endif
