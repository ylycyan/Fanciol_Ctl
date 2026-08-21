/**
 * @file relay_child_table.c
 * @brief 中继子节点在线表（RAM 表，不落 Flash）
 *
 * 记录子节点最近一次在线时间与 RSSI；超时后视为离线。
 * 表满时仅回收已离线槽位（优先最久未出现者），不挤掉在线设备。
 */
#include "relay_child_table.h"

#include <string.h>

/**
 * @brief 判断条目是否过期（从未在线或超过 timeout_ms 未见）
 */
static uint8_t entry_expired(const relay_child_entry_t *entry,
                             uint32_t now_ms,
                             uint32_t timeout_ms)
{
    return !entry->online ||
           (uint32_t)(now_ms - entry->last_seen_ms) > timeout_ms;
}

/**
 * @brief 初始化子节点表，capacity 超过上限时钳制
 */
void RelayChildTable_Init(relay_child_table_t *table, uint8_t capacity)
{
    if(!table) return;
    memset(table, 0, sizeof(*table));
    table->capacity = capacity > RELAY_CHILD_TABLE_MAX
        ? RELAY_CHILD_TABLE_MAX
        : capacity;
}

/**
 * @brief 按节点 ID 查找槽位
 * @return 槽位下标；未找到返回 -1
 */
int8_t RelayChildTable_Find(const relay_child_table_t *table,
                              uint16_t node_id)
{
    uint8_t i;
    if(!table || node_id == 0U) return -1;
    for(i = 0; i < table->count; ++i) {
        if(table->entries[i].node_id == node_id) return (int8_t)i;
    }
    return -1;
}

/**
 * @brief 更新/新建子节点在线记录（触摸）
 *
 * 已有则刷新；没有且未满则追加；表满时回收最久离线的槽位。
 *
 * @return 槽位下标；失败（表满且无离线槽）返回 -1
 */
int8_t RelayChildTable_Touch(relay_child_table_t *table,
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
    index = RelayChildTable_Find(table, node_id);
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

/**
 * @brief 查询槽位是否在线（过期则顺便标记离线）
 */
uint8_t RelayChildTable_IsOnline(relay_child_table_t *table,
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

/**
 * @brief 统计当前在线子节点数
 */
uint8_t RelayChildTable_OnlineCount(relay_child_table_t *table,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms)
{
    uint8_t count = 0U;
    uint8_t i;
    if(!table) return 0U;
    for(i = 0; i < table->count; ++i) {
        if(RelayChildTable_IsOnline(table, i, now_ms, timeout_ms)) count++;
    }
    return count;
}

/**
 * @brief 生成在线子节点位图（bit0=槽0，用于上报）
 */
uint16_t RelayChildTable_OnlineBitmap(relay_child_table_t *table,
                                        uint32_t now_ms,
                                        uint32_t timeout_ms)
{
    uint16_t bitmap = 0U;
    uint8_t i;
    if(!table) return 0U;
    for(i = 0; i < table->count && i < 16U; ++i) {
        if(RelayChildTable_IsOnline(table, i, now_ms, timeout_ms))
            bitmap |= (uint16_t)(1U << i);
    }
    return bitmap;
}
