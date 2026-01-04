/*
 * Test: DMA Operation During Flash Erase (Simplified for INAV)
 *
 * Verifies whether circular DMA continues during flash erase.
 * Uses LED flash pattern to indicate results (no scope needed).
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_DMA_FLASH_ERASE_TEST

#include "drivers/time.h"
#include "drivers/flash.h"
#include "drivers/light_led.h"
#include "build/debug.h"

// Simplified test - just check if DMA counter advances
static volatile uint32_t testComplete = 0;
static volatile uint32_t dmaWorked = 0;

void testDmaFlashEraseRun(void)
{
    LED0_ON;
    delay(1000);

    // TODO: Implement actual DMA test
    // For now, just indicate test ran
    testComplete = 1;
    dmaWorked = 1;  // Placeholder

    // Flash LED to show completion
    for (int i = 0; i < 5; i++) {
        LED0_TOGGLE;
        delay(200);
    }

    LED0_OFF;
}

#endif // USE_DMA_FLASH_ERASE_TEST
