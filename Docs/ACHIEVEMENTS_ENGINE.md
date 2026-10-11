# Achievement engine: first implementation

Branch: `codex/achievement-engine`. The engine uses the pinned upstream rcheevos
C runtime with settings-file controls. This slice evaluates **local definitions**;
it has no RetroAchievements login, service submissions, hardcore mode, or menu UI.
Online client integration still needs canonical disc hashing and verified Wii HTTPS.

## Configuration

Set these in `settings.ini`, or the existing per-game settings file, before loading
the game. The engine is disabled by default. Paths are quoted, complete device paths.

```ini
[Achievements]
Achievements = 1
AchievementFile = "sd:/wiistation/achievements/TEST12345.txt"
AchievementLog = ""
```

An empty log path uses the definition path with `.unlocks` appended. Create the parent
folder yourself. There are no passwords or account settings in this implementation.
Turning the engine off prevents frame evaluation; changes to the file/set require
reloading the game. Existing settings Save writes these fields without menu controls.

## Definition file

```text
# Synthetic fixture only; these addresses are not achievements for a real game.
Game=TEST12345
1|Three consecutive hits|0xH000010=1.3.
2|Little-endian 16-bit word|0x 000020=4660
3|Scratchpad byte|0xH200001=9
```

The first non-comment line must be `Game=<CdromId>` matching the loaded disc.
Following lines are `positive-id|title|rcheevos-condition`. Blank lines and lines
starting with `#` or `;` are ignored. IDs must be unique 32-bit values. Limits:
64 KiB per file, 128 achievements, 79 bytes per title, 2048 bytes per condition.
Malformed files reject the entire set. These are local IDs, not official service
awards. Conditions use the upstream format; initial conditions must become false
before the runtime leaves its WAITING state. A permanently true condition does not
immediately award on startup.

RAM occupies logical addresses `0x000000–0x1fffff`; scratchpad occupies
`0x200000–0x2003ff`. Reads return guest bytes, handle ranges crossing the boundary,
and return a partial/empty read at the map's end. The evaluator handles little-endian
conditions. BIOS, VRAM, and card contents are not part of this map.

## Lifecycle and persistence

- Evaluation runs at the shared emulated frame end, independently of display frameskip.
  No file reads/writes, allocation by this wrapper, or network calls occur in that hook.
- Reset suspends evaluation during BIOS execution, resets hit/progress state afterward,
  and preserves earned local achievements. Shutdown flushes and destroys the runtime.
- A disc swap closes the local set until the game is fully loaded again. The serial
  binding is a local safeguard, **not canonical RA content identification**; associated
  multi-disc sessions are deferred to the future rc_client integration.
- Unlocks accumulate in memory and append to a text journal when CPU execution stops,
  including menu entry and scripted state operations. Each line contains the exact
  definition-file MD5, local ID, and title. Reload restores matching local unlocks.
  Write failure keeps pending entries for the next stopped boundary. Power loss before
  flush can lose pending unlocks; this is not a durable official award outbox.
- Any definition-file edit, including comments, changes its identity and starts a new
  local unlock namespace. The digest is a consistency check, not authentication.
- The optional bounded `ACH1` WSXE state section holds a version marker, definition
  digest, earned bits, and upstream sized runtime progress. Wrong-set, missing, old,
  or corrupt progress resets the runtime. Loading an older state never revokes earned
  local achievements. The underlying PCSX state layout remains unchanged.

## Validation

Run `bash scripts/test-achievements.sh` and `python3 scripts/settings_check.py`.
The host check uses the actual vendored runtime and exercises memory boundaries,
little-endian and scratchpad conditions, hit-count restoration, pause/reset behavior,
monotonic unlocks, journal reload, changed-set rejection, corrupt progress, disabled
mode, duplicates, malformed definitions, and size limits. ASan covers all sources;
UBSan also covers WiiStation's glue. Upstream parser offset expressions trigger Clang
UBSan diagnostics, so upstream is compiled with ASan only and kept unmodified.

Build checks do not establish hardware performance. Test on real Wii/vWii before
enabling it in regular play.

Implementation checks on 2026-10-10: host checks passed; the engine and 18 vendored
runtime/util sources cross-compiled with `-Wall -Wextra -Werror`; `psxcounters.c`,
`r3000a.c`, and `misc.c` cross-compiled against installed libogc2 headers. The missing
`wupc/wupc.h` was traced to an incomplete/incompatible system library installation.
Using the repository's bundled libraries and pinned r41-2 macOS compiler, the full
release DOL build passed, including an end-to-end build from the checkout containing
spaces. The local setup script, library preflight regression test, achievement host
checks, and settings check passed. Build logs are in ignored
`.runs/achievement-build/`; outputs are `Gamecube/WiiSXRX_Release.dol` and `.elf`.
See README's isolated setup instructions. The hardware run
remains unverified. Runtime objects disable
fast-math/LTO to preserve floating-point condition semantics under the core's
`-Ofast` build flags.
