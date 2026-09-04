#ifndef _MEM2_MANAGER_H
#define _MEM2_MANAGER_H

#include <stdint.h>
#include <stddef.h>

bool gx_init_mem2(void);

uint32_t gx_mem2_used(void);

uint32_t gx_mem2_total(void);

void *_mem2_memalign(uint8_t align, uint32_t size);

void *_mem2_malloc(uint32_t size);

void _mem2_free(void *ptr);

void *_mem2_realloc(void *ptr, uint32_t newsize);

void *_mem2_calloc(uint32_t num, uint32_t size);

char *_mem2_strdup(const char *s);

char *_mem2_strndup(const char *s, size_t n);

#endif
