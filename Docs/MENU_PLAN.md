# WiiStation menu plan: one standard for every screen

This file holds everything about the menus (merged 2026-11-04):
- **Part 1** (below): the standard every screen follows, what the code does today and the work to get there.
- **Part 2**: the menu framework plan of 2026-10-09 and what got built (steps 1-6, merge 50ee0b0b).
- **Part 3**: save-state slots with pictures (was `SAVE_STATE_THUMBNAILS.md`; not built).

The cheats page is in `Docs/CHEATS_PLAN.md`; it is built on this standard (it needs List groups
and the Range row).

## Part 1. The standard (2026-11-04, for review)

The goal (Monty, 2026-11-04): every screen, menu, page and tab is built one way, looks consistent
and is easy to add. Earlier question (2026-11-01): "How is the menu framework not keeping things
consistent in the settings menus? Why isn't it easy to place names next to buttons?"

The answer is in the code: the engine exists, but each frame passes its own numbers to it. There
are three label alignments, about 20 hand-typed button widths, 6 row pitches and 4 button heights
(numbers in 1.5 below). The 2026-10-11 decision "every page keeps its look" (Part 2) is why: the
port kept each page's old coordinates. This part is the next decision: one look.
**Nothing below is built yet. The contact sheets show it before any code changes:**
- `.runs/menu_today/today_sheet.png`: every screen as it looks today (Dolphin).
- `.runs/menu_standard/standard_sheet.png`: each screen type on the standard (mock-ups).

### 1.1 The screen frame (all screens)

640 x 480, 4 px kept clear at every edge. Every screen has the same three bands:

| Band | y | What is in it |
|---|---|---|
| Header | 16-72 | a **tab strip** (tab pages) or a **title** at the left (other pages); nothing else |
| Body | 88-392 | the screen type's content (below) |
| Footer | 400-472 | the **help block** (two columns, term and text) at the left; the **logo box** (72 x 72) at the bottom right; one line of **button hints** (A Select, B Back, L/R Tab, - / + Page) at y 452 |

The logo sits in the footer on every screen except Main, where it stays top right. The engine
owns the logo box; no row can enter it (`MENU_ROW_RIGHT`).

### 1.2 One row grid for every form

- **Label column**: right-aligned, ending at x 272. One rule for every page (today: centred at
  150 or 160, centred at a per-page x, or right-aligned).
- **Control column**: from x 288 to x 616 (328 px).
- **Two densities**: Regular rows (button 52 high, pitch 58, 5 rows) and Compact rows (button 40
  high, pitch 46, 6 rows). A page picks one; a page with more rows than fit becomes two pages or
  a tab, never a third pitch.
- **Widths come from the text**: the engine measures the text with the menu font and rounds up to
  a step of 8 px. No page types a width. A row that does not fit in 328 px is a layout error (the
  host check fails), not a smaller font.

### 1.3 Row kinds (one look each)

| Kind | Look | Today |
|---|---|---|
| Toggle | two segments, Off / On, one lit | a lit Yes/No pair, a single On/Off button, or a cycle button, depending on the page |
| Choice | 2-4 segments of equal width (the widest text + 32 px), one lit; more than fit becomes a Cycle | typed widths per page |
| Cycle | one 200 px button, value in the middle, small arrows at both ends; Left/Right step it | the same on Options; Settings draws some as rows of typed buttons |
| Range | `-` value `+` (48 + 120 + 48) | not built |
| Buttons | action buttons from the control column (or centred on menu screens), gap 12 | gaps of 0, 10, 12, 15, 20, 30 and 38 |
| Readout | label and value text, not focusable | as is |
| List | rows 36 high, pitch 40, a scroll bar, "Page 2/5" in the footer | one list per page, text only |
| Help | the footer block, from the row's help text | as is |

### 1.4 The screen types

