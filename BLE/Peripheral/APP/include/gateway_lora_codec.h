#ifndef GATEWAY_LORA_CODEC_H
#define GATEWAY_LORA_CODEC_H

#include <stdint.h>

#define GATEWAY_LORA_MAX_PACKET       128U
#define GATEWAY_LORA_RELAY_BROADCAST  0xFFFFU
#define GATEWAY_LORA_MAX_CHANNEL      32U
#define GATEWAY_LORA_REGISTER_SF      9U
#define GATEWAY_LORA_REGISTER_BW      0x04U
#define GATEWAY_LORA_WORK_SF          10U
#define GATEWAY_LORA_WORK_BW          0x05U

/* 固定网关 eDeviceFancoil(20)：6 个值、11 字节数据，整包 18 字节。 */
#define GATEWAY_LORA_FANCOIL_VALUE_COUNT    6U
#define GATEWAY_LORA_FANCOIL_DATA_LENGTH   11U
#define GATEWAY_LORA_FANCOIL_REPORT_LENGTH 18U

typedef struct {
    uint16_t set_temperature_sf;  /* v0: 温度设定，small-float */
    uint8_t power_setting;        /* v1: 开关设定，0=关、1=开 */
    uint16_t room_temperature_sf; /* v2: 环境温度，small-float */
    uint16_t work_mode_sf;        /* v3: 工作模式，small-float */
    uint16_t fan_speed_sf;        /* v4: 风速档位，small-float */
    uint16_t run_feedback;        /* v5: 运行反馈，负载电流 mA */
} GatewayLoraFancoilState;

uint8_t GatewayLora_Checksum(const uint8_t *data, uint16_t length);
uint8_t GatewayLora_Validate(const uint8_t *packet, uint16_t length);
uint32_t GatewayLora_RegisterFrequencyHz(uint8_t channel);
uint32_t GatewayLora_WorkFrequencyHz(uint8_t channel);
uint8_t GatewayLora_EncodeSmallFloatX10(int16_t valueX10, uint16_t *encoded);
int16_t GatewayLora_DecodeSmallFloatX10(uint16_t encoded);
uint8_t GatewayLora_BuildFancoilReport(uint8_t *out,
                                      uint8_t tag,
                                      uint16_t nodeId,
                                      int8_t rssi,
                                      uint8_t errorInfo,
                                      const GatewayLoraFancoilState *state);

/*
 * 中继本身仍使用普通节点登录（role=0）；只有其子节点的本地登录
 * 才携带 role=1 和 parentRelayId。
 */
uint8_t GatewayLora_BuildNodeLogin(uint8_t *out, uint16_t nodeId);
uint8_t GatewayLora_BuildChildLogin(uint8_t *out,
                                    uint16_t nodeId,
                                    uint16_t parentRelayId);

uint8_t GatewayLora_BuildRelayInner(uint8_t *out,
                                    uint8_t direction,
                                    uint16_t relayId,
                                    uint16_t childId,
                                    uint8_t sequence,
                                    const uint8_t *payload,
                                    uint8_t payloadLength);

uint8_t GatewayLora_UnwrapRelayInnerInPlace(uint8_t *packet,
                                            uint8_t *length,
                                            uint8_t direction,
                                            uint16_t relayId,
                                            uint16_t childId,
                                            uint8_t *sequence);

#endif
