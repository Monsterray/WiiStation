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
 * Token=, and the password is gone. No password or token ever reaches a log.
 *
 * Which account file (Docs/ACHIEVEMENTS.md, Players and Miis steps 4a and 4c): the Mii that plays Official
 * (players_official) gives it by its link: achievements/ra/<name>.ini, or this Wii's
 * ra_account.ini ("*"; also when no Mii is linked). A game load that wants another account than
 * the logged-in one logs out and in again. When that account does not work, this Wii's account
 * loads the set in spectator mode (nothing is sent) and that Mii earns Local with the others.
 * The Miis seated besides the Official one earn the same set Local (achievements_ra_*), with
 * encore mode on so what the account already has stays active for them; can_submit keeps an
 * unlock the account already has from going to the server again.
 *
 * Local mode (setting Achievements = 4, Docs/ACHIEVEMENTS.md "Local mode") uses the same client
 * with nothing sent: spectator mode, every seated Mii (or Guest) earns into the Local journal, and
 * server_call below lets only the downloads through (the login, the game id, the set), keeps
 * the two downloads on the card, and answers a game load from the card with no network and no
 * account when the card has the set (Gamecube/ach_ra_cache.h). */
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
#include "Gamecube/ach_account.h"
#include "Gamecube/ach_progress.h"
#include "Gamecube/ach_ra_cache.h"
#include "Gamecube/players.h"
#include "mem2_manager.h"
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>

/* SysPrintf: psxcommon.h */

#define ACCOUNT_PATH 96   /* an account file's path: ach_account.h's longest, with room */
#define ACCOUNT_TEMP_SUFFIX ".new"

static rc_client_t *client;
static int logged_in, login_busy, game_loaded, beaten_seen, mastered_seen;
static int load_busy;    /* the set was asked for; its answer (and the parse) is not in yet */
static int login_tried;  /* the menu's early login ran once */
static int media_pending;   /* a swapped disc is being checked: nothing is evaluated until then */
static char state[96] = "Not started";   /* what the menus show when no set is loaded */

/* The account of the game load (choose_account), the one logged in, the one a login in flight
 * is for. A login's answer for an older attempt (generation) is ignored. */
static char want_path[ACCOUNT_PATH], want_key[PLAYERS_KEY];   /* the Official Mii's key: "" none */
static char session_path[ACCOUNT_PATH], login_path[ACCOUNT_PATH];
static unsigned login_gen;
static int demoted;        /* the Official Mii's account failed: it earns Local, this Wii's account loads the set */
static char demote_reason[48];
static unsigned local_planned;   /* Miis that earn Local next to the Official side, planned at the game load */

/* Local mode (this game load): the set only, nothing sent. offline: the card has the set, so no
 * login and no network (the login is made up); net_failed: a request got no answer from the
 * server; local_retry: the set did not load, look at the card's copy (from the frame, not from
 * inside rc_client's callback). */
static int local_mode, offline, net_failed, local_retry;
static char local_hash[33];   /* the disc's hash, to load again from the card */

/* The unlocks the account had when the set loaded, and those it got since: encore mode lets them
 * trigger again for the Local players; none goes to the server a second time (can_submit) */
#define MAX_HAD 512
static uint32_t had[MAX_HAD];
static unsigned had_n;
static int had_has(uint32_t id)
{
	unsigned i;
	for (i = 0; i < had_n; i++)
		if (had[i] == id) return 1;
	return 0;
}
static void had_add(uint32_t id) { if (!had_has(id) && had_n < MAX_HAD) had[had_n++] = id; }

const char *achievements_official_status(void)
{
	static char line[96];
	const rc_client_game_t *g;
	rc_client_user_game_summary_t s;
	if (!client || !game_loaded || media_pending || !(g = rc_client_get_game_info(client)))
		return state;
	if (local_mode) {   /* the Local side's own count (the union of its players) */
		snprintf(line, sizeof line, "%u of %u earned (Local)", achievements_earned(), achievements_count());
		return line;
	}
	if (demoted)
		return "Local (account failed)";
	rc_client_get_user_game_summary(client, &s);
	snprintf(line, sizeof line, "%u of %u earned (Official)",
	         (unsigned)s.num_unlocked_achievements, (unsigned)s.num_promoted_achievements);
	return line;
}
static char game_hash[33];   /* the loaded disc's PS1 hash, to load once the login is done */