| Type | Screens | Body |
|---|---|---|
| A. Form with tabs | Settings (5 tabs), Advanced (GPU, Input, SPU), Achievements options | rows on the grid |
| B. Form with a title | the Options pages (Sound, Graphics, Plugins, CD, Memory) | type A with a title instead of tabs |
| C. Action menu | Main, Load ROM, Current ROM | one column (280 px buttons; centred at x 288 on Main to clear the logo, at x 320 elsewhere) or two columns (240 px, gap 24); pitch 58 |
| D. List | File browser, account chooser, cheats | the list fills the body |
| E. List with detail | Achievements (list + badge and text), save-state slots with a picture (Part 3) | list x 24-392, detail panel x 408-616 |
| F. Detail / readout | ISO Info, achievement detail, USB Pad Test, Memory | Compact readout rows, an optional picture at the right |
| G. Picture editor | Configure Buttons, Configure Input, Customize Co-Op | the controller picture in the middle; Compact buttons in a left and a right column on an 8 px grid, a line from each to its place on the picture; the controller is picked on the tab strip; focus from geometry |
| H. Card board | Players (port cards, the Mii coin strip) | 4 port cards in a row (136 x 200, gap 16), the coin strip below |
| I. Dialog | MessageBox questions and notices | the screen behind dims to 50 %; a box 440 x 220 in the middle, title, text wrapped at 400 px, buttons in a row at the bottom right |
| J. Toast | unlocks, Save Block notices | the PS1 styles in `Docs/design/PS1_STYLES.md`; not a page |

### 1.5 What the code does today (inventory, 2026-11-04)

**The engine** (`MenuEngine.h` 441 lines, `MenuEngine.cpp` 1071): `Page { tabs | title, rows,
help, logo, labelCx, labelDy, rowDy, rowY0, overrides, freeButtons, freeTexts, freeLinks, wrap,
columns }` -> `layoutPage()` -> up to 96 controls -> `linkFocus()`, once per page show and
host-testable (`tests/menu_engine_test.cpp`). Row kinds: TOGGLE, CYCLE (one code path,
`MenuEngine.cpp:467-480`), CHOICE, READOUT, BUTTONS, LIST (one per page). `checkLayout()` finds
overlaps, rows in the logo box, text too wide, unreachable controls and bad links. Missing: Range,
engine-owned dialogs, List groups and option rows, an input step (the frames poll the pads), and a
binding from a row to its setting.

**Per frame:**

| Frame | Lines | Engine | Type | Notes |
|---|---|---|---|---|
| SettingsFrame | 1823 | via its own adapter | A | 85 buttons made up front (a page shows at most 18); SetRow tables carry typed widths and x0 295/253/252/85/323; ~70 `Func_*` click handlers; tab input polled in `drawChildren` (808-827); button indices are part of the `menuclick` script interface |
| OptionsFrame | 1072 | via a second adapter (`OptRow`) | A, B, F | its own widget pool; readout functions inline |
| MainFrame | 450 | BUTTONS | C | holds `Func_PlayGame` (game start, a nested `Gui::draw` loop, raw pad polling): game logic in a menu file |
| LoadRomFrame | 284 | BUTTONS | C | buttons 340 wide |
| CurrentRomFrame | 434 | BUTTONS, 2 columns by hand | C | x0 100/150, widths 230/220/120, 4 link overrides; a copy of the debug layout log |
| FileBrowserFrame | 878 | LIST | D | Prev/Next at hand positions; ~150 lines of its own pad polling for every pad type (MenuGlue::tabShoulder exists); file loading in the frame |
| ConfigureInputFrame | 718 | free buttons | G | 70 buttons by index, macro grids (`CI_ROW_Y` 20+62r, ...), macro click handlers, a hand link table on top of the engine's |
| ConfigureButtonsFrame | 1177 | free buttons, geometry focus | G | **lays out the page and refits every mapping button again every frame** (`updateFrame` -> `activateSubmenu`; `drawChildren` -> `fitMappingButtons`); picture lines from literal arrays |
| CustomizeCoopFrame | 310 | free buttons, geometry focus | G | the model for geometry focus |
| PlayersFrame | 769 | none | H | own pad polling |
| AchievementsFrame | 965 | none | E | own pad polling |
| UsbPadTestFrame | 632 | none | F | own pad polling |

