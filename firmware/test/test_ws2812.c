/*
 *  Стенд для прошивки цветомузыки: simavr + разбор битового потока WS2812.
 *
 *  Проверяет:
 *    1. длины импульсов 6/12 тактов, период 20 тактов внутри байта,
 *       пауза сброса >= 50 мкс, ровно 360 байт в кадре;
 *    2. пакет красного канала на стартовой позиции и его яркость;
 *    3. разворот на втором ударе по низкой полосе;
 *    4. переключение режима кнопкой PD2.
 *
 *  Запуск: build/test_ws2812 build/ledstrip.hex
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <avr_adc.h>
#include <avr_ioport.h>
#include <sim_avr.h>
#include <sim_hex.h>
#include <sim_io.h>
#include <sim_irq.h>

/* Контракт прошивки: при смене NLED в main.asm обновите и эти числа. */
#define CPU_HZ 16000000u
#define NLED 120
#define NBYTES (NLED * 3)
#define FRAME_CYCLES (CPU_HZ / 200u)      /* 5 мс */
#define RESET_MIN_CYCLES (CPU_HZ / 20000u) /* 50 мкс */
#define MAX_FRAMES 512

typedef struct {
    uint8_t data[NBYTES];
    uint64_t start;
    uint64_t end;
    uint64_t reset;
    int red, green, blue;
} frame_t;

typedef struct {
    avr_t *avr;
    avr_irq_t *pd2;
    avr_irq_t *adc;
    int frames;

    int frame_pos;
    int bit_index;
    uint8_t byte;
    int in_frame;
    int expect_gap;
    uint64_t rise_cycle;
    uint64_t last_fall;
    uint64_t last_rise;
    uint64_t frame_start;
    uint8_t current[NBYTES];
    frame_t done[MAX_FRAMES];

    int errors;
    int bad_high;
    int bad_period;
    int incomplete;
    uint64_t min_reset;
    uint64_t max_period;
    uint64_t max_boundary;
} ctx_t;

static void check(int cond, const char *fmt, ...)
{
    if (cond)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FAIL: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static int first_lit(const uint8_t *data, int offset)
{
    for (int i = 0; i < NLED; i++)
        if (data[i * 3 + offset])
            return i;
    return -1;
}

static void finish_frame(ctx_t *c, uint64_t now)
{
    check(c->frames < MAX_FRAMES, "more than %d frames", MAX_FRAMES);
    frame_t *f = &c->done[c->frames++];
    memcpy(f->data, c->current, NBYTES);
    f->start = c->frame_start;
    f->end = now;
    f->reset = 0;
    f->red = first_lit(f->data, 1);
    f->green = first_lit(f->data, 0);
    f->blue = first_lit(f->data, 2);
    c->frame_pos = 0;
    c->bit_index = 0;
    c->byte = 0;
    c->expect_gap = 1;
    c->last_rise = 0;
}

static void pin_hook(avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    ctx_t *c = param;
    uint64_t now = c->avr->cycle;

    if (value) {
        if (c->expect_gap) {
            uint64_t gap = now - c->last_fall;
            if (c->frames > 0)
                c->done[c->frames - 1].reset = gap;
            if (gap < RESET_MIN_CYCLES)
                c->errors++;
            if (!c->min_reset || gap < c->min_reset)
                c->min_reset = gap;
            c->expect_gap = 0;
        }

        if (c->frame_pos == 0 && c->bit_index == 0 && !c->in_frame)
            c->frame_start = now;

        if (c->last_rise) {
            uint64_t period = now - c->last_rise;
            if (c->bit_index == 0) {
                /* последний бит прошлого байта несёт служебные такты */
                if (c->frame_pos > 0) {
                    if (period < 27 || period > 31)
                        c->bad_period++;
                    if (period > c->max_boundary)
                        c->max_boundary = period;
                }
            } else {
                if (period < 19 || period > 21)
                    c->bad_period++;
                if (period > c->max_period)
                    c->max_period = period;
            }
        }
        c->last_rise = now;
        c->rise_cycle = now;
        c->in_frame = 1;
    } else {
        if (!c->in_frame)
            return;
        uint64_t high = now - c->rise_cycle;
        int bit;
        if (high == 12)
            bit = 1;
        else if (high == 6)
            bit = 0;
        else {
            c->bad_high++;
            bit = high > 9;
        }
        c->byte = (uint8_t)((c->byte << 1) | bit);
        c->bit_index++;
        if (c->bit_index == 8) {
            c->current[c->frame_pos++] = c->byte;
            c->byte = 0;
            c->bit_index = 0;
            if (c->frame_pos == NBYTES)
                finish_frame(c, now);
        }
        c->last_fall = now;
        c->in_frame = 0;
    }
}

static ctx_t *ctx_new(const char *firmware)
{
    uint32_t size = 0, base = 0;
    uint8_t *code = read_ihex_file(firmware, &size, &base);
    if (!code) {
        fprintf(stderr, "cannot load %s\n", firmware);
        exit(2);
    }

    avr_t *avr = avr_make_mcu_by_name("atmega16");
    if (!avr) {
        fprintf(stderr, "simavr has no atmega16\n");
        exit(2);
    }
    avr_init(avr);
    avr->vcc = avr->avcc = avr->aref = 5000;
    avr->frequency = CPU_HZ;
    avr_loadcode(avr, code, size, base);
    free(code);

    ctx_t *c = calloc(1, sizeof(*c));
    if (!c)
        exit(2);
    c->avr = avr;
    c->adc = avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0);
    c->pd2 = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 2);

    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0), pin_hook, c);
    return c;
}

