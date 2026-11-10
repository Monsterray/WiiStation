![WiiStation logo](https://github.com/xjsxjs197/WiiSXRX_2022/raw/main/logo.png)

# WiiStation

**Version 6.0.0** (2026-11-09): a rebuilt menu and the achievement system. What is new, how to install or upgrade, and the known issues: [Docs/RELEASE_NOTES_6.0.0.md](Docs/RELEASE_NOTES_6.0.0.md). Download `WiiStation-6.0.0.zip` from the [releases](https://github.com/Monsterray/WiiStation/releases) and unzip it onto the root of your SD card.

WiiStation (formerly WiiSXRX_2022), is a Sony PlayStation 1 (PS1/PSX/PSone) emulator, forked from the original WiiSX-RX (https://github.com/niuus/WiiSXRX) emulator by NiuuS, originally a port of PCSX-Reloaded, but with many changes from PCSX-ReARMed, for the Nintendo Wii/Wii U.

## The following changes have been made to the code based on WiiSXRX.

* HID controllers support!

  Supported controllers referred to [Nintendont](https://github.com/FIX94/Nintendont).

  If you want to add controllers beyond what Nintendont supports, you need to add a new INI configuration file. The process should be as follows:
  
  Connect your HID controller to the Wii, and then start **HID_Test.dol** (please modify it to **boot.dol**). If the controller is supported, you will see normal connection information and information when each button is pressed.
  Send me the information when each button is pressed, and I will create an INI file. Or, refer to 0810_0003.ini to create your INI file.
  You can refer to the information in this link:
  https://gbatemp.net/threads/nintendont-custom-controller-configuration-file.633068/#post-10164777

* Incorporating the latest CDROM and CDRISO codes from PCSX-ReARMed, the compatibility of the system has been greatly improved.
  Many games that could not be run or had problems before can be run now.

* CDDA (Compact Disc Digital Audio) tracks & multi-tracks support.

* Incorporating the timer (system timings emulation) codes from PCSX-ReARMed.

* Combined the latest DFSound module from PCSX-ReARMed and used the SDL library.
  The sound quality of the system has been greatly improved.

* Adding the new, updated PSX dynamic recompiler [Lightrec](https://github.com/pcercuei/lightrec) by pcercuei, the speed/performance of the emulation is greatly improved. The 'new' PPC Dynarec is kept as an option in case compatibility or speed changes much.

  Since version 5.2.0 the Lightrec core is synced to upstream a7464cc. On a real Wii it lowers the emulated CPU's share of the load by 0.3 to 0.8 points in each of 8 test games, and its results are the same as the old core's on every PS1 test program (see [Docs/ACCURACY.md](Docs/ACCURACY.md)).

* Posibility of selecting between the old GPU video plugin based on **P.E.Op.S. Soft GPU** (slightly faster) and the new GPU video plugin based on **gpulib + DFXVideo** (more compatible).

* 240p support!

* Interlace mode support - renders the games to a resolution close to the real PSX hardware (480i mode), which gives full speed to 480i games!

* Posibility of enabling/disabling bilinear, trap, and deflicker filters!

* PS1 Lightguns support! Both **Namco GunCon** and **Konami Justifier** lightguns are supported and emulated with the Wiimotes! (Needs to be enabled in emulator settings and calibrate them by using the in-game calibration screen)

* Experimental PS1 Mouse support via the Wiimote IR.

* PS1 Multitap support! With options for enabling them on both Port 1 and Port 2, supports up to 8 players. (Needs to be enabled in emulator settings and controllers need to be set for use the Multitap adequately)

* Support for BIN+CUE, ISO, IMG, and eboot PBP (a compressed format for PS1 games on PSP) formats.

* CHD v1-v5 compressed format support with the [libCHDr](https://github.com/rtissera/libchdr) library from rtissera and [MAME](https://github.com/mamedev/mame).

* Support for multiple languages.
  At first, I wanted to refer to Snes9x GX and support TTF font library.
  However, it encountered a memory leak problem, resulting in automatic exit.
  So it can only be made into a specific font.
  Font char information: first two byte: BigEndianUnicode char code, followed by a character picture in IA8 format with a size of 24 * 24.

* For some customized Chinese culture games, specific BIOS is automatically loaded.
  For example:  sd:\wiistation\isos\武藏传.ISO => sd:\wiistation\bios\武藏传.bin

* Other minor corrections, such as disc changing (swap) and automatic fixes (autoFix functions) for some games.

  ※※※ Note: It reads a font file in a fixed location, so make sure that [sd:/wiistation/fonts/chs.dat] exists ※※※

## Changes on 'new' PPC Dynarec:

* Modification of some dynamic compilation instructions on the PPC Dynarec, such as SLLV, SRLV, SRAV, Final Fantasy 9 and Biohazard 3 (Resident Evil 3) can be run.
(Part of the division instruction uses a static compilation instruction)

* Emulation of more instructions, such BREAK and SYSCALL, makes more games to be playable with the PPC Dynarec (ex., EA Sports F1 2000).

* Many other emulation, speed and events improvements, some of them courtesy of PCSX-ReARMed.

## Goals

* **Two options: Fast and Accurate.** Fast is the emulation of today, with timing tuned for games. Accurate is to behave as a real PlayStation does, at a cost in speed. [Docs/ACCURACY.md](Docs/ACCURACY.md) lists what a real PS1 does that WiiStation does not yet do (measured with PS1 test programs against their output on a real console), and the plan for the Accurate option.

* Improve GTE (Geometry Transformation Engine) code to provide 3D game speed.
  By using the paired single instruction, most of the GTE logic has been rewritten, and FPS has indeed improved by about 2 frames.
  However, due to accuracy issues, there may be minor image/graphical issues. The more exact upstream GTE is planned for the Accurate option.

* Further improvement of HID controller support via USB.
  By reading and checking the code of [Nintendont](https://github.com/FIX94/Nintendont), we have understood the working principle of the HID controller,
  but it is still a little bit short to port Nintendont's HID control logic to WiiStation.

Any help is appreciated.

## Accuracy

[Docs/ACCURACY.md](Docs/ACCURACY.md) records the measured differences from a real PlayStation, test by test: CPU and bus timing, timers, DMA time, register behaviour, exceptions and the GTE. To check a change against a real PS1, run JaCzekanski's [ps1-tests](https://github.com/JaCzekanski/ps1-tests) with `scripts/chains/ps1tests.txt` and compare the console output with the real-console logs with `scripts/ps1tests_check.py` (the procedure is in the same file).

## Configuration

All settings of the `settings.ini` file, the per-game settings files, `autoboot.txt` and the other files in `wiistation/` are described in [SETTINGS.md](SETTINGS.md).

## Testing in Dolphin

Dolphin can run the WiiStation `.dol`. Two Dolphin graphics options change what the OpenGX renderer (`gpuPlugin = 2`) shows. Set them before you compare Dolphin with the Wii:

- **Graphics > Hacks > Texture Cache > Accuracy: Safe** (`GFX.ini`: `[Settings] SafeTextureCacheColorSamples = 0`). The renderer updates small parts of its texture pages in place. The default "Fast" hash does not see many of these updates, and Dolphin keeps the old texture. You see wrong or missing textures that pop in later. Real hardware does not have this problem.
- **Graphics > Hacks > Embedded Frame Buffer > Store EFB Copies to Texture Only: off** (`GFX.ini`: `[Hacks] EFBToTextureEnable = False`). Games on the VRAM read-back list (Dino Crisis 2, Vagrant Story, Spyro the Dragon) read the rendered frame back from the GPU. The renderer copies the frame buffer to memory for this. With the option on, Dolphin keeps the copy on the GPU, the game reads zeros, and effects that use the frame (for example the Spyro pause screen) show a flat colour or black. Real hardware does not have this problem.

Boot a bare `.dol` with `Dolphin.exe -b -e <path to .dol>`. To boot a game without the menu, put `autoboot.txt` in the SD sync folder (see [SETTINGS.md](SETTINGS.md)). A `CHAIN` autoboot runs several games or PS-X EXE test programs in one boot and switches off after the last one (SETTINGS.md, section 10.1).

The unattended test loop (scripted input, debug-build probes, frame and VRAM dumps, log extraction) and the diagnostic method built on it are written up for agents in [.claude/skills/wiistation-diagnostics/SKILL.md](.claude/skills/wiistation-diagnostics/SKILL.md); the scripts it uses are in `scripts/`.

## Backends

The CPU cores, GPU renderers, sound, CD-ROM, pad and font backends, how each is selected and what a new one must provide, are described in [Docs/BACKENDS.md](Docs/BACKENDS.md).

## Setting up

[SETUP.md](SETUP.md) takes you from nothing to WiiStation built (devkitPro), running in Dolphin, and
running on a Wii, in three levels. Developer notes (tests, conventions) are in [CONTRIBUTING.md](CONTRIBUTING.md).

The build uses devkitPro's current devkitPPC and libogc v3. The original WiiSXRX build (devkitPPC r41-2 with prebuilt libogc2, SDL, GNU Lightning and Lightrec libraries) was removed in 6.0.0.

## WiiStation Credits

WiiStation (formerly WiiSXRX_2022) - developed by xjsxjs197 - https://github.com/xjsxjs197/WiiSXRX_2022

240p/Lightgun/Mouse/Multitap support, some fixes and additional improvements by Jokippo - http://github.com/Jokippo

WiiStation icon - made by Dakangel (high quality logo made by saulfabreg)

WiiSX-RX fork - developed by NiuuS - https://github.com/NiuuS/WiiSXRX

WiiSX-R fork - developed by Mystro256 - https://github.com/Mystro256/WiiSXR

PCSX-Revolution - developed by Firnis - https://code.google.com/archive/p/pcsx-revolution/downloads ; https://github.com/Firnis/pcsx-revolution

WiiSX - developed by emu_kidid, tehpola, sepp256 - https://github.com/emukidid/PCSXGC ; https://code.google.com/archive/p/pcsxgc/downloads

PCSX-ReARMed - developed by notaz - https://github.com/notaz/pcsx_rearmed ; some other changes by Libretro - https://github.com/libretro/pcsx_rearmed

libOGC2 - developed by Extrems - https://github.com/extremscorner/libogc2

Lightrec - developed by pcercuei - https://github.com/pcercuei/lightrec

libCHDr - developed by MAME Team and rtissera - https://github.com/rtissera/libchdr

Thanks for everyone's attention and enthusiasm, which gives me the motivation to continue this project.
