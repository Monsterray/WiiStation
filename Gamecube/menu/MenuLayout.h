/**
 * WiiStation - MenuLayout.h
 *
 * The menu's shared geometry. Every Settings tab and every Options page is laid out from
 * these, so that the tabs look alike and a new one has somewhere to start.
 *
 * The screen is 640x480 whatever the television does with it (Gamecube/libgui/GraphicsGX.cpp).
 * Nothing clips: a button or a string placed past an edge is simply drawn off the screen,
 * which is why tests/menu_engine/run.sh lays out every shipped page and checks it against
 * everything here, rather than waiting for someone to notice on a television.
 *
 * THE RULES
 *   - Nothing comes within MENU_EDGE of any edge of the screen.
 *   - Nothing enters the logo's corner. The spinning logo (Gamecube/libgui/Logo.cpp) is
 *     drawn over every frame, so the rows that reach that far have to stop short of it;
 *     MENU_ROW_RIGHT() gives the right edge available to a row that ends at a given y.
 *   - A row whose buttons have a label on the left starts at TAB_BUTTON_X, and its label
 *     is centred on TAB_LABEL_CX. A row that is only buttons is centred on the screen.
 *   - Buttons within one row are separated by exactly TAB_GAP.
 *   - A row that cannot fit under those rules moves left far enough to fit, and no
 *     further, so it still lines up with the others on its right-hand side.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#ifndef MENULAYOUT_H
#define MENULAYOUT_H

#define MENU_W			640.0
#define MENU_H			480.0
#define MENU_EDGE		  4.0	/* keep this clear of every edge */

#define MENU_LEFT		MENU_EDGE
#define MENU_RIGHT		(MENU_W - MENU_EDGE)
#define MENU_BOTTOM		(MENU_H - MENU_EDGE)

/*  The spinning logo  */

/* Logo.cpp draws the logo last, over whatever the frame put there, so its space belongs to
 * it. x and y are its middle. It turns about the view axis, so it sweeps a circle of its
 * own diagonal: 72 x 72 holds it. Measured on a frame dump with the middle at (570, 70),
 * the logo covered x 551..590 and y 45..102.
 *
 * It sits in the top right, where it has always been. A frame that puts something there --
 * the Settings tab strip, an Options row, one Configure Buttons button, Swap CD on the
 * Current ROM frame -- moves it to the bottom right instead.
 * MenuContext::setActiveFrame holds that list and chooses. */
#define MENU_LOGO_W		 72.0
#define MENU_LOGO_H		 72.0
#define LOGO_MAIN_X		570.0
#define LOGO_MAIN_Y		 70.0
#define LOGO_PAGE_X		578.0
#define LOGO_PAGE_Y		428.0
#define LOGO_Z		   -150.0

/* The box the logo sweeps, from the middle of either position. */
#define LOGO_BOX_L(cx)	((cx) - MENU_LOGO_W / 2)
#define LOGO_BOX_R(cx)	((cx) + MENU_LOGO_W / 2)
#define LOGO_BOX_T(cy)	((cy) - MENU_LOGO_H / 2)
#define LOGO_BOX_B(cy)	((cy) + MENU_LOGO_H / 2)

/* The right edge a row may reach, given the y of its lowest pixel. A row level with the
 * logo's bottom-right position must stop short of it. */
#define MENU_ROW_RIGHT(bottom)	\
	((bottom) > LOGO_BOX_T(LOGO_PAGE_Y) ? LOGO_BOX_L(LOGO_PAGE_X) - MENU_EDGE : MENU_RIGHT)

/*  The Settings tabs' grid  */
#define TAB_LABEL_CX	150.0	/* the left-hand label of a row, centred here */
#define TAB_BUTTON_X	295.0	/* the first button of a row that has such a label */
#define TAB_GAP			 10.0	/* between two buttons on the same row */

/*  The menu standard (Docs/MENU_PLAN.md 1.1-1.3)
 *
 * Opt-in: a Page sets `standard` (MenuEngine.h) and is laid out from these; every other page
 * keeps the numbers above. Nothing here changes an existing screen. */

/* The three bands of every screen. */
#define STD_HEADER_Y0	 16.0	/* a tab strip or a centred title */
#define STD_HEADER_Y1	 72.0
#define STD_BODY_Y0		 88.0	/* the rows */
#define STD_BODY_Y1		424.0
#define STD_FOOTER_Y0	432.0	/* at most two lines of help for the focused row, no button hints */
#define STD_FOOTER_Y1	472.0

