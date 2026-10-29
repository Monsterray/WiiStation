/* players_recent_test.c -- Gamecube/players_recent.h on the PC.
 *   clang tests/players_recent_test.c -o .runs/players_recent_test.exe && .runs/players_recent_test.exe */
#include <assert.h>
#include "../Gamecube/players_recent.h"

#define A "82625a00c2123456"
#define B "82625dd1c2123456"
#define C "826261a2c2123456"

int main(void)
{
	char seats[4][256] = { B, "", A, "bad" };
	char recent[256] = C "," A;
	int i;

	players_recent_merge(recent, sizeof recent, (const char (*)[256])seats, 4);
	assert(!strcmp(recent, B "," A "," C));            /* seated first, no duplicate, junk dropped */
	assert(players_recent_rank(recent, B) < players_recent_rank(recent, C));
	assert(players_recent_rank(recent, "826265730000ffff") == 1 << 20);
	assert(players_recent_rank(recent, "") == 1 << 20);

	/* full: 15 keys fit 255 characters, the oldest falls off */
	recent[0] = 0;
	for (i = 0; i < 20; i++) {
		char one[1][256];
		snprintf(one[0], sizeof one[0], "%016x", i);
		players_recent_merge(recent, sizeof recent, (const char (*)[256])one, 1);
	}
	assert(strlen(recent) == 15 * 17 - 1);
	assert(!strncmp(recent, "0000000000000013", 16));   /* the last seated is first */
	assert(!strstr(recent, "0000000000000004"));
	puts("players_recent: ok");
	return 0;
}
