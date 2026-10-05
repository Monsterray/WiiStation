# WiiStation settings reference

This document describes all the settings of WiiStation. It follows the ASD-STE100 Simplified Technical English rules. Each setting has one table row. The row gives the key in the settings file, the permitted values, the default value, the menu location, and the effect.

The line numbers in this document refer to `Gamecube/GamecubeMain.cpp` unless the text gives a different file.

## 1. The settings file

### 1.1 Name and location

The settings file is `settingsRX2022.cfg`. WiiStation reads it from one of two locations:

| Launch device | File that WiiStation reads |
|---|---|
| SD card (the default) | `sd:/wiisxrx/settingsRX2022.cfg` |
| USB storage (the loader argument starts with `u`) | `usb:/wiisxrx/settingsRX2022.cfg` |

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
3. Select **Save settings SD** or **Save settings USB**.

WiiStation writes all the keys in this document to the file. The folder `wiisxrx` must exist on the device. WiiStation does not create it.

### 1.4 Settings for one game

WiiStation can load a different settings file for one game. The file name is the disc serial number, for example `SLUS00067.cfg`. WiiStation looks for the file in this order and uses the first file that it finds:

1. `usb:/wiisxrx/settings/<serial>.cfg`
2. `sd:/wiisxrx/settings/<serial>.cfg`
3. `usb:/wiisxrx/settingsRX2022.cfg`
4. `sd:/wiisxrx/settingsRX2022.cfg`

A game settings file can contain any key in this document. To write the file, load the game, open the Settings menu, and select **Separately** on the General tab. WiiStation writes the file to `usb:/wiisxrx/settings/` if that folder exists. If not, it writes the file to `sd:/wiisxrx/settings/`.

A PS-X EXE program (a `.exe` file, for example a test program) has no disc serial number. WiiStation uses the common settings file (3 or 4) for it, never the file of the game that ran before it.

### 1.5 Settings from the loader

On the Wii, a loader can give settings as arguments after the program name. Each argument has the form `Key=Value`. WiiStation applies these arguments after it reads the settings file.

