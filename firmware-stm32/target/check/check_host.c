#include <stdio.h>

/* Host-side smoke check: proves the target tree's host test path builds and
 * runs without arm-none-eabi-gcc or a board. Firmware behaviour is covered on
 * the bench, not here. */
int main(void)
{
    puts("target check: host toolchain ok");
    return 0;
}
