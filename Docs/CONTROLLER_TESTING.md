# Testing the controller subsystem

Written 2026-09-21. Two tools, because the input path has two halves that fail for
different reasons and neither tool can see the other's half.

## 1. The maths, on the host

`tests/psx_analog_test.c` compiles with any host compiler and runs every possible input
through the conversion the Wii runs, because `Gamecube/gc_input/psx_analog.h` is plain C
with no libogc in it.

```
cc -O2 -o psx_analog_test tests/psx_analog_test.c -lm && ./psx_analog_test
```

For each controller it asserts that rest reads exactly 128, that both 0 and 255 are
reachable, that the curve never steps backwards, and that no long run of raw positions
produces the same output. That last one is what catches the interesting failure: a stick
whose travel is partly wasted still reaches both ends, it just gets there too soon.

What it looked like before the stick path was fixed:

| | rest | full deflection at |
|---|---|---|
| GameCube | 128 | 72 % of travel |
| Classic Controller | 128 | 54 % of travel |
| Wiimote + Nunchuk | **126** | 72 % of travel |

Two gains were being applied one after the other: each driver's own scaling, then a fixed
x1.40625 in the pad plugin. The Nunchuk also centred on 127 rather than 128, so it drifted
at rest. All three now read 128 at rest and use their whole travel.

The numbers each driver scales by are repeated in the test rather than included from the
drivers, which need libogc. A driver that changes its scaling without changing the test
will not be caught, so the test prints the ranges it used.

## 2. The live path, in a running game

The maths being right does not prove a press arrives. There are two moving parts:

**A scripted pad.** `padsweep <vblank>` in `sd:/wiistation/autoinput.txt` makes the GameCube
driver read a generated sweep instead of the pad from that vblank on: each stick axis
walked end to end a step per vblank, then each button held on its own. The substitution
happens at the very top of `controller-GC.c`, where the raw libogc reading is taken, so
everything below it -- that driver's own conversion, the pad plugin, the sensitivity gain,
the PlayStation packing -- runs on it exactly as on a real pad. It repeats, so a run
started before the game has finished loading still catches a whole sweep. Being inside the
emulator, it works on real hardware as well as under Dolphin.

**A pad timeline.** In a debug build `perf_pad_event` (`Gamecube/perf_prof.c`) records one
line each time what a virtual port hands the PlayStation changes, with the driver's own
output beside the final bytes, and writes `sd:/wiistation/padtrace.csv` on every perf report.
Repeats are dropped, so a stick held still costs one line. Release builds compile it out.

Running it, with any game that polls the pad:

```
printf 'padsweep 1200\n' > <sync>/wiistation/autoinput.txt
DOLPHIN_ARGS="-C Dolphin.Core.SIDevice0=6" scripts/dolphin_run.sh out 160 \
    <that autoinput file> <settings with PadType1=1> <an autoboot file>
python scripts/sdimage_read.py <User>/Load/WiiSD.raw wiistation/padtrace.csv out/padtrace.csv
python scripts/padtest.py check out/padtrace.csv
```

`-C Dolphin.Core.SIDevice0=6` is needed: SIDEVICE_GC_CONTROLLER. Without a GameCube
controller on the port, `PAD_ScanPads` reports nothing connected, no driver is assigned to
the virtual port, and the trace holds one line saying so (`type` of `-`).

Since 2026-10-12 (c6e0f59) the test profiles bind no host device to Dolphin's GameCube pads,
and Dolphin then reports the pad as not connected, SIDevice0 or not: every Dolphin run showed
`carry: ... gc=0` and the PadTest DX matrix passed only the 3 cells without GameCube pad 1
(measured 2026-10-19, with the 5.14.0 DOL too). Now a script that drives pad 1 (`padsweep`
or `gcpress`) makes it count as plugged in (`autoinput_gcpad`, `GC_SCAN` in controller-GC.c):
the matrix is 12/12 again.