## 2. General settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Core` | 0 = Lightrec, 1 = Interpreter, 2 = Dynarec | 0 | General, Plugins page, "CPU Core" | Selects the CPU emulation core. A change resets the current game. |
| `BiosDevice` | 0 = HLE, 1 = SD, 2 = USB | 0 | General, Select Bios | Selects the BIOS source. HLE is the built-in BIOS emulation. The file `SCPH1001.BIN` must exist in `wiisxrx/bios/` on the device. If the file does not exist, WiiStation uses HLE. The DVD option in the menu is not implemented. |
| `BootThruBios` | 0 = No, 1 = Yes | 0 | General, Boot Through Bios | Shows the PlayStation start screen before the game starts. |
| `gpuPlugin` | 0 = Old Soft, 1 = New Soft, 2 = OpenGX | 0 | General, Plugins page, "GPU Plugin" | Selects the graphics renderer. Old Soft and New Soft draw with the CPU. OpenGX draws with the Wii graphics hardware. A change resets the current game. |
| `lang` | 0 to 12, see the language table below | 0 | General, Select language | Selects the menu language. The language files are in `wiisxrx/lang/`. The menu does not show a language if its font file is missing. |
| `SoundHwAccel` | `0`, `1` | `0` | Audio tab, "DSP Sound" | Chooses the sound output path. `0` is the CPU path: the stream plays through SDL. `1` hands the stream to the Wii's DSP through an AESND voice, and the DSP does the mixing, so the CPU does less work per frame. Which side converts 44100 to 48000 Hz depends on `SoundResampler` (section 4): with `Hold` the DSP path lets the DSP do it, with the other two the CPU interpolates first and the DSP plays the result 1:1. If the chosen path fails to open, the other one is used. The output driver is chosen when a game starts, so changing this mid-session takes effect the next time a game is loaded. Note for testing in Dolphin: its high-level DSP emulation recognises libogc's AESND microcode by hash, so a version it does not know falls back and sound may differ from real hardware. |
| `MenuFont` | `"Name"` | empty | none, settings file only | Selects the menu font. WiiStation loads `wiisxrx/fonts/Name.dat` from the SD card or the USB device. An empty value or a missing file gives the built-in font. The value does not apply to the Chinese, Japanese and Korean languages, which have their own glyph files. See section 9 for the font files. |
| `fastLoad` | 0 = No, 1 = Yes | 0 | General, Fast Load | Shortens the delays the emulated CD-ROM drive reports, so loading screens pass faster than on a real console. A PlayStation drive takes a fixed time to answer a command, to seek, and to deliver each sector; WiiStation normally reproduces those delays (`cdReadTime` in `cdrom.c`, one sector per 1/75 s at single speed). With this on, the long second response to a read command is cut from about 2.1 million cycles to one sector time, the first-read delay is shortened, and seeks settle sooner. Nothing is read any faster from the SD card: this only stops the emulator waiting. Some games depend on the real timing and break, which is why it is off by default -- audio and video that are streamed from the disc are the usual casualties, since their sectors then arrive earlier than the game expects. Try it per game (`wiisxrx/settings/<CdromId>.cfg`) rather than globally. |
| `CdBuffer` | 0 = 16 KB, 1 = 64 KB, 2 = 256 KB | 0 | General, CD page, "CD Read Buffer" | Size of the stdio read buffer each disc-image file handle gets (the main image, the sub-channel file, the CDDA handle, every file of a multi-file cue). **Keep 16 KB.** Beneath it libfat reads the card in 32 KB pages whatever the buffer is, so a larger buffer cannot make the card's commands longer: it only reads ahead data a seek then throws away. Measured 2026-09-22 on eleven games (perf.log `sd:`): 64 KB gave the same card commands to within 6%, 256 KB gave 20% more, and every card read was 32 KB in all three. The counts are the same on a Wii, where only their duration changes. Applies when the next game is loaded. |
| `GpuTiming` | 0 = Fast, 1 = Accurate | 0 | General, Plugins page, "GPU Timing" | How long the core keeps the GPU busy after the game gives it work, for OpenGX and the Soft Fast renderer (Soft Timed, gpulib, is always accurate). **Fast** is how WiiStation always ran: a draw list ends after as many cycles as it has words, and the GPU reads idle from the end of one list to the start of the next, even while a block upload (a video frame, a texture) is going on. **Accurate** charges gpulib's cost per command (polygons, sprites and fills by size) and leaves the GPU busy for every transfer, so a game sees the GPU as a PS1 shows it. It moves every game's timing a little: games that wait for the GPU run their frames as on a PS1. Takes effect at once. |
| `CpuTiming` | 0 = Fast, 1 = Accurate | 0 | General, Plugins page, "CPU Timing" | Whether the Lightrec core charges the cycles a PS1 CPU waits for its GTE (3D maths) and its multiply/divide unit. Both run beside the CPU, which waits only when it asks for a result too early: MFC2, CFC2, SWC2 or the next GTE command while a command runs (RTPS 15 cycles, RTPT 23, NCDT 44 ...), MFHI/MFLO while a MULT (9) or DIV (36) runs. **Fast** charges every instruction the same, as WiiStation always did, so heavy 3D code gets more done per frame than on a PS1. **Accurate** works the waits out per block when it is compiled; code that hides them behind other work pays nothing. Lightrec only (the Interpreter and Dynarec cores ignore it). Applies when the game resumes from the menu. |
| `SioTiming` | 0 = Fast, 1 = Accurate | 0 | Settings file and chain lines only | How long a byte on the controller ports takes. **Fast** is how WiiStation always ran: the controller's reply is there as soon as the game writes a byte, and its /ACK comes 535 cycles (15.8 us) later. **Accurate** times it as a PS1 does: the byte takes its 8 bits at the baud rate the game set (32 us at the BIOS's 250 kHz), and the controller's /ACK comes 450 cycles (13.3 us) after it (hardware: 6.8 to 13.7 us). A pad read then takes about three times as long, as on a PS1, which moves every game's timing a little. Memory card transfers keep the Fast timing in both. PadTest DX measures both (Docs/CONTROLLER_TESTING.md). |
| `LimiterWait` | 0 = Spin, 1 = Sleep | 1 | Not in the menu | How the frame limiter waits for the next frame. **Spin** busy-waits the whole time: exact, but no other thread runs while a game plays (the Homebrew Channel's agent, the CD read-ahead, the network). **Sleep** sleeps on a hardware timer alarm until 0.2 ms before the frame is due and spins only that last part (1 ms until 2026-10-01; the bench Wii woke at most 33 us late). Measured on the bench Wii 2026-09-30 (Spyro, FF7): the same speed and load, wake-ups at most 4 us late, and 54% of the time handed to other threads. Earlier attempts with usleep() asked for ten times the wait (the limiter's tick is 10 us, not 100 us), which is why they ran games at half speed. |
| `LimiterDebt` | 0 = Short, 1 = Long | 1 | Not in the menu | How much lateness the frame limiter pays back after a frame runs long. The limiter schedules each frame from when the previous one was due, so after a stall the next frames run unthrottled until the debt is paid. **Short** pays back at most 12.5 ms and drops the rest; the sound queue then refills only through the rate control. **Long** pays back up to 125 ms, about what the sound drivers keep queued. **Long by default since 2026-10-01** (`scripts/chains/limiter_debt_ab.txt`, Spyro through its world-entry stall, Dolphin): emulated speed 1.000 against 0.995, the sound queue on average 7033 samples against 4411, and the rate control never at its limit (240 saturated updates with Short). After a stall the next frames run fast for about 0.1 s. |
| `CdPrefetch` | 0 = Off, 1 = On | 1 | General, CD page, "CD Read-Ahead" | Runs a thread that keeps the next 31 sectors of a raw image (bin/cue, .iso) read in advance, so the emulated CPU does not stop while the card delivers them. Compressed images (CHD, PBP, .Z) are not affected. Measured 2026-09-22 on eleven games in Dolphin: 99.7% of sector reads were served from the ring, 77-90% of the time the game waited on the card moved to the thread, the card got 5-12% more commands, and every game ran exactly as with it off (same interrupt counts). **On by default since 2026-10-01**: the bench Wii (`.runs/hw_cd_ab`, `scripts/chains/cd_ab.txt`, `wsx.sh table NAME --detail cd`) showed the total wait on the card fall from 7.4% to 1.3% of wall in Spyro and from 7.9% to 1.4% in Medievil, with a shorter worst single wait (18.6 to 15.5 ms, 18.5 to 13.7 ms). CTR and Point Blank did not change. In Dolphin the worst wait grew (Medievil 9.5 to 18.9 ms); its card answers at once, so only the hardware figure counts. Applies when the next game is loaded. |
| `CdChdHunks` | 0 = 2, 1 = 4, 2 = 8 | 2 | General, CD page, "CHD Hunk Cache" | How many decoded CHD hunks stay in memory (about 20 KB each). The cache is a true LRU, so a larger one never decodes more often than a smaller one (at every moment it holds what the smaller one would, and more); 8 costs about 160 KB of MEM2, of which 13 MB or more is free in game. Games that alternate between places on the disc, such as streamed music beside level data, decode less often. The default was 2 until 2026-09-22. Applies when the next game is loaded. |

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
| `VideoMode` | 0 = Auto, 1 = NTSC, 2 = PAL 50, 3 = PAL 60, 4 = Progressive | 0 | Not in the menu | Sets the television signal mode. Auto uses the console setting. |
| `FPS` | 0 = Off, 1 = On | 0 in the release build, 1 in the debug build | Video, Show FPS | Shows the frame rate on the screen. |
| `LimitFrames` | 0 = Off, 1 = Auto | 1 | Video, Limit FPS | Limits the speed to the speed of a real console. A controller button can change the limit during a game. That change is not saved. |
| `SkipFrames` | 0 = Off, 1 = On | 0 | Video, Frame Skip | Lets the renderer skip frames when the emulation is slow. |
| `Dithering` | 0 = None, 1 = Default, 2 = Always | 1 | Video, Dithering | Controls the dither pattern of the PlayStation graphics. Default applies dither only when the game requests it. |
| `MdecChroma` | 0 = Sharp, 1 = Smooth | 0 | Video, Advanced page, "MDEC Chroma" | How the video decoder gives each pixel its colour. The MDEC sends one colour sample for every 2x2 pixels. **Sharp** repeats that sample across the four, which is what the console does and what makes PS1 video look blocky. **Smooth** mixes the neighbouring samples, so the video looks better than it did on hardware. Measured on Medievil's intro: the colour conversion takes about 2.5 times as long, which was 1.3% of the run against 3.3%. Applies to the next video that starts. |
| `FmvColour` | 0 = 15-bit, 1 = 24-bit | 1 | Video, Advanced page, "Video Colour" | How a 24-bit picture (most full-motion video) reaches the screen. **24-bit** keeps all eight bits of each colour, as the console shows it (an RGBA8 texture). **15-bit** cuts each colour to five bits, which shows as bands in dark or smooth scenes; it was the only mode before 2026-09-22. Measured in Dolphin, the CPU side: +0.26% of wall in Micro Machines' intro video, +0.09% in Medievil's, and nothing outside 24-bit video. The graphics chip's side (an RGBA8 texture has twice the bytes to read) shows only on a Wii: `scripts/chains/fmv_ab.txt`. Applies at once. |
| `TVMode` | 0 = Off, 1 = On | 0 | Video, 240p | Sets the 240p original mode. The picture uses the original PlayStation resolution. Fixed 2026-09-23: with 240p saved in the settings, or on any game after the first, the picture was garbage (a purple screen, a cropped picture, or four narrow copies with interleaved lines) until the 240p button was pressed again. |
| `BilinearFilter` | 0 = Default, 1 = Near, 2 = Bilinear | 1 | Video, texture filter button | Selects the texture filter of the OpenGX renderer. Near keeps sharp pixels. Bilinear makes textures smooth. |
| `TrapFilter` | 0 = Off, 1 = On | 1 | Video, Trap | Enables the video trap filter of the Wii video interface. |
| `Interlaced` | 0 = Off, 1 = On | 0 | Video, Interlaced | Sets interlaced output. |
| `DeflickerFilter` | 0 = Off, 1 = On | 1 | Video, Deflicker | Enables the deflicker filter of the frame copy. |
| `ForceNTSC` | 0 = Off, 1 = On | 0 | Video, Force NTSC | Runs a PAL game at the NTSC timing of 60 Hz. |

