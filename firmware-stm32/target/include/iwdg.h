#ifndef LEDSTRIP_IWDG_H
#define LEDSTRIP_IWDG_H

/* Independent watchdog: LSI / 4, 400 counts, refreshed once per 200 Hz frame.
 * LSI is 32 kHz nominal (17...47 kHz over temperature and part spread), so the
 * nominal timeout is 50 ms and the worst case is about 94 ms. A debugger halt,
 * a stuck strip transfer or a wedged main loop resets the MCU; the strip is
 * re-cleared by the startup path. */
#define IWDG_TIMEOUT_MS 50u
#define IWDG_LSI_HZ 32000u
#define IWDG_CLOCK_DIVIDER 4u
#define IWDG_RELOAD \
    ((IWDG_LSI_HZ / 1000u * IWDG_TIMEOUT_MS) / IWDG_CLOCK_DIVIDER - 1u)

void iwdg_init(void);

/* Reload the down-counter. Call once per DspFrame / WS2812 frame; never from
 * an interrupt that can outlive the frame loop. */
void iwdg_refresh(void);

#endif
