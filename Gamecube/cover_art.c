/* cover_art.h */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cover_art.h"
#include "ach_badge_conv.h"

#ifdef HW_RVL
#include "../mem2_manager.h"
#define CA_MALLOC(n)		_mem2_malloc((uint32_t)(n))
#define CA_REALLOC(p, n)	_mem2_realloc((p), (uint32_t)(n))
#define CA_FREE(p)		_mem2_free(p)
#else
#define CA_MALLOC(n)		malloc(n)
#define CA_REALLOC(p, n)	realloc((p), (n))
#define CA_FREE(p)		free(p)
#endif

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_MALLOC(n)		CA_MALLOC(n)
#define STBI_REALLOC(p, n)	CA_REALLOC(p, n)
#define STBI_FREE(p)		CA_FREE(p)
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../deps/stb/stb_image.h"

#define MAX_SIDE 4096
#define MAX_OUT 512

int cover_art_decode(const uint8_t *file, size_t n, uint16_t *out, unsigned ow, unsigned oh)
{
	int w, h, comp;
	uint8_t *rgba;
	if (!file || !out || n < 16 || n > 0x7fffffffu || !ow || !oh || (ow & 3) || (oh & 3) || ow > MAX_OUT || oh > MAX_OUT)
		return 0;
	if (!stbi_info_from_memory(file, (int)n, &w, &h, &comp) || w < 1 || h < 1 || w > MAX_SIDE || h > MAX_SIDE)
		return 0;
	rgba = stbi_load_from_memory(file, (int)n, &w, &h, &comp, 4);
	if (!rgba)
		return 0;
	ach_rgba_to_rgb5a3_box(rgba, (unsigned)w, (unsigned)h, out, ow, oh);
	stbi_image_free(rgba);
	return 1;
}

#ifdef HW_RVL
#define MAX_FILE (4 * 1024 * 1024)	/* a bigger file is not a cover of ours */

static uint8_t *tex;

void cover_art_free(void)
{
	if (tex) _mem2_free(tex);
	tex = NULL;
}

int cover_art_load(const char *path, GXTexObj *out, unsigned ow, unsigned oh)
{
	FILE *f;
	long n;
	uint8_t *file = NULL;
	int ok;
	cover_art_free();
	if (!(f = fopen(path, "rb"))) return 0;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 16 && n <= MAX_FILE && fseek(f, 0, SEEK_SET) == 0 &&
	    (file = _mem2_malloc((uint32_t)n))) {
		if (fread(file, 1, (size_t)n, f) != (size_t)n) {
			_mem2_free(file);
			file = NULL;
		}
	}
	fclose(f);
	if (!file) return 0;
	tex = _mem2_memalign(32, ow * oh * 2);
	ok = tex && cover_art_decode(file, (size_t)n, (uint16_t *)tex, ow, oh);
	_mem2_free(file);
	if (!ok) {
		cover_art_free();
		return 0;
	}
	DCFlushRange(tex, ow * oh * 2);
	GX_InitTexObj(out, tex, ow, oh, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(out, GX_LINEAR, GX_LINEAR);
	return 1;
}
#endif
