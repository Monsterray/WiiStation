# libhbcagent.a for WiiStation: the Homebrew Channel's in-app agent (sdk/hbc_agent.h), from
# hbc-reborn commit 52eeebd (HBC 1.9.1), built against libogc2 with devkitPPC r41-2 like the
# rest of WiiStation. The files are hbc-reborn's, unchanged: since HBC 1.8.3 the agent knows
# libogc2 itself (sdk/hbc_agent/ogc_flavor.h), and its crash hook is an assembly entry
# (ogc_exc.S) as libogc2's exception table needs -- tested on the bench Wii in HBC 1.8.5.
# The one file added here is channel/channelapp/title_id.h, which HBC's channel Makefile
# generates (TITLE ?= OHBC) and config.h includes.
# To update: copy the same files from a newer hbc-reborn commit (git show <rev>:<path>, not
# its working tree) and build.
#
#   make -C deps/hbc_agent DEVKITPRO=/c/devkitPro DEVKITPPC=/c/devkitPro/devkitPPC-r41-2

ifeq ($(strip $(DEVKITPPC)),)
$(error "Set DEVKITPPC in your environment.")
endif

PREFIX = $(DEVKITPPC)/bin/powerpc-eabi-
CC = $(PREFIX)gcc
AR = $(PREFIX)ar

AGENT = sdk/hbc_agent
HBC_SRC = channel/channelapp/source
BUILD = build

CFLAGS = -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -Wall \
	-DDEVSTREAM_SLOTS=2 -DDEVSTREAM_WORKER_STACK=16384 \
	-I$(HBC_SRC) -Isdk -I$(AGENT) \
	-I$(DEVKITPRO)/libogc2/include -I$(DEVKITPRO)/portlibs/ppc/include

SRC = $(AGENT)/agent.c $(AGENT)/ogc_exc.S $(AGENT)/overlay.c $(AGENT)/ov_ui.c $(AGENT)/ov_draw.c \
	sdk/wiispk/wiispk.c \
	$(HBC_SRC)/devfile.c $(HBC_SRC)/devstream.c $(HBC_SRC)/tcp.c $(HBC_SRC)/zmem.c
OBJ = $(addprefix $(BUILD)/,$(notdir $(patsubst %.S,%.o,$(SRC:.c=.o))))
HDR = sdk/hbc_agent.h sdk/hbc_netlog.h $(wildcard $(HBC_SRC)/*.h) $(wildcard $(AGENT)/*.h) \
	sdk/wiispk/wiispk.h channel/channelapp/config.h channel/channelapp/title_id.h

lib/libhbcagent.a: $(OBJ) | lib
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/%.o: $(AGENT)/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(AGENT)/%.S $(AGENT)/ogc_flavor.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: sdk/wiispk/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(HBC_SRC)/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) lib:
	mkdir -p $@

clean:
	rm -rf $(BUILD) lib
