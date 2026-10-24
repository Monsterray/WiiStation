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
int ach_toast_wanted(void);   /* a toast is on screen (the overlay must be drawn) */
void ach_toast_draw(void);    /* in the present's overlay; sets the whole-picture viewport */

#ifdef __cplusplus
}
#endif
#endif
