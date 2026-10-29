/* One unlock picture as a GX texture (ach_picture.h). The PNG is read into memory, decoded with
 * libpng's simplified API into a temporary RGBA buffer and boxed down to 256 x 192 RGB5A3
 * (ach_badge_conv.h, the badges' conversion). The file, the RGBA buffer and the libpng
 * allocations are freed again at once; only the 98 KB texture stays, in MEM2, until the next
 * picture or ach_picture_free(). MEM1 is the emulator's: nothing here is kept in it. */
#ifdef HW_RVL
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include "ach_picture.h"
#include "ach_badge_conv.h"
#include "../mem2_manager.h"

#define MAX_FILE (1024 * 1024)   /* a bigger file is not one of ours */
#define MAX_SIDE 1024

static u8 *tex;

void ach_picture_free(void)
{
	if (tex) _mem2_free(tex);
	tex = NULL;
}

int ach_picture_load(const char *path, GXTexObj *out)
{
	static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	FILE *f;
	long n;
	u8 *file = NULL, *rgba;
	png_image im;
	ach_picture_free();
	if (!(f = fopen(path, "rb"))) return 0;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 8 && n <= MAX_FILE && fseek(f, 0, SEEK_SET) == 0 &&
	    (file = _mem2_malloc((size_t)n))) {
		if (fread(file, 1, (size_t)n, f) != (size_t)n) {
			_mem2_free(file);
			file = NULL;
		}
	}
	fclose(f);
	if (!file) return 0;
	if (memcmp(file, sig, sizeof sig)) { _mem2_free(file); return 0; }
	memset(&im, 0, sizeof im);
	im.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&im, file, (size_t)n) || !im.width || !im.height ||
	    im.width > MAX_SIDE || im.height > MAX_SIDE) {
		png_image_free(&im);
		_mem2_free(file);
		return 0;
	}
	im.format = PNG_FORMAT_RGBA;
	rgba = _mem2_malloc(PNG_IMAGE_SIZE(im));
	tex = _mem2_memalign(32, ACH_PICTURE_W * ACH_PICTURE_H * 2);
	if (!rgba || !tex || !png_image_finish_read(&im, NULL, rgba, 0, NULL)) {
		png_image_free(&im);
		if (rgba) _mem2_free(rgba);
		_mem2_free(file);
		ach_picture_free();
		return 0;
	}
	png_image_free(&im);
	_mem2_free(file);
	ach_rgba_to_rgb5a3_box(rgba, im.width, im.height, (uint16_t *)tex, ACH_PICTURE_W, ACH_PICTURE_H);
	_mem2_free(rgba);
	DCFlushRange(tex, ACH_PICTURE_W * ACH_PICTURE_H * 2);
	GX_InitTexObj(out, tex, ACH_PICTURE_W, ACH_PICTURE_H, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(out, GX_LINEAR, GX_LINEAR);
	return 1;
}
#else
#include "ach_picture.h"
int ach_picture_load(const char *path, GXTexObj *out) { (void)path; (void)out; return 0; }
void ach_picture_free(void) {}
#endif
