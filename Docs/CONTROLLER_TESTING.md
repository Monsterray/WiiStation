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

## 2. The live path, through Dolphin

The maths being right does not prove a press arrives. `scripts/padtest.py` drives the whole
path from Dolphin's side and reads back what the game saw.

**The debug build keeps a pad timeline.** `perf_pad_event` (`Gamecube/perf_prof.c`) records
one line each time what a virtual port hands the PlayStation changes, with the driver's own
output beside the final bytes, and writes `sd:/wiisxrx/padtrace.csv` on every perf report.
Repeats are dropped, so a stick held still costs one line. Release builds compile it out.

**Generate the input.** A Dolphin input movie plugs in GameCube controller 1 and sweeps
each stick axis end to end, then presses each button on its own:

```
python scripts/padtest.py make /c/tools/Dolphin-x64/pad.dtm
```

**Run it.** The trace only fills while a game is polling the pad -- in the menu nothing
calls the pad plugin -- so the run needs an autoboot file:

```
DOLPHIN_ARGS="-m /c/tools/Dolphin-x64/pad.dtm" scripts/dolphin_run.sh out 120 "" "" <autoboot>
python scripts/sdimage_read.py wiisxrx/padtrace.csv > out/padtrace.csv
```

**Check it.**

```
python scripts/padtest.py check out/padtrace.csv
```

It reports, per port, how much of 0..255 each axis actually covered and whether 128 was
seen at rest, and then how many buttons moved a bit of their own. It deliberately does not
know which bit belongs to which button: it asserts only that each press moves exactly one
bit and that every press moves a different one, which tests that the mapping is complete
and one-to-one without hard-coding the bit order, the active-low sense or the byte swap.

`--start` is how many input frames of nothing come before the sweep, to let the game load;
`--hold` is how long each value is held. The generated movie round-trips through this
repo's own `scripts/dtm2autoinput.py`, which is a useful check that it is well formed.

## 3. What neither tool covers

- **Two controller families at once.** Dolphin can emulate a GameCube pad and a Wiimote
  together, but the movie format carries Wiimote reports in a different record kind that
  `padtest.py` does not write. The bug this would have caught -- one `wpadNeedScan` flag
  guarding three different hardware polls, so whichever driver ran first left the others
  unscanned -- was found by reading, not by testing.
- **Anything about real controller hardware**: worn sticks, dead zones in practice,
  Bluetooth latency, how much travel a Classic Controller really has.
- **Rumble**, which has no observable output in a trace.
