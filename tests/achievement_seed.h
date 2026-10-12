/* One TLS owner, outside the emulation thread. Seed material is never logged. */
#ifndef WIISTATION_ACHIEVEMENT_SEED_H
#define WIISTATION_ACHIEVEMENT_SEED_H
#include <stddef.h>
#define ACHIEVEMENT_SEED_SIZE 64
/* Requires an existing privately provisioned seed. A leftover .busy directory
 * means an interrupted/failed session: refuse it, never restore an old seed. */
int achievement_seed_begin(const char *path);
int achievement_seed_read(unsigned char *out, size_t length);
int achievement_seed_write(unsigned char *in, size_t length);
int achievement_seed_end(void);
#endif