## 4. Audio settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `Audio` | 0 = Off, 1 = On | 1 | Audio, Disable Audio | Enables the sound output. Note: the menu label is inverted. **Disable Audio = Yes** writes `Audio = 0`. |
| `Interpolation` | 1 = Simple, 2 = Gaussian | 2 | Audio, Interpolation | Selects the sample interpolation of the sound processor's voices. The value 0 is not permitted. Gaussian is the default since 2026-09: it is the better of the two and the whole sound processor costs under 1% of a frame. The menu control for this was labelled "Volume" in the source for years; there has never been a volume control. |
| `SoundResampler` | 0 = Hold, 1 = Linear, 2 = Cubic | 2 | Audio, Advanced, "Output Resampler" | How the mixed 44100 Hz stream is converted to the Wii's 48000 Hz output. `Hold` repeats the previous sample, which is what both output paths always did; `Linear` and `Cubic` (4-point Catmull-Rom) interpolate on the CPU instead, at a small cost per frame that the debug build reports as `out_us` in perf.log. `Cubic` is the default since 2026-09-21: measurably cleaner above 16 kHz than `Hold` with no measured cost in the sound system's under-1%-of-a-frame budget. Takes effect at once, also mid-game. |
| `DisableXa` | 0 = play XA, 1 = mute it | 0 | Audio, Disable XA | Mutes the disc's XA audio streams, which most games use for music and speech. Before this key existed the menu toggle worked for the session and was forgotten at the next boot. |
| `DisableCdda` | 0 = play CDDA, 1 = mute it | 0 | Audio, Disable CDDA | Mutes Red Book audio tracks, which is how many games store their music. Same note as above. |
| `SoundReverb` | 0 = Off, 1 = On | 1 | Audio, Advanced, "Reverb" | Turns the sound processor's reverb effect (hall, room, echo) on or off. Games that use it lose that effect entirely with this off; games that do not use it are unaffected either way. |
| `SoundMixerPrecision` | 0 = Legacy, 1 = Hi-Fi | 1 | Audio, Advanced, "Mixer Precision" | Rounds each internal volume scale-down to the nearest value instead of truncating it, in the 24-voice mixer and the final master-volume stage. Halves the average quantisation error at those steps; the mixed level and behaviour are otherwise identical, so this only ever reduces low-level noise. Hi-Fi is the default. |
| `SoundXaResampler` | 0 = Legacy, 1 = Hi-Fi | 1 | Audio, Advanced, "XA Resampler" | How CD-XA speech and streamed music (37800 Hz) is stepped up to 44100 Hz. Legacy is the nearest-sample or Gaussian step FeedXA always had. Hi-Fi replaces it with the sound processor's own real 7-phase, 29-tap filter (an exact 6-in/7-out ratio, no drift). Only 37800 Hz streams are affected; the rarer 18900 Hz streams always use the Legacy step. Hi-Fi is the default. |
| `SoundRateControl` | 0 = Off, 1 = On | 1 | Audio, Sync = Rate | Keeps the sound output's queue at its target by playing a fraction of a percent faster or slower (at most 0.5 %, and the change is gradual, so it is not heard as pitch), the way RetroArch and DuckStation do it. The sound processor emulation itself stays exactly on the emulated clock, so XA streams (speech and streamed music) never run ahead of the emulated disc. Both output paths use it: the CPU path scales its 44100 to 48000 Hz conversion, the DSP path moves the voice frequency. `0` turns it off, for measurements only: the queue then drifts at the difference between the emulator's frame limiter and the Wii's audio clock, and eventually starves or overflows. Stalls (a slow disc read from the SD card) are not this setting's job; the frame limiter lets the emulator catch up by up to 125 ms afterwards, which refills the queue. |
| `SoundTempo` | 0 = Off, 1 = On | 0 | Audio, Sync = Tempo | The older way of keeping the output fed, kept for comparison. When the output runs low, the sound processor emulation generates half a frame of extra audio to refill it. The cost: XA streams are then used up faster than the emulated disc delivers them and get short silent gaps, about one sector (53 ms) long, in scenes where the emulator is slow (measured: 61 gaps in 110 s of Spyro speech in a slow debug build, 2 in the release build). Every release before 2026-09-20 behaved this way. Leave it `0`; `SoundRateControl` replaces it. |

