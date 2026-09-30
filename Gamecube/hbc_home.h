/* hbc_home.h -- the Homebrew Channel's in-app agent in WiiStation (hbc_home.c) */
#ifndef HBC_HOME_H
#define HBC_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

void hbc_home_start(void);         /* once, after lab mode and before the SMB network thread */
void hbc_home_retrace(void);       /* the post-retrace callback */
int hbc_home_menu_frame(void);     /* each menu frame: 1 = close the game and leave */
void hbc_home_net_wait(void);      /* before WiiStation's own net_init / if_config */

#ifdef __cplusplus
}
#endif

#endif
