# Miis as players: plan

Goal: each Mii on the console is a player. A Players menu seats Miis on the PS1 pad ports;
**Local** achievements (the unlock journal) are kept per Mii. Status: plan, 2026-10-28 (owner's
decisions below); the menu is at the mock-up stage.

## The Mii SDK (hbc-reborn `sdk/mii`)

WiiStation uses hbc-reborn's Mii SDK, vendored as `deps/mii` from the same commit as the HBC
agent (`deps/hbc_agent`, HBC 1.12.1, bdf44d2). It is public domain: `mii.c`/`mii.h` (data and
the database), `miidraw.c`/`miidraw.h`/`miibody.c` (drawing with GX). Its README is the reference;
what this plan uses:

| Need | SDK call | Notes |
| --- | --- | --- |
| Read the Miis | `mii_db_load`, `mii_db_get` for slots 0-99, `mii_db_free` | About 0.7 s: once, on a thread, never in the frame loop. **WiiStation never saves** (`mii_db_save`, `_put`, `_add`, `_remove` are not called anywhere). `MII_E_NO_FILE`: the Mii Channel was never opened, so no Miis |
| Identity | `mii.id` (the creation ID) | The key is its 16 hex characters. Slot and name are not stable |
| Name, color | `mii_get_name`, `mii.fav_color` (0-11, `mii_color_name`) | The coin's color is the favorite color, as HBC's picker draws it |
| Coin faces | `mii_res_load`, `mii_model_build` (32-64 px face, normal expression only), `mii_icon`, `mii_model_free` | Needs `sd:/mii/RFL_Res.dat`, which the user saves with HBC's HOME > HBC > Save Mii faces (Nintendo's file: never shipped, never copied by us). Without it a coin shows the name's initial on the favorite color |

Rules the SDK sets that touch WiiStation:

- `mii_icon` draws in the EFB's corner between frames: call it after the menu's last copy and
  before the next frame, then set the menu's GX state again (pixel format, viewport, scissor,
  projection, vertex format, TEV). The menu mixes raw GX and OpenGX, so the icons are made in
  one place (`Gamecube/menu/mii_coins.c`, new) right after `Gui::draw()`'s copy.
- Icons are textures kept in MEM2 (64x64 RGB5A3 = 8 KB, 100 Miis = 800 KB) and made lazily, the
  visible page first. The models are freed as soon as the icon exists; nothing of the SDK stays
  in memory once a game starts (MEM1 is what chains run out of).
- The GX FIFO should stay in MEM1 (SDK README: a FIFO in MEM2 stalled at random on a real
  Wii). WiiStation's is `memalign`ed at start-up, before anything could push the heap into MEM2;
  making it a static array is a one-line safety if that ever changes.
- Dolphin ships no `RFL_DB.dat` or `RFL_Res.dat`. Tests stage a database made by
  `mii_random` + `mii_db_add` on the PC side (`-DMII_NO_OGC`), into the run profile's NAND
  (`Wii/shared2/menu/FaceLib/`); without faces the coins show initials, which is a test case too.

## Owner's decisions (2026-10-28)

1. **A Players menu** (not a prompt at game load): a grid of Mii coins with pages, and the PS1
   pad ports along the bottom.
   - Wii Remote: point at a coin, hold **A+B** to grab it, drop it on a port.
   - Any other pad (Classic, GameCube, Wii U Pro, USB): **A** on a coin picks it up and the focus
     jumps down to the ports; Left/Right chooses a port, **A** drops it there, **B** puts it back.
   - Mock-ups first, to get the menu right (below).
2. **Every seated Mii earns.** An unlock goes to each Mii seated on a port whose pad is
   connected, not only player 1's.
3. **Existing unlocks stay with Guest.** Journal lines written before this feature have no
   player key and belong to Guest.
4. **Official mode per Mii: later,** and only with a design that is elegant. Open questions are at
   the end.

## Design

- **Seats**: `wiistation/players.ini` (5.18.2; `[Players]` in settings.ini before), one line per
  seat (`Player1=`, `Player2=`, multitap `Player1A=` ... `Player2D=`, Co-Op `Player1P1=` ...
  `Player2P8=`), each a Mii key or empty for Guest; the menu writes it when it closes.
  `PlayersRecent=` keeps the last 15 seated, most recent first: the menu lists them first. A Mii can sit on one port only;
  dropping it on another moves it, dropping it on a taken port swaps the two. Seats are kept
  across sessions. A key no longer on the console reads as Guest, with a notice; its unlocks
  stay in the journal (nothing is deleted).
- **Who earns**: at an unlock, every port that has a Mii and a connected pad (the same test the
  Co-Op pad mapping uses). Guest earns when no seated Mii does (a game with only Guest pads still
  unlocks, as today). One journal line per Mii that earned it.
