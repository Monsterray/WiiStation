/* hid_layout.c - see hid_layout.h. Plain C, no libogc (tests/hid_layout_test.c). */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hid_layout.h"

const char hid_input_names[HID_NUM_INPUTS][14] = {
	"Btn 1",  "Btn 2",  "Btn 3",  "Btn 4",  "Btn 5",  "Btn 6",  "Btn 7",  "Btn 8",
	"Btn 9",  "Btn 10", "Btn 11", "Btn 12", "Btn 13", "Btn 14", "Btn 15", "Btn 16",
	"Btn 17", "Btn 18", "Btn 19", "Btn 20", "Btn 21", "Btn 22", "Btn 23", "Btn 24",
	"Btn 25", "Btn 26", "Btn 27", "Btn 28", "Btn 29", "Btn 30", "Btn 31", "Btn 32",
	"Hat Up", "Hat Right", "Hat Down", "Hat Left",
	"Hat2 Up", "Hat2 Right", "Hat2 Down", "Hat2 Left",
	"Axis X-", "Axis X+", "Axis Y-", "Axis Y+", "Axis Z-", "Axis Z+",
	"Axis Rx-", "Axis Rx+", "Axis Ry-", "Axis Ry+", "Axis Rz-", "Axis Rz+",
	"Axis Slider-", "Axis Slider+", "Axis Dial-", "Axis Dial+",
	"Axis Brake-", "Axis Brake+", "Axis Accel-", "Axis Accel+",
};
const char *const hid_axis_names[HID_NUM_AXES] =
	{ "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial", "Brake", "Accel" };
static const char *const HAT_DIRS[4] = { "Up", "Right", "Down", "Left" };
const char *const hid_role_keys[HR_NUM] = {
	"KeyA", "KeyB", "KeyX", "KeyY", "KeyL1", "KeyR1", "KeyL2", "KeyR2", "KeySelect", "KeyStart",
	"KeyL3", "KeyR3", "KeyUp", "KeyDown", "KeyLeft", "KeyRight" };
const char *const hid_stick_keys[HS_NUM] = { "StickX", "StickY", "CStickX", "CStickY" };

/* ---- the report descriptor ---- */

/* Which field a usage is, or NULL. page/usage as in the HID Usage Tables. */
static hid_field *slot(hid_layout *L, unsigned page, unsigned usage)
{
	if (page == 0x09 && usage >= 1 && usage <= HID_MAX_BUTTONS)
		return &L->button[usage - 1];
	if (page == 0x01 && usage >= 0x30 && usage <= 0x37)   /* X Y Z Rx Ry Rz Slider Dial */
		return &L->axis[usage - 0x30];
	if (page == 0x01 && usage == 0x39)
		return !L->hat[0].present ? &L->hat[0] : &L->hat[1];
	if (page == 0x02 && usage == 0xC5)
		return &L->axis[HID_AX_BRAKE];
	if (page == 0x02 && usage == 0xC4)
		return &L->axis[HID_AX_ACCEL];
	return NULL;
}

static int32_t sdata(uint32_t v, int n)   /* an item's data, signed */
{
	return n == 1 ? (int8_t)v : n == 2 ? (int16_t)v : (int32_t)v;
}

