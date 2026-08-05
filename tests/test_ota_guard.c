#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../BLE/Peripheral/APP/include/ota_guard.h"

#define APP_B_START 0x00037000UL
#define IAP_START   0x0006D000UL
#define BLOCK_SIZE  4096UL

static void test_partition_bounds(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);

    assert(OtaGuard_BeginErase(&guard, APP_B_START, 54, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    assert(guard.range_end == IAP_START);
    assert(guard.state == OTA_GUARD_ERASING);

    assert(!OtaGuard_BeginErase(&guard, APP_B_START, 55, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, APP_B_START, 0, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, APP_B_START + 1, 1, BLOCK_SIZE,
                                APP_B_START, IAP_START));
    assert(!OtaGuard_BeginErase(&guard, IAP_START, 1, BLOCK_SIZE,
                                APP_B_START, IAP_START));
}

static void test_order_and_contiguous_ranges(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 2, BLOCK_SIZE,
                               APP_B_START, IAP_START));

    assert(!OtaGuard_CanProgram(&guard, APP_B_START, 240));
    OtaGuard_EndErase(&guard, 1);
    assert(OtaGuard_CanProgram(&guard, APP_B_START, 240));
    assert(!OtaGuard_CanProgram(&guard, APP_B_START + 16, 240));
    OtaGuard_EndProgram(&guard, 240, 1);
    assert(OtaGuard_CanProgram(&guard, APP_B_START + 240, 32));
    OtaGuard_EndProgram(&guard, 32, 1);

    assert(!OtaGuard_CanFinish(&guard));
    assert(OtaGuard_CanVerify(&guard, APP_B_START, 240));
    OtaGuard_EndVerify(&guard, 240, 1);
    assert(!OtaGuard_CanVerify(&guard, APP_B_START + 241, 31));
    assert(OtaGuard_CanVerify(&guard, APP_B_START + 240, 32));
    OtaGuard_EndVerify(&guard, 32, 1);
    assert(OtaGuard_CanFinish(&guard));
    assert(!OtaGuard_CanProgram(&guard, APP_B_START + 272, 4));
}

static void test_failed_operations_do_not_advance(void)
{
    ota_guard_t guard;
    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 1, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    OtaGuard_EndErase(&guard, 1);

    assert(OtaGuard_CanProgram(&guard, APP_B_START, 16));
    OtaGuard_EndProgram(&guard, 16, 0);
    assert(OtaGuard_CanProgram(&guard, APP_B_START, 16));
    OtaGuard_EndProgram(&guard, 16, 1);

    assert(OtaGuard_CanVerify(&guard, APP_B_START, 16));
    OtaGuard_EndVerify(&guard, 16, 0);
    assert(OtaGuard_CanVerify(&guard, APP_B_START, 16));
    assert(!OtaGuard_CanFinish(&guard));

    OtaGuard_Reset(&guard);
    assert(OtaGuard_BeginErase(&guard, APP_B_START, 1, BLOCK_SIZE,
                               APP_B_START, IAP_START));
    OtaGuard_EndErase(&guard, 0);
    assert(guard.state == OTA_GUARD_IDLE);
}

int main(void)
{
    test_partition_bounds();
    test_order_and_contiguous_ranges();
    test_failed_operations_do_not_advance();
    puts("OTA partition/session guard: PASS");
    return 0;
}
