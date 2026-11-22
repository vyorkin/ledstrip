#include "adc_input.h"

#include "stm32f4xx.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"

/* ADC1 is on DMA2 Stream 0, channel 0 (RM0383, table 28). */
#define ADC_DMA_STREAM DMA2_Stream0
#define ADC_DMA_CHANNEL 0u

#define ADC_DMA_LIFCR_FLAGS                                                   \
    (DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 |                 \
     DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0)

#define ADC_READY_HALF0 1u
#define ADC_READY_HALF1 2u

typedef char adc_frame_rate_check[
    (ADC_INPUT_FRAME_SAMPLES * 200u == ADC_INPUT_RATE_HZ) ? 1 : -1];

/* 100 MHz / 96000 = 1041.67; round to ARR 1041, so (ARR + 1) = 1042
 * ticks, i.e. 95.97 kHz. That is 31 Hz low, closer than ARR 1042. */
#define ADC_TIM2_ARR \
    (((SystemCoreClock + ADC_INPUT_RATE_HZ / 2u) / ADC_INPUT_RATE_HZ) - 1u)

/* ADC clock must stay at or below 36 MHz: APB2 100 MHz / 4 = 25 MHz. */
#define ADC_CCR_PRESCALER_4 ADC_CCR_ADCPRE_0

/* Channel 1 sample time 84 ADC clocks (4 << SMP1_Pos): 3.4 us, well inside
 * the 10.4 us sample period even with a high source impedance. */
#define ADC_SMP1_84_CYCLES (4u << ADC_SMPR2_SMP1_Pos)

/* TIM2_TRGO is EXTSEL 0110 in RM0383 table 42. */
#define ADC_EXTSEL_TIM2_TRGO (ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_2)

#define ADC_STABILIZE_LOOPS 1000u

static uint16_t s_adc_dma[ADC_INPUT_BUF_SAMPLES]
    __attribute__((aligned(4)));

static volatile uint8_t s_ready;

volatile DspFrame g_dsp_frame;
volatile uint32_t g_dsp_frame_count;
volatile uint32_t g_adc_overruns;

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t isr = DMA2->LISR;

    if (isr & DMA_LISR_HTIF0) {
        DMA2->LIFCR = DMA_LIFCR_CHTIF0;
        if (s_ready & ADC_READY_HALF0)
            g_adc_overruns++;
        s_ready |= ADC_READY_HALF0;
    }
    if (isr & DMA_LISR_TCIF0) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF0;
        if (s_ready & ADC_READY_HALF1)
            g_adc_overruns++;
        s_ready |= ADC_READY_HALF1;
    }
    if (isr & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) {
        DMA2->LIFCR = ADC_DMA_LIFCR_FLAGS;
        g_adc_overruns++;
    }
}

static void adc_dma_init(void)
{
    ADC_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while (ADC_DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    DMA2->LIFCR = ADC_DMA_LIFCR_FLAGS;

    ADC_DMA_STREAM->PAR = (uint32_t)(uintptr_t)&ADC1->DR;
    ADC_DMA_STREAM->M0AR = (uint32_t)(uintptr_t)s_adc_dma;
    ADC_DMA_STREAM->NDTR = ADC_INPUT_BUF_SAMPLES;
    ADC_DMA_STREAM->FCR = 0;
    ADC_DMA_STREAM->CR = (ADC_DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) |
                         DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC |
                         DMA_SxCR_CIRC | DMA_SxCR_DIR_0 | DMA_SxCR_HTIE |
                         DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE |
                         DMA_SxCR_PL_1;
}

void adc_input_init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_1, LL_GPIO_PULL_NO);

    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_PRESCALER_4;

    /* One regular conversion per trigger, channel 1, 12-bit. */
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SMPR2 = (ADC1->SMPR2 & ~ADC_SMPR2_SMP1) | ADC_SMP1_84_CYCLES;
    ADC1->SMPR1 = 0;
    ADC1->SQR1 = 0;
    ADC1->SQR2 = 0;
    ADC1->SQR3 = 1u;
    ADC1->CR2 = ADC_CR2_ADON;

    /* tSTAB is a few microseconds after ADON. */
    for (uint32_t i = 0; i < ADC_STABILIZE_LOOPS; ++i)
        __NOP();

    /* DMA requests on every conversion; DDS keeps the request enabled after a
     * single conversion. The external trigger is armed in start(), after the
     * timer's UG event has already passed, so no sample is lost before DMA. */
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS;
    ADC1->SR = 0;

    adc_dma_init();
    NVIC_SetPriority(DMA2_Stream0_IRQn, 1);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    TIM2->CR1 = 0;
    TIM2->PSC = 0;
    TIM2->ARR = ADC_TIM2_ARR;
    TIM2->CR2 = TIM_CR2_MMS_1; /* update event -> TRGO */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
}

void adc_input_start(void)
{
    ADC1->SR = ~ADC_SR_OVR;
    ADC1->CR2 |= ADC_CR2_EXTEN_0 | ADC_EXTSEL_TIM2_TRGO;
    ADC_DMA_STREAM->CR |= DMA_SxCR_EN;
    TIM2->CR1 |= TIM_CR1_CEN;
}

/* RM0383 11.8.1: after OVR the ADC blocks DMA requests and ignores triggers
 * until the DMA stream is reinitialized and the OVR flag is cleared. */
static void adc_input_recover(void)
{
    TIM2->CR1 &= ~TIM_CR1_CEN;
    adc_dma_init();
    ADC1->SR = ~ADC_SR_OVR;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    TIM2->CR1 |= TIM_CR1_CEN;
    s_ready = 0;
}

uint32_t adc_input_process(Dsp *d)
{
    __disable_irq();
    uint8_t ready = s_ready;
    s_ready = 0;
    /* Both halves pending means the main loop fell more than one half-period
     * behind. DMA refills half 0 while NDTR is above the half point, and half
     * 1 below it; keep the complete half and count the other as dropped. */
    if ((ready & (ADC_READY_HALF0 | ADC_READY_HALF1)) ==
        (ADC_READY_HALF0 | ADC_READY_HALF1)) {
        ready = (ADC_DMA_STREAM->NDTR > ADC_INPUT_FRAME_SAMPLES)
                    ? ADC_READY_HALF1
                    : ADC_READY_HALF0;
        g_adc_overruns++;
    }
    __enable_irq();

    uint32_t frames = 0;

    for (unsigned half = 0; half < 2u; ++half) {
        if (!(ready & (1u << half)))
            continue;

        const uint16_t *pcm = &s_adc_dma[half * ADC_INPUT_FRAME_SAMPLES];
        for (uint32_t i = 0; i < ADC_INPUT_FRAME_SAMPLES; ++i) {
            float x = ((float)pcm[i] - 2048.0f) * (1.0f / 2048.0f);
            dsp_process(d, x);
        }

        g_dsp_frame = dsp_frame(d);
        g_dsp_frame_count++;
        frames++;
    }

    __disable_irq();
    int overrun = (ADC1->SR & ADC_SR_OVR) != 0;
    if (overrun) {
        /* SR bits are rc_w0: write 1 to everything except OVR. */
        ADC1->SR = ~ADC_SR_OVR;
        g_adc_overruns++;
    }
    __enable_irq();

    if (overrun)
        adc_input_recover();

    return frames;
}
