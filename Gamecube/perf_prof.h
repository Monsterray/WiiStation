/* perf_prof.h - lightweight Wii (Broadway) performance counters.
 *
 * Phase 1 profiling infrastructure: cheap integer counters + microsecond
 * accumulators for CPU/JIT, RAM, GPU, storage and menu. All helpers are a
 * single integer op so they are safe in hot paths; timing uses libogc
 * gettick() (mftb, a few cycles) only around coarse regions (JIT slices,
 * presents, CD reads, menu frames), never per-vertex/per-sector-inner-loop.
 *
 * Counters are plain (non-atomic) globals: the emulated frame, GPU submit
 * and CD paths all run on the same thread; audio runs separately and never
 * touches these.
 *
 * Reporting: perf_present_tick() auto-appends a summary to
 * sd:/wiisxrx/perf.log every 1800 presented frames (~30 s at 60 fps) and
 * mirrors key lines to the on-screen DEBUG overlay when SHOW_DEBUG is on.
 * Method: run the same save-state 30-60 s per build and diff the log.
 */
#ifndef PERF_PROF_H
#define PERF_PROF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the per-event histogram below. Must be >= PSXINT_COUNT (r3000a.h);
 * checked at compile time in lightrec.c so this header needs no emulator
 * includes. */
#define PERF_IRQ_SLOTS 20

/* Records in the audio timeline ring (perf_audio_event). 3072 x 16 bytes. */
#define PERF_AEV_N 3072

/* Records in the pad timeline ring (perf_pad_event). 2048 x 16 bytes: one full sweep of
 * four stick axes is about 1030 records, so a smaller ring fills before the buttons. */
#define PERF_PEV_N 2048

/* A slice below this many PSX cycles does less work than the dispatch around
 * it costs, so these are counted separately as pure overhead. */
#define SLICE_TINY_CYCLES 64

/* The profiler exists only in builds compiled with -DPERF_PROF (the debug
 * Makefile and the matching libOpengx_prof.a). Release builds get no-op
 * macros and inline zero timers below, so every call site compiles away and
 * real hardware pays nothing for the instrumentation. */
#ifdef PERF_PROF

/* Sub-gates, so the expensive probe groups can be turned off without giving up the whole
 * profiler. Each defaults to on inside a PERF_PROF build, so an ordinary debug build is
 * unchanged; pass e.g. EXTRA_CFLAGS="-DPERF_PROF_TRACE=0" to scripts/build.sh to compile
 * one group out and get its cycles back.
 *
 * Cost, roughly, from the hottest down:
 *   PERF_PROF_TRACE  per-primitive tracing (perf_prim_trace, 14 call sites in the GX
 *                    plugin). Runs a scheduling check on EVERY primitive and can write
 *                    ptrace.log. By far the most expensive group; turn it off unless you
 *                    are actually reading a primitive trace.
 *   PERF_PROF_GPU    the per-draw and per-texture sample rings in GlesGpu (ogx_ud, ogx_mm,
 *                    ogx_eq, ogx_va, ogx_op). Several struct stores per primitive.
 *   PERF_PROF_CPU    per-slice timing in the CPU cores: two time-base reads per slice,
 *                    and there are millions of slices.
 *   PERF_PROF_IO     disc, SIO and pad counters. Cheap; here for completeness.
 *
 * Plain PERF_INC/PERF_ADD counters are a single increment and are never gated: they cost
 * about nothing and they are what most of the reports are built from. */
#ifndef PERF_PROF_TRACE
#define PERF_PROF_TRACE 1
#endif
#ifndef PERF_PROF_GPU
#define PERF_PROF_GPU 1
#endif
#ifndef PERF_PROF_CPU
#define PERF_PROF_CPU 1
#endif
#ifndef PERF_PROF_IO
#define PERF_PROF_IO 1
#endif
/* PERF_PROF_NETWAIT measures what the network thread's wait costs. Off by default,
 * because it spends two seconds before the menu opens. Turn it on with:
 *   PROBES="-DPERF_PROF_TRACE=0 -DPERF_PROF_GPU=0 -DPERF_PROF_NETWAIT=1" 
 *       bash scripts/build.sh debug
 * from the devkitPro shell. wsx.sh cannot pass this: it sets PROBES itself. */
#ifndef PERF_PROF_NETWAIT
#define PERF_PROF_NETWAIT 0
#endif
/* PERF_PROF_BIOS counts every HLE BIOS call by its number, so perf.log can name the ones a
 * game really uses. One increment per call and 2.5 KB of counters, so it is on by default;
 * turn it off with -DPERF_PROF_BIOS=0 to measure without it. */
