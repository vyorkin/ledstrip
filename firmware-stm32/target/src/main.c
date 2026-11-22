#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"

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

int main(void)
{
    clock_init_100mhz();
    SystemCoreClockUpdate();

    heartbeat_init();
    /* 1 ms tick; SysTick_Config uses the processor clock by default. */
    (void)SysTick_Config(SystemCoreClock / 1000u);

    uint32_t last = 0;
    for (;;) {
        uint32_t now = g_ms;
        if ((uint32_t)(now - last) >= HEARTBEAT_TOGGLE_MS) {
            last += HEARTBEAT_TOGGLE_MS;
            LL_GPIO_TogglePin(GPIOC, LL_GPIO_PIN_13);
        }
    }
}
