# Menu forms plan: every page from six general forms (draft, 2026-11-07)

Why: the engine (MenuEngine: pure layout, host-tested) is sound, but every frame rebuilds the same 200-300 lines of
adapter around it by hand (button index maps, widget pools, focus links, footer, pad polling, draw loop), and several
tables must stay in step with nothing checking them. Pages are made from a handful of general forms; the code
should say so. Sources: `.runs/notes/MENU_FRAMEWORK_AUDIT.md` (first audit) and the page-by-page audit below
(both read-only reviews of this worktree, 2026-11-07; line numbers are this worktree's).

## 1. Every page today

| Page | File (lines) | Body | On the engine? | Input |
|---|---|---|---|---|
| Main | MainFrame.cpp (428) | 7 action rows, controller status | rows, hand geometry | engine; Play Game has its own draw loop and pad reads (301-310) |
| Current ISO | CurrentRomFrame.cpp (402) | 10 actions in 2 columns, Slot cycle | declarative table (70-82) | engine |
| Settings: System, Video, Input, Audio, Saves | SettingsFrame.cpp (1968) | sub-page buttons, choice, flags, cycle, actions; help line | standard; rows name buttons by index (590-683), values written by position (541-583), 91 buttons made at start, 178 `Func_` | engine + shoulder keys |
| Advanced CPU/GPU/SPU/CD/Input, Adv Sound, Adv Graphics, Memory, Achievements settings, ISO Info | OptionsFrame.cpp (1133) | cycle/radio/readout/action rows | declarative `OptRow` bound to setting keys; 4 pages still on the old grid | engine |
| Explorer (Load ISO, Swap CD) | FileBrowserFrame.cpp (2137) + LoadRomFrame.cpp (210) | source tabs, path, letter strip, list, facts strip, cover | list on the engine; Func_Select1..8 copies | own polling: Z, L/R, C stick, pointer, thumb (1214-1380) |
| Account picker | AccountFrame.cpp (263) | titled list | list; acPick0..7 copies | own L/R polling |
| Achievements book + detail | AchievementsFrame.cpp (965) | custom full-screen drawing | no | libgui focus + own pointer |
| Players + Mii Edit | PlayersFrame.cpp (769) | card board | no | own L/R/+/-, A+B drag |
| Configure Input | ConfigureInputFrame.cpp (717) | 2 port columns | free buttons, 46 copied `Func_` | engine geometry |
| Configure Buttons | ConfigureButtonsFrame.cpp (1171) | pad picture + mapping buttons | free buttons; **layout runs every frame** (652-663) | engine |
| Customize Co-Op | CustomizeCoopFrame.cpp (295) | presets + picture | free buttons | engine |
| USB Pad Test | UsbPadTestFrame.cpp (632) | step wizard over a pad picture | no; raw drawing | own |
| Dialogs (Quit, Credits, notices) | libgui MessageBox | text + Yes/No | no; nested loops, 77 callers | own loop |
| Toasts | ach_toast.c, fadeMessage | overlay | not a page | - |

## 2. The six forms

| Form | What a page author writes | What the form owns | Pages |
|---|---|---|---|
| **Form** (presets: tabbed, titled, readout, action menu) | a table: `FORM("Advanced Sound", DENSITY_REGULAR) { ROW_SET("CpuSpeed"), ROW_INFO("MEM1", infoMem1), ROW_ACTIONS(ACT("Save", saveCfg)) }` | layout once per show, widget pool, labels, help line from the setting's own help text, focus, L/R tabs, B back, drawing | Settings x5, Advanced x5, the 4 titled pages, ISO Info, Memory; Main and Current ISO with fixed geometry (look kept) |
| **List** | `LIST(tabs or title, items, onPick(index), onBack)` | list window, paging, arrows, scroll thumb, cut names | explorer, Account |
| **Editor** | a table of free buttons `{id, text, rect}`, `drawPicture()` | focus by geometry, pool, click by id, relayout only on change | Configure Buttons, Configure Input, Co-Op |
| **Board** | `draw(gfx)`, `onInput(input)` | one input step, hint line, shared fill/text helpers (copied in two frames today) | Achievements book, Players, USB Pad Test |
| **Dialog** | `dialogAsk(text, onYes)`, `dialogNote(text)` | modal on top of the page, no nested loop | Quit, Credits, every MessageBox |
| **Toast** | `toastPush(style, text)` | one queue | unlock toasts, Save Block notices |

Escape hatches, kept on purpose: a custom row for two-setting rows (Sync, Memcard Type); page enter/leave hooks
(the CPU/plugin choice); List hooks for the explorer's letter strip, facts strip and Z sort; Editor pages keep their
picture drawing; Board pages are custom by design.

## 3. The shared code

| Piece | What it replaces |
|---|---|
| `MenuPageView` (glue): pool, labels, help line, focus links, draw loop for any Form or List | the adapter each frame writes by hand |
| click context: `Button` calls `fn(void *ctx, int tag)` | acPick0..7, Func_Select1..8, the copied CI functions, most of Settings' 178 handlers, button index maps |
| `glue::input()`: all pads read once per frame into one struct (direction, A, B, shoulders, page keys, pointer, C stick, Z) | 6 frames' own pad polling |
| page tables in `Gamecube/menu/pages/*.h`, compiled by both the Wii build and the host test | the test's copies of the tables (they can drift) |
| rows bound by setting key (`ROW_SET("CpuSpeed")`): label, choices and help come from the settings table | index-coupled rows, separate help arrays |
| `glue::logProblems` | two copied debug logs |

Expected sizes: Account 263 -> ~80, Settings 1968 -> ~600, Options 1133 -> ~450, Configure Input 717 -> ~350,
explorer 2137 -> ~1700 (data and file reading split out), Main/Current ISO/Book/Players -60..-100 each.

**Performance rules** (the menu already costs +26-31% CPU per frame on a real Wii; this work must not add to it):
layout once per page show or value change, never per frame (this also fixes Configure Buttons); widgets from a pool
made once, never new/delete on show; no text formatting per frame (the help line only when the focused row changes);
pads read once per frame; drawing stays the existing GX path; add a text-width cache (each centered label allocates
twice per draw today). Each step measures the menu's CPU on the bench Wii before and after.

## 4. Steps (each one ships and is proved by one Dolphin walk + one contact sheet)

"=" means the look must not change: `wsx.sh frames` must show 0 differing frames against the step before.

| # | Step | Deletes | Proof |
|---|---|---|---|
| 1 | Baseline: one walk over every page (frame dumps kept), menu CPU per page on the bench Wii, host test run by `wsx.sh build` | - | the baseline sheet |
| 2 | = Page tables into shared headers; the host test includes them | the test's copies | host test, 0 frames |
| 3 | = Click context, `MenuPageView`, `glue::input()`; Account first | acPick*, acButtonOf, Account's polling | 0 frames on Account + explorer |
| 4 | = Options onto the page view, all 10 pages | OptWidget, its footer code, the debug log copy | 0 frames on every Options page |
| 5 | = Rows bound by setting key; Settings tab by tab; `menuclick` kept working (see question 46) | most `Func_`, index rows, the 91 buttons made at start | 0 frames on the 5 tabs + menuclick scripts |
| 6 | = Main and Current ISO as fixed-geometry Forms; Play Game's loop out of the menu file | hand geometry | 0 frames |
| 7 | = Explorer on List with its hooks; LoadRomFrame folded in | the 1214-1380 polling | 0 frames on the explorer walks |
| 8 | **Look change (approved):** Adv Sound, Adv Graphics, Memory, Achievements settings onto the standard; ISO Info with the disc band (I4) | the old grid fields they use | before/after sheet |
| 9 | = Configure Buttons lays out on change only; then Editor for the 3 picture pages; then G2's top (look change) | per-frame layout, copied functions | 0 frames, then a sheet for G2 |
| 10 | = Board helpers + input for the book, Players, USB Pad Test; then the book polish (look change) | copied helpers, own polling | 0 frames, then a sheet |
| 11 | Engine-owned Dialog (riskiest: nested loops and 77 callers) | MessageBox loops | 0 frames on Quit and notices |
| 12 | Delete the unused old-grid fields; build pages on first open if memory is worth it | old `grid::` path | host test, 0 frames |

Work method (from the token log): the main session writes the code directly; agents only for long Dolphin runs and
wide read-only reviews. One step per commit on `try/menu-standard`.

## 5. Monty's answers (2026-11-07)
- 45: Main and Current ISO get their own form (an action menu with the new disc picture), not a fixed-geometry
  escape in Form. So seven forms: Form, ActionMenu, List, Editor, Board, Dialog, Toast.
- 46: `menuclick` by button number goes; scripts move to a new, better method (by setting key and choice, e.g.
  `menuclick FPS 1`, and by page/action name for actions). Update every script in scripts/autoinput and .runs inputs.
- 47: USB Pad Test is an Editor (pad picture + its step text), not a Board.
- 48: Configure Input is an Editor. Rule: base forms exist so no page is forced into a form that does not fit.
- 49: open (see the session's answer: keep the blocking `askMessage` API, rebuild its inside on the shared input and
  draw, so the 77 callers do not change).
- 50: the Range row (- value +) is built when first needed; the first user is the DSP master volume.

## 6. Progress (2026-11-07)
- Step 1: `wsx.sh build` runs the host tests first (`WSX_NO_HOSTTEST=1` skips); `scripts/menu_pages_walk.sh TAG A.dol [B.dol]` photographs
  (or compares) every page, frames kept in `.runs/menu_TAG_pN`. Menu CPU per page on the bench Wii: NOT done, no lab mode measures it yet
  (a new run mode goes to the dev Wii first).
- Step 2 (Options half): the Options tables live in `Gamecube/menu/pages/OptionsPages.h`, included by OptionsFrame.cpp and by
  `tests/menu_pages_test.cpp` (hooks stubbed; checks ids, titles, tabs, return targets, row/footer counts, keys). Footer length against
  rows is a static_assert. The old copies in tests/menu_engine_test.cpp still pin the Settings tabs and three Options pages for layout;
  the Settings tables move with step 5 (they are index-coupled to FRAME_BUTTONS and cannot be shared before that).
- Step 3 (Account) and step 4: `glue::ListView` (Account), `glue::PageView` (MenuPageView.h/.cpp: title, tabs, row widgets, help,
  footer, click context, layout owner). OptionsFrame.cpp 1133 -> ~540 lines with the tables in the header.
- Step 5 (2026-11-07, Settings by setting key): the five tabs are tables in `Gamecube/menu/pages/SettingsPages.h` (shared with the host
  test). A row is a list of buttons; a button is bound to its setting by key: `SEG_SET(key, value)` (a choice), `SEG_FLAG(key)`,
  `SEG_CYCLE(key)`, or `SEG_ACT(name, text, fn)` (no setting). Text, lit state and the click (write the setting, then an optional `apply`)
  come from the key; a button may replace the click (`act`), the lit test (`lit`) or the text (`textFn`) where the setting alone is not
  enough (BIOS device, language, Frame Rate order, the inverted XA/CDDA keys, Sync's two settings, memory card types).
  `PageView` now has one widget set per row (cycle, action and choice buttons are the same pool) and can take lit/inactive from the layout.
  SettingsFrame.cpp 1968 -> ~880 lines: FRAME_BUTTONS (91 buttons made up front), 38 FRAME_STRINGS, ~45 select-and-write handlers and the
  dead General-tab buttons (CPU, GPU, Dithering, Memcard 1/2, Fast Load) are gone; the page makes 41 widgets (9 rows x 4, 5 tabs).
  `menuclick` takes a name now: `menuclick FPS 1` (a choice: the button with that value), `menuclick SaveSettingsSD 1` (an action: n
  presses). scripts/settings_check.py checks all 44 keyed buttons against the OPTIONS table, values included.
  One visible difference by design: a cycle button is no longer left lit by its own click (the old handler set it selected and nothing
  cleared it until the tab was shown again).
  Proof (2026-11-07/08, forms4 against forms5b over the 22 pages): 0 differing frames on 20 pages. Memory differs by its live MEM1
  readout (expected). Input differs in one strip: Light Gun was the only cycle with the select art (an unused strip under the
  button); it is plain like Auto Load Slot now (a consistency fix, not carried over). Save Configs SD/USB kept the plain art
  (`PageView::FLAT` mark, 0 frames). By-name clicks checked on the DOL: `menuclick ScreenMode 1`, `Interlaced 1`, `LightGun 1`,
  `ControllerType 1`, `FPS 1` all change the page as pressed. `autoinput_menuclick` holds ONE click: the last line of a script wins.
  Not measured: menu CPU on the bench Wii; release build.
- Step 6 (2026-11-08, ActionMenu): Main and Current ISO are tables in `Gamecube/menu/pages/ActionPages.h` (a button = text, click,
  row, slot, width, dy; a row = x0, gap) built by `glue::ActionMenu` (MenuPageView.h/.cpp: layout once, buttons from the pool,
  focus links, `setText` for the Play/Resume and Slot labels, layout log under PERF_PROF). MainFrame.cpp 428 -> ~350,
  CurrentRomFrame.cpp 402 -> ~280. The host test checks both tables. Proof: Main's first frame and pages 16 (Current ISO), 17, 1
  against step 5: 0 differing frames. Play Game's loop is still in MainFrame.cpp (it moves out with step 11's Dialog work).
  Not measured: bench-Wii menu CPU; release build; Resume Game label by hand (the code path is the old one, via setPlayLabel).
- Step 7 (2026-11-08, Explorer on List): the explorer is a `glue::ListView` with source tabs (`setTabs`), its own list state
  (`useState(&FB_LIST)`), a hook that sets the lit tab and the path/count line before each layout, and one that turns the page
  its own way (focus past ".."). ListView gained the explorer's label colours (folder gold, dim Recent rows), tab buttons and
  links, `refresh()` (default focus, or the lit tab on an empty list), `item(k)`/`itemRow()` for the one-row step, and
  `update()` returns the keys. `glue::readInput()` now reads the sort key (GC Z, Wiimote 1, Classic ZR, HID R1) with the turn,
  so the explorer's own polling (the three copied pad loops), Func_Select1..8, Func_PrevPage/NextPage, its button table and
  its layout/apply/lookup code are gone. LoadRomFrame.cpp/.h, which held only the device sources, are
  `ExplorerSources.cpp/.h` now. FileBrowserFrame.cpp 2137 -> 1803 lines.
  Proof (forms6 vs forms7): pages 70, 18, 16 and the Account walk 0 differing frames. `scripts/autoinput/explorer_walk.txt`
  (11 Frogger formats staged with WSX_ISOS, see its header): identical through opening the folder and nine Downs (the
  one-row step at the page end); from the first scripted shoulder key on they differ by design: the old explorer read L/R/Z
  from the pads only, so script keys never reached it; the new one turns the page (R, L), sorts (Z twice: A-Z, folders first),
  climbs to the tabs and opens USB as the keys say (stills checked).
  Not done: pointer clicks and the thumb drag by hand (code unchanged, fbPollPointer), a real Wii, the release build.
- Step 9 (2026-11-09, Editor; 9a and 9b, no look change): 9a: Configure Buttons synced and fitted every frame (30 string copies, a
  `fitToLabel` per mapping button); `updateFrame` now hashes what `activateSubmenu` reads (pad, pad list, controller, config bytes, gun
  settings) and syncs and refits only when the hash changes (the engine layout was already guarded by the inactive mask).
  9b: `glue::Editor` (MenuPageView.h/.cpp, +214 lines): a frame `add`s each free button (id, art, text pointer, rectangle) and label once; the
  form makes the pool, gives every button `click(user, id)` and the back function, owns focus (engine geometry rule over the shown buttons, the
  author's `link()`s over it, optional `setWrap`) and lays out only when `relayout(withState)` is called (shown/active/lit from `item(id)`,
  label visibility, an optional engine title). The frame keeps its picture and its own `drawChildren`. Moved: Configure Input 717 -> 564
  (46 `Func_` handlers and the table's click/return columns, the layout/links/pool code -> one `ciClick(id)`), Customize Co-Op 295 -> 230
  (24 handlers, layout code -> `coopClick(id)`), Configure Buttons 1171 -> 1131 (layout code, pool; the table's own 25 handlers stay and are
  called by `cbClick(id)`, they are not copies; focus reads active state from the buttons and passes `relayout(false)`).
  Proof (release DOL step 8 base against 9b): pages 20, 21, 22, 40, 41 0 differing frames; menukeys walks on 40, 20, 21 (focus moves and A
  presses): the same frames move in both DOLs, 0 differing frames (9a and 9b). Host tests and `settings_check.py` pass.
  USB Pad Test left alone: it has no widgets at all (raw drawing of a pad picture, text lines and a ring, state machine in `updateFrame`, the
  Select function for A), so an Editor would be an empty table; it moves with step 10's Board.
  Not done: 9c (G2's top: pad Cycle, Light Gun Off|On, slot Cycle with Default, Load, Save apart, Built-in): it needs a stored "default
  config" distinct from the shipped `config_default` (Save on Default overwrites it, Built-in restores the shipped one) and its file
  format, so it wants its own session; menu CPU on the bench Wii not measured; the Editor on hardware by hand.
- Step 10 (2026-11-09, Board; 10a only, no look change): `glue::Board` (MenuPageView.h/.cpp, +19/+38 lines): a component whose
  `drawComponent` takes ONE input step (`glue::readInput()`, the shared pad reader) and then calls the page's `paint()`; a page overrides
  `onInput(PageInput)` and `paint(gfx)`. Shared helpers in glue: `text` (optional fit width), `width`, `textCenter`, `hintLine` (white on the
  dark band); `fill` was already `glue::fill` (MenuGlue.h). Moved: the book (AchievementsFrame.cpp 965 -> 925: its own `switchPlayer` pad
  loops, `fill/text/width/textCenter/footer` copies; the player switch now takes the turn from the input step, the menu-combo guard stays),
  Players (769 -> 751: the page-turn pad loops and the `fill/text` copies; the turn is read in `onInput` and applied at the old place in
  `paint`, so the order with `layout()` and the Edit page is unchanged), USB Pad Test (632 -> 645: a `UsbPadBoard` whose `onInput` is the
  per-frame tick that `drawChildren` made, `paint` is the old drawing; it has no pad polling to delete). Players' `fill` now blends like the
  book's (no visible change). Wii +/- and GC L/R now also come from the shared reader's WUPC/HID sources.
  Proof (release DOLs `ws_step10_base_release.dol` = cd025e19, `ws_step10_a_release.dol`): `PAGES="15 18" menu_pages_walk.sh` 0 differing frames
  (page 15 has no game there: its "no set" line); `account_walk.txt` (Players, pick up a Mii, Edit slot, Account list) 0 of 1203 frames;
  `ach_book.txt` run with the ach_book fixture (list, tabs, detail, next, back, other player; 5943 frames) `wsx.sh frames` 0 differing;
  USB Pad Test: `menupage 3`, `menuclick TestUsbPad 1 50`, menukeys A A B (no USB pad: the "plug one in" line) 0 of 1224 frames.
  Host tests and `settings_check.py` pass.
  Not done: 10b (the book polish: "today's layout, polished, full screen with a hint line" has no written spec beyond MENU_PLAN sheet 2;
  needs the design and a contact sheet); L/R page turn in Players (needs more than 16 Miis); USB Pad Test with a real pad; menu CPU on
  the bench Wii; hardware.
- Step 10b (2026-11-09, the book polished to sheet 2 E1-E6; try/menu-book, AchievementsFrame.cpp only). MONTY'S CHANGE: the tabs keep
  today's bookmark look (the selected tab joins the page), NOT the flat standard tabs of E1-E3: tab strip 0 differing pixels on the
  three list panels. List: "< Player 1 of 2 >" (two drawn triangles, the font has no such glyph) at the middle of the header beside the
  name, which shrinks to keep clear of it; the line under the bar is gone (the note / rich presence line stays). Rows: Earned shows the
  unlock date under the points, Locked shows "12 % have it" (whole from 10 %, one decimal under, formatted `rarityText`), In progress keeps
  bar + count (no rarity there, as in E1). Detail: 88 px tile + full title (two lines, 1.0 scale), "10 points - Missable - 61 % have it"
  in amber under it (the font has no middle dot: " - "), description left (5 lines), at the bottom of the column the count + bar or
  "<player> earned it" + date, the picture at 320,128 296x222 with "Taken <date>" under it (no band), "No picture yet / taken when it unlocks"
  box; the divider line is gone. All detail text and the header lines are formatted when the row / page changes (`formatDetail`, `build`),
  not per frame. Off (E6): "Achievements are off." + "Turn them on, then load the game again" + an "Achievements Settings" button
  (A, or a pointer click on it) that opens Options > Achievements; other no-list states (a .exe, no set, Official status) keep the status line.
  Tabs, later the same day: Monty's "C1 Folder, color edge" (`drawTabs`): trapezoid folder tabs (8 px slant, 3 px overlap each side, chosen one last, 4 px taller, page color 238, no line to the page via a page-colored band), a 4 px color edge on top (blue / gray / green; 55 % color on the unchosen ones), labels centered (bold = drawn twice), labels cached per count; 0 differing pixels outside the strip.
  Data: `achievement_entry_t.rarity_x100` (RA player share x 100; -1 for a Local set) and `.when` (unix seconds, Official only; 0 for a
  Local set) already existed; a Local set has neither, so the ach_book fixture shows no rarity and no row date (left out, nothing invented);
  the detail's date falls back to the picture file's date. No time of day (UTC only, no zone known). Official paths (rarity / dates) were
  not run on a real account: formatter checked on the host for 11 values. Proof: ws_book_base_release.dol vs ws_book_new_release.dol,
  ach_book walk + an Achievements=0 run (A on the button opens the settings page), contact sheet ws_book_contact_sheet.png.
- Step 11 (2026-11-09, Dialog; read-only finding, NO code change): the dialogs have no pad-polling loop of their own to remove.
  `libgui/MessageBox` (setMessage = notice, askMessage = Yes/No, fadeMessage = non-blocking toast) is an engine Frame with three
  ordinary Buttons; its blocking `while (messageBoxActive) Gui::draw()` runs the SAME per-frame path as every page (`Input::refreshInput`,
  Cursor, Focus, the frame list, `drawMessageBox` from Gui::draw), and A/B/pointer reach it as button click/return functions. So Quit
  (`Func_ExitToLoader`), Credits (`MainFrame.cpp:167`) and all ~75 `setMessage`/`askMessage` callers (CurrentRom 24, Settings 17, FileBrowser
  11, Main 11, Explorer 1, ConfigureButtons 3) already sit on the shared input and draw. `glue::readInput()` (page turn + sort key only)
  is for Board/List pages and has nothing a button dialog needs; routing the box through it would add a second reader and change the keys.
  The Dialog form of section 2 (`dialogAsk`/`dialogNote` without a nested loop) is the NON-blocking redesign, which answer 49 declined;
  not built. Other loops, kept: `Func_PlayGame`'s wait-for-A-release (MainFrame.cpp:233, reads held A on GC/Wiimote/WUPC/DRC; not a
  dialog, and `readInput` has no held-A, so moving it would need a new field), the "Loading achievements" `fadeMessage` + draw loop
  (MainFrame.cpp:268), `MenuContext::Autoboot`, `LoadingBar` (one `Gui::draw`). Chain runs skip boxes (`unattended_message`); keep.
  No DOLs built, no proof run (nothing changed). Open: if Monty wants the non-blocking Dialog or the PlayGame loop on glue, say so.
- Step 12 (2026-11-09, delete the old path; 12b measured): `git diff --stat`: 7 files, 95 insertions, 801 deletions (MenuEngine.cpp 1752 ->
  ~1530, MenuEngine.h -119, MenuPageView.cpp/.h, pages/OptionsPages.h and SettingsPages.h, tests/menu_engine_test.cpp -373). Deleted:
  Page::help/nHelp/HelpLine/ROLE_HELP_*, labelCx, labelDy, rowDy, Row::widths, Row::fit (SetRow::fit, ROW_RADIO_FIT), ListSpec x0/y0/w/h/pitch
  and ListButton x/y/w/h, buttonFitWidth, the old tabs/labels/Toggle/Cycle/Choice/Readout/List layout, the old-grid branches in
  gridPlace/isCell, PageView's 16 help TextBoxes; `grid::` is down to TITLE_X/Y, ROW_DY, BUTTON_H, BUTTON_PAD_X, TEXT_SCALE, FONT_H.
  Kept on purpose: the old path (Page::standard == DENSITY_OFF) still serves two shipped kinds. (1) ActionMenu (Main, Current ISO):
  only ROW_BUTTONS rows (Row::x0/gap, ActionButton width/dy/lit, rowY0, overrides, logo, FOCUS_ROWS). (2) glue::Editor (Configure Input,
  Co-Op, Configure Buttons): free buttons/texts/links, title, titleY, wrap, FOCUS_GEOMETRY, no rows. layoutPage on that path now refuses a
  tab row or any non-Buttons row (-1); PageView::show refuses a DENSITY_OFF page. Page::columns stays (standard two columns).
  Tests: old-geometry list tests (window/links/geometry) and testTwoColumns deleted (explorer, testStdColumns and testStandard cover the
  live ones); list paging, guards, edge wrap, overrides, check-catches, inactive focus kept, with the Cycle/Choice/Toggle/Readout cases moved to
  DENSITY_REGULAR and the logo-box case kept on the old path with ROW_BUTTONS rows. Deleted cases: label past the edge in the old column,
  choice row moved clear of the logo (old geometry only).
  12b (memory the menu's widgets take): only two PageViews exist (OptionsFrame, SettingsFrame); each builds 42 Buttons (6 tabs + 9 rows x 4
  slots) and 57 TextBoxes (title, 9 x (label, rlabel, 4 values), 2 footer lines). Estimated from the class layouts (Button ~200 B, TextBox
  ~130 B, not measured with sizeof on a PPC build): about 16 KB per view, ~32 KB for both. Far under the 200 KB bar: lazy page build NOT
  done, nothing to save.
  Proof: host tests (6 suites) and settings_check.py pass; release DOLs built from the base (4f094d8a) and the new tree. Walk (all 22
  pages): 0 differing frames except page 10 (Memory, 1 frame, 945 px: its live KB number). menukeys walks (Main 2663 frames, Current ISO
  2701 frames, ActionMenu; focus moves and 9-13 menu moves in both): 0 differing frames. DOLs: opencode/ws_step12_base_release.dol and ws_step12_new_release.dol.
- ISO Info disc band (2026-11-09, step 13; design I4, disc picture only): with a disc picture for the loaded game, the page shows the round disc
  at the left (STD_DISC_X/Y/D = 24/88/168, square menu pixels), the game name (24 px) right of it at STD_DISC_TEXT_X 216 with "SERIAL, label X"
  and "CUE, 1 track, 630 MB" (16 px) under it, and the grid starts at the fourth row (System ... Achievements, 5 text rows from
  STD_DISC_ROWS_Y0 268, ending at STD_BODY_Y1 424). No picture (no pack, no serial, an .exe, no disc row): the page as before, 8 rows, no hole.
  Standard way: `eng::Page::rowY0` now also moves a standard page's first row (0 = STD_BODY_Y0; layoutStandard only), and
  `buildOptionsPage(page, tab, bind, out, skip, rowY0)` leaves out the first `skip` rows (ISOINFO_BAND_ROWS = 3: Game, Disc, Image, which the band
  shows). ISO Info got enter/leave hooks (`isoInfoEnter`/`isoInfoLeave`): enter looks the serial up in `<sd|usb>:/wiistation/covers/index.txt`
  (cover_index_find, like the explorer's cover), takes the `disc` path, and `cover_art_load`s it as STD_DISC_D square RGB5A3 (stb_image PNG/JPEG,
  box filter, alpha kept for the round edge). Memory: cover_art holds ONE texture, 168*168*2 = 55 KB in MEM2 (the file, up to 4 MB, and the 600x600
  RGBA decode are transient MEM2 and freed at once); freed by the leave hook; entering also calls `fbCoverForget()` so the explorer, which shares
  that texture, drops its state and loads again. Drawn in `OptionsFrame::drawChildren` before the widgets (GX_LoadTexObjPreloaded into the font's
  region 0, blended quad, then `glue::text` for the three band lines, name scaled down to fit 400 px). A page switch while the page is shown does not
  reload (no tabs on this page).
  Host: `menu_pages_test` lays the page out with skip 3 and rowY0 and checks rows below the disc, inside the body, band clear of the title, text and logo
  box. Proof: scripts/wsx.sh run with --card of a one-serial pack and "menuopen 400 17" (Spyro SCUS94228, game loaded): disc, no pack, and base stills;
  contact sheet opencode/ws_disc_contact_sheet.png; DOLs ws_disc_base_release.dol, ws_disc_new_release.dol (same folder).
  Not done: real Wii (decode time of a 600x600 PNG on Broadway not measured: one hitch when the page opens), multi-disc games (the serial of the
  disc loaded), a pack made with --resize (works the same, smaller file).
- Step 9c (2026-11-09, Configure Buttons' top, G2): line 1 = the pad as a Cycle (`Port 1: Wiimote + Nunchuk`, replaces Next and the title) and, on a
  Wiimote only, `Light Gun: Off | On` (replaces Gun/Mouse; hidden on other pads); line 2 = the slot as a Cycle (Default, Slot 1-4, Game), Load,
  Save, and Built-in at the right (the shipped `config_default`). The six top buttons are `BTN_STD` (square, arrows on the three Cycles, plain
  Load/Save/Built-in); the rectangles keep focus sane under the engine's geometry rule (pad -> down -> slot; slot -> right -> Load). Only
  ConfigureButtonsFrame.cpp changed on the menu side (the Editor was enough; the old title label is kept hidden). Storage: the user's Default is
  one more slot, `config_slot[CONTROLLER_DEFAULT_SLOT]` (5), in the same per-controller file (`buttons/<type>.cfg`), written after the four slots,
  screenshot and ANALOG lists as one more block; a file from before it ends early and the slot stays `config_default` (initialised in
  init_controller_ts). Save on Default = Save on a slot (all files are written); Load Default copies it in. Game start: Auto Load Slot = Default
  and a missing Game file now use the user's Default (`loadGameButtons`), which is `config_default` until overwritten. Proof (release DOL, base
  cd025e19 against new): pages 20, 21 0 differing frames; 40 and 41 differ on the top lines only (`ws_step9c_contact_sheet.png`). Menukeys on page
  40 with a GC pad (`--pad`), card read back with sdimage_read.py: pad Cycle to Port 2 (all inactive) and back; D from the pad lands on the slot;
  slot Cycle Default -> Slot 1; change L1, Save on Slot 1 -> gamecube.cfg 504 bytes, slot 1 L1 index 6, other slots and Default 5; Built-in puts
  L1 back (screen); Load slot 1 restores the saved value (screen); focus reaches pad, slot, Load, Save, Built-in. Not shown: Light Gun Off|On
  and the long Wiimote + Nunchuk text (no Wiimote in Dolphin runs; the pad button is 280 wide, the text is clipped if it does not fit); Save on
  Default was not read back (after the Save toast the scripted keys lose focus; the code path is the same as a slot's).