#ifndef PERF_PROF_BIOS
#define PERF_PROF_BIOS 1
#endif
/* PERF_PROF_MDEC splits the video decode into its two halves, so a change to either can
 * be shown. Two time-base reads per macroblock; turn it off with -DPERF_PROF_MDEC=0. */
#ifndef PERF_PROF_MDEC
#define PERF_PROF_MDEC 1
#endif
/* PERF_PROF_SPU splits the sound mixer into its stages. Four time-base reads per channel
 * per do_channels() call, which is about 0.6% of a run, so it is off by default. Turn it
 * on with -DPERF_PROF_SPU=1 to decide where the SPU's time goes. */
#ifndef PERF_PROF_SPU
#define PERF_PROF_SPU 0
#endif
/* PERF_PROF_GTE times the geometry coprocessor and counts each of its 22 operations.
 * Every GTE instruction leaves the recompiled code for a C call (cop2_op in lightrec.c),
 * so one gate at that door covers the whole unit. Two time-base reads per operation, and
 * a 3D game issues hundreds of thousands a second, so it is off by default. Turn it on
 * with -DPERF_PROF_GTE=1 to find out what share of a run the geometry costs and which
 * operations to look at first. */
#ifndef PERF_PROF_GTE
#define PERF_PROF_GTE 0
#endif
/* PERF_PROF_GPUSPLIT breaks the GPU command loop up. PERF_PROF_CPU already says that
 * about two thirds of a run goes behind the GP0 and DMA2 registers -- the "hw_gpu_us"
 * field of the perf.log "inside:" line -- but that is one number covering everything a
 * display list asks for. This splits it into the image transfers into VRAM, the
 * off-screen software rasterizer, and the primitive functions, with the word-by-word
 * reading of the list itself as the remainder, so the four add up to the whole.
 *
 * Two time-base reads per primitive, and a busy game draws under ten thousand a second,
 * so it is cheap; it is off by default because it only means anything next to
 * PERF_PROF_CPU, which supplies the total it divides. */
#ifndef PERF_PROF_GPUSPLIT
#define PERF_PROF_GPUSPLIT 0
#endif
/* One timed region, charged to a g_perf field. Only defined when the gate is on, so a
 * use of it has to sit behind the same gate. */
#if PERF_PROF_GPUSPLIT
#define PERF_TIME(field, expr) do { \
	unsigned long long pt_t0_ = perf_now_ticks(); \
	expr; \
	g_perf.field += perf_now_ticks() - pt_t0_; \
} while (0)
#else
#define PERF_TIME(field, expr) do { expr; } while (0)
#endif
/* PERF_PROF_PMC reads Broadway's four performance counters over the whole run. It exists
 * for the locked-cache work, which cannot be judged in Dolphin at all: Dolphin implements
 * the locked-cache DMA as a synchronous copy and models no cache misses, so LC code runs
 * correctly there and tells you nothing about its speed. On hardware these counters are
 * what says whether locking something helped.
 *
 * Which event each counter follows is a build-time choice, because the 750CL has far more
 * events than are worth naming here and the one that matters changes with the question.
 * The selects live in MMCR0 (PMC1, PMC2) and MMCR1 (PMC3, PMC4); the defaults below count
 * processor cycles and instructions completed, which give instructions per cycle -- the
 * number that moves when stalls go away, whatever caused them. For a miss count, look up
 * the select in the 750CL manual and pass it here:
 *
 *   PROBES="-DPERF_PROF_PMC=1 -DPMC_MMCR0=0x...":  bash scripts/build.sh debug
 *
 * Verified encodings (they are also what Dolphin's interpreter implements, in
 * PowerPC.cpp UpdatePerformanceMonitor): PMC1 select 1 = processor cycles, PMC2 select 1
 * = processor cycles and 11 = loads and stores completed, PMC3 select 11 = FPU
 * instructions completed. PMC1 select 2 = instructions completed is in the manual but is
 * not implemented by Dolphin, so it reads 0 there and counts properly on a Wii.
 *
 * Measured under Dolphin with the defaults: pmc1 counts (1149497231 over a 120 s Spyro
 * run) and pmc2 reads 0, because Dolphin implements select 1 and not select 2. So a
 * Dolphin run gives cycles and nothing else; both count on a Wii. */
#ifndef PERF_PROF_PMC
#define PERF_PROF_PMC 0
#endif
/* PMC1 = processor cycles (select 1), PMC2 = instructions completed (select 2).
 * MMCR0: PMC1SELECT is bits 19..25, PMC2SELECT bits 26..31, counting from the MSB. */
#ifndef PMC_MMCR0
#define PMC_MMCR0 ((1 << 6) | 2)
#endif
#ifndef PMC_MMCR1
#define PMC_MMCR1 0
#endif

