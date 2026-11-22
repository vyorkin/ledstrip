#include "ledstrip/dsp.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f

#define ENV_ATTACK_S 0.002f
#define ENV_RELEASE_S 0.150f
#define PEAK_ATTACK_S 0.001f
#define PEAK_RELEASE_S 0.300f

#define AGC_TARGET 0.5f
#define AGC_MIN 0.5f
#define AGC_MAX 8.0f
#define AGC_FLOOR 0.01f
#define AGC_ATTACK_S 0.050f
#define AGC_RELEASE_S 0.500f

#define BEAT_DELTA 0.05f
#define BEAT_MIN 0.12f
#define BEAT_REFRAC_FRAMES 40u

static float coeff_for(float tau, float fs)
{
    return 1.0f - expf(-1.0f / (tau * fs));
}

static void biquad_init(Biquad *b, float fs, float f0, float q)
{
    float w0 = 2.0f * PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;

    b->b0 = alpha / a0;
    b->b1 = 0.0f;
    b->b2 = -alpha / a0;
    b->a1 = -2.0f * cosf(w0) / a0;
    b->a2 = (1.0f - alpha) / a0;
    b->z1 = 0.0f;
    b->z2 = 0.0f;
}

static float biquad_step(Biquad *b, float x)
{
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

static float clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}

void dsp_init(Dsp *d, float fs)
{
    static const float f0[DSP_BANDS] = {141.0f, 4823.0f, 13225.0f};
    static const float q[DSP_BANDS] = {1.46f, 1.54f, 1.12f};

    memset(d, 0, sizeof(*d));
    if (fs <= 0.0f)
        fs = 96000.0f;
    d->fs = fs;
    d->dc_pole = expf(-2.0f * PI * 15.0f / fs);
    d->env_attack = coeff_for(ENV_ATTACK_S, fs);
    d->env_release = coeff_for(ENV_RELEASE_S, fs);
    d->peak_attack = coeff_for(PEAK_ATTACK_S, fs);
    d->peak_release = coeff_for(PEAK_RELEASE_S, fs);
    d->agc_gain = 1.0f;
    for (int i = 0; i < DSP_BANDS; ++i)
        biquad_init(&d->band[i], fs, f0[i], q[i]);
}

void dsp_process(Dsp *d, float x)
{
    x = clamp01((x + 1.0f) * 0.5f) * 2.0f - 1.0f;

    float y = x - d->dc_x1 + d->dc_pole * d->dc_y1;
    d->dc_x1 = x;
    d->dc_y1 = y;

    float a = fabsf(y);
    if (a > d->peak)
        d->peak += (a - d->peak) * d->peak_attack;
    else
        d->peak += (a - d->peak) * d->peak_release;

    for (int i = 0; i < DSP_BANDS; ++i) {
        float rect = fabsf(biquad_step(&d->band[i], y));
        if (rect > d->env[i])
            d->env[i] += (rect - d->env[i]) * d->env_attack;
        else
            d->env[i] += (rect - d->env[i]) * d->env_release;
    }
}

DspFrame dsp_frame(Dsp *d)
{
    DspFrame f;

    if (d->peak > AGC_FLOOR) {
        float desired = AGC_TARGET / d->peak;
        if (desired < AGC_MIN)
            desired = AGC_MIN;
        if (desired > AGC_MAX)
            desired = AGC_MAX;
        float tau = desired < d->agc_gain ? AGC_ATTACK_S : AGC_RELEASE_S;
        float k = coeff_for(tau, DSP_FRAME_HZ);
        d->agc_gain += (desired - d->agc_gain) * k;
    }

    for (int i = 0; i < DSP_BANDS; ++i) {
        float level = clamp01(d->env[i] * d->agc_gain);
        f.level[i] = level;

        float norm = level / AGC_TARGET;
        float brightness = 0.0f;
        if (norm >= 0.02f) {
            brightness = (float)DSP_BRIGHT_MAX * powf(norm, 1.8f);
            if (brightness > DSP_BRIGHT_MAX)
                brightness = DSP_BRIGHT_MAX;
        }
        f.brightness[i] = (uint8_t)(brightness + 0.5f);
    }

    f.beat = false;
    if (d->refrac > 0)
        d->refrac--;
    if (d->have_prev) {
        float low = f.level[0];
        float delta = low - d->low_prev;
        if (delta > BEAT_DELTA && low > BEAT_MIN && d->refrac == 0) {
            f.beat = true;
            d->refrac = BEAT_REFRAC_FRAMES;
        }
    } else {
        d->have_prev = true;
    }
    d->low_prev = f.level[0];

    return f;
}
