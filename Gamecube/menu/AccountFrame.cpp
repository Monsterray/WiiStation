/**
 * WiiStation - AccountFrame.cpp
 * The account chooser: a standard titled List page. It is the engine's explorer-density list with a
 * title and no tabs, on a frame of its own (the explorer used to have an account mode for this).
 * "None: earn Local", the files of sd:/wiistation/achievements/ra/ without ".ini", and "This Wii's
 * account" when ra_account.ini is there (only its presence is tested). No account file is opened.
**/

#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include "MenuGlue.h"
#include "MenuContext.h"
#include "MenuEngine.h"	/* before Button.h: its include guard is BUTTON_H, a name the engine uses */
#include "AccountFrame.h"
#include "../libgui/Button.h"
#include "../libgui/IPLFont.h"
#include "../libgui/resources.h"
#include "../libgui/FocusManager.h"
#include "../libgui/CursorManager.h"

extern "C" {
#include "../players.h"
}

namespace eng = menu::engine;

extern MenuContext *pMenuContext;

static const int MAX_ACCOUNTS = 64;

enum { ACCOUNT_FILE, ACCOUNT_NONE, ACCOUNT_WII };
struct AccountItem { char name[PLAYERS_ACCOUNT]; int kind; };
static AccountItem accounts[MAX_ACCOUNTS + 2];
static int numAccounts;
static char accountMii[PLAYERS_KEY], accountTitle[64];
static menu::glue::ListView *acView;

static void accountItem(void *, int index, eng::ListItem &out)
{
	out.text = accounts[index].name;
	out.folder = false;
}

static void acBack() { pMenuContext->setActiveFrame(MenuContext::FRAME_PLAYERS, 1); }   /* the Mii's page */

static void acPick(void *, int i)
{
	if (i < 0 || i >= numAccounts)
		return;
	players_link(accountMii, accounts[i].kind == ACCOUNT_NONE ? "" :
	                         accounts[i].kind == ACCOUNT_WII ? "*" : accounts[i].name);
	acBack();
}

static int accountCompare(const void *x, const void *y)
{
	return strcasecmp(((const AccountItem*)x)->name, ((const AccountItem*)y)->name);
}

void accountFrame_Open(const char *key, const char *mii)
{
	struct stat st;
	snprintf(accountMii, sizeof accountMii, "%s", key);
	snprintf(accountTitle, sizeof accountTitle, "Account for %s", mii);
	numAccounts = 0;
	snprintf(accounts[numAccounts].name, sizeof accounts[0].name, "None: earn Local");
	accounts[numAccounts++].kind = ACCOUNT_NONE;
	if (DIR *d = opendir("sd:/wiistation/achievements/ra")) {
		while (struct dirent *e = readdir(d)) {
			size_t n = strlen(e->d_name);
			if (numAccounts <= MAX_ACCOUNTS && n > 4 && n - 4 < PLAYERS_ACCOUNT && !strcasecmp(e->d_name + n - 4, ".ini")) {
				snprintf(accounts[numAccounts].name, n - 3, "%s", e->d_name);
				accounts[numAccounts++].kind = ACCOUNT_FILE;
			}
		}
		closedir(d);
	}
	qsort(accounts + 1, numAccounts - 1, sizeof(AccountItem), accountCompare);
	if (!stat("sd:/wiistation/achievements/ra_account.ini", &st)) {
		snprintf(accounts[numAccounts].name, sizeof accounts[0].name, "This Wii's account");
		accounts[numAccounts++].kind = ACCOUNT_WII;
	}
	acView->setCount(numAccounts);
	menu::Focus::getInstance().clearPrimaryFocus();
}

AccountFrame::AccountFrame()
{
	numAccounts = 0;
	acView = new menu::glue::ListView(this, accountTitle, accountItem, acPick, NULL, acBack);
}

AccountFrame::~AccountFrame()
{
	delete acView;
	acView = NULL;
}

void AccountFrame::drawChildren(menu::Graphics &gfx)
{
	if (!isVisible())
		return;
	acView->update();
	Frame::drawChildren(gfx);
	acView->drawOverlay(gfx);
}
