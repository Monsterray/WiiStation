# A menu framework for WiiStation -- plan (for review, 2026-10-09, updated 2026-10-10)

Nothing here is built yet. The goal (Monty, 2026-10-09): one robust way to build every menu --
tabs, columns, rows, rows that are only buttons, a safe place for the spinning logo, D-pad
traversal that always works, on/off buttons, cycle buttons, and one-of-several choice buttons --
instead of hand-placed coordinate tables.

## Where we are

Nine frames in `Gamecube/menu` (Main, LoadRom, FileBrowser, CurrentRom, Settings, Options,
ConfigureInput, ConfigureButtons, CustomizeCoOp), built three different ways:

| Frame | Layout | Focus (D-pad) | Size |
|---|---|---|---|
| SettingsFrame | 80 buttons, 30 labels, 99 strings in static tables, each with x/y/w/h and Up/Down/Left/Right **indices** | hand-typed indices, re-patched per tab in `activateSubmenu` | 2055 lines |
| OptionsFrame | **table-driven** rows (`OptRow`/`OptPage`), positions computed | computed from the rows | 934 |
| ConfigureInput / ConfigureButtons / CoOp | macro grids and loops | 32+ explicit `setNextFocus` calls | 765 / 991 / 215 |
| Main, LoadRom, CurrentRom, FileBrowser | hand-placed lists | hand-linked | 250-860 |