int hid_parse(const uint8_t *d, int len, hid_layout *L)
{
	static uint16_t bits[256];   /* input bits so far, per report ID */
	uint32_t page = 0, rsize = 0, rcount = 0, id = 0;
	int32_t lmin = 0, lmax = 0;
	uint32_t usages[64], umin = 0, umax = 0, nus = 0, have_range = 0;
	int depth = 0, app_ok = 0, chosen = -1, found = 0, i = 0;

	memset(L, 0, sizeof(*L));
	memset(bits, 0, sizeof(bits));
	while (i < len) {
		uint8_t pre = d[i];
		if (pre == 0xFE) {                          /* long item: skip it */
			if (i + 2 >= len) break;
			i += 3 + d[i + 1];
			continue;
		}
		int n = (pre & 3) == 3 ? 4 : (pre & 3);
		if (i + 1 + n > len) break;                 /* cut off mid-item */
		uint32_t v = 0;
		for (int k = 0; k < n; k++) v |= (uint32_t)d[i + 1 + k] << (8 * k);
		int type = (pre >> 2) & 3, tag = pre >> 4;
		i += 1 + n;

		if (type == 1) {                            /* global */
			switch (tag) {
			case 0: page = v; break;
			case 1: lmin = sdata(v, n); break;
			case 2: lmax = sdata(v, n);
				/* "25 FF" for 0..255 is -1 to the letter; every OS reads it as 255 */
				if (lmin >= 0 && lmax < lmin) lmax = (int32_t)v;
				break;
			case 7: rsize = v; break;
			case 8: id = v & 0xFF; if (!bits[id] && id) bits[id] = 8; break;
			case 9: rcount = v; break;
			}
			continue;
		}
		if (type == 2) {                            /* local */
			uint32_t u = n == 4 ? v : (page << 16) | v;
			if (tag == 0 && nus < 64) usages[nus++] = u;
			else if (tag == 1) { umin = u; have_range = 1; }
			else if (tag == 2) { umax = u; have_range = 1; }
			continue;
		}
		if (type != 0) continue;
		/* main */
		if (tag == 0xA) {                           /* Collection */
			if (depth == 0 && v == 1) {             /* application: what kind */
				uint32_t u = nus ? usages[nus - 1] : 0;
				app_ok = u == 0x10004 || u == 0x10005 || u == 0x10008;
			}
			depth++;
		} else if (tag == 0xC) {                    /* End Collection */
			if (depth > 0 && --depth == 0) app_ok = 0;
		} else if (tag == 0x8) {                    /* Input */
			uint32_t start = bits[id];
			if (app_ok && !(v & 1) && (v & 2)) {    /* data, variable */
				/* a broken descriptor's count can be 4 billion: past 512 fields nothing fits */
				for (uint32_t k = 0; k < rcount && k < 512; k++) {
					uint32_t u = k < nus ? usages[k] : nus ? usages[nus - 1]
						: have_range ? (umin + k > umax ? umax : umin + k) : 0;
					hid_field *f = slot(L, u >> 16, u & 0xFFFF);
					if (!f || f->present || rsize == 0 || rsize > 32) continue;
					if (chosen < 0) chosen = (int)id;
					if ((int)id != chosen) continue;
					f->present = 1;
					f->bit = (uint16_t)(start + k * rsize);
					f->size = (uint8_t)rsize;
					f->min = lmin;
					f->max = lmax;
					found++;
				}
			}
			bits[id] = (uint16_t)(start + rsize * rcount);
		}
		nus = 0; have_range = 0; umin = umax = 0;   /* locals end with each main item */
	}
	if (chosen < 0) return 0;
	L->report_id = (uint8_t)chosen;
	L->bytes = (uint16_t)((bits[chosen] + 7) / 8);
	for (int b = 0; b < HID_MAX_BUTTONS; b++)
		if (L->button[b].present) L->nbuttons = (uint8_t)(b + 1);
	return found;
}

/* ---- a report ---- */

static int32_t field(const hid_field *f, const uint8_t *r, int len)
{
	uint32_t v = 0;
	for (int k = 0; k < f->size; k++) {
		int b = f->bit + k;
		if ((b >> 3) >= len) break;
		if ((r[b >> 3] >> (b & 7)) & 1) v |= 1u << k;
	}
	if (f->min < 0 && f->size < 32 && ((v >> (f->size - 1)) & 1))
		v |= ~0u << f->size;                        /* sign-extend */
	return (int32_t)v;
}

/* -128..127 across the logical range, 0 at its middle (128 of 0..255 reads 0) */
static int norm(const hid_field *f, int32_t x)
{
	int64_t span = (int64_t)f->max - f->min, v;
	if (span <= 0) return 0;
	if (x < f->min) x = f->min;
	if (x > f->max) x = f->max;
	v = ((2 * ((int64_t)x - f->min) - span) * 128) / span;
	return v > 127 ? 127 : v < -128 ? -128 : (int)v;
}

