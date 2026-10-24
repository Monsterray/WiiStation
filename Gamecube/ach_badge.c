/* RetroAchievements badge pictures as GX textures (ach_badge.h).
 *
 * The way a picture is had, in order: the memory cache (CACHE_N textures) -> the file on the
 * card (sd:/wiistation/achievements/badges/NAME[_lock].png) -> a download through the HTTPS
 * transport (ra_transport.c), written to the card through a temp file and a rename, then
 * decoded. At most MAX_FLIGHT downloads are on their way; the rest wait in a bounded queue.
 * A name that failed is remembered until ach_badge_flush(), so it is not tried every frame.
 *
 * Everything runs on the emulator thread: ach_badge_get() from the overlay, and the download
 * callback from ra_transport_poll(). No thread of its own, so no lock.
 *
 * Memory: one pool of CACHE_N x 8 KB (64 x 64 RGB5A3), allocated on the first use and kept
 * for the run (a pool that is freed and allocated again fragments the heap). The picture is
 * decoded with libpng's simplified API into a temporary RGBA buffer (at most MAX_DIM squared,
 * freed again) and converted by ach_badge_conv.h.
 *
 * The rule for taking a texture away: the GP reads a texture while the overlay is drawn, and
 * the CPU does not wait for it. A cache slot is therefore reused only if its texture was not
 * returned for EVICT_MS (many frames): the overlay draws what it was given in the same frame,
 * so a texture drawn this frame or the last is never overwritten. If every slot was returned
 * that recently, nothing is evicted and the new picture waits (get) or is read from its card
 * file later (download). ach_badge_flush() only marks the slots free; the pool stays. */
#ifdef HW_RVL
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ogc/lwp_watchdog.h>
#include <png.h>
#include "ach_badge.h"
#include "ach_badge_conv.h"
#include "ra_transport.h"

extern void SysPrintf(const char *fmt, ...);

#define BADGE_URL "https://media.retroachievements.org/Badge/"
#define BADGE_DIR "sd:/wiistation/achievements/badges/"
#define CACHE_N 32          /* textures in memory: 256 KB */
#define QUEUE_N 64          /* names waiting for a download slot */
#define FAILED_N 128        /* failed names kept (a ring: the oldest is forgotten when full) */
#define MAX_FLIGHT 2        /* downloads on their way at once */
#define MAX_PNG (64 * 1024) /* a bigger file is not a badge */
#define MAX_DIM 256         /* a bigger picture is not decoded */
#define EVICT_MS 200        /* a texture returned within this time is never reused */
#define LOAD_GAP_MS 8       /* at most one card read and decode per this time (about a frame) */

typedef struct { char name[ACH_BADGE_NAME_MAX + 1]; u8 locked; } Key;

typedef struct {
	Key key;
	u8 used;
	u8 touched;             /* returned_ms means something */
	u32 returned_ms;        /* the last time it was handed out (or made) */
	GXTexObj obj;
} Entry;

typedef struct { Key key; u32 gen; u8 busy; } Flight;

static Entry cache[CACHE_N];
static u8 *pool;                       /* CACHE_N textures, 32-byte aligned */
static Key failed[FAILED_N];
static unsigned failed_next, failed_n;
static Key queue[QUEUE_N];
static unsigned queue_head, queue_n;
static Flight flight[MAX_FLIGHT];
static u32 gen;                        /* ach_badge_flush() counts it up: older downloads are dropped */
static u32 last_load_ms;
static int loaded_once;

static u32 now_ms(void) { return (u32)ticks_to_millisecs(SYS_Time()); }

static int key_eq(const Key *a, const Key *b) { return a->locked == b->locked && !strcmp(a->name, b->name); }

static void key_path(const Key *k, const char *ext, char *out, size_t n)
{
	snprintf(out, n, BADGE_DIR "%s%s.png%s", k->name, k->locked ? "_lock" : "", ext);
}

static int is_failed(const Key *k)
{
	unsigned i;
	for (i = 0; i < failed_n; i++)
		if (key_eq(&failed[i], k)) return 1;
	return 0;
}

static void mark_failed(const Key *k, const char *why)
{
	if (is_failed(k)) return;
	SysPrintf("Achievements: badge %s%s: %s\n", k->name, k->locked ? "_lock" : "", why);
	failed[failed_next] = *k;
	failed_next = (failed_next + 1) % FAILED_N;
	if (failed_n < FAILED_N) failed_n++;
}

