/* Official mode: RetroAchievements through rc_client (Docs/ACHIEVEMENTS.md, "Official login"
 * and "Design rules for Official mode"). Wii only; Casual only.
 *
 * The flow, all on the emulator thread (rule 3):
 *   achievements_official_load()  -- game load: the disc's PS1 hash (read now, through the CD
 *                                    plugin), log in once (ra_account.ini), then load the set
 *   achievements_official_frame() -- every emulated frame: rc_client evaluates, completions run
 *   achievements_official_idle()  -- menu and pause: completions, rc_client_idle
 *   event()                       -- an unlock: the toast, with the server's rarity
 *
 * The account file holds User= and Password= until the first login; after it, User= and
 * Token=, and the password is gone. No password or token ever reaches a log. */
#ifdef HW_RVL
#include "achievements.h"
#include "deps/rcheevos/include/rc_client.h"
#include "deps/rcheevos/include/rc_consoles.h"
#include "deps/rcheevos/include/rc_hash.h"
#include "psxcommon.h"
#include "plugins.h"
#include "Gamecube/ra_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* SysPrintf: psxcommon.h */

#define ACCOUNT_FILE "sd:/wiistation/achievements/ra_account.ini"
#define ACCOUNT_TEMP ACCOUNT_FILE ".new"

static rc_client_t *client;
static int logged_in, login_busy, game_loaded, beaten_seen, mastered_seen;
static char game_hash[33];   /* the loaded disc's PS1 hash, to load once the login is done */

/* ---- the account file ---- */

/* User=, and Token= (preferred) or Password=. 0 when the file or the user is missing. */
static int read_account(char *user, size_t user_size, char *secret, size_t secret_size, int *is_token)
{
	char line[256];
	FILE *f = fopen(ACCOUNT_FILE, "r");
	if (!f) f = fopen(ACCOUNT_TEMP, "r");   /* a power cut between the token write and its rename */
	if (!f) return 0;
	user[0] = secret[0] = 0;
	*is_token = 0;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "User=", 5))
			snprintf(user, user_size, "%s", line + 5);
		else if (!strncmp(line, "Token=", 6) && line[6]) {
			snprintf(secret, secret_size, "%s", line + 6);
			*is_token = 1;
		} else if (!strncmp(line, "Password=", 9) && !*is_token)
			snprintf(secret, secret_size, "%s", line + 9);
	}
	memset(line, 0, sizeof line);
	fclose(f);
	return user[0] && secret[0];
}

/* After a password login: User= and Token= only. Written to a new file first, so a power cut
 * leaves either the old file or the new one. */
static void store_token(const char *user, const char *token)
{
	FILE *f = fopen(ACCOUNT_TEMP, "w");
	int ok;
	if (!f) {
		SysPrintf("Achievements: cannot write the account file; the password stays in it\n");
		return;
	}
	ok = fprintf(f, "User=%s\nToken=%s\n", user, token) > 0;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		remove(ACCOUNT_TEMP);
		SysPrintf("Achievements: cannot write the account file; the password stays in it\n");
		return;
	}
	remove(ACCOUNT_FILE);
	if (rename(ACCOUNT_TEMP, ACCOUNT_FILE) != 0)
		SysPrintf("Achievements: the account file is %s until the next login\n", ACCOUNT_TEMP);
}

/* ---- rc_client's callbacks ---- */

static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t size, rc_client_t *c)
{
	(void)c;
	return achievements_read_memory(address, buffer, size, NULL);
}

static void server_call(const rc_api_request_t *request, rc_client_server_callback_t done, void *done_data,
                        rc_client_t *c)
{
	(void)c;
	ra_transport_post(request->url, request->post_data, request->content_type, done, done_data);
}

