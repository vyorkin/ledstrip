#include "iwdg.h"

#include "stm32f4xx.h"

/* RM0383 18.3: 0xCCCC starts the watchdog, 0x5555 unlocks PR/RLR, 0xAAAA
 * reloads the counter. The LSI starts automatically when the IWDG runs. */
#define IWDG_KEY_START 0xCCCCu
#define IWDG_KEY_UNLOCK 0x5555u
#define IWDG_KEY_RELOAD 0xAAAAu

/* PR=000 is divider 4; RLR is the reload value, so the period is
 * (RLR + 1) * 4 / LSI = 1600 / 32000 s = 50 ms nominal. */
typedef char iwdg_reload_check[(IWDG_RELOAD == 399u) ? 1 : -1];

void iwdg_init(void)
{
    IWDG->KR = IWDG_KEY_START;

    /* The counter restarts at 0xFFF with divider 4 (~512 ms) after 0xCCCC,
     * which leaves ample time for these writes. */
    IWDG->KR = IWDG_KEY_UNLOCK;
    IWDG->PR = IWDG_CLOCK_DIVIDER / 4u - 1u;
    IWDG->RLR = IWDG_RELOAD;

    while (IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) {
    }

    IWDG->KR = IWDG_KEY_RELOAD;
}

void iwdg_refresh(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}
