/**
 * WiiStation - MenuLayout.h
 *
 * The menu's shared geometry. Every Settings tab and every Options page is laid out from
 * these, so that the tabs look alike and a new one has somewhere to start.
 *
 * The screen is 640x480 whatever the television does with it (Gamecube/libgui/GraphicsGX.cpp).
 * Nothing clips: a button or a string placed past an edge is simply drawn off the screen,
 * which is why scripts/menu_text_width.py checks the tables in SettingsFrame.cpp and
 * OptionsFrame.cpp against everything here and fails the build's own sanity check rather
 * than waiting for someone to notice on a television.
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

/* The spinning logo's corner. It is drawn last, over whatever a frame put there, so this
 * much of the bottom right belongs to it. Moving the logo means moving these two numbers
 * and re-running scripts/menu_text_width.py, which will say what no longer fits.
 *
 * Measured from a screenshot: the logo is about 60 wide and 90 tall and turns, so it
 * sweeps a little more than that, and Gui.cpp places its middle at (578, 428). Until
 * 2026-09-21 it sat at (570, 70) and was drawn across the Saves tab. */
#define MENU_LOGO_W		120.0
#define MENU_LOGO_H		110.0
#define MENU_LOGO_X		(MENU_W - MENU_LOGO_W)
#define MENU_LOGO_Y		(MENU_H - MENU_LOGO_H)

#define MENU_LEFT		MENU_EDGE
#define MENU_RIGHT		(MENU_W - MENU_EDGE)
#define MENU_BOTTOM		(MENU_H - MENU_EDGE)

/* The right edge a row may reach, given the y its lowest pixel sits on. */
#define MENU_ROW_RIGHT(bottom)	\
	((bottom) > MENU_LOGO_Y ? MENU_LOGO_X - MENU_EDGE : MENU_RIGHT)

/*  The Settings tabs' grid  */
#define TAB_LABEL_CX	150.0	/* the left-hand label of a row, centred here */
#define TAB_BUTTON_X	295.0	/* the first button of a row that has such a label */
#define TAB_GAP			 10.0	/* between two buttons on the same row */

#endif
