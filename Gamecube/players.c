/* players.c -- the Miis on this Wii as players (players.h, Docs/MII_PLAN.md).
 *
 * A thread reads the Mii database (mii_db_load, about 0.7 s on the bench Wii) and makes
 * every coin plain at once; then, with the face models (sd:/mii/RFL_Res.dat), it builds one
 * Mii's model at a time. The menu's thread draws that model's head into the EFB's corner
 * between frames (mii_icon) and lays it on the coin. Nothing here writes the database. */
#include <gccore.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "players.h"
#include "players_recent.h"
#include "../deps/mii/mii.h"
#include "../deps/mii/miidraw.h"
#include "../mem2_manager.h"

typedef struct {
	player_mii p;
	mii m;
	int tried;                   /* its model was built, or failed to */
} entry;

static entry *miis;              /* PLAYERS_MAX, MEM2, in the database's order */
static int order[PLAYERS_MAX];   /* the menu's order: index i is miis[order[i]] (players_sort) */
static void sort_miis(int n);
static volatile int count = -1;
static int started;
static volatile int quit;
static lwp_t thread = LWP_THREAD_NULL;

/* The model the thread built for miis[ready]; -1 none, -2 drawn (the thread frees it). */
static mii_model built;
static volatile int ready = -1;
static volatile int faces;      /* 0 not known yet, 1 the face models loaded, else the MII_E_* code */

/* The favorite colors as the Mii Channel shows them (MII_RED ...). */
static const u8 fav_rgb[12][3] = {
	{ 0xd2, 0x1e, 0x14 }, { 0xff, 0x6e, 0x19 }, { 0xff, 0xd8, 0x20 }, { 0x78, 0xd2, 0x20 },
	{ 0x00, 0x78, 0x30 }, { 0x0a, 0x48, 0xb4 }, { 0x3c, 0xaa, 0xde }, { 0xf5, 0x5a, 0x7d },
	{ 0x73, 0x28, 0xad }, { 0x48, 0x38, 0x18 }, { 0xe0, 0xe0, 0xe0 }, { 0x18, 0x18, 0x14 }
};
static const u8 gold[3] = { 0xe0, 0xb0, 0x30 };

/* Texel (x, y) of a w-wide GX RGBA8 texture: 4x4 tiles of 64 bytes, A and R in the first
 * 32, G and B in the next: p[0] A, p[1] R, p[32] G, p[33] B. */
static u8 *texel(u8 *t, int w, int x, int y)
{
	return t + ((y / 4) * (w / 4) + x / 4) * 64 + ((y % 4) * 4 + x % 4) * 2;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* The coin, in place: the head mii_icon left in t (alpha 0 where there is none; all of it
 * for a plain coin) over a disc in the favorite color, lighter at the top, inside a rim
 * (pale, or gold for a favorite); clear outside the circle. */
static void make_coin(u8 *t, int fav, int favorite)
{
	const int N = PLAYERS_COIN;
	const u8 *c = fav_rgb[fav < 12 ? fav : 10];
	float mid = (N - 1) / 2.0f, r = N / 2.0f - 0.5f;
	int x, y, k;

	for (y = 0; y < N; ++y)
		for (x = 0; x < N; ++x) {
			u8 *p = texel(t, N, x, y);
			float d = sqrtf((x - mid) * (x - mid) + (y - mid) * (y - mid));
			float head = p[0] / 255.0f * clamp01(r - 5.5f - d);
			float light = 0.4f - 0.3f * y / N;
			float rim = clamp01(d - (r - 6)) * clamp01(r - 1 - d);
			float edge = clamp01(d - (r - 1.5f));
			u8 src[3] = { p[1], p[32], p[33] };

			for (k = 0; k < 3; ++k) {
				float bg = c[k] + (255 - c[k]) * light;
				float ring = favorite ? gold[k] * (1.15f - 0.3f * y / N) : bg + (255 - bg) * 0.75f;
				bg = bg + ((ring > 255 ? 255 : ring) - bg) * rim;
				bg *= 1 - 0.45f * edge;
				src[k] = (u8)(src[k] * head + bg * (1 - head) + 0.5f);
			}
			p[0] = (u8)(255 * clamp01(r - d + 0.5f) + 0.5f);
			p[1] = src[0];
			p[32] = src[1];
			p[33] = src[2];
		}
}

static void id_hex(const uint8_t id[MII_ID_SIZE], char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 0; i < MII_ID_SIZE; i++) {
		out[i * 2] = hex[id[i] >> 4];
		out[i * 2 + 1] = hex[id[i] & 15];
	}
	out[MII_ID_SIZE * 2] = 0;
}

