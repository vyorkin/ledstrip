#include "ledstrip/dsp.h"
#include "ledstrip/frame.h"
#include "ledstrip/ws2812.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define FS 96000.0f

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static DspFrame run_tone(Dsp *d, float freq, float amp, float seconds)
{
    int total = (int)(FS * seconds);
    int acc = 0;
    DspFrame last = {0};

    for (int i = 0; i < total; ++i) {
        float x = amp * sinf(2.0f * PI * freq * (float)i / FS);
        dsp_process(d, x);
        if (++acc == (int)DSP_FRAME_SAMPLES) {
            last = dsp_frame(d);
            acc = 0;
        }
    }
    return last;
}

static void test_bands(void)
{
    static const float freq[DSP_BANDS] = {141.0f, 4823.0f, 13225.0f};

    for (int ch = 0; ch < DSP_BANDS; ++ch) {
        Dsp d;
        dsp_init(&d, FS);
        DspFrame f = run_tone(&d, freq[ch], 0.5f, 1.0f);

        printf("band %d: %u %u %u\n", ch, f.brightness[0], f.brightness[1],
               f.brightness[2]);
        CHECK(f.brightness[ch] > 100);
        for (int other = 0; other < DSP_BANDS; ++other) {
            if (other != ch)
                CHECK(f.brightness[ch] > f.brightness[other]);
        }
    }
}

static void test_agc(void)
{
    Dsp quiet;
    dsp_init(&quiet, FS);
    DspFrame q = run_tone(&quiet, 141.0f, 0.05f, 2.0f);

    Dsp loud;
    dsp_init(&loud, FS);
    DspFrame l = run_tone(&loud, 141.0f, 0.8f, 1.0f);

    printf("agc: quiet=%u loud=%u\n", q.brightness[0], l.brightness[0]);
    CHECK(q.brightness[0] > 80);
    CHECK(q.brightness[0] <= DSP_BRIGHT_MAX);
    CHECK(l.brightness[0] >= 150);
    CHECK(l.brightness[0] <= DSP_BRIGHT_MAX);
}

static void test_beat(void)
{
    Dsp d;
    dsp_init(&d, FS);
    int acc = 0;
    int beats = 0;

    for (int burst = 0; burst < 6; ++burst) {
        for (int i = 0; i < 4800; ++i) {
            float x = 0.8f * sinf(2.0f * PI * 100.0f * (float)i / FS);
            dsp_process(&d, x);
            if (++acc == (int)DSP_FRAME_SAMPLES) {
                if (dsp_frame(&d).beat)
                    beats++;
                acc = 0;
            }
        }
        for (int i = 0; i < 43200; ++i) {
            dsp_process(&d, 0.0f);
            if (++acc == (int)DSP_FRAME_SAMPLES) {
                if (dsp_frame(&d).beat)
                    beats++;
                acc = 0;
            }
        }
    }
    printf("beats: %d\n", beats);
    CHECK(beats >= 5 && beats <= 6);
}

static void test_frame(void)
{
    FrameState st;
    uint8_t out[FRAME_NBYTES];
    uint8_t red[3] = {204, 0, 0};

    frame_init(&st);
    CHECK(st.pos[0] == FRAME_MAXPOS * 10 / 100);
    CHECK(st.pos[1] == FRAME_MAXPOS * 40 / 100);
    CHECK(st.pos[2] == FRAME_MAXPOS * 80 / 100);

    frame_render(&st, red, out);
    CHECK(out[10 * 3 + 1] == 204);
    CHECK(out[29 * 3 + 1] == 204);
    CHECK(out[30 * 3 + 1] == 0);
    CHECK(out[10 * 3 + 0] == 0);
    CHECK(out[10 * 3 + 2] == 0);

    int32_t before = st.pos[0];
    frame_step(&st, red, false);
    CHECK(st.pos[0] == before + FRAME_SPEED);

    FrameState edge;
    frame_init(&edge);
    edge.pos[0] = FRAME_MAXPOS - 10;
    edge.dir[0] = 0;
    frame_step(&edge, red, false);
    CHECK(edge.pos[0] == FRAME_MAXPOS - 28);
    CHECK(edge.dir[0] == 1);

    frame_init(&edge);
    edge.pos[0] = 5;
    edge.dir[0] = 1;
    frame_step(&edge, red, false);
    CHECK(edge.pos[0] == 33);
    CHECK(edge.dir[0] == 0);

    FrameState beat;
    frame_init(&beat);
    frame_step(&beat, red, true);
    CHECK(beat.hitcnt == 1);
    CHECK(beat.flip == 0);
    frame_step(&beat, red, true);
    CHECK(beat.hitcnt == 0);
    CHECK(beat.flip == 1);

    int32_t p = beat.pos[0];
    frame_step(&beat, red, false);
    CHECK(beat.pos[0] == p - FRAME_SPEED);

    frame_set_mode(&beat, 1);
    CHECK(beat.dir[0] == 1 && beat.dir[1] == 0 && beat.dir[2] == 1);
    frame_set_mode(&beat, 2);
    CHECK(beat.dir[0] == 0 && beat.dir[1] == 0 && beat.dir[2] == 1);
    frame_set_mode(&beat, 3);
    CHECK(beat.dir[0] == 0 && beat.dir[1] == 1 && beat.dir[2] == 0);
    CHECK(beat.flip == 1);

    FrameState overlap;
    frame_init(&overlap);
    overlap.pos[0] = 10 * 256;
    overlap.pos[1] = 10 * 256;
    uint8_t both[3] = {100, 100, 0};
    frame_render(&overlap, both, out);
    CHECK(out[10 * 3 + 0] == 100);
    CHECK(out[10 * 3 + 1] == 100);
    CHECK(out[10 * 3 + 2] == 0);
}

