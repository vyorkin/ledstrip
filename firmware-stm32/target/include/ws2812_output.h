#ifndef LEDSTRIP_WS2812_OUTPUT_H
#define LEDSTRIP_WS2812_OUTPUT_H

#include <stdint.h>

#include "ledstrip/ws2812.h"

/* PB0 low, TIM3_CH3 configured for 800 kHz, DMA1 ready. No frame is sent. */
void ws2812_output_init(void);

/* Encode one 360-byte GRB frame and shift it out with PWM + DMA. Blocks for
 * one frame (3.6 ms) and returns with PB0 low, so the strip has its reset
 * gap and latches the frame. */
void ws2812_output_show(const uint8_t grb[WS2812_NBYTES]);

#endif
