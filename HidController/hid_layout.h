/* hid_layout.h - every input of a USB HID gamepad, from its report descriptor.
 *
 * Plain C, no libogc: tests/hid_layout_test.c builds it on the host, so the code the Wii runs
 * is the code under test.
 *
 * Three steps, each a pure function:
 *   hid_parse()     the report descriptor -> where each button, hat and axis sits in a report
 *   hid_decode()    one report -> which inputs are held (hid_state.held) and the axis values
 *   hid_map_*()     those inputs -> the GameCube-shaped pad the rest of WiiStation reads
 *                   (A B X Y L1 R1 L2 R2 Select Start L3 R3, D-pad, two sticks)
 * hid_auto_map() makes that last mapping from the descriptor alone, for a pad with no ini;
 * hid_ini_map() lets a controller ini change any part of it. Docs/CONTROLLER_TESTING.md
 * section 7 has the rules and the ini syntax.
 */
#ifndef HID_LAYOUT_H
#define HID_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_MAX_BUTTONS 32
#define HID_NUM_HATS    2
#define HID_NUM_AXES    10
enum { HID_AX_X, HID_AX_Y, HID_AX_Z, HID_AX_RX, HID_AX_RY, HID_AX_RZ,
       HID_AX_SLIDER, HID_AX_DIAL, HID_AX_BRAKE, HID_AX_ACCEL };

/* Input numbers: the bits of hid_state.held. Button n (1..32) is n-1; a hat's directions are
 * up, right, down, left; an axis has a "-" and a "+" bit (pushed past half its travel). */
#define HID_IN_BUTTON(n)    ((n) - 1)
#define HID_IN_HAT(h, d)    (32 + (h) * 4 + (d))
#define HID_IN_AXIS(a, pos) (40 + (a) * 2 + (pos))
#define HID_NUM_INPUTS      60
#define HID_AXIS_PRESS      64   /* an axis direction is held past half of its travel */

typedef struct {
	uint16_t bit;      /* first bit in the report, report ID byte included */
	uint8_t  size;     /* bits */
	uint8_t  present;
	int32_t  min, max; /* logical range */
} hid_field;

typedef struct {
	uint8_t   report_id;   /* 0: the pad sends no report ID */
	uint8_t   nbuttons;    /* the highest button number found */
	uint16_t  bytes;       /* input report length, report ID byte included */
	hid_field button[HID_MAX_BUTTONS], hat[HID_NUM_HATS], axis[HID_NUM_AXES];
} hid_layout;

typedef struct {
	uint64_t held;                 /* bit HID_IN_*: held */
	int8_t   axis[HID_NUM_AXES];   /* -128..127, 0 at the middle of the range, + right / down */
} hid_state;

/* The descriptor's gamepad inputs into L. Reads what fits in len (a cut-off descriptor gives
 * the fields described before the cut). Only inputs inside a Joystick, Gamepad or Multi-axis
 * application collection count, from the first report ID that has one. Returns how many
 * inputs it found (0: not a gamepad). */
int hid_parse(const uint8_t *desc, int len, hid_layout *L);

/* One report into s. Returns 0 (s unchanged) for a report with another report ID. */
int hid_decode(const hid_layout *L, const uint8_t *r, int len, hid_state *s);

int hid_input_present(const hid_layout *L, int in);
/* "Btn 13", "Hat Up", "Hat2 Left", "Axis Z+": the menu's names, by input number */
extern const char hid_input_names[HID_NUM_INPUTS][14];
/* The ini's names: "Button13", "HatUp", "Hat2Left", "AxisZ+". Writes into buf (16 bytes). */
const char *hid_input_token(int in, char *buf);
extern const char *const hid_axis_names[HID_NUM_AXES];   /* "X" .. "Accel" */

/* The GameCube-shaped pad: its buttons (roles) and sticks, and the ini key of each */
enum { HR_A, HR_B, HR_X, HR_Y, HR_L1, HR_R1, HR_L2, HR_R2, HR_SELECT, HR_START,
       HR_L3, HR_R3, HR_UP, HR_DOWN, HR_LEFT, HR_RIGHT, HR_NUM };
enum { HS_LX, HS_LY, HS_RX, HS_RY, HS_NUM };
extern const char *const hid_role_keys[HR_NUM];    /* "KeyA" .. "KeyRight" */
extern const char *const hid_stick_keys[HS_NUM];   /* "StickX" .. "CStickY" */

/* Where a role comes from. Button roles: an input number (a), or a report bit (byte a, mask
 * b, the old ini form). Sticks: an axis (a = HID_AX_*), or a report byte (a) read as 0..255
 * with 128 the middle (the old ini form). */
enum { HID_SRC_NONE, HID_SRC_INPUT, HID_SRC_BIT };
typedef struct { uint8_t kind, a, b; } hid_src;
typedef struct { hid_src btn[HR_NUM]; hid_src stick[HS_NUM]; } hid_map;

void hid_auto_map(const hid_layout *L, uint16_t vid, hid_map *m);
/* Each Key.../Stick... line of the ini text replaces that role; returns how many did. */
int hid_ini_map(const char *ini, hid_map *m);
/* The ini asks for the descriptor-based reading: "Auto=1", or a value that names a HID
 * input (Button.., Hat.., Axis..). An ini with neither is read the old way, unchanged. */
int hid_ini_wants_auto(const char *ini);
/* The value of a key in controller-ini text: the first "key = value" line whose key, trimmed,
 * is exactly `key`. Blank lines, comment lines (first character ; or #), [section] lines and a
 * comment after a value (" ;" or " #") are skipped. Returns the value's first character and,
 * through *end (may be NULL), the end of the value without its trailing spaces; NULL when the
 * key is not there. Every reader of the ini goes through this (it used to be strstr, which
 * matched a key inside a comment, a value or a longer key: MultiIn in MultiInValue). */
const char *hid_ini_value(const char *ini, const char *key, const char **end);

/* Role bits (1 << HR_*) held now; the stick's value, -128..127, + right / down */
uint32_t hid_map_buttons(const hid_map *m, const hid_state *s, const uint8_t *r, int len);
int hid_map_stick(const hid_map *m, int stick, const hid_state *s, const uint8_t *r, int len);

#ifdef __cplusplus
}
#endif
#endif