static void test_ws2812(void)
{
    uint8_t grb[WS2812_NBYTES];
    uint16_t ccr[WS2812_NBITS];

    CHECK(WS2812_NBYTES == 360);
    CHECK(WS2812_NBITS == 2880);
    CHECK(WS2812_ARR + 1u == 125u);

    /* Contract in ns at 10 ns per tick (docs/interface.md):
     * T0H 375, T1H 750, T0L 875, T1L 500. T0H rounds to 38 ticks. */
    CHECK((WS2812_ARR + 1u) * 10u == 1250u);
    CHECK(WS2812_CCR_ZERO * 10u >= 370u &&
          WS2812_CCR_ZERO * 10u <= 380u);
    CHECK(WS2812_CCR_ONE * 10u == 750u);
    CHECK((WS2812_ARR + 1u - WS2812_CCR_ZERO) * 10u >= 870u &&
          (WS2812_ARR + 1u - WS2812_CCR_ZERO) * 10u <= 880u);
    CHECK((WS2812_ARR + 1u - WS2812_CCR_ONE) * 10u == 500u);

    memset(grb, 0, sizeof grb);
    ws2812_encode(grb, ccr);
    for (int i = 0; i < WS2812_NBITS; ++i)
        CHECK(ccr[i] == WS2812_CCR_ZERO);

    grb[0] = 0x80;
    ws2812_encode(grb, ccr);
    CHECK(ccr[0] == WS2812_CCR_ONE);
    for (int i = 1; i < WS2812_NBITS; ++i)
        CHECK(ccr[i] == WS2812_CCR_ZERO);

    grb[0] = 0x01;
    ws2812_encode(grb, ccr);
    CHECK(ccr[6] == WS2812_CCR_ZERO);
    CHECK(ccr[7] == WS2812_CCR_ONE);

    FrameState st;
    uint8_t rgb[3] = {204, 204, 204};
    frame_init(&st);
    frame_render(&st, rgb, grb);

    CHECK(grb[10 * 3 + 0] == 0);
    CHECK(grb[10 * 3 + 1] == 204);
    CHECK(grb[10 * 3 + 2] == 0);
    CHECK(grb[40 * 3 + 0] == 204);
    CHECK(grb[40 * 3 + 1] == 0);
    CHECK(grb[80 * 3 + 2] == 204);

    ws2812_encode(grb, ccr);
    static const uint8_t bits_204[8] = {1, 1, 0, 0, 1, 1, 0, 0};
    for (int i = 0; i < 8; ++i) {
        uint8_t expected = bits_204[i] ? WS2812_CCR_ONE : WS2812_CCR_ZERO;
        CHECK(ccr[(10 * 3 + 1) * 8 + i] == expected);
        CHECK(ccr[(40 * 3 + 0) * 8 + i] == expected);
        CHECK(ccr[(80 * 3 + 2) * 8 + i] == expected);
    }
}

int main(void)
{
    test_bands();
    test_agc();
    test_beat();
    test_frame();
    test_ws2812();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
