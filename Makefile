#This is just here for convenience
#Call "make help" for available commands
ifndef ECHO
ECHO = echo
endif

VERSION = beta2.5

# LIBOGC=3 (scripts/build.sh) builds the dependency archives from a copy of deps/ in
# build_ogc3/deps, so libogc v3 / devkitPPC r50 objects never land beside the default ones.
# The copy carries sources only (no *.a, *.o, *.d): every archive is rebuilt there, and the
# sources are re-synced from deps/ on each run. The sources reach the repo root with ../../
# (deps/opengx/gc_gl.c includes ../../mem2_manager.h, ../../Gamecube/*.h), so every *.h of
# the repo is mirrored into build_ogc3/ at its own path. LIBOGC unset means 3 (the default since
# 5.13.0); LIBOGC=2 builds deps/ in place for libogc2.
LIBOGC ?= 3
ifeq ($(LIBOGC),3)
DEPS_ROOT := build_ogc3/deps
# devkitPPC r50 has no lightning.h (r41-2 shipped the old one): lightrec takes the GNU Lightning
# headers that the v3 liblightning.a is built from
LIGHTNING_CPATH := $(CURDIR)/deps/lightning/include
else
DEPS_ROOT := deps
endif

.PHONY: ogc3_deps
ogc3_deps:
ifeq ($(LIBOGC),3)
	@mkdir -p $(DEPS_ROOT)
	@tar -C deps --exclude='*.a' --exclude='*.o' --exclude='*.d' -cf - . | tar -C $(DEPS_ROOT) -xf -
	@find . \( -name deps -o -name 'build*' -o -name .git \) -prune -o -name '*.h' -print | tar -cf - -T - | tar -C build_ogc3 -xf -
endif

.PHONY:  all Wii_Release Wii

#all:
#	@$(ECHO) "Rebuilding Wii and logging to build.log..."
#	@$(MAKE) -C Gamecube clean -f Makefile_Wii
#	@$(MAKE) -C Gamecube -f Makefile_Wii 2> temp.log
#  #This step removes all leading pathes from the build.log
#	@sed 's|.*wiisxr/Gamecube|/wiisxr/Gamecube|;s|/./|/|;s|\r\n|\n|' temp.log > build.log
#  #note that msys doesn't seem to like sed -i
#	@rm temp.log
all: Wii Wii_Release

opengx.a: ogc3_deps
	@echo " "
	@echo "Building opengx.a library for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/opengx -f Makefile
	$(MAKE) -C $(DEPS_ROOT)/opengx -f Makefile PROF=1

lightrecNoLog.a: ogc3_deps
	@echo " "
	@echo "Building lightrecNoLog.a library for PPC"
	@echo " "
	$(if $(filter 3,$(LIBOGC)),CPATH=$(LIGHTNING_CPATH),) $(MAKE) -C $(DEPS_ROOT)/lightrec -f Makefile.NoLog

lightrecWithLog.a: ogc3_deps
	@echo " "
	@echo "Building lightrecWithLog.a library for PPC"
	@echo " "
	$(if $(filter 3,$(LIBOGC)),CPATH=$(LIGHTNING_CPATH),) $(MAKE) -C $(DEPS_ROOT)/lightrec -f Makefile.WithLog

zstd.a: ogc3_deps
	@echo " "
	@echo "Building zstd.a library for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/zstd-1.5.6 -f Makefile.wii

lzma.a: ogc3_deps
	@echo " "
	@echo "Building lzma.a library for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/lzma-19.00 -f Makefile.wii

zlibstatic.a: ogc3_deps
	@echo " "
	@echo "Building zlibstatic.a library for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/zlib-1.3.1 -f Makefile.wii

lightning.a: ogc3_deps
	@echo " "
	@echo "Building liblightning.a (GNU Lightning, deps/lightning) for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/lightning DEVKITPPC=$(DEVKITPPC)

hbcagent.a: ogc3_deps
	@echo " "
	@echo "Building libhbcagent.a (the Homebrew Channel's in-app agent) for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/hbc_agent DEVKITPRO=$(DEVKITPRO) DEVKITPPC=$(DEVKITPPC)

smb2.a: ogc3_deps
	@echo " "
	@echo "Building libsmb2.a (SMB2/3 client for the smb: device) for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/libsmb2 -f Makefile.wiistation DEVKITPRO=$(DEVKITPRO) DEVKITPPC=$(DEVKITPPC)

chdrstatic.a: ogc3_deps
	@echo " "
	@echo "Building chdrstatic.a library for PPC"
	@echo " "
	$(MAKE) -C $(DEPS_ROOT)/libchdr -f Makefile.wii

Wii: opengx.a lightrecWithLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
	@$(ECHO) "Building Wii..."
	@$(MAKE) -C Gamecube -f Makefile_Wii

Wii_Release: opengx.a lightrecNoLog.a zstd.a lzma.a zlibstatic.a chdrstatic.a
	@$(ECHO) "Building Wii_Release..."
	@$(MAKE) -C Gamecube -f Makefile_Wii_Release


#GC:
#	@$(ECHO) "Building GC..."
#	@$(MAKE) -C Gamecube -f Makefile_GC

dist: Wii
	@$(MAKE) -C Gamecube/release/ VERSION=$(VERSION)

clean:
	@$(ECHO) "Cleaning..."
ifeq ($(LIBOGC),3)
	@rm -rf build_ogc3
else
	@$(MAKE) -C $(DEPS_ROOT)/opengx clean -f Makefile
	@$(MAKE) -C $(DEPS_ROOT)/lightrec clean -f Makefile.NoLog
	@$(MAKE) -C $(DEPS_ROOT)/lightrec clean -f Makefile.WithLog
	@$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/zstd-1.5.6 clean -f Makefile.wii
	@$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/lzma-19.00 clean -f Makefile.wii
	@$(MAKE) -C $(DEPS_ROOT)/libchdr/deps/zlib-1.3.1 clean -f Makefile.wii
	@$(MAKE) -C $(DEPS_ROOT)/libchdr clean -f Makefile.wii
endif
	@$(MAKE) -C Gamecube clean -f Makefile_Wii
	@$(MAKE) -C Gamecube clean -f Makefile_Wii_Release

help:
	@$(ECHO)
	@$(ECHO) "Available commands:"
	@$(ECHO)
	@$(ECHO) "make            # cleans, rebuilds and log errors (Wii)"
	@$(ECHO) "make Wii        # build everything (Wii)"
	@$(ECHO) "make GC         # build everything (Gamecube)"
	@$(ECHO) "make clean      # clean everything"