What hurts today (from the inventory):
- **Index coupling.** A Settings button, its label and its string are found by number. Buttons
  can never be deleted (15 are hidden-but-kept because other buttons' focus links name them), and
  `scripts/menu_text_width.py` keeps a *second* hand-typed copy of the Settings layout.
- **Five copies** of the tab-row focus loop, one per tab; two row pitches (60 and 70) on one screen.
- **A new setting touches 6-8 files**: `wiiSXconfig.h`, the settings table in `GamecubeMain.cpp`,
  `example_settings.ini`, `SETTINGS.md`, the menu tables, the help text, `lang/*.lang`.
- **Hazards**: focus follows links through inactive buttons with no visibility check or cycle
  guard (`Button.cpp:383-420`); the pointer can click hidden-but-active buttons (`CursorManager`
  checks no visibility); `Button` silently forces height 56; Settings polls tab-switch input
  inside its *draw* function.
- **Limits**: `OptionsFrame` pages hold 6 rows, radio rows 3 choices, 8 help lines; click
  handlers are macro-expanded per row.

## The design

### 1. One description per screen
A screen is data: `Screen { title or tabs[], pages[] }`, `Page { rows[], help[], logo }`,
`Row { kind, label, binding, ... }`. A frame class only declares its screen; one engine lays it
out, links focus, draws the selection state and handles input. OptionsFrame is already 80% of
this and becomes the engine's first user.

### 2. Row kinds (the user's list, plus what the existing screens need)
| Kind | Looks like | Used for |
|---|---|---|
| **Toggle** | one button reading On / Off | Show FPS, Reverb |
| **Choice** | 2-4 buttons, exactly one lit (auto-width; more choices than fit become a Cycle) | CPU core, BIOS device, Frame Rate |
| **Cycle** | one button that steps through values | CPU Speed, CD buffer |
| **Range** | value with - / + (left/right) | cheat option ranges, volume-like values |
| **Buttons** | a row of plain action buttons, no label | Advanced / Memory, Load / Save State, Save Settings |
| **Readout** | label and text, not focusable | SPU plugin, memory use |
| **List** | a scrolling list of items, each a row (toggle or action), with page up/down | cheats, files, save states |
| **Help** | the two-column term/text block under the rows | every page |

### 3. One grid, in `MenuLayout.h`
Every screen uses the same measures (all already the Settings tabs' de-facto values): tab row
y 20 (positions 25/155/275/395/515), first row y 100, pitch 60, button height 56, label column
centred at 150-160, controls from x 295, a 10 px gap. **Columns**: a page may split into two
control columns (CurrentRom's two button columns) or a full-width row (radio rows with long
names start further left, as now). The right edge of every row comes from `MENU_ROW_RIGHT(y)`,
so nothing can enter the logo's box.

### 4. The logo as part of the layout
Each screen declares where the logo sits (top-right on Main, bottom-right on pages, hidden on
the controller screens). The engine reserves that box, so rows and help text are kept clear of
it at layout time. Today this is a hand-kept list in `MenuContext.cpp` plus a check after the fact.

### 5. Focus generated from the grid
Up/Down go row to row and keep the column; Left/Right move within a row (and wrap); the tab row
is the top of the loop. Hidden and inactive controls are never focus targets; every link is
computed when a page is shown, so a hidden button can never break another's links. The pointer
uses the same visibility. Tab switching (L/R, +/-, Classic shoulders) moves from Settings' draw
code into the engine's input step.

### 6. Settings bound once
Each control binds to the setting it shows, via the existing settings table in
`GamecubeMain.cpp`, extended with the label, choice names, help text, section and *when it applies*
(at once / when the game resumes / at the next load / restarts the game). From that one table:
the menu row, the settings-file key, `example_settings.ini` and the `SETTINGS.md` row -- either
generated, or checked by a script (`settings_check.py` already checks two of them). A new
setting then means: one table entry, one row in a page, its effect in the emulator.

### 7. Checking layouts without a Wii
The engine's layout code is plain C++ with no GX calls, so a host build (`clang`) can lay out
every screen and report overlaps, text past its button, anything in the logo box, unreachable
controls and focus dead ends -- replacing the hand-kept `TAB_ROWS` copy in
`menu_text_width.py`. Dolphin screenshots via `menupage N` (`Docs`: SETTINGS.md) stay the
visual check for each converted screen.

## Migration, in steps that each ship
1. **Engine + OptionsFrame** (Advanced tabs, Advanced Sound/Graphics, Memory): add Toggle,
   Buttons, Range, List and columns; generated focus with the guards; the host layout check.
   Behaviour and pictures unchanged except where the old layout was wrong.
2. **Settings, one tab at a time** (General, Video, Input, Audio, Saves) onto the engine; each
   tab's screenshot compared before/after; the index tables shrink tab by tab and are deleted at
   the end, with `TAB_ROWS` in the checker.
3. **Button-row screens**: Main, LoadRom, CurrentRom (two columns).
4. **Lists**: Cheats (new), FileBrowser, save-state slots.
5. **Grids**: ConfigureInput, ConfigureButtons, Customize Co-Op (the most bespoke; last).
6. **One settings table** feeding menu rows, the file and the docs.

Each step is a version with its own changelog line; nothing is removed before its replacement has
been screenshotted in Dolphin and used on the bench Wii.

## Learned since the first draft (2026-10-10)
- **Focus from geometry works.** Customize Co-Op now links every button to the nearest one in each
  direction (centre distance along the direction + 2x the sideways offset). It reached all 24
  buttons with no hand-typed links; the engine's generated focus (section 5) uses the same rule
  for grids and free layouts, and row/column order for rows.
- **Menus can be driven by script.** `menukeys <frame> <keys>` in an input script presses the GC pad
  in any menu, dialogs included (Dolphin and the bench Wii); `menupage N` opens a page. Every
  converted screen gets a script that walks all its controls, and its frames are compared before
  and after (`sheet.py`).
- **Dialogs are part of the engine.** MessageBox runs its own draw loop inside a click handler
  (nested `Gui::draw`); the engine should own modal dialogs as a page state instead.
- **The cheats page needs List, groups and Range first** (Docs/CHEATS_PLAN.md): up to 5,405
  entries in one game, grouped by `\`, with option rows under a cheat.

## Proposed order (answers question 1 below, if agreed)
1. Engine on OptionsFrame (rows, Toggle, Choice, Cycle, Readout, Buttons, Help, generated focus,
   tabs, logo box, host layout check).
2. **List + Range** rows and modal dialogs -> the Cheats page lands here.
3. Settings tabs one by one; 4. Main/LoadRom/CurrentRom; 5. FileBrowser and save-state slots as
   Lists; 6. Configure Input/Buttons/Co-Op grids; 7. one settings table.

## Decided (Monty, 2026-10-11)
- Order of the projects: libogc3/devkitPro update first (Docs/LIBOGC3_PLAN.md), then this framework,
  all steps to the end, then cheats.
- Translations stay: English by default, a language only where contributors provide it
  (`lang/*.lang`); new labels get keys so they can be translated.
- Every page keeps its look: the framework straightens the code, not the layout. Rearranging comes
  after, as its own decision.
- Performance: the engine lays out and links focus when a page is shown, not per frame; drawing
  stays the existing GX path. Anything that would cost a measurable amount of MEM1/CPU is put to
  Monty before it is built.

## Questions for Monty
1. Is the order right? (Cheats need the List row early -- step 4 could move before step 2.)
2. Translations: `lang/*.lang` has no entries for newer labels (Advanced, Memory, ...). Keep the
   gettext files and add entries, or drop translations for new rows?
3. Should two-column pages (e.g. CurrentRom) keep their look, or move to one column of Buttons rows?
4. Any screen you want redesigned rather than ported as-is?