const char *achievements_official_title(void)
{
	const rc_client_game_t *g;
	if (!client || !game_loaded || media_pending || !(g = rc_client_get_game_info(client)) || !g->title) return "";
	return g->title;
}

/* The folder of this game's unlock pictures: its RetroAchievements id */
const char *achievements_official_set_id(void)
{
	static char id[ACH_SET_ID_MAX];
	const rc_client_game_t *g;
	if (!client || !game_loaded || media_pending || !(g = rc_client_get_game_info(client)) || !g->id)
		return "";
	snprintf(id, sizeof id, "%u", (unsigned)g->id);
	return id;
}

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

/* The progress pop-up rule (Gamecube/ach_progress.h): the step each measured achievement was
 * last at, and the settling time after a load, reset, disc swap or state load */
static ach_steps_t steps;
static unsigned settle;
static void steps_restart(void) { ach_steps_reset(&steps); settle = ACH_SETTLE_FRAMES; }

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

/* The account has it: unlocked on the server, or earned now. In encore mode an earned one stays
 * active (its state), so the state alone does not tell. */
static int has_it(const rc_client_achievement_t *a)
{
	return a->unlocked || a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
}

unsigned achievements_official_earned(void)
{
	unsigned i, n = 0;
	list_build();
	for (i = 0; i < listed_n; i++) n += has_it(listed[i]);
	return n;
}

/* The settling time is over: each measured achievement's step now is its baseline, so the first
 * change after it counts (rc_client sends no event for a first reading) */
static void steps_seed(void)
{
	unsigned i;
	list_build();
	for (i = 0; i < listed_n; i++)
		if (!has_it(listed[i]) && listed[i]->measured_progress[0])
			ach_steps_update(&steps, listed[i]->id, (unsigned)listed[i]->measured_percent);
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
	e->earned = has_it(a);
	e->type = a->type <= RC_CLIENT_ACHIEVEMENT_TYPE_WIN ? a->type : ACH_TYPE_STANDARD;
	e->unofficial = a->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNPROMOTED;
	if (e->earned && a->unlock_time > 0) e->when = (unsigned)a->unlock_time;
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
	steps_restart();
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
	steps_restart();
	if (in && size > 4 && !memcmp(in, "WAO1", 4) &&
	    rc_client_deserialize_progress_sized(client, in + 4, size - 4) == RC_OK)
		return 1;
	rc_client_deserialize_progress_sized(client, NULL, 0);   /* no progress in the state: it starts again */
	return 0;
}

/* ---- the account file ---- */

/* The temp file next to an account file: path + ".new" */
static int temp_path(const char *path, char *temp, size_t size)
{
	int n = snprintf(temp, size, "%s" ACCOUNT_TEMP_SUFFIX, path);
	return n > 0 && (size_t)n < size;
}

/* User=, and Token= (preferred) or Password=. 0 when the file or the user is missing. */
static int read_account(const char *path, char *user, size_t user_size, char *secret, size_t secret_size, int *is_token)
{
	char line[256], temp[ACCOUNT_PATH + 8];
	FILE *f = fopen(path, "r");
	if (!f && temp_path(path, temp, sizeof temp)) f = fopen(temp, "r");   /* a power cut between the token write and its rename */
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
static void store_token(const char *path, const char *user, const char *token)
{
	char temp[ACCOUNT_PATH + 8];
	FILE *f;
	int ok;
	if (!temp_path(path, temp, sizeof temp) || !(f = fopen(temp, "w"))) {
		SysPrintf("Achievements: cannot write the account file; the password stays in it\n");
		return;
	}
	ok = fprintf(f, "User=%s\nToken=%s\n", user, token) > 0;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		remove(temp);
		SysPrintf("Achievements: cannot write the account file; the password stays in it\n");
		return;
	}
	remove(path);
	if (rename(temp, path) != 0)
		SysPrintf("Achievements: the account file is %s until the next login\n", temp);
}

/* ---- rc_client's callbacks ---- */

static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t size, rc_client_t *c)
{
	(void)c;
	return achievements_read_memory(address, buffer, size, NULL);
}

/* ---- Local mode: the card's copy of the set, and what may leave the console ---- */

typedef struct { rc_client_server_callback_t done; void *data; int kind; char file[ACH_CACHE_PATH]; } local_call_t;