typedef struct {
	/* CPU / JIT (Wii adapter level, lightrec.c) */
	uint32_t jit_slices;          /* lightrec execute slices run */
	uint32_t jit_resets_full;     /* invalidate_all (cache flush) */
	uint32_t jit_resets_partial;  /* ranged invalidate */
	uint32_t jit_interp_fallbacks;/* slices run on Lightrec interpreter */
	uint32_t jit_hle;             /* unknown-op handled as PSX HLE call */
	uint32_t jit_exceptions;      /* syscall/break/RI taken from JIT */
	uint32_t int_slices;          /* interpreter execute entries */

	/* Slice cost breakdown, in RAW libogc timebase ticks -- converted to
	 * microseconds only at report time. ticks_to_microsecs() does a 64-bit
	 * divide, which has no hardware instruction on this 32-bit PPC and so
	 * costs ~100 cycles in a software routine; this path is entered ~32000
	 * times a second, so four conversions per slice would distort the very
	 * measurement they exist to take. Accumulate raw, divide once. */
	uint64_t cpu_ticks;           /* whole slice */
	uint64_t slice_sched_ticks;   /* gen_interupt: irq_test + reschedule */
	uint64_t slice_jit_ticks;     /* inside lightrec_execute */
	uint64_t slice_post_ticks;    /* transition back + exit-flag handling */

	/* Why the recompiler handed control back. NORMAL means it believes it
	 * used up the cycle budget; CHECK_INTERRUPT means a hardware access
	 * found an interrupt pending and bailed out. */
	uint32_t exit_normal;
	uint32_t exit_check_irq;

	/* Ground truth for speed. Everything above is time *inside* slices;
	 * wall_start_ticks anchors real elapsed time since perf_reset() so the
	 * report can state presents-per-second and slice-time-per-wall-second
	 * without inferring either from an assumed 60 Hz. vblanks counts
	 * emulated VBlanks: a 30 fps title presents on every other one, so
	 * presents alone under-report speed by 2x. */
	uint64_t wall_start_ticks;
	uint32_t vblanks;
	uint32_t psx_pal;             /* Config.PsxType at last VBlank: 0 NTSC, 1 PAL */

	/* Work that runs INSIDE a slice but is not emulation, so the phase
	 * buckets above mislabel it. Kept separate so they can be subtracted:
	 *  - limit: the FrameCap() spin in pl_frame_limit(), reached from the
	 *    VBlank rcnt callback, so it lands in slice_sched_ticks;
	 *  - spu: SPU_async() -- the whole audio mixer -- same path;
	 *  - hw: the hw_read/hw_write callbacks, i.e. all I/O emulation
	 *    including the DMA kick that rasterizes a display list; these run
	 *    inside lightrec_execute and so inflate slice_jit_ticks.
	 * jit_nested counts slices entered re-entrantly (HLE softCall); their
	 * time is already inside the outer slice, so the guard in lightrec.c
	 * keeps them out of the accumulators to avoid double counting. */
	uint64_t limit_ticks;
	uint32_t limit_calls;
	uint32_t limit_target;        /* dwFrameRateTicks being paced to, 10us units */
	uint64_t spu_ticks;
	uint32_t spu_calls;
	/* Subset of spu_*: the 44100 -> 48000 output conversion (dfsound/resample.c),
	 * so the cost of the SoundResampler modes can be read on its own. Zero on the
	 * DSP path in Hold mode, where the microcode does it. */
	uint64_t out_ticks;
	uint32_t out_calls;
	uint64_t hw_ticks;
	uint32_t hw_calls;
	/* Subset of hw_*: GPU data/status registers (0x1f801810) and the DMA2
	 * channel (0x1f8010a0), i.e. the accesses that rasterize -- a primitive
	 * on its final GP0 word, or a whole ordering table on the D2 kick.
	 * hw_ticks - hw_gpu_ticks is every other peripheral combined. */
	uint64_t hw_gpu_ticks;
	uint32_t hw_gpu_calls;
	uint32_t jit_nested;

	/* OpenGX texture-cache behaviour (GlesGpu/gpuTexture.c + deps/opengx),
	 * for the texture pop-in investigation: how hard the 64-page sub-texture
	 * pool is churning, and how often the opaque/semi-transparent layer
	 * split drops a draw outright. */
	uint32_t ogx_gc;          /* DoTexGarbageCollection calls (4 pages recycled each) */
	uint32_t ogx_sub_new;     /* sub-texture uploads, i.e. cache misses */
	uint32_t ogx_sub_hit;     /* sub-texture cache hits */
	uint32_t ogx_skip;        /* draws dropped: layer lacks the needed texel type */
	uint32_t ogx_unaligned;   /* glTexSubImage2D took the per-pixel (unaligned) path */
	uint32_t ogx_oob_upload;  /* sub-uploads that overhung the texture's own bounds */
	/* Skip sites split by site and by evidence. An OPAQUE draw dropped while
	 * its texture holds texels only in the semi layer is the signature of an
	 * entry converted under the wrong blend state -- a visible hole. */
	uint32_t ogx_skip_b1;        /* blended pass 1: no TEX_TYPE_1 texels */
	uint32_t ogx_skip_b2;        /* blended pass 2: no TEX_TYPE_2 texels */
	uint32_t ogx_skip_op_empty;  /* opaque draw: mask 0, nothing to draw (benign) */
	uint32_t ogx_skip_op_semi;   /* opaque draw: mask == TEX_TYPE_1 only (a hole) */
	/* Upload-time blend state vs the primitive's DrawSemiTrans, the value its
	 * cache key is built from. The texel split must follow the key. First
	 * eight disagreements are kept in detail. */
	uint32_t ogx_upl_semi;       /* uploads made for semi-transparent primitives */
	uint32_t ogx_upl_opaque;     /* uploads made for opaque primitives */
	uint32_t ogx_upl_mismatch;   /* GL blend state disagreed with DrawSemiTrans */
	struct { uint16_t w, h, x, y, dw, dh; uint8_t blend, semi; } ogx_mm[8];
	uint32_t ogx_mm_n;
	/* Primitives the plugin refused to draw at all because CheckCoord* judged
	 * them too large (the PSX GPU's 1024x512 limit). A check that is off by a
	 * pixel drops exactly the big close-up ground quads. */
	uint32_t ogx_coord_rej;
	/* Per-draw texture probe for the flat-polygon investigation: for every
	 * textured draw, a 4x4 grid of texels across the draw's UV bounding box is
	 * read straight from the texture's tiled RAM buffer -- exactly what GX will
	 * sample -- and the draw is counted as "uniform" if all 16 are identical.
	 * A uniform textured draw is a flat polygon by construction; the ring keeps
	 * which texture (and which PSX texture mode) produced the first eight. */
	uint32_t ogx_draw_ft, ogx_draw_gt;   /* textured draws: flat-lit / Gouraud */
	uint32_t ogx_uni_ft, ogx_uni_gt;     /* of which sampled uniform */
	uint32_t ogx_cur_mode;               /* PSX texture mode of the current sub-texture */
	uint32_t ogx_cur_clut, ogx_cur_page; /* raw CLUT id and PSX texture page of the same */
	/* VRAM-write invalidation: how often the game wrote VRAM under cached
	 * sub-textures, and how many entries the sweep actually dropped. Writes
	 * with no drops means stale conversions are served forever. */
	uint32_t ogx_vram_wr, ogx_inval;
	struct { uint16_t texid, mode, w, h, texel, clut, page; uint16_t u[4], v[4]; uint8_t semi, gt, nv; } ogx_ud[8];
	uint32_t ogx_ud_n;
	/* Render-to-texture probe: primitives whose destination is VRAM outside
	 * the visible buffers (the dormant bDrawOffscreen* test), i.e. draws a
	 * software rasterizer would have written into VRAM and this port drops,
	 * plus GP0 fills. The ring keeps the first eight destination rects. */
	uint32_t ogx_off_prim, ogx_off_prim_tex, ogx_fill_all, ogx_fill_off;
	/* off-screen primitives handed to the software rasterizer (gpuPlugin.c
	 * OffscreenSoftDraw), and pad-plugin activity: SIO start-polls, pad state
	 * refreshes and calls into the scripted-input mask */
	uint32_t off_soft_prims, off_soft_rejected, pad_startpoll, pad_update, ai_calls;
	/* Where already-drawn EFB content can be lost again: screen re-uploads from
	 * psxVuw (which has no GX-drawn primitives in it) and the present's clear. */
	uint32_t upl_calls, upl_done, pres_total, pres_clear, pres_skipped;
	/* SIO activity (sio.c): data writes, pad-select starts that reached the
	 * pad plugin, control-register writes, data reads, SIO interrupts */
	uint32_t sio_write8, sio_start, sio_ctrl16, sio_read8, sio_irq;
	struct { int16_t x0, y0, x1, y1; uint16_t color; uint8_t kind, tex; } ogx_op[8];
	uint32_t ogx_op_n;
	/* Region of interest = PSX texture pages 12/13 (VRAM x 768..895, y 0..255),
	 * where the flat grey textures live. off_roi: off-screen primitives whose
	 * destination touches it; va_*: VRAM areas invalidated (image loads,
	 * moves, CPU writes) in total and touching it. ogx_va ring: first eight
	 * such areas. */
	uint32_t ogx_off_roi, ogx_va_all, ogx_va_roi;
	struct { int16_t x0, y0, x1, y1; } ogx_va[8];
	uint32_t ogx_va_n;
	/* Texel-equivalence detector: per textured opaque draw, the texel the GX
	 * texture holds at the polygon's UV centroid vs the texel the PSX would
	 * sample from VRAM through the CLUT at the same spot (3x3 tolerance for
	 * rounding). Context is published by SelectSubTextureS. A mismatch means
	 * the draw is bound to the wrong texture content; 'hole' = expected
	 * opaque, got transparent. */
	uint16_t ogx_cur_tx, ogx_cur_ty, ogx_cur_semi, ogx_cur_twin;
	uint16_t ogx_cur_u[4], ogx_cur_v[4];
	uint32_t ogx_eq_n, ogx_eq_mism, ogx_eq_hole;
	struct { uint16_t texid, mode, page, clut, pu, pv, x, y, exp, got; uint8_t nv; } ogx_eq[8];
	uint32_t ogx_eq_r;
	/* Primitive-stream trace (see perf_prim_trace in perf_prof.c). */
	struct { uint16_t present; uint8_t cmd, flags, abr; uint32_t color; int16_t x0, y0, x1, y1; } pt[2600];   /* one full PSX frame */
	uint32_t pt_n, pt_armed, pt_start_present, pt_start_vblank, pt_printed;
	/* scripted-input events (autoinput.txt): vblank, mask, present index */
	struct { uint32_t vbl, present; uint16_t mask; } ai_ev[16];
	uint32_t ai_n;
	uint16_t pt_prims[16], pt_semi[16], pt_fills[16];
	uint32_t vram_dumped;                /* one-shot VRAM snapshot taken (see perf_prof.c) */

	/* Slice granularity: why the recompiler keeps exiting.
	 *
	 * A slice runs until the nearest pending PSX event, so the event that
	 * fires most often is the one setting the slice length -- and a slice
	 * shorter than a basic block is nearly all dispatch overhead (exit,
	 * irq_test, reschedule, re-enter) rather than emulation. slice_cycles
	 * / jit_slices gives the average slice in PSX cycles; compare against
	 * 564480 cycles per NTSC frame to see how many slices a frame costs.
	 *
	 * irq_fires[] is indexed by PSXINT_*. PERF_IRQ_SLOTS must be >=
	 * PSXINT_COUNT; lightrec.c static-asserts that, since this header is
	 * deliberately free of emulator headers. */
	uint32_t irq_fires[PERF_IRQ_SLOTS];
	uint64_t slice_cycles;        /* summed PSX-cycle budget over all slices */
	uint32_t slice_tiny;          /* slices shorter than SLICE_TINY_CYCLES */

	/* RAM (sampled at report; fails counted live) */
	uint32_t mem2_alloc_fails;    /* _mem2_memalign NULL returns */
	uint32_t mem2_peak_kb;        /* max sampled MEM2 used */
	/* PSX LUT health (Phase 3): RLUT-null reads = genuinely unmapped
	 * region touched (copy-protection probes, bad pointers). WLUT is
	 * intentionally NOT counted (PIO-drop + isolate protocol pollute
	 * it by design). */
	uint32_t mem_null_read;

	/* GPU. Cache hit/miss/batch counters come from deps/opengx gc_gl.c and so
	 * only move on the OpenGX (GlesGpu) plugin; the SoftGPU plugin does not go
	 * through that layer, and reports resets/loads/bytes/convert_us/drawdone
	 * from SoftGPU/drawGX.c instead. Present counters work on both. */
	uint32_t gx_tex_hits;         /* texture-cache hits (OpenGX only) */
	uint32_t gx_tex_misses;       /* texture-cache misses (OpenGX only) */
	uint32_t gx_tex_resets;       /* OpenGX: evicts; SoftGPU: tex re-inits */
	uint32_t gx_tex_loads;        /* OpenGX: uploads; SoftGPU: per-frame blits */
	uint64_t gx_tex_bytes;        /* uploaded/converted texture bytes */
	uint64_t gx_convert_us;       /* SoftGPU: CPU time converting PSX fb -> GX */
	uint32_t gx_drawdone;         /* blocking GX_DrawDone calls */
	uint64_t gx_drawdone_us;      /* time spent inside them */
	uint32_t gx_batches;          /* glDrawArrays batches submitted */
	uint32_t present_frames;      /* emulated frames presented */
	uint64_t present_us;          /* time inside present/flip */

	/* CPU texture and decode kernels, timed to size them as locked-cache
	 * candidates (perf.log "texk:" line). Ticks, converted at report time. */
	uint64_t ogx_conv_ticks;      /* LoadSubTexturePageSort: CLUT expand into texturepart */
	uint32_t ogx_conv_calls;
	uint64_t ogx_conv_texels;     /* texels expanded (dx*dy per call) */
	uint64_t ogx_tile_ticks;      /* opengx glTexSubImage2D: RGB5A3 4x4 tiling + flush */
	uint32_t ogx_tile_calls;
	uint64_t mdec_ticks;          /* psxDma1: MDEC IDCT + YUV->RGB for one DMA */
	uint32_t mdec_calls;
	/* Inside that: rl2blk (run-length decode, dequantise, IDCT) against the colour
	 * conversion. mdec_blocks counts macroblocks, mdec_dconly the ones whose IDCT was
	 * the DC-only short cut. */
	uint64_t mdec_rl_us, mdec_yuv_us;
	uint32_t mdec_blocks, mdec_dconly;

	/* Storage (cdriso.c) */
	uint32_t cd_reads;            /* sector-read calls (raw + CHD) */
	uint64_t cd_bytes;            /* bytes handed to caller */
	uint32_t cd_seq;              /* sequential (no re-seek) reads */
	uint32_t cd_rand;             /* reads that needed a seek */
	uint32_t cd_pf_hit;           /* sector served from the read-ahead ring */
	uint32_t cd_pf_miss;          /* ring did not have it (or prefetch off: not counted) */
	uint32_t cd_pf_reads;         /* sectors the read-ahead thread fetched */
	uint32_t chd_hit;             /* CHD hunk already resident */
	uint32_t chd_miss;            /* CHD hunk decompressed */
	uint32_t chd_err;             /* chd_read failures */
	uint64_t chd_us;              /* time inside chd_read */
	uint64_t io_total_us;         /* total sector-read time */
	uint32_t io_worst_us;         /* worst single sector-read (stall) */

	/* CD-XA streaming: cdrom.c -> decode_xa.c -> dfsound/xa.c FeedXA -> MixCD. A gap is a
	 * MixCD call that found the XA ring empty with the repeat count used up while a stream
	 * was active: the mixer then contributes silence. */
	uint32_t xa_sectors;          /* sectors decoded and queued (FeedXA calls) */
	uint32_t xa_starts;           /* stream starts (XA ring reset) */
	uint32_t xa_fed;              /* 44.1 kHz stereo samples written into the XA ring */
	uint32_t xa_trunc;            /* FeedXA stopped early: ring full */
	uint32_t xa_fill_min, xa_fill_max; /* ring fill (samples) at FeedXA entry */
	uint32_t xa_freq, xa_stereo;  /* format of the last stream start */
	uint32_t xa_dt_min, xa_dt_max;/* PSX cycles between consecutive sectors of a stream */
	uint32_t xa_filtered;         /* XA sectors dropped by the file/channel filter */
	uint32_t xa_mix;              /* MixCD calls that consumed XA data */
	uint32_t xa_hold;             /* MixCD calls that repeated the last value: ring empty */
	uint32_t xa_gaps;             /* silence episodes while a stream was active */
	uint32_t xa_gap_calls, xa_gap_samples;
	uint32_t spu_ns;              /* mixer samples produced (44.1 kHz) */
	uint32_t spu_pulls;           /* tempo pull-backs: output driver not busy, SoundTempo on */
	uint32_t spu_busy;            /* SPU_async calls that found the output driver busy */
	uint32_t spu_desync;          /* do_samples resets: |cycle gap| > 2M cycles */
	/* Where the mixer's time goes (PERF_PROF_SPU). `chans` counts channel-iterations, not
	 * channels: one per audible channel per do_channels() call. */
	uint64_t spu_adpcm_us;        /* ADPCM decode and interpolation, or the noise source */
	uint64_t spu_adsr_us;         /* the envelope */
	uint64_t spu_mix_us;          /* summing a channel into the output, dry or to reverb */
	uint64_t spu_rvb_us;          /* the reverb itself, once per call */
	uint32_t spu_chans;

	/* The geometry coprocessor (PERF_PROF_GTE). `gte_calls` is indexed by the CP2
	 * function field, so the 22 real operations sit at their own opcode numbers.
	 * Ticks, converted at report time: a 3D game issues hundreds of thousands of
	 * these a second, and perf_now_us() would charge each one a 64-bit divide. */
	uint64_t gte_ticks;

	/* The GPU command loop, split (PERF_PROF_GPUSPLIT). Ticks, converted at report
	 * time. `parse` is whatever is left of GL_GPUwriteDataMem once the other three
	 * are taken out of it. */
	uint64_t gpu_vram_ticks, gpu_off_ticks, gpu_prim_ticks, gpu_parse_ticks;
	uint32_t gpu_vram_words, gpu_off_calls, gpu_prim_calls;
	/* The primitive time again, by class of GP0 command: index is the top three bits
	 * of the command byte, so 1 is the polygons, 2 the lines, 3 the sprites, and 7 the
	 * drawing-state commands (E1..E6). */
	uint64_t gpu_cls_ticks[8];
	uint32_t gpu_cls_calls[8];
	/* Inside the two expensive ones: the GP0 02 fill split into its GX half, its
	 * software half and the write record, and the image transfer split into the pixel
	 * loop against what FinishedVRAMWrite does afterwards. */
	uint64_t gpu_fill_gx_ticks, gpu_fill_sw_ticks, gpu_fill_mark_ticks;
	uint64_t gpu_vramfin_ticks;
	uint32_t gpu_vramfin_calls;
	uint32_t gte_calls[64];

	/* Broadway's performance counters, read once at the end of a run (PERF_PROF_PMC). */
	uint32_t pmc[4];
	uint32_t out_dry;             /* output frames the driver had no data for (SDL: zero-filled; AESND: silence) */
	uint32_t out_drop;            /* feed() calls that found the driver full and dropped the rest */

	/* Output-stage rate control (dfsound/ratectl.c): the nudge applied to the playback
	 * rate, in ppm, and the queue occupancy it was computed from, in 44.1 kHz frames. */
	int32_t  rate_ppm_now, rate_ppm_min, rate_ppm_max;
	int32_t  rate_i_now;          /* the integral term's share of rate_ppm_now */
	uint32_t rate_occ_min, rate_occ_max;
	uint64_t rate_occ_sum;
	uint32_t rate_updates;        /* ratectl_update calls */
	uint32_t rate_sat;            /* updates whose demand exceeded +-RATECTL_MAX_PPM */
	uint32_t rate_changes;        /* AESND voice frequency writes (DSP path) */
	uint32_t rate_prefill;        /* output frames of silence before the queue first reached its target */
	uint32_t limit_debt_max;      /* FrameCap: largest debt carried, 100 us ticks */
	uint32_t limit_debt_drops;    /* FrameCap: stalls longer than the debt bound, remainder dropped */

	/* Audio timeline: one record per XA sector (F, R = stream start), gap start (G), first
	 * repeated sample (H) and a mixer heartbeat every 16th SPU_async (S). Recording starts at
	 * autoinput's 'atrace <vblank>' (or the first event without one), fills once, and is
	 * written to sd:/wiisxrx/atrace.log; scripts/atrace_summary.py reads it. */
	struct { uint32_t wall_us, cycle; int32_t a, b; uint16_t c; uint8_t kind; } aev[PERF_AEV_N];
	uint32_t aev_n;
	uint8_t  aev_on, aev_printed;

	/* Pad timeline: one record each time what a virtual port hands the PlayStation
	 * changes -- buttons or either stick. Both ends of the conversion are kept, so the
	 * driver's output and the bytes the game finally reads can be compared against the
	 * input Dolphin was told to send. Written to sd:/wiisxrx/padtrace.csv on every perf
	 * report; scripts/padtest.py generates the input and checks the result. */
	struct {
		uint32_t vbl;
		uint16_t drv_btns, out_btns;
		uint8_t  pad, type, drv_lx, drv_ly, drv_rx, drv_ry;
		uint8_t  out_lx, out_ly, out_rx, out_ry;
	} pev[PERF_PEV_N];
	uint32_t pev_n;

	/* Saves. A save state writes about 2.8 MB through gzip and a memory card 128 KB, both
	 * on the main thread with the picture frozen, so their wall time is what a player
	 * feels. `fail` counts the ones that wrote nothing. */
	uint32_t state_saves, state_loads, state_fails;
	uint64_t state_save_us, state_load_us;
	uint32_t state_bytes;          /* the last state file, uncompressed */
	uint32_t mcd_saves, mcd_fails;
	uint64_t mcd_save_us;

	/* HLE BIOS calls by table and number (psxbios.c). `bios_custom` counts the ones a game
	 * redirected to its own code, which HLE has to run through softCall() instead. */
	uint32_t bios_a[256], bios_b[256], bios_c[256];
	uint32_t bios_custom;
	/* Wall time inside the whole HLE dispatch, from the JIT exit to the JIT re-entry:
	 * the register sync out, the BIOS function, and the sync back. Compare it with
	 * cpu_us to see what the HLE path really costs. */
	uint64_t bios_us;
	/* Of that, the part spent in the exception and boot hooks rather than in an
	 * A0/B0/C0 table call. hleop 1,2,3 and 24,25,26 are the tables; the rest are
	 * psxBiosException and the hleExc* handlers. */
	uint64_t bios_exc_us;
	uint32_t bios_exc_calls;

	/* Menu (Gamecube/libgui) */
	uint32_t menu_frames;         /* Gui::draw calls */
	uint64_t menu_us;             /* time inside Gui::draw */
	uint32_t menu_strings;        /* drawString calls */
	uint32_t menu_glyphs;         /* glyphs rastered */
	uint32_t menu_texloads;       /* font texture loads */
} perf_counters_t;

