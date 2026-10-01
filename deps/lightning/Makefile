# liblightning.a for WiiStation: GNU Lightning (lib/, include/) from the official repository,
# https://git.savannah.gnu.org/git/lightning.git at the commit in UPSTREAM_COMMIT, with only the
# PowerPC backend kept. It replaces the prebuilt liblightning.a in devkitPPC-r41-2 (same API:
# the installed lightning.h matches this include/lightning.h.in), so Lightning can be measured
# and tuned like Lightrec. include/lightning.h is lightning.h.in with the stdint line filled in.
#
#   make -C deps/lightning DEVKITPPC=/c/devkitPro/devkitPPC-r41-2
#
# HAVE_MMAP=0: the Wii has no mmap; Lightrec hands Lightning its own code buffer.

ifeq ($(strip $(DEVKITPPC)),)
$(error "Set DEVKITPPC in your environment.")
endif

CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc
AR = $(DEVKITPPC)/bin/powerpc-eabi-ar

CFLAGS = -O2 -g -w -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float \
	-Iinclude -DHAVE_MMAP=0 -DDISASSEMBLER=0 -DNDEBUG $(EXTRA_CFLAGS)

SRC = lib/lightning.c lib/jit_disasm.c lib/jit_memory.c lib/jit_note.c lib/jit_print.c lib/jit_size.c
OBJ = $(addprefix build/,$(notdir $(SRC:.c=.o)))
# lightning.c includes the PowerPC backend (jit_ppc*.c) and the shared helpers
DEPS = $(wildcard lib/*.c) $(wildcard include/*.h include/lightning/*.h) Makefile

lib/liblightning.a: $(OBJ) | lib
	rm -f $@
	$(AR) rcs $@ $^

build/%.o: lib/%.c $(DEPS) | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

clean:
	rm -rf build lib/liblightning.a
