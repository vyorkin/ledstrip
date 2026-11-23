CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
HOSTCC ?= cc
STFLASH ?= st-flash

BUILD ?= build
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DSTM32F411xE -DHSE_VALUE=25000000u
INCLUDES := -Iinclude -I../include \
            -Ivendor/CMSIS/Core/Include \
            -Ivendor/CMSIS/Device/ST/STM32F4xx/Include \
            -Ivendor/STM32F4xx_LL
CFLAGS := $(MCU) $(DEFS) $(INCLUDES) -O2 -g -Wall -Wextra -Werror -std=c99 \
          -MMD -MP -ffunction-sections -fdata-sections
LDFLAGS := $(MCU) -T ld/stm32f411ce.ld -nostartfiles --specs=nano.specs \
           -Wl,--gc-sections -Wl,--print-memory-usage
LDLIBS := -lm

SRC := src/main.c src/system_stm32f4xx.c src/ws2812_output.c \
       src/adc_input.c ../src/ws2812.c ../src/frame.c ../src/button.c \
       ../src/dsp.c
ASM := src/startup_stm32f411xe.s
OBJ := $(BUILD)/main.o $(BUILD)/system_stm32f4xx.o \
       $(BUILD)/ws2812_output.o $(BUILD)/adc_input.o \
       $(BUILD)/ws2812.o $(BUILD)/frame.o $(BUILD)/button.o \
       $(BUILD)/dsp.o $(BUILD)/startup.o
SELFTEST_OBJ := $(BUILD)/main_selftest.o $(BUILD)/system_stm32f4xx.o \
                $(BUILD)/startup.o
ELF := $(BUILD)/ledstrip.elf
HEX := $(BUILD)/ledstrip.hex
BIN := $(BUILD)/ledstrip.bin
SELFTEST_ELF := $(BUILD)/clockselftest.elf
SELFTEST_HEX := $(BUILD)/clockselftest.hex
SELFTEST_BIN := $(BUILD)/clockselftest.bin
CHECK := $(BUILD)/check_host
DIST ?= ../../dist/bench

.PHONY: all clean check flash selftest dist

all: $(ELF) $(HEX) $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: ../src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/main_selftest.o: src/main.c | $(BUILD)
	$(CC) $(CFLAGS) -DCLOCK_SELFTEST -c $< -o $@

$(BUILD)/startup.o: $(ASM) | $(BUILD)
	$(CC) $(MCU) -c $< -o $@

-include $(OBJ:.o=.d) $(SELFTEST_OBJ:.o=.d)

$(ELF): $(OBJ) ld/stm32f411ce.ld | $(BUILD)
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/ledstrip.map,--cref $(OBJ) -o $@ $(LDLIBS)
	$(SIZE) $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

selftest: $(SELFTEST_HEX) $(SELFTEST_BIN)

$(SELFTEST_ELF): $(SELFTEST_OBJ) ld/stm32f411ce.ld | $(BUILD)
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/clockselftest.map,--cref \
	      $(SELFTEST_OBJ) -o $@
	$(SIZE) $@

$(SELFTEST_HEX): $(SELFTEST_ELF)
	$(OBJCOPY) -O ihex $< $@

$(SELFTEST_BIN): $(SELFTEST_ELF)
	$(OBJCOPY) -O binary $< $@

check: $(CHECK)
	./$(CHECK)

$(CHECK): check/check_host.c | $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -o $@ $<

flash: $(BIN)
	$(STFLASH) --reset write $< 0x08000000

dist: all selftest
	mkdir -p $(DIST)
	cp $(HEX) $(SELFTEST_HEX) $(DIST)/
	cp $(BUILD)/ledstrip.map $(BUILD)/clockselftest.map $(DIST)/
	cp ../../docs/bench-remote.md $(DIST)/README.ru.md
	cp ../../docs/bench-results.md $(DIST)/results.md
	cd $(DIST) && sha256sum *.hex *.map > SHA256SUMS

clean:
	rm -rf $(BUILD)
