/* The loaded game's information, one list for every place that shows it: the ISO Info page
 * (Current ISO > Show ISO Info, an engine page of readouts in OptionsFrame.cpp) and the
 * message after a game loads (FileBrowserFrame.cpp).
 *
 * HOW TO ADD OR CHANGE A ROW: one line in ROM_INFO_ROWS below (its label and its function),
 * and the function in RomInfo.cpp. A value is at most 31 characters (a readout's width,
 * OptionsFrame VALUE_LEN); the ISO Info page holds 8 rows (OPT_MAX_ROWS). */
#ifndef ROMINFO_H
#define ROMINFO_H

void romInfoFile(char *buf, int len);           /* the image's file name (its end, when long) */
void romInfoLabel(char *buf, int len);          /* the disc's volume label */
void romInfoId(char *buf, int len);             /* the disc's serial (CdromId) */
void romInfoSize(char *buf, int len);
void romInfoTracks(char *buf, int len);
void romInfoSystem(char *buf, int len);         /* region and BIOS */
void romInfoFixes(char *buf, int len);          /* the automatic fixes for this game */
void romInfoAchievements(char *buf, int len);   /* what achievements do here (achievements_status) */

#define ROM_INFO_ROWS(X) \
	X("File",          romInfoFile) \
	X("CD-ROM Label",  romInfoLabel) \
	X("CD-ROM ID",     romInfoId) \
	X("Size",          romInfoSize) \
	X("Tracks",        romInfoTracks) \
	X("System",        romInfoSystem) \
	X("Auto fixes",    romInfoFixes) \
	X("Achievements",  romInfoAchievements)

/* All rows as "Label: value" lines, appended to text (the message after a load) */
void romInfoText(char *text, int size);

#endif
