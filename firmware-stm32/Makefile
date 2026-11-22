CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -std=c99
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lm
BUILD ?= build

SRC = src/dsp.c src/frame.c
TEST_SRC = test/test_core.c
TEST_BIN = $(BUILD)/test_core

.PHONY: all test target flash check clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_BIN): $(SRC) $(TEST_SRC) $(wildcard include/ledstrip/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) $(TEST_SRC) $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

target:
	$(MAKE) -C target

flash:
	$(MAKE) -C target flash

check:
	$(MAKE) -C target check

clean:
	rm -rf $(BUILD)
	$(MAKE) -C target clean
