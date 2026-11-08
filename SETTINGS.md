# WiiStation settings reference

This document describes all the settings of WiiStation. It follows the ASD-STE100 Simplified Technical English rules. Each setting has one table row. The row gives the key in the settings file, the permitted values, the default value, the menu location, and the effect.

The line numbers in this document refer to `Gamecube/GamecubeMain.cpp` unless the text gives a different file.

## 1. The settings file

### 1.1 Name and location

The settings file is `settings.ini`, an INI file: one `key = value` on each line, text values in double quotes, and a line that starts with `;` or `#` is a comment. WiiStation writes it in sections (`[General]`, `[Files]`, `[Video]`, `[Audio]`, `[Timing]`, `[Performance]`, `[Saves]`, `[Controllers]`, `[Co-Op]`, `[Achievements]`, `[Network]`), each with a one-line comment. The sections are only for people: WiiStation reads a key in any section, or outside one. `example_settings.ini` (in the release, and in `sd:/wiistation/` after an install) lists every setting in its section, with its default and a comment that tells what it does; WiiStation does not read it. `scripts/settings_check.py` keeps it the same as the code. WiiStation reads the settings from one of two locations:

| Launch device | File that WiiStation reads |
|---|---|
| SD card (the default) | `sd:/wiistation/settings.ini` |
| USB storage (the loader argument starts with `u`) | `usb:/wiistation/settings.ini` |

WiiStation reads only one of the two files. It does not merge them. If the file does not exist, WiiStation uses the default values in this document.

### 1.2 Format

The file is a text file. Each line has one setting:

```
Key = Value
# A line that starts with # is a comment.
smbusername = "name"
```

Obey these rules:

- Write the key exactly as this document shows it. The keys are case sensitive.
- Put a space, a tab, a colon (`:`), or an equals sign (`=`) between the key and the value.
- Write numbers without quotation marks.
- Write text values inside double quotation marks.
- WiiStation rejects a number that is outside the permitted range. It keeps the current value.
- WiiStation ignores a key that it does not know.

### 1.3 How to save the settings from the menu

1. Open the Settings menu.
2. Select the General tab.
3. Select **Save Settings > SD** or **Save Settings > USB**.

WiiStation writes all the keys in this document to the file, except `FilterTour`. If the folder `wiistation` does not exist on the device, WiiStation creates it.

### 1.4 Settings for one game

A game can have its own settings file. The file name is the disc serial number, for example `SLUS00067.ini`. The file holds only the settings that are different for that game. Each time a game starts, WiiStation sets the settings in this order:

1. The default values.
2. The common settings file: `usb:/wiistation/settings.ini`, or `sd:/wiistation/settings.ini` when there is no file on USB.
3. The game's own file, on top: the first file of `usb:/wiistation/settings/<serial>.ini`, `usb:/wiistation/settings/<serial>.cfg`, `sd:/wiistation/settings/<serial>.ini`, `sd:/wiistation/settings/<serial>.cfg`.

Thus a change to the common file reaches every game, except in the settings that the game's file holds.

To write the game's file, load the game, open the Settings menu, and select **Save Settings > This Game** on the General tab. WiiStation compares each setting with the default values and the common file (1 and 2 above) and writes only the settings that are different. The message tells how many. WiiStation writes the file to `usb:/wiistation/settings/` if that folder exists. If not, it writes the file to `sd:/wiistation/settings/`. When no setting is different, the file holds only its comment lines: the game then uses the common file. To remove all the settings of a game, delete its file.

**Load Settings > This Game** sets the settings in the same order (1, 2, 3) now. It does nothing when the game has no file.

These keys are never in a game's file: `lang`, `MenuFont`, `FileSortMode`, `TabWrap` (the menu), `RomDirSD`, `RomDirUSB`, `RomDirSMB`, `NativeDevice`, `StatesDevice`, `AchievementFile`, `AchievementLog`, `AchievementUTCOffset` (folders, devices and the clock), the Players seats `Player1` ... `Player2D`, `Player1P1` ... `Player2P8`, `PlayersRecent` and `PlayersOfficial` (in `players.ini`), and `smbipaddr`, `smbsharename`, `smbusername`, `smbpassword` (the network share and its login). They belong to the Wii, not to the game. **This Game** does not write them, and WiiStation ignores them in a game's file. A game file from 5.14.0 or earlier holds every key: WiiStation still reads it, and its other keys still replace the common file's values. The next **This Game** save writes only the differences. `FilterTour` is in no settings file (section 3).

Versions 5.6.1 to 5.13.1 wrote the game's file as `<serial>.cfg`, and no version read it. WiiStation reads `<serial>.cfg` on a device when that device has no `<serial>.ini`. The next **This Game** save writes `<serial>.ini`. You can then delete the `.cfg` file.

WiiStation reads the game's file after it opens the disc. `SoundHwAccel`, `CdBuffer`, `CdPrefetch` and `CdChdHunks` are used when the disc is opened, so their values in a game's file take effect at the next game load, not at this one. `VideoMode` takes effect only when WiiStation starts. All other keys take effect when the game starts.

A PS-X EXE program (a `.exe` file, for example a test program) has no disc serial number. WiiStation uses the default values and the common settings file for it, never the file of the game that ran before it.

### 1.5 Settings from the loader

On the Wii, a loader can give settings as arguments after the program name. Each argument has the form `Key=Value`. WiiStation applies these arguments after it reads the settings file. A game start sets the settings again from the default values and the files (section 1.4), so an argument holds until the first game starts.

