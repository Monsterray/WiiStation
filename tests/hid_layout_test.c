/* hid_layout_test.c - USB HID gamepads: descriptor -> report -> GameCube-shaped pad, exhaustively.
 *
 *     clang -O2 -Wall -o hid_layout_test tests/hid_layout_test.c HidController/hid_layout.c
 *     ./hid_layout_test Gamecube/release/controllers/[0-9A-F]*.ini
 *
 * HidController/hid_layout.c is plain C, so the code the Wii runs is the code under test.
 * For five real or real-shaped descriptors it checks every button, every hat value and every
 * axis value through hid_decode, the automatic layout of each pad, the ini override syntax,
 * descriptors cut off at every length, and that no shipped controller ini is taken for a new-
 * style one (old inis must keep the old reading).
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../HidController/hid_layout.h"

static int fails;
#define CHECK(c, ...) do { if (!(c) && ++fails <= 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int unhex(const char *s, uint8_t *out)
{
	int n = 0;
	for (; s[0] && s[1]; s += 2) {
		unsigned v;
		sscanf(s, "%2x", &v);
		out[n++] = (uint8_t)v;
	}
	return n;
}

/* The pads. Button n sits at bit btn0 + n - 1 (byte = bit/8, LSB first), from each pad's
 * own documentation or ini, not from the parser. */
typedef struct {
	const char *name, *desc;
	uint16_t vid;
	int report_id, nbuttons, bytes, btn0;
	int hat_bit, hat_min;      /* -1: no hat */
	const char *axes;          /* axes present, as "X Y Z Rz" */
	const char *rest;          /* a report at rest, hex */
	int role_in[HR_NUM];       /* expected input per role (-1 none) */
	int stick_ax[HS_NUM];      /* expected axis per stick (-1 none) */
} pad_t;

#define B(n) HID_IN_BUTTON(n)
#define H(d) HID_IN_HAT(0, d)
#define AX(a, p) HID_IN_AXIS(HID_AX_##a, p)

