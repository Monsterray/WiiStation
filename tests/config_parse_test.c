/* config_parse_test.c - the settings parser against lines nobody meant to write.
 *
 *     cc -std=c11 -O2 -o config_parse_test tests/config_parse_test.c && ./config_parse_test
 *
 * Needs C11, not C89/C99: the file uses _Static_assert, so build with -std=c11 (or later).
 *
 * Runs on the host: config_parse.h is plain C with no libogc in it, so the exact code the
 * Wii runs is the code under test here. That includes config_getline(), which readConfig()
 * uses to read the settings file; its checks write small files with tmpfile().
 *
 * Each line goes into the middle of a buffer with a guard pattern around it, the way
 * readConfig() puts it on the stack. A scan that runs off the end of the line shows up as
 * a changed guard. Both scans used to do that: a value with no closing quote, and a line
 * with no separator at all, each walked past the end and wrote a zero into what followed.
 */
#include <stdio.h>
#include <string.h>

#include "../Gamecube/config_parse.h"

/* The test frame matches the production line buffer by construction, and the
 * assert below breaks the build if the two ever drift apart again. */
#define LINE_MAX CONFIG_LINE_MAX
#define GUARD    0x5a

_Static_assert(LINE_MAX == 256, "production line buffer changed size; update the frames");

static int failures;

static void fail(const char* what, const char* why)
{
	printf("  FAIL  %-34s %s\n", what, why);
	failures++;
}

/* One line, framed by guard bytes. Returns 0 when a guard was touched. */
struct frame {
	char before[32];
	char line[LINE_MAX];
	char after[32];
};

static void frame_set(struct frame* f, const char* line)
{
	memset(f, GUARD, sizeof(*f));
	memset(f->line, GUARD, sizeof(f->line));
	/* Bounded like the production fgets: a bad test line must fail the test,
	 * not smash the frame (which would look exactly like a parser overrun). */
	{
		size_t n = strlen(line);
		if (n >= sizeof(f->line)) {
			printf("  FAIL  frame_set %-22s line too long (%u)\n", line, (unsigned)n);
			failures++;
			n = sizeof(f->line) - 1;
		}
		memcpy(f->line, line, n);
		f->line[n] = 0;
	}
}

static int frame_intact(const struct frame* f)
{
	unsigned i;

	for(i = 0; i < sizeof(f->before); i++)
		if((unsigned char)f->before[i] != GUARD) return 0;
	for(i = 0; i < sizeof(f->after); i++)
		if((unsigned char)f->after[i] != GUARD) return 0;
	return 1;
}

/* What handleConfigPair() does, without the table lookup. */
static void parse(struct frame* f, const char** key, const char** value)
{
	char* v = config_split(f->line);

	*key = f->line;
	if(!v) { *value = 0; return; }
	*value = (v[0] == '"') ? config_unquote(v) : v;
}

static void check(const char* line, const char* want_key, const char* want_value)
{
	struct frame f;
	const char *key, *value;
	char what[96];

	snprintf(what, sizeof(what), "\"%s\"", line);
	frame_set(&f, line);
	parse(&f, &key, &value);

	if(!frame_intact(&f)) {
		fail(what, "the scan ran off the end of the line");
		return;
	}
	if(want_key && strcmp(key, want_key)) {
		fail(what, "wrong key");
		return;
	}
	if(!want_value) {
		if(value) fail(what, "a line with no separator gave a value");
		else printf("  ok    %-34s no setting, buffer intact\n", what);
		return;
	}
	if(!value || strcmp(value, want_value)) {
		fail(what, "wrong value");
		return;
	}
	printf("  ok    %-34s -> %s = \"%s\"\n", what, key, value);
}

/* readConfig's reader over a whole file: the lines config_getline returns, joined, must be
 * `expect`. A line longer than LINE_MAX must not come back in pieces. */
static void check_reader(const char* what, const char* content, const char* expect)
{
	FILE* f = tmpfile();
	char line[LINE_MAX];
	char got[1024] = "";

	if(!f) {
		fail(what, "tmpfile failed");
		return;
	}
	fputs(content, f);
	rewind(f);
	while(config_getline(line, LINE_MAX, f)) {
		if(strlen(got) + strlen(line) >= sizeof(got)) {
			fail(what, "more text read than expected");
			fclose(f);
			return;
		}
		strcat(got, line);
	}
	fclose(f);
	if(strcmp(got, expect))
		fail(what, "lines read differ from the expected ones");
	else
		printf("  ok    %-34s as expected\n", what);
}

