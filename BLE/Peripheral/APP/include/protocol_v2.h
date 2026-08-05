#ifndef SPLITAC_PROTOCOL_V2_H
#define SPLITAC_PROTOCOL_V2_H

#include <stdint.h>

#define V2_BLE_MAGIC             0xA5u
#define V2_PROTOCOL_VERSION      0x02u
#define V2_BLE_HEADER_SIZE       9u
#define V2_BLE_OVERHEAD          11u
#define V2_FRAGMENT_HEADER_SIZE  5u
#define V2_MAX_PAYLOAD           240u
#define V2_MAX_FRAME_SIZE        (V2_MAX_PAYLOAD + V2_BLE_OVERHEAD)
#define V2_MAX_FRAGMENTS         20u
#define V2_MAX_FRAGMENT_CHUNK    246u

typedef enum {
    V2_FRAME_REQUEST = 1,
    V2_FRAME_RESPONSE = 2,
    V2_FRAME_EVENT = 3
} v2_frame_type_t;

typedef enum {
    V2_STATUS_OK = 0,
    V2_STATUS_INVALID_ARG = 1,
    V2_STATUS_UNAUTHORIZED = 2,
    V2_STATUS_BUSY = 3,
    V2_STATUS_NOT_SUPPORTED = 4,
    V2_STATUS_CONFLICT = 5,
    V2_STATUS_IO_ERROR = 6,
    V2_STATUS_TIMEOUT = 7,
    V2_STATUS_VERIFY_FAILED = 8
} v2_status_t;

typedef enum {
    V2_OP_GET_CAPABILITIES = 0x01,
    V2_OP_GET_DEVICE_INFO = 0x02,
    V2_OP_AUTH_BEGIN = 0x03,
    V2_OP_AUTH_PROVE = 0x04,
    V2_OP_IDENTIFY = 0x05,
    V2_OP_GET_STATE = 0x10,
    V2_OP_GET_CONFIG = 0x11,
    V2_OP_VALIDATE_CONFIG = 0x12,
    V2_OP_COMMIT_CONFIG = 0x13,
    V2_OP_EXEC_CONTROL = 0x20,
    V2_OP_GET_RULES = 0x30,
    V2_OP_SET_RULES = 0x31,
    V2_OP_IR_CONFIG = 0x40,
    V2_OP_IR_ACTION = 0x41,
    V2_OP_GET_DIAGNOSTICS = 0x50,
    V2_OP_FACTORY_RESET = 0x51,
    V2_OP_GET_LORA_PARAMS = 0x52,
    V2_OP_SET_LORA_PARAMS = 0x53,
    V2_OP_GET_HEALTH_HISTORY = 0x54,
    V2_OP_OTA_BEGIN = 0x60,
    V2_OP_OTA_CHUNK = 0x61,
    V2_OP_OTA_FINISH = 0x62,
    V2_OP_CONTROL_RESULT_EVENT = 0xA0,
    V2_OP_STATE_CHANGED_EVENT = 0xA1,
    V2_OP_LOG_EVENT = 0xA2
} v2_opcode_t;

/* EXEC_CONTROL 固定载荷: control:u8 | value:u16LE */
typedef enum {
    V2_CONTROL_POWER = 1,
    V2_CONTROL_MODE = 2,
    V2_CONTROL_TEMPERATURE = 3,
    V2_CONTROL_FAN = 4,
    V2_CONTROL_WIND_DIRECTION = 5,
    V2_CONTROL_WIND_AUTO = 6,
    V2_CONTROL_SLEEP = 7,
    V2_CONTROL_AUX_HEAT = 8,
    V2_CONTROL_LIGHT = 9,
    V2_CONTROL_ENERGY = 10,
    V2_CONTROL_FAST_MODE = 11,
    V2_CONTROL_MUTE = 12,
    V2_CONTROL_TEMP_STEP = 13,
    /* 仅 BLE 现场控制使用：value 为已学习通道 0~9。 */
    V2_CONTROL_LEARNED_CHANNEL = 14
} v2_control_t;

typedef struct {
    uint8_t type;
    uint16_t seq;
    uint8_t opcode;
    uint8_t status;
    uint16_t payload_len;
    const uint8_t *payload;
} v2_ble_frame_t;

typedef struct {
    uint8_t active;
    uint16_t seq;
    uint8_t fragment_count;
    uint32_t received_mask;
    uint16_t stored_len;
    uint16_t part_len[V2_MAX_FRAGMENTS];
    uint16_t part_offset[V2_MAX_FRAGMENTS];
    uint8_t data[V2_MAX_FRAME_SIZE];
} v2_ble_reassembler_t;

uint16_t V2_Crc16(const uint8_t *data, uint16_t len);
uint8_t V2_BleEncode(const v2_ble_frame_t *frame, uint8_t *out, uint16_t capacity, uint16_t *out_len);
uint8_t V2_BleDecode(const uint8_t *data, uint16_t len, v2_ble_frame_t *frame);
void V2_ReassemblerReset(v2_ble_reassembler_t *ctx);
uint8_t V2_ReassemblerPush(v2_ble_reassembler_t *ctx, const uint8_t *fragment, uint16_t len,
                           uint8_t *frame, uint16_t capacity, uint16_t *frame_len);

#endif
