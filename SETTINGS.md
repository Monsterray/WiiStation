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
| `CdBuffer` | 0 = 16 KB, 1 = 64 KB, 2 = 256 KB | 0 | General, Storage page, "CD Read Buffer" | Size of the read buffer each disc-image file handle gets (the main image, the sub-channel file, the CDDA handle, every file of a multi-file cue). A bigger buffer means fewer and longer SD/USB transactions while a game streams from disc, and more wasted read-ahead when it jumps around. Applies when the next game is loaded. Before this setting existed there was one 16 KB buffer, on the main image only. |
| `CdPrefetch` | 0 = Off, 1 = On | 0 | General, Storage page, "CD Read-Ahead" | Runs a thread that keeps the next 31 sectors of a raw image (bin/cue, .iso) read in advance, so the emulated CPU no longer stops while the card delivers a sector. Compressed images (CHD, PBP, .Z) are not affected. Only real hardware can show the difference, since Dolphin's SD card never stalls; the debug build reports hits and misses on perf.log's `cdpf:` line. Applies when the next game is loaded. |
| `CdChdHunks` | 0 = 2, 1 = 4, 2 = 8 | 0 | General, Storage page, "CHD Hunk Cache" | How many decoded CHD hunks stay in memory (about 20 KB each). Games that alternate between two places on the disc, such as streamed music beside level data, decode less often with 4 or 8. Applies when the next game is loaded. |

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
| `TVMode` | 0 = Off, 1 = On | 0 | Video, 240p | Sets the 240p original mode. The picture uses the original PlayStation resolution. |
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
| `Memcard0` | 0 = Off, 1 = On | 1 | Saves, Memcard 1 | Connects memory card 1 to the console. |
| `Memcard1` | 0 = Off, 1 = On | 1 | Saves, Memcard 2 | Connects memory card 2 to the console. |

## 6. Input settings

### 6.1 Controller settings

| Key | Values | Default | Menu | Effect |
|---|---|---|---|---|
| `PadAutoAssign` | 0 = Manual, 1 = Automatic | 1 | Input, Configure Input | Lets WiiStation connect the controllers automatically. |
| `PadType1` to `PadType10` | 0 = None, 1 = GameCube Pad, 2 = Wii Pad, 3 = HID Pad, 4 = Multitap | 0 | Input, Configure Input | Sets the controller type of each PlayStation port. See the port list below. |
| `PadAssign1` to `PadAssign10` | 0 to 9 | 0, except `PadAssign2` = 1 | Input, Configure Input | Sets which physical controller drives each port. The menu can select only the values 0 to 3. |
| `RumbleEnabled` | 0 = Off, 1 = On | 1 | Input, Disable Rumble | Enables the controller rumble. Note: the menu label is inverted. **Disable Rumble = Yes** writes `RumbleEnabled = 0`. |
| `ControllerType` | 0 = Standard, 1 = Analog | 0 | Input, PSX Controller Type | Sets the emulated PlayStation controller type. |
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

## 12. Known differences between the code and the menu

- `BiosDevice`, `BilinearFilter`, `TrapFilter`, `Interlaced`, `DeflickerFilter`, and `LightGun` get their default from the variable initialiser. The function `loadSettings()` does not set them.
- `Interpolation` and `Dithering` start at 0 in the variable initialiser. The function `loadSettings()` then sets them to 2 and 1 respectively before it reads the file.
- The value `ControllerType = 2` (light gun) exists in the code. The file parser rejects it. Use `LightGun` instead.
- The keys `Debug` and `NumberMultitaps` are commented out in the code. WiiStation does not read or write them.