**Host button combinations.** `gcpress <vblank> <hex>` holds raw GameCube buttons (libogc
`PAD_BUTTON_*`/`PAD_TRIGGER_*`, e.g. `1060` = L+R+Start) on pad 1 for 10 vblanks, on top of
the pad, up to 8 lines. It goes through the driver, so it presses what the PSX press lines
cannot: the ANALOG button, the screenshot. `scripts/autoinput/medievil_analog_combo.txt`
presses ANALOG in MediEvil at vblank 6800; tty.log then has `analog vbl=6800 Port 1: Analog
(red light)` and the pad lines go from `id=41` to `id=73` (Docs/ANALOG_BUTTON.md).

`padtest.py check` reports, per port, how much of 0..255 each axis covered and how many
buttons moved a bit of their own. It deliberately does not know which bit belongs to which
button: it asserts only that each press moves exactly one bit and that every press moves a
different one, which tests that the mapping is complete and one-to-one without hard-coding
the bit order, the active-low sense or the byte swap.

Two numbers in it are worth knowing:

- **193, not 256.** A GameCube stick has 193 usable positions (-96..96), so a perfect
  sweep of one produces 193 distinct PlayStation values. The check is that the values are
  finely graded and reach both ends, not that all 256 appear.
- **14, not 16.** The default GameCube mapping reaches fourteen of the PlayStation's
  sixteen buttons. L3 and R3 are "None" by default and no press can produce them.

A real result, Ape Escape under Dolphin, which is a good test because the game refuses to
start without an analog pad and uses both sticks throughout:

```
port 0: 2047 records, driver 'G'
  lx     0..255    193 distinct
  ly     0..255    193 distinct
  rx     0..255    193 distinct
  ry     0..255    193 distinct
buttons: rest word ffff, 14 distinct single-bit presses seen
pad path intact
```

Every one of the 193 raw stick steps produced a different value at the other end: no part
of the travel does nothing.

## 3. A test disc to look at

The trace says what the emulator handed the game. A PlayStation program written to display
the pad says what a program actually receives, on screen, which is the check a person can
make in two seconds. Two exist and both run in WiiStation:

**PadTest 1.1** by Shendo and ggrtk -- https://github.com/ShendoXT/padtest, a bin/cue in
the release zip. **This is the one to use.** It shows both ports side by side, whether each
is in digital or analog mode, every button, and each stick twice over: as a crosshair in a
circle and as numbers. Running it with `padsweep` gives a frame dump in which the crosshair
walks the circle and the numbers run the range, which is the whole path confirmed by eye.
Its README says it uses direct SIO access and "may not work on emulators" -- it works here,
because that SIO protocol is exactly what `SSS_PADpoll` implements.

**PadTest DX** -- the same program, forked to github.com/Monsterray/padtest and cloned to
`../padtest`, ported to PSn00bSDK (installed at `$PSN00BSDK`) and extended for
this work. Build it with `bash build.sh` in that folder. Since 2.4.0 (2026-10-02) it reads
both ports as a game does, multitaps included: per frame and port a request (TAP byte 01h),
the long read of all four slots, and a single-slot read of each other slot (01h..04h);
every rule and its source is in the fork's `docs/PROTOCOL.md`. On screen: a pad on its own
port as before (raw reply, `cfg n/16`, `btn n`, `axes`), a multitap as four slot blocks and
a `probe` line. Each frame it copies a status block to VRAM (640,256), 64x32 halfwords,
layout in the fork's `include/dx.h` (version 4). One command runs the **controller
matrix** -- a dozen setups in one Dolphin boot -- and prints one line per setup:

```
bash scripts/padtest_dx.sh               # about two minutes; -v for what each cell received
bash scripts/padtest_dx.sh --cells taps_ct1,switch_ct1   # some cells; --frames for pictures
```

