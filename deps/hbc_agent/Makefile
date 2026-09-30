# libhbcagent.a for WiiStation: the Homebrew Channel's in-app agent (sdk/hbc_agent.h), from
# hbc-reborn commit 0205bf1 (HBC 1.7.6), built against libogc2 with devkitPPC r41-2 like the
# rest of WiiStation. Two changes from hbc-reborn, both behind HBC_AGENT_LIBOGC2:
#   sdk/hbc_agent/agent.c    the crash block is written from libogc2's per-exception handler
#                            (__exception_sethandler), which then calls c_default_exceptionhandler
#   sdk/hbc_agent/overlay.c  WiiMote > Connect: libogc2's WPAD has no pairing call
# To update: copy the same files from a newer hbc-reborn commit and keep those two changes.
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

CFLAGS = -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -Wall -DHBC_AGENT_LIBOGC2 \
	-DDEVSTREAM_SLOTS=2 -DDEVSTREAM_WORKER_STACK=16384 \
	-I$(HBC_SRC) -Isdk -I$(AGENT) \
	-I$(DEVKITPRO)/libogc2/include -I$(DEVKITPRO)/portlibs/ppc/include

SRC = $(AGENT)/agent.c $(AGENT)/overlay.c $(AGENT)/ov_ui.c $(AGENT)/ov_draw.c \
	$(HBC_SRC)/devfile.c $(HBC_SRC)/devstream.c $(HBC_SRC)/tcp.c $(HBC_SRC)/zmem.c
OBJ = $(addprefix $(BUILD)/,$(notdir $(SRC:.c=.o)))
HDR = sdk/hbc_agent.h sdk/hbc_netlog.h $(wildcard $(HBC_SRC)/*.h) $(wildcard $(AGENT)/*.h) channel/channelapp/config.h

lib/libhbcagent.a: $(OBJ) | lib
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/%.o: $(AGENT)/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(HBC_SRC)/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) lib:
	mkdir -p $@

clean:
	rm -rf $(BUILD) lib
