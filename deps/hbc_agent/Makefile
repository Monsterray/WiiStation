# libhbcagent.a for WiiStation: the Homebrew Channel's in-app agent (sdk/hbc_agent.h), from
# hbc-reborn commit b774f54 (HBC 1.10.4), built against the same libogc as the rest of WiiStation
# (libogc v3 by default since 5.13.0, libogc2 with LIBOGC=2). The files are hbc-reborn's, unchanged: since HBC 1.8.3 the agent knows
# libogc2 itself (sdk/hbc_agent/ogc_flavor.h), and its crash hook is an assembly entry
# (ogc_exc.S) as libogc2's exception table needs -- tested on the bench Wii in HBC 1.8.5.
# The one file added here is channel/channelapp/title_id.h, which HBC's channel Makefile
# generates (TITLE ?= OHBC) and config.h includes.
# To update: copy the same files from a newer hbc-reborn commit (git show <rev>:<path>, not
# its working tree) and build.
#
#   scripts/build.sh builds it (top Makefile); by hand, libogc2:
#   make -C deps/hbc_agent LIBOGC=2 DEVKITPRO=/c/devkitPro DEVKITPPC=/c/devkitPro/devkitPPC-r41-2

ifeq ($(strip $(DEVKITPPC)),)
$(error "Set DEVKITPPC in your environment.")
endif

PREFIX = $(DEVKITPPC)/bin/powerpc-eabi-
CC = $(PREFIX)gcc
AR = $(PREFIX)ar

AGENT = sdk/hbc_agent
HBC_SRC = channel/channelapp/source
BUILD = build

# libogc: LIBOGC=3 (exported by scripts/build.sh; the default) builds against devkitPro's libogc v3
# headers and AGENT_TUXEDO=1. LIBOGC=2 is libogc2.
LIBOGC ?= 3
ifeq ($(LIBOGC),3)
OGC_INC = $(DEVKITPRO)/libogc/include
ifeq ($(wildcard $(OGC_INC)/tuxedo/ppc/exception.h),)
$(error libogc v3 headers not found under $(OGC_INC) (LIBOGC=3))
endif
else
OGC_INC = $(DEVKITPRO)/libogc2/include
endif

CFLAGS = -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -O2 -Wall \
	-DDEVSTREAM_SLOTS=2 -DDEVSTREAM_WORKER_STACK=16384 \
	-I$(HBC_SRC) -Isdk -I$(AGENT) \
	-I$(OGC_INC)$(if $(filter 3,$(LIBOGC)), -DAGENT_TUXEDO=1) -I$(DEVKITPRO)/portlibs/ppc/include

SRC = $(AGENT)/agent.c $(AGENT)/safety.c $(AGENT)/alloc_wrap.c $(AGENT)/info.c $(AGENT)/ogc_exc.S $(AGENT)/overlay.c $(AGENT)/ov_ui.c $(AGENT)/ov_draw.c \
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
