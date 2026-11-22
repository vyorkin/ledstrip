# Target firmware: STM32F411CE, clock, heartbeat and static WS2812

Tickets #4 and #5: the bare-metal target build proves the ARM toolchain,
the 100 MHz clock setup, the board's PC13 LED path and one static WS2812
frame on PB0. No ADC, DSP, button, dynamic rendering, USB, HAL or CubeMX
code is here.

The strip frame is fixed: the three packets of `frame_init()` (red
10…29, green 40…59, blue 80…99) at brightness 204 (80 %). It is rendered
once by `frame_render()`, encoded into 2880 CCR3 halfwords and shifted out
by TIM3_CH3 + DMA. The same frame is refreshed once per second; the strip
latches it and needs no refresh, so this only recovers from a data glitch.

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
           FLASH:        2160 B       512 KB      0.41%
             RAM:        8848 B       128 KB      6.75%
```

Most of the RAM is the 2880-halfword DMA buffer (5760 B).

## Check without a board

```sh
cd firmware-stm32
make check
```

Runs a native host smoke test; it does not need the ARM toolchain or hardware.
The host DSP/frame/WS2812-encoder suite stays at `make test`.

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

With a strip on PB0 (through the level shifter and 330 Ω), LEDs 10…29 are
red, 40…59 green and 80…99 blue; the other LEDs stay off. At power-up the
firmware drives PB0 low, sends an all-zero frame, waits at least 50 µs and
only then sends the pattern, so a strip that latched garbage at power-up is
cleared without a white flash. If the 100 MHz PLL fails, the firmware leaves
PB0 low and only blinks PC13 at 5 Hz: the wrong bit rate must not reach the
strip.

## Clock

`SystemInit()` (CMSIS) sets the FPU and vector table. `clock_init_100mhz()`
enables the PWR clock and selects regulator scale 1 (100 MHz; reset is
scale 2, 84 MHz max). It then configures the PLL with LL calls: 25 MHz
HSE / 25 = 1 MHz, × 200 = 200 MHz VCO, / 2 = 100 MHz SYSCLK; AHB /1,
APB1 /2, APB2 /1, 3 flash wait states. It waits for PLL lock and VOS ready
before switching. `SystemCoreClockUpdate()` loads `SystemCoreClock`, and the
1 ms SysTick is derived from it.

If HSE or the PLL does not become ready within the timeout, the firmware stays
on the 16 MHz HSI fallback and sets `g_clock_fallback = 1` in RAM. The LED then
blinks at 5 Hz instead of 1 Hz, so the failure is visible without a debugger.
Read `SystemCoreClock` or `g_clock_fallback` in the debugger to confirm the
clock source.

## Clock self-test build

`make selftest` produces `target/build/clockselftest.elf/.hex/.bin`. It is the
same clock and heartbeat code plus MCO1 on PA8 (AF0) = PLLCLK / 5. At 100 MHz
PA8 outputs 20 MHz. PA8 is on the board header, unlike PC9 (MCO2), which the
WeAct Black Pill does not route out. On the HSI fallback PLLCLK is not running,
so PC13 blinks at 5 Hz and PA8 is not a usable clock. This lets a scope prove
the 100 MHz clock without a debugger.

## Remote bench package

`make dist` builds the normal and self-test images and copies them with the
Russian bench guide and results template into `dist/bench/`:

```sh
cd firmware-stm32
make dist
```

See [`docs/bench-remote.md`](../../docs/bench-remote.md) and
[`docs/bench-results.md`](../../docs/bench-results.md).

## WS2812 output

| Parameter | Value |
|---|---|
| Pin | PB0, TIM3_CH3, AF2 |
| Timer clock | 100 MHz (APB1 /2, timer ×2) |
| Bit rate | 800 kHz: ARR 124, 125 ticks |
| CCR3 | 38 for 0 (T0H 0.38 µs), 75 for 1 (T1H 0.75 µs) |
| T0L / T1L | 0.87 µs / 0.50 µs (contract 0.875 / 0.5) |
| DMA | DMA1 Stream 2, channel 5, TIM3 update request |
| Buffer | 2880 halfwords = 360 GRB bytes |
| Frame time | 3.6 ms |
| Reset gap | PB0 low between frames, ≥50 µs |

`ws2812_encode()` expands each GRB byte MSB first. The first halfword is
loaded by a software `UG` update before the counter starts; the next 2879
updates each load one halfword, for 2880 DMA transfers. After the last
bit's falling edge the firmware stops TIM3 with `CCR3 = 0`, so PB0 goes
low without a glitch and stays low for the reset gap. The CPU only starts
the transfer and waits for the flags; it never emits bits.

## Pin map for this slice

| Signal | Pin |
|---|---|
| Heartbeat LED | PC13, active low |
| WS2812 data | PB0, TIM3_CH3, AF2 |
| MCO1 clock output (self-test only) | PA8, AF0 |
| SWD | PA13/PA14 |

## Faults

No target faults are expected in this slice. If one fires, the CMSIS startup
vector sends it to `Default_Handler`, which loops with the state intact; halt
the core in the debugger and inspect it. There is no watchdog yet (ticket #10).
The WS2812 transfer polls DMA/TIM flags with a bounded spin; a failed transfer
returns with PB0 low. Check `DMA1_Stream2->NDTR`, `DMA1->LISR` and `TIM3->SR`
in the debugger.
