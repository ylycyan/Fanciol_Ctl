/**
 * @file device_protocol.c
 * @brief BLE device protocol 协议帧编解码与分片重组
 *
 * 逻辑帧格式（DEVICE_MAX_FRAME_SIZE = 251 字节）：
 *   [MAGIC 0xA5][Ver 0x02][Type][Seq(2)][Opcode][Status][PayloadLen(2)][Payload][CRC16(2)]
 * 物理链路（CHAR1）按 MTU 分片，每片头 5 字节：flags + index + count + seq(2)。
 * CRC16 为 CCITT-1021，覆盖 CRC 之前全部字节。
 */
#include "device_protocol.h"
#include <string.h>

static uint16_t get_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static void put_u16(uint8_t *p, uint16_t value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }

/**
 * @brief 计算 CCITT-1021 CRC16（初始值 0xFFFF）
 */
uint16_t DeviceProtocol_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t bit;
    for(i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0; bit < 8; ++bit) crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/**
 * @brief 编码逻辑帧（含头、payload、CRC）
 */
uint8_t DeviceProtocol_Encode(const device_frame_t *f, uint8_t *out, uint16_t cap, uint16_t *out_len)
{
    uint16_t total;
    uint16_t crc;
    if(!f || !out || !out_len || f->payload_len > DEVICE_MAX_PAYLOAD || (f->payload_len && !f->payload)) return DEVICE_STATUS_INVALID_ARG;
    total = (uint16_t)(DEVICE_FRAME_OVERHEAD + f->payload_len);
    if(cap < total) return DEVICE_STATUS_INVALID_ARG;
    out[0] = DEVICE_FRAME_MAGIC; out[1] = DEVICE_PROTOCOL_VERSION; out[2] = f->type;
    put_u16(out + 3, f->seq); out[5] = f->opcode; out[6] = f->status;
    put_u16(out + 7, f->payload_len);
    if(f->payload_len) memcpy(out + 9, f->payload, f->payload_len);
    crc = DeviceProtocol_Crc16(out, (uint16_t)(total - 2));
    put_u16(out + total - 2, crc); *out_len = total;
    return DEVICE_STATUS_OK;
}

/**
 * @brief 解码逻辑帧：校验 MAGIC/版本/长度/CRC
 */
uint8_t DeviceProtocol_Decode(const uint8_t *data, uint16_t len, device_frame_t *f)
{
    uint16_t payload_len;
    if(!data || !f || len < DEVICE_FRAME_OVERHEAD) return DEVICE_STATUS_INVALID_ARG;
    if(data[0] != DEVICE_FRAME_MAGIC || data[1] != DEVICE_PROTOCOL_VERSION) return DEVICE_STATUS_INVALID_ARG;
    payload_len = get_u16(data + 7);
    if(payload_len > DEVICE_MAX_PAYLOAD || len != (uint16_t)(DEVICE_FRAME_OVERHEAD + payload_len)) return DEVICE_STATUS_INVALID_ARG;
    if(DeviceProtocol_Crc16(data, (uint16_t)(len - 2)) != get_u16(data + len - 2)) return DEVICE_STATUS_VERIFY_FAILED;
    f->type = data[2]; f->seq = get_u16(data + 3); f->opcode = data[5]; f->status = data[6];
    f->payload_len = payload_len; f->payload = data + 9;
    return DEVICE_STATUS_OK;
}

/**
 * @brief 清空分片重组上下文（连接断开/协议错误时调用）
 */
void DeviceProtocol_Reset(device_reassembler_t *ctx) { if(ctx) memset(ctx, 0, sizeof(*ctx)); }

/**
 * @brief 接收一个物理分片并尝试重组完整逻辑帧
 *
 * - 校验分片头：flags 首片/末片位、index<count<=20、块长上限
 * - 新序号/新分片数时重置上下文
 * - 所有分片到齐后按 offset 拼接成完整帧（容量校验）
 *
 * @retval DEVICE_STATUS_BUSY 未到齐，继续等；DEVICE_STATUS_OK 重组完成（*frame_len 为长度）
 */
uint8_t DeviceProtocol_Reassemble(device_reassembler_t *ctx, const uint8_t *frag, uint16_t len,
                           uint8_t *frame, uint16_t cap, uint16_t *frame_len)
{
    uint8_t index;
    uint8_t count;
    uint16_t seq;
    uint16_t chunk_len;
    uint16_t total = 0;
    uint8_t i;
    if(!ctx || !frag || !frame || !frame_len || len < DEVICE_FRAGMENT_HEADER_SIZE) return DEVICE_STATUS_INVALID_ARG;
    index = frag[1]; count = frag[2]; seq = get_u16(frag + 3); chunk_len = (uint16_t)(len - 5);
    if(!count || count > DEVICE_MAX_FRAGMENTS || index >= count || chunk_len > DEVICE_MAX_FRAGMENT_CHUNK) return DEVICE_STATUS_INVALID_ARG;
    if((frag[0] & 0x3Fu) != 0u || ((frag[0] & 0x80u) != 0u) != (index == 0u) ||
       ((frag[0] & 0x40u) != 0u) != (index == (uint8_t)(count - 1u))) return DEVICE_STATUS_INVALID_ARG;
    if(!ctx->active || ctx->seq != seq || ctx->fragment_count != count) {
        DeviceProtocol_Reset(ctx); ctx->active = 1; ctx->seq = seq; ctx->fragment_count = count;
    }
    if(!(ctx->received_mask & (1UL << index))) {
        if((uint16_t)(ctx->stored_len + chunk_len) > sizeof(ctx->data)) { DeviceProtocol_Reset(ctx); return DEVICE_STATUS_INVALID_ARG; }
        ctx->part_offset[index] = ctx->stored_len;
        memcpy(ctx->data + ctx->stored_len, frag + 5, chunk_len);
        ctx->stored_len = (uint16_t)(ctx->stored_len + chunk_len);
        ctx->part_len[index] = chunk_len;
        ctx->received_mask |= 1UL << index;
    }
    for(i = 0; i < count; ++i) if(!(ctx->received_mask & (1UL << i))) return DEVICE_STATUS_BUSY;
    for(i = 0; i < count; ++i) {
        if((uint16_t)(total + ctx->part_len[i]) > cap) { DeviceProtocol_Reset(ctx); return DEVICE_STATUS_INVALID_ARG; }
        memcpy(frame + total, ctx->data + ctx->part_offset[i], ctx->part_len[i]); total = (uint16_t)(total + ctx->part_len[i]);
    }
    *frame_len = total; DeviceProtocol_Reset(ctx); return DEVICE_STATUS_OK;
}
