/* Run scripts/test-achievements.sh: exercise the real rcheevos integration on the host. */
#include "../achievements.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static unsigned char ram[0x200000], scratch[0x400];
unsigned char *psxM = ram, *psxH = scratch;
void SysPrintf(const char *fmt, ...) { (void)fmt; }

static void set_file(const char *text)
{
    FILE *f = fopen(achievementFile, "wb");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(fclose(f) == 0);
}

int main(int argc, char **argv)
{
    unsigned char bytes[8], *state, *old_state;
    int size, old_size;
    FILE *f;
    (void)argc;
    assert(argv[1]);
    assert(snprintf(achievementFile, sizeof(achievementFile), "%s/set.txt", argv[1]) < 256);
    assert(snprintf(achievementLog, sizeof(achievementLog), "%s/unlocks.txt", argv[1]) < 256);
    achievementsEnabled = 1;

    ram[0x1ffffe] = 0x12; ram[0x1fffff] = 0x34;
    scratch[0] = 0x56; scratch[1] = 0x78;
    memset(bytes, 0xcc, sizeof(bytes));
    assert(achievements_read_memory(0x1ffffe, bytes, 4, NULL) == 4);
    assert(!memcmp(bytes, "\x12\x34\x56\x78", 4));
    assert(bytes[4] == 0xcc);
    assert(achievements_read_memory(0x2003ff, bytes, UINT32_MAX, NULL) == 1);
    assert(achievements_read_memory(UINT32_MAX, bytes, 4, NULL) == 0);
    assert(achievements_read_memory(0x200400, bytes, 4, NULL) == 0);
    assert(achievements_read_memory(0, NULL, 4, NULL) == 0);

    set_file("# local fixture\nGame=TEST12345\n1|Three frames|0xH000010=1.3.\n2|Little endian word|0x 000020=4660\n3|Scratch byte|0xH200001=9\n");
    assert(achievements_load("WRONG") == -1);
    assert(!achievements_count());
    assert(achievements_load("TEST12345") == 3);
    achievements_frame(); /* initially false: release the runtime's WAITING state */
    old_size = achievements_state_save(NULL, 0);
    old_state = malloc(old_size); assert(old_state);
    assert(achievements_state_save(old_state, old_size) == old_size);
    ram[0x10] = 1;
    achievements_frame();
    size = achievements_state_save(NULL, 0);
    state = malloc(size); assert(state);
    assert(achievements_state_save(state, size) == size);
    achievements_frame(); assert(!achievements_earned());
    assert(achievements_state_load(state, size)); /* roll back from two hits to one */
    achievements_frame(); assert(!achievements_earned());
    achievements_pause(); achievements_frame(); assert(!achievements_earned());
    /* Loading does not resume a deliberately paused engine. Reset restarts evaluation. */
    achievements_reset();
    ram[0x10] = 0; achievements_frame();
    assert(achievements_state_load(state, size));
    ram[0x10] = 1;
    achievements_frame(); assert(!achievements_earned());
    achievements_frame(); assert(achievements_earned() == 1);
    ram[0x20] = 0x34; ram[0x21] = 0x12; scratch[1] = 9;
    achievements_frame(); assert(achievements_earned() == 3);
    assert(achievements_state_load(old_state, old_size));
    assert(achievements_earned() == 3); /* older states do not revoke unlocks */
    achievements_flush();
    f = fopen(achievementLog, "r"); assert(f);
    {
        char record[256];
        unsigned records = 0;
        while (fgets(record, sizeof(record), f)) ++records;
        assert(records == 3); /* one record per unlock despite older state restoration */
    }
    fclose(f);
    achievements_close();
    assert(achievements_load("TEST12345") == 3 && achievements_earned() == 3);
    achievements_reset(); assert(achievements_earned() == 3);
    assert(!achievements_state_load(state, 2));
    assert(achievements_earned() == 3);
    achievements_close();

    /* Content identity prevents an old journal/state from leaking into a changed set. */
    set_file("Game=TEST12345\n1|Changed definition|0xH000010=2.3.\n");
    ram[0x10] = 0;
    assert(achievements_load("TEST12345") == 1 && achievements_earned() == 0);
    assert(!achievements_state_load(state, size));
    assert(!achievements_earned());
    assert(!achievements_state_load(NULL, 0));
    achievements_frame();
    ram[0x10] = 2; achievements_frame(); achievements_frame();
    assert(!achievements_earned());
    size = achievements_state_save(NULL, 0);
    free(state); state = malloc(size); assert(state);
    assert(achievements_state_save(state, size) == size);
    state[size - 1] ^= 0x80;
    assert(!achievements_state_load(state, size)); /* corrupt runtime checksum */
    ram[0x10] = 0; achievements_frame();
    ram[0x10] = 2; achievements_frame(); assert(!achievements_earned());
    achievements_close();

    set_file("Game=TEST12345\n1|A|0xH000010=1\n1|Duplicate|0xH000020=1\n");
    assert(achievements_load("TEST12345") == -1 && !achievements_count());
    set_file("Game=TEST12345\n1|Broken|not a condition\n");
    assert(achievements_load("TEST12345") == -1 && !achievements_count());
    set_file("Game=TEST12345\n4294967296|Overflow|0xH000010=1\n");
    assert(achievements_load("TEST12345") == -1);
    set_file("Game=TEST12345\n");
    assert(achievements_load("TEST12345") == -1);
    set_file("Game=TEST12345\n1|Valid|0xH000010=1\n");
    strcpy(achievementLog, achievementFile);
    assert(achievements_load("TEST12345") == -1); /* never append over definitions */
    achievementLog[0] = 0;
    achievementsEnabled = 0;
    assert(!achievements_load("TEST12345"));
    achievements_frame();
    assert(!achievements_count());
    assert(!achievements_state_save(NULL, 0));
    achievementsEnabled = 1;
    f = fopen(achievementFile, "wb"); assert(f);
    for (size = 0; size < 65537; ++size) fputc('a', f);
    fclose(f);
    assert(achievements_load("TEST12345") == -1);
    free(state); free(old_state);
    puts("achievements: memory, evaluation, state identity, journal, and malformed sets ok");
    return 0;
}