## 2. General settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Core` | 0 = Lightrec, 1 = Interpreter, 2 = Dynarec | 0 | General, Advanced, CPU tab, "CPU Core" | Selects the CPU emulation core. A change resets the current game. |
| `BiosDevice` | 0 = HLE, 1 = SD, 2 = USB | 0 | General, Select BIOS | Selects the BIOS source. HLE is the built-in BIOS emulation. The file `SCPH1001.BIN` must exist in `wiistation/bios/` on the device. If the file does not exist, WiiStation uses HLE. The DVD option in the menu is not implemented. |
| `BootThruBios` | 0 = No, 1 = Yes | 0 | General, Boot Through BIOS | Shows the PlayStation start screen before the game starts. |
| `gpuPlugin` | 0 = Soft Fast, 1 = Soft Timed, 2 = OpenGX | 0 | General, Advanced, GPU tab, "GPU Plugin" | Selects the graphics renderer. Soft Fast and Soft Timed draw with the CPU. OpenGX draws with the Wii graphics hardware. A change resets the current game. |
| `lang` | 0 to 12, see the language table below | 0 | General, Select Language | Selects the menu language. The language files are in `wiistation/lang/`. The menu does not show a language if its font file is missing. |
| `SoundDriver` | 0 = CPU, 1 = DSP | `0` | Audio tab, "Sound Driver" | Chooses the sound output driver, and nothing else. `1` (DSP) is `dfsound/cube.c`: the stream plays through an AESND voice on the Wii's DSP at 44100 Hz. `0` (CPU) is `dfsound/sdl.c`: the CPU writes the stream at 48000 Hz. The PS1 sound emulation (the SPU: interpolation, reverb, XA) and the output device (the Wii's audio interface) are the same with either. With `SoundResampler = 0` (Hold) and DSP, the DSP's microcode holds the samples and the CPU resampler is skipped (`resample.c`); with the other two the CPU interpolates first and the DSP plays the result 1:1. If the chosen driver fails to open, the other one is used. The driver is chosen when a game starts, so a change takes effect the next time a game is loaded. The old key `SoundHwAccel` (up to 5.13.1) is still read. Note for testing in Dolphin: its high-level DSP emulation recognizes libogc's AESND microcode by hash, so a version it does not know falls back and sound may differ from real hardware. |
| `MenuFont` | `"Name"` | empty | none, settings file only | Selects the menu font. WiiStation loads `wiistation/fonts/Name.dat` from the SD card or the USB device. An empty value or a missing file gives the built-in font. The value does not apply to the Chinese, Japanese and Korean languages, which have their own glyph files. See section 9 for the font files. |
| `RomDirSD`, `RomDirUSB` | `"folder, folder, ..."` | `"/wiistation/isos"` | none, settings file only | The game folders on the SD card and on the USB device, with a comma between two folders: `RomDirSD = "/wiistation/isos, /roms/psx"`. Load from SD and Load from USB use them. A folder that does not exist is left out. With one folder left, the browser opens it; with several, it lists them by full path to choose from (up to 16); with none, it opens `/wiistation/isos` and says that it is missing. A folder can also name its device (`usb:/roms`). SD and USB cards are FAT, which has no links or junctions, so this is how a shared folder such as `/roms/psx` is used. |
| `fastLoad` | 0 = No, 1 = Yes | 0 | General, Advanced, CD tab, "Fast Load" | Shortens the delays the emulated CD-ROM drive reports, so loading screens pass faster than on a real console. A PlayStation drive takes a fixed time to answer a command, to seek, and to deliver each sector; WiiStation normally reproduces those delays (`cdReadTime` in `cdrom.c`, one sector per 1/75 s at single speed). With this on, the long second response to a read command is cut from about 2.1 million cycles to one sector time, the first-read delay is shortened, and seeks settle sooner. Nothing is read any faster from the SD card: this only stops the emulator waiting. Some games depend on the real timing and break, which is why it is off by default -- audio and video that are streamed from the disc are the usual casualties, since their sectors then arrive earlier than the game expects. Try it per game (`wiistation/settings/<CdromId>.ini`) rather than globally. |
| `CdBuffer` | 0 = 16 KB, 1 = 64 KB, 2 = 256 KB | 0 | General, Advanced, CD tab, "CD Read Buffer" | Size of the stdio read buffer each disc-image file handle gets (the main image, the sub-channel file, the CDDA handle, every file of a multi-file cue). **Keep 16 KB.** Beneath it libfat reads the card in 32 KB pages whatever the buffer is, so a larger buffer cannot make the card's commands longer: it only reads ahead data a seek then throws away. Measured 2026-09-22 on eleven games (perf.log `sd:`): 64 KB gave the same card commands to within 6%, 256 KB gave 20% more, and every card read was 32 KB in all three. The counts are the same on a Wii, where only their duration changes. Applies when the next game is loaded. |
| `GpuTiming` | 0 = Fast, 1 = Accurate | 0 | General, Advanced, GPU tab, "GPU Timing" | How long the core keeps the GPU busy after the game gives it work, for OpenGX and the Soft Fast renderer (Soft Timed, gpulib, is always accurate). **Fast** is how WiiStation always ran: a draw list ends after as many cycles as it has words, and the GPU reads idle from the end of one list to the start of the next, even while a block upload (a video frame, a texture) is going on. **Accurate** charges gpulib's cost per command (polygons, sprites and fills by size) and leaves the GPU busy for every transfer, so a game sees the GPU as a PS1 shows it. It moves every game's timing a little: games that wait for the GPU run their frames as on a PS1. Takes effect at once. With `SoftGpuMode` Accurate or Hi-Fi, both soft plugins charge the PS1 draw time of each command (`SoftGPU/acc.c`, fitted to ps1-tests `gpu/bandwidth`), image transfers by DMA take 2.2 (to the CPU) and 1.7 (to VRAM) cycles per word, and GPUSTAT bit 28 (ready for a DMA block) is clear while the GPU is busy; bit 28 and the DMA times also apply to OpenGX. |
| `CpuTiming` | 0 = Fast, 1 = Accurate | 0 | General, Advanced, CPU tab, "CPU Timing" | Whether the Lightrec core charges the cycles a PS1 CPU waits for its GTE (3D maths) and its multiply/divide unit. Both run beside the CPU, which waits only when it asks for a result too early: MFC2, CFC2, SWC2 or the next GTE command while a command runs (RTPS 15 cycles, RTPT 23, NCDT 44 ...), MFHI/MFLO while a MULT (9) or DIV (36) runs. **Fast** charges every instruction the same, as WiiStation always did, so heavy 3D code gets more done per frame than on a PS1. **Accurate** works the waits out per block when it is compiled; code that hides them behind other work pays nothing. Lightrec only (the Interpreter and Dynarec cores ignore it). Applies when the game resumes from the menu. |
| `CpuSpeed` | 0 = Auto, 1 = 50%, 2 = 75%, 3 = 100%, 4 = 125%, 5 = 150%, 6 = 200%, 7 = 300% | 0 | General, Advanced, CPU tab, "CPU Speed"; per game with Save Settings, This Game | The PS1 CPU's speed (an overclock above 100%). The cores charge 1.75 cycles per instruction by default; 150% charges 1.17, so the PS1 runs 1.5 times the instructions in each emulated second. A game that slows down because its CPU work does not fit in a frame can then hold its frame rate; it costs that much more of the Wii's CPU, and some games break (timing loops, music speed). **Auto** is 100% plus the built-in per-game fixes (database.c `cycle_multiplier_overrides`, e.g. CTR); a percentage replaces those fixes. Some games also start at a percentage of their own, a built-in game default the menu shows (GamecubeMain.cpp `gameDefaults`: Crash Team Racing 150%); This Game saves a change from it. Lightrec and the interpreter; applies when the game resumes from the menu (r3000a.c `psxCycleMult`). |
| `FrameRate` | 0 = Auto, 1 = TV, 2 = PS1, 3 = Unlimited | 0 | Video, "Frame Rate" | The refresh rate the PlayStation runs at, and how it is paced. **Auto**: 60.00 Hz (PAL 50.00), paced by a timer; the games in database.c `fractional_Framerate_hack_db` run 59.82 / 49.75. A TV shows 59.94 (PAL 50) pictures a second, so at 60.00 one emulated frame in about 1000 never reaches it: a small hitch every ~17 s in smooth motion. **TV**: the core runs the TV's rate (60000/1001 = 59.94 Hz, PAL 50) and each emulated vblank waits for the next Wii video retrace (SoftGPU/oldGpuFps.c FrameCap, VIDEO_WaitVSync), so every frame meets exactly one TV refresh: the smoothest motion, the game 0.1% slower than Auto. **PS1**: a PlayStation's own rate (its GPU dot clock over 263 lines of 3413 dots: 59.82 Hz; PAL 49.75), the exact game and music speed, but against the TV a picture repeats about every 8 s. Sound follows the rate through the rate control. Takes effect at the next vblank. Measured in Dolphin on Gex 2, 1200 vblanks: 20.02 / 20.04 / 20.08 s. **Unlimited**: the frame limit is off, so the game runs as fast as the Wii allows; the rate is Auto's. A controller button can change the limit during a game. That change is not saved. |
| `SioTiming` | 0 = Fast, 1 = Accurate | 0 | General, Advanced, Input tab, "Port Timing" | How long a byte on the controller ports takes. **Fast** is how WiiStation always ran: the controller's reply is there as soon as the game writes a byte, and its /ACK comes 535 cycles (15.8 us) later. **Accurate** times it as a PS1 does: the byte takes its 8 bits at the baud rate the game set (32 us at the BIOS's 250 kHz), and the controller's /ACK comes 450 cycles (13.3 us) after it (hardware: 6.8 to 13.7 us). A pad read then takes about three times as long, as on a PS1, which moves every game's timing a little. Memory card transfers keep the Fast timing in both. PadTest DX measures both (Docs/CONTROLLER_TESTING.md). |
| `LimiterWait` | 0 = Spin, 1 = Sleep | 1 | Not in the menu | How the frame limiter waits for the next frame. **Spin** busy-waits the whole time: exact, but no other thread runs while a game plays (the Homebrew Channel's agent, the CD read-ahead, the network). **Sleep** sleeps on a hardware timer alarm until 0.2 ms before the frame is due and spins only that last part (1 ms until 2026-10-01; the bench Wii woke at most 33 us late). Measured on the bench Wii 2026-09-30 (Spyro, FF7): the same speed and load, wake-ups at most 4 us late, and 54% of the time handed to other threads. Earlier attempts with usleep() asked for ten times the wait (the limiter's tick is 10 us, not 100 us), which is why they ran games at half speed. |
| `LimiterDebt` | 0 = Short, 1 = Long | 1 | Not in the menu | How much lateness the frame limiter pays back after a frame runs long. The limiter schedules each frame from when the previous one was due, so after a stall the next frames run unthrottled until the debt is paid. **Short** pays back at most 12.5 ms and drops the rest; the sound queue then refills only through the rate control. **Long** pays back up to 125 ms, about what the sound drivers keep queued. **Long by default since 2026-10-01** (`scripts/chains/limiter_debt_ab.txt`, Spyro through its world-entry stall, Dolphin): emulated speed 1.000 against 0.995, the sound queue on average 7033 samples against 4411, and the rate control never at its limit (240 saturated updates with Short). After a stall the next frames run fast for about 0.1 s. |
| `EfbSync` | 0 = Auto, 1 = Always, 2 = Never | 0 | Not in the menu | OpenGX only. When a game reads the picture back: Auto decides per game, Always copies the picture, Never does not copy it. Use 0. The other values are for tests. See Docs/EFB_SYNC.md. |
| `GxTexMin` | 0 = Off, 1 = Linear, 2 = Anisotropic 2x, 3 = Anisotropic 4x | 0 | General, Advanced, GPU tab, "Far Textures" | OpenGX only, a trial. Texels drawn smaller than a pixel (distant ground) are filtered linear, and with 2 or 3 also with up to 2 or 4 anisotropic taps, only on polygons with texels drawn smaller than a pixel (a sprite or a magnified polygon keeps the plain filter: the taps blurred cut-out texels, Ape Escape's hand cursor); magnified texels keep the `BilinearFilter` filter. The textures have no mipmaps. Read when the game starts. See Docs/GX_QUALITY.md. |
| `GxColor` | 0 = RGB8 (24-bit), 1 = RGBA6 dithered, 2 = anti-aliased (240p) | 0 | General, Advanced, GPU tab, "GX Color" | OpenGX only, a trial. The color format of the Wii frame buffer. 1 draws with 6 bits per color and lets the Wii dither where the game asks for dither (`Dithering`). 2 draws into an anti-aliased RGB565 frame buffer (5/6/5 bits, the Wii's dither where the game dithers) when `TVMode` is 240p and the picture is at most 264 lines; otherwise it is the same as 0. Checked on the Wii 2026-10-19 (Docs/GX_QUALITY.md, "AA on the Wii"). Read when the game starts. See Docs/GX_QUALITY.md. |
| `GxMask` | 0 = Accurate, 1 = Fast | 0 | General, Advanced, GPU tab, "GX Mask Bit" | OpenGX only. Bit 15 of the pixels OpenGX draws: the PS1's STP / mask bit, which a game reads back (GP0 C0), copies (GP0 80), blends by (a frame drawn back as a semi-transparent texture blends only its pixels with bit 15) and tests (check-mask, GP0 E6 bit 1). **Accurate** keeps it per pixel in the Wii's depth buffer: every GX pass writes one of two depths, textures are split by their texels' STP bit for every draw, the mask check is the depth test, and each EFB snapshot gets a Z8 copy that the VRAM sync reads (`deps/opengx/gc_gl.c` `OGX_ZC_SET`, `GlesGpu/efbSync.inc`). The color format and the picture do not change. **Fast** is 5.14.0: one bit per 16x16 block from the last primitive drawn there, textured draws taken as set, and P.E.Op.S.'s depth mask. Both apply E6 to GP0 A0 image loads and the check-mask to GP0 80 copies. Takes effect at the next frame (the texture cache is rebuilt). Docs/STP_BIT15.md. |
| `TabWrap` | 0 = No, 1 = Yes | 1 | Not in the menu | On every screen with tabs (Settings, Advanced), the tab buttons turn the tabs: L and R on a GameCube, HID, Classic or Wii U Pro controller, - and + on a Wiimote, ZL and ZR on the Wii U GamePad. With `1` the last tab goes round to the first and the first back to the last; with `0` they stop at the ends. |
| `ChainDump` | 0 = at the last vblank, 1 = before the next fill | 0 | Not in the menu | For chained test runs (section 10.1) only: when a chained game's VRAM is written to `wiistation/vram_NN.bin`. 0 = at the vblank the game ends. 1 = at the first GP0 02 fill after that vblank (the clear that starts the next frame), or 60 vblanks later if no fill comes: a test program that redraws every frame (ps1-tests `gpu/lines`) is then dumped as a whole frame. `scripts/chains/ps1tests_gpu.txt` sets it on every line. With OpenGX, all of VRAM is synced from the Wii's frame buffer before the dump. |
| `Disc` | 1 to 8 | 1 | Not in the menu | For chained test runs only: which disc of a multi-disc PBP a game boots. A player changes discs with Swap CD: picking the PBP that is in the drive moves to its next disc. |
| `JitDelay` | 0 to 127 | 16 | Not in the menu | Lightrec's background compiler (Docs/BACKENDS.md, "jitq"). New code runs in Lightrec's interpreter while a low-priority thread compiles it, and its compiled code starts exactly this many visits after the compile was queued, waiting for the thread there if it is late. The visit count, never the clock, decides, so a run and a recording replay the same every time in Dolphin and on the Wii. **0** compiles on the emulator after a block's first run, as before (the timing of older recordings). Bench Wii 2026-10-19, Spyro's elevator: 107 ms lost at 0, 7 ms at 16; compiling on the emulator's time 1.41 s against 0.53 s. |
| `JitQueueAt` | 1, 2 | 2 | Not in the menu | How many times a block runs in the interpreter before its compile is queued: **2** skips code that runs once. |
| `JitThreadTest` | 0 to 100 | 0 | Not in the menu | Test only: the compile thread sleeps this many ms before each compile, to show that a slow thread changes the waits and nothing the game sees. |
| `JitPending` | 1 to 127 | 12 | Not in the menu | How many compiles may wait for their install, in 64s (12 = 768). A block that finds the queue full is compiled on the emulator at once (or waits, with `JitFull`). The count depends only on the game, so a run and a recording replay the same, but a different value moves the timing of any run that fills the queue (Ape Escape's title demo and finish_lvl1 do, replay3 does a little), so recordings made with 12 need 12. Docs/BACKENDS.md, "jitq: first-run bursts". |
| `JitPick` | 0 = Queue order, 1 = Closest to install | 0 | Not in the menu | Which waiting compile the thread takes next. It changes when the emulator has to wait, never what the game sees. |
| `JitFull` | 0 = Compile now, 1 = Keep interpreting | 0 | Not in the menu | What a block does when the queue is full. 1 keeps it in the interpreter and queues it on a later visit, when installs have made room (moves the timing of runs that fill the queue, like `JitPending`). |
| `JitLightning` | 0 to 127 | 63 | Not in the menu | Test only: one bit per guest-identical speed-up in GNU Lightning's compile (deps/lightning, `LIGHTNING_OPT_*`): 1 = emit-time liveness on demand, 2 = one register mask per compile and no patch-bit loops, 4 = call register masks, 8 = jit_follow skips no-op handlers, 16 = jit_get_reg by masks, 32 = node pools not cleared. 63 = all on; 0 = the upstream algorithms. Add 64 to make every compile check itself (the emit is run three times and compared byte for byte, the masks against the loops they replace; perf.log `lightning2: bad= control_bad= opt_bad=` must stay 0). Nothing the guest sees changes with any value. Docs/BACKENDS.md, "jitq: first-run bursts". |
| `LockedCache` | 0 to 127 | 0 | Not in the menu | The locked cache of the Wii CPU, for measurements. Add the values of the parts: 1 = the sound interpolation table, 2 = the OpenGX texture tiles, 0 = off. A chained autoboot can set it per game. See Docs/LOCKED_CACHE.md. |
| `CdPrefetch` | 0 = Off, 1 = On | 1 | General, Advanced, CD tab, "CD Read-Ahead" | Runs a thread that keeps the next 31 sectors of a raw image (bin/cue, .iso) read in advance, so the emulated CPU does not stop while the card delivers them. Compressed images (CHD, PBP, .Z) are not affected. Measured 2026-09-22 on eleven games in Dolphin: 99.7% of sector reads were served from the ring, 77-90% of the time the game waited on the card moved to the thread, the card got 5-12% more commands, and every game ran exactly as with it off (same interrupt counts). **On by default since 2026-10-01**: the bench Wii (`.runs/hw_cd_ab`, `scripts/chains/cd_ab.txt`, `wsx.sh table NAME --detail cd`) showed the total wait on the card fall from 7.4% to 1.3% of wall in Spyro and from 7.9% to 1.4% in Medievil, with a shorter worst single wait (18.6 to 15.5 ms, 18.5 to 13.7 ms). CTR and Point Blank did not change. In Dolphin the worst wait grew (Medievil 9.5 to 18.9 ms); its card answers at once, so only the hardware figure counts. Applies when the next game is loaded. |
| `CdChdHunks` | 0 = 2, 1 = 4, 2 = 8 | 2 | General, Advanced, CD tab, "CHD Hunk Cache" | How many decoded CHD hunks stay in memory (about 20 KB each). The cache is a true LRU, so a larger one never decodes more often than a smaller one (at every moment it holds what the smaller one would, and more); 8 costs about 160 KB of MEM2, of which 13 MB or more is free in game. Games that alternate between places on the disc, such as streamed music beside level data, decode less often. The default was 2 until 2026-09-22. Applies when the next game is loaded. |

The values of `lang` are:

| Value | Language | Value | Language |
|---|---|---|---|
| 0 | English | 7 | Traditional Chinese |
| 1 | Simplified Chinese | 8 | Japanese |
| 2 | Korean | 9 | French |
| 3 | Spanish | 10 | Brazilian Portuguese |
| 4 | Portuguese | 11 | Catalan |
| 5 | Italian | 12 | Turkish |
| 6 | German | | |

## 3. Video settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `ScreenMode` | 0 = 4:3, 1 = 16:9, 2 = Force 16:9 | 0 | Video, Screen Mode | Sets the aspect ratio of the picture. |
| `VideoMode` | 0 = Auto, 1 = NTSC, 2 = PAL 50, 3 = PAL 60, 4 = Progressive | 0 | Video, Advanced page, "Video Mode" | Sets the television signal mode. **Auto** uses the Wii's own setting (480p there gives 480p here). **480p** (Progressive) needs the component cable: without one WiiStation starts in Auto instead, so a TV that cannot show 480p is never left with no picture. 480p has no interlace flicker, so the deflicker filter does nothing in it. Read when WiiStation starts: a change applies at the next start. |
| `FPS` | 0 = Off, 1 = On | 0 in the release build, 1 in the debug build | Video, Show FPS | Shows, once a second, what really happened: **FPS** the frames the game presented (30 for a game that draws every other vblank), **VPS** the emulated vblanks per second (60.00 NTSC, 50.00 PAL; 59.81/49.75 for the games with the fractional-rate fix), and the speed (VPS against the rate the core aims for; 100% is full speed). Until 5.9.5 it showed vblanks per second clamped to 59.83 whenever the real value was higher. |
| `SkipFrames` | 0 = Off, 1 = On | 0 | Video, Frame Skip | Lets the renderer skip frames when the emulation is slow. |
| `Dithering` | 0 = None, 1 = Default, 2 = Always | 1 | Video, Dithering | Controls the dither pattern of the PlayStation graphics. Default applies dither only when the game requests it. |
| `MdecChroma` | 0 = Sharp, 1 = Smooth | 0 | Video, Advanced page, "MDEC Chroma" | How the video decoder gives each pixel its color. The MDEC sends one color sample for every 2x2 pixels. **Sharp** repeats that sample across the four, which is what the console does and what makes PS1 video look blocky. **Smooth** mixes the neighboring samples, so the video looks better than it did on hardware. Measured on Medievil's intro: the color conversion takes about 2.5 times as long, which was 1.3% of the run against 3.3%. Applies to the next video that starts. |
| `FmvColour` | 0 = 15-bit, 1 = 24-bit | 1 | Video, Advanced page, "Video Color" | How a 24-bit picture (most full-motion video) reaches the screen. **24-bit** keeps all eight bits of each color, as the console shows it (an RGBA8 texture). **15-bit** cuts each color to five bits, which shows as bands in dark or smooth scenes; it was the only mode before 2026-09-22. Measured in Dolphin, the CPU side: +0.26% of wall in Micro Machines' intro video, +0.09% in Medievil's, and nothing outside 24-bit video. The graphics chip's side (an RGBA8 texture has twice the bytes to read) shows only on a Wii: `scripts/chains/fmv_ab.txt`. Applies at once. |
| `Gamma` | 0 to 5 = 1.0, 1.1, 1.2, 1.3, 1.4, 1.5 | 0 | Video, Advanced page, "Gamma" | The gamma of the Wii video encoder (`VIDEO_SetGamma`, `Gamecube/vi_encoder.c`). Higher is brighter. 1.0 writes nothing at start, so the picture is as before. It is one write to the encoder: at start, and when the setting changes. |
| `DeflickerLevel` | 0 = Normal, 1 = Light, 2 = Strong | 0 | Video, Advanced page, "Deflicker Level" | How much the frame copy mixes each line with the lines above and below while `DeflickerFilter` is on, in an interlaced mode (480i, 576i). In 64ths, line above / line / line below: **Normal** 16/32/16, the video mode's own taps (8 8 10 12 10 8 8); **Light** 8/48/8 (4 4 16 16 16 4 4): sharper, a little more flicker; **Strong** 21/22/21 (10 11 7 8 7 11 10): the softest, the least flicker. 240p and 480p have no flicker to remove and keep no filter whatever the level. Until this version Light was 0 0 21 22 21 0 0, which is no filter at all: it looked the same as Deflicker off. Applies at once: a change is written to the copy filter when it is made, and again whenever a game starts or resumes (`go()`, `applyCopyFilter()` in `Gamecube/libgui/GraphicsGX.cpp`). Before, a game in 480i kept the menu's filter until something else set it: with the Soft renderers Deflicker and its level changed nothing at all (measured in Dolphin's XFB: 0 pixels between Deflicker on and off), OpenGX took them at its first picture readback. |
| `FilterTour` | 0 = Off, 1 = On | 0 | Video, Advanced page, "Filter Tour" | A demonstration. While a game runs, every 4 seconds the next step: each setting alone, then a few combinations, only the ones that change something on the running renderer. Soft renderers (`gpuPlugin` 0 and 1), 7 steps: Near; Bilinear; `GpuTiming` Accurate; `SoftGpuMode` Hi-Fi; Fast; Bilinear + Hi-Fi; Hi-Fi + `GpuTiming` Accurate (the others with `SoftGpuMode` Accurate). OpenGX (`gpuPlugin` 2), 10 steps: Near; Bilinear; `GpuTiming` Accurate; `GxTexMin` Linear, Aniso 2x, Aniso 4x; `GxColor` 6-bit dither; Bilinear + Aniso 4x; Aniso 4x + 6-bit dither; Bilinear + Aniso 4x + 6-bit dither. In 240p (`TVMode` 1) the Bilinear steps are left out (every renderer draws Near there) and OpenGX gains `GxColor` AA alone and with Aniso 4x: 5 soft steps, 9 OpenGX steps. Each step applies at once: `SoftGpuMode` and `GpuTiming` are read live, the texture filters go on OpenGX's textures at their next use, and `GxColor` changes the EFB format right after the next copy to the TV picture. `GpuTiming` changes timing, not the picture. The step is written in bold under the FPS counter, which is on while the tour runs. Your own Bilinear, `SoftGpuMode`, `GpuTiming`, `GxTexMin`, `GxColor` and Show FPS come back when the game stops, before the menu shows them. It is Off at every start: no settings file holds it, and WiiStation ignores it in a file. An `autoinput.txt` line `filtertour <seconds>` turns it on for a test run, with that many seconds a step. The old soft GPU (PeopsSoftGPU, not built) has no tour. |
| `SoftGpuMode` | 0 = Accurate, 1 = Hi-Fi, 2 = Fast | 0 | Video, Advanced page, "Soft Mode" | The pixel pipeline of the two soft renderers, Soft Fast and Soft Timed (OpenGX ignores it). **Accurate** draws as a PS1 GPU (`SoftGPU/acc.c`, the rules of Mednafen's PS1 GPU): triangle edges and color and texture interpolation in the PS1's fixed point, the PS1 dither matrix where the game asks for dither (GP0 E1 bit 9), 15-bit semi-transparency, set-mask (E6.0) and check-mask (E6.1) on every write (polygons, rectangles, lines, GP0 A0 image loads and GP0 80 copies; the GP0 02 fill writes bit 15 = 0 and ignores both), the CLUT cache and the texture cache, image loads that wrap at x = 1024, textured 1x1 rectangles (GP0 6C..6F), and the GPUSTAT texture-disable bit (GP1 09h). Every ps1-tests GPU test with a real-PS1 result passes with it (`scripts/chains/ps1tests_gpu.txt`); `gpu/bandwidth` also needs `GpuTiming` = 1. **Hi-Fi** keeps every Accurate rule (where pixels go, which pixels blend, the mask bit) and changes only the color maths: no dither, colors rounded to the nearest 5-bit step instead of cut, and semi-transparent pixels blended from the 8-bit color. **Fast** is the P.E.Op.S. code of 5.15.0 and before: its own dither table, approximate edges and blending, no mask on image loads. `Dithering` applies to Fast only. With `GpuTiming` = 1, Accurate and Hi-Fi also keep the GPU busy for the time each command draws on a PS1. Accurate and Hi-Fi cost more Wii CPU time than Fast. Takes effect at once. |
| `TVMode` | 0 = Off, 1 = On | 0 | Video, 240p | Sets the 240p original mode. The picture uses the original PlayStation resolution. Fixed 2026-09-23: with 240p saved in the settings, or on any game after the first, the picture was garbage (a purple screen, a cropped picture, or four narrow copies with interleaved lines) until the 240p button was pressed again. |
| `BilinearFilter` | 0 = Default, 1 = Near, 2 = Bilinear | 1 | Video, texture filter button | Selects the texture filter of the OpenGX renderer. Near keeps sharp pixels. Bilinear makes textures smooth. |
| `TrapFilter` | 0 = Off, 1 = On | 1 | Video, Trap | Enables the trap filter of the Wii video encoder: a notch that takes the color carrier's frequency out of the brightness signal, so composite (and S-Video) shows less color crawl and fine detail a little softer. It does nothing on component (480p or 480i over YPbPr) or HDMI adapters, and Dolphin does not emulate it. Applies at once. |
| `Interlaced` | 0 = Off, 1 = On | 0 | Video, Interlaced | Sets interlaced output. |
| `DeflickerFilter` | 0 = Off, 1 = On | 1 | Video, Deflicker | Enables the deflicker filter of the frame copy. |
| `ForceNTSC` | 0 = Off, 1 = On | 0 | Video, Force NTSC | Runs a PAL game at the NTSC timing of 60 Hz. |

## 4. Audio settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Audio` | 0 = Off, 1 = On | 1 | Audio, Enable: Audio | Enables the sound output. Note: the menu label is inverted. **Disable Audio = Yes** writes `Audio = 0`. |
| `Interpolation` | 1 = Simple, 2 = Gaussian | 2 | Audio, Interpolation | Selects the sample interpolation of the sound processor's voices. The value 0 is not permitted. Gaussian is the default since 2026-09: it is the better of the two and the whole sound processor costs under 1% of a frame. The menu control for this was labelled "Volume" in the source for years; there has never been a volume control. |
| `SoundResampler` | 0 = Hold, 1 = Linear, 2 = Cubic | 2 | Audio, Advanced, "Output Resampler" | How the mixed 44100 Hz stream is converted to the Wii's 48000 Hz output. `Hold` repeats the previous sample, which is what both output paths always did; `Linear` and `Cubic` (4-point Catmull-Rom) interpolate on the CPU instead, at a small cost per frame that the debug build reports as `out_us` in perf.log. `Cubic` is the default since 2026-09-21: measurably cleaner above 16 kHz than `Hold` with no measured cost in the sound system's under-1%-of-a-frame budget. Takes effect at once, also mid-game. |
| `DisableXa` | 0 = play XA, 1 = mute it | 0 | Audio, Enable: XA | Mutes the disc's XA audio streams, which most games use for music and speech. Before this key existed the menu toggle worked for the session and was forgotten at the next boot. |
| `DisableCdda` | 0 = play CDDA, 1 = mute it | 0 | Audio, Enable: CDDA | Mutes Red Book audio tracks, which is how many games store their music. Same note as above. |
| `SoundReverb` | 0 = Off, 1 = On | 1 | Audio, Advanced, "Reverb" | Turns the sound processor's reverb effect (hall, room, echo) on or off. Games that use it lose that effect entirely with this off; games that do not use it are unaffected either way. |
| `SoundMixerPrecision` | 0 = Legacy, 1 = Hi-Fi | 1 | Audio, Advanced, "Mixer Precision" | Rounds each internal volume scale-down to the nearest value instead of truncating it, in the 24-voice mixer and the final master-volume stage. Halves the average quantisation error at those steps; the mixed level and behavior are otherwise identical, so this only ever reduces low-level noise. Hi-Fi is the default. |
| `SoundXaResampler` | 0 = Legacy, 1 = Hi-Fi | 1 | Audio, Advanced, "XA Resampler" | How CD-XA speech and streamed music (37800 Hz) is stepped up to 44100 Hz. Legacy is the nearest-sample or Gaussian step FeedXA always had. Hi-Fi replaces it with the sound processor's own real 7-phase, 29-tap filter (an exact 6-in/7-out ratio, no drift). Only 37800 Hz streams are affected; the rarer 18900 Hz streams always use the Legacy step. Hi-Fi is the default. |
| `SoundRateControl` | 0 = Off, 1 = On | 1 | Audio, Sync = Rate | Keeps the sound output's queue at its target by playing a fraction of a percent faster or slower (at most 0.5 %, and the change is gradual, so it is not heard as pitch), the way RetroArch and DuckStation do it. The sound processor emulation itself stays exactly on the emulated clock, so XA streams (speech and streamed music) never run ahead of the emulated disc. Both output paths use it: the CPU path scales its 44100 to 48000 Hz conversion, the DSP path moves the voice frequency. `0` turns it off, for measurements only: the queue then drifts at the difference between the emulator's frame limiter and the Wii's audio clock, and eventually starves or overflows. Stalls (a slow disc read from the SD card) are not this setting's job; the frame limiter lets the emulator catch up by up to 125 ms afterwards, which refills the queue. |
| `SoundTempo` | 0 = Off, 1 = On | 0 | Audio, Sync = Tempo | The older way of keeping the output fed, kept for comparison. When the output runs low, the sound processor emulation generates half a frame of extra audio to refill it. The cost: XA streams are then used up faster than the emulated disc delivers them and get short silent gaps, about one sector (53 ms) long, in scenes where the emulator is slow (measured: 61 gaps in 110 s of Spyro speech in a slow debug build, 2 in the release build). Every release before 2026-09-20 behaved this way. Leave it `0`; `SoundRateControl` replaces it. |

The menu shows the two as one three-way control, **Sync**: Off, Tempo or Rate. Selecting one turns the other off, and Off turns both off. The file can still hold both keys at `1`; both mechanisms then run together, which is harmless but pointless.

The menu also has the buttons **Disable XA** and **Disable CDDA**. WiiStation does not save these two buttons to the file. They return to Off at each start.

## 5. Save settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `NativeDevice` | 0 = SD, 1 = USB, 2 = CardA, 3 = CardB | 0 | Saves, Memcard Save Device | Selects where WiiStation keeps the memory card files. Card A and Card B are GameCube memory cards. |
| `StatesDevice` | 0 = SD, 1 = USB | 0 | Saves, Save States Device | Selects where WiiStation keeps the save states. |
| `AutoSave` | 0 = No, 1 = Yes | 1 | Saves, Auto Save Memcards | Writes the memory cards to the device when you exit a game. |
| `Memcard0`, `Memcard1` | 0 = Off, 1 = On | 1 | Saves, Memcard 1, Memcard 2 | Whether each memory card is there at all. The menu shows this and the file setting below as one button per card: **Off**, **Shared** or **Game**. |
| `Memcard0File`, `Memcard1File` | 0 = Shared, 1 = Game | 1 for card 1, 0 for card 2 | Saves, Memcard Type | Which file each memory card lives in. **Game** gives every disc its own card: card 1 is `<CdromId>.mcd` and card 2 is `<CdromId>-2.mcd`. **Shared** gives one card that every game opens: card 1 is `shared1.mcd` and card 2 is `slot2.mcd`. The defaults keep the names WiiStation always used, so no existing card is left behind. Shared is the only way one game can read another game's save, which a few games do. Applies when the next game is loaded, because the card in memory was read from the old file. |

## 6. Input settings

### 6.1 Controller settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `PadAutoAssign` | 0 = Manual, 1 = Automatic | 1 | Input, Configure Input, Pad Assignment | Lets WiiStation connect the controllers automatically. On a multitap, slot A to D takes GameCube pad 1 to 4 whenever that pad is connected, so each player keeps the same slot; the other ports and empty slots then take the remaining controllers (GameCube, then Wii, then HID). A port or slot with no controller stays empty and does not stop the ones after it. |
| `PadType1` to `PadType10` | 0 = None, 1 = GameCube Pad, 2 = Wii Pad, 3 = HID Pad, 4 = Multitap, 5 = Co-Op (`PadType1` and `PadType2` only, section 6.1.1) | 0 for `PadType1` and `PadType2`; 1 (GameCube Pad) for the multitap slots `PadType3` to `PadType10` | Input, Configure Input | Sets the controller type of each PlayStation port. See the port list below. A multitap's slots A to D start as GameCube pads 1 to 4; a port switched to Multitap whose four slots are all None gets the same. When a port's type changes while a game runs, the port shows as empty for half a second before the new controller appears, as when a real controller is unplugged and another plugged in: the game then looks for its controllers again. Without this, Crash Bash took no input from a multitap that had become a GameCube pad. |
| `PadAssign1` to `PadAssign10` | 0 to 3 | 0, except `PadAssign2` = 1 and the multitap slots: A to D are 0 to 3 (`PadAssign3`..`6` and `PadAssign7`..`10`) | Input, Configure Input | Sets which physical controller (1 to 4, written 0 to 3) drives each port. Values 4 to 9, which older versions accepted, are ignored. |
| `RumbleEnabled` | 0 = No, 1 = Yes | 1 | Input, Enable Rumble | Enables the controller rumble. Note: the menu label is inverted. **Disable Rumble = Yes** writes `RumbleEnabled = 0`. A Wii Remote or GameCube pad motor is only on or off: the DualShock's small motor turns it on, and the large motor's strength (00h to FFh) turns it on for that share of every 8 frames (FFh all of them). A motor keeps the value the game last sent, and every motor stops when the game goes to the menu. |
| `ControllerType` | 0 = Standard, 1 = Analog, 2 = Stick D-pad | 0 | Input, PSX Pad Type | Sets the emulated PlayStation controller type. **Standard** is a digital pad (SCPH-1080): it answers the read command 42h and nothing else. **Analog** is a PS1 DualShock (SCPH-1200): it starts in digital mode, as the real pad does, and a game that supports analog switches it to analog mode itself with the config commands (Spyro, Ape Escape). Both behave as the real pads do, checked command by command with PadTest DX (Docs/CONTROLLER_TESTING.md). Its ANALOG button is a key combination on the Wii controller (section 6.6): games that leave the mode to the player (MediEvil, and games made for the older Dual Analog pad) stay digital until it is pressed, as on a PlayStation, unless `AnalogAuto` (below the table) is on, which is the default: it does that press for the games that ask the pad about its modes. **Stick D-pad** is the Standard digital pad, and the left stick also presses its D-pad: past half of full travel, the stick's direction presses Up, Down, Left or Right, or two of them on a diagonal (each of the eight directions takes 45 degrees). Use it for games that read only the D-pad, so they can be played with the stick. It applies to every port and multitap slot. |
| `AnalogAuto` | 0 = Off, 1 = DualShock games, 2 = Always | 1 | Not in the menu | Applies to PSX Pad Type Analog only: it puts the pad in analog mode (red light) by itself, so the player need not press the ANALOG combination (section 6.6). **Off**: the pad starts digital and stays so until the game or the combination changes it, as a real DualShock does. **DualShock games** (the default): a game that asks the pad what it can do (config command 43h 01h) and leaves config mode, and then sends no mode command (44h) to any pad for 10 seconds (600 vblanks), gets analog mode on its connected pads, once. The wait keeps games that choose their own mode, a few seconds after they first ask (Ape Escape), exactly as they were, so recordings of them replay as made. Gex 3 and MediEvil do this and never send the mode command 44h, so on a real pad the player has to press ANALOG. The pad then tells the game its mode changed (as the real button does), the game asks again and finds the sticks. Games that never use config mode (digital-only games, and games made for the older Dual Analog pad) stay digital, so every digital-only game keeps reading its buttons. A game's own 44h and the player's combination always win and are never undone. **Always**: every game starts in analog mode, as DuckStation's "force analog mode on reset" does. A game made only for the digital pad (ID 41h) may then ignore the buttons: use it for Dual Analog era games that read the sticks without config mode, and switch it back to 1 if a game does not respond. |
| `PadMirror` | 0 = Off, 1 = On | 0 | Not in the menu | One controller plays every player. Each multitap slot that has no controller of its own takes the buttons and sticks of port 1 (with a multitap on port 1: of slot A), and the game sees a controller there; so does port 2 when port 1 has no multitap. With a multitap on port 1, port 2 stays as set: a copy there would be a fifth player, and menus that read every controller (CTR's pause menu) would take each press twice. For trying a split-screen game alone, for example Crash Team Racing with four players: put `PadType1 = 4` (multitap on port 1) and `PadMirror = 1` in the game's own file (`settings/SCUS94426.ini`). A Co-Op port, and a slot with its own controller, are not changed. |
| `LoadButtonSlot` | 0 to 3 = Slot 1 to Slot 4, 4 = Default, 5 = Auto | 5 | Input, Auto Load Slot | Loads a saved button mapping at start. Auto loads the game's own mapping (the Game slot, `wiistation/buttons/games/<CdromId>.cfg`) if it has one, else the default. Default loads the built-in mapping at each game start (as Restore Default does). |
| `LightGun` | 0 = Off, 1 = GunCon, 2 = Justifier, 3 = Mouse | 0 | Input, Light Gun | Selects the emulated light gun or mouse. The Wii Remote pointer controls it. One type for every port; a port takes it when its `PadLightgunN` is on and a Wii Remote plays it (Docs/GUN_MOUSE_TESTING.md). |
| `GunReticle` | 0 = Off, 1 = On | 1 | (no menu row yet) | Draws a small crosshair where the Wii Remote points while it is a light gun or mouse: a cross, and a ring for a gun, in the remote's color (green, red, blue, yellow for remotes 1 to 4). |
| `PadLightgun1` to `PadLightgun10` | 0 = Off, 1 = On | 1 | Input, Configure Buttons, Gun/Mouse | Enables the light gun or mouse on each port. |

The ten port numbers map to the PlayStation ports in this order:

| Number | PlayStation port |
|---|---|
| 1 | Port 1 |
| 2 | Port 2 |
| 3 to 6 | Multitap 1, slots A to D |
| 7 to 10 | Multitap 2, slots A to D |

#### 6.1.1 Co-Op: several controllers on one port

A port set to Co-Op (`PadType1` or `PadType2` = 5) is one PlayStation controller played by up to eight people at once, for example one steering and one firing in a game made for one player. Each player has a controller and a layout: the PlayStation buttons and sticks that player works. A button counts as pressed when any player who has it presses it. On each stick axis the player who pushes further wins: an average would halve one player's push whenever the others leave their sticks alone. Rumble runs in every player's controller.

In the menu: Configure Input, Manual, then press the port's type button until it shows Co-Op. The port's number button then sets the number of players, and one row per player appears under the port: the controller type (GC, Wii, HID or None), Customize, and which controller of that type (1 to 4). Customize shows the PlayStation controller with the player's buttons lit, and six layouts: Full pad, Left half (L1, L2, L3, Select, the D-pad and the left stick), Right half (R1, R2, R3, Start, the four action buttons and the right stick), D-pad+sticks (the D-pad, both sticks, L3 and R3), Action (the four action buttons) and Shoulders (L1, L2, R1, R2). Switching to Automatic turns a Co-Op port back into a plain one, as it does a Multitap.

| Key | Values | Default | Effect |
|---|---|---|---|
| `CoopPlayers1`, `CoopPlayers2` | 1 to 8 | 2 | The number of players on port 1 or 2. |
| `Coop1Type1` to `Coop1Type8`, `Coop2Type1` to `Coop2Type8` | 0 = None, 1 = GameCube Pad, 2 = Wii Pad, 3 = HID Pad | players 1 to 4: 1, players 5 to 8: 2 | The kind of controller player N of port 1 or 2 uses. |
| `Coop1Assign1` ... `Coop2Assign8` | 0 to 3 | player N: (N - 1) modulo 4 | Which controller of that kind (1 to 4, written 0 to 3). By default players 1 to 4 are GameCube pads 1 to 4 and players 5 to 8 Wii controllers 1 to 4. |
| `Coop1Layout1` ... `Coop2Layout8` | 0 = Full pad, 1 = Left half, 2 = Right half, 3 = D-pad+sticks, 4 = Action, 5 = Shoulders, 6 = Custom | 0 | The buttons and sticks the player works. Custom is the player's own list (`CoopPButtonsK`): on the Customize Co-Op page every button turns on or off by itself (A, or the pointer); a set that matches a preset becomes that preset. |
| `Coop1Buttons1` ... `Coop2Buttons8` | a list: L1, L2, Select, Start, R1, R2, Up, Left, Right, Down, Triangle, Square, Circle, Cross, LStick, RStick, L3, R3 | "" | The Custom layout's buttons and sticks, comma-separated (Gamecube/coop.c `coop_buttons_parse`; unknown names are skipped). Read only when the layout is 6. |

### 6.2 Button mapping files

The button mappings are not in the settings file. They are in the folder `wiistation/buttons/`, one file for each controller type:

| File | Controller |
|---|---|
| `gamecube.cfg` | GameCube pad |
| `usb-hid.cfg` | USB HID gamepad |
| `classic.cfg` | Classic Controller |
| `wiimote-nunchuk.cfg` | Wiimote with Nunchuk |
| `wiimote.cfg` | Wiimote alone |
| `wiiu-pro.cfg` | Wii U Pro Controller |
| `wiiu-gamepad.cfg` | Wii U GamePad |

Each game has its own file, `wiistation/buttons/games/<CdromId>.cfg`. It holds the Game slot of every controller type.

Each controller file holds that controller's four slots, its analog sensitivity (below), its screenshot combination and its ANALOG button combination (below). The files are binary. Do not edit them by hand.

**Save.** Configure Buttons, Save writes every file at once: the four slots of every controller type and the game's file. Settings, Save Button Configs (SD or USB) writes every file too. The Settings rows choose the device. Configure Buttons, Save writes to the device last chosen. The message names the device.

**Read.** At boot and at each game start, one set of files comes from one device. WiiStation first tries the device it booted from, or the device last chosen in Settings. If that device has a file for every controller type, WiiStation uses it. Otherwise it uses the other device when that one has every file. If neither has every file, it uses the device with more files. A set is never a mix of USB and SD.

**Older names.** Earlier versions used the files `controlG.cfg`, `controlH.cfg`, `controlC.cfg`, `controlN.cfg`, `controlW.cfg`, `controlP.cfg` and `controlD.cfg` in `wiistation/`, and `<CdromId>.btn` in `wiistation/` for each game. WiiStation reads an older file when its new file is missing. A Save writes only the new names. It never deletes the older files. You can delete them yourself after you check that the new files work.

### 6.3 Analog stick sensitivity

Input, Configure Buttons, the `x1.0` control. It is per controller type and per port, adjustable from x0.2 to x2.0 in steps of 0.1, and it is saved in that controller's button-mapping file, not in the settings file.

**What it does.** Each driver already maps its own hardware's full travel onto the range a PlayStation pad reports -- 0 hard left or up, 255 hard right or down, 128 at rest. The sensitivity is one gain applied after that, about the center:

    reported = 128 + (stick - 128) x sensitivity

So **x1.0 is a true 1:1 stick**: what the game reads is what the hardware measured, and the whole of the stick's travel is used. That is the default and it is what almost everyone should leave it on.

**Above 1.0** the stick reaches full deflection before the end of its physical travel, and the rest of the throw does nothing. It trades range for reach. Two reasons to want it: a worn stick whose springs no longer push it to the gate, and a game that wants full tilt to turn or accelerate where you would rather not push the stick all the way. At x2.0 the outer half of the travel is flat.

**Below 1.0** the stick can no longer reach either end: at x0.5 full deflection reports about a quarter and three quarters instead of 0 and 255. Games read that as a half-pressed stick, which is a way to walk instead of run, or to slow an over-eager camera.

**It does not change the dead zone and it does not change where the center is.** Rest is 128 at every setting, so raising it never makes a stick drift; it makes an existing drift bigger, because the drift is multiplied too. It also does not affect the digital directions, which come from the D-pad or from a separate threshold on the stick.

Until 2026-09-21 this setting moved the light-gun and mouse pointer only and did nothing to the sticks. What the sticks had instead was a fixed x1.40625, on top of whatever gain the driver had already applied; a Classic Controller reached full deflection at 54 % of its travel and a GameCube pad at 72 %. That is gone -- the setting is the only gain now.

### 6.4 HID controller files

A USB HID controller needs a configuration file in the Nintendont format. The file name is the vendor ID and the product ID of the controller in hexadecimal, for example `0810_0003.ini`. Put the file in `sd:/wiistation/controllers/` or `usb:/wiistation/controllers/`. See `README.md` for the file format.

### 6.5 Screenshot combination

Each controller type has its own screenshot combination. It is set in Configure Buttons, with the Screenshot button, and it is saved in that controller's button-mapping file, as the menu combination is. It is not a key of the settings file.

The choices are Off and a short list for each controller type. The default is:

- GameCube pad: L+R+Z.
- HID pad: L2+R2+Select.
- Wii Classic Controller, Wii U Pro Controller and Wii U GamePad: L+R+ZL.
- Wiimote with Nunchuk, or Wiimote alone: A+B+1.

Press the combination in a game to save the picture on screen. The file goes to `sd:/wiistation/screenshots/`. Each press saves one picture. The Current ROM page has a Screenshot button that does the same.

A screenshot combination never contains a menu combination. Configure Buttons refuses a screenshot choice that contains the exit or fast-forward combination, and it refuses an exit or fast-forward choice that lies inside the screenshot combination, because either one would open the menu or fast-forward first.

A mapping file from before this version has no screenshot entry. Each controller then takes its default.

### 6.6 ANALOG button combination

The PlayStation DualShock has an ANALOG button between the sticks. It switches the pad between digital mode (light off, the game reads the buttons only) and analog mode (red light, the game also reads the sticks). A Wii controller has no such button, so each controller type has a combination that presses it. It is set in Configure Buttons, with the ANALOG button under Fast Forward, and it is saved in that controller's button-mapping file, as the screenshot combination is. It is not a key of the settings file.

The choices are Off and a short list for each controller type. The default is the screenshot combination's two buttons with a third one:

- GameCube pad: L+R+Start.
- HID pad: L2+R2+Start.
- Wii Classic Controller: L+R+ZR.
- Wii U Pro Controller and Wii U GamePad: L+R+ZR, or LS+RS (both sticks pressed in).
- Wiimote with Nunchuk, or Wiimote alone: A+B+2.

While the combination is held the game gets no buttons from that controller, so the Start in L+R+Start does not pause the game. Press L and R (or A and B) first, then the third button.

It works as the DualShock's button does (Docs/ANALOG_BUTTON.md):

- It works with PSX Pad Type Analog only. Standard and Stick D-pad are digital pads, which have no ANALOG button.
- Every game starts with the pad in digital mode, as at power-on. The setting `AnalogAuto` (table above) changes that: by default a game that asks the pad about its modes gets analog mode when it is done asking, so you press the combination only to switch back to digital (or to switch on for a game that does not ask).
- Each press switches digital to analog or back, for the PlayStation port (or multitap slot) of the controller that pressed it. On a Co-Op port any player's combination presses it.
- A game can lock the mode (Ape Escape does). Then the press does nothing.
- The press stops the rumble motors; the game starts them again.
- A note shows on the screen for two seconds: "Port 1: Analog (red light)" in red, "Port 1: Digital (light off)", "Port 1: ANALOG locked by the game", or "Port 1: no ANALOG button (PSX Pad Type)".

With `AnalogAuto = 1` (the default), games that leave the mode to the player but ask the pad about its modes (MediEvil, Gex 3) get analog mode by themselves, with the same note on the screen. With `AnalogAuto = 0` they stay digital until the combination is pressed, as on a PlayStation: press it once in the game and the left stick moves Dan. Games that never ask the pad (digital-only games, and games made for the older Dual Analog pad) stay digital until the combination is pressed, or for good with the default; `AnalogAuto = 2` starts every game in analog mode.

Configure Buttons keeps the combination apart from the others: it refuses an ANALOG choice that contains the exit or fast-forward combination, or that contains the screenshot combination or lies inside it, and the same the other way round. A mapping file from before this version has no ANALOG entry. Each controller then takes its default.

## Achievement engine

The engine evaluates a local rcheevos definition file for each game. It is off by
default. There is no account login, online award, or hardcore mode yet. Load the
game again after you change a set. See [the achievements reference](Docs/ACHIEVEMENTS.md)
for the definition format, state handling, persistence boundaries, and tests.

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Achievements` | 0 = Off, 1 = Custom, 2 = Test, 3 = Official, 4 = Local | 0 | General, Achievements, "Mode" (the menu cycles Off, Local, Official, Custom, Test) | Custom: your own definition file, unlocks kept in the journal; nothing is downloaded (this was called Local before 5.19.0: a settings file with 1, 2 or 3 keeps its meaning). Test: the same, nothing read from or written to the journal. Official: downloads the RetroAchievements set, Casual; needs an account (a Mii's, linked in Players, or `achievements/ra_account.ini`), `tls_seed.bin` and `cacert.pem` on the card (Docs/ACHIEVEMENTS.md). Local: the same RetroAchievements set, but nothing is ever sent to the site: every player (each seated Mii, or Guest) earns into the Local journal `achievements/ra_local/<game id>.unlocks`, in Casual rules. The set is downloaded once (it needs a login: the Official Mii's account, `achievements/ra_account.ini`, or the first file in `achievements/ra/`) and saved in `achievements/ra_cache/`; after that the game loads with no network and no account, and the card is asked again after 30 days (the old copy is used when there is no network). With no network and nothing saved, the game card says so. The Achievements page has no account row since 5.19.0: the accounts are in Players. |
| `AchievementFile` | `"Path"` | empty | none, settings file only (General, Achievements shows it) | The rcheevos definition file for a game, for example `sd:/wiistation/achievements/<game>.txt`. |
| `AchievementLog` | `"Path"` | empty | none, settings file only | Stores earned unlocks. Empty puts them beside the definition file, with `.unlocks` appended. A quoted full path overrides it. |
| `AchievementUnofficial` | 0 = Off, 1 = On | 0 | General, Achievements, "Unofficial" | Official and Local modes: the set's Unofficial (not yet approved) achievements too; RetroAchievements does not keep their unlocks. Load the game again after a change. |
| `AchievementEncore` | 0 = Off, 1 = On | 0 | General, Achievements, "Encore" | Official mode: achievements already earned can be earned again this session (the card shows again; nothing new is sent). It is also on, whatever this says, while a Local Mii is seated next to the Official one (it earns what the account already has). |
| `AchievementChallenge` | 0 = Off, 1 = On | 1 | General, Achievements, "Challenge icons" | The flashing icon at the bottom right of an achievement that is one step away (RetroAchievements' challenge indicator; often a missable achievement, and a Local set's primed one). Off hides it, at once; the unlock card and the progress pop-up stay. |
| `AchievementSpectator` | 0, 1 | 0 | none, settings file only (hidden) | Official mode for scripted test runs on a real account: everything is evaluated and shown, no unlock is sent. |
| `AchievementUTCOffset` | `"Minutes"` | empty | none, settings file only | Official mode: minutes to add to the Wii's clock (local time) to get UTC, for certificate dates, e.g. `"+420"`, up to 840 either way. Empty (the default): the Wii learns it from the server's answers and keeps it in `achievements/utc_offset.txt`, summer time included. Set, it wins and nothing is learned. |

## Players

Who plays on each PlayStation port, set in the Players menu (Main, Players): a Mii from the
Mii Channel's database, read only, never written (Docs/ACHIEVEMENTS.md, Players and Miis). The keys are per Wii: a
game's file never holds them. Since 5.18.2 they are in their own file, `wiistation/players.ini`
beside `settings.ini`, which the Players menu writes when it closes. Seat keys in a
`settings.ini` from 5.18.0 or 5.18.1 are still read; `players.ini`, read after it, wins.

Since 5.19.0 the seats decide who earns achievements in Local mode: every Mii seated on a port
whose pad is connected, and Guest when none is (`Docs/ACHIEVEMENTS.md`, "Unlock journal"). The
unlock journal gets a line per Mii, with its key; lines from before are Guest's.

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Player1` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1A` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 1A: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1B` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 1B: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1C` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 1C: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1D` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 1D: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2A` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 2A: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2B` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 2B: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2C` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 2C: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2D` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated on port 2D: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P1` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 1 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P2` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 2 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P3` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 3 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P4` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 4 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P5` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 5 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P6` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 6 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P7` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 7 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player1P8` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 8 of port 1: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P1` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 1 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P2` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 2 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P3` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 3 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P4` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 4 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P5` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 5 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P6` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 6 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P7` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 7 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `Player2P8` | `"Key"` | empty | none, players.ini only (Main, Players sets it) | The Mii seated as Co-Op player 8 of port 2: its creation ID, 16 hex characters. Empty is Guest. |
| `PlayersRecent` | `"Keys"` | empty | none, players.ini only (Main, Players sets it) | The Miis last seated, most recent first: up to 15 keys, comma-separated. The Players menu lists them first, then the others in the Mii Channel's order. |
| `PlayersOfficial` | `"Key"` | empty | none, players.ini only (Main, Players: a card's Official button, or a Mii's Edit page) | The Mii that plays Official (RetroAchievements), one per game, when it is seated on a port that plays and linked to an account. Empty, or that Mii not seated: port 1's Mii (1A with a multitap, 1 P1 with Co-Op) if it is linked. Official mode logs in with that Mii's account file at the next game load (`achievements/ra/<name>.ini`, or `ra_account.ini` for "*" and for no Mii); the other seated Miis earn Local. |

`players.ini` also has an `[Accounts]` section: one `<Mii key> = "<name>"` line per Mii linked to
a RetroAchievements account (a Mii's Edit page, Account). `<name>` is an account file,
`achievements/ra/<name>.ini`, with the same lines as `ra_account.ini`; `"*"` is `ra_account.ini`
itself (This Wii's account). The menu lists the files by name and never opens them. A Mii with no
line earns Local.

A chain line (`scripts/chains/`, unattended runs) may seat a Mii as `Player1="#2"`: the console's second Mii,
in the Mii database's own order. For a run on a Wii whose Miis are not known beforehand.

## 7. Network settings

WiiStation can read games from a network share (SMB2 or SMB3: a Samba server, a NAS or a
Windows shared folder, as they are set up by default). Since 5.7.0 it uses libsmb2
(`deps/libsmb2`); the library before it spoke only SMB1 and NTLMv1, which current servers
turn off. These keys are text values. They are not in the menu.

| Key | Value | Default | Effect |
|---|---|---|---|
| `smbipaddr` | `"192.168.1.10"` | empty | The IP address of the server. |
| `smbsharename` | `"share"` | empty | The name of the share. |
| `smbusername` | `"name"` | empty | The user name of the share. |
| `smbpassword` | `"password"` | empty | The password of the share. |
| `RomDirSMB` | `"folder, folder, ..."` | empty (the top of the share) | The game folders on the share, as `RomDirSD`: `RomDirSMB = "/Games/PSX/ROMs"`. Load from Samba opens it, or lists several to choose from. Folders inside the share open like folders on the SD card. |

WiiStation starts the network only when `smbsharename` and `smbipaddr` are both set.

**Speed.** The Wii's Wi-Fi carries about 1 MB/s of plain TCP, and an SMB read reply comes
in at about 650 KB/s (the bench Wii, 2026-10-08). A game reads its disc at up to 300 KB/s.
Since 5.8.0 WiiStation reads the share in 16 KB requests on a thread of its own, and asks
for the next 16 KB while the game uses the last: Ape Escape's first 20 s waited 0.6-5 s for
the network (with the Wi-Fi conditions), against 7.8-8.8 s in 5.7.0, and on a good run ran
as fast as from the SD card. A game that streams a lot (loading screens, video) can still
wait now and then. WiiStation starts no CD read-ahead for a game on the share. Compressed
images (CHD) send less data, so they load faster over the network.

**The password is not encrypted.** WiiStation writes it back to the settings file as
plain text when you save the settings from the menu. Use an account that can read the
share and nothing else.

One background thread does all of the network work. It brings the interface up, then it
connects to the share, and it waits five seconds between attempts. This takes some
seconds after the menu opens, so the first time you open **Load from Samba** you can get
*Still connecting to the share*. Try again.

The thread stops while a game runs, and starts again when you go back to the menu.

If the share goes away, the next directory you open fails and the thread makes a new
connection. You do not have to restart WiiStation. A game that is running reconnects by
itself, once, when a read fails.

| Message | What it means |
|---|---|
| `SMB is not configured` | `smbsharename` or `smbipaddr` is empty. |
| `Still connecting to the network` | The interface does not have an address yet. |
| `Still connecting to the share` | The interface is up and the share is being opened. |
| `Cannot connect to the share` | The address, the share name or the account is wrong. The next line gives the server's reason. |

## 8. File browser settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `FileSortMode` | 0 = Folders and files mixed, 1 = Folders first | 1 | none, File browser Z button | Sets the sort order of the file list. |

## 9. Other files in `wiistation/`

| File or folder | Purpose |
|---|---|
| `autoboot.txt` | Starts a game without the menu. See section 10. |
| `autoinput.txt` | Presses controller buttons at given times. See section 11. |
| `bios/SCPH1001.BIN` | The PlayStation BIOS. Needed when `BiosDevice` is 1 or 2. |
| `fonts/<Name>.dat` | A menu font, selected with `MenuFont`. The repository has four in `fonts/menu/`: `Classic` (the original glyphs, descenders cut), `Segoe` (the built-in face with full descenders and Latin accents), `Times` (a Times New Roman face) and `CalibriBold` (a bold Calibri face, made with the recipe in section 9.1). Make a new one with `scripts/genfont.py`. |
| `ppf/` | PPF patch files. WiiStation applies a patch with the same name as the game. |
| `lang/` | Menu language files. |
| `fonts/chs.dat` | Font file for the Chinese menu language. |
| `settings/` | Settings files for one game. See section 1.4. |
| `players.ini` | The Mii seated on each port, the Official Mii and the Miis' account links, written by the Players menu. See Players. |
| `saves/` | Memory card files. WiiStation creates this folder when it saves. |
| `controllers/` | HID controller files. See section 6.4. |
| `perf.log` | Performance counters. Only the debug build writes this file. |
| `vram.bin` | Video memory snapshot. Only the debug build writes this file. |

### 9.1 How to make a menu font

A menu font file holds one 24 x 24 pixel image for each character. The script `scripts/genfont.py` makes the file from a TrueType font. It needs Python 3 and the Pillow package (`python -m pip install --user pillow`).

1. Open a shell in the repository folder (the WiiStation checkout, not a subfolder) and run the script. This example makes a bold Calibri font with all Latin characters:

   ```
   python scripts/genfont.py --charset latin --ttf C:/Windows/Fonts/calibrib.ttf --out fonts/menu/CalibriBold.dat --preview preview.png
   ```

   The script selects the largest size that keeps the tallest and the deepest characters inside the 24 rows. It reports the size and the baseline. Open `preview.png` to check the result.
2. Copy the file to `sd:/wiistation/fonts/CalibriBold.dat`.
3. Add this line to `settings.ini`: `MenuFont = "CalibriBold"`.
4. Start WiiStation. To go back to the built-in font, remove the line.

Use `--charset ascii` for a smaller file with only the English characters. Use `--ref fonts/En.dat` instead of `--charset` to keep the character widths of an existing font file, so that the menu layout does not change.

## 10. Start a game without the menu

The file `sd:/wiistation/autoboot.txt` makes WiiStation start a game immediately. WiiStation reads this file only from the SD card. The file has exactly two lines:

```
sd:/wiistation/isos/Spyro the Dragon
Spyro the Dragon [NTSC-U] [SCUS-94228].cue
```

- Line 1 is the folder that contains the game.
- Line 2 is the file name of the game. The name must be unique in the folder.

WiiStation ignores the file when a loader already gives a game as an argument. Delete the file to get the menu back.

To start the BIOS shell instead of a game, write only the word `BIOS` on line 1:

```
BIOS
```

This does the same as the menu item "Execute Bios". `BiosDevice` must be 1 or 2, because the HLE BIOS has no shell.

### 10.1 Several games in one boot (test runs)

If line 1 is the word `CHAIN`, WiiStation runs a list of games one after the other, and switches the console off after the last one. This is a test aid: it is how one Dolphin run or one session on a real Wii measures many games. Each game takes three lines:

```
CHAIN
600 sd:/wiistation/spyro_play.txt PadAutoAssign=1 ControllerType=0
sd:/wiistation/isos/Spyro the Dragon
Spyro the Dragon [NTSC-U] [SCUS-94228].cue
```

- Line 1 of a game: the number of emulated vblanks to run, then optionally an input script (section 11), then optionally `Key=Value` settings for this game only.
- Lines 2 and 3: the folder and the file name, as in `autoboot.txt`. A `.exe` program can take the place of a disc image.
- Lines that start with `#` are comments.

Each game writes its results to `sd:/wiistation/perf.log` (debug builds). When WiiStation stops because of an error, the last line of `perf.log` is `fatal: code=... <reason>`. The chain files used for testing are in `scripts/chains/`.

A chain holds at most 64 games; WiiStation ignores the lines after them. At its last vblank each game also writes its VRAM to `sd:/wiistation/vram_NN.bin` (NN = its place in the chain); `ChainDump = 1` moves that dump to the next frame's clear (section 2).

## 11. Scripted controller input

The file `sd:/wiistation/autoinput.txt` presses controller buttons at given times. It is a test aid. It works with `autoboot.txt`: a boot with both files is deterministic up to any screen. WiiStation reads the file when the controller plugin opens. A missing file has no effect. Real controllers keep working. The script only adds presses.

Port 1 must have a controller type. Set `PadType1 = 1` and `PadAutoAssign = 0` in the settings file when no controller is connected. With `PadType1 = 0` the PlayStation sees no controller and ignores the script. With `PadAutoAssign = 1` WiiStation sets the type back to 0 when it finds no controller. A script with at least one press line makes port 1 report a connected digital controller, so the BIOS shell and games that check for a controller accept the presses.

Each line has a vblank number and a button mask. From that vblank on, WiiStation holds the listed buttons on PlayStation port 1. The next line replaces the mask. A mask of `0000` releases all buttons. Lines that start with `#` are comments.

```
# vblank  mask (hexadecimal)
1400 0008
1440 0000
2100 0008
2140 0000
```

This example presses Start at the title screen and presses Start again in the game to open the pause menu.

| Button | Mask | Button | Mask |
|---|---|---|---|
| Select | `0001` | L2 | `0100` |
| Start | `0008` | R2 | `0200` |
| Up | `0010` | L1 | `0400` |
| Right | `0020` | R1 | `0800` |
| Down | `0040` | Triangle | `1000` |
| Left | `0080` | Circle | `2000` |
| | | Cross | `4000` |
| | | Square | `8000` |

Add the masks to hold more than one button. Delete the file to stop the script.

Lines for another port or a multitap slot start with its name: `p2 <vblank> <mask>` for port
2, and on a multitap `p1b`, `p1c`, `p1d` (multitap 1, slots B to D) and `p2b`, `p2c`, `p2d`.
Slot A shares its port's lines (`p1` or no name, `p2`; `p1a` and `p2a` mean the same), so a
recording made with one pad plays a multitap game too. A slot with lines answers as a
connected controller when its type is not None (`PadType3` to `PadType10`), whether or not a
real controller is assigned to it.

```
100 0008
p1b 100 4000
p2c 160 0010
```

Up to eight `padtype <vblank> <port 1|2> <type 0..4>` lines change a port's type while the game
runs, as the Configure Input menu does, with the half-second unplug gap the `PadType` rows describe.

### 11.1 Sweeping the controller

One more line kind, for testing the controller path rather than a game:

```
padsweep 1200
```

From that vblank on, the GameCube driver reads a generated sweep instead of the pad: each
stick axis walked from one end to the other a step per vblank, then each button held on
its own, repeating. The substitution happens where the raw reading is taken, so the
driver's conversion, the pad plugin, the sensitivity setting and the PlayStation packing
all run on it exactly as on a real pad. Port 1 still needs a GameCube controller
(`PadType1 = 1`) for a driver to be assigned to it at all.

Unlike the press lines above, this replaces the pad rather than adding to it, so a real
controller does nothing while it runs. A debug build writes what came out to
`sd:/wiistation/padtrace.csv`; `scripts/padtest.py check` reads it. See
`Docs/CONTROLLER_TESTING.md`.

`padsweep <vblank> fast` is a short sweep (214 vblanks instead of 996): each button held 3
vblanks, the two X axes walked together in steps of 3, then the two Y axes.

`gcpress <vblank> <hex>` holds raw GameCube buttons on GameCube pad 1 for 10 vblanks, on top of
the pad (up to 8 lines; libogc's values: `1060` is L+R+Start, the ANALOG combination). It is for
the host combinations, which the PlayStation press lines cannot reach. A script with `padsweep`
or `gcpress` lines makes GameCube pad 1 count as connected, so it works in a Dolphin with no
input device bound.

For ports and slots without a GameCube pad, `sweep <vblank> <name>...` generates input in
the script instead: from that vblank each port or slot named (`p1`, `p2`, `p1b` ... `p2d`)
gets each button on its own (held 3 vblanks), then both X sticks end to end, then both Y
sticks, in steps of 4, repeating every 214 vblanks. Each name presses the buttons in its
own order, so a controller that turns up in the wrong slot shows. A swept slot counts as
connected, as with press lines. PadTest DX's controller matrix uses both
(`scripts/autoinput/padtest_dx_matrix.txt`).

### 11.2 Opening a menu page for a screenshot

```
menupage 2
```

Half a second after the menu appears, WiiStation opens the page named and stays there.
It is the only way an unattended run can photograph a menu: the menu reads the controllers
directly rather than through the drivers, so `padsweep` cannot drive it and the scripted
presses above only reach a running game.

| Value | Page | Value | Page |
|---|---|---|---|
| 1 | Settings, General | 6 | Advanced Sound |
| 2 | Settings, Video | 7 | Advanced Graphics |
| 3 | Settings, Input | 8 | Advanced, CPU tab |
| 4 | Settings, Audio | 9 | Advanced, CD tab |
| 5 | Settings, Saves | 10 | Memory |
| | | 11 | Advanced, GPU tab |
| | | 12 | Advanced, Input tab |
| | | 13 | Advanced, SPU tab |
| | | 20 | Configure Input |
| | | 21 to 36 | Co-Op Customize, port 1 players 1 to 8 (21 to 28), port 2 players 1 to 8 (29 to 36) |
| | | 40 to 65 | Configure Buttons for virtual controller N-40: 40/41 ports 1/2, 42 to 49 multitap slots 1A to 2D, 50 to 65 Co-Op players (as 21 to 36). A port that is a multitap or Co-Op opens on its first slot or player |
| | | 70 | Load ROM > Load from SD: the `RomDirSD` folders (one opens, several are listed) |

Use it with no `autoboot.txt`, so the run stays in the menu, and collect the frames the
run dumps. `scripts/menu_text_width.py` checks the same pages' geometry without booting
anything, which is faster but is not the same as looking.

## 12. Known differences between the code and the menu

- `BiosDevice`, `BilinearFilter`, `TrapFilter`, `Interlaced`, `DeflickerFilter`, and `LightGun` get their default from the variable initializer. The function `loadSettings()` does not set them.
- `Interpolation` and `Dithering` start at 0 in the variable initializer. The function `loadSettings()` then sets them to 2 and 1 respectively before it reads the file.
- `ControllerType = 2` is Stick D-pad since 5.3.0. Before, the value was an unused "light gun" entry that the file parser rejected; the light gun is the `LightGun` key.
- The keys `Debug` and `NumberMultitaps` are commented out in the code. WiiStation does not read or write them.
- WiiStation reads the old key `LimitFrames` (before `FrameRate` had Unlimited): `LimitFrames = 0` gives `FrameRate = 3`. WiiStation does not write it.