The menu shows the two as one three-way control, **Sync**: Off, Tempo or Rate. Selecting one turns the other off, and Off turns both off. The file can still hold both keys at `1`; both mechanisms then run together, which is harmless but pointless.

The menu also has the buttons **Disable XA** and **Disable CDDA**. WiiStation does not save these two buttons to the file. They return to Off at each start.

## 5. Save settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `NativeDevice` | 0 = SD, 1 = USB, 2 = Card A, 3 = Card B | 0 | Saves, Memcard Save Device | Selects where WiiStation keeps the memory card files. Card A and Card B are GameCube memory cards. |
| `StatesDevice` | 0 = SD, 1 = USB | 0 | Saves, Save States Device | Selects where WiiStation keeps the save states. |
| `AutoSave` | 0 = No, 1 = Yes | 1 | Saves, Auto Save Memcards | Writes the memory cards to the device when you exit a game. |
| `Memcard0`, `Memcard1` | 0 = Off, 1 = On | 1 | Saves, Memcard Type | Whether each memory card is there at all. The menu shows this and the file setting below as one button per card: **Off**, **Shared** or **Game**. |
| `Memcard0File`, `Memcard1File` | 0 = Shared, 1 = Game | 1 for card 1, 0 for card 2 | Saves, Memcard Type | Which file each memory card lives in. **Game** gives every disc its own card: card 1 is `<CdromId>.mcd` and card 2 is `<CdromId>-2.mcd`. **Shared** gives one card that every game opens: card 1 is `shared1.mcd` and card 2 is `slot2.mcd`. The defaults keep the names WiiStation always used, so no existing card is left behind. Shared is the only way one game can read another game's save, which a few games do. Applies when the next game is loaded, because the card in memory was read from the old file. |

