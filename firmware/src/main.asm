; ============================================================================
;  Цветомузыка: ATmega16 @ 16 MHz, лента WS2812 (120 диодов)
;
;  Три канала:
;    PA0 - огибающая низкой полосы   (100-200 Гц)   -> красный
;    PA1 - огибающая средней полосы  (3.5-6.5 кГц)  -> зелёный
;    PA2 - огибающая высокой полосы  (8.5 кГц+)     -> синий
;    PB0 - данные WS2812
;    PD2 - кнопка смены режима (на GND, внутренняя подтяжка)
;
;  Каждый канал - "пакет" из 20 диодов, бегущий по всей ленте.
;  Яркость пакета = уровень канала. Направление меняется на каждом
;  втором ударе по низкой полосе. Кнопка циклически меняет режим.
;
;  Кадр 5 мс (200 Гц), прерывания не используются.
; ============================================================================

.include "m16def.inc"

.equ MAXPOS    = 100 * 256      ; 100.0 диодов в формате 8.8
.equ REFRAC_FRAMES = 40        ; 40 кадров = 200 мс между ударами
.equ HIT_DELTA = 60             ; прирост АЦП, считающийся ударом
.equ HIT_MIN   = 120            ; ниже этого уровня удар не считаем
.equ HIT_N     = 2              ; разворот каждый второй удар
.equ FRAME_TICKS = 1250         ; 1250 * 4 мкс = 5 мс (Timer1, /64)

.def zero    = r15
.def porthi  = r2               ; PORTB со снятым PB0
.def portlo  = r3               ; PORTB с низким PB0

.dseg
.org 0x0060
buffer:   .byte 360             ; 120 * 3, порядок GRB

pos0l:    .byte 1
pos0h:    .byte 1
pos1l:    .byte 1
pos1h:    .byte 1
pos2l:    .byte 1
pos2h:    .byte 1

speed0:   .byte 1
speed1:   .byte 1
speed2:   .byte 1
dir0:     .byte 1
dir1:     .byte 1
dir2:     .byte 1

lvl0:     .byte 1
lvl1:     .byte 1
lvl2:     .byte 1

flip:     .byte 1
mode:     .byte 1
hitcnt:   .byte 1
refrac:   .byte 1
prevl:    .byte 1
prevh:    .byte 1
prevok:   .byte 1
btncnt:   .byte 1
btnstate: .byte 1

.cseg
.org 0
    rjmp reset

; ----------------------------------------------------------------------------
;  Инициализация
; ----------------------------------------------------------------------------
reset:
    ldi r16, low(RAMEND)
    out SPL, r16
    ldi r16, high(RAMEND)
    out SPH, r16

    clr zero
    ldi r16, 0x01
    mov porthi, r16
    clr portlo
    out DDRB, porthi
    out PORTB, portlo

    cbi DDRD, 2                 ; кнопка - вход
    sbi PORTD, 2                ; подтяжка

    ldi r16, 0x86               ; ADEN, делитель 64
    out ADCSRA, r16
    clr r16
    out ADMUX, r16
    sbi ADCSRA, ADSC            ; холостое преобразование
adc_init_wait:
    sbic ADCSRA, ADSC
    rjmp adc_init_wait

    clr r16
    out TCCR1A, r16
    ldi r16, 0x03               ; Timer1, clk/64
    out TCCR1B, r16

    clr r16
    sts flip, r16
    sts hitcnt, r16
    sts refrac, r16
    sts prevl, r16
    sts prevh, r16
    sts prevok, r16
    sts btncnt, r16
    sts btnstate, r16
    sts lvl0, r16
    sts lvl1, r16
    sts lvl2, r16

    sts pos0l, r16
    ldi r16, 0x0A               ; 10.0
    sts pos0h, r16
    clr r16
    sts pos1l, r16
    ldi r16, 0x28               ; 40.0
    sts pos1h, r16
    clr r16
    sts pos2l, r16
    ldi r16, 0x50               ; 80.0
    sts pos2h, r16

    clr r16
    sts mode, r16
    rcall load_mode

