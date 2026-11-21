#include "ledstrip/dsp.h"
#include "ledstrip/frame.h"

#include <math.h>
#include <stdio.h>

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

int main(void)
{
    test_bands();
    test_agc();
    test_beat();
    test_frame();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
