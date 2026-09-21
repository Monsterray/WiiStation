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

#endif