extern perf_counters_t g_perf;

#define PERF_INC(f)    (g_perf.f++)
#define PERF_ADD(f, n) (g_perf.f += (n))
#define PERF_SET(f, v) (g_perf.f = (v))

/* Append one record to the audio timeline (see aev[] above). cycle is the PSX cycle the
 * event belongs to; a, b, c are event-specific and documented in perf_prof.c. */
void perf_audio_event(unsigned kind, unsigned cycle, int a, int b, unsigned c);

/* Microsecond timestamp (libogc ticks). Declared here, defined in
 * perf_prof.c so this header stays dependency-free (no gccore.h). */
unsigned long long perf_now_us(void);

/* What a one-second wait really takes, measured once at start-up: the part over 1000000us
 * is what the wakeups cost. `old` is the shape the network thread used before -- usleep(100)
 * ten thousand times -- and `new` is the shape it uses now. These sit outside g_perf
 * because perf_reset() wipes it at every go(). */
extern unsigned long long g_netwait_old_us, g_netwait_new_us;
extern unsigned long g_netwait_old_wakes, g_netwait_new_wakes;

/* Raw timebase ticks, no unit conversion -- for hot paths that accumulate and
 * convert once at report time. See the cpu_ticks comment above. */
unsigned long long perf_now_ticks(void);