int hid_decode(const hid_layout *L, const uint8_t *r, int len, hid_state *s)
{
	static const uint8_t HAT8[8] = { 1, 1|2, 2, 2|4, 4, 4|8, 8, 8|1 };   /* up right down left */
	uint64_t held = 0;
	if (L->report_id && (len < 1 || r[0] != L->report_id))
		return 0;
	for (int b = 0; b < HID_MAX_BUTTONS; b++)
		if (L->button[b].present && field(&L->button[b], r, len))
			held |= 1ull << HID_IN_BUTTON(b + 1);
	for (int h = 0; h < HID_NUM_HATS; h++) {
		const hid_field *f = &L->hat[h];
		if (!f->present) continue;
		int32_t v = field(f, r, len) - f->min, n = f->max - f->min + 1;
		int dir = n == 8 ? v : n == 4 ? v * 2 : -1;  /* outside the range: the null state */
		if (v < 0 || v >= n || dir < 0 || dir > 7) continue;
		for (int k = 0; k < 4; k++)
			if (HAT8[dir] & (1 << k)) held |= 1ull << HID_IN_HAT(h, k);
	}
	for (int a = 0; a < HID_NUM_AXES; a++) {
		const hid_field *f = &L->axis[a];
		int v = f->present ? norm(f, field(f, r, len)) : 0;
		s->axis[a] = (int8_t)v;
		if (v < -HID_AXIS_PRESS) held |= 1ull << HID_IN_AXIS(a, 0);
		if (v > HID_AXIS_PRESS)  held |= 1ull << HID_IN_AXIS(a, 1);
	}
	s->held = held;
	return 1;
}

int hid_input_present(const hid_layout *L, int in)
{
	if (in < 0 || in >= HID_NUM_INPUTS) return 0;
	if (in < 32) return L->button[in].present;
	if (in < 40) return L->hat[(in - 32) / 4].present;
	return L->axis[(in - 40) / 2].present;
}

const char *hid_input_token(int in, char *buf)
{
	if (in < 0 || in >= HID_NUM_INPUTS) snprintf(buf, 16, "None");
	else if (in < 32) snprintf(buf, 16, "Button%d", in + 1);
	else if (in < 40) snprintf(buf, 16, "Hat%s%s", in < 36 ? "" : "2", HAT_DIRS[(in - 32) % 4]);
	else snprintf(buf, 16, "Axis%s%c", hid_axis_names[(in - 40) / 2], (in & 1) ? '+' : '-');
	return buf;
}

/* ---- the GameCube-shaped pad ---- */

static void set(hid_src *s, int in) { s->kind = HID_SRC_INPUT; s->a = (uint8_t)in; s->b = 0; }

/* Standard gamepad conventions (DirectInput order, as a DualShock 4, a Logitech Dual Action
 * and most generic pads number them): buttons 1-4 Square Cross Circle Triangle (ini KeyA KeyB
 * KeyX KeyY), 5-8 L1 R1 L2 R2, 9-10 Select Start, 11-12 L3 R3; a pad with fewer than 10
 * buttons has Select and Start on its last two and shoulders on the ones between. Hat 1 is the
 * D-pad; with no hat, the X/Y axes are (an SNES-style pad sends its D-pad as X/Y). Sticks X/Y
 * and Z/Rz, or Rx/Ry when there is no Z/Rz pair or the pad is Microsoft's (X/Y + Rx/Ry, Z/Rz
 * triggers). Brake/Accelerator axes (Xbox-style triggers) are L2/R2. */
