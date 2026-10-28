/**
 * WiiStation - PlayersFrame.h
 * The Players menu: the Miis on this Wii seated on the PlayStation ports (Docs/MII_PLAN.md,
 * layout D: port cards first, a paged strip of Mii coins below).
**/

#ifndef PLAYERSFRAME_H
#define PLAYERSFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class PlayersBoard;

class PlayersFrame : public menu::Frame
{
public:
	PlayersFrame();
	~PlayersFrame();
	void activateSubmenu(int submenu);
	void drawChildren(menu::Graphics& gfx);

private:
	PlayersBoard *board;
};

#endif