`python scripts/padtest_dx.py cells` lists the cells: ports None / GameCube pad / Multitap,
slots filled or empty, ControllerType 0/1/2, a multitap -> pad -> multitap switch while the
ROM runs (`padtype` lines), and two cells with `SioTiming=1` that also check the byte and
/ACK times against hardware. GameCube pad 1 (the only one in Dolphin's test profile) plays
port 1 or slot 1A through the real driver (`padsweep 1 fast`); every other port and slot
gets the input script's `sweep`, which presses the buttons in an order of its own, so a
controller that turns up in the wrong slot fails its cell. `python scripts/padtest_dx.py
show VRAM.BIN` prints one status block. The ROM goes on the test profile's card
(`.dolphin`) only. Use `vram_NN.bin` for the block, not for pictures: the hardware GPU draws
on the GX side, so the dump holds uploads, not the rendered screen.

What it found on 2026-09-23, against psx-spx, DuckStation, MiSTer's PSX core and PsxNewLib,
and what was changed so that both pad types act as the real pads do:
- Analog had DualShock 2 replies: 45h type 03h (a PS1 DualShock says 01h) and 4Fh switching
  to ID 79h. It answered config commands outside config mode, 42h in config mode gave the
  normal ID instead of F3h, and config-mode 43h returned the buttons. 44h with an LED value
  of 2 or more indexed past a table. 46h, 47h and 4Ch ignored their parameter; 48h was missing.
  4Dh did not return the old map, and a motor mapped late grew the reply (ID 42h, which no pad
  has). The small motor ran on any value, not bit 0, and the one-motor method did not work.
- Standard answered every command as a read, and `sio.c` changed the ID to 43h for 43h and
  to F3h for 45h. A real digital pad answers 42h only; anything else gets no /ACK.
- Now, 16/16 config checks pass for each type, the sweep passes, and the replies match
  (the matrix's `pads_ct1` and `pad_ct0` cells).
- /ACK comes after each byte but the last, as on hardware, 15.7 us after the byte: exactly
  `SIO_CYCLES` (535 cycles) in `sio.c`, which the ROM's timer confirms.
- A byte takes 0.5 us: the reply is ready as soon as the byte is written. On hardware 8 bits
  at 250 kHz take 32 us, so a whole poll here runs about twice as fast as on a console.
  Nothing found so far depends on it. `SioTiming = 1` (2026-10-02) times controller bytes
  as a PS1 does: the ROM then measures 33.0 us a byte and 13.0..13.6 us to /ACK.

What the matrix found on 2026-10-02 (the old code passed 0 of 10 cells; now 12 of 12):
- A plain pad answered addresses 02h..04h as if four pads were there, and an empty multitap
  slot answered single-slot reads as a pad. Both now get no /ACK, as on a PS1.
- In a long read, an empty slot's stick bytes and a digital pad's last four bytes held stick
  values (`80 80 80 80`, or another slot's) instead of FFh: the slots shared one stick
  buffer (`lastport1`). Each controller now has its own transfer state and output.
- The long read was a canned reply, not the four controllers clocked at once: no cut-short
  read after refused slot blocks, a command other than 42h taken as a read, a multitap with
  every slot empty still answering, and the request accepted from an empty slot.
  Gamecube/PadSSSPSX.c now models the multitap as Mednafen does from hardware tests.
- The HLE BIOS returned from a `syscall` at an address ending in FCh 256 bytes short (a
  byte-swapped add on the big-endian Wii). PadTest DX 2.4.0 hung in ResetGraph under the HLE
  BIOS (main's build too); any program can hit it. Fixed in psxbios.c.
- A memory card answered addresses 82h..84h (multitap slots B..D); now 81h only.

**PSXTEST 2.3** by Haunted360 -- a general console tester (three songs for the speakers, a
dead-pixel checker, a pad tester). It runs too, and the trace under it is identical, but its
pad display is one line of digital button names on a menu screen with no analog readout, so
it says much less. `psxdev.net` no longer serves it; the Wayback Machine does, at
`web.archive.org/web/2018/http://psxdev.net/homebrew/files/psxtest.zip`.

PadTest 1.1 and PSXTEST are third-party binaries and not in this repository. Put the bin/cue in
`wiistation/isos/<name>/` on the SD card and point an autoboot file at it.

## 4. Things that did not work, so nobody tries them again

**Dolphin input movies (.dtm).** The obvious way to script a pad is to hand Dolphin a
movie with `-m`. `scripts/padtest.py make` still writes one, and it is well formed -- it
round-trips through this repo's own `scripts/dtm2autoinput.py`, which parses 2188 polls and
recognises every button press with the right mapping. The installed Dolphin
(`$DOLPHIN`) has the `-m` option in its binary and accepts it without
complaint, but `Movie::PlayController` never runs: a deliberately corrupt movie does not
produce the "Invalid recording file" it should, and a 100-frame movie does not produce
"Premature movie end", with `Logger.Logs.MASTER_LOG=True` and verbosity 5. So the movie is
not being loaded at all in this build, in batch mode or out of it. That is why the sweep
lives inside the emulator instead. The generator is kept because it costs nothing and
would be the better tool if a Dolphin build is found where playback works -- it is the only
way to test the path *above* the driver, including whether libogc's own SI handling is
right.

## 5. What neither tool covers

- **Two controller families at once.** The bug that motivated much of this -- one
  `wpadNeedScan` flag guarding three different hardware polls, so whichever driver ran
  first left the others unscanned -- needs a Classic Controller and a Wii U Pro Controller
  plugged in together. It was found by reading, not by testing.
- **The Wiimote, Classic, Wii U Pro and GamePad drivers' own readings.** The sweep
  substitutes a GameCube pad only; the other drivers' conversions are covered by the host
  test but their live paths are not.
- **Anything about real controller hardware**: worn sticks, dead zones in practice,
  Bluetooth latency, how much travel a Classic Controller really has.
- **Rumble**, which has no observable output in a trace.
- **The menus.** Nothing here drives the menu, which reads the pads directly rather than
  through these drivers. `scripts/menu_text_width.py` checks the Settings tabs' geometry
  and text widths against the 640-pixel screen instead, which is not the same as looking.

## 6. USB controllers (HID pads): the USB Pad Test page

Added 2026-10-18. A USB controller is read through its controller ini
(`<device>:/wiistation/controllers/VVVV_PPPP.ini`, VID and PID in hex) when it has one, and
through its own HID report descriptor when it has none (section 7). When a pad does nothing,
or the wrong thing, this page shows why, and writes an ini for it. It needs a real Wii: Dolphin has no USB HID pads (in Dolphin the page runs and
logs "devices: 0").

**Running it.**

1. Plug the USB controller into the Wii. A gamepad with no ini is read by its report
   descriptor (section 7). A device whose descriptor names no gamepad input, and has no
   ini: open the page first, then unplug it and plug it in again (the page says so); only
   while the page is open is it read at all ("raw"), and a game never gets it.
2. Settings > General > **Advanced** > **Input** tab > USB Pad: **Test**. B on the page
   goes back there. A scripted run opens the page with the autoinput line
   `menuclick 83 1` (Settings button 83, kept on no tab for this; B then goes back to
   Settings > Input); `menupage 12` photographs the Advanced > Input tab.
3. Put the controller down flat and let go: the first second learns what the pad sends at
   rest. Then the page lights one PlayStation button at a time on the pad picture: the
   d-pad, the four face buttons, L1 R1 L2 R2, Select, Start, L3, R3, then each stick up,
   down, left and right, then "press nothing" for 3 seconds.
4. Press the lit button and let go. A button the pad does not have: press A on the Wii
   Remote or GameCube pad to skip it, or wait 10 seconds. B stops the test. At the end, A
   runs it again and B goes back to Settings.
5. Take `sd:/wiistation/usbpad.log` (or `usb:` when there is no SD card) to the PC and run
   `python scripts/usbpad_read.py usbpad.log`.

**What the log holds.** One line per fact, so it can be read by eye too:

| line | what |
|---|---|
| `hid:` | `running`, or `not-running` (no IOS58 USB HID: no USB pad can work) |
| `devices:` | every USB HID device the Wii lists, VID:PID |
| `device:` | the one picked as the controller: VID, PID, product name, interface class/subclass/protocol, endpoints, packet size |
| `ini:` | the ini path on SD and USB, `found` or `missing` |
| `mode:` | `mapped` (decoded as in a game), `raw` (no ini and no gamepad in the descriptor: bytes only), `not-read` |
| `layout:` | `ini` (an old-style ini), `auto` (no ini, or a new-style one: section 7), `none` (raw) |
| `params:` | the USB descriptors the Wii's HID driver gave (device, configuration, interface, endpoints), hex |
| `reportdesc:` | the HID report descriptor, hex, when the pad answered (the first 512 bytes) |
| `hidinputs:` | what WiiStation found in it: the count, report ID, report length, highest button number, hats, axes |
| `rest:`, `noise:` | the report at rest; the bytes that moved at rest (sticks, motion sensors) and their range |
| `step N Name: result` | `pressed` (with `raw=` the whole report held down, `changed=` byte:rest>pressed for each byte that moved, `hid=` the HID input that moved in the ini's words (`Button2`, `HatUp`, `AxisZ+`; for a stick step the axis, `AxisRz`), then `gc=`, `ps1=`, `buttons=`, `sticks=`, `triggers=`: what WiiStation decoded), `timeout`, `skipped`, `no-controller`; for the last step `rest-ok` or `moved` |
| `suggest:` | after a full run: a new-style controller ini made from the `hid=` answers (section 7) |
| `end:` | `done` or `stopped` |

**Reading the table.** `usbpad_read.py` prints the header, then one row per step with a
verdict: OK; WRONG (another PlayStation button came out: the ini, or the HID button
config, maps it elsewhere); NOT MAPPED (the report changed but WiiStation saw nothing: the
ini has no key for that byte and bit); NO INI (the pad was read raw); DRIFT (the pad moved
with nothing pressed: stick drift or a noisy byte). The `ini` column is a first guess at
the ini line (offset and mask in hex, as the ini takes them) for that button, from the bits
that turned on. A d-pad sent as a hat switch (one byte, 0..7, 8 or 0x0F at rest) needs
`DPAD=1` and the values as masks; check the `changed` column before writing it in. The `hid`
column is the same press in the new-style ini's words.

**The suggested ini.** The page shows the HID input each press moved ("Cross: pressed
Button2 ...") and, after a full run, writes a complete new-style ini into the log as
`suggest:` lines. `usbpad_read.py` prints it under the table; `usbpad_read.py --ini
usbpad.log > 046D_C216.ini` writes only the ini, ready for `<device>:/wiistation/controllers/`.
For a log from before these lines (no `hid=`), it builds an old-style ini from the `ini`
column instead (a hat d-pad: `DPAD=1` and the eight values).

The page reads the same report buffer the games read (`HidController/KernelHID.c`,
`hidProbe`), so a pad that is NOT MAPPED here is not mapped in a game either. Its own code
is `Gamecube/menu/UsbPadTestFrame.cpp`. A Dolphin run collects the log as
`.runs/NAME/usbpad.log` (`scripts/dolphin_run.sh`).

## 7. Every input of a USB pad: the report descriptor, the automatic layout, the ini

Added 2026-10-19 (`HidController/hid_layout.c`, plain C). When a USB pad is plugged in,
WiiStation reads its HID report descriptor (the first 512 bytes) and finds every gamepad
input in it: buttons 1-32 (usage page Button), up to two hat switches, and the axes X, Y, Z,
Rx, Ry, Rz, Slider, Dial, Brake and Accelerator, each with its report ID, bit position, bit
size and logical range. Only inputs inside a Joystick, Gamepad or Multi-axis application
collection count (a keyboard or a vendor device is not taken for a pad), from the first
report ID that has one. A descriptor cut off part-way keeps the inputs described before the
cut. A logical maximum written `25 FF` (strictly -1) is read as 255, as every OS does.

**Which ini wins.**

| the pad has | it is read |
|---|---|
| an old-style ini (every shipped one) | exactly as before. New: its `KeyL3=` / `KeyR3=` lines (`byte,mask`), and Configure Buttons' Btn/Hat/Axis entries work on top of it |
| no ini | by the automatic layout below |
| a new-style ini: `Auto=1`, or any value `Button..`, `Hat..`, `Axis..` | the automatic layout, then each `Key...`/`Stick...` line of the ini replaces that one role |

A pad whose descriptor names no gamepad input and that has no ini is not read (only raw, in
the USB Pad Test page). A new-style ini on such a pad is read the old way.

**The automatic layout** (standard DirectInput numbering, which a DualShock 4, a Logitech
Dual Action and most generic pads use; PlayStation names under WiiStation's default HID
button config, Square = KeyA):

- Buttons 1-4: Square, Cross, Circle, Triangle (KeyA KeyB KeyX KeyY).
- 10 or more buttons: 5-8 L1 R1 L2 R2, 9 Select, 10 Start, 11 L3, 12 R3.
  5 to 9 buttons: Select and Start are the last two, the shoulders (L1 R1 L2 R2) the ones
  between (an 8-button SNES-style pad: 5 L1, 6 R1, 7 Select, 8 Start).
- Brake and Accelerator axes (Xbox-style analog triggers): L2 and R2, pushed past half.
- Hat 1: the D-pad. No hat: the X/Y axes are the D-pad too (an SNES-style pad sends its
  D-pad as X/Y).
- Left stick X/Y. Right stick Z/Rz; Rx/Ry when the pad has no Z/Rz pair, or is Microsoft's
  (VID 045E with Rx/Ry: the older Xbox layout, Z/Rz there are the triggers).
- A report ID: only reports with it are read (`MultiIn=1`); a DualShock 3 (054C:0268) with
  no ini is polled the way its ini says (`Polltype=0`).

Guesses that are wrong for known pads, all of which ship an ini: a DualShock 3 numbers its
buttons Select L3 R3 Start, D-pad, L2 R2 L1 R1, Triangle Circle Cross Square; an Xbox pad
over Bluetooth numbers A B (gap) X Y. Write a new-style ini for such a pad (the USB Pad Test
writes one).

**The ini syntax.** Keys: `KeyA KeyB KeyX KeyY KeyL1 KeyR1 KeyL2 KeyR2 KeySelect KeyStart
KeyL3 KeyR3 KeyUp KeyDown KeyLeft KeyRight` and `StickX StickY CStickX CStickY` (left and
right stick). Values:

| value | for | means |
|---|---|---|
| `Button13` | a key | button 13 (1-32) |
| `HatUp`, `HatRight`, `HatDown`, `HatLeft`; `Hat2Up` ... | a key | a hat direction (a diagonal holds both) |
| `AxisZ+`, `AxisRz-`, `AxisBrake+` ... | a key | an axis pushed past half its travel, that way (`-` left/up) |
| `5,40` | a key | report byte 5, mask 0x40 (hex, as in the old ini) |
| `AxisRz` | a stick | that axis (names: X Y Z Rx Ry Rz Slider Dial Brake Accel) |
| `6,0,0` | a stick | report byte 6 read as 0..255 with 128 at rest (the old form) |
| `None` | either | nothing |

A value it cannot read leaves the automatic layout's choice. In a new-style ini a hat d-pad is
`KeyUp=HatUp`, not the old `DPAD=1` value form. `Polltype` defaults to 1 there, and the
report ID filter is set from the descriptor unless the ini has `MultiIn=`. `Rumble...` lines
work as before. Example, a Dual Action by its descriptor, with Select and Start swapped:

```
[Logitech Dual Action For WiiStation]
VID=046D
PID=C216
Auto=1
KeySelect=Button10
KeyStart=Button9
```

**In Configure Buttons** every USB pad has, after the old entries (D-Up ... A-Down), `L3` and
`R3` (the ini's or the layout's), then every HID input by name: `Btn 1` ... `Btn 32`, `Hat Up`
... `Hat2 Left`, `Axis X-` ... `Axis Accel+`. Entries for inputs the pad does not have are
skipped. The menu, fast-forward, screenshot and ANALOG combinations can be any one HID input
alone too (a Home button for the menu). The analog sources add `Axes X/Y`, `Axes Z/Rz`,
`Axes Rx/Ry` and `Slider/Dial`. The A-/C- stick-direction entries now work (they were never
set before). A HID axis reaches the PlayStation stick through one mapping: its logical range
to -1..+1, `psx_square`, `psx_analog` (no GameCube scaling on the way).

**Host test.** `tests/hid_layout_test.c` runs every button, every hat value and every value of
every axis of five descriptors (the user's Dual Action as logged, cut off; a DualShock 3; a
DualShock 4 cut at 512 bytes; an Xbox-style pad with 16-bit sticks, 10-bit Brake/Accelerator
and a 1-based hat; an SNES-style pad with `25 FF`) through `hid_decode` and the automatic
layout, the ini syntax for all 60 inputs, every cut-off length, 20000 junk descriptors, and
that no shipped ini reads as new-style:

```
clang -O2 -Wall -o hid_layout_test tests/hid_layout_test.c HidController/hid_layout.c
./hid_layout_test Gamecube/release/controllers/[0-9A-F]*.ini
```

It needs a real Wii for the rest: Dolphin has no USB HID pads.
