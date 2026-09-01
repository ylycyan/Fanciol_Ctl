#include "health.h"
#include "CH58x_common.h"

/*
 * The hardware watchdog follows the main scheduler only.  Each peripheral
 * already owns a bounded timeout/backoff, so a slow radio or Flash operation
 * must not stop watchdog refresh or write a second fault journal.
 */
static uint8_t last_reset_reason;
static uint8_t watchdog_reset;

void Health_Init(uint8_t boot_reset_reason, uint16_t fault_snapshot)
{
    (void)fault_snapshot;
    last_reset_reason = boot_reset_reason;
    watchdog_reset = (boot_reset_reason == RST_STATUS_WTR ||
                      boot_reset_reason == RST_STATUS_LRM1) ? 1U : 0U;
    PRINT("Reset reason=%u watchdog=%u\r\n",
          last_reset_reason, watchdog_reset);
}

uint8_t Health_ConsecutiveResets(void) { return watchdog_reset; }
uint8_t Health_LastResetReason(void) { return last_reset_reason; }
uint8_t Health_LastUnhealthyMask(void) { return 0U; }
uint8_t Health_StorageError(void) { return 0U; }