static void ctx_free(ctx_t *c)
{
    avr_terminate(c->avr);
    free(c->avr);
    free(c);
}

static void set_adc(ctx_t *c, int channel, uint32_t millivolts)
{
    avr_raise_irq(c->adc + ADC_IRQ_ADC0 + channel, millivolts);
}

static void set_all_adc(ctx_t *c, uint32_t millivolts)
{
    for (int i = 0; i < 3; i++)
        set_adc(c, i, millivolts);
}

static void step(ctx_t *c)
{
    int state = avr_run(c->avr);
    if (state == cpu_Done || state == cpu_Crashed) {
        fprintf(stderr, "FAIL: CPU stopped (state %d) at cycle %llu, pc=%04x\n",
                state, (unsigned long long)c->avr->cycle, c->avr->pc);
        exit(1);
    }
    if (c->errors > 1000) {
        fprintf(stderr, "FAIL: decoder gave up at cycle %llu\n",
                (unsigned long long)c->avr->cycle);
        exit(1);
    }
}

static void run_frames(ctx_t *c, int n)
{
    int target = c->frames + n;
    uint64_t limit = c->avr->cycle + (uint64_t)(n + 4) * FRAME_CYCLES * 2;
    while (c->frames < target && c->avr->cycle < limit)
        step(c);
    check(c->frames >= target, "expected %d frames, got %d", target, c->frames);
}

static const frame_t *last_frame(ctx_t *c)
{
    check(c->frames > 0, "no frames decoded");
    return &c->done[c->frames - 1];
}

static void check_timing(ctx_t *c)
{
    check(c->bad_high == 0, "%d bits with wrong high time", c->bad_high);
    check(c->bad_period == 0, "%d bits with wrong period", c->bad_period);
    check(c->errors == 0, "%d framing errors", c->errors);
    check(c->min_reset >= RESET_MIN_CYCLES,
          "reset gap %llu cycles < 50 us",
          (unsigned long long)c->min_reset);
}

static void test_timing(const char *hex)
{
    ctx_t *c = ctx_new(hex);
    set_all_adc(c, 0);
    run_frames(c, 3);
    check_timing(c);
    /* WDTCR: I/O 0x21, значит в data space 0x41 */
    check((c->avr->data[0x41] & 0x08) == 0x08, "WDT is not enabled");
    check((c->avr->data[0x41] & 0x07) == 0x07,
          "WDT prescaler is not 2 s (%02x)", c->avr->data[0x41]);
    for (int i = c->frames - 3; i < c->frames; i++)
        for (int b = 0; b < NBYTES; b++)
            check(c->done[i].data[b] == 0,
                  "silence frame %d byte %d = %u", i, b,
                  c->done[i].data[b]);
    const frame_t *f = last_frame(c);
    printf("ok  timing: %d frames, high 6/12, period 20, "
           "reset min %llu cycles, data span %llu us\n",
           c->frames, (unsigned long long)c->min_reset,
           (unsigned long long)((f->end - f->start) / (CPU_HZ / 1000000u)));
    ctx_free(c);
}

