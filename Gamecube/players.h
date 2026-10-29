/* players.h -- the Miis on this Wii as players (Docs/MII_PLAN.md), for the Players menu.
 *
 * The Miis come from the Mii Channel's database through hbc-reborn's Mii SDK (deps/mii),
 * read only: WiiStation never writes it. A Mii's key is its creation ID as 16 hex
 * characters. Each Mii has a coin: a disc in its favorite color (a gold rim for a
 * favorite), with its head on it once sd:/mii/RFL_Res.dat is there (HBC's HOME > HBC >
 * Save Mii faces), its initial before that and without it. */
#ifndef PLAYERS_H
#define PLAYERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PLAYERS_MAX 100
#define PLAYERS_COIN 80          /* a coin's side in pixels: GX_TF_RGBA8, PLAYERS_COIN^2 * 4 bytes */
#define PLAYERS_KEY 17           /* 16 hex characters and the NUL */
#define PLAYERS_SEATS 26         /* virtualControllers 0-25: ports 1, 2; multitap 1A-1D, 2A-2D; Co-Op 1 P1-P8, 2 P1-P8 */

typedef struct {
	char key[PLAYERS_KEY];
	char name[32];               /* UTF-8 */
	unsigned char fav_color;     /* 0-11, RFL's order (MII_RED ...) */
	unsigned char favorite;      /* starred in the Mii Channel: the coin's rim is gold */
	unsigned char face;          /* the coin shows the head (else the initial) */
	unsigned coin_serial;        /* changes whenever the coin's pixels change */
	void *coin;                  /* PLAYERS_COIN x PLAYERS_COIN GX_TF_RGBA8, MEM2, flushed */
} player_mii;

/* Starts reading the Miis on a thread (once; about 0.7 s, then the faces one by one). */
void players_start(void);
/* -1 while the database is being read, else how many Miis (0: none, or no database). */
int players_count(void);
const player_mii *players_get(int i);
/* The face models: 0 not known yet, 1 loaded, negative: the MII_E_* code (MII_E_NO_FILE:
 * no sd:/mii/RFL_Res.dat, HBC's HOME > HBC > Save Mii faces makes it). */
int players_faces(void);
int players_find(const char *key);       /* the index of a key, or -1 */
/* players_get's order: the Miis last seated first (playersRecent), then the database's. The
 * reader sorts once; the menu sorts again when it opens (never while the menu shows). */
void players_sort(void);
/* Call after the menu's last copy to the screen, before the next frame is drawn: puts one
 * finished head on its coin (mii_icon draws in the EFB's corner). Returns 1 when it did, so
 * the caller sets its GX state again. */
int players_between_frames(void);
/* Before a game starts: frees the face models and the coins' work (the coins stay). */
void players_idle(void);

/* The seats, indexed as virtualControllers 0-25 (ports 1 and 2, multitap 1A-1D and 2A-2D,
 * then Co-Op players 1-8 of each port, COOP_VC): the key of the Mii on each, "" for Guest.
 * players.ini Player1 ... Player2D, Player1P1 ... Player2P8 (GamecubeMain.cpp), written when
 * the Players menu closes. */
extern char playerSeat[PLAYERS_SEATS][256];
extern const char *const PLAYERS_SEAT_NAMES[PLAYERS_SEATS];   /* "1", "2", "1A" ... "2D", "1 P1" ... "2 P8" */
void players_seat_mii(int seat, const char *key);   /* seats a Mii (leaving its other seat), or Guest for NULL */
/* The Miis last seated, most recent first (players_recent.h): players.ini PlayersRecent. */
extern char playersRecent[256];
void players_used(void);   /* the seated Miis to the front of playersRecent */
/* Is the Mii with this key on the console, and what is its name (UTF-8, at most size - 1
 * bytes)? For a game's achievements, which need the names of the seated Miis: waits for the
 * menu's reader if it is running, else reads the database once. 1 yes, 0 no (a Mii that left
 * the console, or no database), -1 not known (the reader did not finish). */
int players_lookup(const char *key, char *name, unsigned size);

/* RetroAchievements accounts linked to Miis: players.ini [Accounts], "<Mii key> = <name>".
 * A name is an account file's: "" none (the Mii earns Local), "*" this Wii's ra_account.ini,
 * else achievements/ra/<name>.ini. Only names: nothing here opens an account file. */
#define PLAYERS_ACCOUNT 32
const char *players_account(const char *key);              /* its link, "" none */
void players_link(const char *key, const char *name);      /* "" unlinks */
const char *players_link_at(int i, const char **name);     /* the i-th link's key, NULL past the end */
void players_links_clear(void);                            /* before players.ini is read again */
extern int playersLinksChanged;                            /* a link changed since players.ini */
/* The Mii that plays Official (one per game): PlayersOfficial when it is linked and seated,
 * else port 1's Mii (1A, 1 P1) when that one is linked. "" none: today's ra_account.ini. */
extern char playersOfficial[256];
const char *players_official(void);
int players_seat_active(int seat);   /* a seat that plays as the ports are set now */

#ifdef __cplusplus
}
#endif

#endif
