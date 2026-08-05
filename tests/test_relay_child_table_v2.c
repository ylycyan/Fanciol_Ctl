#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "relay_child_table_v2.h"

static void test_full_table_reuses_only_expired_slots(void)
{
    relay_child_table_v2_t table;
    int8_t slot_a;
    int8_t slot_b;
    int8_t slot_c;

    RelayChildTableV2_Init(&table, 2U);
    slot_a = RelayChildTableV2_Touch(&table, 0x1001U, 40U, 0U, 100U);
    slot_b = RelayChildTableV2_Touch(&table, 0x1002U, 41U, 10U, 100U);
    assert(slot_a == 0);
    assert(slot_b == 1);
    assert(RelayChildTableV2_Touch(&table, 0x1003U, 42U, 50U, 100U) < 0);

    /* 0x1001 最久未出现且已超时，应原位回收；在线的 0x1002 保留。 */
    slot_c = RelayChildTableV2_Touch(&table, 0x1003U, 42U, 101U, 100U);
    assert(slot_c == slot_a);
    assert(RelayChildTableV2_Find(&table, 0x1001U) < 0);
    assert(RelayChildTableV2_Find(&table, 0x1002U) == slot_b);
    assert(RelayChildTableV2_Find(&table, 0x1003U) == slot_c);
}

static void test_online_metrics_expire_without_growing_table(void)
{
    relay_child_table_v2_t table;

    RelayChildTableV2_Init(&table, 4U);
    assert(RelayChildTableV2_Touch(&table, 0x2001U, 50U, 1000U, 200U) == 0);
    assert(RelayChildTableV2_Touch(&table, 0x2002U, 51U, 1100U, 200U) == 1);
    assert(RelayChildTableV2_OnlineCount(&table, 1200U, 200U) == 2U);
    assert(RelayChildTableV2_OnlineBitmap(&table, 1200U, 200U) == 0x0003U);

    assert(RelayChildTableV2_OnlineCount(&table, 1250U, 200U) == 1U);
    assert(RelayChildTableV2_OnlineBitmap(&table, 1250U, 200U) == 0x0002U);
    assert(table.count == 2U);

    /* 毫秒时钟回卷时，短时间差仍按无符号减法正确计算。 */
    RelayChildTableV2_Init(&table, 1U);
    assert(RelayChildTableV2_Touch(&table, 0x3001U, 60U,
                                  UINT32_MAX - 50U, 100U) == 0);
    assert(RelayChildTableV2_IsOnline(&table, 0U, 25U, 100U));
    assert(!RelayChildTableV2_IsOnline(&table, 0U, 75U, 100U));
}

int main(void)
{
    test_full_table_reuses_only_expired_slots();
    test_online_metrics_expire_without_growing_table();
    puts("relay child table lifecycle: PASS");
    return 0;
}