static int queued(const Key *k)
{
	unsigned i;
	for (i = 0; i < queue_n; i++)
		if (key_eq(&queue[(queue_head + i) % QUEUE_N], k)) return 1;
	return 0;
}

static int in_flight(const Key *k)
{
	unsigned i;
	for (i = 0; i < MAX_FLIGHT; i++)
		if (flight[i].busy && flight[i].gen == gen && key_eq(&flight[i].key, k)) return 1;
	return 0;
}

static Entry *find(const Key *k)
{
	unsigned i;
	for (i = 0; i < CACHE_N; i++)
		if (cache[i].used && key_eq(&cache[i].key, k)) return &cache[i];
	return NULL;
}

/* A slot for a new texture: a free one, else the one returned longest ago. Any slot that was
 * handed out less than EVICT_MS ago is off limits (the GP may still read it), freed by a
 * flush or not. NULL: none can be taken now. */
static Entry *take_slot(void)
{
	unsigned i, best = CACHE_N;
	u32 now = now_ms(), best_age = 0;
	if (!pool) {
		pool = memalign(32, (size_t)CACHE_N * ACH_BADGE_BYTES);
		if (!pool) return NULL;
	}
	for (i = 0; i < CACHE_N; i++) {
		u32 age = now - cache[i].returned_ms;
		if (cache[i].touched && age < EVICT_MS) continue;
		if (!cache[i].used) return &cache[i];
		if (best == CACHE_N || age > best_age) { best = i; best_age = age; }
	}
	return best == CACHE_N ? NULL : &cache[best];
}

static int png_ok(const u8 *d, size_t len)
{
	static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	return len >= sizeof sig && len <= MAX_PNG && !memcmp(d, sig, sizeof sig);
}

/* PNG bytes -> a cache entry. 0: not a decodable badge. 1 with *made NULL: decodable, but no
 * slot can be taken at this moment. */
static int decode_into(const Key *k, const u8 *png_data, size_t len, Entry **made)
{
	png_image im;
	u8 *rgba;
	Entry *e;
	u8 *tex;
	*made = NULL;
	memset(&im, 0, sizeof im);
	im.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&im, png_data, len)) { png_image_free(&im); return 0; }
	if (im.width == 0 || im.height == 0 || im.width > MAX_DIM || im.height > MAX_DIM) {
		png_image_free(&im);
		return 0;
	}
	im.format = PNG_FORMAT_RGBA;
	rgba = malloc(PNG_IMAGE_SIZE(im));
	if (!rgba) { png_image_free(&im); return 0; }
	if (!png_image_finish_read(&im, NULL, rgba, 0, NULL)) {
		png_image_free(&im);
		free(rgba);
		return 0;
	}
	png_image_free(&im);
	e = take_slot();
	if (!e) { free(rgba); return 1; }
	tex = pool + (size_t)(e - cache) * ACH_BADGE_BYTES;
	ach_badge_rgba_to_rgb5a3(rgba, im.width, im.height, (uint16_t *)tex);
	free(rgba);
	DCFlushRange(tex, ACH_BADGE_BYTES);
	GX_InitTexObj(&e->obj, tex, ACH_BADGE_DIM, ACH_BADGE_DIM, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(&e->obj, GX_NEAR, GX_NEAR);
	e->key = *k;
	e->used = 1;
	e->touched = 1;
	e->returned_ms = now_ms();
	*made = e;
	return 1;
}

/* the file on the card; NULL if there is none (or it is not the size of a badge) */
static u8 *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	u8 *buf = NULL;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n <= MAX_PNG && fseek(f, 0, SEEK_SET) == 0 &&
	    (buf = malloc((size_t)n))) {
		if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
			*len = (size_t)n;
		} else {
			free(buf);
			buf = NULL;
		}
	}
	fclose(f);
	return buf;
}

/* the temp file + rename way: a power cut leaves no half file under the real name */
static void write_file(const Key *k, const u8 *d, size_t len)
{
	static int dirs;
	char path[160], tmp[168];
	FILE *f;
	if (!dirs) {
		mkdir("sd:/wiistation", 0777);
		mkdir("sd:/wiistation/achievements", 0777);
		mkdir("sd:/wiistation/achievements/badges", 0777);
		dirs = 1;
	}
	key_path(k, "", path, sizeof path);
	key_path(k, ".tmp", tmp, sizeof tmp);
	f = fopen(tmp, "wb");
	if (!f) { SysPrintf("Achievements: badge cache: cannot write %s\n", tmp); return; }
	if (fwrite(d, 1, len, f) != len) {
		fclose(f);
		remove(tmp);
		SysPrintf("Achievements: badge cache: short write %s\n", tmp);
		return;
	}
	fclose(f);
	remove(path);   /* FAT: rename does not replace */
	if (rename(tmp, path) != 0) {
		remove(tmp);
		SysPrintf("Achievements: badge cache: cannot rename %s\n", tmp);
	}
}

