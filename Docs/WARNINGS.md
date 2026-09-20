# Compiler warnings: where they stand and what to do with them

The normal build compiles with `-w`. `scripts/build.sh debug-warn` (or `release-warn`) turns on
`-Wall -Wextra` instead, and `scripts/warn_summary.py` turns the resulting log into counts:

```
bash scripts/build.sh debug-warn 2>&1 | tee /tmp/warn.log
python scripts/warn_summary.py /tmp/warn.log --by kind
python scripts/warn_summary.py /tmp/warn.log --kind -Wsign-compare   # every site of one flag
```

A clean rebuild of the Gamecube target is needed for a full count; an incremental one only
reports the files it recompiled. The vendored dependencies under `deps/` build separately with
warnings off and are not included.

**Baseline: 0 errors, 560 warnings.** The defects among them have been fixed (commit
"Fix the warnings that were real"); what is left is tidying, and it is worth doing because the
noise is what hides the next real one.

## What is left

| Flag | Count | Group |
|---|---|---|
| `-Wunused-parameter` | 177 | C |
| `-Wincompatible-pointer-types` | 87 | B |
| `-Wunused-variable` | 76 | D |
| `-Wsign-compare` | 76 | D |
| (no flag) | 42 | A and D |
| `-Wwrite-strings` | 25 | D |
| `-Wempty-body` | 20 | D |
| `-Wunused-function` | 13 | D |
| `-Wpointer-sign` | 12 | D |
| `-Wdiscarded-qualifiers` | 8 | D |
| `-Wmissing-field-initializers` | 8 | D |
| `-Wformat=` | 4 | A |
| `-Wcast-function-type` | 2 | A |
| `-Wcomment`, `-Wtype-limits`, others | ~10 | D |

## Group A: small, and each one is a real defect (about 40 warnings)

These are not style. Do them first.

1. **`SoftGPU/drawGX.c:489,511`** — `drawCircle` and `drawLine` are defined with no return
   type, so they default to `int` and *conflict with their own declarations*. Four warnings
   between them (`-Wimplicit-int` plus "conflicting types"). Give them the declared type.
2. **`GlesGpu/gpuPlugin.c`, 18 sites** — objects with internal linkage (`ReadbackEnabled`,
   `MarkCpuVramWrite`, `g_readbackState` and others) are used from `inline` functions that are
   not `static` (`FinishedVRAMWrite`, `FinishedVRAMRead`). That is a constraint violation in
   C99 and later; it links today because of link-time optimization. Make those helpers
   `static inline`.
3. **Twelve `-fpermissive` conversions** in the C++ menu and graphics files
   (`GamecubeMain.cpp`, `FileBrowserFrame.cpp`, `MainFrame.cpp`, `GraphicsGX.cpp`) — `void*`
   assigned to a typed pointer without a cast, which is legal C and an **error** in standard
   C++. The build only compiles because `-fpermissive` is passed. Add the casts, then consider
   dropping the flag so the next one cannot slip in. One of them, the `GXTexRegionCallback`
   signature in `GraphicsGX.cpp`, is a const mismatch against libogc's own prototype and
   deserves a real fix rather than a cast.
4. **`Gamecube/plugins.c:490`, 2 sites** — the netplay table casts `void (*)(netInfo *)` to
   `long (*)(netInfo *)`. Calling through a function pointer of the wrong type is undefined;
   it survives only because this ABI returns in a register the caller then ignores. Fix the
   table's types, or delete the netplay path if it is dead.
5. **Four `-Wformat=`** — `%x` given a `long`. Harmless where `long` and `int` are both 32 bits,
   which is everywhere this runs, but the correct specifier costs nothing.
6. **`ppc/ppc.h:45`** — `ppcAlign` is declared `inline` and never defined.

## Group B: one lever, 53 warnings

Fifty-three of the 87 `-Wincompatible-pointer-types` are a single family: `GETLE32` and
`PUTLE32`, whose definitions in `gpulib/gpu.h` take whatever they are handed and cast it. One
of them, `PUTLE32`, is visibly broken as written (it assigns to a cast and ignores its second
argument), which means either nothing uses that definition or its uses are getting a different
one from `PeopsSoftGPU/swap.h`. Work out which definition is in force, give the pair honest
prototypes or inline functions with the right pointer types, and the count falls by about a
tenth of the total. Do this one carefully and on its own: it touches byte-order handling on a
big-endian target, so a Spyro run comparison before and after is not optional.

## Group C: suppress instead of fixing, 177 warnings

`-Wunused-parameter` is the largest group and the least informative. It concentrates where
signatures are fixed by an interface and a parameter genuinely is not needed: the Lightrec
adapter (22), `SoftGPU/gpulib_if.c` (10), `Gamecube/plugins.c` (9), and the controller drivers
(8 and 7 in the Wii DRC and HID GameCube back ends). Editing 177 call signatures to add
`(void)x;` is churn that changes no behaviour and risks typos in code that is otherwise
correct.

Add `-Wno-unused-parameter` to the `WARN=1` flags in `Gamecube/Makefile_Wii` and
`Makefile_Wii_Release` instead, with a comment saying why. That alone takes the count from 560
to under 400 and makes the rest readable.

## Group D: mechanical sweeps, about 250 warnings

Do these one flag at a time, one commit each, with the before and after counts in the message.

- **`-Wunused-variable` (76), `-Wunused-function` (13), `-Wunused-but-set-variable` (1).**
  Every one is either dead code to delete or a symptom of logic that was commented out and
  left half-connected. Read each before deleting; do not silence with a cast to void, which
  preserves the confusion.
- **`-Wsign-compare` (76).** Usually a loop counter declared `int` against a `size_t` or an
  unsigned length. Fix the declaration, not the comparison.
- **`-Wempty-body` (20).** All of them are `if (cond) LOG(...);` where the logging macro
  compiles to nothing. Redefine those macros as `((void)0)` (or `do {} while (0)`) and the
  whole group disappears at once. `PSXBIOS_LOG`, `CDR_LOG_I` and `log_anomaly` cover most.
- **Macro redefinitions (9, no flag).** `bool`, `unlikely`, `NULL`, `UINT_MAX`, `STACK_ALIGN`,
  `SWAP16`, `SWAP32` are each defined in more than one header. Guard them or include the
  canonical header instead of redefining.
- **`-Wwrite-strings` (25), `-Wpointer-sign` (12), `-Wdiscarded-qualifiers` (8).** Const and
  signedness on string handling. Low risk, and they make the next real type error visible.
- **`-Wmissing-field-initializers` (8).** Usually harmless, occasionally a struct that grew a
  field nobody set. Check each before adding zeros.

## How to work through it

- One category per commit. Put the before and after counts from `warn_summary.py` in the
  message, the way the baseline commit did.
- Build `release` as well as `debug`; the debug probes must compile out either way.
- Run the Spyro script after each batch and compare with `scripts/perf_compare.py`. When a
  change is genuinely behaviour-neutral the counters match bit for bit, including
  `slice cycles`, so any difference is a signal and not noise.
- `CACHE=1` belongs to release testing, not to this loop.
- Never silence a warning with a cast when the type underneath is actually wrong. The point of
  the exercise is that the remaining warnings mean something.

When the count is near zero, `-Werror` on new code becomes possible, which is the only way it
stays there.
