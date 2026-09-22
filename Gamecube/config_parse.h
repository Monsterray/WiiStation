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

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
