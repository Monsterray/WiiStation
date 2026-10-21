/* unecm.c -- decode an ECM image (Neill Corlett's "Error Code Modeler" format) back to the
 * raw CD image it was made from. Written for WiiStation's test library (2026-10-21): Final
 * Fantasy VIII discs 2 and 3 came as .img.ecm, which the disc loader does not read.
 *
 *   clang -O2 -o unecm.exe tools/unecm/unecm.c
 *   unecm "Disc2.img.ecm" ["Disc2.img"]      (default: the name without .ecm)
 *
 * The format: "ECM\0", then records. A record header is a type (2 bits) and a count (5 bits,
 * then 7 more per continuation byte); count 0xFFFFFFFF ends the stream, otherwise count+1
 * items follow:
 *   type 0  that many raw bytes, copied as they are;
 *   type 1  Mode 1 sectors: 3 address bytes + 2048 data stored; the sync, the mode byte, the
 *           EDC and the ECC are rebuilt (2352 bytes out);
 *   type 2  Mode 2 Form 1 sectors without their 16-byte header: 4 subheader bytes + 2048 data
 *           stored; the subheader copy, EDC and ECC rebuilt (2336 bytes out);
 *   type 3  Mode 2 Form 2 sectors without their header: 4 subheader bytes + 2324 data stored;
 *           the subheader copy and EDC rebuilt (2336 bytes out).
 * After the end marker comes the EDC (CRC) of everything decoded, little-endian: a decode
 * that differs from the original by one bit fails it. The EDC/ECC are the CD-ROM ones
 * (ECMA-130): CRC polynomial 0xD8018001 (reflected), Reed-Solomon P (86x24) and Q (52x43)
 * parity over GF(2^8) with 0x11D. ECC is computed with the address bytes as zero for Mode 2.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t edc_lut[256];
static uint8_t ecc_f_lut[256], ecc_b_lut[256];

static void tables_init(void)
{
	uint32_t i, j, edc;
	for (i = 0; i < 256; i++) {
		j = (i << 1) ^ ((i & 0x80) ? 0x11D : 0);
		ecc_f_lut[i] = (uint8_t)j;
		ecc_b_lut[i ^ j] = (uint8_t)i;
		edc = i;
		for (j = 0; j < 8; j++)
			edc = (edc >> 1) ^ ((edc & 1) ? 0xD8018001u : 0);
		edc_lut[i] = edc;
	}
}

static uint32_t edc_block(uint32_t edc, const uint8_t *p, size_t n)
{
	while (n--)
		edc = (edc >> 8) ^ edc_lut[(edc ^ *p++) & 0xFF];
	return edc;
}

/* One parity set: major_count columns of minor_count bytes, walking the 2340-byte block
 * from 0xC with the CD's stride; the results land at dest[major] and dest[major+major_count]. */
static void ecc_block(const uint8_t *src, uint32_t major_count, uint32_t minor_count,
                      uint32_t major_mult, uint32_t minor_inc, uint8_t *dest)
{
	uint32_t size = major_count * minor_count, major, minor;
	for (major = 0; major < major_count; major++) {
		uint32_t index = (major >> 1) * major_mult + (major & 1);
		uint8_t a = 0, b = 0;
		for (minor = 0; minor < minor_count; minor++) {
			uint8_t t = src[index];
			index += minor_inc;
			if (index >= size)
				index -= size;
			a ^= t;
			b ^= t;
			a = ecc_f_lut[a];
		}
		a = ecc_b_lut[ecc_f_lut[a] ^ b];
		dest[major] = a;
		dest[major + major_count] = a ^ b;
	}
}

