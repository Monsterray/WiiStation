/* cover_index.h */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "cover_index.h"

#define LINE_MAX_LEN 256

/* serial in the form the index uses: upper case, letters and digits only, at most 11 characters */
static int norm(const char *in, char *out)
{
	int n = 0;
	for (; *in && *in != '\t' && *in != '\r' && *in != '\n'; in++) {
		if (!isalnum((unsigned char)*in)) continue;
		if (n >= 11) return -1;
		out[n++] = (char)toupper((unsigned char)*in);
	}
	out[n] = 0;
	return n ? n : -1;
}

static void field(const char **p, char *out, int size)
{
	int n = 0;
	const char *s = *p;
	while (*s && *s != '\t' && *s != '\r' && *s != '\n') {
		if (n < size - 1) out[n++] = *s;
		s++;
	}
	out[n] = 0;
	if (*s == '\t') s++;
	*p = s;
	if (n == 1 && out[0] == '-') out[0] = 0;
}

/* Reads the line that starts at or after pos (pos 0: the first line). Returns its start in *start
 * and its end (the next line's start) in *next; 0 when there is no line. */
static int line_at(FILE *f, long pos, long size, char *line, long *start, long *next)
{
	int c;
	long p = pos;
	if (pos > 0) {	/* in the middle of a line: skip to the next one */
		if (fseek(f, pos - 1, SEEK_SET)) return 0;
		c = fgetc(f);
		p = pos - 1 + 1;
		while (c != '\n' && c != EOF) {
			c = fgetc(f);
			p++;
		}
		if (c == EOF) return 0;
	} else if (fseek(f, 0, SEEK_SET)) {
		return 0;
	}
	if (p >= size) return 0;
	*start = p;
	{
		int n = 0;
		while ((c = fgetc(f)) != EOF && c != '\n') {
			p++;
			if (n < LINE_MAX_LEN - 1) line[n++] = (char)c;
		}
		line[n] = 0;
		if (c == '\n') p++;
	}
	*next = p;
	return 1;
}

int cover_index_find(const char *index_path, const char *serial, cover_row *out)
{
	char want[16], line[LINE_MAX_LEN], key[16];
	long size, lo = 0, hi, start, next;
	int found = 0;
	FILE *f;
	if (!index_path || !serial || !out || norm(serial, want) < 0) return 0;
	if (!(f = fopen(index_path, "rb"))) return 0;
	if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0) {
		fclose(f);
		return 0;
	}
	hi = size;	/* the answer, if there is one, starts in [lo, hi) */
	while (lo < hi) {
		long mid = lo + (hi - lo) / 2;
		int cmp;
		if (!line_at(f, mid, size, line, &start, &next) || start >= hi) {
			hi = mid;	/* no line starts at or after mid inside the range: look left of it */
			continue;
		}
		if (norm(line, key) < 0) break;	/* a damaged line: stop, find nothing */
		cmp = strcmp(want, key);
		if (cmp == 0) {
			const char *p = line;
			field(&p, out->serial, sizeof out->serial);
			field(&p, out->box, sizeof out->box);
			field(&p, out->disc, sizeof out->disc);
			field(&p, key, sizeof key);	/* the source column is not needed */
			field(&p, out->art3d, sizeof out->art3d);
			found = 1;
			break;
		}
		if (cmp < 0) hi = mid;	/* no line starts in [mid, start) */
		else lo = next;
	}
	fclose(f);
	return found;
}

int cover_index_path(char *out, int outsize, const char *dir, const char *rel)
{
	int n;
	if (!out || outsize < 2 || !dir || !rel || !rel[0]) return -1;
	n = (int)strlen(dir);
	if (n + 1 + (int)strlen(rel) + 1 > outsize) return -1;
	memcpy(out, dir, (size_t)n);
	if (n && dir[n - 1] != '/') out[n++] = '/';
	strcpy(out + n, rel);
	return 0;
}
