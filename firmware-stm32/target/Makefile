CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
SIZE := $(CROSS)size
OBJCOPY := $(CROSS)objcopy
HOSTCC ?= cc
STFLASH ?= st-flash

BUILD ?= build
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DSTM32F411xE -DHSE_VALUE=25000000u
INCLUDES := -Ivendor/CMSIS/Core/Include \
            -Ivendor/CMSIS/Device/ST/STM32F4xx/Include \
            -Ivendor/STM32F4xx_LL
CFLAGS := $(MCU) $(DEFS) $(INCLUDES) -O2 -g -Wall -Wextra -Werror -std=c99 \
          -MMD -MP -ffunction-sections -fdata-sections
LDFLAGS := $(MCU) -T ld/stm32f411ce.ld -nostartfiles --specs=nano.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/ledstrip.map,--cref \
           -Wl,--print-memory-usage

SRC := src/main.c src/system_stm32f4xx.c
ASM := src/startup_stm32f411xe.s
OBJ := $(BUILD)/main.o $(BUILD)/system_stm32f4xx.o $(BUILD)/startup.o
ELF := $(BUILD)/ledstrip.elf
HEX := $(BUILD)/ledstrip.hex
BIN := $(BUILD)/ledstrip.bin
CHECK := $(BUILD)/check_host

.PHONY: all clean check flash

all: $(ELF) $(HEX) $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/startup.o: $(ASM) | $(BUILD)
	$(CC) $(MCU) -c $< -o $@

-include $(OBJ:.o=.d)

$(ELF): $(OBJ) ld/stm32f411ce.ld | $(BUILD)
	$(CC) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

check: $(CHECK)
	./$(CHECK)

$(CHECK): check/check_host.c | $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -Werror -o $@ $<

flash: $(BIN)
	$(STFLASH) --reset write $< 0x08000000

clean:
	rm -rf $(BUILD)