static void *reader(void *arg)
{
	static mii_db db;
	mii_res res;
	mii_cache *cache;
	int i, n = 0;

	(void)arg;
	memset(&db, 0, sizeof db);
	if (mii_db_load(&db) == MII_OK) {
		for (i = 0; i < MII_DB_SLOTS && n < PLAYERS_MAX; i++) {
			entry *e = &miis[n];
			if (mii_db_get(&db, i, &e->m) != MII_OK)
				continue;
			id_hex(e->m.id, e->p.key);
			mii_get_name(e->m.name, e->p.name, sizeof e->p.name);
			e->p.fav_color = e->m.fav_color;
			e->p.favorite = e->m.favorite;
			e->p.coin = _mem2_memalign(32, PLAYERS_COIN * PLAYERS_COIN * 4);
			if (e->p.coin) {
				memset(e->p.coin, 0, PLAYERS_COIN * PLAYERS_COIN * 4);
				make_coin(e->p.coin, e->p.fav_color, e->p.favorite);
				DCFlushRange(e->p.coin, PLAYERS_COIN * PLAYERS_COIN * 4);
				e->p.coin_serial++;
			}
			n++;
		}
		mii_db_free(&db);
	}
	sort_miis(n);
	count = n;

	/* The heads: only with the user's face models. */
	if (!n || quit)
		return NULL;
	if ((faces = mii_res_load(&res, NULL)) != MII_OK)
		return NULL;
	faces = 1;
	cache = mii_cache_new();
	for (i = 0; i < n && !quit; ) {
		if (ready == -1) {
			mii_model_opts o = { 64, MII_EXPR_BIT(MII_EXPR_NORMAL), cache, 0, 0 };   /* the face texture: 32, 64, 128 or 256 */
			miis[i].tried = 1;
			if (miis[i].p.coin && mii_model_build(&res, &miis[i].m, &o, &built) == MII_OK)
				ready = i;
			i++;
		} else if (ready == -2) {
			mii_model_free(&built);
			ready = -1;
		} else
			usleep(5000);
	}
	while (ready >= 0 && !quit)  /* the last one, still to draw */
		usleep(5000);
	if (ready == -2 || ready >= 0)
		mii_model_free(&built);
	ready = -1;
	if (cache)
		mii_cache_free(cache);
	mii_res_free(&res);
	return NULL;
}

/* The Miis last seated first (players.ini PlayersRecent), the rest in the database's order. */
static void sort_miis(int n)
{
	static int rank[PLAYERS_MAX];
	int i, j;
	for (i = 0; i < n; i++)
		rank[i] = players_recent_rank(playersRecent, miis[i].p.key);
	for (i = 0; i < n; i++) {
		for (j = i; j > 0 && rank[order[j - 1]] > rank[i]; j--)
			order[j] = order[j - 1];
		order[j] = i;
	}
}

void players_sort(void)
{
	if (count > 0)
		sort_miis(count);
}

void players_used(void)
{
	players_recent_merge(playersRecent, sizeof playersRecent, (const char (*)[256])playerSeat, PLAYERS_SEATS);
}

void players_start(void)
{
	if (started)
		return;
	started = 1;
	miis = _mem2_memalign(32, sizeof(entry) * PLAYERS_MAX);
	if (!miis) {
		count = 0;
		return;
	}
	memset(miis, 0, sizeof(entry) * PLAYERS_MAX);
	if (LWP_CreateThread(&thread, reader, NULL, NULL, 32 * 1024, 65) < 0)
		count = 0;
}

int players_count(void) { return count; }
int players_faces(void) { return faces; }

const player_mii *players_get(int i)
{
	return i >= 0 && i < count ? &miis[order[i]].p : NULL;
}

