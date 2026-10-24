// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Copyright (C) 2019-2021 Paul Cercueil <paul@crapouillou.net>
 */

#include "lightrec-config.h"
#include "lightrec-private.h"
#include "memmanager.h"

#include <stdlib.h>

#if ENABLE_THREADED_COMPILER
#include <stdatomic.h>

static atomic_uint lightrec_bytes[MEM_TYPE_END];

void lightrec_register(enum mem_type type, unsigned int len)
{
	atomic_fetch_add(&lightrec_bytes[type], len);
}

void lightrec_unregister(enum mem_type type, unsigned int len)
{
	atomic_fetch_sub(&lightrec_bytes[type], len);
}

unsigned int lightrec_get_mem_usage(enum mem_type type)
{
	return atomic_load(&lightrec_bytes[type]);
}

#else /* ENABLE_THREADED_COMPILER */

/* WiiStation: atomic, as with the threaded compiler -- jitq's thread allocates too */
static unsigned int lightrec_bytes[MEM_TYPE_END];

void lightrec_register(enum mem_type type, unsigned int len)
{
	__atomic_fetch_add(&lightrec_bytes[type], len, __ATOMIC_RELAXED);
}

void lightrec_unregister(enum mem_type type, unsigned int len)
{
	__atomic_fetch_sub(&lightrec_bytes[type], len, __ATOMIC_RELAXED);
}

unsigned int lightrec_get_mem_usage(enum mem_type type)
{
	return __atomic_load_n(&lightrec_bytes[type], __ATOMIC_RELAXED);
}
#endif /* ENABLE_THREADED_COMPILER */

/* WiiStation: where Lightrec's own memory comes from (the blocks, the IR, the MIPS copies,
 * the jitq jobs): the host's allocator (WiiStation's lightrec.c: MEM2, which locks itself).
 * Only the state with its code LUT (lightrec_alloc_state) stays on malloc, in MEM1. With all
 * of it in MEM1, CTR after another game in one boot took MEM1 to 0 bytes and Lightning wrote
 * through a NULL allocation (bench Wii, 2026-10-24). lightrec_bound_memory() keeps the total
 * under LIGHTREC_DYN_MAX. */
void *lightrec_host_malloc(size_t len);
void *lightrec_host_calloc(size_t len);
void lightrec_host_free(void *ptr);

unsigned int lightrec_get_total_mem_usage(void)
{
	unsigned int i, count;

	for (i = 0, count = 0; i < MEM_TYPE_END; i++)
		count += lightrec_get_mem_usage((enum mem_type)i);

	return count;
}

void * lightrec_malloc(struct lightrec_state *state,
		       enum mem_type type, unsigned int len)
{
	void *ptr;

	ptr = lightrec_host_malloc(len);
	if (!ptr)
		return NULL;

	lightrec_register(type, len);

	return ptr;
}

void * lightrec_calloc(struct lightrec_state *state,
		       enum mem_type type, unsigned int len)
{
	void *ptr;

	ptr = lightrec_host_calloc(len);
	if (!ptr)
		return NULL;

	lightrec_register(type, len);

	return ptr;
}

void lightrec_free(struct lightrec_state *state,
		   enum mem_type type, unsigned int len, void *ptr)
{
	lightrec_unregister(type, len);
	lightrec_host_free(ptr);
}

float lightrec_get_average_ipi(void)
{
	unsigned int code_mem = lightrec_get_mem_usage(MEM_FOR_CODE);
	unsigned int native_mem = lightrec_get_mem_usage(MEM_FOR_MIPS_CODE);

	return native_mem ? (float)code_mem / (float)native_mem : 0.0f;
}
