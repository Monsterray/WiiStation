/**
 * WiiStation - smb2dev.h
 *
 * "smb:" as a device (fopen, opendir, stat) over libsmb2 (deps/libsmb2): SMB2 and SMB3, so
 * a current Samba or Windows share works as it is set up. It replaces libtinysmb, which
 * spoke only SMB1 and NTLMv1 -- both off by default on current servers.
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
**/

#ifndef SMB2DEV_H
#define SMB2DEV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to \\ip\share and add the "smb:" device. Returns 0, or a negative errno; then
 * smb2dev_error() says why in words. Blocks for up to the connect timeout. */
int  smb2dev_mount(const char *ip, const char *share, const char *user, const char *password);
void smb2dev_unmount(void);
int  smb2dev_mounted(void);
const char *smb2dev_error(void);

/* What the share has cost so far: requests and the bytes they brought; us = the time the
 * game waited for data, over `waits` reads that had to; cache_hits = the copies from the
 * blocks; req_us = the requests' summed time in flight, inflight_max = most in flight at once;
 * the pump thread's polls and their time. */
typedef struct {
	unsigned requests, cache_hits, reconnects, waits, inflight_max, pump_polls;
	unsigned long long bytes, us, req_us, pump_poll_us;
	/* the socket calls under them (deps/libsmb2/lib/compat.c) */
	unsigned polls, recvs, sends;
	unsigned long long poll_us, recv_us, send_us, recv_bytes;
} smb2dev_stats_t;
void smb2dev_stats(smb2dev_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
