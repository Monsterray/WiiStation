# libwiidrc for libogc v3

libwiidrc (Wii U GamePad input) built from source for devkitPro libogc v3, which ships no
wiidrc library. WiiStation links it with `-lwiidrc` (Gamecube/Makefile_Wii).

## Origin

- Upstream: FIX94/libwiidrc, commit 4e156201 (2017, unchanged since). Copyright (C) 2017 FIX94.
- `source/wiidrc.c` and `source/wiidrc_structs.h`: taken from the usbloadergx tree,
  `source/libs/libdrc/` (commit 7a96396a "Build libwiidrc from source"). That commit already
  built this same file against libogc 3, because the old prebuilt archive calls
  `DCInvalidateRange`, which libogc 3 only provides as an asm alias of `PPCDCacheInvalidate`.
  The source is otherwise unmodified.
- `include/wiidrc/wiidrc.h`: copied from `C:\devkitPro\libogc2\include\wiidrc\wiidrc.h`. The
  same header is in vbagx (`source/drivers/ogc/wii/wiidrc.h`) and usbloadergx; apart from line
  endings all three are identical.
- `LICENSE`: MIT, copied from usbloadergx `source/libs/libdrc/LICENSE`. It covers all files here.

## Build

    make -C deps/wiidrc            # -> deps/wiidrc/lib/libwiidrc.a
    make -C deps/wiidrc clean

Toolchain: `$(DEVKITPRO)/devkitPPC/bin/powerpc-eabi-gcc` (DEVKITPRO defaults to /c/devkitPro),
flags `-mrvl -mcpu=750 -meabi -mhard-float -O2`, libogc at `$(DEVKITPRO)/libogc/include`.

## API

Same API as the libogc2 header: 15 functions (`WiiDRC_Init`, `WiiDRC_Inited`,
`WiiDRC_Recalibrate`, `WiiDRC_ScanPads`, `WiiDRC_Connected`, `WiiDRC_ShutdownRequested`,
`WiiDRC_GetRawI2CAddr`, `WiiDRC_Data`, `WiiDRC_ButtonsUp/Down/Held`, `WiiDRC_lStickX/Y`,
`WiiDRC_rStickX/Y`) and the `WIIDRC_*` button and extra-flag macros.