## 6. Input settings

### 6.1 Controller settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `PadAutoAssign` | 0 = Manual, 1 = Automatic | 1 | Input, Configure Input | Lets WiiStation connect the controllers automatically. On a multitap, slot A to D takes GameCube pad 1 to 4 whenever that pad is connected, so each player keeps the same slot; the other ports and empty slots then take the remaining controllers (GameCube, then Wii, then HID). A port or slot with no controller stays empty and does not stop the ones after it. |
| `PadType1` to `PadType10` | 0 = None, 1 = GameCube Pad, 2 = Wii Pad, 3 = HID Pad, 4 = Multitap, 5 = Co-Op (`PadType1` and `PadType2` only, section 6.1.1) | 0 for `PadType1` and `PadType2`; 1 (GameCube Pad) for the multitap slots `PadType3` to `PadType10` | Input, Configure Input | Sets the controller type of each PlayStation port. See the port list below. A multitap's slots A to D start as GameCube pads 1 to 4; a port switched to Multitap whose four slots are all None gets the same. When a port's type changes while a game runs, the port shows as empty for half a second before the new controller appears, as when a real controller is unplugged and another plugged in: the game then looks for its controllers again. Without this, Crash Bash took no input from a multitap that had become a GameCube pad. |
| `PadAssign1` to `PadAssign10` | 0 to 3 | 0, except `PadAssign2` = 1 and the multitap slots: A to D are 0 to 3 (`PadAssign3`..`6` and `PadAssign7`..`10`) | Input, Configure Input | Sets which physical controller (1 to 4, written 0 to 3) drives each port. Values 4 to 9, which older versions accepted, are ignored. |
| `RumbleEnabled` | 0 = Off, 1 = On | 1 | Input, Disable Rumble | Enables the controller rumble. Note: the menu label is inverted. **Disable Rumble = Yes** writes `RumbleEnabled = 0`. |
| `ControllerType` | 0 = Standard, 1 = Analog, 2 = Stick D-pad | 0 | Input, PSX Controller Type | Sets the emulated PlayStation controller type. **Standard** is a digital pad (SCPH-1080): it answers the read command 42h and nothing else. **Analog** is a PS1 DualShock (SCPH-1200): it starts in digital mode, as the real pad does, and a game that supports analog switches it to analog mode itself with the config commands (Spyro, Ape Escape). Both behave as the real pads do, checked command by command with PadTest DX (Docs/CONTROLLER_TESTING.md). Games made for the older Dual Analog pad, which expect the player to press the ANALOG button, stay digital. **Stick D-pad** is the Standard digital pad, and the left stick also presses its D-pad: past half of full travel, the stick's direction presses Up, Down, Left or Right, or two of them on a diagonal (each of the eight directions takes 45 degrees). Use it for games that read only the D-pad, so they can be played with the stick. It applies to every port and multitap slot. |
| `LoadButtonSlot` | 0 to 3 = Slot 1 to Slot 4, 4 = Default | 4 | Input, Auto Load Slot | Loads a saved button mapping at start. |
| `LightGun` | 0 = Off, 1 = GunCon, 2 = Justifier, 3 = Mouse | 0 | Input, light gun button | Selects the emulated light gun or mouse. The Wii Remote pointer controls it. |
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
| `Coop1Layout1` ... `Coop2Layout8` | 0 = Full pad, 1 = Left half, 2 = Right half, 3 = D-pad+sticks, 4 = Action, 5 = Shoulders | 0 | The buttons and sticks the player works. |

