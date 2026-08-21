#ifndef SPLITAC_RELAY_CHILD_TABLE_H
#define SPLITAC_RELAY_CHILD_TABLE_H

#include <stdint.h>

#define RELAY_CHILD_TABLE_MAX 8U

typedef struct {
    uint32_t last_seen_ms;
    uint16_t node_id;
    uint8_t last_rssi;
    uint8_t online;
} relay_child_entry_t;

typedef struct {
    relay_child_entry_t entries[RELAY_CHILD_TABLE_MAX];
    uint8_t count;
    uint8_t capacity;
} relay_child_table_t;

void RelayChildTable_Init(relay_child_table_t *table, uint8_t capacity);
int8_t RelayChildTable_Find(const relay_child_table_t *table, uint16_t node_id);
int8_t RelayChildTable_Touch(relay_child_table_t *table,
                               uint16_t node_id,
                               uint8_t rssi,
                               uint32_t now_ms,
                               uint32_t timeout_ms);
uint8_t RelayChildTable_IsOnline(relay_child_table_t *table,
                                   uint8_t index,
                                   uint32_t now_ms,
                                   uint32_t timeout_ms);
uint8_t RelayChildTable_OnlineCount(relay_child_table_t *table,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms);
uint16_t RelayChildTable_OnlineBitmap(relay_child_table_t *table,
                                        uint32_t now_ms,
                                        uint32_t timeout_ms);

#endif