/* The whole file, in MEM2 (a set can be hundreds of KB); NULL when it is not there */
static char *cache_read(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long size;
	char *body = NULL;
	if (!f) return NULL;
	if (!fseek(f, 0, SEEK_END) && (size = ftell(f)) > 0 && size <= ACH_CACHE_MAX && !fseek(f, 0, SEEK_SET) &&
	    (body = _mem2_malloc((uint32_t)size + 1))) {
		if (fread(body, 1, (size_t)size, f) == (size_t)size) {
			body[size] = 0;
			*len = (size_t)size;
		} else {
			_mem2_free(body);
			body = NULL;
		}
	}
	fclose(f);
	return body;
}

/* Through a temp file and a rename, as the account file: a power cut leaves the old copy or the new */
static void cache_write(const char *path, const char *body, size_t len)
{
	char temp[ACH_CACHE_PATH + 8];
	FILE *f;
	int ok;
	mkdir(ACH_ACCOUNT_DIR "ra_cache", 0777);
	if (!temp_path(path, temp, sizeof temp) || !(f = fopen(temp, "wb"))) {
		SysPrintf("Achievements: Local: cannot save the set on the card\n");
		return;
	}
	ok = fwrite(body, 1, len, f) == len;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		remove(temp);
		SysPrintf("Achievements: Local: cannot save the set on the card\n");
		return;
	}
	remove(path);
	if (rename(temp, path) != 0)
		SysPrintf("Achievements: Local: the saved set is %s until the next save\n", temp);
	else
		SysPrintf("Achievements: Local: the set is saved on the card (%u bytes)\n", (unsigned)len);
}

static int local_cached(const char *hash)
{
	char file[ACH_CACHE_PATH];
	struct stat st;
	return ach_cache_hash_path(ACH_REQ_SETS, hash, file, sizeof file) && !stat(file, &st);
}

/* The server's answer to a download: kept on the card when it is worth keeping; a request that
 * got no answer (or a server error) is remembered, so the card's older copy can be used */
static void local_done(const rc_api_server_response_t *resp, void *data)
{
	local_call_t *lc = data;
	if (resp->http_status_code < 0 || resp->http_status_code >= 500)
		net_failed = 1;
	else if (lc->file[0] && resp->http_status_code == 200 && ach_cache_keeps(lc->kind, resp->body, resp->body_length))
		cache_write(lc->file, resp->body, resp->body_length);
	lc->done(resp, lc->data);
	free(lc);
}

/* Local mode, a game load served from the card: the login is made up, the set comes from the file */
static void local_from_card(int kind, const rc_api_request_t *request, rc_client_server_callback_t done, void *done_data)
{
	char file[ACH_CACHE_PATH], body[160], user[40];
	size_t len = 0;
	char *text;
	if (kind == ACH_REQ_LOGIN) {
		if (!ach_req_param(request->post_data, "u", user, sizeof user)) user[0] = 0;
		ach_synth_login(user, body, sizeof body);
		ra_transport_complete(done, done_data, 200, body, strlen(body));
		return;
	}
	if (ach_cache_path(kind, request->post_data, file, sizeof file) && (text = cache_read(file, &len))) {
		SysPrintf("Achievements: Local: %s from the card\n", ach_req_name(kind));
		ra_transport_complete(done, done_data, 200, text, len);
		_mem2_free(text);
		return;
	}
	SysPrintf("Achievements: Local: no saved %s for this disc\n", ach_req_name(kind));
	ra_transport_complete(done, done_data, RC_API_SERVER_RESPONSE_CLIENT_ERROR, NULL, 0);
}

