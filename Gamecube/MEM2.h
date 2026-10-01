/* MEM2.h - MEM2 boundaries for different chunks of memory
   by Mike Slegeir for Mupen64-Wii adapted for WiiSX by emu_kidid
 */

#ifndef MEM2_H
#define MEM2_H

// Define a KiloByte (KB) and a MegaByte (MB)
#define KB (1024)
#define MB (1024*1024)

// MEM2 begins at MEM2_LO, the Starlet's Dedicated Memory begins at MEM2_HI
#define MEM2_LO   ((char*)0x90080000)
#define MEM2_HI   ((char*)0x933E0000)
#define MEM2_SIZE (MEM2_HI - MEM2_LO)

// We want 128KB for our MEMCARD 1
#define MCD1_SIZE     (128*KB)
#define MCD1_LO       (MEM2_LO)
#define MCD1_HI       (MCD1_LO + MCD1_SIZE)

// We want 128KB for our MEMCARD 2
#define MCD2_SIZE     (128*KB)
#define MCD2_LO       (MCD1_HI)
#define MCD2_HI       (MCD2_LO + MCD2_SIZE)

// We want 128KB for HID BUF
#define HID_BUF_SIZE  (128*KB)
#define HID_BUF_LO    (MCD2_HI)
#define HID_BUF_HI    (HID_BUF_LO + HID_BUF_SIZE)

// The menu font's glyph heap is no longer a fixed region: IPLFont.cpp takes a block of the
// general MEM2 heap sized to the loaded font (0.1-0.4 MB for a Latin menu, up to ~8 MB for a
// full CJK font), so the 9 MB this region reserved goes to the heap. CN_FONT_SIZE is only
// the most a font may take.
#define CN_FONT_SIZE (9*MB)

// The old PPC dynarec's 10 MB is no longer a fixed region either: ppc/pR3000A.c takes it
// from the general MEM2 heap when that core starts (Core = 2) and frees it at shutdown.
#define RECMEM2_SIZE (10*MB)

// We want 512KB for the SPU buffer
#define SPU_BUF_SIZE (512*KB)
#define SPU_BUF_LO   (HID_BUF_HI)
#define SPU_BUF_HI   (SPU_BUF_LO + SPU_BUF_SIZE)

// We want 4MB for Lightrec code buffer
#define LIGHTREC_BUF_SIZE (4*MB)
#define LIGHTREC_BUF_LO   (SPU_BUF_HI)
#define LIGHTREC_BUF_HI   (LIGHTREC_BUF_LO + LIGHTREC_BUF_SIZE)

// We want 512KB for the PSX BIOS ROM image. It's read-mostly data that HLE
// mode -- the hardcoded default -- never fetches as code, so it doesn't
// need MEM1's speed; only opt-in LLE mode (a user-supplied BIOS dump) walks
// it as executable CPU-core input, and only briefly at boot.
#define PSXR_BUF_SIZE (512*KB)
#define PSXR_BUF_LO   (LIGHTREC_BUF_HI)
#define PSXR_BUF_HI   (PSXR_BUF_LO + PSXR_BUF_SIZE)


#define NEW_MEM2_LO PSXR_BUF_HI

#endif