**Why the pages differ** (the numbers): label centre 160 (engine), 150 (`TAB_LABEL_CX`, Settings),
110 moved left by the widest row (choice rows), or right-aligned (two-column pages). Control x
295, 205, 253, 252, 85, 323, 100, 150, 315 or centred. Widths typed by hand: 55, 70, 75, 90, 95,
100, 105, 110, 117, 125, 130, 135, 140, 152, 160, 165, 170, 171, 220, 235 (Settings), 200 (Main),
340 (Load ROM), 230/220/120 (Current ROM). Gaps 10 (`TAB_GAP`) and 12 (`CHOICE_GAP`), and 15, 20,
30, 38 per row. Pitch 60, 70 (Audio, Saves), 46 (Memory), 54 (GPU), 62 (Configure Input). Heights
56 (engine; `Button.cpp:75,82` forces it), 40 (Configure Buttons), 30 (Co-Op). The same kind of
setting looks different on two pages (a cycle is a 200 px button on Options and a row of typed
buttons on Settings). The logo spot is chosen in two places (`Page.logo` and `MenuContext.cpp`
`LOGO_BOTTOM_FRAMES`).

**Duplication:** two adapters do the same engine-to-widget mapping (Settings, Options), plus about
11 more `xxxButtonOf(control)` / layout functions, one per frame; the debug layout log is copied
twice; pad polling is copied in 6 frames; spare Settings buttons are kept by index (a spare FPS
Limit, 3 audio Yes/No, 5/6/58/64/65/66, Test USB Pad) because `menuclick` names indices.

**Cost:** `MenuContext.cpp:77-88` makes all 12 frames and their widgets at start; Configure
Buttons' per-frame layout (above). Not measured yet: `IPLFont` text conversion per draw, the
menu's MEM1/MEM2 use (`Docs/MEMORY_MAP.md`).

**Hazards still open:** `Button` forces height 56; input polled in draw code (Settings, Options,
FileBrowser, Main, Players, Achievements, USB Pad Test); nested draw loops (`Func_PlayGame`,
MessageBox). Not checked yet: the pointer clicking hidden-but-active buttons (CursorManager), the
focus loop guard in `Button.cpp:383-420`.

### 1.6 The work, in steps that each ship

1. **Audit** what the inventory did not read (libgui Button, CursorManager, FocusManager,
   IPLFont, MessageBox, TextBox, Frame; MenuContext; Players, Achievements, USB Pad Test; the host
   test's coverage; the menu's memory). Measure the menu's per-frame CPU on a Wii.
2. **One adapter.** The engine creates and maps its own widgets (a pool per page, made when the
   page is shown); every frame drops its `xxxButtonOf` and layout functions.
3. **The standard in the engine**: `MenuLayout.h` holds the bands, the grid and the two densities;
   the engine measures text and computes every width; frames pass no coordinates. `Button` takes
   any height. The Range row. List groups and option rows. Engine-owned dialogs (type I) replace
   the nested MessageBox loop.
4. **One input step** in the engine (pads, tabs, pages, pointer, with the visibility rules); the
   frames' own pad polling goes.
5. **Port by type**, each screen before/after on the contact sheet (`wsx.sh menu`): A and B
   (Settings, Options), C, D, E (Achievements), F, G (picture editors; fix the per-frame layout
   first), H (Players).
6. **Bindings**: a row names its setting; label, choices, help and when it applies come from the
   settings table (Part 2, step 6 started this). `menuclick` moves from button indices to setting
   names, so the spare buttons can go.
7. **Lazy frames**: a frame is built when first opened, not at start, if the audit shows the memory
   is worth it.

Rules for the work: the release build must cost no more than today on the Wii (layout once per
page show; drawing stays the GX path); translations keep working (`lang/*.lang` keys); every
screen is screenshotted before and after; nothing is removed before its replacement works on a Wii.

## Part 2. The framework plan as built (2026-10-09 .. 2026-11-03)