### 6.2 Button mapping files

The button mappings are not in the settings file. WiiStation keeps them in the files `controlG.cfg`, `controlH.cfg`, `controlC.cfg`, `controlN.cfg`, `controlW.cfg`, `controlP.cfg`, and `controlD.cfg` in `wiisxrx/`. The Configure Buttons menu writes these files. Each one also holds that controller's analog sensitivity, below.

### 6.3 Analog stick sensitivity

Input, Configure Buttons, the `x1.0` control. It is per controller type and per port, adjustable from x0.2 to x2.0 in steps of 0.1, and it is saved in that controller's button-mapping file, not in the settings file.

**What it does.** Each driver already maps its own hardware's full travel onto the range a PlayStation pad reports -- 0 hard left or up, 255 hard right or down, 128 at rest. The sensitivity is one gain applied after that, about the centre:

    reported = 128 + (stick - 128) x sensitivity

So **x1.0 is a true 1:1 stick**: what the game reads is what the hardware measured, and the whole of the stick's travel is used. That is the default and it is what almost everyone should leave it on.

**Above 1.0** the stick reaches full deflection before the end of its physical travel, and the rest of the throw does nothing. It trades range for reach. Two reasons to want it: a worn stick whose springs no longer push it to the gate, and a game that wants full tilt to turn or accelerate where you would rather not push the stick all the way. At x2.0 the outer half of the travel is flat.

