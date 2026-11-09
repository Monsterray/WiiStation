/* The Recent tab's record (recent_list.h). Plain C: the host test builds it alone. */
#include "recent_list.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define RL_STRCASECMP _stricmp
#else
#include <strings.h>
#define RL_STRCASECMP strcasecmp
#endif

void recent_clear(recent_list *l)
{
	memset(l, 0, sizeof *l);
}

static void copy(char *out, size_t size, const char *s)
{
	size_t n = strlen(s);
	if (n >= size)
		n = size - 1;
	memcpy(out, s, n);
	out[n] = 0;
}

int recent_find(const recent_list *l, const char *path)
{
	int i;
	for (i = 0; i < l->n; i++)
		if (!RL_STRCASECMP(l->e[i].path, path))
			return i;
	return -1;
}

int recent_add(recent_list *l, const recent_entry *e)
{
	int at;
	if (!e->path[0])
		return -1;
	at = recent_find(l, e->path);
	if (at < 0)
		at = l->n < RECENT_MAX ? l->n++ : RECENT_MAX - 1;	/* a full list drops its oldest */
	memmove(&l->e[1], &l->e[0], (size_t)at * sizeof l->e[0]);
	l->e[0] = *e;
	return 0;
}

/* ---- the file ---- */

static const char *setName(int set)
{
	return set == RECENT_SET_FOUND ? "found" : set == RECENT_SET_NONE ? "none" : "unchecked";
}

