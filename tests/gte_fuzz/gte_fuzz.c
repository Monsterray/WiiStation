/* gte.c against a real PlayStation: replays JaCzekanski's gte-fuzz log (ps1-tests tests.zip,
 * gte-fuzz/gte_valid_0xc0ffee_50.log: 50 random register states per command, each written
 * with MTC2 0..31 then CTC2 0..31, one command, every register read back; the "<" lines are
 * what a real PS1 returned). Two transfer paths, because the game runs on Lightrec and the
 * interpreter has its own: "i" = gte.c gteMTC2/gteCTC2/gteMFC2/gteCFC2, "l" = Lightrec's
 * lightrec_mtc2/ctc2/mfc2 and its CFC2 sign extension (deps/lightrec/lightrec.c, copied).
 *
 *   bash tests/gte_fuzz/run.sh LOG       per command: cases that differ and which registers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psxcommon.h"
#include "r3000a.h"
#include "gte.h"

psxRegisters psxRegs;
u32 psxMemRead32(u32 a) { (void)a; return 0; }
void psxMemWrite32(u32 a, u32 v) { (void)a; (void)v; }

static u32 g_enc;   /* the command word of the case being read */

typedef void (*gtefn)(struct psxCP2Regs *);
static gtefn op_for(int f)
{
	switch (f) {
	case 0x01: return gteRTPS;  case 0x06: return gteNCLIP; case 0x0c: return gteOP;
	case 0x10: return gteDPCS;  case 0x11: return gteINTPL; case 0x12: return gteMVMVA;
	case 0x13: return gteNCDS;  case 0x14: return gteCDP;   case 0x16: return gteNCDT;
	case 0x1b: return gteNCCS;  case 0x1c: return gteCC;    case 0x1e: return gteNCS;
	case 0x20: return gteNCT;   case 0x28: return gteSQR;   case 0x29: return gteDCPL;
	case 0x2a: return gteDPCT;  case 0x2d: return gteAVSZ3; case 0x2e: return gteAVSZ4;
	case 0x30: return gteRTPT;  case 0x3d: return gteGPF;   case 0x3e: return gteGPL;
	case 0x3f: return gteNCCT;
	}
	return NULL;
}

/* ---- "i": the interpreter's transfers (gte.c) ---- */
static void i_mtc2(int r, u32 v) { psxRegs.GPR.r[1] = v; psxRegs.code = (1 << 16) | (r << 11); gteMTC2(); }
static void i_ctc2(int r, u32 v) { psxRegs.GPR.r[1] = v; psxRegs.code = (1 << 16) | (r << 11); gteCTC2(); }
static u32 i_mfc2(int r) { psxRegs.code = (1 << 16) | (r << 11); gteMFC2(); return psxRegs.GPR.r[1]; }
static u32 i_cfc2(int r) { psxRegs.code = (1 << 16) | (r << 11); gteCFC2(); return psxRegs.GPR.r[1]; }