**Below 1.0** the stick can no longer reach either end: at x0.5 full deflection reports about a quarter and three quarters instead of 0 and 255. Games read that as a half-pressed stick, which is a way to walk instead of run, or to slow an over-eager camera.

**It does not change the dead zone and it does not change where the centre is.** Rest is 128 at every setting, so raising it never makes a stick drift; it makes an existing drift bigger, because the drift is multiplied too. It also does not affect the digital directions, which come from the D-pad or from a separate threshold on the stick.

Until 2026-09-21 this setting moved the light-gun and mouse pointer only and did nothing to the sticks. What the sticks had instead was a fixed x1.40625, on top of whatever gain the driver had already applied; a Classic Controller reached full deflection at 54 % of its travel and a GameCube pad at 72 %. That is gone -- the setting is the only gain now.

### 6.4 HID controller files

A USB HID controller needs a configuration file in the Nintendont format. The file name is the vendor ID and the product ID of the controller in hexadecimal, for example `0810_0003.ini`. Put the file in `sd:/wiisxrx/controllers/` or `usb:/wiisxrx/controllers/`. See `README.md` for the file format.

## 7. Network settings

WiiStation can read games from a Windows network share (SMB). These keys are text values. They are not in the menu.

| Key | Value | Default | Effect |
|---|---|---|---|
| `smbusername` | `"name"` | empty | The user name of the share. |
| `smbpassword` | `"password"` | empty | The password of the share. |
| `smbsharename` | `"share"` | empty | The name of the share. |
| `smbipaddr` | `"192.168.1.10"` | empty | The IP address of the server. |

WiiStation starts the network only when `smbsharename` and `smbipaddr` are both set.

**The password is not encrypted.** WiiStation writes it back to the settings file as
plain text when you save the settings from the menu. Use an account that can read the
share and nothing else.

One background thread does all of the network work. It brings the interface up, then it
connects to the share, and it waits five seconds between attempts. This takes some
seconds after the menu opens, so the first time you open **Load from Samba** you can get
*Still connecting to the share*. Try again.

The thread stops while a game runs, and starts again when you go back to the menu.

If the share goes away, the next directory you open fails and the thread makes a new
connection. You do not have to restart WiiStation.

| Message | What it means |
|---|---|
| `SMB is not configured` | `smbsharename` or `smbipaddr` is empty. |
| `Still connecting to the network` | The interface does not have an address yet. |
| `Still connecting to the share` | The interface is up and the share is being opened. |
| `Cannot connect to the share` | The address, the share name or the account is wrong. |

## 8. File browser settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `FileSortMode` | 0 = Folders and files mixed, 1 = Folders first | 1 | File browser, trigger button | Sets the sort order of the file list. |

## 9. Other files in `wiisxrx/`

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
| `saves/` | Memory card files. WiiStation creates this folder when it saves. |
| `controllers/` | HID controller files. See section 6.4. |
| `perf.log` | Performance counters. Only the debug build writes this file. |
| `vram.bin` | Video memory snapshot. Only the debug build writes this file. |

