/* config_parse_test.c - the settings parser against lines nobody meant to write.
 *
 *     cc -O2 -o config_parse_test tests/config_parse_test.c && ./config_parse_test
 *
 * Runs on the host: config_parse.h is plain C with no libogc in it, so the exact code the
 * Wii runs is the code under test here.
 *
 * Each line goes into the middle of a buffer with a guard pattern around it, the way
 * readConfig() puts it on the stack. A scan that runs off the end of the line shows up as
 * a changed guard. Both scans used to do that: a value with no closing quote, and a line
 * with no separator at all, each walked past the end and wrote a zero into what followed.
 */
#include <stdio.h>
#include <string.h>

#include "../Gamecube/config_parse.h"

#define LINE_MAX 256
#define GUARD    0x5a

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
	strcpy(f->line, line);          /* strcpy writes the terminator, as fgets does */
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

	printf(failures ? "\n%d failure(s)\n" : "\nall checks passed\n", failures);
	return failures ? 1 : 0;
}
