/* lab_net.c -- a test run driven over the network, for a Wii on the bench.
 *
 * scripts/wii_lab.py sends the DOL with wiiload (the Homebrew Channel, TCP 4299) and the
 * argument "lab=HOST:PORT", where it listens. Then:
 *
 *   1. lab_fetch(), before autoboot.txt is read: connect, say "HELLO WiiStation", and take
 *      lines from the PC until "GO":
 *        PUT <bytes> <path>   the next <bytes> bytes are sd:/wiisxrx/<path> (autoboot.txt,
 *                             input scripts, settings; games too, into isos/...)
 *        WANT <path>          send sd:/wiisxrx/<path> back at the end, if it exists
 *   2. The chain runs as it does in Dolphin.
 *   3. lab_report(), when the chain is done: connect again, say "RESULTS", then
 *      "FILE <bytes> <path>" + the bytes for each wanted file, then "END". The chain then
 *      exits to the Homebrew Channel (its reload stub) instead of powering off, so the next
 *      test can be sent at once. A crash returns there too (__exception_setreload).
 *
 * Paths are relative to sd:/wiisxrx/ and may not contain "..", ':' or start with '/'. */
#include <gccore.h>
#include <network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lab_net.h"

#define LAB_ROOT "sd:/wiisxrx/"

static char lab_host[64];
static int lab_port;
static char lab_want[32][96];
static int lab_nwant;
static u8 lab_buf[32 * 1024] __attribute__((aligned(32)));

void lab_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *c;
		if (!argv[i] || strncmp(argv[i], "lab=", 4))
			continue;
		snprintf(lab_host, sizeof lab_host, "%s", argv[i] + 4);
		c = strchr(lab_host, ':');
		lab_port = c ? atoi(c + 1) : 4300;
		if (c)
			*c = 0;
	}
}

int lab_active(void)
{
	return lab_host[0] != 0;
}

static s32 lab_connect(void)
{
	static int up;
	struct sockaddr_in sa;
	char ip[16];
	s32 s;
	int t;

	for (t = 0; !up && t < 5; t++)
		up = if_config(ip, NULL, NULL, true) >= 0;
	if (!up)
		return -1;
	s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (s < 0)
		return -1;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(lab_port);
	if (!inet_aton(lab_host, &sa.sin_addr) ||
	    net_connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
		net_close(s);
		return -1;
	}
	return s;
}

static int lab_send(s32 s, const void *p, int n)
{
	const u8 *b = p;
	while (n > 0) {
		s32 k = net_send(s, b, n > 32768 ? 32768 : n, 0);
		if (k <= 0)
			return -1;
		b += k;
		n -= k;
	}
	return 0;
}

static int lab_line(s32 s, char *line, int max)
{
	int n = 0;
	while (n < max - 1) {
		char c;
		if (net_recv(s, &c, 1, 0) != 1)
			return -1;
		if (c == '\n')
			break;
		if (c != '\r')
			line[n++] = c;
	}
	line[n] = 0;
	return n;
}

static int lab_path_ok(const char *p)
{
	return p[0] && p[0] != '/' && !strstr(p, "..") && !strchr(p, ':');
}

/* the directories a path needs, under the root */
static void lab_mkdirs(const char *full)
{
	char d[256];
	char *c;
	snprintf(d, sizeof d, "%s", full);
	for (c = d + strlen(LAB_ROOT); (c = strchr(c, '/')); c++) {
		*c = 0;
		mkdir(d, 0777);
		*c = '/';
	}
}

int lab_fetch(void)
{
	char line[256], full[256];
	s32 s;
	int ok = -1;

	if (!lab_active() || (s = lab_connect()) < 0)
		return -1;
	lab_send(s, "HELLO WiiStation\n", 17);
	while (lab_line(s, line, sizeof line) >= 0) {
		if (!strcmp(line, "GO")) {
			ok = 0;
			break;
		}
		if (!strncmp(line, "WANT ", 5) && lab_path_ok(line + 5) && lab_nwant < 32) {
			snprintf(lab_want[lab_nwant++], sizeof lab_want[0], "%s", line + 5);
			continue;
		}
		if (!strncmp(line, "PUT ", 4)) {
			char *path = strchr(line + 4, ' ');
			long left = atol(line + 4);
			FILE *f = NULL;
			if (path && lab_path_ok(path + 1)) {
				snprintf(full, sizeof full, LAB_ROOT "%s", path + 1);
				lab_mkdirs(full);
				f = fopen(full, "wb");
			}
			while (left > 0) {   /* read it even if it cannot be written, to stay in step */
				s32 k = net_recv(s, lab_buf, left > (long)sizeof lab_buf ? (s32)sizeof lab_buf : (s32)left, 0);
				if (k <= 0)
					break;
				if (f)
					fwrite(lab_buf, 1, k, f);
				left -= k;
			}
			if (f)
				fclose(f);
			if (left > 0)
				break;
			continue;
		}
		break;   /* anything else: the PC side is not what we expect */
	}
	lab_send(s, ok ? "FAIL\n" : "OK\n", ok ? 5 : 3);
	net_close(s);
	return ok;
}

void lab_report(void)
{
	char line[160], full[256];
	struct stat st;
	s32 s;
	int i;

	if (!lab_active() || (s = lab_connect()) < 0)
		return;
	lab_send(s, "RESULTS\n", 8);
	for (i = 0; i < lab_nwant; i++) {
		FILE *f;
		snprintf(full, sizeof full, LAB_ROOT "%s", lab_want[i]);
		if (stat(full, &st) || !(f = fopen(full, "rb")))
			continue;
		snprintf(line, sizeof line, "FILE %ld %s\n", (long)st.st_size, lab_want[i]);
		lab_send(s, line, strlen(line));
		for (;;) {
			size_t k = fread(lab_buf, 1, sizeof lab_buf, f);
			if (!k || lab_send(s, lab_buf, (int)k))
				break;
		}
		fclose(f);
	}
	lab_send(s, "END\n", 4);
	net_close(s);
}