static void test_packet(const char *hex)
{
    ctx_t *c = ctx_new(hex);
    set_all_adc(c, 5000);
    run_frames(c, 2);
    const frame_t *f = last_frame(c);
    check(f->red == 10, "red packet starts at %d, expected 10", f->red);
    check(f->green == 40, "green packet starts at %d, expected 40",
          f->green);
    check(f->blue == 80, "blue packet starts at %d, expected 80", f->blue);
    for (int i = 0; i < NLED; i++) {
        int red = f->data[i * 3 + 1];
        int green = f->data[i * 3 + 0];
        int blue = f->data[i * 3 + 2];
        check(red == (i >= 10 && i < 30 ? 204 : 0),
              "diode %d red = %d", i, red);
        check(green == (i >= 40 && i < 60 ? 204 : 0),
              "diode %d green = %d", i, green);
        check(blue == (i >= 80 && i < 100 ? 204 : 0),
              "diode %d blue = %d", i, blue);
    }
    printf("ok  packet: red 10..29, green 40..59, blue 80..99 at 204\n");
    ctx_free(c);
}

static void test_flip(const char *hex)
{
    ctx_t *c = ctx_new(hex);
    set_all_adc(c, 0);
    set_adc(c, 0, 2500);   /* фон: пакет виден, но это не удар */
    run_frames(c, 20);
    int p0 = last_frame(c)->red;

    set_adc(c, 0, 5000);   /* удар 1 */
    run_frames(c, 3);
    set_adc(c, 0, 2500);
    run_frames(c, 45);     /* 225 мс > REFRAC 200 мс */
    int p1 = last_frame(c)->red;

    set_adc(c, 0, 5000);   /* удар 2: разворот */
    run_frames(c, 3);
    set_adc(c, 0, 2500);
    run_frames(c, 40);
    int p2 = last_frame(c)->red;

    check(p1 > p0, "before flip red moved %d -> %d (expected right)", p0, p1);
    check(p2 < p1, "after flip red moved %d -> %d (expected left)", p1, p2);
    printf("ok  flip: red %d -> %d -> %d\n", p0, p1, p2);
    ctx_free(c);
}

static void test_button(const char *hex)
{
    ctx_t *c = ctx_new(hex);
    set_all_adc(c, 0);
    set_adc(c, 0, 5000);        /* ровный уровень: пакет виден, удара нет */
    avr_raise_irq(c->pd2, 1);   /* кнопка отпущена */
    run_frames(c, 20);
    int p0 = last_frame(c)->red;

    avr_raise_irq(c->pd2, 0);   /* нажатие > 25 мс */
    run_frames(c, 10);
    avr_raise_irq(c->pd2, 1);
    run_frames(c, 30);
    int p1 = last_frame(c)->red;

    check(p1 < p0, "mode 1 red moved %d -> %d (expected left)", p0, p1);
    printf("ok  button: mode 1, red %d -> %d\n", p0, p1);
    ctx_free(c);
}

/* WDT 2 с не должен срабатывать, пока wdr выполняется в каждом кадре. */
static void test_watchdog(const char *hex)
{
    ctx_t *c = ctx_new(hex);
    set_adc(c, 0, 5000);        /* ровный уровень: ударов нет */
    run_frames(c, 450);         /* 2.25 с > WDT 2 с */
    int red = last_frame(c)->red;
    /* после сброса сторожем пакет снова стоит у 10, а не ушёл вправо */
    check(red > 50, "red at %d after 2.25 s: WDT reset?", red);
    /* MCUCSR: I/O 0x34, значит в data space 0x54, WDRF = 3 */
    check((c->avr->data[0x54] & 0x08) == 0,
          "WDRF set: WDT reset happened (MCUCSR %02x)", c->avr->data[0x54]);
    printf("ok  watchdog: 2.25 s without reset, red %d\n", red);
    ctx_free(c);
}

int main(int argc, char **argv)
{
    const char *hex = argc > 1 ? argv[1] : "build/ledstrip.hex";
    printf("simavr: %s\n", hex);
    test_timing(hex);
    test_packet(hex);
    test_flip(hex);
    test_button(hex);
    test_watchdog(hex);
    printf("all checks passed\n");
    return 0;
}
