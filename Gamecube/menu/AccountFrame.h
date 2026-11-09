/**
 * WiiStation - AccountFrame.h
 * A Mii's RetroAchievements account, chosen from a short list on a page of its own: a standard
 * titled List page ("Account for <Mii>"), the engine's DENSITY_EXPLORER list without tabs.
 * Opened by Players (Edit slot). A pick links the account and goes back to Players.
**/

#ifndef ACCOUNTFRAME_H
#define ACCOUNTFRAME_H

#include "../libgui/Frame.h"
#include "MenuTypes.h"

class AccountFrame : public menu::Frame
{
public:
	AccountFrame();
	~AccountFrame();
	void drawChildren(menu::Graphics& gfx);
};

/* Fills the list for the Mii with this key (name shown in the title) and shows it. Call after
 * setActiveFrame(MenuContext::FRAME_ACCOUNT, 0). */
void accountFrame_Open(const char *key, const char *mii);

#endif