/* Zero all counters (called when a game session starts). */
void perf_reset(void);

/* Write one summary block to sd:/wiisxrx/perf.log (+ DEBUG overlay /
 * SysPrintf when available). */
void perf_report(void);

/* Call once per presented emulated frame with the flip cost in us.
 * Accumulates present time and auto-reports ~every 30 s. */
void perf_present_tick(unsigned long long present_us);
#if PERF_PROF_TRACE
void perf_prim_trace(unsigned cmd, unsigned flags, unsigned abr, unsigned color, int x0, int y0, int x1, int y1);
#else
static inline void perf_prim_trace(unsigned cmd, unsigned flags, unsigned abr, unsigned color, int x0, int y0, int x1, int y1)
{ (void)cmd; (void)flags; (void)abr; (void)color; (void)x0; (void)y0; (void)x1; (void)y1; }
#endif
void perf_autoinput_event(unsigned vblank, unsigned mask);

/* One pad record (see pev[] above). `type` is the driver's identifier letter, or 0 for a
 * port with nothing assigned. Sticks are packed x << 8 | y. Repeats are dropped, so a
 * stick held still costs one record, not one per poll. */
void perf_pad_event(unsigned pad, unsigned type, unsigned drv_btns, unsigned drv_l,
                    unsigned drv_r, unsigned out_btns, unsigned out_l, unsigned out_r);