static void on_done(const rc_api_server_response_t *resp, void *data);

static void start_download(const Key *k)
{
	char url[96];
	Flight *fl = NULL;
	unsigned i;
	for (i = 0; i < MAX_FLIGHT; i++)
		if (!flight[i].busy) { fl = &flight[i]; break; }
	if (!fl) return;
	fl->key = *k;
	fl->gen = gen;
	fl->busy = 1;   /* before the post: a transport that is full answers at once, inside it */
	snprintf(url, sizeof url, BADGE_URL "%s%s.png", k->name, k->locked ? "_lock" : "");
	ra_transport_post(url, NULL, NULL, on_done, fl);
}

/* the free flights take the queue's names */
static void pump(void)
{
	unsigned i;
	for (;;) {
		Key k;
		for (i = 0; i < MAX_FLIGHT && flight[i].busy; i++) ;
		if (i == MAX_FLIGHT || !queue_n) return;
		k = queue[queue_head];
		queue_head = (queue_head + 1) % QUEUE_N;
		queue_n--;
		start_download(&k);
	}
}

static void on_done(const rc_api_server_response_t *resp, void *data)
{
	Flight *fl = data;
	Key k = fl->key;
	int current = fl->gen == gen;   /* else a new game began meanwhile: the picture is dropped */
	fl->busy = 0;
	if (current) {
		const u8 *body = (const u8 *)resp->body;
		size_t len = resp->body_length;
		if (resp->http_status_code != 200 || !body) {
			mark_failed(&k, "the download failed");
		} else if (!png_ok(body, len)) {
			mark_failed(&k, "not a PNG, or over 64 KB");
		} else {
			Entry *made;
			write_file(&k, body, len);
			if (!decode_into(&k, body, len, &made)) {
				char path[160];
				key_path(&k, "", path, sizeof path);
				remove(path);
				mark_failed(&k, "cannot decode the PNG");
			}
			/* decodable but no slot free: its file is on the card, the next get() reads it */
		}
	}
	pump();
}

int ach_badge_get(const char *badge_name, int locked, GXTexObj *out)
{
	Key k;
	Entry *e;
	u8 *file;
	size_t len = 0;
	char path[160];
	if (!out || !ach_badge_name_ok(badge_name)) return -1;
	memset(&k, 0, sizeof k);
	strcpy(k.name, badge_name);
	k.locked = locked ? 1 : 0;

	if ((e = find(&k))) {
		e->returned_ms = now_ms();
		*out = e->obj;
		return 1;
	}
	if (is_failed(&k)) return -1;
	pump();
	if (in_flight(&k) || queued(&k)) return 0;

	/* the card: one read and decode per frame at most */
	if (loaded_once && now_ms() - last_load_ms < LOAD_GAP_MS) return 0;
	loaded_once = 1;
	last_load_ms = now_ms();
	key_path(&k, "", path, sizeof path);
	file = read_file(path, &len);
	if (file) {
		int ok = png_ok(file, len);
		Entry *made = NULL;
		if (ok) ok = decode_into(&k, file, len, &made);
		free(file);
		if (made) {
			*out = made->obj;
			return 1;
		}
		if (ok) return 0;   /* no slot can be taken yet: the next frame */
		remove(path);       /* a bad file: gone, and the picture is downloaded again */
	}

	if (!ra_transport_available()) {
		mark_failed(&k, "no HTTPS stack, and not on the card");
		return -1;
	}
	if (queue_n >= QUEUE_N) return 0;   /* full: asked again next frame */
	queue[(queue_head + queue_n++) % QUEUE_N] = k;
	pump();
	return 0;
}

void ach_badge_flush(void)
{
	unsigned i;
	for (i = 0; i < CACHE_N; i++) cache[i].used = 0;   /* the pool stays: see the top */
	failed_n = failed_next = 0;
	queue_n = queue_head = 0;
	gen++;
}
#endif /* HW_RVL */
