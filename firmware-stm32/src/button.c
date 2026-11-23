#include "ledstrip/button.h"

void button_init(ButtonState *b)
{
    b->count = 0;
    b->fired = false;
}

bool button_frame(ButtonState *b, bool pressed)
{
    if (!pressed) {
        b->count = 0;
        b->fired = false;
        return false;
    }

    if (b->fired)
        return false;

    if (b->count < BUTTON_DEBOUNCE_FRAMES)
        b->count++;
    if (b->count < BUTTON_DEBOUNCE_FRAMES)
        return false;

    b->fired = true;
    return true;
}
