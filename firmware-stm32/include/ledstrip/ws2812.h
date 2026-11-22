#ifndef LEDSTRIP_WS2812_H
#define LEDSTRIP_WS2812_H

#include <stdint.h>

#define WS2812_NLED 120
#define WS2812_NBYTES (WS2812_NLED * 3)
#define WS2812_NBITS (WS2812_NBYTES * 8)

/* 100 MHz timer clock, 800 kHz: 125 ticks (10 ns each) per bit.
 * Contract: T0H 0.375 us, T1H 0.75 us, T0L 0.875 us, T1L 0.5 us.
 * T0H is 37.5 ticks, so use 38: T0H 0.38 us, T0L 0.87 us. */
#define WS2812_ARR 124u
#define WS2812_CCR_ZERO 38u
#define WS2812_CCR_ONE 75u

/* Expand 360 GRB bytes, MSB first per byte, into 2880 CCR3 halfwords. */
void ws2812_encode(const uint8_t grb[WS2812_NBYTES],
                   uint16_t ccr[WS2812_NBITS]);

#endif
