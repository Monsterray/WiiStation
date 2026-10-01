/* xa_zigzag.h - the SPU's own 37800 -> 44100 Hz XA filter (dfsound/xa.c FeedXA_zigzag), kept
 * apart so tests/xa_zigzag_test.c runs the same code on the host. Plain C, no SPU state. */
#ifndef XA_ZIGZAG_H
#define XA_ZIGZAG_H
#include <stdint.h>

static const short xa_zigzag_tables[7][29] = {
 {      0,  0x0000,  0x0000,  0x0000,  0x0000, -0x0002,  0x000A, -0x0022,  0x0041, -0x0054,
   0x0034,  0x0009, -0x010A,  0x0400, -0x0A78,  0x234C,  0x6794, -0x1780,  0x0BCD, -0x0623,
   0x0350, -0x016D,  0x006B,  0x000A, -0x0010,  0x0011, -0x0008,  0x0003, -0x0001 },
 {      0,  0x0000,  0x0000, -0x0002,  0x0000,  0x0003, -0x0013,  0x003C, -0x004B,  0x00A2,
  -0x00E3,  0x0132, -0x0043, -0x0267,  0x0C9D,  0x74BB, -0x11B4,  0x09B8, -0x05BF,  0x0372,
  -0x01A8,  0x00A6, -0x001B,  0x0005,  0x0006, -0x0008,  0x0003, -0x0001,  0x0000 },
 {      0,  0x0000, -0x0001,  0x0003, -0x0002, -0x0005,  0x001F, -0x004A,  0x00B3, -0x0192,
   0x02B1, -0x039E,  0x04F8, -0x05A6,  0x7939, -0x05A6,  0x04F8, -0x039E,  0x02B1, -0x0192,
   0x00B3, -0x004A,  0x001F, -0x0005, -0x0002,  0x0003, -0x0001,  0x0000,  0x0000 },
 {      0, -0x0001,  0x0003, -0x0008,  0x0006,  0x0005, -0x001B,  0x00A6, -0x01A8,  0x0372,
  -0x05BF,  0x09B8, -0x11B4,  0x74BB,  0x0C9D, -0x0267, -0x0043,  0x0132, -0x00E3,  0x00A2,
  -0x004B,  0x003C, -0x0013,  0x0003,  0x0000, -0x0002,  0x0000,  0x0000,  0x0000 },
 { -0x0001,  0x0003, -0x0008,  0x0011, -0x0010,  0x000A,  0x006B, -0x016D,  0x0350, -0x0623,
   0x0BCD, -0x1780,  0x6794,  0x234C, -0x0A78,  0x0400, -0x010A,  0x0009,  0x0034, -0x0054,
   0x0041, -0x0022,  0x000A, -0x0001,  0x0000,  0x0001,  0x0000,  0x0000,  0x0000 },
 {  0x0002, -0x0008,  0x0010, -0x0023,  0x002B,  0x001A, -0x00EB,  0x027B, -0x0548,  0x0AFA,
  -0x16FA,  0x53E0,  0x3C07, -0x1249,  0x080E, -0x0347,  0x015B, -0x0044, -0x0017,  0x0046,
  -0x0023,  0x0011, -0x0005,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000 },
 { -0x0005,  0x0011, -0x0023,  0x0046, -0x0017, -0x0044,  0x015B, -0x0347,  0x080E, -0x1249,
   0x3C07,  0x53E0, -0x16FA,  0x0AFA, -0x0548,  0x027B, -0x00EB,  0x001A,  0x002B, -0x0023,
   0x0010, -0x0008,  0x0002,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000 },
};

/* Persistent across calls (a continuous filter over the whole stream, not per-sector); the
 * right channel holds the left one's samples for mono. Zero-initialised, like the legacy
 * path's own gauss_window: a stream restart does not clear this (dfspu.c's reset does).
 * Each sample is stored twice, at p and at p + 32, so tap i reads ring[p + 32 - i] with no
 * mask; that is ring[(p - i) & 31] of a 32-entry ring. */
static short xa_zz_ring[2][64];
static unsigned int xa_zz_p = 0;
static unsigned char xa_zz_lo[7], xa_zz_hi[7];   /* each phase's first and last nonzero tap */

static void xa_zz_init(void)
{
 unsigned int j, i;
 for (j = 0; j < 7; j++)
  {
   for (i = 0; !xa_zigzag_tables[j][i]; i++) ;
   xa_zz_lo[j] = (unsigned char)i;
   for (i = 28; !xa_zigzag_tables[j][i]; i--) ;
   xa_zz_hi[j] = (unsigned char)i;
  }
}

static inline void xa_zz_push(short l, short r)
{
 xa_zz_ring[0][xa_zz_p] = xa_zz_ring[0][xa_zz_p + 32] = l;
 xa_zz_ring[1][xa_zz_p] = xa_zz_ring[1][xa_zz_p + 32] = r;
 xa_zz_p = (xa_zz_p + 1) & 31;
}

/* Phase j on both channels, packed L | R << 16. The same sums as the 29-tap loop it
 * replaced: a zero tap adds (x * 0) >> 15 = 0, so the zero ends are skipped; integer adds
 * do not depend on order (29 terms of at most 32767 cannot overflow). Both channels are
 * always summed: after a stereo-to-mono switch the right ring still holds stereo samples. */
static inline uint32_t xa_zz_out(unsigned int j)
{
 const short *c = xa_zigzag_tables[j];
 const short *bl = &xa_zz_ring[0][xa_zz_p + 32], *br = &xa_zz_ring[1][xa_zz_p + 32];
 int32_t sl = 0, sr = 0;
 int i, hi;

 if (!xa_zz_hi[0])
  xa_zz_init();
 hi = xa_zz_hi[j];
 for (i = xa_zz_lo[j]; i <= hi; i++)
  {
   int32_t k = c[i];
   sl += ((int32_t)bl[-i] * k) >> 15;
   sr += ((int32_t)br[-i] * k) >> 15;
  }
 if (sl < -32768) sl = -32768; else if (sl > 32767) sl = 32767;
 if (sr < -32768) sr = -32768; else if (sr > 32767) sr = 32767;
 return (uint32_t)(unsigned short)sl | ((uint32_t)(unsigned short)sr << 16);
}
#endif
