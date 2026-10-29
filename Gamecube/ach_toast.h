/* The achievement toast: the card that slides in when an achievement unlocks (ach_toast.c).
 * The design and its tiers: Docs/design/PS1_STYLES.md. */
#ifndef ACH_TOAST_H
#define ACH_TOAST_H

#include "../achievements.h"

#ifdef __cplusplus
extern "C" {
#endif

void ach_toast_push(const achievement_notice_t *notice);   /* achievements_notify points here */
void ach_toast_clear(void);   /* a new game: drop what is still queued */
void ach_toast_menu_opened(void);   /* the game stopped for the menu: remember the achievement of the pop-up on screen */
unsigned ach_toast_take_jump(void); /* its id (once), 0 when no pop-up was on screen: the menu opens the book on it */
int ach_toast_wanted(void);   /* a toast is on screen (the overlay must be drawn) */
int ach_toast_animating(void);   /* the card or the progress panel is on screen: it moves, so a vblank with no game present still draws it */
void ach_toast_draw(void);    /* in the present's overlay; sets the whole-picture viewport */

#ifdef __cplusplus
}
#endif
#endif