int players_find(const char *key)
{
	int i;
	if (!key || !*key)
		return -1;
	for (i = 0; i < count; i++)
		if (!strcmp(miis[order[i]].p.key, key))
			return i;
	return -1;
}

int players_between_frames(void)
{
	int i = ready;
	u8 *t;

	if (i < 0)
		return 0;
	t = miis[i].p.coin;
	DCInvalidateRange(t, PLAYERS_COIN * PLAYERS_COIN * 4);
	if (mii_icon(&built, MII_EXPR_NORMAL, PLAYERS_COIN, (GXColor){ 0, 0, 0, 0 }, t) == MII_OK) {
		GX_DrawDone();   /* the CPU lays the head on its coin: the copy first */
		DCInvalidateRange(t, PLAYERS_COIN * PLAYERS_COIN * 4);
		make_coin(t, miis[i].p.fav_color, miis[i].p.favorite);
		DCFlushRange(t, PLAYERS_COIN * PLAYERS_COIN * 4);
		miis[i].p.face = 1;
		miis[i].p.coin_serial++;
	}
	ready = -2;
	return 1;
}

char playerSeat[PLAYERS_SEATS][256];
char playersRecent[256];
const char *const PLAYERS_SEAT_NAMES[PLAYERS_SEATS] = { "1", "2", "1A", "1B", "1C", "1D", "2A", "2B", "2C", "2D",
	"1 P1", "1 P2", "1 P3", "1 P4", "1 P5", "1 P6", "1 P7", "1 P8",
	"2 P1", "2 P2", "2 P3", "2 P4", "2 P5", "2 P6", "2 P7", "2 P8" };

void players_seat_mii(int seat, const char *key)
{
	int i;
	if (seat < 0 || seat >= PLAYERS_SEATS)
		return;
	if (key && *key)
		for (i = 0; i < PLAYERS_SEATS; i++)
			if (!strcmp(playerSeat[i], key))
				playerSeat[i][0] = 0;
	snprintf(playerSeat[seat], sizeof playerSeat[seat], "%s", key ? key : "");
}

void players_idle(void)
{
	if (thread == LWP_THREAD_NULL)
		return;
	quit = 1;
	if (ready >= 0)
		ready = -2;      /* not drawn: the thread frees it */
	LWP_JoinThread(thread, NULL);
	thread = LWP_THREAD_NULL;
}

/* A seated Mii's name for the achievements (ach_players.c), at a game's start. The menu's
 * reader has the Miis when the Players menu was opened; if it is still reading, wait for it
 * (it takes about 0.7 s); without it, read the database here once and keep the names. */
#define LOOKUP_WAIT_MS 3000
static struct { char key[PLAYERS_KEY]; char name[32]; } looked[PLAYERS_MAX];
static int looked_n = -1;

int players_lookup(const char *key, char *name, unsigned size)
{
	int i, waited;

	if (!key || !*key || !name || !size)
		return -1;
	name[0] = 0;
	for (waited = 0; started && count < 0 && waited < LOOKUP_WAIT_MS; waited += 10)
		usleep(10000);
	if (count >= 0) {
		i = players_find(key);
		if (i < 0)
			return 0;
		snprintf(name, size, "%s", miis[order[i]].p.name);
		return 1;
	}
	if (started)
		return -1;               /* the reader is stuck: the Mii may be there, its name is not known */
	if (looked_n < 0) {
		static mii_db db;
		looked_n = 0;
		memset(&db, 0, sizeof db);
		if (mii_db_load(&db) == MII_OK) {
			for (i = 0; i < MII_DB_SLOTS && looked_n < PLAYERS_MAX; i++) {
				mii m;
				if (mii_db_get(&db, i, &m) != MII_OK)
					continue;
				id_hex(m.id, looked[looked_n].key);
				mii_get_name(m.name, looked[looked_n].name, sizeof looked[looked_n].name);
				looked_n++;
			}
			mii_db_free(&db);
		} else
			looked_n = -2;       /* no database: no Mii is there, and no second try */
	}
	for (i = 0; i < looked_n; i++)
		if (!strcmp(looked[i].key, key)) {
			snprintf(name, size, "%s", looked[i].name);
			return 1;
		}
	return 0;
}
