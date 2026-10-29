/* achievements_official_alone_test.c -- Official mode with no Local player (achievements.c): the
 * page is the Official side alone, and it names who plays it: the Mii the account is linked to
 * (its name from a seat, or from the Mii database when no connected seat has it), else this
 * Wii's account. It said Guest. scripts/test-achievements.sh builds it with the rcheevos runtime. */
#define HOSTCHECK_MAIN
#include "host/hostcheck.h"
#include "../achievements.h"
#include <stdarg.h>
#include <stdlib.h>

static unsigned char ram[0x200000], scratch[0x400];
unsigned char *psxM = ram, *psxH = scratch;

void SysPrintf(const char *fmt, ...)
{
	(void)fmt;
}

/* The Official hooks achievements.c finds by name (weak there): the page is rc_client's */
unsigned achievements_official_count(void) { return 3; }

#define KEY_OFF "82625a00c2123456"   /* Kat: plays Official */
#define KEY_B "82625dd1c2123456"     /* Ollie */

static achievement_player_t seated[ACH_PLAYERS_MAX];
static unsigned seated_n;
static unsigned hook(achievement_player_t *out)
{
	memcpy(out, seated, sizeof seated);
	return seated_n;
}

static void seat(const char *key, const char *name)
{
	memset(seated, 0, sizeof seated);
	seated_n = 0;
	if (key) {
		snprintf(seated[0].key, sizeof seated[0].key, "%s", key);
		snprintf(seated[0].name, sizeof seated[0].name, "%s", name);
		seated_n = 1;
	}
}

/* the Mii database: Kat is on the console, nobody else */
static int lookup(const char *key, char *name, unsigned size)
{
	if (strcmp(key, KEY_OFF))
		return 0;
	snprintf(name, size, "Kat");
	return 1;
}

static void setup(void)
{
	seat(NULL, NULL);
	achievementsEnabled = ACH_OFFICIAL;
	achievements_players = hook;
	achievements_player_lookup = NULL;
	achievements_close();
}

static void test_the_official_mii_is_named(void)
{
	seat(KEY_OFF, "Kat");
	CHECK(achievements_ra_plan(KEY_OFF) == 0, "nobody else earns Local");
	CHECK(achievements_player_count() == 1, "one player on the page");
	CHECK(!strcmp(achievements_player_key(0), KEY_OFF), "key %s", achievements_player_key(0));
	CHECK(!strcmp(achievements_player_name(0), "Kat"), "name %s", achievements_player_name(0));
}

static void test_a_mii_on_no_connected_seat_keeps_its_name(void)
{
	seat(NULL, NULL);   /* its pad is not connected: the hook does not give it */
	CHECK(achievements_ra_plan(KEY_OFF) == 0, "nobody earns Local");
	CHECK(!strcmp(achievements_player_name(0), "Mii"), "no lookup: Mii, got %s", achievements_player_name(0));
	achievements_player_lookup = lookup;
	CHECK(achievements_ra_plan(KEY_OFF) == 0, "nobody earns Local");
	CHECK(!strcmp(achievements_player_name(0), "Kat"), "the database's name, got %s", achievements_player_name(0));
	CHECK(!strcmp(achievements_player_key(0), KEY_OFF), "key");
}

static void test_the_wiis_own_account(void)
{
	seat(KEY_B, "Ollie");
	CHECK(achievements_ra_plan("") == 1, "Ollie earns Local, this Wii's account plays Official");
	achievements_close();
	seat(NULL, NULL);
	CHECK(achievements_ra_plan("") == 0, "Guest alone");
	CHECK(!achievements_player_key(0)[0] && !strcmp(achievements_player_name(0), "This Wii"), "this Wii, got %s",
	      achievements_player_name(0));
	CHECK(!achievements_player_name(1)[0], "no second player");
}

static const struct hc_test tests[] = {
	{ "official_mii_is_named", test_the_official_mii_is_named, false },
	{ "mii_on_no_connected_seat_keeps_its_name", test_a_mii_on_no_connected_seat_keeps_its_name, false },
	{ "the_wiis_own_account", test_the_wiis_own_account, false },
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return hc_main(tests, HC_COUNT(tests), 1, argv, setup);
}
