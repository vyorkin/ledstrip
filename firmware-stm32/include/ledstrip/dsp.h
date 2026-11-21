#ifndef LEDSTRIP_DSP_H
#define LEDSTRIP_DSP_H

#include <stdbool.h>
#include <stdint.h>

#define DSP_BANDS 3
#define DSP_FRAME_HZ 200.0f
#define DSP_FRAME_SAMPLES 480u
#define DSP_BRIGHT_MAX 204

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} Biquad;

typedef struct {
    float fs;
    float dc_pole;
    float dc_x1, dc_y1;
    Biquad band[DSP_BANDS];
    float env[DSP_BANDS];
    float peak;
    float agc_gain;
    float low_prev;
    bool have_prev;
    uint8_t refrac;
} Dsp;

typedef struct {
    float level[DSP_BANDS];
    uint8_t brightness[DSP_BANDS];
    bool beat;
} DspFrame;

void dsp_init(Dsp *d, float fs);
void dsp_process(Dsp *d, float x);
DspFrame dsp_frame(Dsp *d);

#endif
