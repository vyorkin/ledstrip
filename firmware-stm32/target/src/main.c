#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"

#include "ledstrip/button.h"
#include "ledstrip/frame.h"
#include "adc_input.h"
#include "iwdg.h"
#include "reset.h"
#include "ws2812_output.h"

/* frame_render() fills FRAME_NBYTES; ws2812_output_show() reads this many. */
typedef char ws2812_frame_size_check[
    (FRAME_NBYTES == WS2812_NBYTES) ? 1 : -1];

/* PC13 is the Black Pill LED, active low. Toggle every 500 ms: 1 Hz blink. */
#define HEARTBEAT_TOGGLE_MS 500u
/* Bounded spin count for HSE/PLL/VOS readiness; not a calibrated time. */
#define CLOCK_WAIT_LOOPS 1000000u

/* Read in the debugger: 0 = PLL/HSE selected 100 MHz, 1 = HSI fallback. */
volatile uint8_t g_clock_fallback;

static volatile uint32_t g_ms;

void SysTick_Handler(void)
{
    g_ms++;
}

/* The clock waits are bounded, but each iteration is a possible halt point
 * for the debugger; keep the watchdog fed while waiting for HSE/PLL/VOS. The
 * self-test build has no watchdog. */
static void clock_wait_kick(void)
{
#ifndef CLOCK_SELFTEST
    iwdg_refresh();
#endif
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
        clock_wait_kick();
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
        clock_wait_kick();
    }
    if (i == CLOCK_WAIT_LOOPS) {
        g_clock_fallback = 1;
        return;
    }

    /* VOS takes effect only with the PLL running. */
    for (i = 0; i < CLOCK_WAIT_LOOPS && !LL_PWR_IsActiveFlag_VOS(); i++) {
        clock_wait_kick();
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
        clock_wait_kick();
    }
    g_clock_fallback = 1;
}

#ifndef CLOCK_SELFTEST
/* Packet state and the GRB bytes of the frame being shifted out. */
static FrameState s_frame;
static ButtonState s_button;
static uint8_t s_grb[FRAME_NBYTES];

/* PA0 is the Black Pill KEY button: input with the internal pull-up, active
 * low. */
static void button_gpio_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_0, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_0, LL_GPIO_PULL_UP);
}

static bool button_pressed(void)
{
    return LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_0) == 0u;
}

/* 200 Hz LED frame: the DSP frame's brightness sets each packet's level, its
 * beat advances the every-second-beat flip, and frame_render() writes GRB in
 * strip order, so no colour swap is needed. ws2812_output_show() blocks for
 * 3.6 ms while the ADC DMA keeps filling the next half buffer. */
static void ws2812_show_dsp_frame(const DspFrame *f)
{
    /* One sample per 200 Hz frame: five pressed frames (25 ms) switch the
     * mode before this frame's step, so the new directions apply immediately.
     * Positions, flip and the hit counter are preserved. */
    if (button_frame(&s_button, button_pressed()))
        frame_next_mode(&s_frame);

    frame_step(&s_frame, f->brightness, f->beat);
    frame_render(&s_frame, f->brightness, s_grb);
    ws2812_output_show(s_grb);

    /* One reload per 200 Hz frame: a stalled transfer, a wedged DSP path or a
     * debugger halt gets 50 ms (nominal) before the MCU resets. */
    iwdg_refresh();
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
    /* Capture the sticky reset flags before anything else touches RCC. */
    reset_cause_capture();

    /* Start the watchdog first; clock_init_100mhz() refreshes it inside its
     * bounded waits, so a halt anywhere in startup resets the MCU. */
    iwdg_init();

    /* Before waiting for the PLL: PB0 must not float into the level shifter
     * while the strip is powering up. The timer stays stopped, CCR3 = 0. */
    ws2812_output_init();

    clock_init_100mhz();
    SystemCoreClockUpdate();

    heartbeat_init();
    button_gpio_init();
#endif
    /* 1 ms tick; SysTick_Config uses the processor clock by default. */
    (void)SysTick_Config(SystemCoreClock / 1000u);

#ifndef CLOCK_SELFTEST
    /* A WS2812 latches whatever it sees at power-up. Clear it, then leave the
     * line low for the >=50 us reset gap. If the clock fell back to HSI the
     * bit rate is wrong, so leave PB0 low instead. */
    if (!g_clock_fallback) {
        static const uint8_t off[FRAME_NBYTES];

        ws2812_output_show(off);

        /* Two tick boundaries: the next tick may already be imminent. */
        uint32_t reset_at = g_ms;
        while ((uint32_t)(g_ms - reset_at) < 2u) {
        }
    }

    /* Deterministic start: positions 10/40/80, mode 0, flip and hit counter
     * zero. Capture starts after the strip is clear: each 5 ms DSP frame then
     * renders and shifts one WS2812 frame. */
    frame_init(&s_frame);
    button_init(&s_button);

    static Dsp dsp;
    dsp_init(&dsp, (float)ADC_INPUT_RATE_HZ);
    adc_input_init();
    if (!g_clock_fallback)
        adc_input_start();
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
        /* No DspFrames on the HSI fallback, so keep the watchdog alive here;
         * the diagnostic 5 Hz blink is the point of that mode. */
        if (g_clock_fallback)
            iwdg_refresh();

        /* Each call consumes at most two completed 480-sample halves; each
         * one renders and shifts a strip frame. */
        (void)adc_input_process(&dsp, ws2812_show_dsp_frame);
#endif
    }
}