/* The tab strip: equal widths (the widest text + 16, step 8), centred. */
#define STD_TAB_Y		 18.0
#define STD_TAB_H		 52.0
#define STD_TAB_GAP		 12.0
#define STD_TAB_PAD		 16
#define STD_TAB_PAD_OWN	 16	/* tabs that cannot be equal: each its own text + 16 */
#define STD_TITLE_Y		 44.0	/* centre of the centred title */

/* One label edge on every form; it never moves. Labels end at it, right-aligned, and are at most
 * STD_LABEL_MAX_W wide (MENU_EDGE is not it: that is the screen's margin). */
#define STD_LABEL_EDGE	248.0
#define STD_LABEL_MAX_W	224
#define STD_CTRL_X0		264.0	/* the control column */
#define STD_CTRL_X1		616.0
#define STD_CTRL_W		(STD_CTRL_X1 - STD_CTRL_X0)	/* 352 */

/* Densities: button height and row pitch. A page with more rows than fit becomes two pages. */
#define STD_REG_H		 50.0	/* Regular, the default: 6 rows */
#define STD_REG_PITCH	 56.0
#define STD_REG_ROWS	 6
#define STD_CMP_H		 40.0	/* Compact: 7 rows */
#define STD_CMP_PITCH	 46.0
#define STD_CMP_ROWS	 7
#define STD_TXT_H		 28.0	/* text rows (readouts): 10 rows */
#define STD_TXT_PITCH	 32.0
#define STD_TXT_ROWS	 10
#define STD_LIST_H		 34.0	/* list rows: they touch (pitch = height), 8 of them */
#define STD_LIST_PITCH	 34.0

/* The explorer (DENSITY_EXPLORER, Docs/MENU_PLAN.md 1.4 type D, no logo): a tab strip, the path and the
 * count in the line under it, the letter strip (y 94-118, empty until it is built), then STD_LIST_ROWS list
 * rows from y 126 on every tab, one touching the next (y 126-398 for 8, 14 px above the facts strip). A name is cut with "..." to the room left of the note and the tag; the
 * focused row's name scrolls inside the same room. The scroll column (x 604-616) is the paging: an
 * arrow button at each end of the track, and the thumb the frame draws between them. */
#define STD_LIST_X0		 24.0
#define STD_LIST_W		576.0
#define STD_LIST_Y0		126.0
#define STD_LIST_ROWS	 8	/* the one row count: the explorer's window, page size and the account list's */
#define STD_LIST_NAME_X	 38.0	/* a name starts here; a folder's after its icon */
#define STD_LIST_FOLDER_X 66.0
#define STD_LIST_ICON_X	 36.0	/* the folder icon's left edge (the frame draws it) */
#define STD_LIST_TAG_R	590.0	/* the format tag's right edge */
#define STD_LIST_NOTE_R	536.0	/* the disc count's (or the date's) right edge when the row has no tag */
#define STD_LIST_NOTE_GAP 10.0	/* with a tag: the note ends this far left of the tag's own left edge */
#define STD_LIST_NAME_GAP 16.0	/* between a name and the text on its right */
#define STD_LIST_PX_TAG	 13.0	/* tag and note text size in pixels */
#define STD_PATH_X		 24.0
#define STD_PATH_Y		 80.0	/* 2 px above the mock-up's 82: the count's descenders clear the letter strip */
#define STD_PATH_PX		 14.0
#define STD_COUNT_R		616.0	/* the count's right edge, on the path's line */
#define STD_SCROLL_X	604.0
#define STD_SCROLL_W	 12.0
#define STD_SCROLL_ARROW_H 24.0
#define STD_CUT_LEN		128		/* bytes of a cut name, with its "..." */
/* The letter strip: "#" and A-Z, 27 cells across x 24..616 at y 94-118 (the mock-up's render.py). The
 * frame draws it; the engine owns the cell boxes, the letters and the jumps (stripCell and friends). */
#define STD_STRIP_X0	 24.0
#define STD_STRIP_X1	616.0
#define STD_STRIP_Y		 94.0
#define STD_STRIP_H		 24.0
#define STD_STRIP_N		 27
#define STD_STRIP_GAP	  2		/* between two cells */
#define STD_STRIP_PX	 13.0