void hid_auto_map(const hid_layout *L, uint16_t vid, hid_map *m)
{
	static const uint8_t FULL[8] = { HR_L1, HR_R1, HR_L2, HR_R2, HR_SELECT, HR_START, HR_L3, HR_R3 };
	int n = L->nbuttons, b;
	const hid_field *ax = L->axis;
	memset(m, 0, sizeof(*m));
	for (b = 1; b <= 4 && b <= n; b++)
		set(&m->btn[HR_A + b - 1], HID_IN_BUTTON(b));
	if (n >= 10) {
		for (b = 5; b <= 12 && b <= n; b++)
			set(&m->btn[FULL[b - 5]], HID_IN_BUTTON(b));
	} else if (n >= 5) {
		for (b = 5; b <= n - 2 && b <= 8; b++)
			set(&m->btn[FULL[b - 5]], HID_IN_BUTTON(b));
		if (n >= 6) set(&m->btn[HR_SELECT], HID_IN_BUTTON(n - 1));
		set(&m->btn[HR_START], HID_IN_BUTTON(n));
	}
	if (ax[HID_AX_BRAKE].present && ax[HID_AX_ACCEL].present) {
		set(&m->btn[HR_L2], HID_IN_AXIS(HID_AX_BRAKE, 1));
		set(&m->btn[HR_R2], HID_IN_AXIS(HID_AX_ACCEL, 1));
	}
	if (L->hat[0].present) {
		set(&m->btn[HR_UP], HID_IN_HAT(0, 0));    set(&m->btn[HR_RIGHT], HID_IN_HAT(0, 1));
		set(&m->btn[HR_DOWN], HID_IN_HAT(0, 2));  set(&m->btn[HR_LEFT], HID_IN_HAT(0, 3));
	} else if (ax[HID_AX_X].present && ax[HID_AX_Y].present) {
		set(&m->btn[HR_LEFT], HID_IN_AXIS(HID_AX_X, 0)); set(&m->btn[HR_RIGHT], HID_IN_AXIS(HID_AX_X, 1));
		set(&m->btn[HR_UP], HID_IN_AXIS(HID_AX_Y, 0));   set(&m->btn[HR_DOWN], HID_IN_AXIS(HID_AX_Y, 1));
	}
	if (ax[HID_AX_X].present) set(&m->stick[HS_LX], HID_AX_X);
	if (ax[HID_AX_Y].present) set(&m->stick[HS_LY], HID_AX_Y);
	int rxry = ax[HID_AX_RX].present && ax[HID_AX_RY].present;
	int zrz = ax[HID_AX_Z].present && ax[HID_AX_RZ].present;
	if (rxry && (!zrz || vid == 0x045E)) {
		set(&m->stick[HS_RX], HID_AX_RX); set(&m->stick[HS_RY], HID_AX_RY);
	} else if (zrz) {
		set(&m->stick[HS_RX], HID_AX_Z);  set(&m->stick[HS_RY], HID_AX_RZ);
	}
}

/* One ini value: Button<n>, Hat[2]<dir>, Axis<name>[+|-], None, or hex "byte,mask"
 * (stick lines: "byte[,deadzone,radius]"). Returns 0 for anything else. */
static int parse_src(const char *v, const char *end, int stick, hid_src *s)
{
	size_t len = (size_t)(end - v);
	char *e;
	memset(s, 0, sizeof(*s));
	if (len == 4 && !strncmp(v, "None", 4))
		return 1;
	if (!stick && len > 6 && !strncmp(v, "Button", 6)) {
		long b = strtol(v + 6, &e, 10);
		if (e != end || b < 1 || b > HID_MAX_BUTTONS) return 0;
		set(s, HID_IN_BUTTON(b)); return 1;
	}
	if (!stick && len > 3 && !strncmp(v, "Hat", 3)) {
		int h = v[3] == '2';
		for (int d = 0; d < 4; d++)
			if (len == 3 + (size_t)h + strlen(HAT_DIRS[d]) && !strncmp(v + 3 + h, HAT_DIRS[d], len - 3 - h)) {
				set(s, HID_IN_HAT(h, d)); return 1;
			}
		return 0;
	}
	if (len > 4 && !strncmp(v, "Axis", 4)) {
		int sign = end[-1] == '+' ? 1 : end[-1] == '-' ? 0 : -1;
		size_t nl = len - 4 - (sign >= 0);
		if (stick == (sign >= 0)) return 0;          /* a stick takes the axis, a button a direction */
		for (int a = 0; a < HID_NUM_AXES; a++)
			if (strlen(hid_axis_names[a]) == nl && !strncmp(v + 4, hid_axis_names[a], nl)) {
				set(s, stick ? a : HID_IN_AXIS(a, sign)); return 1;
			}
		return 0;
	}
	unsigned long byte = strtoul(v, &e, 16);
	if (e == v || byte > 255) return 0;
	s->kind = HID_SRC_BIT;
	s->a = (uint8_t)byte;
	if (!stick) {
		if (*e != ',') return 0;
		s->b = (uint8_t)strtoul(e + 1, NULL, 16);
	}
	return 1;
}

