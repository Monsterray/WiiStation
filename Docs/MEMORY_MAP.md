# The Wii's memory, and what WiiStation does with it

Written 2026-09-21. Section 1 is the hardware; section 2 is where this emulator's memory
actually goes, measured from the debug ELF and the live counters on the menu's Memory page;
section 3 is the 9 MB glyph cache and what to do about it.

## 1. What the machine has

Broadway (the Wii's 729 MHz PowerPC 750CL) sees five distinct kinds of storage, and they
differ by more than an order of magnitude in speed. Fast code is mostly a question of
putting each thing in the right one.

| | Size | Where | Latency | Who else can reach it |
|---|---|---|---|---|
| **L1 data cache** | 32 KB | on the core | ~3 cycles | nobody — not coherent with any DMA |
| **Locked cache (LC)** | 16 KB of that L1 | on the core, addressed at `0xE0000000` | ~3 cycles | its own DMA engine, to and from main memory |
| **L2 cache** | 256 KB unified | on the core | ~10-15 cycles | nobody |
| **MEM1** | 24 MB 1T-SRAM | on the graphics die | low | GPU, video interface |
| **MEM2** | 64 MB GDDR3 | separate chip | substantially higher | IOS, USB and SD DMA |
| **TMEM** | 1 MB | inside the GPU | — | GPU only; the CPU fills it through GX |

Two more that are easy to forget:

- **The uncached mirror.** Every physical address is visible twice: cached at `0x8...`,
  uncached at `0xC...` (`MEM_K0_TO_K1`). Uncached access needs no flushing and is correct by
  construction, but every read is a bus transaction. Right for a DMA descriptor or a status
  word, wrong for anything bulk.
- **ARAM**, 16 MB, exists on the GameCube only and the CPU cannot address it at all — data
  moves in and out by DMA (`AR_*`). On the Wii, MEM2 took its place. WiiStation's GameCube
  build is the only place this would matter.

### What each one is actually for

**MEM1 is the one that decides frame rate.** It is on the same die as the graphics
processor, and everything the CPU touches every frame belongs there: emulated PSX RAM, the
emulated VRAM, the GX command buffer, the display buffers. Being physically closer is most
of why it is faster, and it is also the memory the GPU reads while drawing.

**MEM2 is bulk.** Four times the capacity, meaningfully slower, and shared with IOS (which
owns USB, SD and networking, and keeps a slice for itself — libogc's usable window ends at
`0x933E0000`). It is the right home for anything streamed, DMA'd or read once: recompiled
code, disc-image buffers, texture pages, decompressed assets. Putting a per-frame structure
here is a measurable mistake.

**The locked cache is the interesting one, and almost nothing uses it.** Half the L1 data
cache can be switched out of being a cache and into being 16 KB of directly addressed
memory at `0xE0000000`, with a DMA engine of its own that moves 32-byte blocks to and from
main memory while the CPU works. The pattern it is built for is streaming: start the load
for block N+1, process block N out of the scratchpad, store block N-1 — a tight kernel over
a large buffer then runs with no cache misses at all. The cost is that everything else gets
half the L1. WiiStation does not use it yet; `Docs/LOCKED_CACHE_PLAN.md` has the measured
case for where it would pay (parked until the hardware baseline exists).

**TMEM** is the GPU's own texture memory. The CPU never addresses it; textures are uploaded
into MEM1/MEM2 and GX manages the cache regions. Dolphin does not emulate it at all, which
is why a TMEM-related bug is invisible under the emulator.

### The rules that bite

- Nothing snoops the CPU's caches. Every buffer shared with the GPU, the DSP or any DMA
  engine needs `DCFlushRange` after the CPU writes it and `DCInvalidateRange` before the CPU
  reads what an engine wrote. These work on 32-byte lines, so a shared buffer must be
  32-byte aligned and a multiple of 32 long, or flushing it corrupts whatever shares its
  line. (This is exactly how the AESND audio driver's buffers are sized.)
- `DCZeroRange` (`dcbz`) establishes cache lines as zero without reading memory first. For a
  buffer you are about to overwrite completely it is roughly twice as fast as `memset`.

## 2. Where WiiStation's memory goes

### MEM1 (24 MB)

From the debug ELF: 3.06 MB of code, 0.30 MB of initialised data and 8.93 MB of zeroed data,
so the binary alone occupies about 12.3 MB before a single allocation. The largest individual
items:

| Symbol | Size | What |
|---|---|---|
| `psxM_buf` | 2.13 MB | emulated PSX main RAM (2 MB) plus the parallel-port and scratchpad windows |
| `globalVram` | 2.00 MB | emulated PSX video RAM, shared by every GPU plugin |
| `GXtexture` | 2.00 MB | the GX texture pages the OpenGX plugin draws from |
| `psxMemRLUT` + `psxMemWLUT` | 0.50 MB | the 64K-entry read and write address lookup tables |
| `XABuf` | 0.38 MB | decoded XA audio |
| `texturepart` | 0.25 MB | the staging buffer the CLUT expansion writes into |
| `memb_mem_pool_pbufs` | 0.20 MB | lwIP network buffers |
| `semiTransBuf` | 0.13 MB | opengx's semi-transparent texture scratch |
| `g_perf` | 0.11 MB | the profiler's counters — **debug build only** |

Measured free at runtime, in a game: **5.2-5.6 MB**, consistently, across every title tested --
before 2026-09-30, when Lightrec's code buffer moved here from MEM2 (1.5 MB, `LIGHTREC_CODE_SIZE`):
an instruction-cache miss in compiled code is served faster from MEM1, and on the Wii every one of
eleven games ran 0.4-1.4 load points lower (mean 38.2 -> 37.5%). The code peaked at 1.0 MB in the
five recordings; a full buffer makes Lightrec interpret a block (`jit_full`), never crash. Since
then the lowest MEM1 free seen on the Wii is 1.67 MB in the debug build (perf.log `mem2map:`).
There is no MEM1 pressure today.

### MEM2

libogc's usable window is 51.4 MB. WiiStation reserves fixed regions at fixed addresses
(`Gamecube/MEM2.h`), and whatever is left becomes the general heap that `_mem2_malloc` hands
out:

| Region | Size | Notes |
|---|---|---|
| Memory card 1 / 2 | 0.25 MB | |
| HID controller buffer | 0.13 MB | |
| SPU buffer | 0.50 MB | |
| PSX BIOS image | 0.50 MB | read-mostly, cold under the default HLE |
| libogc arena | 1.84 MB | kept below the heap for libogc's own Arena2 allocations; 0 KB used on the Wii with networking |
| **reserved total** | **3.2 MB** | |
| general heap | **47.0 MB** | textures, disc buffers, the font's glyph heap, the old dynarec's code when `Core = 2` |
| gap under IOS's top | 1.16 MB | between the heap's end and the MEM2 top the loader reports (0x933E0000) |

Since 2026-09-30 (598aaa3) the 9 MB font region and the 10 MB old-dynarec region are no
longer fixed: the font takes a block sized to it (~430 KB for a Latin menu, up to 9 MB for a
full CJK font) and `Core = 2` takes its 10 MB when it starts. The heap went from 24.6 MB to
47 MB (43 MB, then 4 MB more when Lightrec's code buffer moved to MEM1, below). Measured on the Wii, five recordings in one boot: peak 22.8 MB (Crash 3's exit
transition fills every texture page), no failed allocation. Above 0x933E0000 is IOS's,
with its IPC buffer at 0x935E0000-0x93600000.

This is why the Memory page is a set of readouts and not a set of settings: every large
region above is reserved at a compile-time address, with the next region defined relative to
the end of the previous one, so there is nothing a runtime setting could move. Changing the
balance means changing that layout and rebuilding — which section 3 is about.

## 3. The 9 MB glyph cache

`Gamecube/libgui/IPLFont.cpp` builds the menu font by allocating **one 1152-byte RGB5A3 tile
per character** out of a dedicated heap covering the whole `CN_FONT_SIZE` region. 9 MB
divided by 1152 is 8192 glyphs, which is the giveaway: the region is sized for a full CJK
font, and it is reserved whether or not one is loaded.

What the shipped fonts actually need:

| Font | Glyphs | Heap used |
|---|---|---|
| `En.dat` (built in, English) | 95 | 106 KB |
| `Segoe`, `Times`, `CalibriBold` | 319 | 358 KB |
| a 3500-character common-hanzi set | 3500 | 3.9 MB |
| full GB2312 | 6763 | 7.6 MB |

So for a Latin menu, **about 8.6 MB of the 9 MB is reserved and never touched** — a third of
what is left for the general heap, for nothing. For a Chinese menu the reservation is
justified and 9 MB is about right.

The Memory page now shows this directly ("Font glyph cache: 106 / 9216 KB"), which is the
point of putting it there: the waste is visible instead of buried in a header.

**One real bug found and fixed while investigating.** The allocation had no failure check:

```c
u8 *tmpPngBuf = (u8*) __lwp_heap_allocate(GXtexCache, CHAR_IMG_SIZE);
memcpy(tmpPngBuf, ...);          /* tmpPngBuf is NULL when the heap is exhausted */
```

A font with more glyphs than the region holds would `memcpy` to address 0. It now stops
loading and keeps the glyphs read so far. This was latent rather than observed — no shipped
font comes close to 8192 glyphs — but it is exactly what a user supplying their own large
`chs.dat` would hit.

### What could be done, and the catch

Shrinking `CN_FONT_SIZE` moves every region defined after it (they chain: `CN_FONT_HI` ->
`RECMEM2_LO` -> ... -> `NEW_MEM2_LO`), so the difference lands in the general MEM2 heap. A
one-line change to `MEM2.h` would move ~8 MB from reserved-and-unused to available.

The catch is that it is a **compile-time** layout, so a smaller build simply cannot load a
CJK font at all: a user who selects the Chinese menu language and supplies `chs.dat` would
get a partial font (gracefully, now, rather than a crash). The honest options are:

1. **Leave it.** Neither MEM1 nor MEM2 is under pressure, so the 8 MB buys nothing today.
   This is the recommendation until something actually needs the memory.
2. **Shrink it to ~1 MB** and accept that CJK menus need a separate build. Simple, frees the
   most, loses a feature.
3. **Size the heap at runtime from the font file** — read the glyph count, take what is
   needed, and give the remainder to the general heap. This keeps CJK working and recovers
   the memory for everyone else. It means the MEM2 layout can no longer be a chain of
   compile-time constants, which is a real refactor of `MEM2.h` and `gx_init_mem2`.

The memory was needed (Crash 3 after FF7 ran the heap out, 2026-09-30), and option 3 is what
was done: the glyph heap is now a block of the general heap sized to the font (598aaa3).
