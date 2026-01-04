/*
 * DMA Flash Erase Test
 *
 * Tests whether circular DMA continues operating during flash erase operations.
 *
 * Test setup:
 * - Timer triggers DMA at 25Hz to toggle LED0/LED1 via GPIO BSRR
 * - Task erases blackbox flash every 500ms (blocking operation)
 * - Visual verification: If LEDs stop blinking, DMA stalled during flash erase
 *
 * This file is part of INAV.
 */

#include "platform.h"

#ifdef TEST_CIRCULAR_DSHOT

#include "build/debug.h"
#include "common/time.h"
#include "drivers/io.h"
#include "drivers/light_led.h"
#include "drivers/timer.h"
#include "drivers/dma.h"
#include "io/flashfs.h"

#if defined(STM32H7)
#include "drivers/timer_impl_hal.h"
#elif defined(STM32F4) || defined(STM32F7)
#include "drivers/timer_impl_stdperiph.h"
#endif

// LED GPIO pins (defined in target.h)
extern const IO_t leds[];

// DMA buffer for LED toggling (circular mode)
// Pattern: [LED0 ON / LED1 OFF, LED0 OFF / LED1 ON]
static uint32_t ledDmaBuffer[2];

// Test state
static bool testInitialized = false;
static uint32_t flashEraseStart = 0;
static uint32_t flashEraseEnd = 0;
static uint32_t ledRestartCount = 0;
static uint32_t taskCallCount = 0;

#if defined(STM32H7)
// STM32H7 HAL-based DMA setup
static DMA_HandleTypeDef ledDmaHandle;
static TIM_HandleTypeDef ledTimerHandle;

static void initLedDmaH7(void)
{
    // Get GPIO port and pins for LED0 and LED1
    GPIO_TypeDef *led0Port = IO_GPIO(leds[0]);
    GPIO_TypeDef *led1Port = IO_GPIO(leds[1]);
    uint32_t led0Pin = IO_Pin(leds[0]);
    uint32_t led1Pin = IO_Pin(leds[1]);

    if (!led0Port || !led1Port) {
        // LEDs not defined on this target
        return;
    }

    // Calculate BSRR values for LED toggling
    // BSRR format: upper 16 bits = reset, lower 16 bits = set
    ledDmaBuffer[0] = (led1Pin << 16) | led0Pin;  // LED0 ON, LED1 OFF
    ledDmaBuffer[1] = (led0Pin << 16) | led1Pin;  // LED0 OFF, LED1 ON

    // Use TIM2 for 25Hz timing (40ms period)
    __HAL_RCC_TIM2_CLK_ENABLE();

    ledTimerHandle.Instance = TIM2;
    ledTimerHandle.Init.Prescaler = (SystemCoreClock / 1000000) - 1;  // 1MHz tick
    ledTimerHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    ledTimerHandle.Init.Period = 40000 - 1;  // 40ms = 25Hz
    ledTimerHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    ledTimerHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&ledTimerHandle) != HAL_OK) {
        return;
    }

    // Configure DMA for GPIO BSRR transfers
    __HAL_RCC_DMA1_CLK_ENABLE();

    ledDmaHandle.Instance = DMA1_Stream0;
    ledDmaHandle.Init.Request = DMA_REQUEST_TIM2_UP;
    ledDmaHandle.Init.Direction = DMA_MEMORY_TO_PERIPH;
    ledDmaHandle.Init.PeriphInc = DMA_PINC_DISABLE;
    ledDmaHandle.Init.MemInc = DMA_MINC_ENABLE;
    ledDmaHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    ledDmaHandle.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    // Switch between DMA_NORMAL and DMA_CIRCULAR to compare behavior
    ledDmaHandle.Init.Mode = DMA_NORMAL;  // NORMAL mode - one-shot (stops after buffer)
    //ledDmaHandle.Init.Mode = DMA_CIRCULAR;  // CIRCULAR mode - auto-repeats buffer
    ledDmaHandle.Init.Priority = DMA_PRIORITY_LOW;
    ledDmaHandle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&ledDmaHandle) != HAL_OK) {
        return;
    }

    // Start DMA transfer to GPIO BSRR
    // Note: Both LEDs on same port (usually)
    uint32_t bsrrAddr = (uint32_t)&led0Port->BSRR;
    HAL_DMA_Start(&ledDmaHandle, (uint32_t)ledDmaBuffer, bsrrAddr, 2);

    // Enable timer DMA request
    __HAL_TIM_ENABLE_DMA(&ledTimerHandle, TIM_DMA_UPDATE);

    // Start timer
    HAL_TIM_Base_Start(&ledTimerHandle);
}

static void restartLedDmaH7(void)
{
    // Check if DMA completed (NORMAL mode stops after buffer)
    if (HAL_DMA_GetState(&ledDmaHandle) != HAL_DMA_STATE_BUSY) {
        // Restart DMA transfer
        GPIO_TypeDef *led0Port = IO_GPIO(leds[0]);
        uint32_t bsrrAddr = (uint32_t)&led0Port->BSRR;
        HAL_DMA_Start(&ledDmaHandle, (uint32_t)ledDmaBuffer, bsrrAddr, 2);
        ledRestartCount++;
    }
}

