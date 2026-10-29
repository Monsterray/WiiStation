/* ach_players.h -- who plays, for the achievements (Docs/MII_PLAN.md, phase 3).
 *
 * ach_players_pick is plain C (tests/achievements_players_test.c): from the seats, the Miis
 * that earn: those on a seat whose pad is connected and that are still on the console. The
 * hook the engine calls (achievements_players) is ach_players_get in ach_players.c. */
#ifndef ACH_PLAYERS_H
#define ACH_PLAYERS_H

#include <ctype.h>
#include <string.h>
#include "../achievements.h"

#ifdef __cplusplus
extern "C" {
#endif

/* seats: the keys by seat (players.h playerSeat), "" = Guest. connected(seat): a pad answers on
 * it. known(key, name, size): 1 the Mii is on the console (name filled), 0 it is not (its
 * seat plays as Guest), -1 not known (taken as there). Returns how many Miis (at most
 * ACH_PLAYERS_MAX), in seat order, each once. */
static inline unsigned ach_players_pick(const char (*seats)[256], int nseats, int (*connected)(int seat),
                                        int (*known)(const char *key, char *name, unsigned size),
                                        achievement_player_t *out)
{
	unsigned n = 0, i, j;
	int seat;

	for (seat = 0; seat < nseats && n < ACH_PLAYERS_MAX; seat++) {
		achievement_player_t p;
		int r;
		if (strlen(seats[seat]) != ACH_KEY_LEN || !connected(seat))
			continue;
		memset(&p, 0, sizeof p);
		for (i = 0; i < ACH_KEY_LEN; i++)
			p.key[i] = (char)tolower((unsigned char)seats[seat][i]);
		for (j = 0; j < n && strcmp(out[j].key, p.key); j++)
			;
		if (j < n)
			continue;
		r = known(p.key, p.name, sizeof p.name);
		if (r == 0)
			continue;
		if (r < 0 || !p.name[0])
			strcpy(p.name, "Mii");
		out[n++] = p;
	}
	return n;
}

/* The hook for achievements_players (GamecubeMain.cpp sets it before achievements_load) */
unsigned ach_players_get(achievement_player_t *out);

#ifdef __cplusplus
}
#endif
#endif
