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
/* the pinned rc_client's own record of which game a disc hash belongs to (disc swaps) */
#include "deps/rcheevos/src/client/rc_client_internal.h"
#include "psxcommon.h"
#include "plugins.h"
#include "Gamecube/ra_transport.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* SysPrintf: psxcommon.h */

#define ACCOUNT_FILE "sd:/wiistation/achievements/ra_account.ini"
#define ACCOUNT_TEMP ACCOUNT_FILE ".new"

static rc_client_t *client;
static int logged_in, login_busy, game_loaded, beaten_seen, mastered_seen;
static int media_pending;   /* a swapped disc is being checked: nothing is evaluated until then */
static char state[96] = "Not started";   /* what the menus show when no set is loaded */

const char *achievements_official_status(void)
{
	static char line[96];
	const rc_client_game_t *g;
	rc_client_user_game_summary_t s;
	if (!client || !game_loaded || media_pending || !(g = rc_client_get_game_info(client)))
		return state;
	rc_client_get_user_game_summary(client, &s);
	snprintf(line, sizeof line, "%u of %u earned (Official)",
	         (unsigned)s.num_unlocked_achievements, (unsigned)s.num_promoted_achievements);
	return line;
}
static char game_hash[33];   /* the loaded disc's PS1 hash, to load once the login is done */

/* ---- what the menus list: the achievements and the leaderboards of the loaded set ---- */

#define MAX_LISTED 512
/* rc_client.c's RC_CLIENT_ACHIEVEMENT_WARNING_ID: a made-up "Warning: Unknown Emulator" entry,
 * always earned, that the server's answer adds while RetroAchievements does not know the
 * client. It is no achievement of the set: the list and its counts leave it out. */
#define ACHIEVEMENT_WARNING_ID 101000001
static rc_client_achievement_list_t *list;
static const rc_client_achievement_t *listed[MAX_LISTED];
static unsigned listed_n;
static int list_stale = 1;   /* read rc_client's list again before the next use */

#define MAX_BOARDS 256
static rc_client_leaderboard_list_t *boards;
static const rc_client_leaderboard_t *boarded[MAX_BOARDS];
static unsigned boarded_n;
static int boards_built;

/* One board's scores at a time: the best ten and the player's own. `generation` tells a late
 * answer for a board no longer shown from the current one. */
#define TOP_SCORES 10
static struct {
	uint32_t board;
	int started, top_state, mine_state;   /* 0 waiting, 1 here, -1 failed */
	unsigned generation, n_top;
	achievement_score_t top[TOP_SCORES], mine;
} sc;

/* Before rc_client unloads the set: the lists point into it */
static void lists_drop(void)
{
	if (list) rc_client_destroy_achievement_list(list);
	if (boards) rc_client_destroy_leaderboard_list(boards);
	list = NULL;
	boards = NULL;
	listed_n = boarded_n = 0;
	list_stale = 1;
	boards_built = 0;
	sc.started = 0;
}

static int set_ready(void) { return client && game_loaded && !media_pending; }