/* Beaten, as RetroAchievements counts it: every Progression earned, and a Win if there is one */
static int is_beaten(void)
{
	rc_client_achievement_list_t *list = rc_client_create_achievement_list(client,
		RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	uint32_t b, i;
	int typed = 0, wins = 0, won = 0, all = 1;
	if (!list) return 0;
	for (b = 0; b < list->num_buckets; b++)
		for (i = 0; i < list->buckets[b].num_achievements; i++) {
			const rc_client_achievement_t *a = list->buckets[b].achievements[i];
			if (a->type == RC_CLIENT_ACHIEVEMENT_TYPE_PROGRESSION) {
				typed = 1;
				if (!a->unlocked) all = 0;
			} else if (a->type == RC_CLIENT_ACHIEVEMENT_TYPE_WIN) {
				typed = wins = 1;
				if (a->unlocked) won = 1;
			}
		}
	rc_client_destroy_achievement_list(list);
	return typed && all && (!wins || won);
}

static int is_mastered(void)
{
	rc_client_user_game_summary_t s;
	rc_client_get_user_game_summary(client, &s);
	return s.num_promoted_achievements && s.num_unlocked_achievements >= s.num_promoted_achievements;
}

static void event(const rc_client_event_t *e, rc_client_t *c)
{
	(void)c;
	switch (e->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
		const rc_client_achievement_t *a = e->achievement;
		const rc_client_game_t *g = rc_client_get_game_info(client);
		rc_client_user_game_summary_t s;
		achievement_notice_t n;
		rc_client_get_user_game_summary(client, &s);
		memset(&n, 0, sizeof n);
		n.title = a->title;
		n.desc = a->description;
		n.game = g && g->title ? g->title : "";
		n.points = a->points;
		n.total_points = s.points_available;
		n.rarity_x100 = (int)(a->rarity * 100 + 0.5f);
		n.earned = s.num_unlocked_achievements;
		n.count = s.num_promoted_achievements;
		n.type = a->type <= RC_CLIENT_ACHIEVEMENT_TYPE_WIN ? a->type : ACH_TYPE_STANDARD;
		n.beaten = !beaten_seen && is_beaten();
		n.mastered = !mastered_seen && is_mastered();
		beaten_seen |= n.beaten;
		mastered_seen |= n.mastered;
		SysPrintf("Achievement unlocked (Official, Casual): %u %s\n", (unsigned)a->id, a->title);
		if (achievements_notify) achievements_notify(&n);
		break;
	}
	case RC_CLIENT_EVENT_SERVER_ERROR:
		SysPrintf("Achievements: server error in %s: %s\n",
		          e->server_error && e->server_error->api ? e->server_error->api : "?",
		          e->server_error && e->server_error->error_message ? e->server_error->error_message : "?");
		break;
	case RC_CLIENT_EVENT_DISCONNECTED:
		SysPrintf("Achievements: offline; unlocks wait and are sent when the connection is back\n");
		break;
	case RC_CLIENT_EVENT_RECONNECTED:
		SysPrintf("Achievements: back online; waiting unlocks sent\n");
		break;
	default:
		break;
	}
}

/* ---- the disc reader for the PS1 hash ----
 *
 * rc_hash reads ISO9660 sectors of the first data track. They come from WiiStation's own CD
 * plugin, as the HLE fast boot reads them (misc.c), so every format WiiStation boots hashes the
 * same way: BIN/CUE, ISO, IMG/CCD, MDF/MDS, TOC, CBN, CHD and PBP (the disc chosen in it).
 * At game load only, before the game runs: the reads move the plugin's position as
 * LoadCdrom's did, and the game seeks before it reads (Docs/ACHIEVEMENTS.md rule 2 asks for a
 * separate handle for a hash made while the game runs: a disc swap). */

static void *hash_open_track(const char *path, uint32_t track)
{
	(void)path;   /* always the loaded image */
	return track == 1 || track == RC_HASH_CDTRACK_FIRST_DATA ? (void *)1 : NULL;
}

static size_t hash_read_sector(void *track, uint32_t sector, void *buffer, size_t size)
{
	unsigned char time[3], *buf;
	uint32_t lba = sector + 150;   /* sector 0 is 00:02:00 */
	(void)track;
	time[0] = (unsigned char)(((lba / 4500) / 10) << 4 | (lba / 4500) % 10);   /* BCD, as CDR_readTrack takes */
	time[1] = (unsigned char)(((lba / 75 % 60) / 10) << 4 | (lba / 75 % 60) % 10);
	time[2] = (unsigned char)(((lba % 75) / 10) << 4 | (lba % 75) % 10);
	if (CDR_readTrack(time) == 0 || !(buf = CDR_getBuffer()))
		return 0;
	if (size > 2048) size = 2048;
	memcpy(buffer, buf + 12, size);   /* the user data of a mode 2 form 1 sector */
	return size;
}

static void hash_close_track(void *track) { (void)track; }
static uint32_t hash_first_sector(void *track) { (void)track; return 0; }
static void hash_error(const char *message) { SysPrintf("Achievements: hash: %s\n", message); }

