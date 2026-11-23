#ifndef LEDSTRIP_FRAME_H
#define LEDSTRIP_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_NLED 120
#define FRAME_PKT 20
#define FRAME_NBYTES (FRAME_NLED * 3)
#define FRAME_MAXPOS ((FRAME_NLED - FRAME_PKT) * 256)
#define FRAME_SPEED 38

typedef struct {
    int32_t pos[3];
    int8_t dir[3];
    uint8_t flip;
    uint8_t mode;
    uint8_t hitcnt;
} FrameState;

void frame_init(FrameState *st);
void frame_set_mode(FrameState *st, uint8_t mode);
/* Advance 0 -> 1 -> 2 -> 3 -> 0. Only directions change; positions, flip and
 * the hit counter are preserved, so the switch takes effect on the next step. */
void frame_next_mode(FrameState *st);
void frame_step(FrameState *st, const uint8_t brightness[3], bool beat);
void frame_render(const FrameState *st, const uint8_t brightness[3],
                  uint8_t out[FRAME_NBYTES]);

#endif
