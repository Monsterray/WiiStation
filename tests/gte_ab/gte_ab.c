/* Host A/B of two gte.c builds: the same seeded register states and opcodes go through
 * each; every data and control register is read back through MFC2/CFC2 and written to a
 * file. gte_ab_cmp.py compares two files; tests/gte_ab/run.sh does it all. Usage: gte_ab OUT.bin CASES_PER_OP [bench] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "psxcommon.h"
#include "r3000a.h"
#include "gte.h"

psxRegisters psxRegs;
u32 psxMemRead32(u32 a) { (void)a; return 0; }
void psxMemWrite32(u32 a, u32 v) { (void)a; (void)v; }

typedef void (*gtefn)(struct psxCP2Regs *);
static const struct { int funct; gtefn fn; const char *name; } ops[] = {
	{0x01, gteRTPS, "RTPS"}, {0x06, gteNCLIP, "NCLIP"}, {0x0c, gteOP, "OP"},
	{0x10, gteDPCS, "DPCS"}, {0x11, gteINTPL, "INTPL"}, {0x12, gteMVMVA, "MVMVA"},
	{0x13, gteNCDS, "NCDS"}, {0x14, gteCDP, "CDP"}, {0x16, gteNCDT, "NCDT"},
	{0x1b, gteNCCS, "NCCS"}, {0x1c, gteCC, "CC"}, {0x1e, gteNCS, "NCS"},
	{0x20, gteNCT, "NCT"}, {0x28, gteSQR, "SQR"}, {0x29, gteDCPL, "DCPL"},
	{0x2a, gteDPCT, "DPCT"}, {0x2d, gteAVSZ3, "AVSZ3"}, {0x2e, gteAVSZ4, "AVSZ4"},
	{0x30, gteRTPT, "RTPT"}, {0x3d, gteGPF, "GPF"}, {0x3e, gteGPL, "GPL"},
	{0x3f, gteNCCT, "NCCT"},
};
#define NOPS (int)(sizeof ops / sizeof ops[0])

static u32 rs = 0x12345678;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* three input kinds: 0 = any 32-bit value, 1 = game-like (+-4096, 1.0 in 4.12),
 * 2 = mid (+-0x8000, near the 16-bit limits) */
static u32 val(int kind)
{
	u32 r = rnd();
	if (kind == 0) return r;
	if (kind == 1) return (u32)(((s32)(r & 0x1fff) - 0x1000)) & 0xffff
		| ((u32)(((s32)((r >> 13) & 0x1fff) - 0x1000)) << 16);
	return r & 0xffff | ((r >> 16) << 16);
}

static void mtc2(int reg, u32 v) { psxRegs.GPR.r[1] = v; psxRegs.code = (1 << 16) | (reg << 11); gteMTC2(); }
static void ctc2(int reg, u32 v) { psxRegs.GPR.r[1] = v; psxRegs.code = (1 << 16) | (reg << 11); gteCTC2(); }
static u32 mfc2(int reg) { psxRegs.code = (1 << 16) | (reg << 11); gteMFC2(); return psxRegs.GPR.r[1]; }
static u32 cfc2(int reg) { psxRegs.code = (1 << 16) | (reg << 11); gteCFC2(); return psxRegs.GPR.r[1]; }

static void setup(int kind)
{
	int i;
	for (i = 0; i < 31; i++)
		if (i != 15 && i != 28 && i != 29) mtc2(i, val(kind));
	for (i = 0; i < 32; i++) {
		u32 v = val(kind);
		/* the screen offset, H and the Z scale factors are wide in games */
		if (kind == 1 && (i == 24 || i == 25)) v = (rnd() & 0x3ffffff) - 0x2000000;
		if (kind == 1 && i == 26) v = 0x100 + (rnd() & 0x3ff);
		ctc2(i, v);
	}
}

/* the encodings the PsyQ SDK macros emit (psx-spx "GTE Command Encoding"): sf=1, fixed lm */
static const u32 sdk[] = { 0x0180001, 0x1400006, 0x170000c, 0x0780010, 0x0980011, 0x0400012,
	0x0e80413, 0x1280414, 0x0f80416, 0x108041b, 0x138041c, 0x0c8041e, 0x0d80420, 0x0a80428,
	0x0680029, 0x0f8002a, 0x158002d, 0x168002e, 0x0280030, 0x198003d, 0x1a8003e, 0x118043f };

static u32 opcode_sdk(int o)
{
	u32 c = sdk[o];
	if (ops[o].funct == 0x12)	/* MVMVA: games use every mx/v/cv, and lm both ways */
		c |= (rnd() & (0x7f << 13 | 1 << 10)) | 1 << 19;
	return (0x4a << 25) | c;
}

static u32 opcode(int funct)
{
	/* sf (19), mx (17-18), v (15-16), cv (13-14), lm (10) random; funct fixed */
	return (0x4a << 25) | (rnd() & 0x01ffffc0 & ~0x03f) | funct;
}

int main(int argc, char **argv)
{
	int n = atoi(argv[2]), o, c, i;
	FILE *f = fopen(argv[1], "wb");
	for (o = 0; o < NOPS; o++)
		for (c = 0; c < n; c++) {
			u32 rec[66];
			int kind = c % 3, enc = (c / 3) & 1;
			setup(kind);
			psxRegs.code = enc ? opcode(ops[o].funct) : opcode_sdk(o);
			rec[0] = o | (kind << 8) | (enc << 16);
			rec[1] = psxRegs.code;
			ops[o].fn(&psxRegs.CP2);
			for (i = 0; i < 32; i++) rec[2 + i] = mfc2(i);
			for (i = 0; i < 32; i++) rec[34 + i] = cfc2(i);
			fwrite(rec, 4, 66, f);
		}
	fclose(f);
	if (argc > 3) {
		/* time each op on game-like states: 256 states, repeated */
		for (o = 0; o < NOPS; o++) {
			static psxCP2Regs st[256]; static u32 cd[256];
			clock_t t0; long k; volatile u32 sink = 0;
			for (i = 0; i < 256; i++) { setup(1); cd[i] = opcode_sdk(o); st[i] = psxRegs.CP2; }
			t0 = clock();
			for (k = 0; k < 2000000; k++) {
				psxRegs.CP2 = st[k & 255];
				psxRegs.code = cd[k & 255];
				ops[o].fn(&psxRegs.CP2);
				sink += psxRegs.CP2D.r[k & 31];
			}
			printf("time %s %.1f ns\n", ops[o].name, (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / 2000000);
		}
	}
	return 0;
}