static const pad_t PADS[] = {
	{ "Logitech Dual Action 046D:C216 (the user's usbpad.log, cut off as logged)",
	  "05010904A101A102150026FF00350046FF0075089504093009310932093581022507463B01750495016514093981426500250145017501950C05091901290C81020600FF750195102501450109018102C0A10226FF0046FF007508950709029102C0A102",
	  0x046D, 0, 12, 8, 36, 32, 0, "X Y Z Rz", "8080807F08000000",
	  { B(1), B(2), B(3), B(4), B(5), B(6), B(7), B(8), B(9), B(10), B(11), B(12), H(0), H(2), H(3), H(1) },
	  { HID_AX_X, HID_AX_Y, HID_AX_Z, HID_AX_RZ } },
	{ "Sony DualShock 3 054C:0268 (Linux hid-sony sixaxis_rdesc)",
	  "05010904A101A102850175089501150026FF00810375019513150025013500450105091901291381027501950D0600FF8103150026FF0005010901A10075089504350046FF0009300931093209358102C00501951309018102950C81017510950426FF0346FF0309018102C0A1028502750895300901B102C0A10285EE750895300901B102C0A10285EF750895300901B102C0C0",
	  0x054C, 1, 19, 49, 16, -1, 0, "X Y Z Rz", "0100000000008080807F",
	  /* the shipped 054C_0268.ini is used instead; this is what no ini would give */
	  { B(1), B(2), B(3), B(4), B(5), B(6), B(7), B(8), B(9), B(10), B(11), B(12),
	    AX(Y, 0), AX(Y, 1), AX(X, 0), AX(X, 1) },
	  { HID_AX_X, HID_AX_Y, HID_AX_Z, HID_AX_RZ } },
	{ "Sony DualShock 4 054C:05C4 (Linux hid-sony dualshock4_usb_rdesc, cut at the Wii's 512 bytes)",
	  "05010905A10185010930093109320935150026FF007508950481020939150025073500463B016514750495018142650005091901290E150025017501950E81020600FF0920750695011500253F8102050109330934150026FF007508950281020600FF09219503810205011940294216008026007F751095038102194329451600E026FF1F950381020600FF0921150026FF0075089527810285050922951F9102850409239524B102850209249524B102850809259503B102851009269504B102851109279502B10285120602FF0921950FB102851309229516B10285140605FF09209510B10285150921952CB1020680FF858009209506B102858109219506B102858209229505B102858309239501B102858409249504B102858509259506B102858609269506B102858709279523B102858809289522B102858909299502B102859009309505B102859109319503B102859209329503B10285930933950CB10285A009409506B10285A109419501B10285A209429501B10285A309439530B10285A40944950DB10285A509459515B10285A609469515B10285F00947953FB10285F10948953FB10285F20949950FB10285A7094A9501B10285A8094B9501B10285A9094C9508B10285AA094E9501B10285AB094F9539B10285AC09509539B10285AD0951950BB10285AE09529501B10285AF09539502B10285B00954953FB102C0",
	  0x054C, 1, 14, 64, 44, 40, 0, "X Y Z Rx Ry Rz", "01808080800800000000",
	  { B(1), B(2), B(3), B(4), B(5), B(6), B(7), B(8), B(9), B(10), B(11), B(12), H(0), H(2), H(3), H(1) },
	  { HID_AX_X, HID_AX_Y, HID_AX_Z, HID_AX_RZ } },
	{ "Xbox-style pad (shape of an Xbox One S over Bluetooth: 16-bit sticks, Brake/Accelerator triggers, hat 1..8, 15 buttons)",
	  "05010905A1018501" "0901A10009300931150027FFFF0000950275108102C0"
	  "0901A10009320935150027FFFF0000950275108102C0"
	  "050209C5150026FF039501750A8102" "15002500750695018103"
	  "050209C4150026FF039501750A8102" "15002500750695018103"
	  "05010939150125083500463B01661400750495018142" "75049501150025003500450065008103"
	  "05091901290F150025017501950F8102" "15002500750195018103"
	  "050C0A240215002501950175018102" "15002500750795018103" "C0",
	  0x045E, 1, 15, 17, 112, 104, 1, "X Y Z Rz Brake Accel", "0100800080008000800000000000000000",
	  { B(1), B(2), B(3), B(4), B(5), B(6), AX(BRAKE, 1), AX(ACCEL, 1), B(9), B(10), B(11), B(12), H(0), H(2), H(3), H(1) },
	  { HID_AX_X, HID_AX_Y, HID_AX_Z, HID_AX_RZ } },
	{ "SNES-style pad: X/Y as the D-pad (logical max written 25 FF), 8 buttons",
	  "05010904A101A10275089502150025FF350045FF09300931810275019508150025010509190129088102C0C0",
	  0x0079, 0, 8, 3, 16, -1, 0, "X Y", "808000",
	  { B(1), B(2), B(3), B(4), B(5), B(6), -1, -1, B(7), B(8), -1, -1,
	    AX(Y, 0), AX(Y, 1), AX(X, 0), AX(X, 1) },
	  { HID_AX_X, HID_AX_Y, -1, -1 } },
};

static int has_axis(const char *list, int a)
{
	char buf[64], *t;
	snprintf(buf, sizeof buf, "%s", list);
	for (t = strtok(buf, " "); t; t = strtok(NULL, " "))
		if (!strcmp(t, hid_axis_names[a])) return 1;
	return 0;
}

static void setbit(uint8_t *r, int bit) { r[bit >> 3] |= (uint8_t)(1 << (bit & 7)); }

static int checks;