; ----------------------------------------------------------------------------
;  Главный цикл: 5 мс на кадр
; ----------------------------------------------------------------------------
main_loop:
    clr r16
    out TCNT1H, r16
    out TCNT1L, r16

    ldi r24, 0
    rcall read_ch
    mov r18, r24
    mov r19, r25
    rcall gamma_from_raw
    sts lvl0, r22
    mov r20, r18
    mov r21, r19
    rcall beat_detect

    ldi r24, 1
    rcall read_ch
    rcall gamma_from_raw
    sts lvl1, r22

    ldi r24, 2
    rcall read_ch
    rcall gamma_from_raw
    sts lvl2, r22

    rcall button

    lds r23, flip
    ldi XL, low(pos0l)
    ldi XH, high(pos0l)
    lds r18, speed0
    lds r19, dir0
    rcall update_one
    sts dir0, r19

    ldi XL, low(pos1l)
    ldi XH, high(pos1l)
    lds r18, speed1
    lds r19, dir1
    rcall update_one
    sts dir1, r19

    ldi XL, low(pos2l)
    ldi XH, high(pos2l)
    lds r18, speed2
    lds r19, dir2
    rcall update_one
    sts dir2, r19

    rcall clear_buffer

    lds r24, lvl0
    lds r25, pos0h
    ldi r16, 1                  ; красный в GRB
    rcall draw_packet

    lds r24, lvl1
    lds r25, pos1h
    ldi r16, 0                  ; зелёный
    rcall draw_packet

    lds r24, lvl2
    lds r25, pos2h
    ldi r16, 2                  ; синий
    rcall draw_packet

    rcall ws_send
    rcall wait_frame
    rjmp main_loop

; ----------------------------------------------------------------------------
;  АЦП: канал в r24, результат r25:r24 (0..1023)
; ----------------------------------------------------------------------------
read_ch:
    ori r24, 0x40               ; REFS0: опора AVcc
    out ADMUX, r24
    sbi ADCSRA, ADSC
rc_wait1:
    sbic ADCSRA, ADSC
    rjmp rc_wait1
    sbi ADCSRA, ADSC            ; первое после смены канала - в мусор
rc_wait2:
    sbic ADCSRA, ADSC
    rjmp rc_wait2
    in r24, ADCL
    in r25, ADCH
    ret

; ----------------------------------------------------------------------------
;  r25:r24 -> яркость r22 по таблице gamma_tab
; ----------------------------------------------------------------------------
gamma_from_raw:
    mov r20, r24
    lsr r20
    lsr r20
    mov r21, r25
    lsl r21
    lsl r21
    lsl r21
    lsl r21
    lsl r21
    lsl r21
    or r20, r21
    ldi ZL, low(gamma_tab * 2)  ; avra: метка CSEG - адрес слова, lpm - байта
    ldi ZH, high(gamma_tab * 2)
    add ZL, r20
    adc ZH, zero
    lpm r22, Z
    ret

; ----------------------------------------------------------------------------
;  Детектор удара по низкой полосе. Вход: r21:r20 (сырой АЦП).
; ----------------------------------------------------------------------------
beat_detect:
    lds r16, refrac
    tst r16
    breq bd_noref
    dec r16
    sts refrac, r16
bd_noref:
    mov r18, r20
    mov r19, r21
    lds r16, prevok             ; первый кадр только запоминаем
    tst r16
    brne bd_have_prev
    ldi r16, 1
    sts prevok, r16
    sts prevl, r18
    sts prevh, r19
    rjmp bd_done
bd_have_prev:
    lds r16, prevl
    lds r17, prevh
    sts prevl, r18
    sts prevh, r19
    sub r18, r16
    sbc r19, r17
    sbrc r19, 7                 ; уровень упал - не удар
    rjmp bd_done
    tst r19                     ; прирост больше 255 - тоже удар
    brne bd_delta_ok
    cpi r18, HIT_DELTA
    brlo bd_done
bd_delta_ok:
    mov r16, r20
    mov r17, r21
    cpi r16, low(HIT_MIN)
    ldi r18, high(HIT_MIN)
    cpc r17, r18
    brlo bd_done
    lds r16, refrac
    tst r16
    brne bd_done
    ldi r16, REFRAC_FRAMES
    sts refrac, r16
    lds r16, hitcnt
    inc r16
    cpi r16, HIT_N
    brlo bd_store
    clr r16
    lds r18, flip
    ldi r19, 1
    eor r18, r19
    sts flip, r18
bd_store:
    sts hitcnt, r16
bd_done:
    ret

; ----------------------------------------------------------------------------
;  Кнопка PD2: 5 подряд кадров нажатия = переключение режима
; ----------------------------------------------------------------------------
button:
    sbic PIND, 2
    rjmp bt_rel
    lds r18, btncnt
    cpi r18, 5
    brsh bt_done
    inc r18
    sts btncnt, r18
    cpi r18, 5
    brne bt_done
    lds r19, btnstate
    tst r19
    brne bt_done
    ldi r19, 1
    sts btnstate, r19
    rcall next_mode
    rjmp bt_done
bt_rel:
    clr r18
    sts btncnt, r18
    clr r18
    sts btnstate, r18
bt_done:
    ret

next_mode:
    lds r18, mode
    inc r18
    andi r18, 3
    sts mode, r18
    rcall load_mode
    ret

