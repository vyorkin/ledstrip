#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"

#include "ledstrip/frame.h"
#include "ws2812_output.h"

/* frame_render() fills FRAME_NBYTES; ws2812_output_show() reads this many. */
typedef char ws2812_frame_size_check[
    (FRAME_NBYTES == WS2812_NBYTES) ? 1 : -1];

/* PC13 is the Black Pill LED, active low. Toggle every 500 ms: 1 Hz blink. */
#define HEARTBEAT_TOGGLE_MS 500u
#ifndef CLOCK_SELFTEST
/* The strip keeps its frame; refresh only to recover from a glitch. */
#define WS2812_REFRESH_MS 1000u
#endif
/* Bounded spin count for HSE/PLL/VOS readiness; not a calibrated time. */
#define CLOCK_WAIT_LOOPS 1000000u

/* Read in the debugger: 0 = PLL/HSE selected 100 MHz, 1 = HSI fallback. */
volatile uint8_t g_clock_fallback;

static volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    g_ms++;
}

/* SystemInit() in the CMSIS template only sets the FPU and vector table; the
 * clock tree is configured here with LL, no HAL and no raw register writes. */
static void clock_init_100mhz(void)
{
    /* VOS resets to scale 2 (84 MHz max); 100 MHz needs scale 1, and VOS
     * may only change while the PLL is off. */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

    LL_FLASH_SetLatency(LL_FLASH_LATENCY_3);
    LL_FLASH_EnablePrefetch();
    LL_FLASH_EnableInstCache();
    LL_FLASH_EnableDataCache();

    LL_RCC_HSE_Enable();
    uint32_t i;
    for (i = 0; i < CLOCK_WAIT_LOOPS && !LL_RCC_HSE_IsReady(); i++) {
    }
    if (i == CLOCK_WAIT_LOOPS) {
        g_clock_fallback = 1;
        return;
    }

    /* 25 MHz HSE / 25 = 1 MHz; * 200 = 200 MHz VCO; / 2 = 100 MHz SYSCLK. */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE,
                                LL_RCC_PLLM_DIV_25, 200u,
                                LL_RCC_PLLP_DIV_2);
    LL_RCC_PLL_Enable();
    for (i = 0; i < CLOCK_WAIT_LOOPS && !LL_RCC_PLL_IsReady(); i++) {
    }
    if (i == CLOCK_WAIT_LOOPS) {
        g_clock_fallback = 1;
        return;
    }

    /* VOS takes effect only with the PLL running. */
    for (i = 0; i < CLOCK_WAIT_LOOPS && !LL_PWR_IsActiveFlag_VOS(); i++) {
    }
    if (i == CLOCK_WAIT_LOOPS) {
        g_clock_fallback = 1;
        return;
    }

    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    for (i = 0; i < CLOCK_WAIT_LOOPS; i++) {
        if (LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
            return;
        }
    }
    g_clock_fallback = 1;
}

#ifndef CLOCK_SELFTEST
/* frame_render() writes GRB in strip order, so no colour swap is needed. */
static void ws2812_show_frame(const uint8_t brightness[3])
{
    FrameState st;
    uint8_t grb[FRAME_NBYTES];

    frame_init(&st);
    frame_render(&st, brightness, grb);
    ws2812_output_show(grb);
}

/* Fixed bench pattern: the three packets of frame_init(), brightness 204
 * (80 %). */
static void ws2812_show_test_frame(void)
{
    static const uint8_t brightness[3] = {204u, 204u, 204u};

    ws2812_show_frame(brightness);
}
#endif

static void heartbeat_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_13, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOC, LL_GPIO_PIN_13, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOC, LL_GPIO_PIN_13, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_13, LL_GPIO_PULL_NO);

    /* Active low: high is off, so power-up does not flash the LED. */
    LL_GPIO_SetOutputPin(GPIOC, LL_GPIO_PIN_13);
}

#ifdef CLOCK_SELFTEST
/* Bench-only build: MCO1 (PA8, AF0) = PLLCLK / 5, so 100 MHz reads as
 * 20 MHz on a scope. PA8 is on the header; PC9 (MCO2) is not routed on
 * the WeAct Black Pill. On the HSI fallback PLLCLK is not running. */
static void mco1_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_8, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_8, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_8, LL_GPIO_PULL_NO);
    LL_GPIO_SetAFPin_8_15(GPIOA, LL_GPIO_PIN_8, LL_GPIO_AF_0);

    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_PLLCLK, LL_RCC_MCO1_DIV_5);
}
#endif

int main(void)
{
#ifdef CLOCK_SELFTEST
    clock_init_100mhz();
    SystemCoreClockUpdate();

    heartbeat_init();
    mco1_init();
#else
    /* Before waiting for the PLL: PB0 must not float into the level shifter
     * while the strip is powering up. The timer stays stopped, CCR3 = 0. */
    ws2812_output_init();

    clock_init_100mhz();
    SystemCoreClockUpdate();

    heartbeat_init();
#endif
    /* 1 ms tick; SysTick_Config uses the processor clock by default. */
    (void)SysTick_Config(SystemCoreClock / 1000u);

#ifndef CLOCK_SELFTEST
    /* A WS2812 latches whatever it sees at power-up. Clear it before the
     * pattern, with a >=50 us low gap between the two frames. If the clock
     * fell back to HSI the bit rate is wrong, so leave PB0 low instead. */
    if (!g_clock_fallback) {
        static const uint8_t off[3] = {0u, 0u, 0u};

        ws2812_show_frame(off);

        /* Two tick boundaries: the next tick may already be imminent. */
        uint32_t reset_at = g_ms;
        while ((uint32_t)(g_ms - reset_at) < 2u) {
        }
        ws2812_show_test_frame();
    }
    uint32_t last_ws = g_ms;
#endif

    /* HSI fallback blinks 5x faster, so the failure is visible without a
     * debugger; the self-test build has no PLLCLK on PA8 either. */
    const uint32_t toggle_ms = g_clock_fallback ? 100u : HEARTBEAT_TOGGLE_MS;

    uint32_t last = 0;
    for (;;) {
        uint32_t now = g_ms;
        if ((uint32_t)(now - last) >= toggle_ms) {
            last += toggle_ms;
            LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_13);
        }
#ifndef CLOCK_SELFTEST
        if (!g_clock_fallback &&
            (uint32_t)(now - last_ws) >= WS2812_REFRESH_MS) {
            last_ws += WS2812_REFRESH_MS;
            ws2812_show_test_frame();
        }
#endif
    }
}
