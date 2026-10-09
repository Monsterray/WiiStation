# What WiiXplorer NG's hardware research means for WiiStation

Source: `examples/wiixplorer-wii-hardware-research-0.1.14` (a WiiXplorer maintenance fork,
research snapshot 2026-10-08). Its numbers come from **the same bench Wii** (IOS58 rev 6175,
shared queue), verified by CRC, three samples each; raw CSVs under its `build/wii.*`. Read
its `MEMORY.md`, `STORAGE.md`, `TRANSFERS.md` and `HBC-NETWORKING.md` for method and caveats.
Their builds use libogc 3.1.0 (not WiiStation's pinned libogc2), so library-level findings
were rechecked here; CPU/memory/device numbers are hardware facts and carry over.

## The numbers that matter for an emulator

MiB/s, medians. "Reused" = the same buffer again (cache may hold it); "cold" = invalidated.

| Access (MEM1 unless said) | <= 32 KiB (L1) | 64-256 KiB (L2) | >= 512 KiB (past L2) |
|---|---:|---:|---:|
| sequential read, reused | ~925 | ~810 | ~370 |
| read one word per cache line or wider stride, reused | 650-925 | ~240 | **~57** |
| copy + CRC, cached output, reused | 160-200 | 105-160 | ~90 |
| copy MEM1 -> MEM2 + CRC, cached output, reused | 176-202 | 152-166 (128 KiB), 57 (256 KiB) | ~49 |
| copy MEM1 -> MEM2 + CRC, uncached (K1) output | ~65 | ~65 | ~56 |

| Other | MiB/s |
|---|---:|
| `memcpy` MEM1->MEM1 / MEM1->MEM2 / MEM2->MEM2 (256 KiB) | 202 / 102 / 67 |
| uncached (K1) reads, MEM1 / MEM2 | 71 / **20** |
| locked-cache DMA: MEM1->LC / LC->MEM1 / MEM2->LC / LC->MEM2 | 1349 / 1461 / **440** / 1466 |
| SD / USB sequential read (FAT32, 256 KiB requests) | ~7.1 / ~7.5 |
| plain TCP into the Wii, 4 KiB / 16 KiB+ IOS calls (hbc-reborn netblock, rerun here) | 0.80 / ~1.05 MB/s |

What follows from them:

1. **A pass whose working set outgrows the 256 KiB L2 is 2-3x slower**, and strided access
   past L2 is 6x slower than sequential. Count input + output + tables together.
2. **MEM1 is ~2-3x MEM2 for copies.** WiiStation already keeps every hot structure in MEM1
   (PSX RAM, VRAM, GX texture pages, the RLUT/WLUT, Lightrec code: `Docs/MEMORY_MAP.md`).
3. **Uncached output is not a win** for anything that fits L2 (65 vs 152-202 MiB/s); only past
   L2 is it level. Do not move texture/audio writes to K1 aliases on this evidence.
4. **Uncached reads are very expensive** (MEM2: 20 MiB/s): never read K1 memory in a loop.
5. **Locked-cache DMA should load from MEM1, not MEM2** (3x), and store anywhere.
6. **Storage is not a bottleneck**: a PS1 disc stream is ~0.3 MB/s against ~7 MiB/s; only
   latency (seeks) shows. The share is network-bound (`Gamecube/fileBrowser/smb2dev.c`).

## Checked against WiiStation (2026-10-08)

| Finding | WiiStation |
|---|---|
| libogc 3.1.0's LC DMA helpers strip the `0xE0000000` tag / MEM2 bit 28 and misread the queue length | **Not affected.** The pinned libogc2 build (2023-05-05) disassembles correctly: `LCLoadBlocks` keeps 29 address bits (`clrlwi r4,r4,3`), `LCQueueLength` is `(HID2 >> 24) & 15`. |
| HBC-Reborn 1.10.0 keeps netlog/crash/last-log records at MEM2 `0x91800000-0x91801140`; WiiXplorer reserves the gap | Inside WiiStation's general MEM2 heap, **by design harmless**: the agent writes them only as the app stops (`deps/hbc_agent/sdk/hbc_agent.h`). No change. |
| Non-blocking IOS sends can report bytes as sent that were not (hbc-reborn: 2048 reported, 2040 queued, near 4 KiB) | The share sends only ~100-byte SMB requests on a non-blocking socket (`deps/libsmb2/lib/compat.c`), far from a full send buffer. **Revisit if WiiStation ever writes bulk data to the share** (e.g. memory cards): send blocking, or poll `SO_SNDLOWAT` capacity first, as WiiXplorer does. |
| 16 KiB IOS receive calls (+30% plain TCP) | Done in 5.8.0. |
| Hot data in MEM1, sequential walks, dcbz for fills | Done (`Docs/MEMORY_MAP.md`; fills with dcbz 3-4x, VRAM transfers as words). |

## What to do with it, in order

1. **Fuse the CLUT -> tile texture upload** (`Docs/LOCKED_CACHE_PLAN.md` step 2). Today one
   upload runs through `texturepart` (256 KiB staging, MEM1) and a 128 KiB page: ~384 KiB, past
   L2, the regime measured at ~50-90 MiB/s instead of ~150-200. One pass per 4-row band keeps it
   in L1/L2, with or without the locked cache. Measure with `texk:` on the Wii.
2. **Measure the locked-cache regions that exist** (`LockedCache` bits, all off): `tex-tile`
   stores LC -> texture page at ~1.46 GB/s (pages are MEM1), the direction measured fast. A
   chain with the same game twice, `LockedCache=0` vs `=1`, on Spyro / Medievil / an FMV game.
3. **Keep large passes sequential.** The 1 MiB VRAM is past L2: a column-wise walk (vertical
   strides of 2 KiB) runs at the ~57 MiB/s strided rate, a row-wise one at ~370. Check the soft
   GPU's and the VRAM-transfer paths for column-major loops before tuning arithmetic.
4. **FMV upload** (`glInitMovieTextures`, 5-10% of busy time in FMV on the Wii): the 16-bit
   path tiles into `semiTransBuf` and then `memcpy`s up to 128 KiB into the page; tiling
   straight into the page saves a pass. The 24-bit path that tiles to RGB5A3 (not RGBA8) `memset`s up
   to 256 KiB a part before tiling over it: `DCZeroRange` (dcbz) is ~2x memset (`Docs/MEMORY_MAP.md`), or drop it where
   the tiler writes every block.
5. **Not worth doing on this evidence**: K1 (uncached) texture or audio writes; moving disc or
   share buffers between banks; FAT cache sizes; cIOS USB (`Docs/` cIOS notes in memory).

Their tooling worth borrowing: a debug-only memory benchmark that runs on request
(`source/Diagnostics/MemoryBench.cpp`), CSV rows checkpointed with `fsync` so a crash keeps
completed work, and HDMI capture of the real Wii's TV output (a UGREEN 15389 dongle on the
macOS workstation, `scripts/build-wii-capture.py`) -- this Windows PC has no capture device.