static void server_call(const rc_api_request_t *request, rc_client_server_callback_t done, void *done_data,
                        rc_client_t *c)
{
	local_call_t *lc;
	int kind;
	(void)c;
	if (!local_mode) {
		ra_transport_post(request->url, request->post_data, request->content_type, done, done_data);
		return;
	}
	/* Local mode: only a download may leave; an award, a leaderboard entry, a ping or a session
	 * start never does (spectator mode makes none; this is the second lock) */
	kind = ach_req_kind(request->post_data);
	if (!ach_local_may_send(kind)) {
		SysPrintf("Achievements: Local: a request of kind %s is refused, nothing is sent\n", ach_req_name(kind));
		ra_transport_complete(done, done_data, 200, ACH_REFUSED_BODY, strlen(ACH_REFUSED_BODY));
		return;
	}
	if (offline) {
		local_from_card(kind, request, done, done_data);
		return;
	}
	if (!(lc = calloc(1, sizeof *lc))) {
		ra_transport_complete(done, done_data, RC_API_SERVER_RESPONSE_CLIENT_ERROR, NULL, 0);
		return;
	}
	lc->done = done;
	lc->data = done_data;
	lc->kind = kind;
	if (!ach_cache_path(kind, request->post_data, lc->file, sizeof lc->file)) lc->file[0] = 0;
	SysPrintf("Achievements: Local: a %s download goes to the server\n", ach_req_name(kind));
	ra_transport_post(request->url, request->post_data, request->content_type, local_done, lc);
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
	/* a progress pop-up only at a new 10 % step, never for the first reading or while settling */
	if (kind == ACH_NOTICE_PROGRESS && (!ach_steps_update(&steps, a->id, (unsigned)a->measured_percent) || settle)) return;
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
		if (a->id >= ACHIEVEMENT_WARNING_ID) {   /* "Warning: Unknown Emulator" (Hardcore): a banner, not an unlock */
			memset(&n, 0, sizeof n);
			n.kind = ACH_NOTICE_WARNING;
			n.title = "Unknown Emulator";   /* RA's own words are cut on the card ("Warning: Unknown Emu...") */
			n.desc = "Hardcore is off; Casual only";
			SysPrintf("Achievements: %s\n", a->title);
			if (achievements_notify) achievements_notify(&n);
			break;
		}
		int before = had_has(a->id);   /* the account had it before this trigger */
		int no_side = demoted || local_mode;   /* no account side: every player earns Local */
		int fresh = !no_side && !before;
		achievement_notice_t ln;
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
		ln = n;   /* the Local players' toast starts from the same card */
		if (fresh || (!no_side && achievementEncore)) {   /* the account's side: new, or shown again by Encore */
			n.beaten = !beaten_seen && is_beaten();
			n.mastered = !mastered_seen && is_mastered();
			beaten_seen |= n.beaten;
			mastered_seen |= n.mastered;
			if (achievements_ra_active()) {   /* Local players play too: say which side this is */
				n.side = "RetroAchievements";
				n.who = achievements_player_name(0);
			}
			SysPrintf(fresh ? "Achievement unlocked (Official, Casual): %u %s\n" : "Achievement earned again (Encore): %u %s\n",
			          (unsigned)a->id, a->title);
			if (achievements_notify) achievements_notify(&n);
		}
		had_add(a->id);
		if (achievements_ra_active()) {   /* the Local players without it get it in their journal */
			ln.side = local_mode ? NULL : "Local";   /* Local mode has no other side to tell it from */
			if (achievements_ra_trigger(a->id, &ln) && achievements_notify) achievements_notify(&ln);
		}
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

static char last_disc_hash[33];   /* the last disc hashed, kept after game_hash is used */

/* The PS1 hash of the loaded disc, into game_hash. 0 when it has no PS1 boot executable. */
static int hash_disc(const char *image)
{
	static rc_hash_cdreader_t reader = { hash_open_track, hash_read_sector, hash_close_track, hash_first_sector, NULL };
	rc_hash_init_custom_cdreader(&reader);
	rc_hash_init_error_message_callback(hash_error);
	game_hash[0] = 0;
	last_disc_hash[0] = 0;
	if (!rc_hash_generate_from_file(game_hash, RC_CONSOLE_PLAYSTATION, image)) {
		game_hash[0] = 0;
		return 0;
	}
	SysPrintf("Achievements: disc hash %s\n", game_hash);   /* public: the set's identity */
	snprintf(last_disc_hash, sizeof last_disc_hash, "%s", game_hash);
	return 1;
}

/* The hash of the disc loaded last, "" when it had none or was not hashed (the Recent record) */
const char *achievements_official_disc_hash(void)
{
	return last_disc_hash;
}

/* ---- which account, loading the set, and the login ---- */

static void start_session(void);

/* The account of this game load: the Mii that plays Official gives its link's file; no such
 * Mii: this Wii's ra_account.ini. ach_account.h turns the link into a path. */
static void choose_account(void)
{
	const char *key = players_official();
	const char *link = *key ? players_account(key) : "";
	snprintf(want_key, sizeof want_key, "%s", key);
	ach_account_path(link, want_path, sizeof want_path);
}

/* Local mode: any account will do, it only downloads. The Official Mii's own if its file is
 * there, else this Wii's ra_account.ini, else the first file in achievements/ra/. When there is
 * none, ra_account.ini, so the failure names it. */
static void local_account(char *out, size_t size)
{
	const char *key = players_official();
	const char *link = *key ? players_account(key) : "";
	char path[ACCOUNT_PATH], best[ACH_ACCOUNT_NAME_MAX + 8] = "";
	struct stat st;
	DIR *d;
	struct dirent *e;
	if (*link && ach_account_path(link, path, sizeof path) != ACH_ACCOUNT_BAD && !stat(path, &st)) {
		snprintf(out, size, "%s", path);
		return;
	}
	if (!stat(ACH_ACCOUNT_WII, &st)) {
		snprintf(out, size, "%s", ACH_ACCOUNT_WII);
		return;
	}
	if ((d = opendir(ACH_ACCOUNT_DIR "ra"))) {
		while ((e = readdir(d))) {
			size_t n = strlen(e->d_name);
			char name[ACH_ACCOUNT_NAME_MAX + 8];
			if (n < 5 || n >= sizeof name || strcasecmp(e->d_name + n - 4, ".ini")) continue;
			snprintf(name, sizeof name, "%s", e->d_name);
			name[n - 4] = 0;
			if (!ach_account_name_ok(name)) continue;
			if (!best[0] || strcmp(name, best) < 0) snprintf(best, sizeof best, "%s", name);
		}
		closedir(d);
	}
	if (best[0] && ach_account_path(best, path, sizeof path) == ACH_ACCOUNT_NAMED)
		snprintf(out, size, "%s", path);
	else
		snprintf(out, size, "%s", ACH_ACCOUNT_WII);
}

/* Local mode, a game load: the card's copy of the set when there is a recent one (no account, no
 * network), else an account for the download */
static void local_pick(void)
{
	char file[ACH_CACHE_PATH];
	struct stat st;
	want_key[0] = 0;
	local_account(want_path, sizeof want_path);
	snprintf(local_hash, sizeof local_hash, "%s", game_hash);
	if (ach_cache_hash_path(ACH_REQ_SETS, game_hash, file, sizeof file) && !stat(file, &st) &&
	    ach_cache_fresh(st.st_mtime, time(NULL))) {
		offline = 1;
		snprintf(want_path, sizeof want_path, "%s", ACH_OFFLINE_LINK);
		SysPrintf("Achievements: Local: the saved set is used, no network\n");
	}
}

static void drop_session(void)
{
	rc_client_logout(client);   /* also unloads a game: none is loaded when the account changes */
	logged_in = 0;
	session_path[0] = 0;
}

/* The account this game load wants did not work (no file, a failed login). A linked Mii: this
 * Wii's account loads the set in spectator mode, so nothing is sent to it, and that Mii earns
 * Local with the others. Else (this Wii's own account, or no such account): no set. Nothing
 * tries a password again by itself. */
static void local_fail(const char *why)
{
	int net = net_failed || !ra_transport_available();
	if (!offline && local_hash[0] && local_cached(local_hash)) {   /* the card's copy, however old */
		offline = 1;
		snprintf(want_path, sizeof want_path, "%s", ACH_OFFLINE_LINK);
		snprintf(game_hash, sizeof game_hash, "%s", local_hash);
		snprintf(state, sizeof state, "Connecting...");
		SysPrintf("Achievements: Local: %s; the saved set is used\n", why);
		start_session();
		return;
	}
	SysPrintf("Achievements: Local: no set: %s%s\n", why, net ? " (no network, none saved)" : "");
	snprintf(state, sizeof state, "%s", net ? "No network, no saved set" : why);
	game_hash[0] = 0;
	if (achievements_notify && (net || strstr(why, "ccount"))) {   /* said plainly, on the screen */
		achievement_notice_t n;
		memset(&n, 0, sizeof n);
		n.kind = ACH_NOTICE_WARNING;
		n.title = net ? "Local: no network, no set" : "Local: no account";
		n.desc = net ? "Go online once to save it" : "Link one in Players";
		achievements_notify(&n);
	}
}

/* A set that did not load: the next frame, outside rc_client's callback */
static void local_pending(void)
{
	if (!local_retry) return;
	local_retry = 0;
	local_fail("No set for this disc");
}

static void account_failed(const char *why)
{
	struct stat st;
	if (local_mode) {
		local_fail(why);
		return;
	}
	SysPrintf("Achievements: the Official account did not work: %s\n", why);
	if (want_key[0] && !demoted && game_hash[0] && ra_transport_available() && want_path[0] &&
	    strcmp(want_path, ACH_ACCOUNT_WII) && !stat(ACH_ACCOUNT_WII, &st)) {
		demoted = 1;
		snprintf(demote_reason, sizeof demote_reason, "%s: earns Local", why);
		snprintf(want_path, sizeof want_path, "%s", ACH_ACCOUNT_WII);
		rc_client_set_spectator_mode_enabled(client, 1);
		local_planned = achievements_ra_plan("");   /* every seated Mii earns Local, that one too */
		SysPrintf("Achievements: this Wii's account loads the set, spectating; the Mii %.8s earns Local\n", want_key);
		start_session();
		return;
	}
	snprintf(state, sizeof state, "%s", why);
	game_hash[0] = 0;
}

/* The account had these when the set loaded */
static void had_build(void)
{
	rc_client_achievement_list_t *l = rc_client_create_achievement_list(client,
		RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED_AND_UNPROMOTED, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	uint32_t b, i;
	had_n = 0;
	if (!l) return;
	for (b = 0; b < l->num_buckets; b++)
		for (i = 0; i < l->buckets[b].num_achievements; i++)
			if (l->buckets[b].achievements[i]->unlocked && l->buckets[b].achievements[i]->id < ACHIEVEMENT_WARNING_ID)
				had_add(l->buckets[b].achievements[i]->id);
	rc_client_destroy_achievement_list(l);
}

/* The set is in: the Local side, if Miis other than the Official one are seated. Their journal
 * is achievements/ra_local/<game id>.unlocks, its digest made from the game id. */
static void local_begin(const rc_client_game_t *g)
{
	char digest[33], journal[96];
	rc_client_achievement_list_t *l;
	uint32_t b, i;
	if (!local_planned || !g || !ach_ra_journal_path(g->id, journal, sizeof journal)) return;
	ach_ra_digest(g->id, digest);
	mkdir(ACH_ACCOUNT_DIR "ra_local", 0777);
	if (!achievements_ra_begin(digest, journal, !demoted && !local_mode, NULL, demoted ? want_key : "", demoted ? demote_reason : ""))
		return;
	l = rc_client_create_achievement_list(client,
		rc_client_get_unpromoted_enabled(client) ? RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED_AND_UNPROMOTED
		                                          : RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED,
		RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	if (l) {
		for (b = 0; b < l->num_buckets; b++)
			for (i = 0; i < l->buckets[b].num_achievements; i++) {
				const rc_client_achievement_t *a = l->buckets[b].achievements[i];
				if (a->id >= ACHIEVEMENT_WARNING_ID) continue;
				achievements_ra_add(a->id, a->title, a->description, a->badge_name, a->points,
				                    (int)(a->rarity * 100 + 0.5f),
				                    a->type <= RC_CLIENT_ACHIEVEMENT_TYPE_WIN ? (unsigned char)a->type : ACH_TYPE_STANDARD,
				                    a->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNPROMOTED);
			}
		rc_client_destroy_achievement_list(l);
	}
	achievements_ra_end();
}

/* The server's own check: an unlock the account already had does not go again (encore mode
 * lets it trigger for the Local players) */
static int can_submit(uint32_t id, rc_client_t *c)
{
	(void)c;
	return !had_has(id);
}

static void game_loaded_cb(int result, const char *error, rc_client_t *c, void *data)
{
	const rc_client_game_t *g;
	rc_client_user_game_summary_t s;
	(void)c; (void)data;
	load_busy = 0;
	if (result != RC_OK) {
		SysPrintf("Achievements: no RetroAchievements set for this disc (%s)\n", error ? error : "unknown");
		snprintf(state, sizeof state, "No set for this disc");
		if (local_mode && !offline) local_retry = 1;   /* the card's copy, if there is one (local_pending) */
		else if (local_mode) local_fail("No set for this disc");
		else if (result == RC_NO_GAME_LOADED && achievements_notify) {
			/* RetroAchievements answered and knows no game by this disc's hash: another release
			 * or a changed image (Gex 3 SLUS-00806 here: a2812183..., while RA has a set for
			 * the game). Said on the screen, or the player waits for achievements that never come. */
			achievement_notice_t n;
			memset(&n, 0, sizeof n);
			n.kind = ACH_NOTICE_WARNING;
			n.title = "No achievements for this disc";
			n.desc = "RetroAchievements does not know this version";
			achievements_notify(&n);
		}
		return;
	}
	game_loaded = 1;
	steps_restart();
	beaten_seen = is_beaten();
	mastered_seen = is_mastered();
	g = rc_client_get_game_info(client);
	rc_client_get_user_game_summary(client, &s);
	SysPrintf("Achievements: %s, %u of %u earned (%s, Casual%s%s)\n", g && g->title ? g->title : "?",
	          (unsigned)s.num_unlocked_achievements, (unsigned)s.num_promoted_achievements,
	          demoted || local_mode ? "Local" : "Official",
	          rc_client_get_encore_mode_enabled(client) ? ", Encore" : "",
	          rc_client_get_unpromoted_enabled(client) ? ", Unofficial" : "");
	list_stale = 1;
	had_build();
	local_begin(g);
	if (local_mode && !achievements_ra_active()) {   /* the journal could not be read: no player earns */
		snprintf(state, sizeof state, "Local journal unreadable");
		return;
	}
	if (achievements_notify) {   /* the game card: the set is on */
		achievement_notice_t n;
		memset(&n, 0, sizeof n);
		n.kind = ACH_NOTICE_GAME;
		n.game = g && g->title ? g->title : "";
		n.badge = g && g->badge_name ? g->badge_name : "";
		n.desc = demoted || local_mode ? "Local" : "Official";
		n.rarity_x100 = -1;
		n.earned = s.num_unlocked_achievements;
		n.count = s.num_promoted_achievements;
		n.points = s.points_unlocked;
		n.total_points = s.points_available;
		if (local_mode)   /* nothing is the account's: the players' own */
			achievements_ra_summary(&n.earned, &n.count, &n.points, &n.total_points);
		achievements_notify(&n);
	}
}

static void load_set(void)
{
	if (!game_hash[0]) return;
	load_busy = 1;
	rc_client_begin_load_game(client, game_hash, game_loaded_cb, NULL);
	game_hash[0] = 0;
}

/* data: the attempt's generation, and 1 when a password (not a token) was used */
static void login_cb(int result, const char *error, rc_client_t *c, void *data)
{
	const rc_client_user_t *u;
	uintptr_t d = (uintptr_t)data;
	(void)c;
	if ((unsigned)(d >> 1) != login_gen) return;   /* an older attempt's answer */
	login_busy = 0;
	if (result != RC_OK) {
		SysPrintf("Achievements: login failed: %s\n",
		          ra_transport_available() ? (error ? error : "unknown") : "this build has no secure connection yet");
		if (game_hash[0] && strcmp(login_path, want_path))   /* the early login's account is not the one wanted: that one is next */
			start_session();
		else
			account_failed(ra_transport_available() ? "Login failed" : "No secure connection");
		return;
	}
	logged_in = 1;
	snprintf(session_path, sizeof session_path, "%s", login_path);
	u = rc_client_get_user_info(client);
	SysPrintf("Achievements: logged in as %s\n", u && u->display_name ? u->display_name : "?");
	if ((d & 1) && u && u->username && u->token)
		store_token(session_path, u->username, u->token);
	if (game_hash[0] && strcmp(session_path, want_path)) {   /* the early login (menu) was another account */
		drop_session();
		start_session();
		return;
	}
	load_set();
}

static void login(const char *path)
{
	char user[64], secret[256];
	int is_token;
	if (!path[0]) {
		account_failed("Account name not valid");
		return;
	}
	if (!strcmp(path, ACH_OFFLINE_LINK)) {   /* Local, from the card: server_call makes the login up; no file, no token stored */
		login_busy = 1;
		snprintf(login_path, sizeof login_path, "%s", path);
		login_gen++;
		rc_client_begin_login_with_token(client, "Local", "offline", login_cb, (void *)(uintptr_t)(login_gen << 1));
		return;
	}
	if (!read_account(path, user, sizeof user, secret, sizeof secret, &is_token)) {
		SysPrintf("Achievements: no account file; put User= and Password= in %s\n", path);
		account_failed(strcmp(path, ACH_ACCOUNT_WII) ? "Account file missing" : "No account (ra_account.ini)");
		return;
	}
	login_busy = 1;
	snprintf(login_path, sizeof login_path, "%s", path);
	login_gen++;
	if (is_token)
		rc_client_begin_login_with_token(client, user, secret, login_cb, (void *)(uintptr_t)(login_gen << 1));
	else
		rc_client_begin_login_with_password(client, user, secret, login_cb, (void *)(uintptr_t)(login_gen << 1 | 1));
	memset(secret, 0, sizeof secret);   /* rc_client keeps its own copy for the request */
}

/* Log in as want_path (or stay logged in as it), then load the set. An answer still on its way
 * (the menu's early login) decides in login_cb. A game load that wants another account than the
 * one logged in logs the old one out first: never in the middle of a game. */
static void start_session(void)
{
	if (login_busy) return;
	if (logged_in && strcmp(session_path, want_path)) drop_session();
	if (logged_in)
		load_set();
	else
		login(want_path);
}

/* ---- the engine's entry points (achievements.c) ---- */

static int client_up(void)
{
	if (client) return 1;
	client = rc_client_create(read_memory, server_call);
	if (!client) {
		SysPrintf("Achievements: out of memory for RetroAchievements\n");
		return 0;
	}
	rc_client_set_event_handler(client, event);
	client->callbacks.can_submit_achievement_unlock = can_submit;   /* rc_client_internal.h: no setter in the pinned API */
	rc_client_set_hardcore_enabled(client, 0);   /* Casual only: Hardcore needs RA's approval */
	rc_client_set_allow_background_memory_reads(client, 0);
	return 1;
}

/* The set is still on its way (the login, the game data, the parse): the game should not
 * start yet, or the 120-180 ms parse lands in its first seconds (bench Wii, 2026-10-24). */
void achievements_official_idle(void);

int achievements_official_loading(void)
{
	return client && (login_busy || load_busy || local_retry);
}

/* Wait for it, polling, up to ms milliseconds: the start of a game (go()). The menu shows a
 * message meanwhile (MainFrame); an autoboot or a chain waits here. */
void achievements_official_wait(unsigned ms)
{
	u64 end = gettime() + millisecs_to_ticks(ms);
	if (!achievements_official_loading()) return;
	SysPrintf("Achievements: waiting for the set before the game starts\n");
	while (achievements_official_loading() && gettime() < end) {
		achievements_official_idle();
		usleep(10000);
	}
	if (achievements_official_loading())
		SysPrintf("Achievements: the set is late; the game starts and it arrives in play\n");
}

int achievements_official_load(const char *image)
{
	if (!client_up()) return 0;
	demoted = 0;
	demote_reason[0] = 0;
	local_mode = achievementsEnabled == ACH_LOCAL;
	offline = net_failed = local_retry = 0;
	local_hash[0] = 0;
	if (local_mode) {   /* everyone earns Local: every seated Mii, or Guest */
		want_key[0] = want_path[0] = 0;
		local_planned = achievements_ra_plan_all();
	} else {
		choose_account();
		local_planned = achievements_ra_plan(want_key);   /* the other seated Miis earn Local */
	}
	/* before the set loads: rc_client reads these when it activates the achievements. Encore
	 * with Local players: the account's earned ones stay active for them. */
	rc_client_set_unpromoted_enabled(client, achievementUnofficial != 0);
	rc_client_set_encore_mode_enabled(client, achievementEncore != 0 || local_planned > 0);
	rc_client_set_spectator_mode_enabled(client, achievementSpectator != 0 || local_mode);   /* scripted runs, and Local: nothing reaches the account */
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
	if (local_mode) local_pick();
	start_session();
	return 0;   /* the set arrives later, asynchronously */
}

void achievements_official_frame(void)
{
	if (!client) return;
	ra_transport_poll();
	local_pending();
	/* every frame, loaded or not: with background memory reads off, rc_client finishes a game
	 * load (it validates addresses through read_memory) in its next do_frame. While a swapped
	 * disc is being checked, only idle: no award against a disc that may not be the game's. */
	if (media_pending)
		rc_client_idle(client);
	else {
		rc_client_do_frame(client);
		if (settle && --settle == 0) steps_seed();
	}
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
		steps_restart();   /* another disc, other memory: a new baseline */
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
	/* the menu is up: log in now, once, so a game load only fetches its set */
	if (!client && !client_up()) return;
	local_pending();
	if (!logged_in && !login_busy && !login_tried && !game_loaded && achievementsEnabled != ACH_LOCAL) {   /* Local logs in only to download a set */
		login_tried = 1;
		choose_account();
		login(want_path);
	}
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
	had_n = 0;
	local_retry = 0;
}

/* The seats or the pads changed: Local mode's players are asked for again, and the set's entries
 * go in again for the new ones (their journals are read; the old players' unlocks are written) */
void achievements_official_players_changed(void)
{
	const rc_client_game_t *g;
	if (!local_mode || !game_loaded || !client || !(g = rc_client_get_game_info(client))) return;
	if (!achievements_ra_replan("", 1)) return;
	SysPrintf("Achievements: the players changed, the Local side loads again\n");
	local_begin(g);
}

#endif /* HW_RVL */