### 9.1 How to make a menu font

A menu font file holds one 24 x 24 pixel image for each character. The script `scripts/genfont.py` makes the file from a TrueType font. It needs Python 3 and the Pillow package (`python -m pip install --user pillow`).

1. Open a shell in the repository folder (`C:\projects\WiiStation`, not a subfolder) and run the script. This example makes a bold Calibri font with all Latin characters:

   ```
   python scripts/genfont.py --charset latin --ttf C:/Windows/Fonts/calibrib.ttf --out fonts/menu/CalibriBold.dat --preview preview.png
   ```

   The script selects the largest size that keeps the tallest and the deepest characters inside the 24 rows. It reports the size and the baseline. Open `preview.png` to check the result.
2. Copy the file to `sd:/wiisxrx/fonts/CalibriBold.dat`.
3. Add this line to `settingsRX2022.cfg`: `MenuFont = "CalibriBold"`.
4. Start WiiStation. To go back to the built-in font, remove the line.

Use `--charset ascii` for a smaller file with only the English characters. Use `--ref fonts/En.dat` instead of `--charset` to keep the character widths of an existing font file, so that the menu layout does not change.

## 10. Start a game without the menu

The file `sd:/wiisxrx/autoboot.txt` makes WiiStation start a game immediately. WiiStation reads this file only from the SD card. The file has exactly two lines:

```
sd:/wiisxrx/isos/Spyro the Dragon
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
600 sd:/wiisxrx/spyro_play.txt PadAutoAssign=1 ControllerType=0
sd:/wiisxrx/isos/Spyro the Dragon
Spyro the Dragon [NTSC-U] [SCUS-94228].cue
```

- Line 1 of a game: the number of emulated vblanks to run, then optionally an input script (section 11), then optionally `Key=Value` settings for this game only.
- Lines 2 and 3: the folder and the file name, as in `autoboot.txt`. A `.exe` program can take the place of a disc image.
- Lines that start with `#` are comments.

Each game writes its results to `sd:/wiisxrx/perf.log` (debug builds). When WiiStation stops because of an error, the last line of `perf.log` is `fatal: code=... <reason>`. The chain files used for testing are in `scripts/chains/`.

## 11. Scripted controller input

The file `sd:/wiisxrx/autoinput.txt` presses controller buttons at given times. It is a test aid. It works with `autoboot.txt`: a boot with both files is deterministic up to any screen. WiiStation reads the file when the controller plugin opens. A missing file has no effect. Real controllers keep working. The script only adds presses.

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
`sd:/wiisxrx/padtrace.csv`; `scripts/padtest.py check` reads it. See
`Docs/CONTROLLER_TESTING.md`.

`padsweep <vblank> fast` is a short sweep (214 vblanks instead of 996): each button held 3
vblanks, the two X axes walked together in steps of 3, then the two Y axes.

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
| 2 | Settings, Video | 7 | Plugins |
| 3 | Settings, Input | 8 | CD |
| 4 | Settings, Audio | 9 | Memory |
| 5 | Settings, Saves | 20 | Configure Input |
| | | 21 to 36 | Co-Op Customize, port 1 players 1 to 8 (21 to 28), port 2 players 1 to 8 (29 to 36) |

Use it with no `autoboot.txt`, so the run stays in the menu, and collect the frames the
run dumps. `scripts/menu_text_width.py` checks the same pages' geometry without booting
anything, which is faster but is not the same as looking.

## 12. Known differences between the code and the menu

- `BiosDevice`, `BilinearFilter`, `TrapFilter`, `Interlaced`, `DeflickerFilter`, and `LightGun` get their default from the variable initialiser. The function `loadSettings()` does not set them.
- `Interpolation` and `Dithering` start at 0 in the variable initialiser. The function `loadSettings()` then sets them to 2 and 1 respectively before it reads the file.
- `ControllerType = 2` is Stick D-pad since 5.3.0. Before, the value was an unused "light gun" entry that the file parser rejected; the light gun is the `LightGun` key.
- The keys `Debug` and `NumberMultitaps` are commented out in the code. WiiStation does not read or write them.
