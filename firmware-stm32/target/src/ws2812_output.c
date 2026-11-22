#include "ws2812_output.h"

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/* DMA1 Stream 2, channel 5 is the TIM3 update/CH4 row on the F411 map.
 * Only the update request (UDE) is enabled, so each update moves the next
 * CCR3 halfword. The CH3 compare request would be Stream 7, channel 5. */
#define WS2812_DMA_STREAM DMA1_Stream2
#define WS2812_DMA_CHANNEL 5u

#define WS2812_DMA_LIFCR_FLAGS                                                \
    (DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTEIF2 |                 \
     DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2)

/* Generous spin limit: one frame is 3.6 ms, a million loops is tens of ms.
 * A transfer that never completes must not wedge the heartbeat loop. */
#define WS2812_WAIT_LOOPS 1000000u

static uint16_t s_ccr[WS2812_NBITS];

static void data_pin_low(void)
{
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_0);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_OUTPUT);
}

static void data_pin_af(void)
{
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_0, LL_GPIO_AF_2);
}

static void dma_stop(void)
{
    WS2812_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while (WS2812_DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    DMA1->LIFCR = WS2812_DMA_LIFCR_FLAGS;
}

void ws2812_output_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

    /* PB0 stays a plain GPIO output low until a frame is ready. The level
     * shifter must not see a floating input, and the timer output must not
     * go high while the first CCR3 value is still being fetched. */
    data_pin_low();
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_0, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_0, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);

    /* 100 MHz timer clock / (124 + 1) = 800 kHz. PWM mode 1, active high.
     * CCR3 preload is off on purpose: each update DMA write must shape the
     * bit that just started. The write lands within a few tens of cycles of
     * CNT=0, well before the earliest compare (38), so it cannot create a
     * late edge. With OC3PE set, the value would apply one period later. */
    TIM3->CR1 = 0;
    TIM3->PSC = 0;
    TIM3->ARR = WS2812_ARR;
    TIM3->CCR3 = 0;
    TIM3->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1;
    TIM3->CCER = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
}

void ws2812_output_show(const uint8_t grb[WS2812_NBYTES])
{
    uint32_t i;

    ws2812_encode(grb, s_ccr);

    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CCR3 = 0;
    TIM3->CCER = 0;
    TIM3->DIER = 0;
    data_pin_low();
    dma_stop();

    WS2812_DMA_STREAM->PAR = (uint32_t)(uintptr_t)&TIM3->CCR3;
    WS2812_DMA_STREAM->M0AR = (uint32_t)(uintptr_t)s_ccr;
    WS2812_DMA_STREAM->NDTR = WS2812_NBITS;
    WS2812_DMA_STREAM->FCR = 0;
    WS2812_DMA_STREAM->CR = (WS2812_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                             DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 |
                             DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_PL_1;
    __DSB();
    WS2812_DMA_STREAM->CR |= DMA_SxCR_EN;

    TIM3->DIER = TIM_DIER_UDE;
    /* UG raises the first update request while the counter is stopped, so
     * halfword 0 is in CCR3 before bit 0 starts. The remaining 2879 requests
     * come from the 2879 updates after each bit, for 2880 transfers total. */
    TIM3->EGR = TIM_EGR_UG;
    for (i = 0; i < WS2812_WAIT_LOOPS; ++i) {
        if (WS2812_DMA_STREAM->NDTR != WS2812_NBITS)
            break;
    }
    if (i == WS2812_WAIT_LOOPS)
        goto stop;
    TIM3->SR = ~TIM_SR_UIF;

    /* PB0 is still low. Select AF2 while the channel is off, then enable the
     * channel and the counter back-to-back: CC3E asserts the first level a
     * few cycles before CEN, so the first bit is extended by tens of ns. */
    data_pin_af();
    TIM3->CCER = TIM_CCER_CC3E;
    TIM3->CR1 |= TIM_CR1_CEN;
    for (i = 0; i < WS2812_WAIT_LOOPS; ++i) {
        if (DMA1->LISR & DMA_LISR_TCIF2)
            break;
    }
    if (i == WS2812_WAIT_LOOPS)
        goto stop;

    /* TC is raised by the update that loaded the last halfword, at the start
     * of the last bit. Drop the stale compare flag, wait for the last bit's
     * falling edge, then stop while PB0 is low: no short high glitch at the
     * reset boundary, and the line stays low for the >=50 us reset gap. */
    TIM3->SR = ~TIM_SR_CC3IF;
    for (i = 0; i < WS2812_WAIT_LOOPS; ++i) {
        if (TIM3->SR & TIM_SR_CC3IF)
            break;
    }

stop:
    TIM3->CCR3 = 0;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CCER = 0;
    TIM3->DIER = 0;
    dma_stop();
    data_pin_low();
}