#elif defined(STM32F4) || defined(STM32F7)
// STM32F4/F7 StdPeriph-based DMA setup
static void initLedDmaF4F7(void)
{
    // Get GPIO port and pins for LED0 and LED1
    GPIO_TypeDef *led0Port = IO_GPIO(leds[0]);
    GPIO_TypeDef *led1Port = IO_GPIO(leds[1]);
    uint32_t led0Pin = IO_Pin(leds[0]);
    uint32_t led1Pin = IO_Pin(leds[1]);

    if (!led0Port || !led1Port) {
        return;
    }

    // Calculate BSRR values
    ledDmaBuffer[0] = (led1Pin << 16) | led0Pin;  // LED0 ON, LED1 OFF
    ledDmaBuffer[1] = (led0Pin << 16) | led1Pin;  // LED0 OFF, LED1 ON

    // Configure TIM2 for 25Hz
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef timInit;
    timInit.TIM_Prescaler = (SystemCoreClock / 1000000) - 1;  // 1MHz
    timInit.TIM_CounterMode = TIM_CounterMode_Up;
    timInit.TIM_Period = 40000 - 1;  // 40ms = 25Hz
    timInit.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &timInit);

    // Configure DMA
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    DMA_InitTypeDef dmaInit;
    DMA_StructInit(&dmaInit);
    dmaInit.DMA_Channel = DMA_Channel_3;  // TIM2_UP
    dmaInit.DMA_PeripheralBaseAddr = (uint32_t)&led0Port->BSRR;
    dmaInit.DMA_Memory0BaseAddr = (uint32_t)ledDmaBuffer;
    dmaInit.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dmaInit.DMA_BufferSize = 2;
    dmaInit.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dmaInit.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dmaInit.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dmaInit.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    // Switch between DMA_Mode_Normal and DMA_Mode_Circular to compare behavior
    dmaInit.DMA_Mode = DMA_Mode_Normal;  // NORMAL mode - one-shot (stops after buffer)
    //dmaInit.DMA_Mode = DMA_Mode_Circular;  // CIRCULAR mode - auto-repeats buffer
    dmaInit.DMA_Priority = DMA_Priority_Low;
    DMA_Init(DMA1_Stream0, &dmaInit);

    // Enable DMA stream
    DMA_Cmd(DMA1_Stream0, ENABLE);

    // Enable timer DMA request
    TIM_DMACmd(TIM2, TIM_DMA_Update, ENABLE);

    // Start timer
    TIM_Cmd(TIM2, ENABLE);
}

static void restartLedDmaF4F7(void)
{
    // Check if DMA completed (NORMAL mode stops after buffer)
    // Check TCIF (Transfer Complete Interrupt Flag) for Stream 0
    if (DMA_GetFlagStatus(DMA1_Stream0, DMA_FLAG_TCIF0) == SET) {
        // Clear the flag
        DMA_ClearFlag(DMA1_Stream0, DMA_FLAG_TCIF0);

        // Disable DMA stream
        DMA_Cmd(DMA1_Stream0, DISABLE);

        // Wait for DMA to actually disable
        while (DMA_GetCmdStatus(DMA1_Stream0) != DISABLE) {}

        // Restart DMA
        DMA1_Stream0->NDTR = 2;  // Reset transfer count
        DMA_Cmd(DMA1_Stream0, ENABLE);

        ledRestartCount++;
    }
}
#endif

void testDmaFlashEraseInit(void)
{
    if (testInitialized) {
        return;
    }

    // Initialize flashfs
    if (!flashfsIsReady()) {
        flashfsInit();
    }

    // Define erase range: first 64KB of blackbox flash
    // This is large enough to cause blocking but not destroy data
    flashEraseStart = 0;
    flashEraseEnd = 64 * 1024;

    // Set up DMA-based LED toggling
#if defined(STM32H7)
    initLedDmaH7();
#elif defined(STM32F4) || defined(STM32F7)
    initLedDmaF4F7();
#endif

    testInitialized = true;
}

void taskDmaFlashTest(timeUs_t currentTimeUs)
{
    UNUSED(currentTimeUs);

    if (!testInitialized) {
        testDmaFlashEraseInit();
        return;
    }

    // Restart DMA if needed (NORMAL mode requires manual restart)
    // This is called every task iteration to keep LEDs blinking
#if defined(STM32H7)
    restartLedDmaH7();
#elif defined(STM32F4) || defined(STM32F7)
    restartLedDmaF4F7();
#endif

    taskCallCount++;

    // Erase flash every 50 task calls (50 * 10ms = 500ms at 100Hz task rate)
    if (taskCallCount >= 50) {
        taskCallCount = 0;

        // Erase a range of blackbox flash
        // This is a BLOCKING operation that stalls the CPU during erase
        flashfsEraseRange(flashEraseStart, flashEraseEnd);

        // If DMA continues during flash erase (NORMAL mode with manual restart):
        //   - LEDs keep blinking, but may briefly pause during erase
        // If DMA stalls during flash erase:
        //   - LEDs will have longer visible pauses/interruptions
    }
}

#endif // TEST_CIRCULAR_DSHOT
