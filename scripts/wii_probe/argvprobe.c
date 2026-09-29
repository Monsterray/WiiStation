/* argvprobe.c -- what a DOL sent by wiiload receives on the bench Wii.
 *
 * Shows argc/argv on the TV, then reports them over TCP to PROBE_HOST:4300 (this PC; the
 * address is compiled in, so the test does not depend on argv reaching the program), with
 * each network step's result, and returns to the Homebrew Channel after 8 s.
 * Build: scripts/wii_probe/build.sh. */
#include <gccore.h>
#include <network.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef PROBE_HOST
#define PROBE_HOST "192.168.8.147"
#endif

static char report[2048];
static int rlen;

static void say(const char *line)
{
	printf("  %s\n", line);
	if (rlen + (int)strlen(line) + 2 < (int)sizeof report)
		rlen += sprintf(report + rlen, "%s\n", line);
}

int main(int argc, char **argv)
{
	GXRModeObj *m;
	void *fb;
	struct sockaddr_in sa;
	char ip[16] = "", line[256];
	s32 r = 0, s;
	int i, t;

	VIDEO_Init();
	m = VIDEO_GetPreferredMode(NULL);
	fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
	console_init(fb, 20, 20, m->fbWidth, m->xfbHeight, m->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(m);
	VIDEO_SetNextFramebuffer(fb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	printf("\n\n  argvprobe\n\n");
	snprintf(line, sizeof line, "argc %d", argc);
	say(line);
	for (i = 0; i < argc; i++) {
		snprintf(line, sizeof line, "argv[%d] = %s", i, argv[i] ? argv[i] : "(null)");
		say(line);
	}

	for (t = 0; t < 300 && (r = net_init()) == -EAGAIN; t++)
		usleep(100 * 1000);
	snprintf(line, sizeof line, "net_init %d (after %d retries)", (int)r, t);
	say(line);
	for (t = 0; t < 10; t++) {
		if ((r = if_config(ip, NULL, NULL, true)) >= 0)
			break;
		usleep(500 * 1000);
	}
	snprintf(line, sizeof line, "if_config %d, wii ip %s", (int)r, ip);
	say(line);

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(4300);
	inet_aton(PROBE_HOST, &sa.sin_addr);
	for (t = 0; t < 5; t++) {
		s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		r = net_connect(s, (struct sockaddr *)&sa, sizeof sa);
		snprintf(line, sizeof line, "net_connect %s:4300 -> %d", PROBE_HOST, (int)r);
		say(line);
		if (r >= 0) {
			net_send(s, report, rlen, 0);
			net_close(s);
			break;
		}
		net_close(s);
		sleep(1);
	}
	printf("\n  back to the Homebrew Channel in 8 s\n");
	sleep(8);
	net_deinit();
	return 0;                             /* exit() goes back through HBC's stub */
}