static void test_pad(const pad_t *p)
{
	uint8_t desc[1024], rest[64], r[64];
	char hexd[2048];
	hid_layout L;
	hid_state s;
	hid_map m;
	snprintf(hexd, sizeof hexd, "%s", p->desc);
	int dlen = unhex(hexd, desc);
	if (dlen > 512) dlen = 512;
	memset(rest, 0, sizeof rest);
	int rlen = p->bytes;   /* the rest given, then zeros */
	CHECK(unhex(p->rest, rest) <= rlen, "rest report too long");
	printf("%s: %d descriptor bytes\n", p->name, dlen);

	int found = hid_parse(desc, dlen, &L);
	CHECK(found > 0, "nothing found");
	CHECK(L.report_id == p->report_id, "report id %d", L.report_id);
	CHECK(L.nbuttons == p->nbuttons, "nbuttons %d", L.nbuttons);
	CHECK(L.bytes == p->bytes, "bytes %d", L.bytes);
	CHECK(L.hat[0].present == (p->hat_bit >= 0), "hat present");
	CHECK(!L.hat[1].present, "second hat");
	for (int a = 0; a < HID_NUM_AXES; a++)
		CHECK(L.axis[a].present == has_axis(p->axes, a), "axis %s present=%d", hid_axis_names[a], L.axis[a].present);

	/* rest: no button or hat held; only an axis that rests at its end (a trigger) holds a
	 * direction, and no role is pressed */
	CHECK(hid_decode(&L, rest, rlen, &s) == 1, "decode rest");
	uint64_t rest_held = s.held;
	CHECK((s.held & ((1ull << 40) - 1)) == 0, "held at rest %llx", (unsigned long long)s.held);
	for (int a = 0; a < HID_NUM_AXES; a++)
		if ((s.held >> HID_IN_AXIS(a, 0)) & 3)
			CHECK(s.axis[a] == -128, "axis %s held at rest", hid_axis_names[a]);
	hid_auto_map(&L, p->vid, &m);
	CHECK(hid_map_buttons(&m, &s, rest, rlen) == 0, "roles at rest");

	/* a report with another ID is left alone */
	if (p->report_id) {
		memcpy(r, rest, rlen);
		r[0] = (uint8_t)(p->report_id + 1);
		hid_state t = s;
		CHECK(hid_decode(&L, r, rlen, &t) == 0 && t.held == s.held, "other report id");
	}

	/* every button alone: exactly that input, and exactly the roles mapped to it */
	for (int n = 1; n <= HID_MAX_BUTTONS; n++) {
		CHECK(hid_input_present(&L, B(n)) == (n <= p->nbuttons), "button %d present", n);
		if (n > p->nbuttons) continue;
		memcpy(r, rest, rlen);
		setbit(r, p->btn0 + n - 1);
		hid_decode(&L, r, rlen, &s);
		CHECK(s.held == (rest_held | 1ull << B(n)), "button %d gives %llx", n, (unsigned long long)s.held);
		uint32_t want = 0;
		for (int k = 0; k < HR_NUM; k++) if (p->role_in[k] == B(n)) want |= 1u << k;
		CHECK(hid_map_buttons(&m, &s, r, rlen) == want, "button %d roles", n);
		checks++;
	}

	/* every hat value: 0..7 the eight directions, the rest the null state */
	if (p->hat_bit >= 0) {
		static const uint8_t DIRS[8] = { 1, 3, 2, 6, 4, 12, 8, 9 };   /* bit 0 up 1 right 2 down 3 left */
		for (int v = 0; v < 16; v++) {
			memcpy(r, rest, rlen);
			for (int k = 0; k < 4; k++) r[(p->hat_bit + k) >> 3] &= (uint8_t)~(1 << ((p->hat_bit + k) & 7));
			for (int k = 0; k < 4; k++) if ((v >> k) & 1) setbit(r, p->hat_bit + k);
			hid_decode(&L, r, rlen, &s);
			int d = v - p->hat_min;
			uint64_t want = 0;
			if (d >= 0 && d < 8)
				for (int k = 0; k < 4; k++) if (DIRS[d] & (1 << k)) want |= 1ull << H(k);
			want |= rest_held;
			CHECK(s.held == want, "hat value %d gives %llx", v, (unsigned long long)s.held);
			uint32_t roles = hid_map_buttons(&m, &s, r, rlen), wr = 0;
			for (int k = 0; k < HR_NUM; k++)
				if (p->role_in[k] >= 0 && (want >> p->role_in[k]) & 1) wr |= 1u << k;
			CHECK(roles == wr, "hat value %d roles %x", v, roles);
			checks++;
		}
	}

	/* every axis, every value: never backwards, both ends reached, the direction bits at
	 * past half travel; the stick roles follow it */
	for (int a = 0; a < HID_NUM_AXES; a++) {
		const hid_field *f = &L.axis[a];
		if (!f->present) continue;
		int prev = -999, lo = 999, hi = -999;
		long long steps = (long long)f->max - f->min;
		for (long long x = f->min; x <= f->max; x++) {
			memcpy(r, rest, rlen);
			for (int k = 0; k < f->size; k++) {
				int b = f->bit + k;
				r[b >> 3] = (uint8_t)((r[b >> 3] & ~(1 << (b & 7))) | ((((unsigned long long)x >> k) & 1) << (b & 7)));
			}
			hid_decode(&L, r, rlen, &s);
			int v = s.axis[a];
			CHECK(v >= prev, "axis %s backwards at %lld", hid_axis_names[a], x);
			prev = v;
			if (v < lo) lo = v;
			if (v > hi) hi = v;
			uint64_t dirs = s.held & (3ull << HID_IN_AXIS(a, 0));
			CHECK((s.held & ~(3ull << HID_IN_AXIS(a, 0))) == (rest_held & ~(3ull << HID_IN_AXIS(a, 0))), "axis %s moves others", hid_axis_names[a]);
			uint64_t want = v < -HID_AXIS_PRESS ? 1ull << HID_IN_AXIS(a, 0) : v > HID_AXIS_PRESS ? 1ull << HID_IN_AXIS(a, 1) : 0;
			CHECK(dirs == want, "axis %s dirs at %lld", hid_axis_names[a], x);
			for (int st = 0; st < HS_NUM; st++)
				if (p->stick_ax[st] == a)
					CHECK(hid_map_stick(&m, st, &s, r, rlen) == v, "stick %d", st);
			if (x == f->min + steps / 2 + (steps & 1))  /* the upper middle: 128 of 0..255 */
				CHECK(v == 0, "axis %s middle %d", hid_axis_names[a], v);
			checks++;
		}
		CHECK(lo == -128 && hi == 127, "axis %s range %d..%d", hid_axis_names[a], lo, hi);
	}

	/* the automatic layout */
	for (int k = 0; k < HR_NUM; k++) {
		int got = m.btn[k].kind == HID_SRC_INPUT ? m.btn[k].a : -1;
		CHECK(got == p->role_in[k], "role %s = %d, want %d", hid_role_keys[k], got, p->role_in[k]);
	}
	for (int st = 0; st < HS_NUM; st++) {
		int got = m.stick[st].kind == HID_SRC_INPUT ? m.stick[st].a : -1;
		CHECK(got == p->stick_ax[st], "stick %s = %d", hid_stick_keys[st], got);
	}

	/* cut off at every length: never more found than with more of it, no crash */
	int last = 0;
	for (int n = 0; n <= dlen; n++) {
		hid_layout C;
		int f = hid_parse(desc, n, &C);
		CHECK(f >= last, "cut at %d found %d after %d", n, f, last);
		last = f;
	}
	CHECK(last == found, "full length");
}

