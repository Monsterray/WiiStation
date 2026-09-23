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

**A scripted pad.** `padsweep <vblank>` in `sd:/wiisxrx/autoinput.txt` makes the GameCube
driver read a generated sweep instead of the pad from that vblank on: each stick axis
walked end to end a step per vblank, then each button held on its own. The substitution
happens at the very top of `controller-GC.c`, where the raw libogc reading is taken, so
everything below it -- that driver's own conversion, the pad plugin, the sensitivity gain,
the PlayStation packing -- runs on it exactly as on a real pad. It repeats, so a run
started before the game has finished loading still catches a whole sweep. Being inside the
emulator, it works on real hardware as well as under Dolphin.

**A pad timeline.** In a debug build `perf_pad_event` (`Gamecube/perf_prof.c`) records one
line each time what a virtual port hands the PlayStation changes, with the driver's own
output beside the final bytes, and writes `sd:/wiisxrx/padtrace.csv` on every perf report.
Repeats are dropped, so a stick held still costs one line. Release builds compile it out.

Running it, with any game that polls the pad:

```
printf 'padsweep 1200\n' > <sync>/wiisxrx/autoinput.txt
DOLPHIN_ARGS="-C Dolphin.Core.SIDevice0=6" scripts/dolphin_run.sh out 160 \
    <that autoinput file> <settings with PadType1=1> <an autoboot file>
python scripts/sdimage_read.py <User>/Load/WiiSD.raw wiisxrx/padtrace.csv out/padtrace.csv
python scripts/padtest.py check out/padtrace.csv
```

`-C Dolphin.Core.SIDevice0=6` is needed: SIDEVICE_GC_CONTROLLER. Without a GameCube
controller on the port, `PAD_ScanPads` reports nothing connected, no driver is assigned to
the virtual port, and the trace holds one line saying so (`type` of `-`).

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
`C:\projects\padtest`, ported to PSn00bSDK (installed at `C:\PSn00bSDK`) and extended for
this work. Build it with `bash build.sh` in that folder. Besides the pad it shows, per port,
the raw reply in pairs, the reply length, `cfg n/10` (config replies that match a DualShock)
and `btn n` (button bits pressed so far), and for analog pads `axes` (distinct values per
axis). Each frame it copies a status block to VRAM (640,256), 64x8 halfwords, layout in the
fork's `include/dx.h`. One command runs it with the sweep and decodes the block:

```
bash scripts/padtest_dx.sh --ct 1        # 0 = Standard, 1 = Analog; --frames for pictures
```

It ends in `sweep: PASS` or `sweep: FAIL: <what>`, judged on port 1 (the port the sweep
drives; port 2 is Dolphin's emulated Wiimote and fails on its own). The ROM puts itself on
the test profile's card (`.dolphin`) only. Use `vram.bin` for the block, not for pictures:
the hardware GPU draws on the GX side, so the dump holds uploads, not the rendered screen.

What it found on 2026-09-23 (commit of this section), against psx-spx:
- Analog: 9 of 10 config replies match a DualShock. Config-mode 43h (exit) returns
  `F3 5A FF FF 00..` where a DualShock returns `F3 5A 00 00 00..`. 45h reports type 03h
  (DualShock 2); a PS1 DualShock reports 01h.
- Standard: acts as a digital pad without config commands, except that `sio.c` answers 43h
  with ID 43h and 45h with F3h; a real digital pad answers 41h to every command.
- /ACK comes after each byte but the last (about 20 loops of the ROM's wait), as on hardware.

**PSXTEST 2.3** by Haunted360 -- a general console tester (three songs for the speakers, a
dead-pixel checker, a pad tester). It runs too, and the trace under it is identical, but its
pad display is one line of digital button names on a menu screen with no analog readout, so
it says much less. `psxdev.net` no longer serves it; the Wayback Machine does, at
`web.archive.org/web/2018/http://psxdev.net/homebrew/files/psxtest.zip`.

PadTest 1.1 and PSXTEST are third-party binaries and not in this repository. Put the bin/cue in
`wiisxrx/isos/<name>/` on the SD card and point an autoboot file at it.

## 4. Things that did not work, so nobody tries them again

**Dolphin input movies (.dtm).** The obvious way to script a pad is to hand Dolphin a
movie with `-m`. `scripts/padtest.py make` still writes one, and it is well formed -- it
round-trips through this repo's own `scripts/dtm2autoinput.py`, which parses 2188 polls and
recognises every button press with the right mapping. The installed Dolphin
(`C:\tools\Dolphin-x64`) has the `-m` option in its binary and accepts it without
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