int recent_save(const recent_list *l, const char *file)
{
	char tmp[RECENT_PATH + 8];
	FILE *f;
	int i;

	snprintf(tmp, sizeof tmp, "%s.tmp", file);
	f = fopen(tmp, "wb");
	if (!f)
		return -1;
	fprintf(f, "; WiiStation: the last %d games loaded, newest first. The explorer's Recent tab writes this file.\n"
	           "; Time is seconds since 1970. Set: found, none or unchecked (RetroAchievements).\n", RECENT_MAX);
	for (i = 0; i < l->n; i++) {
		const recent_entry *e = &l->e[i];
		fprintf(f, "\n[Game]\nPath = \"%s\"\nSource = %s\nDisc = %d\nTime = %lld\nSerial = %s\nHash = %s\nSet = %s\n",
		        e->path, e->src, e->disc, e->time, e->serial, e->hash, setName(e->set));
	}
	if (fclose(f) != 0) {
		remove(tmp);
		return -1;
	}
	remove(file);
	if (rename(tmp, file) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

/* A line, at most size - 1 characters; the rest of a longer line is dropped. 0 at the end of the file. */
static int getLine(char *line, int size, FILE *f)
{
	int n = 0, c;
	while ((c = fgetc(f)) != EOF) {
		if (c == '\n')
			break;
		if (n < size - 1)
			line[n++] = (char)c;
	}
	line[n] = 0;
	while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
		line[--n] = 0;
	return c != EOF || n > 0;
}

/* True when s holds only printable characters (a damaged file may hold anything). */
static int clean(const char *s)
{
	for (; *s; s++)
		if ((unsigned char)*s < 32 || *s == 127)
			return 0;
	return 1;
}

static void setKey(recent_entry *e, const char *key, char *v)
{
	size_t n;
	if (*v == '"') {		/* a quoted value */
		v++;
		n = strlen(v);
		if (n && v[n - 1] == '"')
			v[n - 1] = 0;
	}
	if (!clean(v))
		return;
	if (!strcmp(key, "Path"))			copy(e->path, sizeof e->path, v);
	else if (!strcmp(key, "Source"))	copy(e->src, sizeof e->src, v);
	else if (!strcmp(key, "Disc"))		e->disc = atoi(v) > 0 && atoi(v) < 100 ? atoi(v) : 0;
	else if (!strcmp(key, "Time"))		e->time = atoll(v) > 0 ? atoll(v) : 0;
	else if (!strcmp(key, "Serial"))	copy(e->serial, sizeof e->serial, v);
	else if (!strcmp(key, "Hash")) {
		n = strlen(v);
		if (n == 32 || n == 0)
			copy(e->hash, sizeof e->hash, v);
	}
	else if (!strcmp(key, "Set"))		e->set = !strcmp(v, "found") ? RECENT_SET_FOUND :
	                                              !strcmp(v, "none") ? RECENT_SET_NONE : RECENT_SET_UNKNOWN;
}

int recent_load(recent_list *l, const char *file)
{
	FILE *f = fopen(file, "rb");
	recent_entry cur;
	char line[RECENT_PATH + 64];
	int open = 0;

	recent_clear(l);
	if (!f)
		return 0;
	memset(&cur, 0, sizeof cur);
	while (getLine(line, sizeof line, f)) {
		char *p = line + strspn(line, " \t");
		char *eq;
		if (*p == ';' || *p == '#' || !*p)
			continue;
		if (*p == '[') {
			if (open && cur.path[0] && l->n < RECENT_MAX)	/* the entry before is whole: keep it */
				l->e[l->n++] = cur;
			memset(&cur, 0, sizeof cur);
			open = !strncmp(p, "[Game]", 6);
			continue;
		}
		if (!open || !(eq = strchr(p, '=')))
			continue;
		*eq = 0;
		{
			char *k = p, *v = eq + 1;
			size_t kn = strlen(k);
			while (kn && (k[kn - 1] == ' ' || k[kn - 1] == '\t'))
				k[--kn] = 0;
			v += strspn(v, " \t");
			setKey(&cur, k, v);
		}
	}
	if (open && cur.path[0] && l->n < RECENT_MAX)
		l->e[l->n++] = cur;
	fclose(f);
	return l->n;
}

/* ---- what the list shows ---- */

/* Year, month, day of a day count since 1970-01-01 (Howard Hinnant's civil_from_days). */
static void civil(long long z, int *y, int *m, int *d)
{
	long long era, doe, yoe, doy, mp;
	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yoe + era * 400 + (*m <= 2));
}

void recent_when(long long t, long long now, char *out, unsigned size)
{
	long long dt, dn;
	int y, m, d;
	if (t <= 0 || t > now + 86400) {	/* not known, or a clock set back: a day of slack */
		snprintf(out, size, "-");
		return;
	}
	dt = t / 86400;
	dn = now / 86400;
	if (dt >= dn)
		snprintf(out, size, "today");
	else if (dt == dn - 1)
		snprintf(out, size, "yesterday");
	else {
		civil(dt, &y, &m, &d);
		snprintf(out, size, "%04d-%02d-%02d", y, m, d);
	}
}

void recent_src_of(const char *path, char *out, unsigned size)
{
	const char *c = strchr(path, ':');
	size_t n = c ? (size_t)(c - path) : 0;
	if (!c || n == 0 || n >= size || n >= 8) {
		out[0] = 0;
		return;
	}
	memcpy(out, path, n);
	out[n] = 0;
}

/* What the achievements module says about the disc just loaded, as a RECENT_SET_*: a set is
 * FOUND when RetroAchievements is on, the disc has a hash and the set has achievements; NONE when
 * all of that holds but the finished load gave none; anything else is not known. */
int recent_set_result(int ra_on, const char *hash, unsigned count, int loading)
{
	if (!ra_on || !hash || !hash[0])
		return RECENT_SET_UNKNOWN;
	if (count > 0)
		return RECENT_SET_FOUND;
	return loading ? RECENT_SET_UNKNOWN : RECENT_SET_NONE;
}

/* A new entry whose set is not known keeps the hash and set of the older entry for the same path. */
void recent_keep_known(recent_entry *e, const recent_list *l)
{
	int i;
	if (e->set != RECENT_SET_UNKNOWN)
		return;
	i = recent_find(l, e->path);
	if (i < 0 || l->e[i].set == RECENT_SET_UNKNOWN)
		return;
	e->set = l->e[i].set;
	memcpy(e->hash, l->e[i].hash, sizeof e->hash);
}

/* A save state's file: "<dev><statespath><serial>.st<slot>" ("sd:" "/wiistation/savestates/" ...);
 * 0 when it fits, -1 when it does not or the serial is empty. */
int recent_state_path(char *out, unsigned size, const char *dev, const char *statespath,
                      const char *serial, int slot)
{
	int n;
	if (!serial || !serial[0] || slot < 0 || slot > 9) {
		if (size) out[0] = 0;
		return -1;
	}
	n = snprintf(out, size, "%s%s%s.st%d", dev, statespath, serial, slot);
	return n > 0 && (unsigned)n < size ? 0 : -1;
}

/* ---- The letter filter (slice 6): same cells as the explorer's letter strip (eng::stripLetter) ---- */

int recent_cell(const char *path)
{
	const char *name = path, *p;
	int ch;
	if (!path)
		return -1;
	for (p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			name = p + 1;
	if (!name[0])
		return -1;
	ch = (unsigned char)name[0];
	if (ch >= 'a' && ch <= 'z') return ch - 'a' + 1;
	if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 1;
	return 0;
}

unsigned recent_cells(const recent_list *l)
{
	unsigned m = 0;
	int i;
	for (i = 0; i < l->n; i++) {
		const int k = recent_cell(l->e[i].path);
		if (k >= 0)
			m |= 1u << k;
	}
	return m;
}

int recent_filter_list(const recent_list *l, int cell, int *idx, int max)
{
	int i, n = 0;
	for (i = 0; i < l->n && n < max; i++)
		if (cell < 0 || recent_cell(l->e[i].path) == cell)
			idx[n++] = i;
	return n;
}

int recent_filter_press(const recent_list *l, int cur, int cell)
{
	if (cell < 0 || cell > 26)
		return cur;
	if (cell == cur)
		return -1;						/* the same letter again clears */
	if (!(recent_cells(l) & (1u << cell)))
		return cur;						/* no recent game starts with it: nothing happens */
	return cell;
}