; ----------------------------------------------------------------------------
;  Загрузка скоростей и направлений текущего режима
; ----------------------------------------------------------------------------
load_mode:
    lds r18, mode
    mov r19, r18
    lsl r19                     ; *2
    mov r20, r18
    lsl r20
    lsl r20                     ; *4
    add r19, r20                ; *6
    ldi ZL, low(mode_tab * 2)   ; avra: метка CSEG - адрес слова, lpm - байта
    ldi ZH, high(mode_tab * 2)
    add ZL, r19
    adc ZH, zero
    lpm r22, Z+
    sts speed0, r22
    lpm r22, Z+
    sts speed1, r22
    lpm r22, Z+
    sts speed2, r22
    lpm r22, Z+
    sts dir0, r22
    lpm r22, Z+
    sts dir1, r22
    lpm r22, Z+
    sts dir2, r22
    ret

; ----------------------------------------------------------------------------
;  Один канал: X -> младший байт позиции, r18 = скорость, r19 = dir,
;  r23 = flip. На выходе r19 - новое направление (разворот на краю).
; ----------------------------------------------------------------------------
update_one:
    ld r16, X
    adiw r26, 1
    ld r17, X
    sbiw r26, 1

    mov r21, r19
    eor r21, r23
    sbrc r21, 0
    rjmp uo_sub
    add r16, r18
    adc r17, zero
    rjmp uo_check
uo_sub:
    sub r16, r18
    sbci r17, 0

uo_check:
    sbrc r17, 7                 ; ушли ниже нуля
    rjmp uo_neg
    cpi r16, low(MAXPOS)
    ldi r21, high(MAXPOS)
    cpc r17, r21
    brlo uo_store
    breq uo_store
    ldi r21, low(2 * MAXPOS)    ; отражаем от правого края
    ldi r22, high(2 * MAXPOS)
    sub r21, r16
    sbc r22, r17
    mov r16, r21
    mov r17, r22
    ldi r21, 1
    eor r19, r21
    rjmp uo_store

uo_neg:
    com r16
    com r17
    subi r16, -1
    sbci r17, -1
    ldi r21, 1
    eor r19, r21

uo_store:
    st X, r16
    adiw r26, 1
    st X, r17
    sbiw r26, 1
    ret

; ----------------------------------------------------------------------------
;  Очистка буфера кадра
; ----------------------------------------------------------------------------
clear_buffer:
    ldi YL, low(buffer)
    ldi YH, high(buffer)
    ldi r20, low(360)
    ldi r21, high(360)
    clr r22
cb_loop:
    st Y+, r22
    subi r20, 1
    sbci r21, 0
    mov r19, r20
    or r19, r21
    brne cb_loop
    ret

; ----------------------------------------------------------------------------
;  Рисуем пакет: r24 = яркость, r25 = стартовый диод, r16 = смещение цвета
; ----------------------------------------------------------------------------
draw_packet:
    tst r24
    breq dp_done
    mov r20, r25
    ldi r21, 0
    lsl r20
    rol r21
    add r20, r25
    adc r21, zero               ; старт * 3
    ldi YL, low(buffer)
    ldi YH, high(buffer)
    add YL, r20
    adc YH, r21
    add YL, r16
    adc YH, zero
    ldi r21, 20
dp_loop:
    ld r22, Y
    add r22, r24
    brcc dp_ok
    ldi r22, 255
dp_ok:
    st Y, r22
    adiw r28, 3
    dec r21
    brne dp_loop
dp_done:
    ret

; ----------------------------------------------------------------------------
;  Выдача 360 байт в WS2812. 16 МГц, 20 тактов на бит.
;  Бит 1: 12 тактов высокого, 8 низкого. Бит 0: 6 высокого, 14 низкого.
; ----------------------------------------------------------------------------
ws_send:
    ldi ZL, low(buffer)
    ldi ZH, high(buffer)
    ldi r18, low(360)
    ldi r19, high(360)
ws_byte:
    ld r16, Z+
    ldi r17, 8
ws_bit:
    out PORTB, porthi
    lsl r16
    brcc ws_zero
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    out PORTB, portlo
    nop
    nop
    nop
    nop
    dec r17
    brne ws_bit
    rjmp ws_next
ws_zero:
    nop
    nop
    out PORTB, portlo
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    dec r17
    brne ws_bit
ws_next:
    subi r18, 1
    sbci r19, 0
    mov r20, r18
    or r20, r19
    brne ws_byte
    ret

; ----------------------------------------------------------------------------
;  Ждём 5 мс от начала кадра (Timer1, /64: 1250 тиков)
; ----------------------------------------------------------------------------
wait_frame:
    in r16, TCNT1L
    in r17, TCNT1H
    subi r16, low(FRAME_TICKS)
    sbci r17, high(FRAME_TICKS)
    brcs wait_frame
    ret

.include "tables.inc"
