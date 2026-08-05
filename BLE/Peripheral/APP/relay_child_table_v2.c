#include "relay_child_table_v2.h"

#include <string.h>

static uint8_t entry_expired(const relay_child_entry_v2_t *entry,
                             uint32_t now_ms,
                             uint32_t timeout_ms)
{
    return !entry->online ||
           (uint32_t)(now_ms - entry->last_seen_ms) > timeout_ms;
}

void RelayChildTableV2_Init(relay_child_table_v2_t *table, uint8_t capacity)
{
    if(!table) return;
    memset(table, 0, sizeof(*table));
    table->capacity = capacity > RELAY_CHILD_TABLE_V2_MAX
        ? RELAY_CHILD_TABLE_V2_MAX
        : capacity;
}

int8_t RelayChildTableV2_Find(const relay_child_table_v2_t *table,
                              uint16_t node_id)
{
    uint8_t i;
    if(!table || node_id == 0U) return -1;
    for(i = 0; i < table->count; ++i) {
        if(table->entries[i].node_id == node_id) return (int8_t)i;
    }
    return -1;
}

int8_t RelayChildTableV2_Touch(relay_child_table_v2_t *table,
                               uint16_t node_id,
                               uint8_t rssi,
                               uint32_t now_ms,
                               uint32_t timeout_ms)
{
    int8_t index;
    int8_t victim = -1;
    uint32_t victim_age = 0U;
    uint8_t i;

    if(!table || node_id == 0U || table->capacity == 0U) return -1;
    index = RelayChildTableV2_Find(table, node_id);
    if(index < 0 && table->count < table->capacity) {
        index = (int8_t)table->count++;
    }
    if(index < 0) {
        /*
         * 表满时只回收已经离线的槽，并优先选择最久未出现者。
         * 这样更换子节点后无需重启中继，也不会挤掉仍在线的设备。
         */
        for(i = 0; i < table->count; ++i) {
            uint32_t age = (uint32_t)(now_ms - table->entries[i].last_seen_ms);
            if(entry_expired(&table->entries[i], now_ms, timeout_ms) &&
               (victim < 0 || age > victim_age)) {
                victim = (int8_t)i;
                victim_age = age;
            }
        }
        index = victim;
    }
    if(index < 0) return -1;

    table->entries[(uint8_t)index].node_id = node_id;
    table->entries[(uint8_t)index].last_seen_ms = now_ms;
    table->entries[(uint8_t)index].last_rssi = rssi;
    table->entries[(uint8_t)index].online = 1U;
    return index;
}

uint8_t RelayChildTableV2_IsOnline(relay_child_table_v2_t *table,
                                   uint8_t index,
                                   uint32_t now_ms,
                                   uint32_t timeout_ms)
{
    if(!table || index >= table->count) return 0U;
    if(entry_expired(&table->entries[index], now_ms, timeout_ms)) {
        table->entries[index].online = 0U;
        return 0U;
    }
    return 1U;
}

uint8_t RelayChildTableV2_OnlineCount(relay_child_table_v2_t *table,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms)
{
    uint8_t count = 0U;
    uint8_t i;
    if(!table) return 0U;
    for(i = 0; i < table->count; ++i) {
        if(RelayChildTableV2_IsOnline(table, i, now_ms, timeout_ms)) count++;
    }
    return count;
}

uint16_t RelayChildTableV2_OnlineBitmap(relay_child_table_v2_t *table,
                                        uint32_t now_ms,
                                        uint32_t timeout_ms)
{
    uint16_t bitmap = 0U;
    uint8_t i;
    if(!table) return 0U;
    for(i = 0; i < table->count && i < 16U; ++i) {
        if(RelayChildTableV2_IsOnline(table, i, now_ms, timeout_ms))
            bitmap |= (uint16_t)(1U << i);
    }
    return bitmap;
}
