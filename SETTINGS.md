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
| `Core` | 0 = Lightrec, 1 = Interpreter, 2 = Dynarec | 0 | General, Select CPU Core | Selects the CPU emulation core. A change resets the current game. |
| `BiosDevice` | 0 = HLE, 1 = SD, 2 = USB | 0 | General, Select Bios | Selects the BIOS source. HLE is the built-in BIOS emulation. The file `SCPH1001.BIN` must exist in `wiisxrx/bios/` on the device. If the file does not exist, WiiStation uses HLE. The DVD option in the menu is not implemented. |
| `BootThruBios` | 0 = No, 1 = Yes | 0 | General, Boot Through Bios | Shows the PlayStation start screen before the game starts. |
| `gpuPlugin` | 0 = Old Soft, 1 = New Soft, 2 = OpenGX | 0 | General, GPU Plugin | Selects the graphics renderer. Old Soft and New Soft draw with the CPU. OpenGX draws with the Wii graphics hardware. A change resets the current game. |
| `lang` | 0 to 12, see the language table below | 0 | General, Select language | Selects the menu language. The language files are in `wiisxrx/lang/`. The menu does not show a language if its font file is missing. |
| `MenuFont` | `"Name"` | empty | none, settings file only | Selects the menu font. WiiStation loads `wiisxrx/fonts/Name.dat` from the SD card or the USB device. An empty value or a missing file gives the built-in font. The value does not apply to the Chinese, Japanese and Korean languages, which have their own glyph files. See section 9 for the font files. |
| `fastLoad` | 0 = No, 1 = Yes | 0 | General, Fast Load | Makes CD reads faster than a real console. Some games do not work correctly with this setting. |

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
| `Interpolation` | 1 = Simple, 2 = Gaussian | 1 | Audio, Interpolation | Selects the sample interpolation of the sound processor. The value 0 is not permitted. |

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

The button mappings are not in the settings file. WiiStation keeps them in the files `controlG.cfg`, `controlH.cfg`, `controlC.cfg`, `controlN.cfg`, `controlW.cfg`, `controlP.cfg`, and `controlD.cfg` in `wiisxrx/`. The Configure Buttons menu writes these files.

### 6.3 HID controller files

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
| `fonts/<Name>.dat` | A menu font, selected with `MenuFont`. The repository has three in `fonts/menu/`: `Classic` (the original glyphs, descenders cut), `Segoe` (the built-in face with full descenders and Latin accents) and `Times` (a Times New Roman face). Make a new one with `scripts/genfont.py`. |
| `ppf/` | PPF patch files. WiiStation applies a patch with the same name as the game. |
| `lang/` | Menu language files. |
| `fonts/chs.dat` | Font file for the Chinese menu language. |
| `settings/` | Settings files for one game. See section 1.4. |
| `saves/` | Memory card files. WiiStation creates this folder when it saves. |
| `controllers/` | HID controller files. See section 6.3. |
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

## 12. Known differences between the code and the menu

- `BiosDevice`, `BilinearFilter`, `TrapFilter`, `Interlaced`, `DeflickerFilter`, and `LightGun` get their default from the variable initialiser. The function `loadSettings()` does not set them.
- `Interpolation` and `Dithering` start at 0 in the variable initialiser. The function `loadSettings()` then sets them to 1 before it reads the file.
- The value `ControllerType = 2` (light gun) exists in the code. The file parser rejects it. Use `LightGun` instead.
- The keys `Debug` and `NumberMultitaps` are commented out in the code. WiiStation does not read or write them.
