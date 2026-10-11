/**
 * WiiStation - config_parse.h
 *
 * The two scans in the settings-file parser, on their own so that a host test can feed
 * them a malformed line. Everything here works in place on one line of the file and has
 * no dependency on libogc, so tests/config_parse_test.c runs the same code the Wii runs.
 *
 * Both scans used to have no end test. A line with an opening quote and no closing one,
 * or a line with no separator at all -- a blank line, for example -- walked off the end
 * of the caller's 256-byte buffer and wrote a zero into whatever was after it. Hand
 * editing the settings file is the only way to set the SMB fields, so this was easy to
 * reach.
**/

#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H

#include <stdio.h>    /* FILE, fgets, fgetc */
#include <string.h>   /* memcpy, strchr */

/* Every caller reads settings/chain lines into a buffer this big and hands the
 * buffer to the scans below. One constant so the size cannot drift apart again
 * (the scans once walked off a 256-byte buffer; see tests/config_parse_test.c). */
#define CONFIG_LINE_MAX 256

#ifdef __cplusplus
extern "C" {
#endif

/* The start of the setting on a line of settings.ini, or NULL for a line that holds none:
 * blank, a comment ('#' or ';', as INI files have them) or a [section] header (the file
 * has no sections; one an INI editor adds is ignored). Leading blanks are skipped, so an
 * indented line still reads. */
static inline char* config_line(char* l)
{
	while(*l == ' ' || *l == '\t')
		++l;
	if(!*l || *l == '\r' || *l == '\n' || *l == '#' || *l == ';' || *l == '[')
		return 0;
	return l;
}

/* The next item of a comma-separated value ("/wiistation/isos, /roms/psx") into out,
 * without the blanks around it. *p moves past the item and its comma. Returns 0 when the
 * list has no more items; an empty item ("a,,b") is skipped. */
static inline int config_list_next(const char** p, char* out, unsigned size)
{
	for(;;) {
		const char* s = *p;
		const char* e;
		unsigned n;

		while(*s == ' ' || *s == '\t' || *s == ',')
			++s;
		if(!*s)
			return 0;
		e = s;
		while(*e && *e != ',')
			++e;
		*p = e;
		while(e > s && (e[-1] == ' ' || e[-1] == '\t'))
			--e;
		n = (unsigned)(e - s);
		if(!n)
			continue;
		if(n >= size)
			n = size - 1;
		memcpy(out, s, n);
		out[n] = 0;
		return 1;
	}
}

/* True for the characters that can come between a key and its value. */
static inline int config_is_sep(char c)
{
	return c == ' ' || c == '\t' || c == ':' || c == '=';
}

/* Split "key = value" in place: writes a zero over the separator and returns the value.
 * Returns NULL when the line holds no separator, and so no value. */
static inline char* config_split(char* kv)
{
	char* vs = kv;

	while(*vs && !config_is_sep(*vs))
		++vs;
	if(!*vs)
		return 0;

	*(vs++) = 0;
	while(config_is_sep(*vs))
		++vs;

	return vs;
}

/* Take the quotes off a quoted value, in place. Give it a value that starts with a quote.
 * A value with no closing quote ends at the end of the line. */
static inline char* config_unquote(char* v)
{
	char* p = ++v;

	while(*p && *p != '"')
		++p;
	*p = 0;

	return v;
}

/* The next whole line of a settings file into line (size bytes, newline kept), or 0 at the
 * end of the file. A line longer than the buffer is not split: fgets would hand its tail
 * over as a line of its own, and a tail that happens to read "FPS = 1" would set FPS. So the
 * rest of such a line is read up to its newline and dropped, and the truncated start is
 * dropped with it. */
static inline int config_getline(char* line, int size, FILE* f)
{
	int c;

	for(;;) {
		if(!fgets(line, size, f))
			return 0;
		if(strchr(line, '\n') || feof(f))
			return 1;
		while((c = fgetc(f)) != '\n' && c != EOF)
			;
	}
}

#ifdef __cplusplus
}
#endif

#endif
