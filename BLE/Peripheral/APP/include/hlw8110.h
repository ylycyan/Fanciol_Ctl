#ifndef __HLW8110_H__
#define __HLW8110_H__

#include <stdint.h>

typedef enum {
    HLW8110_ERROR_NONE = 0,
    HLW8110_ERROR_TX_BUSY = 1,
    HLW8110_ERROR_UART_LINE = 2,
    HLW8110_ERROR_RX_LENGTH = 3,
    HLW8110_ERROR_ALL_FF = 4,
    HLW8110_ERROR_CHECKSUM = 5,
    HLW8110_ERROR_TIMEOUT = 6,
    HLW8110_ERROR_CONFIG_VERIFY = 7,
    HLW8110_ERROR_COEFFICIENT = 8,
    HLW8110_ERROR_CURRENT_RANGE = 9,
    HLW8110_ERROR_VOLTAGE_RANGE = 10,
    HLW8110_ERROR_STATE = 11
} HLW8110_Error_t;

typedef struct {
    uint16_t voltage_dv;          /* 电压，0.1 V */
    uint16_t current_ma;          /* 电流，mA */
    uint16_t power_w_x10;         /* 有功功率，0.1 W */
    uint16_t communication_errors;
    uint32_t last_sample_ms;
    uint8_t valid;
    uint8_t consecutive_errors;
    uint8_t last_error_reason;
    uint8_t last_error_state;
    uint8_t last_error_register;
} HLW8110_Status_t;

/* UART0: PB4/RX、PB7/TX，9600 8E1。所有处理均为非阻塞状态机。 */
void HLW8110_Init(void);
void HLW8110_Poll(void);
const HLW8110_Status_t *HLW8110_GetStatus(void);

#endif