/* The PS1 hash of the loaded disc, into game_hash. 0 when it has no PS1 boot executable. */
static int hash_disc(const char *image)
{
	static rc_hash_cdreader_t reader = { hash_open_track, hash_read_sector, hash_close_track, hash_first_sector, NULL };
	rc_hash_init_custom_cdreader(&reader);
	rc_hash_init_error_message_callback(hash_error);
	game_hash[0] = 0;
	if (!rc_hash_generate_from_file(game_hash, RC_CONSOLE_PLAYSTATION, image)) {
		game_hash[0] = 0;
		return 0;
	}
	SysPrintf("Achievements: disc hash %s\n", game_hash);   /* public: the set's identity */
	return 1;
}

/* ---- loading the set, and the login ---- */

static void game_loaded_cb(int result, const char *error, rc_client_t *c, void *data)
{
	const rc_client_game_t *g;
	rc_client_user_game_summary_t s;
	(void)c; (void)data;
	if (result != RC_OK) {
		SysPrintf("Achievements: no RetroAchievements set for this disc (%s)\n", error ? error : "unknown");
		return;
	}
	game_loaded = 1;
	beaten_seen = is_beaten();
	mastered_seen = is_mastered();
	g = rc_client_get_game_info(client);
	rc_client_get_user_game_summary(client, &s);
	SysPrintf("Achievements: %s, %u of %u earned (Official, Casual)\n", g && g->title ? g->title : "?",
	          (unsigned)s.num_unlocked_achievements, (unsigned)s.num_promoted_achievements);
}

static void load_set(void)
{
	if (!game_hash[0]) return;
	rc_client_begin_load_game(client, game_hash, game_loaded_cb, NULL);
	game_hash[0] = 0;
}

static void login_cb(int result, const char *error, rc_client_t *c, void *used_password)
{
	const rc_client_user_t *u;
	(void)c;
	login_busy = 0;
	if (result != RC_OK) {
		SysPrintf("Achievements: login failed: %s\n",
		          ra_transport_available() ? (error ? error : "unknown") : "this build has no secure connection yet");
		game_hash[0] = 0;
		return;
	}
	logged_in = 1;
	u = rc_client_get_user_info(client);
	SysPrintf("Achievements: logged in as %s\n", u && u->display_name ? u->display_name : "?");
	if (used_password && u && u->username && u->token)
		store_token(u->username, u->token);
	load_set();
}

static void login(void)
{
	char user[64], secret[256];
	int is_token;
	if (!read_account(user, sizeof user, secret, sizeof secret, &is_token)) {
		SysPrintf("Achievements: no account; put User= and Password= in %s\n", ACCOUNT_FILE);
		game_hash[0] = 0;
		return;
	}
	login_busy = 1;
	if (is_token)
		rc_client_begin_login_with_token(client, user, secret, login_cb, NULL);
	else
		rc_client_begin_login_with_password(client, user, secret, login_cb, (void *)1);
	memset(secret, 0, sizeof secret);   /* rc_client keeps its own copy for the request */
}

/* ---- the engine's entry points (achievements.c) ---- */

int achievements_official_load(const char *image)
{
	if (!client) {
		client = rc_client_create(read_memory, server_call);
		if (!client) {
			SysPrintf("Achievements: out of memory for RetroAchievements\n");
			return 0;
		}
		rc_client_set_event_handler(client, event);
		rc_client_set_hardcore_enabled(client, 0);   /* Casual only: Hardcore needs RA's approval */
		rc_client_set_allow_background_memory_reads(client, 0);
	}
	/* the hash now, while nothing else reads the disc; the set loads after the login */
	if (!image || !hash_disc(image)) {
		SysPrintf("Achievements: no PS1 boot executable on this disc; no RetroAchievements set\n");
		return 0;
	}
	if (logged_in)
		load_set();
	else if (!login_busy)
		login();
	return 0;   /* the set arrives later, asynchronously */
}

void achievements_official_frame(void)
{
	if (!client) return;
	ra_transport_poll();
	/* every frame, loaded or not: with background memory reads off, rc_client finishes a game
	 * load (it validates addresses through read_memory) in its next do_frame */
	rc_client_do_frame(client);
}

void achievements_official_idle(void)
{
	if (!client) return;
	ra_transport_poll();
	rc_client_idle(client);
}

void achievements_official_close(void)
{
	if (!client) return;
	game_hash[0] = 0;
	if (game_loaded) rc_client_unload_game(client);
	game_loaded = 0;
}

#endif /* HW_RVL */
