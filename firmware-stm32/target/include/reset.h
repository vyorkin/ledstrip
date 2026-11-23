#ifndef LEDSTRIP_RESET_H
#define LEDSTRIP_RESET_H

#include <stdint.h>

/* g_reset_cause bits, captured once at boot from RCC->CSR before the sticky
 * flags are cleared. Several bits can be set together: a power-on reset also
 * pulls NRST low, for example. Read the symbol in the debugger. */
#define RESET_CAUSE_IWDG (1u << 0) /* independent watchdog */
#define RESET_CAUSE_WWDG (1u << 1) /* window watchdog */
#define RESET_CAUSE_LPWR (1u << 2) /* low-power reset */
#define RESET_CAUSE_SOFT (1u << 3) /* software reset */
#define RESET_CAUSE_POR (1u << 4)  /* power-on reset */
#define RESET_CAUSE_PIN (1u << 5)  /* NRST pin */
#define RESET_CAUSE_BOR (1u << 6)  /* brown-out reset */

extern volatile uint32_t g_reset_cause;

/* Read and clear RCC->CSR reset flags; call once before the clock setup. */
void reset_cause_capture(void);

#endif
