#ifndef SPLITAC_DEVICE_PROTOCOL_H
#define SPLITAC_DEVICE_PROTOCOL_H

#include <stdint.h>

#define DEVICE_FRAME_MAGIC             0xA5u
#define DEVICE_PROTOCOL_VERSION      0x02u
#define DEVICE_FRAME_HEADER_SIZE       9u
#define DEVICE_FRAME_OVERHEAD          11u
#define DEVICE_FRAGMENT_HEADER_SIZE  5u
#define DEVICE_MAX_PAYLOAD           240u
#define DEVICE_MAX_FRAME_SIZE        (DEVICE_MAX_PAYLOAD + DEVICE_FRAME_OVERHEAD)
#define DEVICE_MAX_FRAGMENTS         17u
#define DEVICE_MAX_FRAGMENT_CHUNK    246u

typedef enum {
    DEVICE_FRAME_REQUEST = 1,
    DEVICE_FRAME_RESPONSE = 2,
    DEVICE_FRAME_EVENT = 3
} device_frame_type_t;

typedef enum {
    DEVICE_STATUS_OK = 0,
    DEVICE_STATUS_INVALID_ARG = 1,
    DEVICE_STATUS_UNAUTHORIZED = 2,
    DEVICE_STATUS_BUSY = 3,
    DEVICE_STATUS_NOT_SUPPORTED = 4,
    DEVICE_STATUS_CONFLICT = 5,
    DEVICE_STATUS_IO_ERROR = 6,
    DEVICE_STATUS_TIMEOUT = 7,
    DEVICE_STATUS_VERIFY_FAILED = 8
} device_status_t;

typedef enum {
    DEVICE_OP_GET_CAPABILITIES = 0x01,
    DEVICE_OP_GET_DEVICE_INFO = 0x02,
    DEVICE_OP_AUTH_BEGIN = 0x03,
    DEVICE_OP_AUTH_PROVE = 0x04,
    DEVICE_OP_IDENTIFY = 0x05,
    DEVICE_OP_GET_STATE = 0x10,
    DEVICE_OP_GET_CONFIG = 0x11,
    DEVICE_OP_VALIDATE_CONFIG = 0x12,
    DEVICE_OP_COMMIT_CONFIG = 0x13,
    DEVICE_OP_EXEC_CONTROL = 0x20,
    DEVICE_OP_GET_RULES = 0x30,
    DEVICE_OP_SET_RULES = 0x31,
    DEVICE_OP_IR_CONFIG = 0x40,
    DEVICE_OP_IR_ACTION = 0x41,
    DEVICE_OP_GET_DIAGNOSTICS = 0x50,
    DEVICE_OP_FACTORY_RESET = 0x51,
    DEVICE_OP_GET_LORA_PARAMS = 0x52,
    DEVICE_OP_SET_LORA_PARAMS = 0x53,
    DEVICE_OP_GET_CONNECTIVITY_CONFIG = 0x55,
    DEVICE_OP_SET_CONNECTIVITY_CONFIG = 0x56,
    DEVICE_OP_GET_CONNECTIVITY_STATUS = 0x57,
    DEVICE_OP_RESTART_CELLULAR = 0x58,
    DEVICE_OP_CELLULAR_AT = 0x59,
    DEVICE_OP_SET_DEVICE_NAME = 0x5A,
    DEVICE_OP_RESTART_DEVICE = 0x5B,
    DEVICE_OP_GET_REMOTE_OTA_STATUS = 0x5F,
    DEVICE_OP_OTA_BEGIN = 0x60,
    DEVICE_OP_OTA_CHUNK = 0x61,
    DEVICE_OP_OTA_FINISH = 0x62,
    DEVICE_OP_CONTROL_RESULT_EVENT = 0xA0,
    DEVICE_OP_STATE_CHANGED_EVENT = 0xA1,
    DEVICE_OP_LOG_EVENT = 0xA2
} device_opcode_t;

/* EXEC_CONTROL 固定载荷: control:u8 | value:u16LE */
typedef enum {
    CONTROL_FIELD_POWER = 1,
    CONTROL_FIELD_MODE = 2,
    CONTROL_FIELD_TEMPERATURE = 3,
    CONTROL_FIELD_FAN = 4,
    CONTROL_FIELD_WIND_DIRECTION = 5,
    CONTROL_FIELD_WIND_AUTO = 6,
    CONTROL_FIELD_SLEEP = 7,
    CONTROL_FIELD_AUX_HEAT = 8,
    CONTROL_FIELD_LIGHT = 9,
    CONTROL_FIELD_ENERGY = 10,
    CONTROL_FIELD_FAST_MODE = 11,
    CONTROL_FIELD_MUTE = 12,
    CONTROL_FIELD_TEMP_STEP = 13,
    /* 仅 BLE 现场控制使用：value 为已学习通道 0~9。 */
    CONTROL_FIELD_LEARNED_CHANNEL = 14
} device_control_t;

typedef struct {
    uint8_t type;
    uint16_t seq;
    uint8_t opcode;
    uint8_t status;
    uint16_t payload_len;
    const uint8_t *payload;
} device_frame_t;

typedef struct {
    uint8_t active;
    uint16_t seq;
    uint8_t fragment_count;
    uint32_t received_mask;
    uint16_t stored_len;
    uint16_t part_len[DEVICE_MAX_FRAGMENTS];
    uint16_t part_offset[DEVICE_MAX_FRAGMENTS];
    uint8_t data[DEVICE_MAX_FRAME_SIZE];
} device_reassembler_t;

uint16_t DeviceProtocol_Crc16(const uint8_t *data, uint16_t len);
uint8_t DeviceProtocol_Encode(const device_frame_t *frame, uint8_t *out, uint16_t capacity, uint16_t *out_len);
uint8_t DeviceProtocol_Decode(const uint8_t *data, uint16_t len, device_frame_t *frame);
void DeviceProtocol_Reset(device_reassembler_t *ctx);
uint8_t DeviceProtocol_Reassemble(device_reassembler_t *ctx, const uint8_t *fragment, uint16_t len,
                           uint8_t *frame, uint16_t capacity, uint16_t *frame_len);

#endif
