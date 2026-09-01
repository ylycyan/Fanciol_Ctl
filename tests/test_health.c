#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "CH58x_common.h"
#include "../BLE/Peripheral/APP/include/health.h"

static void test_reset_snapshot_is_ram_only(void)
{
    Health_Init(0x37U, 0x1234U);
    assert(Health_LastResetReason() == 0x37U);
    assert(Health_ConsecutiveResets() == 0U);
    assert(Health_LastUnhealthyMask() == 0U);
    assert(Health_StorageError() == 0U);
}

static void test_watchdog_reset_is_reported_without_journaling(void)
{
    Health_Init(RST_STATUS_WTR, 0U);
    assert(Health_LastResetReason() == RST_STATUS_WTR);
    assert(Health_ConsecutiveResets() == 1U);

    /* Re-initializing does not increment a persistent counter or touch Flash. */
    Health_Init(RST_STATUS_WTR, 0U);
    assert(Health_ConsecutiveResets() == 1U);
}

int main(void)
{
    test_reset_snapshot_is_ram_only();
    test_watchdog_reset_is_reported_without_journaling();
    puts("minimal watchdog health tests passed");
    return 0;
}
