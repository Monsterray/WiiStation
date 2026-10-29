/* Run scripts/test-achievements.sh: exercise the real rcheevos integration on the host. */
#include "../achievements.h"
#include "../Gamecube/ach_progress.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

static unsigned char ram[0x200000], scratch[0x400];
unsigned char *psxM = ram, *psxH = scratch;
void SysPrintf(const char *fmt, ...) { (void)fmt; }

static achievement_notice_t notices[8];
static int notice_count;
static void on_notice(const achievement_notice_t *n)
{
    if (notice_count < 8) notices[notice_count++] = *n;
}

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

    /* Exercise the runtime's byte decoder too, not just our byte-copy callback.
     * On a big-endian host a byte must not land in the high end of a u32. */
    set_file("Game=TEST12345\n4|Single byte|0xH000010=1\n"
             "6|High-bit 32-bit word|0xX000024=2309737967\n"
             "7|IEEE float|fF000028=f1.0\n");
    ram[0x10] = 0;
    assert(achievements_load("TEST12345") == 3);
    achievements_frame();
    ram[0x10] = 1;
    achievements_frame();
    assert(achievements_earned() == 1);
    memcpy(ram + 0x24, "\xef\xcd\xab\x89\0\0\x80\x3f", 8);
    achievements_frame();
    assert(achievements_earned() == 3);
    achievements_close();
    assert(remove(achievementLog) == 0);
    ram[0x10] = 0;

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

    /* A failed SD/host journal open must leave an unlock pending for retry. */
    {
        char parent[256];
        assert(snprintf(parent, sizeof parent, "%s/retry", argv[1]) < 256);
        assert(snprintf(achievementLog, sizeof achievementLog, "%s/unlocks", parent) < 256);
        set_file("Game=TEST12345\n5|Retry write|0xH000030=1\n");
        ram[0x30] = 0;
        assert(achievements_load("TEST12345") == 1);
        achievements_frame();
        ram[0x30] = 1;
        achievements_frame();
        assert(achievements_earned() == 1);
        achievements_flush();
        assert(fopen(achievementLog, "r") == NULL);
        assert(mkdir(parent, 0700) == 0);
        achievements_flush();
        f = fopen(achievementLog, "r"); assert(f);
        {
            char record[256];
            assert(fgets(record, sizeof record, f));
            assert(strstr(record, " 5 Retry write"));
            assert(!fgets(record, sizeof record, f));
        }
        fclose(f);
        achievements_close();
    }

    /* A journal over the 1 MiB cap refuses the whole set; it is never partly restored. */
    {
        char big[256];
        unsigned long n;
        assert(snprintf(big, sizeof big, "%s/big_unlocks", argv[1]) < 256);
        strcpy(achievementLog, big);
        f = fopen(big, "wb"); assert(f);
        for (n = 0; n < 1024UL * 1024UL + 1; ++n) fputc('#', f);
        fclose(f);
        set_file("Game=TEST12345\n1|Big|0xH000010=1\n");
        assert(achievements_load("TEST12345") == -1 && !achievements_count());
        achievementLog[0] = 0;
    }
    /* A power loss can cut the journal's last record short. "<digest> 1" (cut from
     * "<digest> 12 Twelve") must not restore achievement 1, and the next unlock must
     * still be recorded on a line of its own, not glued to the fragment. */
    {
        char cut[256], line[256];
        assert(snprintf(cut, sizeof cut, "%s/cut_unlocks", argv[1]) < 256);
        strcpy(achievementLog, cut);
        remove(cut);
        set_file("Game=TEST12345\n1|One|0xH000030=1\n12|Twelve|0xH000031=1\n");
        ram[0x30] = 0; ram[0x31] = 0;
        assert(achievements_load("TEST12345") == 2);
        achievements_frame();
        ram[0x31] = 1;
        achievements_frame();
        assert(achievements_earned() == 1);
        achievements_close();
        f = fopen(cut, "rb"); assert(f);
        assert(fgets(line, sizeof line, f) && line[32] == ' ' && !strncmp(line + 33, "12 ", 3));
        fclose(f);
        line[34] = 0;                         /* "<digest> 1", no newline */
        f = fopen(cut, "wb"); assert(f); assert(fputs(line, f) >= 0); fclose(f);
        ram[0x31] = 0;
        assert(achievements_load("TEST12345") == 2);
        assert(achievements_earned() == 0);   /* the fragment restored nothing */
        achievements_frame();
        ram[0x30] = 1;                        /* now earn 1 for real */
        achievements_frame();
        assert(achievements_earned() == 1);
        achievements_close();
        ram[0x30] = 0;
        assert(achievements_load("TEST12345") == 2);
        assert(achievements_earned() == 1);   /* its record survived on its own line */
        achievements_close();
        achievementLog[0] = 0;
    }
    /* Modes: Test evaluates but never reads or writes the journal; Official loads nothing
     * until it is built. achievements_entry feeds the menu's list. */
    {
        char tj[256];
        achievement_entry_t e;
        assert(snprintf(tj, sizeof tj, "%s/test_unlocks", argv[1]) < 256);
        strcpy(achievementLog, tj);
        f = fopen(tj, "wb"); assert(f);   /* a journal that already says id 1 is earned */
        assert(fputs("0123456789abcdef0123456789abcdef 1 One\n", f) >= 0); fclose(f);
        set_file("Game=TEST12345\n1|One|0xH000040=1\n2|Two|0xH000041=1\n");
        achievementsEnabled = ACH_TEST;
        ram[0x40] = 0; ram[0x41] = 0;
        assert(achievements_load("TEST12345") == 2);
        assert(achievements_earned() == 0);
        achievements_frame();
        ram[0x41] = 1;
        achievements_frame();
        assert(achievements_entry(1, &e) == 0 && e.earned && !strcmp(e.title, "Two") && e.rarity_x100 == -1);
        assert(achievements_entry(0, &e) == 0 && !e.earned && !strcmp(e.title, "One") && !e.progress[0]);
        assert(achievements_entry(2, &e) == -1);
        achievements_close();
        f = fopen(tj, "rb"); assert(f);   /* untouched: still the one fake line */
        { char l[128]; int n = 0; while (fgets(l, sizeof l, f)) ++n; assert(n == 1); }
        fclose(f);
        achievementsEnabled = ACH_OFFICIAL;
        assert(achievements_load("TEST12345") == 0 && !achievements_count());
        achievementsEnabled = ACH_CUSTOM;
        achievementLog[0] = 0;
    }
    /* Optional points|type|description, the Title= line, and the notice the toast draws:
     * Beaten once every Progression and a Win are earned, Mastered with the last one. */
    {
        achievementsEnabled = ACH_TEST;
        achievements_notify = on_notice;
        set_file("Game=TEST12345\nTitle=Fixture\n"
                 "1|Story|0xH000050=1|5|progression|Reach the end\n"
                 "2|Finale|0xH000051=1|10|Win\n"
                 "3|Extra|0xH000052=1|100||The rest of the line | keeps its bar\n");
        ram[0x50] = ram[0x51] = ram[0x52] = 0;
        assert(achievements_load("TEST12345") == 3);
        assert(notice_count == 1 && notices[0].kind == ACH_NOTICE_GAME);
        notice_count = 0;
        achievements_frame();
        ram[0x50] = 1; achievements_frame();
        assert(notice_count == 1 && notices[0].points == 5 && notices[0].type == ACH_TYPE_PROGRESSION);
        assert(!notices[0].beaten && !notices[0].mastered && notices[0].earned == 1 && notices[0].count == 3);
        assert(notices[0].total_points == 115 && notices[0].rarity_x100 == -1);
        assert(!strcmp(notices[0].game, "Fixture") && !strcmp(notices[0].desc, "Reach the end"));
        ram[0x51] = 1; achievements_frame();
        assert(notice_count == 2 && notices[1].type == ACH_TYPE_WIN && notices[1].beaten && !notices[1].mastered);
        ram[0x52] = 1; achievements_frame();
        assert(notice_count == 3 && notices[2].mastered && !notices[2].beaten && notices[2].points == 100);
        assert(notices[2].type == ACH_TYPE_STANDARD && !strcmp(notices[2].desc, "The rest of the line | keeps its bar"));
        achievements_frame();
        assert(notice_count == 3);   /* an earned one never notifies again */
        achievements_close();
        set_file("Game=TEST12345\n1|Too many points|0xH000050=1|1001\n");
        assert(achievements_load("TEST12345") == -1);
        set_file("Game=TEST12345\n1|Unknown type|0xH000050=1|5|boss\n");
        assert(achievements_load("TEST12345") == -1);
        achievements_notify = NULL;
        achievementsEnabled = ACH_CUSTOM;
    }
    /* The game card at load, measured progress (only when it grows), a challenge (primed) */
    {
        achievement_entry_t e;
        achievementsEnabled = ACH_TEST;
        achievements_notify = on_notice;
        notice_count = 0;
        set_file("Game=TEST12345\nTitle=Fixture\n"
                 "1|Count to ten|M:0xH000070=10|5\n"
                 "2|Two steps|0xH000071=1_0xH000072=1|10\n");
        ram[0x70] = ram[0x71] = ram[0x72] = 0;
        assert(achievements_load("TEST12345") == 2);
        assert(notice_count == 1 && notices[0].kind == ACH_NOTICE_GAME && !strcmp(notices[0].game, "Fixture"));
        assert(notices[0].count == 2 && notices[0].earned == 0 && notices[0].total_points == 15 && !strcmp(notices[0].desc, "Test"));
        assert(!strcmp(achievements_title(), "Fixture"));
        assert(!strcmp(achievements_set_id(), ""));   /* Test mode keeps nothing, so it takes no unlock pictures */
        achievements_frame();
        ram[0x70] = 3; achievements_frame();
        assert(notice_count == 1);   /* the settling time: a reading only sets the baseline (Gamecube/ach_progress.h) */
        assert(achievements_entry(0, &e) == 0 && !strcmp(e.progress, "3/10") && e.percent == 30);
        { int f; for (f = 0; f < ACH_SETTLE_FRAMES; f++) achievements_frame(); }
        ram[0x70] = 3; achievements_frame();
        assert(notice_count == 1);   /* 30 % was the baseline: no pop-up for it, before or after the settling time */
        ram[0x70] = 4; achievements_frame();
        assert(notice_count == 2 && notices[1].kind == ACH_NOTICE_PROGRESS && notices[1].id == 1 && notices[1].percent == 40);
        ram[0x70] = 2; achievements_frame();   /* it falls back: nothing, and the step goes down with it */
        assert(notice_count == 2);
        ram[0x70] = 3; achievements_frame();
        assert(notice_count == 3 && notices[2].percent == 30);   /* climbing to a new step shows again */
        achievements_reset();   /* a reset: a new baseline and a new settling time */
        ram[0x70] = 6; achievements_frame();
        { int f; for (f = 0; f < ACH_SETTLE_FRAMES; f++) achievements_frame(); }
        assert(notice_count == 3);
        ram[0x70] = 7; achievements_frame();
        assert(notice_count == 4 && notices[3].percent == 70);
        achievements_close();
        notice_count = 0;
        set_file("Game=TEST12345\n1|Primed|0xH000073=1_T:0xH000074=1|10\n");
        ram[0x73] = ram[0x74] = 0;
        assert(achievements_load("TEST12345") == 1 && notice_count == 1);
        achievements_frame();
        ram[0x73] = 1; achievements_frame();
        assert(notice_count == 2 && notices[1].kind == ACH_NOTICE_CHALLENGE_ON && notices[1].id == 1);
        ram[0x73] = 0; achievements_frame();
        assert(notice_count == 3 && notices[2].kind == ACH_NOTICE_CHALLENGE_OFF);
        ram[0x73] = 1; achievements_frame();
        ram[0x74] = 1; achievements_frame();
        assert(notices[notice_count - 1].kind == ACH_NOTICE_UNLOCK && achievements_earned() == 1);
        achievements_close();
        achievements_notify = NULL;
        achievementsEnabled = ACH_CUSTOM;
    }
    /* A multi-disc set: every Game= serial loads it, and a swap to a listed disc keeps it running */
    {
        achievementsEnabled = ACH_TEST;
        set_file("Game=DISC00001, DISC00002\nGame=DISC00003\n1|Disc three|0xH000060=1\n");
        ram[0x60] = 0;
        assert(achievements_load("DISC00002") == 1 && achievements_load("DISC00003") == 1);
        assert(achievements_load("DISC00004") == -1);
        assert(achievements_load("DISC00001") == 1);
        achievements_frame();
        achievements_change_media("disc3.cue", "DISC00003");
        assert(achievements_count() == 1);
        ram[0x60] = 1; achievements_frame();
        assert(achievements_earned() == 1);
        achievements_change_media("other.cue", "SLUS00001");
        assert(!achievements_count());
        assert(achievements_load("DISC00001") == 1);
        achievements_change_media("bad.cue", "");   /* no PS1 boot file on the new disc */
        assert(!achievements_count());
        set_file("Game=DISC00001\n1|A|0xH000060=1\nGame=DISC00002\n");   /* Game= after an achievement */
        assert(achievements_load("DISC00001") == -1);
        set_file("Game=DISC00001,,DISC00002\n1|A|0xH000060=1\n");
        assert(achievements_load("DISC00001") == -1);
        set_file("Game=DISC000010\n1|A|0xH000060=1\n");             /* a serial is at most 9 characters */
        assert(achievements_load("DISC000010") == -1);
        set_file("Game=A,B,C,D,E,F,G,H,I\n1|A|0xH000060=1\n");      /* at most 8 discs */
        assert(achievements_load("A") == -1);
        achievementsEnabled = ACH_CUSTOM;
    }
    free(state); free(old_state);
    puts("achievements: memory, evaluation, state identity, journal, modes, notices, multi-disc sets and malformed sets ok");
    return 0;
}