/* The facts strip under the last list row (render.py facts()): x 24-616, y 412-472. The frame draws it; the
 * engine owns the boxes (factsLayout). A title line (the focused name in full), then two columns of three
 * lines: a key right-aligned at the column's key edge and its value from the value edge. The left 64 px
 * are kept for the cover (slice 7: 48 x 48 at x 30, y 418). */
#define STD_COVER_X	 30		/* the cover slot (a cover is drawn only when the pack has one) */
#define STD_COVER_Y	418
#define STD_COVER_W	 48
#define STD_COVER_H	 48
#define STD_FACTS_X0	 24
#define STD_FACTS_X1	616
#define STD_FACTS_Y0	412
#define STD_FACTS_Y1	472
#define STD_FACTS_TITLE_X 88
#define STD_FACTS_TITLE_Y 414	/* the title's top */
#define STD_FACTS_TITLE_PX 14
#define STD_FACTS_PX	 12		/* keys and values */
#define STD_FACTS_ROW_Y0 431	/* the first fact line's top */
#define STD_FACTS_PITCH	 14
#define STD_FACTS_COL_X	 88		/* the first column's left; the second is 264 further */
#define STD_FACTS_COL_DX 264
#define STD_FACTS_KEY_R	 96		/* a key's right edge, from the column's left */
#define STD_FACTS_VAL_X	104		/* a value's left edge, from the column's left */
#define STD_FACTS_VAL_W	156		/* the room of a value */
#define STD_FACTS_N		  6

/* The ISO Info disc band (design I4, Docs/MENU_FORMS_PLAN.md step 13): when the game has disc art, the round disc at the left
 * (STD_DISC_D px square: menu pixels are square on the TV in both screen modes), the game name large to its right with the
 * disc and image lines under it, and the info rows from STD_DISC_ROWS_Y0 (5 text rows end at 424 = STD_BODY_Y1). */
#define STD_DISC_X		 24
#define STD_DISC_Y		 88
#define STD_DISC_D		168
#define STD_DISC_TEXT_X	216		/* the name and the two lines: STD_DISC_X + STD_DISC_D + 24 */
#define STD_DISC_NAME_Y	108		/* tops of the 24 px name, then the 16 px lines */
#define STD_DISC_LINE1_Y 144
#define STD_DISC_LINE2_Y 168
#define STD_DISC_ROWS_Y0 268.0

/* The logo box, bottom right; the engine owns it and no control may enter it. */
#define STD_LOGO_X		572.0
#define STD_LOGO_Y		384.0
#define STD_LOGO_W		 64.0
#define STD_LOGO_H		 88.0

/* Buttons that act alone never touch anything. The segments of one choice do touch. */
#define STD_BTN_GAP		 12.0
#define STD_STEP		  8	/* a width from text is rounded up to a multiple of this */
#define STD_SEG_PAD_EQ	 32	/* equal segments, and a separate button: widest text + 32 */
#define STD_SEG_PAD_OWN	 16	/* segments that cannot be equal: each its own text + 16 */
#define STD_CYCLE_W		200.0	/* a Cycle button */
#define STD_CELL_PAD	 38	/* a two-column cell: text + the arrows (base 17 px in from each end, drawArrows) + 2 px clear each side */
#define STD_CELL_LGAP	  8	/* label to button in a two-column cell */
#define STD_CELL_CGAP	 12	/* between the two columns */
#define STD_CYCLES_W	140.0	/* one of several small cycles in a row, after its prefix text */
#define STD_CYCLES_PRE	  8.0	/* between a small cycle's prefix and its button */
#define STD_CYCLES_GAP	 16.0	/* between two small cycles */

/* Footer help for the focused row: 0-2 lines, 13 px, from x 24 to x 556. */
#define STD_HELP_PX		 13.0
#define STD_HELP_X0		 24.0
#define STD_HELP_X1		556.0
#define STD_HELP_Y0		442.0	/* the first line's top; the second is STD_HELP_DY below */
#define STD_HELP_DY		 17.0
#define STD_HELP_LINES	 2
#define STD_HELP_COLOR	{ 44, 44, 44, 255 }	/* opaque and dark, as the mock-ups (render.py HELP_COL) */

#endif
