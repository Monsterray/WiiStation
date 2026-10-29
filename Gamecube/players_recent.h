/* players_recent.h -- the list of the Miis last seated (players.ini PlayersRecent): keys of 16
 * hex characters, comma-separated, most recent first, as many as fit the setting's 255
 * characters (15). Pure C, no libogc: tests/players_recent_test.c. */
#ifndef PLAYERS_RECENT_H
#define PLAYERS_RECENT_H

#include <stdio.h>
#include <string.h>

#define PLAYERS_RECENT_KEY 16

/* A key at the end of out, unless it is no key, out has it already or it does not fit. */
static inline void players_recent_add(char *out, size_t size, const char *key, size_t len)
{
	size_t n = strlen(out);
	char k[PLAYERS_RECENT_KEY + 1];
	if (len != PLAYERS_RECENT_KEY || n + 1 + len >= size)
		return;
	memcpy(k, key, len);
	k[len] = 0;
	if (strstr(out, k))
		return;
	snprintf(out + n, size - n, "%s%s", n ? "," : "", k);
}

/* The seated keys first (in seat order, Guest skipped), then the list as it was. */
static inline void players_recent_merge(char *recent, size_t size, const char (*seats)[256], int nseats)
{
	char out[256] = "";
	const char *p = recent;
	int s;
	if (size > sizeof out)
		size = sizeof out;
	for (s = 0; s < nseats; s++)
		players_recent_add(out, size, seats[s], strlen(seats[s]));
	while (*p) {
		size_t n = strcspn(p, ",");
		players_recent_add(out, size, p, n);
		p += n + (p[n] == ',');
	}
	snprintf(recent, size, "%s", out);
}

/* Where a key is in the list: smaller is more recent; one not in it comes after all. */
static inline int players_recent_rank(const char *recent, const char *key)
{
	const char *at = *key ? strstr(recent, key) : NULL;
	return at ? (int)(at - recent) : 1 << 20;
}

#endif