static void test_ini(void)
{
	hid_map m;
	char tok[16], line[64];
	memset(&m, 0, sizeof m);
	/* every input's ini name names that input, and its menu name says the same */
	for (int in = 0; in < HID_NUM_INPUTS; in++) {
		hid_input_token(in, tok);
		snprintf(line, sizeof line, "KeyL3=%s\r\n", tok);
		memset(&m, 0, sizeof m);
		CHECK(hid_ini_map(line, &m) == 1 && m.btn[HR_L3].kind == HID_SRC_INPUT && m.btn[HR_L3].a == in, "token %s", tok);
		CHECK(hid_ini_wants_auto(line), "token %s is new-style", tok);
		char a[16] = "", b[16] = "";
		for (const char *s = tok; *s; s++) if (*s != ' ') { size_t l = strlen(a); a[l] = *s; a[l + 1] = 0; }
		for (const char *s = hid_input_names[in]; *s; s++) if (*s != ' ') { size_t l = strlen(b); b[l] = *s; b[l + 1] = 0; }
		/* "Button13" vs "Btn13" */
		if (!strncmp(a, "Button", 6)) memmove(a + 3, a + 6, strlen(a + 6) + 1), a[1] = 't', a[2] = 'n';
		CHECK(!strcmp(a, b), "names %s / %s", tok, hid_input_names[in]);
	}
	const char *ini =
		"[Test]\nVID=046D\nPID=C216\nAuto=1\n"
		"KeyA=Button13\nKeyB=HatUp\nKeyX=Hat2Left\nKeyY=AxisZ+\nKeyL1=5,40\nKeyR1=None\n"
		"StickX=AxisRz\r\nStickY=2,0,0\nCStickX=AxisSlider\nKeyStart=Button99\nKeyStartX=Button1\n"
		"KeySelect=AxisZ\nStickY2=AxisX\n";
	hid_layout L;
	memset(&L, 0, sizeof L);
	hid_auto_map(&L, 0, &m);
	m.btn[HR_R1].kind = HID_SRC_INPUT;
	CHECK(hid_ini_map(ini, &m) == 9, "ini lines taken");
	CHECK(m.btn[HR_A].a == B(13) && m.btn[HR_B].a == H(0) && m.btn[HR_X].a == HID_IN_HAT(1, 3) &&
	      m.btn[HR_Y].a == AX(Z, 1), "ini inputs");
	CHECK(m.btn[HR_L1].kind == HID_SRC_BIT && m.btn[HR_L1].a == 5 && m.btn[HR_L1].b == 0x40, "ini bit");
	CHECK(m.btn[HR_R1].kind == HID_SRC_NONE, "ini None");
	CHECK(m.btn[HR_START].kind == HID_SRC_NONE && m.btn[HR_SELECT].kind == HID_SRC_NONE, "bad values ignored");
	CHECK(m.stick[HS_LX].kind == HID_SRC_INPUT && m.stick[HS_LX].a == HID_AX_RZ, "stick axis");
	CHECK(m.stick[HS_LY].kind == HID_SRC_BIT && m.stick[HS_LY].a == 2, "stick byte");
	CHECK(m.stick[HS_RX].a == HID_AX_SLIDER, "stick slider");
	/* the old forms read the same as the old reader did */
	uint8_t r[8] = { 0x80, 0xFF, 0x00, 0x80, 0, 0x40, 0, 0 };
	hid_state s = { 0 };
	CHECK(hid_map_buttons(&m, &s, r, 8) == 1u << HR_L1, "bit role");
	CHECK(hid_map_stick(&m, HS_LY, &s, r, 8) == -128 && hid_map_stick(&m, HS_LY, &s, r, 2) == 0, "byte stick");
	checks++;
}

