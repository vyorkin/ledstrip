#include "ledstrip/ws2812.h"

void ws2812_encode(const uint8_t grb[WS2812_NBYTES],
                   uint16_t ccr[WS2812_NBITS])
{
    int n = 0;

    for (int i = 0; i < WS2812_NBYTES; ++i) {
        uint8_t byte = grb[i];
        for (int bit = 7; bit >= 0; --bit)
            ccr[n++] = (byte & (1u << bit)) ? WS2812_CCR_ONE
                                            : WS2812_CCR_ZERO;
    }
}