/* One line of ini text: returns the next line; for a "key = value" line sets the trimmed key
 * [*k, *k + *kl) and value [*v, *vend), else *kl = 0 (blank, comment or [section] line). */
static const char *ini_line(const char *p, const char **k, size_t *kl, const char **v, const char **vend)
{
	const char *nl = strchr(p, '\n'), *eol = nl ? nl : p + strlen(p), *next = nl ? nl + 1 : eol;
	const char *a = p, *eq, *e;
	*kl = 0;
	while (a < eol && (*a == ' ' || *a == '\t')) a++;
	if (a == eol || *a == ';' || *a == '#' || *a == '[' || *a == '\r')
		return next;
	eq = memchr(a, '=', (size_t)(eol - a));
	if (!eq)
		return next;
	for (e = eq; e > a && (e[-1] == ' ' || e[-1] == '\t'); e--) ;
	*k = a;
	*kl = (size_t)(e - a);
	for (a = eq + 1; a < eol && (*a == ' ' || *a == '\t'); a++) ;
	for (e = a; e < eol; e++)   /* a comment after the value */
		if ((*e == ';' || *e == '#') && e > a && (e[-1] == ' ' || e[-1] == '\t')) break;
	while (e > a && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
	*v = a;
	*vend = e;
	return next;
}

const char *hid_ini_value(const char *ini, const char *key, const char **end)
{
	size_t want = strlen(key);
	while (ini && *ini) {
		const char *k, *v, *vend;
		size_t kl;
		ini = ini_line(ini, &k, &kl, &v, &vend);
		if (kl == want && !strncmp(k, key, want)) {
			if (end) *end = vend;
			return v;
		}
	}
	return NULL;
}

int hid_ini_map(const char *ini, hid_map *m)
{
	int count = 0;
	while (ini && *ini) {
		const char *k, *v, *vend;
		size_t kl;
		ini = ini_line(ini, &k, &kl, &v, &vend);
		if (!kl || v >= vend)
			continue;
		hid_src s;
		for (int r = 0; r < HR_NUM + HS_NUM; r++) {
			const char *key = r < HR_NUM ? hid_role_keys[r] : hid_stick_keys[r - HR_NUM];
			if (strlen(key) != kl || strncmp(k, key, kl)) continue;
			if (parse_src(v, vend, r >= HR_NUM, &s)) {
				if (r < HR_NUM) m->btn[r] = s; else m->stick[r - HR_NUM] = s;
				count++;
			}
			break;
		}
	}
	return count;
}

int hid_ini_wants_auto(const char *ini)
{
	const char *a = hid_ini_value(ini, "Auto", NULL);
	if (a && *a == '1')
		return 1;
	while (ini && *ini) {   /* any value naming a HID input */
		const char *k, *v, *vend;
		size_t kl;
		ini = ini_line(ini, &k, &kl, &v, &vend);
		if (kl && (!strncmp(v, "Button", 6) || !strncmp(v, "Hat", 3) || !strncmp(v, "Axis", 4)))
			return 1;
	}
	return 0;
}


uint32_t hid_map_buttons(const hid_map *m, const hid_state *s, const uint8_t *r, int len)
{
	uint32_t out = 0;
	for (int k = 0; k < HR_NUM; k++) {
		const hid_src *x = &m->btn[k];
		int on = x->kind == HID_SRC_INPUT ? (int)((s->held >> x->a) & 1)
		       : x->kind == HID_SRC_BIT ? (x->a < len && (r[x->a] & x->b)) : 0;
		if (on) out |= 1u << k;
	}
	return out;
}

int hid_map_stick(const hid_map *m, int stick, const hid_state *s, const uint8_t *r, int len)
{
	const hid_src *x = &m->stick[stick];
	if (x->kind == HID_SRC_INPUT && x->a < HID_NUM_AXES) return s->axis[x->a];
	if (x->kind == HID_SRC_BIT && x->a < len) return r[x->a] - 128;
	return 0;
}