#else /* !PERF_PROF: everything is a no-op */

#define PERF_PROF_NETWAIT 0
#define PERF_PROF_BIOS 0
#define PERF_PROF_MDEC 0
#define PERF_PROF_SPU 0
#define PERF_PROF_PMC 0
#define PERF_PROF_GTE 0
#define PERF_PROF_GPUSPLIT 0
#define PERF_TIME(field, expr) do { expr; } while (0)

#define PERF_PROF_TRACE 0
#define PERF_PROF_GPU   0
#define PERF_PROF_CPU   0
#define PERF_PROF_IO    0

#define PERF_INC(f)    ((void)0)
#define PERF_ADD(f, n) ((void)(n))
#define PERF_SET(f, v) ((void)(v))
#define perf_audio_event(k, cy, a, b, c) ((void)0)
static inline unsigned long long perf_now_us(void) { return 0; }
static inline unsigned long long perf_now_ticks(void) { return 0; }
static inline void perf_reset(void) {}
static inline void perf_report(void) {}
static inline void perf_present_tick(unsigned long long present_us) { (void)present_us; }
static inline void perf_prim_trace(unsigned cmd, unsigned flags, unsigned abr, unsigned color, int x0, int y0, int x1, int y1)
{ (void)cmd; (void)flags; (void)abr; (void)color; (void)x0; (void)y0; (void)x1; (void)y1; }
static inline void perf_autoinput_event(unsigned vblank, unsigned mask) { (void)vblank; (void)mask; }
static inline void perf_pad_event(unsigned pad, unsigned type, unsigned drv_btns, unsigned drv_l,
                                  unsigned drv_r, unsigned out_btns, unsigned out_l, unsigned out_r)
{ (void)pad; (void)type; (void)drv_btns; (void)drv_l; (void)drv_r; (void)out_btns; (void)out_l; (void)out_r; }

#endif /* PERF_PROF */

#ifdef __cplusplus
}
#endif

#endif /* PERF_PROF_H */