/* Every shipped ini must read the old way: none of them may look new-style */
static void test_shipped(int n, char **paths)
{
	for (int i = 0; i < n; i++) {
		char buf[16384];
		FILE *f = fopen(paths[i], "rb");
		CHECK(f != NULL, "cannot read %s", paths[i]);
		if (!f) continue;
		size_t got = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
		buf[got] = 0;
		hid_ini_clean(buf, got);   /* as PADReadGC.c loads it */
		/* old-style unless it says Auto=1 (046D_C216: the automatic layout fits the pad) */
		if (strstr(buf, "Auto=1")) {
			checks++;
			CHECK(hid_ini_wants_auto(buf), "%s: Auto=1 not seen", paths[i]);
			continue;
		}
		CHECK(!hid_ini_wants_auto(buf), "%s reads as new-style", paths[i]);
		/* KeyL3/KeyR3, where a shipped ini has them, are old-style bits */
		hid_map m;
		memset(&m, 0, sizeof m);
		hid_ini_map(buf, &m);
		CHECK(m.btn[HR_L3].kind != HID_SRC_INPUT && m.btn[HR_R3].kind != HID_SRC_INPUT, "%s L3/R3", paths[i]);
	}
	printf("shipped inis: %d checked\n", n);
	CHECK(n > 20, "only %d inis given", n);
}