**Status (2026-11-03): steps 1 to 6 are built and merged** (`try/menu-framework`, merge
50ee0b0b, 2026-10-12); the line below is the 2026-10-09 text. Evidence: step 1 `MenuEngine` core and
OptionsFrame (b7bb07fe, dba48334); step 2 Settings tabs General, Video, Input, Audio, Saves and
focus links from the engine only (1f6a32cf, e47cf234, 779d5d6c, dbb34150, cc8b23d3, af511141);
hidden and inactive buttons never targets (8aa879ee); step 3 Main, Load ROM, Current ROM
(8288766d, eb764785, f1f31cd2); step 4 List rows and the file browser (ce8218c7); step 5 Customize
Co-Op, Configure Input, Configure Buttons (231bdde2, aa506cb4, 3a1d5dde); step 6 one settings
table for label, choices and help (a39ed8f7, 9c15c81a, 2598141d). Later additions: optional edge
wrap (46e0fde6, b23d8601), two-column pages (11a1a2a2), a compact four-column layout (4b218204,
268bfb66). `scripts/menu_text_width.py` no longer carries `TAB_ROWS`. Not done or unverified:
the Range row (not in `MenuEngine.h`), modal dialogs owned by the engine (no sign of it), save-state
slots as a List (unverified), the Cheats page (nothing built, see CHEATS_PLAN.md).
`PlayersFrame` and `AchievementsFrame` do not use `MenuEngine` (grep of `Gamecube/menu`).

The original text follows. The goal (Monty, 2026-10-09): one robust way to build every menu --
tabs, columns, rows, rows that are only buttons, a safe place for the spinning logo, D-pad
traversal that always works, on/off buttons, cycle buttons, and one-of-several choice buttons --
instead of hand-placed coordinate tables.

### Where we are

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

### The design

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

### Migration, in steps that each ship
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

### Learned since the first draft (2026-10-10)
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

### Proposed order (answers question 1 below, if agreed)
1. Engine on OptionsFrame (rows, Toggle, Choice, Cycle, Readout, Buttons, Help, generated focus,
   tabs, logo box, host layout check).
2. **List + Range** rows and modal dialogs -> the Cheats page lands here.
3. Settings tabs one by one; 4. Main/LoadRom/CurrentRom; 5. FileBrowser and save-state slots as
   Lists; 6. Configure Input/Buttons/Co-Op grids; 7. one settings table.

### Decided (Monty, 2026-10-11)
- Order of the projects: libogc3/devkitPro update first (Docs/done/LIBOGC3_PLAN.md), then this framework,
  all steps to the end, then cheats.
- Translations stay: English by default, a language only where contributors provide it
  (`lang/*.lang`); new labels get keys so they can be translated.
- Every page keeps its look: the framework straightens the code, not the layout. Rearranging comes
  after, as its own decision.
- Performance: the engine lays out and links focus when a page is shown, not per frame; drawing
  stays the existing GX path. Anything that would cost a measurable amount of MEM1/CPU is put to
  Monty before it is built.

### Questions for Monty
1. Is the order right? (Cheats need the List row early -- step 4 could move before step 2.)
2. Translations: `lang/*.lang` has no entries for newer labels (Advanced, Memory, ...). Keep the
   gettext files and add entries, or drop translations for new rows?
3. Should two-column pages (e.g. CurrentRom) keep their look, or move to one column of Buttons rows?
4. Any screen you want redesigned rather than ported as-is?

## Part 3. Save-state slots with pictures (was SAVE_STATE_THUMBNAILS.md)

**Status (2026-11-03): not built.** `GPU__getScreenPic` in `Gamecube/plugins.c` still returns -1 and
no `getScreenPic` exists in the GPU code (grep). The achievement unlock pictures (`ach_shot.c`) are
a separate feature and do not fill the state file's picture area.

The slot picker on the Current ROM screen says "Slot 3" and, since the last round, "Slot 3*"
when that slot holds something. It cannot say *what* it holds. Ten slots per game and no way
to tell them apart is the complaint this would fix.

