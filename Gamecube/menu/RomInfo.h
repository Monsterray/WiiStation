/* The loaded game's information, one list for every place that shows it: the ISO Info page
 * (Current ISO > Show ISO Info, an engine page of readouts in OptionsFrame.cpp) and the
 * message after a game loads (FileBrowserFrame.cpp). The rows are MENU_PLAN 1.4 type F (F1):
 * Game, Disc, Image, System, Memory cards, Settings, Auto fixes, Achievements.
 *
 * HOW TO ADD OR CHANGE A ROW: one line in ROM_INFO_ROWS below (its label and its function),
 * and the function in RomInfo.cpp. A value is at most 31 characters (a readout's width,
 * PageView::VALUE_LEN); the ISO Info page holds 8 rows (eng::MAX_ROWS). */
#ifndef ROMINFO_H
#define ROMINFO_H

void romInfoGame(char *buf, int len);           /* the image's file name without its extension */
void romInfoDisc(char *buf, int len);           /* "SCUS-94228, label SPYRO"; "SLUS-00892, disc 1 of 4" */
void romInfoImage(char *buf, int len);          /* "CUE, 1 track, 630 MB" */
const char *romInfoSerial(void);                /* CdromId ("SCUS94228", the cover pack's form), or "" when there is none */
void romInfoSystem(char *buf, int len);         /* "NTSC, 60 Hz, HLE BIOS"; "PAL, forced to NTSC, HLE BIOS" */
void romInfoCards(char *buf, int len);          /* "Per game (SCUS-94228)", "Shared", "Off" */
void romInfoSettings(char *buf, int len);       /* "Global" or "This Game (SCUS94228.ini)" */
void romInfoFixes(char *buf, int len);          /* the automatic fixes for this game */
void romInfoAchievements(char *buf, int len);   /* what achievements do here (achievements_status) */

#define ROM_INFO_ROWS(X) \
	X("Game",          romInfoGame) \
	X("Disc",          romInfoDisc) \
	X("Image",         romInfoImage) \
	X("System",        romInfoSystem) \
	X("Memory cards",  romInfoCards) \
	X("Settings",      romInfoSettings) \
	X("Auto fixes",    romInfoFixes) \
	X("Achievements",  romInfoAchievements)

/* All rows as "Label: value" lines, appended to text (the message after a load) */
void romInfoText(char *text, int size);

#endif
