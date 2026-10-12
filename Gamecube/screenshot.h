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

/* Saves a picture that a button combination asked for. Called at each vblank. */
void screenshot_vblank(void);

#ifdef __cplusplus
}
#endif

#endif
