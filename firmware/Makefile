AVRA    ?= avra
AVRDUDE ?= avrdude
MCU      = m16
SRC      = src/main.asm
OUT      = build/ledstrip

all: $(OUT).hex

$(OUT).hex: $(SRC) src/tables.inc
	mkdir -p build
	$(AVRA) -I src -I /usr/local/include/avr -o $(OUT).hex -l $(OUT).lst -m $(OUT).map $(SRC)

clean:
	rm -rf build
	rm -f src/main.obj src/main.eep.hex

flash: $(OUT).hex
	$(AVRDUDE) -c usbasp -p $(MCU) -U flash:w:$(OUT).hex:i

fuses:
	$(AVRDUDE) -c usbasp -p $(MCU) -U lfuse:w:0xFF:m -U hfuse:w:0xCF:m

.PHONY: all clean flash fuses