static void ecc_generate(uint8_t *s, int zero_address)
{
	uint8_t addr[4];
	if (zero_address) {
		memcpy(addr, s + 0xC, 4);
		memset(s + 0xC, 0, 4);
	}
	ecc_block(s + 0xC, 86, 24, 2, 86, s + 0x81C);    /* P */
	ecc_block(s + 0xC, 52, 43, 86, 88, s + 0x8C8);   /* Q */
	if (zero_address)
		memcpy(s + 0xC, addr, 4);
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void eccedc_generate(uint8_t *s, int type)
{
	switch (type) {
	case 1:   /* Mode 1 */
		put_le32(s + 0x810, edc_block(0, s, 0x810));
		memset(s + 0x814, 0, 8);
		ecc_generate(s, 0);
		break;
	case 2:   /* Mode 2 Form 1 */
		put_le32(s + 0x818, edc_block(0, s + 0x10, 0x808));
		ecc_generate(s, 1);
		break;
	case 3:   /* Mode 2 Form 2: EDC only */
		put_le32(s + 0x92C, edc_block(0, s + 0x10, 0x91C));
		break;
	}
}

static int get(FILE *f)
{
	int c = getc(f);
	if (c == EOF) {
		fprintf(stderr, "unecm: unexpected end of file\n");
		exit(1);
	}
	return c;
}

static void read_exact(void *p, size_t n, FILE *f)
{
	if (fread(p, 1, n, f) != n) {
		fprintf(stderr, "unecm: unexpected end of file\n");
		exit(1);
	}
}

int main(int argc, char **argv)
{
	FILE *in, *out;
	char outname[4096];
	uint8_t sector[2352], buf[4096], magic[4], tail[4];
	uint32_t edc = 0;
	unsigned long long written = 0, sectors = 0;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: unecm IN.ecm [OUT]\n");
		return 2;
	}
	if (argc == 3)
		snprintf(outname, sizeof outname, "%s", argv[2]);
	else {
		size_t n = strlen(argv[1]);
		if (n < 5 || strcmp(argv[1] + n - 4, ".ecm")) {
			fprintf(stderr, "unecm: %s does not end in .ecm; give the output name\n", argv[1]);
			return 2;
		}
		snprintf(outname, sizeof outname, "%.*s", (int)(n - 4), argv[1]);
	}
	tables_init();
	if (!(in = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
	read_exact(magic, 4, in);
	if (memcmp(magic, "ECM\0", 4)) { fprintf(stderr, "unecm: %s is not an ECM file\n", argv[1]); return 1; }
	if (!(out = fopen(outname, "wb"))) { perror(outname); return 1; }

	for (;;) {
		int c = get(in), bits = 5, type = c & 3;
		uint32_t num = (c >> 2) & 0x1F;
		while (c & 0x80) {
			c = get(in);
			num |= (uint32_t)(c & 0x7F) << bits;
			bits += 7;
		}
		if (num == 0xFFFFFFFFu)
			break;
		num++;
		if (num >= 0x80000000u) { fprintf(stderr, "unecm: corrupt record count\n"); return 1; }
		if (type == 0) {
			while (num) {
				size_t n = num > sizeof buf ? sizeof buf : num;
				read_exact(buf, n, in);
				edc = edc_block(edc, buf, n);
				fwrite(buf, 1, n, out);
				written += n;
				num -= (uint32_t)n;
			}
			continue;
		}
		while (num--) {
			memset(sector, 0, sizeof sector);
			memset(sector + 1, 0xFF, 10);   /* the sync pattern: 00 FF x10 00 */
			if (type == 1) {
				sector[0x0F] = 0x01;
				read_exact(sector + 0x00C, 0x003, in);
				read_exact(sector + 0x010, 0x800, in);
				eccedc_generate(sector, 1);
				edc = edc_block(edc, sector, 2352);
				fwrite(sector, 1, 2352, out);
				written += 2352;
			} else {
				sector[0x0F] = 0x02;
				read_exact(sector + 0x014, type == 2 ? 0x804 : 0x918, in);
				memcpy(sector + 0x10, sector + 0x14, 4);   /* the subheader is stored twice */
				eccedc_generate(sector, type);
				edc = edc_block(edc, sector + 0x10, 2336);
				fwrite(sector + 0x10, 1, 2336, out);
				written += 2336;
			}
			sectors++;
		}
	}
	read_exact(tail, 4, in);
	fclose(in);
	if (fclose(out)) { perror(outname); return 1; }
	{
		uint32_t want = tail[0] | (tail[1] << 8) | (tail[2] << 16) | ((uint32_t)tail[3] << 24);
		printf("%s: %llu bytes, %llu rebuilt sectors, checksum %08X %s\n", outname, written, sectors,
		       edc, edc == want ? "OK" : "MISMATCH");
		if (edc != want) {
			fprintf(stderr, "unecm: checksum mismatch (file says %08X): the output is wrong\n", want);
			return 1;
		}
	}
	return 0;
}
