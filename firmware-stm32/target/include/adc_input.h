#ifndef LEDSTRIP_ADC_INPUT_H
#define LEDSTRIP_ADC_INPUT_H

#include <stdint.h>

#include "ledstrip/dsp.h"

/* ADC1 channel 1 on PA1, one 12-bit sample per TIM2 TRGO update. */
#define ADC_INPUT_RATE_HZ 96000u
#define ADC_INPUT_FRAME_SAMPLES DSP_FRAME_SAMPLES
#define ADC_INPUT_BUF_SAMPLES (2u * ADC_INPUT_FRAME_SAMPLES)

/* Debugger snapshot: last completed DspFrame, frame count, and DMA/ADC
 * overruns. Read these symbols from an SWD session; the target never prints. */
extern volatile DspFrame g_dsp_frame;
extern volatile uint32_t g_dsp_frame_count;
extern volatile uint32_t g_adc_overruns;

/* Clocks, PA1 analog mode, ADC1, TIM2, and DMA2 Stream 0. The DMA stream and
 * timer stay stopped, so no sample is captured yet. */
void adc_input_init(void);

/* Start circular DMA and the 96 kHz timer. Only call this on the 100 MHz PLL
 * clock: with the HSI fallback the sample rate would be wrong. */
void adc_input_start(void);

/* Process every completed 480-sample half-frame in the 960-sample circular
 * buffer and update g_dsp_frame. Returns the number of DspFrames (0..2).
 * Must be called more often than once per 5 ms half period. */
uint32_t adc_input_process(Dsp *d);

#endif
