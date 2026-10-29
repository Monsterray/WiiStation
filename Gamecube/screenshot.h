/**
 * WiiStation - screenshot.h
 *
 * Saves the picture on screen as a PNG file (screenshot.c).
 **/

#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Saves the picture now. name (when not NULL) gets the file's name. Returns 0, or -1 on failure. */
int screenshot_save(char *name, unsigned size);

/* For the unlock pictures (ach_shot.c): the picture as PNG scanlines (MEM2, free with _mem2_free), and
 * the PNG file made around a deflate stream that sits SHOT_PNG_HEAD bytes into buf (see screenshot.c). */
#define SHOT_PNG_HEAD 41
#define SHOT_PNG_TAIL 16
unsigned char *screenshot_grab(unsigned *w, unsigned *h);
unsigned long screenshot_png_seal(unsigned char *buf, unsigned long idat_len, unsigned w, unsigned h);

/* Saves a picture that a button combination asked for. Called at each vblank. */
void screenshot_vblank(void);

#ifdef __cplusplus
}
#endif

#endif
