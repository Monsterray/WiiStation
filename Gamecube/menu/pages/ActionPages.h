/**
 * WiiStation - pages/ActionPages.h
 *
 * The action menus: Main and Current ISO. A page of buttons that each do one thing, placed in rows. The
 * form is menu::glue::ActionMenu (MenuPageView.h); this file is the data of the two pages, plain tables
 * and function names that the Wii build and the host test (tests/menu_pages_test.cpp) both compile.
 *
 * A button is one line: its text, what A does, its row and slot (column) in the row, its width and how far
 * below the row's top it sits (0 = at the top). A row says where its first button starts and the gap
 * between buttons. Add, move or rename a button here; the rows, the focus links and the cursor follow.
**/

#ifndef ACTIONPAGES_H
#define ACTIONPAGES_H

#include <stddef.h>
#include "../MenuEngine.h"

struct ActionDef
{
	const char	*text;
	void		(*click)(void);
	int			row, slot;
	float		width, dy;
};

struct ActionRowDef { float x0, gap; };

struct ActionMenuDef
{
	const char						*name;		/* for the layout log */
	const ActionDef					*acts;
	int								nActs;
	const ActionRowDef				*rows;
	int								nRows;
	float							rowY0;		/* the first row's top */
	menu::engine::LogoSpot			logo;
	const menu::engine::LinkOverride *links;	/* focus links the rule does not give */
	int								nLinks;
};

#define ACTION_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- Main: seven rows of one button, 200 wide at x 315, 60 apart from y 36 ---- */

void Func_LoadROM();
void Func_CurrentROM();
void Func_Settings();
void Func_Players();
void Func_Credits();
void Func_ExitToLoader();
void Func_PlayGame();

enum { MAIN_PLAY = 6 };		/* the button that says Play Game or Resume Game */

static const ActionDef MAIN_ACTS[] =
{	//	text			A does				row slot width    dy
	{	"Load ISO",		Func_LoadROM,		0, 0, 200.0f, 0.0f },
	{	"Current ISO",	Func_CurrentROM,	1, 0, 200.0f, 0.0f },
	{	"Settings",		Func_Settings,		2, 0, 200.0f, 0.0f },
	{	"Players",		Func_Players,		3, 0, 200.0f, 0.0f },
	{	"Credits",		Func_Credits,		4, 0, 200.0f, 0.0f },
	{	"Quit",			Func_ExitToLoader,	5, 0, 200.0f, 0.0f },
	{	"Play Game",	Func_PlayGame,		6, 0, 200.0f, 0.0f },
};
static const ActionRowDef MAIN_ROW_PLACE[] =
{
	{ 315.0f, 0.0f }, { 315.0f, 0.0f }, { 315.0f, 0.0f }, { 315.0f, 0.0f }, { 315.0f, 0.0f }, { 315.0f, 0.0f }, { 315.0f, 0.0f },
};
static const ActionMenuDef MAIN_MENU =
{
	"Main", MAIN_ACTS, ACTION_COUNT(MAIN_ACTS), MAIN_ROW_PLACE, ACTION_COUNT(MAIN_ROW_PLACE),
	36.0f, menu::engine::LOGO_SPOT_TOP, NULL, 0,	/* the last row ends at 452 */
};

/* ---- Current ISO: two columns on the first rows, then single buttons; the logo is bottom right ---- */

void Func_ShowRomInfo();
void Func_ResetROM();
void Func_SwapCD();
void Func_LoadSave();
void Func_SaveGame();
void Func_LoadState();
void Func_SaveState();
void Func_StateCycle();
void Func_Screenshot();
void Func_AchievementsList();

enum { CUR_SLOT_BUTTON = 7 };	/* "Slot N": its text follows the slot */

static const ActionDef CUR_ACTS[] =
{	//	text				A does					row slot width   dy
	{	"Restart Game",		Func_ResetROM,			0, 0, 230.0f,  0.0f },
	{	"Swap CD",			Func_SwapCD,			0, 1, 230.0f,  0.0f },
	{	"Load MemCards",	Func_LoadSave,			1, 0, 230.0f,  0.0f },
	{	"Save MemCards",	Func_SaveGame,			1, 1, 230.0f,  0.0f },
	{	"Show ISO Info",	Func_ShowRomInfo,		2, 0, 230.0f,  0.0f },
	{	"Achievements",		Func_AchievementsList,	2, 1, 230.0f,  0.0f },
	{	"Load State",		Func_LoadState,			3, 0, 220.0f,  0.0f },
	{	"Slot 0",			Func_StateCycle,		3, 1, 120.0f, 30.0f },
	{	"Save State",		Func_SaveState,			4, 0, 220.0f,  0.0f },
	{	"Screenshot",		Func_Screenshot,		5, 0, 220.0f,  0.0f },
};
static const ActionRowDef CUR_ROW_PLACE[] =
{
	{ 100.0f, 20.0f }, { 100.0f, 20.0f }, { 100.0f, 20.0f }, { 150.0f, 20.0f }, { 150.0f, 0.0f }, { 150.0f, 0.0f },
};
/* The four links the rule does not give: Swap CD Up and Slot Down (the two buttons above and below each
 * other on the right), and Save State Left and Right (the Slot button). Applied after the rule. */
static const menu::engine::LinkOverride CUR_LINKS[] =
{
	{ 0, 1, menu::engine::DIR_UP,    3, 1 },
	{ 3, 1, menu::engine::DIR_DOWN,  0, 1 },
	{ 4, 0, menu::engine::DIR_LEFT,  3, 1 },
	{ 4, 0, menu::engine::DIR_RIGHT, 3, 1 },
};
static const ActionMenuDef CUR_MENU =
{
	"Current ISO", CUR_ACTS, ACTION_COUNT(CUR_ACTS), CUR_ROW_PLACE, ACTION_COUNT(CUR_ROW_PLACE),
	60.0f, menu::engine::LOGO_SPOT_BOTTOM, CUR_LINKS, ACTION_COUNT(CUR_LINKS),
};

#endif
