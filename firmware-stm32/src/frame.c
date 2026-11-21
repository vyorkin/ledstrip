#include "ledstrip/frame.h"

#include <string.h>

static const int8_t mode_dirs[4][3] = {
    {0, 0, 0},
    {1, 0, 1},
    {0, 0, 1},
    {0, 1, 0},
};

static const uint8_t color_offset[3] = {1, 0, 2}; /* GRB */

void frame_init(FrameState *st)
{
    memset(st, 0, sizeof(*st));
    st->pos[0] = FRAME_MAXPOS * 10 / 100;
    st->pos[1] = FRAME_MAXPOS * 40 / 100;
    st->pos[2] = FRAME_MAXPOS * 80 / 100;
    frame_set_mode(st, 0);
}

void frame_set_mode(FrameState *st, uint8_t mode)
{
    st->mode = mode & 3u;
    for (int i = 0; i < 3; ++i)
        st->dir[i] = mode_dirs[st->mode][i];
}

void frame_step(FrameState *st, const uint8_t brightness[3], bool beat)
{
    (void)brightness;

    if (beat) {
        if (++st->hitcnt >= 2u) {
            st->hitcnt = 0;
            st->flip ^= 1u;
        }
    }

    for (int i = 0; i < 3; ++i) {
        int dir = st->dir[i] ^ (st->flip & 1u);
        if (dir)
            st->pos[i] -= FRAME_SPEED;
        else
            st->pos[i] += FRAME_SPEED;

        if (st->pos[i] < 0) {
            st->pos[i] = -st->pos[i];
            st->dir[i] ^= 1;
        } else if (st->pos[i] > FRAME_MAXPOS) {
            st->pos[i] = 2 * FRAME_MAXPOS - st->pos[i];
            st->dir[i] ^= 1;
        }
    }
}

void frame_render(const FrameState *st, const uint8_t brightness[3],
                  uint8_t out[FRAME_NBYTES])
{
    memset(out, 0, FRAME_NBYTES);

    for (int ch = 0; ch < 3; ++ch) {
        uint8_t b = brightness[ch];
        if (b == 0)
            continue;

        int start = (int)(st->pos[ch] >> 8);
        if (start < 0 || start + FRAME_PKT > FRAME_NLED)
            continue;

        int off = color_offset[ch];
        for (int i = 0; i < FRAME_PKT; ++i) {
            int idx = (start + i) * 3 + off;
            int v = out[idx] + b;
            out[idx] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}
