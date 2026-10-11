# deps/wiidrc/Makefile -- build libwiidrc.a (Wii U GamePad input, FIX94, MIT)
# from the source in this directory, for devkitPro libogc v3.
#
# Usage (Git Bash or devkitPro MSYS2, from anywhere):
#   make -C deps/wiidrc            # builds deps/wiidrc/lib/libwiidrc.a
#   make -C deps/wiidrc clean      # removes build/ and lib/
#
# Paths are Unix-style (/c/devkitPro/...) on purpose: the compiler runs
# through sh/make, and backslash paths break -I flags there (see AGENTS.md).

DEVKITPRO ?= /c/devkitPro
PREFIX    := $(DEVKITPRO)/devkitPPC/bin/powerpc-eabi-
CC        := $(PREFIX)gcc
AR        := $(PREFIX)ar

SRC_DIR   := source
INC_DIR   := include
BUILD     := build
LIB       := lib/libwiidrc.a

CFLAGS    := -mrvl -mcpu=750 -meabi -mhard-float -O2 -Wall \
             -I$(DEVKITPRO)/libogc/include -I$(INC_DIR) -I$(INC_DIR)/wiidrc

SRC       := $(SRC_DIR)/wiidrc.c
OBJ       := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRC))
HDR       := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(INC_DIR)/wiidrc/*.h)

all: $(LIB)

$(LIB): $(OBJ) | lib
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/%.o: $(SRC_DIR)/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) lib:
	mkdir -p $@

clean:
	rm -rf $(BUILD) lib

.PHONY: all clean