- **Journal**: one file, as now, with the player key as a new column:
  `<set digest> <id> <player key> <title>`. A line without a key (every journal written so far)
  is Guest's. Each achievement is earned once per Mii. The 1 MB cap and the fail-closed rules
  of `achievements.c` stay.
- **"Earned" in the game**: the game's achievement state is the union of the seated Miis'
  unlocks, so an achievement one Mii has keeps firing for the others who do not have it yet.
  rcheevos sees an achievement as active while any seated Mii lacks it; when it triggers, only
  the Miis without it get the line and the toast names them.
- **Save states**: the achievement section (Local: `WAC1`, `WAO1` is Official's) records the seat keys, after rcheevos' progress (5.19.0). Loading a state made by
  other players keeps the game state, but earned flags come from the journals of the Miis seated
  now, so a state never moves one Mii's unlocks to another.
- **Toast and Achievements page**: "Kat unlocked ..." (two or more: "Kat and Ollie
  unlocked ...", four or more: "4 players unlocked ..."); the page shows one seated Mii at a time
  with L/R to switch, its coin in the header.
- **Privacy**: Mii names are personal. Logs, perf.log and crash reports print only the first 8
  hex characters of a key. The creator name is never read out of `mii`.

## The Players menu (layout D, chosen 2026-10-28)

Mock-ups A-E were shown in the chat; the owner chose **D**, with subtle PS1 port theming.

- **Ports first**, on a band the gray of the PS1's plastic: one card per port (two, or eight
  in two rows with multitap). A card reads like the console's label: the port number large,
  "CONTROLLER" small, the pad connected there, the seated Mii's coin in a round recess, its
  name and, with a game loaded that has a set, "4 / 31 achievements" (that Mii's unlocks in
  this set). A small PS1 controller socket sits in the card's corner; its pins light when a
  Mii is seated on a port with a pad. A port without a pad is a shade darker.
- **The coins** below: a strip of 16 a page (8 x 2), paged with + / - (no arrows at the top).
  A coin is the Mii's head on a disc in its favorite color; **a favorite Mii's coin has a
  gold rim**. Without the face models a coin shows the name's initial. A seated coin is dimmed
  and wears its port in a tag under it. The focused coin's name and progress show on one line under the strip.
- **Input**: Wii Remote pointer, hold A+B on a coin, drop it on a card (on a taken card the
  two swap; back on the strip unseats). A pad: A on a coin picks it up and the focus goes to
  the cards (arrows choose), A seats it, B puts it back; on a card A picks its Mii up, B
  makes the seat Guest. Up from the strip's top row goes to the cards.
- No plugs: a plug-and-cable mock-up was fun but too silly.
- **Co-Op** (5.18.1, the owner's call): a Co-Op port's card is a group, a coin well per
  player and a "..." button. A, a click or a dropped coin opens the same screen with a card
  per Co-Op player (seats `Player1P1` ... `Player2P8`, virtual controllers 10-25); B leaves.
- Details the owner asked for (5.18.1): round coin slots ("Guest" centered in an empty one),
  a solid focus ring, "Port 1A" in a tag under a seated coin, the socket centered on the two
  big cards and in the corner on the small ones, names shrunk to fit, the coins' page
  ("Miis 1 / 2") at the bottom right. US spelling everywhere.

## Phases and checks

| Phase | Work | Check |
| --- | --- | --- |
| 0 | Vendor `deps/mii`; a read-only probe on the **bench** Wii: `mii_db_load`, print the count and key prefixes (no names) to perf.log, time the load | One bench job; never on a protected Wii |
| 1 | Host side: a fixture maker (`scripts/mii_fixture` from `mii.c` with `-DMII_NO_OGC`: fictional Miis, real CRC) and the seat/journal logic in plain C with a host test (`tests/host/hostcheck.h`): old lines = Guest, two Miis, a swap, a missing Mii, the cap | Host test |
| 2 | The Players menu: coins (initials first, then `mii_icon` faces), pages, both input paths, seats in settings.ini | `wsx.sh menu` pictures in Dolphin with the fixture; bench Wii with its own Miis and saved faces; capture card for the TV picture |
| 3 | Journal column, union of earned, per-Mii toast and page, WAO1 seat keys | Dolphin chain: Local set, Mii A unlocks, seat B (not earned), A + B seated (only B gets it), a state from A loaded with B seated |

## Open questions for Official mode per Mii (decision 4, later)

- Where an account is linked: on the Mii's coin (a long press, or a "link account" item on the
  focused coin's tip), so the Players menu stays the one place a player is set up.
- Which seated Mii's account plays Official: player 1's only (RetroAchievements sessions are one
  user per game), with the others earning Local as now.
- Token storage: one token file per key in `sd:/wiistation/ra/`, never the password, as today.
