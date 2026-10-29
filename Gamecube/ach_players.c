/* ach_players.c -- who plays, for the achievements (ach_players.h, Docs/MII_PLAN.md phase 3).
 *
 * The seats are players.ini's (players.h playerSeat); a seat counts when its virtual
 * controller answers, the same test the Co-Op pad mapping uses (coop.c: inUse and control),
 * and its port is set up for that seat (a multitap's slot only with the multitap, a Co-Op
 * player only within coopPlayers). A Mii no longer on the console reads as Guest. Logs print
 * only the first 8 characters of a key, never a name. */
#include <gccore.h>
#include <string.h>
#include "ach_players.h"
#include "gc_input/controller.h"
#include "wiiSXconfig.h"
#include "players.h"

extern void SysPrintf(const char *fmt, ...);

extern int autoinput_active(int port);   /* PadWiiSX.c: an input script plays this script port (p1, p2, p1b ...) */

/* A scripted pad (an unattended run's input script has lines for the port) stands in for a
 * controller, as it does for the PlayStation pad (PlugPAD.c auto_assign_controllers) */
static int scripted(int seat)
{
	return autoinput_active(seat < 2 ? seat : seat == 2 ? 0 : seat == 6 ? 1 : seat < 6 ? seat - 1 : seat - 2);
}

static int seat_connected(int seat)
{
	const virtualControllers_t *vc = &virtualControllers[seat];
	int port, answers;

	if (seat < 0 || seat >= NUM_VIRTUAL_CONTROLLERS)
		return 0;
	answers = vc->inUse && vc->control;
	if (seat < 2)                       /* a port's own pad: not when it holds a multitap or Co-Op */
		return padType[seat] != PADTYPE_MULTITAP && padType[seat] != PADTYPE_COOP && (answers || scripted(seat));
	if (seat < 10)                      /* multitap slots 1A-1D, 2A-2D */
		return padType[seat < 6 ? 0 : 1] == PADTYPE_MULTITAP && (answers || scripted(seat));
	port = (seat - 10) / 8;             /* Co-Op player 1-8 of port 1 or 2 */
	return answers && padType[port] == PADTYPE_COOP && (seat - 10) % 8 < coopPlayers[port];
}

static int known(const char *key, char *name, unsigned size)
{
	int r = players_lookup(key, name, size);
	if (r == 0)
		SysPrintf("Players: Mii %.8s is not on this console, its seat plays as Guest\n", key);
	else if (r < 0)
		SysPrintf("Players: Mii %.8s: the Mii database was not read, its name is not known\n", key);
	return r;
}

unsigned ach_players_get(achievement_player_t *out)
{
	int seat, seated = 0;
	for (seat = 0; seat < PLAYERS_SEATS; seat++)   /* a key's first 8 characters, never a name */
		if (playerSeat[seat][0]) {
			seated++;
			SysPrintf("Players: seat %s has Mii %.8s, %s\n", PLAYERS_SEAT_NAMES[seat], playerSeat[seat],
			          seat_connected(seat) ? "its pad is connected" : "no pad is connected");
		}
	if (!seated)
		SysPrintf("Players: no Mii is seated, Guest plays\n");
	return ach_players_pick((const char (*)[256])playerSeat, PLAYERS_SEATS, seat_connected, known, out);
}
