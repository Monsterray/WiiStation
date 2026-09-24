# Save states for testing

Written 2026-09-23. A save state here is the PlayStation only -- RAM, BIOS, CPU, GPU and VRAM,
SPU, CD drive, timers, MDEC, the controller ports and the pad -- not the Wii and not Dolphin.
Memory cards are not in it, as on a real console. The same files serve the menu's ten slots
and scripted runs.

The point for testing: save a game at one moment, then run from that moment as often as
needed, with different settings each time, in one boot.

## Using it

**Make a state** -- boot a game, play a recording if there is one, save at a vblank:

```
bash scripts/wsx.sh state spyro3000 spyro --at 3000 --rec spyro_the_dragon
```

It lands on the test card as `sd:/wiisxrx/states/spyro3000.st`, with `spyro3000.txt` beside
it: the game folder and cue, the vblank, the date, the build date and every setting.

**Run settings from it** -- one boot, one chain line per settings group, each loading the state
and running the same number of vblanks:

```
bash scripts/wsx.sh abstate fmvtest spyro3000 1800 - "FmvColour=1" "gpuPlugin=1"
```

`-` is the settings as they are. `--rec NAME` plays that recording on from the state's vblank
(its pad lines before that vblank are skipped). Every group ends with a `statefp:` line, a
fingerprint of the machine, so groups can be compared: the same settings twice give the same
line. The table has a row per group; its `vbl` and `speed` count from vblank 0, so after a
load `speed` is too high -- compare the other columns.

**In a chain file** a line's `State=NAME` loads the state once the game has booted (its first
vblank); the line's vblank count is then counted from the state's vblank. `State=` takes a full
path too (`State=sd:/wiisxrx/savestates/SCUS_942.28.st0`, a menu slot).

**In an input script** (`sd:/wiisxrx/<script>`):

| Line | Does |
|---|---|
| `state save NAME <vblank>` | save to `states/NAME.st` (+ `NAME.txt`) |
| `state load NAME <vblank>` | load it |
| `statefp <vblank>` | log a fingerprint of the machine (`statefp:` in perf.log) |
| `statecheck <vblank> <n>` | save, run n vblanks, fingerprint; load, run the same n, fingerprint again. `statecheck:` says `match` or which parts differ |
| `statetest <vblank>` | old name: `statecheck <vblank> 120` |

Scripted saves and loads stop the CPU at their vblank and run with it stopped, as the menu
does, and draw no loading bar (it is drawn with raw GX calls and changes the GPU plugin's
state). The old `statetest` saved from inside the vblank handler, part way through a frame.

## Compatibility

The file is the PCSX layout WiiStation always wrote, then optional sections after it:
`WSXE`, then sections of a 4-byte tag, a u32 length and the data, ended by `END0`
(`misc.c`, `stateExtWrite`/`stateExtRead`).

- **An older build loading a new state** reads the PCSX layout and stops: the sections are
  never read. It loads as any state did.
- **This build loading an older state** finds no `WSXE` and loads it exactly as before.
- **A later build** can add sections; a reader skips tags it does not know.

| Section | Holds |
|---|---|
| `PAD1` | the pad plugin: DualShock or digital mode, config mode, the rumble map, a reply half sent (`SSS_PADfreeze`). Before it, a state loaded into Ape Escape came back with a digital pad |
| `SPU1` | the SPU mixer: every voice as it runs, each voice's interpolation history, the FM buffer, the XA and CD-DA samples waiting to be mixed, a few counters (`dfsound/dfspu.c` `spu_ext_save`). The PCSX layout keeps one XA sector or a capped CD-DA tail and rebuilds the rest roughly |
| `GPU1` | the OpenGX GPU plugin between commands: a command half received, a VRAM transfer part done, the busy countdown, the whole status (`GL_GPUfreezeExtra`). Written and read only with that plugin |
| `TIM1` | no data: the timers and the event table were saved exactly and are put back as they were, after the devices' own restores rescheduled their events |