int main(void)
{
	printf("Lines that are wrong -- the ones that used to walk off the buffer:\n");
	check("smbpassword = \"hunter2", "smbpassword", "hunter2");  /* no closing quote */
	check("smbsharename = \"", "smbsharename", "");              /* quote, then nothing */
	check("", 0, 0);                                             /* a blank line */
	check("\n", 0, 0);                                           /* fgets keeps the newline */
	check("novalueatall", 0, 0);
	check("#########", 0, 0);

	printf("\nLines that are right -- these must still work:\n");
	check("smbipaddr = \"192.168.1.10\"", "smbipaddr", "192.168.1.10");
	check("smbusername=\"wii\"", "smbusername", "wii");
	check("Memcard0File = 1", "Memcard0File", "1");
	check("FPS:1", "FPS", "1");
	check("MenuFont\t=\t\"Segoe\"", "MenuFont", "Segoe");
	check("smbpassword = \"\"", "smbpassword", "");
	check("smbsharename = \"two words\"", "smbsharename", "two words");

	printf("\nLines that hold no setting (config_line), and an indented one that does:\n");
	{
		static const char* none[] = { "", "\n", "\r\n", "   \n", "# comment", "; comment",
			"  ; indented comment", "[WiiStation]", "\t[section]\r\n" };
		char buf[64];
		unsigned i;
		for(i = 0; i < sizeof(none) / sizeof(none[0]); i++) {
			strcpy(buf, none[i]);
			if(config_line(buf)) fail(none[i], "read as a setting");
		}
		strcpy(buf, "  FPS = 1\n");
		if(!config_line(buf) || strncmp(config_line(buf), "FPS", 3)) fail("  FPS = 1", "indent not skipped");
	}

	printf("\nComma-separated lists (config_list_next):\n");
	{
		static const struct { const char* in; const char* items[4]; } lists[] = {
			{ "/wiistation/isos, /roms/psx", { "/wiistation/isos", "/roms/psx" } },
			{ "  a ,, b ,", { "a", "b" } },
			{ "", { 0 } },
			{ " , ", { 0 } },
			{ "/Games/PS One", { "/Games/PS One" } },
			{ "sd:/x,usb:/y", { "sd:/x", "usb:/y" } },
		};
		unsigned i, k;
		char out[32];
		for(i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
			const char* p = lists[i].in;
			for(k = 0; config_list_next(&p, out, sizeof(out)); k++)
				if(k >= 4 || !lists[i].items[k] || strcmp(out, lists[i].items[k]))
					fail(lists[i].in, "wrong item");
			if(k < 4 && lists[i].items[k])
				fail(lists[i].in, "item missing");
		}
		{   /* an item longer than the buffer is cut, not overrun */
			const char* p = "abcdefghijklmnopqrstuvwxyz0123456789";
			char small[8];
			if(!config_list_next(&p, small, sizeof(small)) || strcmp(small, "abcdefg"))
				fail("long item", "not cut to the buffer");
		}
	}

	printf("\nSettings files read line by line (config_getline, as readConfig reads them):\n");
	{
		char cut[300];      /* a comment of 255 bytes: fgets stops there, the tail is next */
		char fit[300];      /* 254 characters and a newline: 255 bytes, one fgets */
		char content[600];
		char expect[600];

		cut[0] = '#';
		memset(cut + 1, 'c', 254);
		cut[255] = 0;
		snprintf(content, sizeof(content), "%sFPS = 0\nMenuFont = \"Segoe\"\n", cut);
		check_reader("over-long line, tail dropped", content, "MenuFont = \"Segoe\"\n");

		check_reader("over-long line at end of file", cut, "");

		fit[0] = '#';
		memset(fit + 1, 'd', 253);
		fit[254] = '\n';
		fit[255] = 0;
		snprintf(content, sizeof(content), "%sFPS = 1\n", fit);
		snprintf(expect, sizeof(expect), "%sFPS = 1\n", fit);
		check_reader("line that fits, with newline", content, expect);

		check_reader("last line without newline", "FPS = 1", "FPS = 1");
	}

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
