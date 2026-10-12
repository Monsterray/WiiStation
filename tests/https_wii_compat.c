/* Compatibility for a public HTTPS-only probe with r41's older newlib.
 * curl falls back to the original CA path when canonicalization is unavailable.
 * FTP wildcard matches are unsupported and fail closed; FTP is forbidden by
 * CURLOPT_PROTOCOLS_STR. Rebuild curl against the pinned SDK before product use. */
#include <errno.h>
#include <fnmatch.h>
#include <stdlib.h>
char *realpath(const char *path, char *resolved) {
    (void)path; (void)resolved;
    errno = ENOSYS;
    return NULL;
}
int fnmatch(const char *pattern, const char *string, int flags) {
    (void)pattern; (void)string; (void)flags;
    return FNM_NOMATCH;
}