A section whose size does not match this build's layout is skipped, and that part loads as
before.

## What made a loaded state go its own way, and the fixes

`statecheck` found each of these. The test: save at a vblank, fingerprint the machine 300
vblanks later; load, run the same 300, fingerprint again. Before the fixes every game
differed; the emulator itself is deterministic (the same boot twice gives the same
fingerprint, with or without audio rate control), so each difference was state lost.

1. **The timers.** On a load `psxRcntFreeze` recomputed each counter from its value --
   `(cycle - cycleStart) / rate`, rounding off the phase -- and rescheduled the next timer
   event: 8077 cycles late in Gex. With `TIM1` the saved timers come back exactly.
2. **The event table.** Every device restore (timers, SPU, CD) reschedules its own events.
   With `TIM1` the table is put back as saved afterwards. `next_interupt` is also reset on
   every load, so the first slice looks at every event (it held the old game's next one).
3. **GPUSTAT's draw-mode bits.** The GPU restore replays the GP1 commands starting with
   GP1(00), a reset, which cleared the texture page, dither and mask bits the state had just
   set. They are put back after the replay (`GL_GPUfreeze`).
4. **The drawing environment** (draw area, offset, texture window, mask) was not in the
   GlesGpu state; a state loaded into another boot drew with that boot's until the game set
   its own. Now kept in slots `0xE1-0xE6` of the GPU state and replayed, as gpulib does; an
   older state has zeros there and loads as before.
5. **The SPU mixer** (`SPU1`): FF7 and Crash Bash each kept one RAM word apart without it.
6. **The GPU plugin between commands** (`GPU1`): Crash 3 loaded into another boot ran 74
   cycles apart without it.
7. **The pad** (`PAD1`).

Measuring it also needed: the CPU registers copied out of Lightrec before a fingerprint
(`R3000ACPU_NOTIFY_BEFORE_SAVE`; `psxRegs` is stale while Lightrec runs), and the "saved"
fingerprint taken after the save, which writes its own block into `psxH` at 0xF000.

## Results (2026-09-23, debug build, under Dolphin)

- **Same boot** (`statecheck 2400 300`, seven games, with the user's recordings playing where
  there is one): Spyro, Crash 3, FF7, Ape Escape, Gex and Medievil identical in RAM, VRAM,
  hardware registers, CPU and pending events, SPU RAM and cycle count. Crash Bash identical
  when it runs first in the boot; after other games, one RAM word differs (below).
- **Across boots** (save in one chain entry, `State=` in the next, fingerprint 300 vblanks
  on): Gex and Spyro identical. Crash 3 and Ape Escape identical when they run first or alone
  (Crash 3 also identical with gpulib); after other games, one RAM word or a few cycles
  differ.
- **`abstate`**: the same settings twice give identical fingerprints; so does gpulib against
  OpenGX -- the machine does not depend on which plugin draws it.
- **An older state** (made by the build before this work, no sections) loads with `State=`,
  and the game goes on; a `statecheck` from there matches.

**Still open**: what differs depends on which games ran earlier in the same boot, so it is
state carried from one game to the next inside a plugin -- a static the per-game reset does
not clear (the same family as the fixes in 372a470 and 627af35) -- not state a file could
hold. The Crash 3 case is one 16-bit counter at 0x8005E268, one lower in the loaded run.
`statefp V dump` writes RAM to `sd:/wiisxrx/fpram_<k>.bin` to find such words.

## What a state cannot carry

- Settings that decide the machine: the BIOS (HLE or real; a state records which it used),
  the region, the disc. A state is for the game it was saved in.
- Memory cards: a real console keeps them outside too. Chains delete each game's cards when it
  ends, so a run from a state starts with the cards the chain had then.
- The pad type: changing ControllerType under a state works as swapping the pad on a real
  console would -- a DualShock state loaded with Standard comes back digital.
