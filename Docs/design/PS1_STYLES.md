# PS1 UI styles

Reference link (claude.ai, private): https://claude.ai/artifact/1dyHBQsa2sqAj1NuW6JEBx

Three toast styles from the achievement mockups (round 3, 2026-10-23). The user likes all three
and wants to use the theme in other places. The working page is `ps1_toast_styles.html` in this
folder (open it in a browser and press Play). Each style below lists what makes it look like a PS1.

## Common rules

- **Motion:** slides, wipes and steps only. No bounce, no squash, no overshoot.
  Entrances use ease-out (`cubic-bezier(.1,.7,.2,1)`); exits use ease-in (`cubic-bezier(.6,0,.9,.4)`).
- **Colour:** PS1 colour is 15-bit, so a gradient shows as bands. Make a gradient of 4 or 5 hard
  bands, then put a 2 x 2 dither on top (black at 16 % alpha, checker pattern).
- **Pixels:** small icons are 16 x 16 and are drawn with nearest-neighbour filtering.
- **Text:** the menu font (Segoe-based) with a hard 1-2 px shadow, not a soft shadow.
- **Counters:** numbers step up in whole values (points, earned count). They do not slide.

## 1. Save Block (memory card)

Looks like the PS1 memory-card manager.

| Part | Value |
|---|---|
| Card bands (top to bottom) | `#2b50a8` `#27499e` `#234394` `#1f3c89` `#1a347c`, each 20 % |
| Border | 2 px `#a7c2ff`, inner 1 px `#0d1d45` |
| Icon box | 56 x 56, `#0a1638`, inset border `#6f8fd8`, dithered |
| Icon | 16 x 16 trophy, three frames: front, turned, side, turned; 0.3 s per frame (1.2 s loop) |
| Sparkles | three white pixels that blink in turn (0.9 s loop, 0.3 s apart) |
| Set meter | 40 blocks; the new block flashes white twice |
| Count | "BLOCK 12/40" in mono |
| Sheen | one diagonal white band crosses the card once after it lands |
| Gold bands (completion) | `#ffe7a0` `#f6d27a` `#eec064` `#e3ad4c` `#d39936` |

## 2. Disc Read (spinning CD)

A disc that spins up like a PS1 drive, and a laser that reads the text in.

| Part | Value |
|---|---|
| Disc | 70 px, rainbow conic surface, black hub with a white ring, one dark mark to show rotation |
| Spin-up | constant acceleration for 1.2 s: angle = a t² / 2, up to 900 deg/s; "SPIN UP" blinks beside it |
| At speed | a pale ring fades in over the surface (motion smear); 900 deg/s from then on |
| Read | the panel opens, then a red laser line sweeps left to right; the text shows behind it |
| Exit | the same 900 deg/s while the whole toast slides off to the left |

## 3. Broadcast (lower third)

A sports-TV graphic, built in steps.

| Step | What happens |
|---|---|
| 1 | The gold tab slides in (with the 16 x 16 icon) |
| 2 | The dark strip wipes out from behind the tab |
| 3 | A thin top tier wipes up with the game's name |
| 4 | The points box counts up; a glint runs along the gold line |
| 5 | The earned count goes up and the new block flashes |
| Exit | The same steps in reverse order |

It stays inside the title-safe width (32 px from each side).

## Also on the page

- **Polygon:** a PS1 "item get" box. It opens from a line in chunky steps with a white flash. A
  low-poly trophy is drawn one flat triangle at a time (45 ms each), turns in 16 steps and jitters
  by 1 px. Banded gold letters drop in one at a time. It closes like an old TV turning off.
- **Completion animations:** Roll-up, Gold Card, Gold Disc, COMPLETE!

## Achievement tiers (decided 2026-10-23)

Unlock toasts use the Save Block II card. The card colour is the tier. The tier comes from
rarity (the share of players who have the achievement, `rc_client_achievement_t.rarity`), or
from points when rarity is not known (Local sets, before login).

| Tier | Rarity | Points (no rarity) | Effect as it lands |
|---|---|---|---|
| Blue | 10 % or more | under 10 | slide in, sheen |
| Bronze | under 10 % | 10 | one white flash |
| Silver | under 5 % | 25 | burst of rays from the icon |
| Gold | under 2 % | 50 | flash and rays |
| Crystal (see-through) | under 1 % | 100 | rays and a white core that fills the card |
| Beaten (game) | every Progression + a Win | - | the Win card flashes into Gold (G1) |
| Mastered (game) | every achievement | - | the last card bursts into Platinum (G2), crown icon |

- Every type takes the tier of its rarity, Progression too.
- Platinum is brushed metal (bright and dark bands, two specular bands), no rainbow.
- The card has no heading text. Top right: the points it gave. Bottom left: `BLOCK x/y`.
  Bottom right: the share of players (`8.1% have it`).
- Icons (16 x 16): Standard trophy (3 frames), Progression key, Missable hourglass (3 frames of
  sand), Win chequered flag (2 frames). Chips: `Progress` (teal), `Missable` (red), `Win`.
- Mockup: https://claude.ai/artifact/89LDeEztjrySZwdLPPZiLX (version 7).