The file format already has room. Every state file reserves 36,864 bytes right after the
header for a 128×96 RGB picture, and `LoadState` seeks over it. Today those bytes are zeros.
So nothing about the format has to change — this is purely about filling them in and drawing
them.

### Why it isn't a small job

The obvious call, `gpuPtr->getScreenPic(pMem)`, is commented out in `SaveState`. It has been
commented out for a long time, and it would not compile if you uncommented it: `gpu_t` in
`gpu.h` has no `getScreenPic` member. The only implementations anywhere in the tree are
`GPU__getScreenPic` in `plugins.c`, which returns −1, and `GPUgetScreenPic` in PeopsSoftGPU,
whose whole body is `// Unsupported`.

There is also no single place to read the picture from. Each of the three renderers keeps its
own copy of the display geometry — `PSXDisplay` is declared separately in `SoftGPU/gpulib_if.c`
and `PeopsSoftGPU/gpu.c`, and GlesGpu has its own. Without that struct you do not know where
in the 1 MB of emulated VRAM the visible picture starts, or how big it is.

### The three pieces

**1. A way to ask the renderer for the picture.**

Add to `gpu_t` in `gpu.h`:

    void (*getScreenPic)(unsigned char *rgb);   /* 128 x 96, 3 bytes per pixel */

Implement it in all three renderers. The body is the same shape each time: take
`PSXDisplay.DisplayPosition` and `PSXDisplay.DisplayMode`, walk that rectangle of
`psxVuw` (the emulated VRAM, 16-bit BGR555), box-average down to 128×96, write RGB.
Roughly 40 lines per renderer, and the only thing that differs is which `PSXDisplay` it
reads.

**Check this first**: under OpenGX the drawing goes through the Wii's GX hardware, and the
emulated VRAM is only brought up to date when something reads it back. If the VRAM is stale
at save time, the thumbnail will be a frame or two old, or blank. Write the capture, save a
state in a game, pull the 36,864 bytes out of the file with a script, and look at them as a
PNG on the PC. If it is blank under OpenGX and right under the software renderers, that is
the answer, and the fix is to force a readback before capturing.

**2. Call it.** In `SaveState`, where the zero buffer is written, call
`gpuPtr->getScreenPic()` into a 36,864-byte buffer and write that instead. A renderer that
has nothing to give leaves it zeroed, which is what the file holds today, so nothing breaks.

**3. Draw it.** This is the fiddly part, because GX does not take a flat RGB buffer.

- Read the 36,864 bytes from the state file. They start at offset 33: 32 bytes of header,
  then one byte of `Config.HLE`. `StateExists` in `misc.c` already opens a slot's file and
  reads the header; extend it, or add a sibling next to it.
- Convert 128×96 RGB888 to RGB5A3 in GX's 4×4 tile order. There is already an example of
  this in the tree — `glTexSubImage2D` in `deps/opengx/gc_gl.c` does the same tiling for
  the game's textures.
- Hand the result to `menu::Image` (`Gamecube/libgui/Image.h`), which wraps a `GXTexObj`,
  and draw it with `Graphics::drawImage` from `CurrentRomFrame::drawChildren`.

The Current ROM screen has room at roughly x 520–650, y 240–340, to the right of the Load
State and Save State buttons. `scripts/menu_text_width.py` will tell you if that runs into
the spinning logo.

### Cost and order

Piece 1 is the risky one and the cheapest to test on its own, so do it first and prove the
picture is real before writing any menu code. Pieces 2 and 3 are straightforward after that.
Call it a day's work if the OpenGX readback behaves, two if it does not.

### Worth knowing

- `menu::Image` has to keep the converted buffer alive for as long as the texture is used,
  and it must be 32-byte aligned and flushed from the CPU cache before GX reads it.
- Only one thumbnail needs to exist at a time — the one for the selected slot. Rebuild it
  when the slot changes, the same way the `Slot 3*` marker is rebuilt now.
- A state written by an older build has zeros there. Drawing a black rectangle is fine;
  do not treat it as an error.