/* Junk descriptors and reports (a broken pad, a cut transfer): no crash, no read past len.
 * Build with -fsanitize=address to make an overrun fail loudly. */
static void test_junk(void)
{
	uint32_t x = 12345;
	for (int t = 0; t < 20000; t++) {
		uint8_t d[512], r[64];
		int n = 0, len;
		hid_layout L;
		hid_state s;
		hid_map m;
		/* half the time a real descriptor with junk spliced in, so parsing gets far */
		len = (x = x * 1103515245 + 12345) >> 16 & 511;
		for (int i = 0; i < len; i++) d[i] = (uint8_t)((x = x * 1103515245 + 12345) >> 16);
		if (t & 1) {
			static const uint8_t HEAD[] = { 0x05, 0x01, 0x09, 0x05, 0xA1, 0x01 };
			memcpy(d, HEAD, len < 6 ? len : 6);
		}
		n = hid_parse(d, len, &L);
		for (int i = 0; i < 64; i++) r[i] = (uint8_t)((x = x * 1103515245 + 12345) >> 16);
		hid_auto_map(&L, 0, &m);
		int rl = (x >> 16) % 65;
		hid_decode(&L, r, rl, &s);
		hid_map_buttons(&m, &s, r, rl);
		for (int k = 0; k < HS_NUM; k++) hid_map_stick(&m, k, &s, r, rl);
		(void)n;
	}
	checks++;
}

/* hid_ini_clean: comments and sections cannot match a key; the keys still read the same */
static void test_ini_clean(void)
{
	char t[] = "[KeyA test]\r\n; KeyA=4,10 old line\r\nVID=046D\r\n  # KeyB=1\r\n"
	           "KeyA=4,20 ; the bottom button\r\nKeyX=4,40#notacomment\r\nAuto=1\r\n";
	size_t n = strlen(t);
	hid_ini_clean(t, n);
	checks += 7;
	CHECK(strlen(t) == n, "clean keeps the length");
	CHECK(strstr(t, "KeyA") == strstr(t, "KeyA=4,20"), "first KeyA is the real line: %s", t);
	CHECK(strstr(t, "KeyB") == NULL, "commented KeyB gone: %s", t);
	CHECK(strstr(t, "the bottom") == NULL, "inline comment gone: %s", t);
	CHECK(strstr(t, "KeyX=4,40#notacomment") != NULL, "# inside a value kept: %s", t);
	CHECK(strstr(t, "VID=046D") && strstr(t, "Auto=1"), "keys kept: %s", t);
	CHECK(hid_ini_wants_auto(t), "Auto=1 still seen");
}

int main(int argc, char **argv)
{
	test_junk();
	for (size_t i = 0; i < sizeof PADS / sizeof PADS[0]; i++)
		test_pad(&PADS[i]);
	test_ini();
	test_ini_clean();
	test_shipped(argc - 1, argv + 1);
	printf("%d checks, %d failures\n", checks, fails);
	return fails != 0;
}