/* ---- "l": Lightrec's transfers, on the same register file (lightrec.c, little-endian host) ---- */
#define D psxRegs.CP2D.r
#define C psxRegs.CP2C.r
static s32 clamp_s32(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }
static u32 l_mfc2(int reg)
{
	s16 a, b, c;
	switch (reg) {
	case 1: case 3: case 5: case 8: case 9: case 10: case 11: return (s32)(s16)(u16)D[reg];
	case 7: case 16: case 17: case 18: case 19: return (u16)D[reg];
	case 28: case 29:
		a = (s16)(u16)D[9]; b = (s16)(u16)D[10]; c = (s16)(u16)D[11];
		return clamp_s32(a >> 7, 0, 0x1f) | clamp_s32(b >> 7, 0, 0x1f) << 5 | clamp_s32(c >> 7, 0, 0x1f) << 10;
	case 15: reg = 14; /* fallthrough */
	default: return D[reg];
	}
}
static u32 l_cfc2(int reg)
{
	u32 v = C[reg];
	switch (reg) { case 4: case 12: case 20: case 26: case 27: case 29: case 30: v = (u32)(s16)v; }
	return v;
}
static u32 count_leading_bits(s32 d) { return 1 + __builtin_clrsb(d); }
static void l_mtc2(int reg, u32 data)
{
	switch (reg) {
	case 15: D[12] = D[13]; D[13] = D[14]; D[14] = data; break;
	case 28: D[9] = (data << 7) & 0xf80; D[10] = (data << 2) & 0xf80; D[11] = (data >> 3) & 0xf80; break;
	case 31: return;
	case 30: D[31] = count_leading_bits((s32)data); /* fallthrough */
	default: D[reg] = data; break;
	}
}
static void l_ctc2(int reg, u32 data)
{
	switch (reg) {
	case 4: case 12: case 20: case 26: case 27: case 29: case 30:
		C[reg] = (C[reg] & 0xffff0000u) | (data & 0xffff); break;
	case 31: data = (data & 0x7ffff000) | (u32)!!(data & 0x7f87e000) << 31; /* fallthrough */
	default: C[reg] = data; break;
	}
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "r");
	int lr = argc > 2 && argv[2][0] == 'l';
	char line[256], name[16] = "";
	u32 in[64], want[64];
	int nin = 0, nwant = 0, funct = -1, r;
	unsigned v;
	static int cases[64], bad[64], regbad[64][64];
	static char names[64][16];

	if (!f) { perror(argv[1]); return 2; }
	for (;;) {
		char *ok = fgets(line, sizeof line, f);
		if (!ok || !strncmp(line, "Test ", 5) || !strncmp(line, "----", 4)) {
			if (funct >= 0 && nwant == 64) {
				gtefn fn = op_for(funct);
				int i, diff = 0;
				memset(&psxRegs, 0, sizeof psxRegs);
				for (i = 0; i < 32; i++) (lr ? l_mtc2 : i_mtc2)(i, in[i]);
				for (i = 0; i < 32; i++) (lr ? l_ctc2 : i_ctc2)(i, in[32 + i]);
				psxRegs.code = (0x4a << 25) | g_enc;   /* gteop: sf, mx, v, cv, lm */
				if (fn && !getenv("NOOP")) fn(&psxRegs.CP2);   /* NOOP=1: the harness must then report differences */
				for (i = 0; i < 64; i++) {
					u32 got = i < 32 ? (lr ? l_mfc2 : i_mfc2)(i) : (lr ? l_cfc2 : i_cfc2)(i - 32);
					if (got != want[i]) { diff = 1; regbad[funct][i]++; }
				}
				cases[funct]++; bad[funct] += diff;
				if (diff && getenv("SHOW") && atoi(getenv("SHOW")) == funct) {
					for (i = 0; i < 64; i++) {
						u32 got = i < 32 ? (lr ? l_mfc2 : i_mfc2)(i) : (lr ? l_cfc2 : i_cfc2)(i - 32);
						if (got != want[i]) printf("  %s r[%d] got %08x want %08x\n", name, i, got, want[i]);
					}
				}
			}
			if (!ok) break;
			nin = nwant = 0; funct = -1;
			continue;
		}
		if (sscanf(line, "> r[%d] = 0x%x", &r, &v) == 2 && r < 64) { in[r] = v; nin++; continue; }
		if (sscanf(line, "< r[%d] = 0x%x", &r, &v) == 2 && r < 64) { want[r] = v; nwant++; continue; }
		{
			int fu, sf, lm, tx, vx, mx;
			if (sscanf(line, "GTE 0x%x %15s (sf=%d, lm=%d, tx=%d, vx=%d, mx=%d)", &fu, name, &sf, &lm, &tx, &vx, &mx) == 7) {
				funct = fu;
				g_enc = (u32)fu | (u32)sf << 19 | (u32)mx << 17 | (u32)vx << 15 | (u32)tx << 13 | (u32)lm << 10;
				strcpy(names[fu], name);
			}
		}
	}
	for (r = 0; r < 64; r++) {
		int i;
		if (!cases[r]) continue;
		printf("%-6s %2d/%2d differ", names[r], bad[r], cases[r]);
		for (i = 0; i < 64; i++)
			if (regbad[r][i]) printf(" %s%d:%d", i < 32 ? "d" : "c", i & 31, regbad[r][i]);
		printf("\n");
	}
	return 0;
}
