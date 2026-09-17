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

typedef struct {
	/* CPU / JIT (Wii adapter level, lightrec.c) */
	uint32_t jit_slices;          /* lightrec execute slices run */
	uint32_t jit_resets_full;     /* invalidate_all (cache flush) */
	uint32_t jit_resets_partial;  /* ranged invalidate */
	uint32_t jit_interp_fallbacks;/* slices run on Lightrec interpreter */
	uint32_t jit_hle;             /* unknown-op handled as PSX HLE call */
	uint32_t jit_exceptions;      /* syscall/break/RI taken from JIT */
	uint32_t int_slices;          /* interpreter execute entries */
	uint64_t cpu_us;              /* accumulated JIT slice time */

	/* RAM (sampled at report; fails counted live) */
	uint32_t mem2_alloc_fails;    /* _mem2_memalign NULL returns */
	uint32_t mem2_peak_kb;        /* max sampled MEM2 used */

	/* GPU (deps/opengx gc_gl.c + present paths) */
	uint32_t gx_tex_hits;         /* texture-cache hits */
	uint32_t gx_tex_misses;       /* texture-cache misses */
	uint32_t gx_tex_resets;       /* full "8 full -> discard all" evicts */
	uint32_t gx_tex_loads;        /* glTexImage2D uploads */
	uint64_t gx_tex_bytes;        /* uploaded texture bytes */
	uint32_t gx_drawdone;         /* blocking GX_DrawDone calls */
	uint64_t gx_drawdone_us;      /* time spent inside them */
	uint32_t gx_batches;          /* glDrawArrays batches submitted */
	uint32_t present_frames;      /* emulated frames presented */
	uint64_t present_us;          /* time inside present/flip */

	/* Storage (cdriso.c) */
	uint32_t cd_reads;            /* sector-read calls (raw + CHD) */
	uint64_t cd_bytes;            /* bytes handed to caller */
	uint32_t cd_seq;              /* sequential (no re-seek) reads */
	uint32_t cd_rand;             /* reads that needed a seek */
	uint32_t chd_hit;             /* CHD hunk already resident */
	uint32_t chd_miss;            /* CHD hunk decompressed */
	uint32_t chd_err;             /* chd_read failures */
	uint64_t chd_us;              /* time inside chd_read */
	uint64_t io_total_us;         /* total sector-read time */
	uint32_t io_worst_us;         /* worst single sector-read (stall) */

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

/* Microsecond timestamp (libogc ticks). Declared here, defined in
 * perf_prof.c so this header stays dependency-free (no gccore.h). */
unsigned long long perf_now_us(void);

/* Zero all counters (called when a game session starts). */
void perf_reset(void);

/* Write one summary block to sd:/wiisxrx/perf.log (+ DEBUG overlay /
 * SysPrintf when available). */
void perf_report(void);

/* Call once per presented emulated frame with the flip cost in us.
 * Accumulates present time and auto-reports ~every 30 s. */
void perf_present_tick(unsigned long long present_us);

#ifdef __cplusplus
}
#endif

#endif /* PERF_PROF_H */
