# Target firmware: STM32F411CE, clock and heartbeat

Ticket #4: the smallest bare-metal target build. It proves the ARM toolchain,
the 100 MHz clock setup and the board's PC13 LED path. No ADC, WS2812, DSP,
USB, HAL or CubeMX code is here.

## Build

Requires `arm-none-eabi-gcc` and `make`.

```sh
cd firmware-stm32
make target
```

Outputs in `target/build/`:

| File | Purpose |
|---|---|
| `ledstrip.elf` | ELF with debug symbols |
| `ledstrip.hex` | Intel HEX for SWD/programmers |
| `ledstrip.bin` | raw image for `st-flash` |
| `ledstrip.map` | link map and section sizes |

`make target` also prints the memory map against the F411CE limits (512 KB
flash, 128 KB RAM):

```
Memory region         Used Size  Region Size  %age Used
           FLASH:        1168 B       512 KB      0.22%
             RAM:        3088 B       128 KB      2.36%
```

## Check without a board

```sh
cd firmware-stm32
make check
```

Runs a native host smoke test; it does not need the ARM toolchain or hardware.
The host DSP/frame suite stays at `make test`.

## Flash and run

ST-Link over SWD, board powered from its 5 V pin (do not feed USB and external
5 V at the same time):

```sh
cd firmware-stm32
make target
st-flash --reset write target/build/ledstrip.bin 0x08000000
```

`st-flash` comes from the `stlink` package. `st-info --probe` should show the
STM32F411 target before flashing.

Expected result: PC13 blinks at about 1 Hz (500 ms on, 500 ms off). PC13 is
active low, so the LED is off at reset and starts blinking from the first
500 ms tick.

## Clock

`SystemInit()` (CMSIS) sets the FPU and vector table. `clock_init_100mhz()`
enables the PWR clock and selects regulator scale 1 (100 MHz; reset is
scale 2, 84 MHz max). It then configures the PLL with LL calls: 25 MHz
HSE / 25 = 1 MHz, × 200 = 200 MHz VCO, / 2 = 100 MHz SYSCLK; AHB /1,
APB1 /2, APB2 /1, 3 flash wait states. It waits for PLL lock and VOS ready
before switching. `SystemCoreClockUpdate()` loads `SystemCoreClock`, and the
1 ms SysTick is derived from it.

If HSE or the PLL does not become ready within the timeout, the firmware stays
on the 16 MHz HSI fallback and sets `g_clock_fallback = 1` in RAM; the LED still
blinks at 1 Hz. Read `SystemCoreClock` or `g_clock_fallback` in the debugger to
confirm the clock source.

## Pin map for this slice

| Signal | Pin |
|---|---|
| Heartbeat LED | PC13, active low |
| SWD | PA13/PA14 |

## Faults

No target faults are expected in this slice. If one fires, the CMSIS startup
vector sends it to `Default_Handler`, which loops with the state intact; halt
the core in the debugger and inspect it. There is no watchdog yet (ticket #10).
