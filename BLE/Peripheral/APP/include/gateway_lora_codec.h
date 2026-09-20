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

/*
 * 统一数据帧（LoRa 空口与 4G MQTT 完全相同）：
 *   CMD(1) + TAG(1) + NodeId(2) + RSSI(1) + errorInfo(1) + 数据(31) + CRC(1) = 38 字节
 * 数据区字段顺序：
 *   红外码(u16) 开关(u8) 运行模式(u8) 风速(u8) 温度(sf) 湿度(sf) 设定温度(sf)
 *   电压(float) 电流(float,A) 功率(float,W) 累计电量(float,kWh)
 *   statusCode1(u16,硬件状态) statusCode2(u16,保留)
 */
#define GATEWAY_LORA_REPORT_LENGTH      38U
#define GATEWAY_LORA_REPORT_DATA_LENGTH 31U

typedef struct {
    uint16_t ir_code;              /* 红外码，Dev.irType */
    uint8_t  power_setting;        /* 开关，0=关、1=开 */
    uint8_t  work_mode;            /* 运行模式 */
    uint8_t  fan_speed;            /* 风速档位 */
    int16_t  room_temperature_x10; /* 环境温度 ×10，编码为 small-float */
    uint16_t humidity_x10;         /* 湿度 ×10，编码为 small-float */
    uint16_t set_temperature_x10;  /* 设定温度 ×10，编码为 small-float */
    uint16_t voltage_dv;           /* 电压，0.1V；帧内编码为 IEEE754 小端 float(V) */
    uint16_t current_ma;           /* 电流，mA；帧内编码为 IEEE754 小端 float(A) */
    uint16_t power_w_x10;          /* 有功功率，0.1W；帧内编码为 float(W) */
    uint32_t energy_wh;            /* 累计电量，0.1kWh；帧内编码为 float(kWh) */
    uint16_t status_code1;         /* 硬件状态（沿用原 errorCode 位域） */
    uint16_t status_code2;         /* 保留 */
} GatewayLoraSplitAcState;

uint8_t GatewayLora_Checksum(const uint8_t *data, uint16_t length);
uint8_t GatewayLora_Validate(const uint8_t *packet, uint16_t length);
uint32_t GatewayLora_RegisterFrequencyHz(uint8_t channel);
uint32_t GatewayLora_WorkFrequencyHz(uint8_t channel);
uint8_t GatewayLora_EncodeSmallFloatX10(int16_t valueX10, uint16_t *encoded);
int16_t GatewayLora_DecodeSmallFloatX10(uint16_t encoded);
uint8_t GatewayLora_BuildSplitAcReport(uint8_t *out,
                                      uint8_t tag,
                                      uint16_t nodeId,
                                      int8_t rssi,
                                      uint8_t errorInfo,
                                      const GatewayLoraSplitAcState *state);

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
