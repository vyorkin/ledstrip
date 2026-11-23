# Target firmware: STM32F411CE, clock, ADC/DSP, button and music-driven WS2812

Tickets #4, #5, #6, #8 and #9: the bare-metal target build proves the ARM
toolchain, the 100 MHz clock setup, the board's PC13 LED path, the PA1 ADC
capture, the DSP frame, the music-to-light loop on PB0 and the PA0 mode
button. No watchdog, USB, HAL or CubeMX code is here.

Each completed 480-sample half runs `dsp_process()` and produces one
`DspFrame` at 200 Hz. The frame's brightness sets the three packet levels,
its beat advances the every-second-beat flip, and `frame_step()` /
`frame_render()` build the 360 GRB bytes. `ws2812_output_show()` encodes them
into 2880 CCR3 halfwords and shifts them out with TIM3_CH3 + DMA. The strip
therefore gets a fresh frame every 5 ms; each transfer blocks the CPU for
3.6 ms, leaving about 1.4 ms for DSP and the next half's preparation.

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
           FLASH:       12060 B       512 KB      2.30%
             RAM:       11400 B       128 KB      8.70%
```

Most of the RAM is the 2880-halfword WS2812 buffer (5760 B) and the two
480-sample ADC halves (1920 B).

The ADC/DSP slice lives in `src/adc_input.c`; the button logic is the portable
`../src/button.c`. Both are linked from the normal target only; `make selftest`
stays clock-only.

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

At power-up the firmware drives PB0 low and sends one all-zero frame, so a
strip that latched garbage is cleared without a white flash. The strip then
stays dark until the input has signal: silence gives brightness 0.

With a signal on PA1 and a strip on PB0 (through the level shifter and
330 Ω), the low band lights the red packet 10…29, the middle band green
40…59 and the high band blue 80…99. Each packet moves 30 LEDs/s; every
second low-band beat reverses all three directions. Music on the line input
therefore gives the three colors moving with their band levels. If the
100 MHz PLL fails, the firmware leaves PB0 low and only blinks PC13 at 5 Hz:
the wrong bit rate must not reach the strip, and ADC/DSP capture stays
stopped.

Halt in the debugger and read `g_dsp_frame`, `g_dsp_frame_count` and
`g_adc_overruns`; feed a known tone at PA1 and check the expected band.
`g_dsp_frame_count` grows at about 200/s and equals the number of
DSP-driven strip frames; the power-up clear frame is one more.

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

## ADC capture and DSP snapshot

| Parameter | Value |
|---|---|
| Pin | PA1, ADC1 channel 1, analog |
| Sample rate | 96 kHz: TIM2 ARR 1041, 95.97 kHz actual |
| Trigger | TIM2 update event -> TRGO, EXTSEL 0110 |
| ADC clock | APB2 100 MHz / 4 = 25 MHz |
| Sample time | 84 ADC clocks |
| DMA | DMA2 Stream 0, channel 0, circular, halfword |
| Buffer | 960 samples: two 480-sample halves |
| Frame | 480 samples = 5 ms = 200 Hz |
| Interrupts | DMA2 Stream 0 half/full transfer |

`adc_input_process()` consumes every completed half, converts each 12-bit
sample around 2048 and runs `dsp_process()`. Each completed 480-sample frame
goes to the sink, which in the normal target advances the packets, renders
GRB and shifts the strip frame. It updates three debugger symbols and never
prints:

| Symbol | Meaning |
|---|---|
| `g_dsp_frame` | last `DspFrame` (brightness, beat, AGC levels) |
| `g_dsp_frame_count` | completed frames since reset (≈200/s) |
| `g_adc_overruns` | dropped half-frames and DMA/ADC errors |

The normal target starts capture only after the strip has been cleared and
only when the PLL clock is valid. Frame state is initialized before capture:
positions 10/40/80, mode 0, `flip` and the hit counter zero. The HSI fallback
leaves TIM2 and DMA stopped, because the sample rate and the WS2812 bit rate
would both be wrong.

The ADR for the WS2812 CCR values is
[`0003-ws2812-timing.md`](../../docs/adr/0003-ws2812-timing.md).

## Button

PA0 is the Black Pill KEY button, input with the internal pull-up, active low.
It is sampled once per 200 Hz DSP frame, before that frame's `frame_step()`.
Five consecutive pressed frames (25 ms) call `frame_next_mode()` once; a
shorter press is ignored, and holding the button does not repeat until it is
released. The mode advances 0→1→2→3→0 using the four AVR direction rows.
Only `dir` changes: positions, `flip` and the hit counter are preserved, so
there is no reset and no EEPROM write.

`frame_next_mode()` runs before the movement step, so the new directions apply
to the same frame that confirms the press. Add `s_button.count` and
`s_frame.mode` to the debugger watch to observe the debounce and the mode.

## Pin map for this slice

| Signal | Pin |
|---|---|
| Heartbeat LED | PC13, active low |
| WS2812 data | PB0, TIM3_CH3, AF2 |
| Audio input | PA1, ADC1 channel 1 |
| Mode button | PA0, input, internal pull-up, active low |
| MCO1 clock output (self-test only) | PA8, AF0 |
| SWD | PA13/PA14 |

## Faults

No target faults are expected in this slice. If one fires, the CMSIS startup
vector sends it to `Default_Handler`, which loops with the state intact; halt
the core in the debugger and inspect it. There is no watchdog yet (ticket #10).
The WS2812 transfer polls DMA/TIM flags with a bounded spin; a failed transfer
returns with PB0 low. Check `DMA1_Stream2->NDTR`, `DMA1->LISR` and `TIM3->SR`
in the debugger.

For ADC faults check `DMA2_Stream0->NDTR`, `DMA2->LISR`, `ADC1->SR` (OVR) and
`g_adc_overruns`. A rising overrun count means the main loop did not consume a
480-sample half within its 5 ms window — the 3.6 ms strip transfer plus DSP no
longer fit — or a DMA/ADC error fired. Compare `g_dsp_frame_count` with the
expected 200/s to see whether frames are being lost.
