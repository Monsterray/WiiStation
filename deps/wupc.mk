# deps/wupc.mk -- build libwupc (Wii U Pro Controller library, FIX94, GPL)
# for devkitPro libogc v3 and install it where the WiiStation link line
# expects it.
#
# Prerequisite: scripts/fetch_deps.sh wupc   (clones the pinned source into
# deps/ext/libwupc and applies deps/patches/libwupc-libogc3.patch)
#
# Usage (from Git Bash / devkitPro MSYS2, repo root):
#   make -f deps/wupc.mk            # builds deps/ext/lib/libwupc.a
#   make -f deps/wupc.mk clean      # removes deps/ext/build/wupc and the outputs
#
# Outputs:
#   deps/ext/lib/libwupc.a
#   deps/ext/include/wupc/wupc.h
# Intermediates: deps/ext/build/wupc/ (all under deps/ext/, gitignored)
#
# Toolchain: devkitPPC at $(DEVKITPRO)/devkitPPC (powerpc-eabi-gcc), libogc at
# $(DEVKITPRO)/libogc. Paths are Unix-style (/c/devkitPro/...) on purpose: the
# compiler is driven through sh/make, and Windows-style paths with backslashes
# break -I flags there (see AGENTS.md).

# MSYS2 make does not pass TMP/TEMP/TMPDIR from the shell to its recipes; gcc
# then falls back to C:\WINDOWS and fails with "Cannot create temporary file".
# Export a Windows temp path from here instead (override with make TMPWIN=...).
TMPWIN  ?= C:\Users\MONTYP~1\AppData\Local\Temp
export TMP := $(TMPWIN)
export TEMP := $(TMPWIN)
export TMPDIR := $(TMPWIN)

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
DEVKITPRO ?= /c/devkitPro

SRC     := $(ROOT)/deps/ext/libwupc
EXT     := $(ROOT)/deps/ext
BUILD   := $(EXT)/build/wupc
LIB     := $(EXT)/lib/libwupc.a
HDR     := $(EXT)/include/wupc/wupc.h

CC      := $(DEVKITPRO)/devkitPPC/bin/powerpc-eabi-gcc
AR      := $(DEVKITPRO)/devkitPPC/bin/powerpc-eabi-gcc-ar
LIBOGC  := $(DEVKITPRO)/libogc

MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
CFLAGS  := -O2 -g -Wall $(MACHDEP) -I$(SRC)/include -I$(LIBOGC)/include

SOURCES := $(wildcard $(SRC)/source/*.c)
OBJS    := $(patsubst $(SRC)/source/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean

all: $(LIB) $(HDR)

$(BUILD)/%.o: $(SRC)/source/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)

$(HDR): $(SRC)/include/wupc/wupc.h
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf $(BUILD) $(LIB) $(EXT)/include/wupc

-include $(OBJS:.o=.d)
