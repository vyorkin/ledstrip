#include "reset.h"

#include "stm32f4xx.h"

volatile uint32_t g_reset_cause;

void reset_cause_capture(void)
{
    uint32_t csr = RCC->CSR;
    uint32_t cause = 0;

    if (csr & RCC_CSR_IWDGRSTF)
        cause |= RESET_CAUSE_IWDG;
    if (csr & RCC_CSR_WWDGRSTF)
        cause |= RESET_CAUSE_WWDG;
    if (csr & RCC_CSR_LPWRRSTF)
        cause |= RESET_CAUSE_LPWR;
    if (csr & RCC_CSR_SFTRSTF)
        cause |= RESET_CAUSE_SOFT;
    if (csr & RCC_CSR_PORRSTF)
        cause |= RESET_CAUSE_POR;
    if (csr & RCC_CSR_PINRSTF)
        cause |= RESET_CAUSE_PIN;
    if (csr & RCC_CSR_BORRSTF)
        cause |= RESET_CAUSE_BOR;

    g_reset_cause = cause;

    /* Flags are sticky across resets; clear them so the next boot reports its
     * own cause and a delayed reset is not mistaken for the previous one. */
    RCC->CSR |= RCC_CSR_RMVF;
}