static void list_build(void)
{
	uint32_t b, i;
	if (!list_stale) return;
	if (list) rc_client_destroy_achievement_list(list);
	list = NULL;
	listed_n = 0;
	if (!set_ready()) return;   /* stays stale: it is read once the set is in */
	list = rc_client_create_achievement_list(client,
		rc_client_get_unpromoted_enabled(client) ? RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED_AND_UNPROMOTED
		                                          : RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED,
		RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	list_stale = 0;
	if (!list) return;
	for (b = 0; b < list->num_buckets; b++)   /* rc_client's order: challenges, recent, almost there, locked, earned */
		for (i = 0; i < list->buckets[b].num_achievements && listed_n < MAX_LISTED; i++)
			if (list->buckets[b].achievements[i]->id != ACHIEVEMENT_WARNING_ID)
				listed[listed_n++] = list->buckets[b].achievements[i];
}

static void boards_build(void)
{
	uint32_t b, i;
	if (boards_built || !set_ready()) return;
	boards_built = 1;
	boards = rc_client_create_leaderboard_list(client, RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE);
	if (!boards) return;
	for (b = 0; b < boards->num_buckets; b++)
		for (i = 0; i < boards->buckets[b].num_leaderboards && boarded_n < MAX_BOARDS; i++)
			boarded[boarded_n++] = boards->buckets[b].leaderboards[i];
}

unsigned achievements_official_count(void) { list_build(); return listed_n; }

unsigned achievements_official_earned(void)
{
	unsigned i, n = 0;
	list_build();
	for (i = 0; i < listed_n; i++) n += listed[i]->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
	return n;
}

void achievements_official_list_refresh(void)
{
	list_stale = 1;
	sc.started = 0;   /* and the scores are asked for again */
}

int achievements_official_entry(unsigned i, achievement_entry_t *e)
{
	const rc_client_achievement_t *a;
	list_build();
	if (i >= listed_n) return -1;
	a = listed[i];
	memset(e, 0, sizeof *e);
	e->title = a->title ? a->title : "";
	e->desc = a->description ? a->description : "";
	e->badge = a->badge_name;
	e->id = a->id;
	e->points = a->points;
	e->rarity_x100 = (int)(a->rarity * 100 + 0.5f);
	e->earned = a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
	e->type = a->type <= RC_CLIENT_ACHIEVEMENT_TYPE_WIN ? a->type : ACH_TYPE_STANDARD;
	e->unofficial = a->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNPROMOTED;
	if (!e->earned && a->measured_progress[0]) {
		snprintf(e->progress, sizeof e->progress, "%s", a->measured_progress);
		e->percent = a->measured_percent >= 100 ? 100 : (unsigned char)(a->measured_percent + 0.5f);
	}
	return 0;
}

/* Rich presence: the game's own line about what the player is doing. rc_client works it out
 * every few seconds in do_frame and sends it to the server with its ping. */
const char *achievements_official_presence(void)
{
	static char line[128];
	if (!set_ready() || !rc_client_has_rich_presence(client)) return "";
	rc_client_get_rich_presence_message(client, line, sizeof line);
	{	/* the menu font has ASCII only: an emoji (Spyro's set opens with a flag, "[🇺🇸] ...")
		 * is dropped, with the brackets it leaves empty and the space after them */
		char *r = line, *w = line;
		for (; *r; r++)
			if ((unsigned char)*r < 0x80) *w++ = *r;
		*w = 0;
		while ((r = strstr(line, "[]")) || (r = strstr(line, "()"))) memmove(r, r + 2, strlen(r + 2) + 1);
		for (r = line; *r == ' '; r++) ;
		memmove(line, r, strlen(r) + 1);
	}
	return line;
}

unsigned achievements_official_board_count(void) { boards_build(); return boarded_n; }

int achievements_official_board(unsigned i, const char **title, const char **desc)
{
	boards_build();
	if (i >= boarded_n) return -1;
	if (title) *title = boarded[i]->title ? boarded[i]->title : "";
	if (desc) *desc = boarded[i]->description ? boarded[i]->description : "";
	return 0;
}

static void copy_score(achievement_score_t *o, const rc_client_leaderboard_entry_t *e)
{
	o->rank = e->rank;
	snprintf(o->user, sizeof o->user, "%s", e->user ? e->user : "");
	snprintf(o->score, sizeof o->score, "%s", e->display);
}

static void top_cb(int result, const char *error, rc_client_leaderboard_entry_list_t *l, rc_client_t *c, void *gen)
{
	uint32_t i;
	(void)error; (void)c;
	if ((uintptr_t)gen == sc.generation) {
		sc.n_top = 0;
		if (result == RC_OK && l)
			for (i = 0; i < l->num_entries && sc.n_top < TOP_SCORES; i++)
				copy_score(&sc.top[sc.n_top++], &l->entries[i]);
		sc.top_state = result == RC_OK ? 1 : -1;
	}
	if (l) rc_client_destroy_leaderboard_entry_list(l);
}

static void mine_cb(int result, const char *error, rc_client_leaderboard_entry_list_t *l, rc_client_t *c, void *gen)
{
	(void)error; (void)c;
	if ((uintptr_t)gen == sc.generation) {
		memset(&sc.mine, 0, sizeof sc.mine);   /* rank 0: the player has no score on it */
		if (result == RC_OK && l && l->user_index >= 0 && (uint32_t)l->user_index < l->num_entries)
			copy_score(&sc.mine, &l->entries[l->user_index]);
		sc.mine_state = result == RC_OK ? 1 : -1;
	}
	if (l) rc_client_destroy_leaderboard_entry_list(l);
}

int achievements_official_board_scores(unsigned i, achievement_score_t *top, unsigned *n, achievement_score_t *mine)
{
	unsigned k;
	boards_build();
	if (i >= boarded_n) return -1;
	if (!sc.started || sc.board != boarded[i]->id) {
		sc.started = 1;
		sc.board = boarded[i]->id;
		sc.top_state = sc.mine_state = 0;
		sc.generation++;
		rc_client_begin_fetch_leaderboard_entries(client, sc.board, 1, TOP_SCORES, top_cb, (void *)(uintptr_t)sc.generation);
		rc_client_begin_fetch_leaderboard_entries_around_user(client, sc.board, 1, mine_cb, (void *)(uintptr_t)sc.generation);
	}
	if (sc.top_state < 0 || sc.mine_state < 0) return -1;
	if (!sc.top_state || !sc.mine_state) return 0;
	k = *n < sc.n_top ? *n : sc.n_top;
	memcpy(top, sc.top, k * sizeof *top);
	*n = k;
	*mine = sc.mine;
	return 1;
}

/* ---- save states and reset: the set's progress (hit counts, measured values) ---- */

void achievements_official_reset(void)
{
	if (set_ready()) rc_client_reset(client);
}

int achievements_official_state_save(void *data, unsigned size)
{
	size_t n;
	if (!set_ready()) return 0;
	n = rc_client_progress_size(client);
	if (!n || n + 4 > ACHIEVEMENTS_STATE_MAX) return 0;
	if (!data) return (int)(n + 4);
	if (size < n + 4) return 0;
	memcpy(data, "WAO1", 4);   /* not the Local "WAC1": a state never crosses modes */
	if (rc_client_serialize_progress_sized(client, (uint8_t *)data + 4, n) != RC_OK) return 0;
	return (int)(n + 4);
}

int achievements_official_state_load(const void *data, unsigned size)
{
	const uint8_t *in = data;
	if (!set_ready()) return 0;
	if (in && size > 4 && !memcmp(in, "WAO1", 4) &&
	    rc_client_deserialize_progress_sized(client, in + 4, size - 4) == RC_OK)
		return 1;
	rc_client_deserialize_progress_sized(client, NULL, 0);   /* no progress in the state: it starts again */
	return 0;
}

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

/* A challenge (one step away) or measured progress, for the small indicators */
static void indicator(unsigned char kind, const rc_client_achievement_t *a)
{
	achievement_notice_t n;
	if (!achievements_notify || !a) return;
	memset(&n, 0, sizeof n);
	n.kind = kind;
	n.id = a->id;
	n.title = a->title;
	n.badge = a->badge_name;
	n.progress = a->measured_progress;
	n.percent = a->measured_percent >= 100 ? 100 : (unsigned char)(a->measured_percent + 0.5f);
	n.rarity_x100 = -1;
	achievements_notify(&n);
}

static void event(const rc_client_event_t *e, rc_client_t *c)
{
	(void)c;
	switch (e->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		indicator(ACH_NOTICE_CHALLENGE_ON, e->achievement);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		indicator(ACH_NOTICE_CHALLENGE_OFF, e->achievement);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		indicator(ACH_NOTICE_PROGRESS, e->achievement);   /* the toast hides it after a while itself */
		break;
	case RC_CLIENT_EVENT_GAME_COMPLETED:
		SysPrintf("Achievements: every achievement of the set earned\n");
		break;
	case RC_CLIENT_EVENT_SUBSET_COMPLETED:
		SysPrintf("Achievements: every achievement of a subset earned\n");
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
		const rc_client_achievement_t *a = e->achievement;
		const rc_client_game_t *g = rc_client_get_game_info(client);
		rc_client_user_game_summary_t s;
		achievement_notice_t n;
		rc_client_get_user_game_summary(client, &s);
		list_stale = 1;   /* the menu's list moves it to Earned */
		memset(&n, 0, sizeof n);
		n.kind = ACH_NOTICE_UNLOCK;
		n.id = a->id;
		n.badge = a->badge_name;
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
		snprintf(state, sizeof state, "No set for this disc");
		return;
	}
	game_loaded = 1;
	beaten_seen = is_beaten();
	mastered_seen = is_mastered();
	g = rc_client_get_game_info(client);
	rc_client_get_user_game_summary(client, &s);
	SysPrintf("Achievements: %s, %u of %u earned (Official, Casual%s%s)\n", g && g->title ? g->title : "?",
	          (unsigned)s.num_unlocked_achievements, (unsigned)s.num_promoted_achievements,
	          rc_client_get_encore_mode_enabled(client) ? ", Encore" : "",
	          rc_client_get_unpromoted_enabled(client) ? ", Unofficial" : "");
	list_stale = 1;
	if (achievements_notify) {   /* the game card: the set is on */
		achievement_notice_t n;
		memset(&n, 0, sizeof n);
		n.kind = ACH_NOTICE_GAME;
		n.game = g && g->title ? g->title : "";
		n.badge = g && g->badge_name ? g->badge_name : "";
		n.desc = "Official";
		n.rarity_x100 = -1;
		n.earned = s.num_unlocked_achievements;
		n.count = s.num_promoted_achievements;
		n.points = s.points_unlocked;
		n.total_points = s.points_available;
		achievements_notify(&n);
	}
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
		snprintf(state, sizeof state, "%s", ra_transport_available() ? "Login failed" : "No secure connection");
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
		snprintf(state, sizeof state, "No account (ra_account.ini)");
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
	/* before the set loads: rc_client reads both when it activates the achievements */
	rc_client_set_unpromoted_enabled(client, achievementUnofficial != 0);
	rc_client_set_encore_mode_enabled(client, achievementEncore != 0);
	rc_client_set_spectator_mode_enabled(client, achievementSpectator != 0);   /* scripted runs: nothing reaches the account */
	if (image && strlen(image) > 4 && !strcasecmp(image + strlen(image) - 4, ".exe")) {
		SysPrintf("Achievements: a .exe has no disc to identify; no achievements for it\n");
		snprintf(state, sizeof state, "None: a .exe has no disc");
		return 0;
	}
	/* the hash now, while nothing else reads the disc; the set loads after the login */
	if (!image || !hash_disc(image)) {
		SysPrintf("Achievements: no PS1 boot executable on this disc; no RetroAchievements set\n");
		snprintf(state, sizeof state, "No PS1 boot file to identify");
		return 0;
	}
	snprintf(state, sizeof state, "Connecting...");
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
	 * load (it validates addresses through read_memory) in its next do_frame. While a swapped
	 * disc is being checked, only idle: no award against a disc that may not be the game's. */
	if (media_pending)
		rc_client_idle(client);
	else
		rc_client_do_frame(client);
}

/* The check of a swapped disc: rc_client looked its hash up. The same game's disc goes on;
 * a disc of another game, or one RetroAchievements does not know, stops the set until the
 * next game load (Docs/ACHIEVEMENTS.md, "Multi-disc games"). */
static void media_changed_cb(int result, const char *error, rc_client_t *c, void *data)
{
	const rc_client_game_t *g = rc_client_get_game_info(client);
	rc_client_game_hash_t *h = g && g->hash ? rc_client_find_game_hash(client, g->hash) : NULL;
	(void)c; (void)data;
	media_pending = 0;
	if (result == RC_OK && g && h && h->game_id == g->id) {
		SysPrintf("Achievements: disc changed; %s goes on\n", g->title ? g->title : "the game");
		return;
	}
	snprintf(state, sizeof state, "Off: not this game's disc");
	SysPrintf("Achievements: this disc is not part of %s (%s); achievements are off until the next game load\n",
	          g && g->title ? g->title : "the game", result == RC_OK ? "another or an unknown disc" : (error ? error : "no answer"));
	lists_drop();
	rc_client_unload_game(client);
	game_loaded = 0;
}

/* A disc swap (loadISOSwap), the new disc already open: hash it now, from the menu, before
 * the game reads it */
void achievements_official_change_media(const char *image)
{
	if (!client) return;
	if (!image || !hash_disc(image)) {
		SysPrintf("Achievements: the new disc has no PS1 boot executable; achievements are off until the next game load\n");
		snprintf(state, sizeof state, "Off: no PS1 boot file");
		lists_drop();
		if (game_loaded) rc_client_unload_game(client);
		game_loaded = 0;
		return;
	}
	if (!game_loaded)   /* the set has not loaded yet (the login is still going): it loads from this disc */
		return;
	media_pending = 1;
	lists_drop();   /* read again once the disc is accepted */
	rc_client_begin_change_media(client, game_hash, media_changed_cb, NULL);
	game_hash[0] = 0;
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
	lists_drop();
	if (game_loaded) rc_client_unload_game(client);
	game_loaded = media_pending = 0;
}

#endif /* HW_RVL */
