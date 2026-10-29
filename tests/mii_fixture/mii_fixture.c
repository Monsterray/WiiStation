/* mii_fixture: N fictional Miis for test databases (scripts/mii_fixture.py), made with the
 * Mii SDK on the PC (deps/mii, -DMII_NO_OGC). Writes N x 74 packed bytes to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include "mii.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static const char *NAMES[] = { "Testorb", "Kat", "Ollie", "Rina", "Big Al", "Nana", "Jules", "Pip",
	"Marco", "Sora", "Dee", "Hiro", "Luz", "Benji", "Tess", "Ray", "Mo", "Ivy", "Gus", "Kiki" };

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 20, i;
	uint32_t seed = 12345;
	const uint8_t mac[6] = { 0x00, 0x17, 0xab, 0x12, 0x34, 0x56 };   /* not a real Wii's */
#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	for (i = 0; i < n; i++) {
		mii m;
		uint8_t raw[MII_SIZE];
		const char *why = NULL;
		mii_random(&m, MII_ANY, MII_ANY, &seed);
		mii_set_name(m.name, NAMES[i % 20]);
		mii_set_name(m.creator, "WiiStation");
		m.fav_color = i % 12;
		m.favorite = i % 5 == 1;
		mii_id_make(m.id, 11505369649262175ull + (40000000ull + i * 977ull) * 4 * 60750000ull, mac);   /* 2006 + 40 Ms + a step: each Mii its own ID */
		if (mii_check(&m, &why) != MII_OK) {
			fprintf(stderr, "Mii %d: %s\n", i, why ? why : "?");
			return 1;
		}
		mii_pack(&m, raw);
		fwrite(raw, 1, MII_SIZE, stdout);
	}
	return 0;
}
