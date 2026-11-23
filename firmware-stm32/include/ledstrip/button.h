#ifndef LEDSTRIP_BUTTON_H
#define LEDSTRIP_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

/* PA0 is sampled once per 200 Hz LED frame: five consecutive pressed frames
 * are 25 ms. A release restarts the count. */
#define BUTTON_DEBOUNCE_FRAMES 5u

typedef struct {
    uint8_t count;
    bool fired;
} ButtonState;

void button_init(ButtonState *b);

/* Feed one frame's pressed state (true = active low pin is low). Returns true
 * exactly once per confirmed press; holding the button does not repeat. */
bool button_frame(ButtonState *b, bool pressed);

#endif
