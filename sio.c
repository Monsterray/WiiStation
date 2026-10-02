/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/*
* SIO functions.
*/

#include "sio.h"
#include "Gamecube/fileBrowser/fileBrowser.h"
#include "Gamecube/fileBrowser/fileBrowser-libfat.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "Gamecube/wiiSXconfig.h"
#include "Gamecube/PadSSSPSX.h"
#include "Gamecube/perf_prof.h"
extern unsigned char pad_unplug[2];   /* Gamecube/PlugPAD.c: a port shows as empty for these vblanks */

void netError(void); // defined below; used earlier in this file

// *** FOR WORKS ON PADS AND MEMORY CARDS *****

static unsigned char buf[256];
static unsigned char cardh1[4] = { 0xff, 0x08, 0x5a, 0x5d };
static unsigned char cardh2[4] = { 0xff, 0x08, 0x5a, 0x5d };

// Transfer Ready and the Buffer is Empty
// static unsigned short StatReg = 0x002b;
unsigned short StatReg = TX_RDY | TX_EMPTY;
unsigned short ModeReg;
unsigned short CtrlReg;
unsigned short BaudReg;

static unsigned int bufcount;
static unsigned int parp;
static unsigned int mcdst, rdwr;
static unsigned char adrH, adrL;
static unsigned int padst;

char mcd1Written = 0;
char mcd2Written = 0;

PadDataS pad;

#ifdef HW_RVL
#include "Gamecube/MEM2.h"
char *Mcd1Data = (char*)MCD1_LO;
char *Mcd2Data = (char*)MCD2_LO;
#else
char Mcd1Data[MCD_SIZE], Mcd2Data[MCD_SIZE];
#endif
char McdDisable[2];

// clk cycle byte
// 4us * 8bits = (PSXCLK / 1000000) * 32; (linuzappz)
// TODO: add SioModePrescaler and BaudReg
#define SIO_CYCLES		535

void sioWrite8(unsigned char value) {
	int more_data = 0;
#ifdef PAD_LOG
	PAD_LOG("sio write8 %x\n", value);
#endif
	PERF_INC(sio_write8);
	switch (padst) {
		case 1:
			if ((value&0x40) == 0x40) {
				padst = 2; parp = 1;
				if (!Config.UseNet) {
					switch (CtrlReg&0x2002) {
						case 0x0002:
							buf[parp] = PAD1_poll(value);
							break;
						case 0x2002:
							buf[parp] = PAD2_poll(value);
							break;
					}
				}

				/* FFh: the device does not take this command. It sends no /ACK, so no IRQ,
				 * and the transfer ends (psx-spx; DuckStation and MiSTer do the same). */
				if (buf[parp] == 0xFF) {
					bufcount = parp;
					padst = 0;
					return;
				}
				if (!(buf[parp] & 0x0f)) {
					bufcount = 2 + 32;
				} else {
					bufcount = 2 + (buf[parp] & 0x0f) * 2;
				}
				set_event(PSXINT_SIO, SIO_CYCLES);
			}
			else padst = 0;
			return;
		case 2:
			parp++;
			if (!Config.UseNet) {
				switch (CtrlReg&0x2002) {
					case 0x0002: buf[parp] = PAD1_poll(value); break;
					case 0x2002: buf[parp] = PAD2_poll(value); break;
				}
			}

			if (parp == bufcount) { padst = 0; return; }
			set_event(PSXINT_SIO, SIO_CYCLES);
			return;
	}

	switch (mcdst) {
		case 1:
			set_event(PSXINT_SIO, SIO_CYCLES);
			if (rdwr) { parp++; return; }
			parp = 1;
			switch (value) {
				case 0x52: rdwr = 1; break;
				case 0x57: rdwr = 2; break;
				default: mcdst = 0;
			}
			return;
		case 2: // address H
			set_event(PSXINT_SIO, SIO_CYCLES);
			adrH = value;
			*buf = 0;
			parp = 0;
			bufcount = 1;
			mcdst = 3;
			return;
		case 3: // address L
			set_event(PSXINT_SIO, SIO_CYCLES);
			adrL = value;
			*buf = adrH;
			parp = 0;
			bufcount = 1;
			mcdst = 4;
			return;
		case 4:
			set_event(PSXINT_SIO, SIO_CYCLES);
			parp = 0;
			switch (rdwr) {
				case 1: // read
					buf[0] = 0x5c;
					buf[1] = 0x5d;
					buf[2] = adrH;
					buf[3] = adrL;
					switch (CtrlReg & 0x2002) {
						case 0x0002:
							memcpy(&buf[4], Mcd1Data + (adrL | (adrH << 8)) * 128, 128);
							mcd1Written = 1;
							break;
						case 0x2002:
							memcpy(&buf[4], Mcd2Data + (adrL | (adrH << 8)) * 128, 128);
							mcd2Written = 1;
							break;
					}
					{
					char xor = 0;
					int i;
					for (i = 2; i < 128 + 4; i++)
						xor ^= buf[i];
					buf[132] = xor;
					}
					buf[133] = 0x47;
					bufcount = 133;
					break;
				case 2: // write
					buf[0] = adrL;
					buf[1] = value;
					buf[129] = 0x5c;
					buf[130] = 0x5d;
					buf[131] = 0x47;
					bufcount = 131;
					break;
			}
			mcdst = 5;
			return;
		case 5:
			parp++;
			if ((rdwr == 1 && parp == 132) ||
			    (rdwr == 2 && parp == 129)) {
				// clear "new card" flags
				if (CtrlReg & 0x2000)
					cardh2[1] &= ~8;
				else
					cardh1[1] &= ~8;
			}
			if (rdwr == 2) {
				if (parp < 128) buf[parp + 1] = value;
			}
			set_event(PSXINT_SIO, SIO_CYCLES);
			return;
	}

	switch (value) {
		case 0x01: // start pad
		case 0x02: // start pad
		case 0x03: // start pad
		case 0x04: // start pad
			StatReg |= RX_RDY;		// Transfer is Ready

			if (!Config.UseNet) {
				switch (CtrlReg&0x2002) {
					case 0x0002:
						if (padType[0] && !pad_unplug[0]){
							PERF_INC(sio_start);
							SSS_SetMultiPad(0, value);
							buf[0] = PAD1_startPoll(1);
							break;
						}
						else{
							buf[0] = 0xff;
							parp = 0;
							bufcount = 0;
							return;
						}

					case 0x2002:
						if (padType[1] && !pad_unplug[1]){
							SSS_SetMultiPad(1, value);
							buf[0] = PAD1_startPoll(2);
							break;
						}
						else{
							buf[0] = 0xff;
							parp = 0;
							bufcount = 0;
							return;
						}
				}
			} else {
				if ((CtrlReg & 0x2002) == 0x0002) {
					int i, j;

					PAD1_startPoll(1);
					buf[0] = 0;
					buf[1] = PAD1_poll(0x42);
					if (!(buf[1] & 0x0f)) {
						bufcount = 32;
					} else {
						bufcount = (buf[1] & 0x0f) * 2;
					}
					buf[2] = PAD1_poll(0);
					i = 3;
					j = bufcount;
					while (j--) {
						buf[i++] = PAD1_poll(0);
					}
					bufcount+= 3;

					if (NET_sendPadData(buf, bufcount) == -1)
						netError();

					if (NET_recvPadData(buf, 1) == -1)
						netError();
					if (NET_recvPadData(buf+128, 2) == -1)
						netError();
				} else {
					memcpy(buf, buf+128, 32);
				}
			}

			bufcount = 2;
			parp = 0;
			padst = 1;
			set_event(PSXINT_SIO, SIO_CYCLES);
			return;
		case 0x81: // start memcard
		case 0x82: // start memcard
		case 0x83: // start memcard
		case 0x84: // start memcard
			if (CtrlReg & 0x2000)
			{
				if (memCard[1] == MEMCARD_DISABLE || McdDisable[1])
					goto no_device;
				memcpy(buf, cardh2, 4);
			}
			else
			{
				if (memCard[0] == MEMCARD_DISABLE || McdDisable[0])
					goto no_device;
				memcpy(buf, cardh1, 4);
			}
			StatReg |= RX_RDY;
			parp = 0;
			bufcount = 3;
			mcdst = 1;
			rdwr = 0;
			set_event(PSXINT_SIO, SIO_CYCLES);
			return;
		default:
		no_device:
			StatReg |= RX_RDY;
			buf[0] = 0xff;
			parp = 0;
			bufcount = 0;
			return;
	}
}

void sioWriteCtrl16(unsigned short value) {
	PERF_INC(sio_ctrl16);
	CtrlReg = value & ~RESET_ERR;
	if (value & RESET_ERR) StatReg &= ~IRQ;
	if ((CtrlReg & SIO_RESET) || !(CtrlReg & DTR)) {
		padst = 0; mcdst = 0; parp = 0;
		StatReg = TX_RDY | TX_EMPTY;
		psxRegs.interrupt &= ~(1 << PSXINT_SIO);
	}
}

unsigned char sioRead8() {
	unsigned char ret = 0;

	PERF_INC(sio_read8);
	if ((StatReg & RX_RDY)/* && (CtrlReg & RX_PERM)*/) {
//		StatReg &= ~RX_OVERRUN;
		ret = buf[parp];
		if (parp == bufcount) {
			StatReg &= ~RX_RDY;		// Receive is not Ready now
			if (mcdst == 5) {
				mcdst = 0;
				if (rdwr == 2) {
					switch (CtrlReg & 0x2002) {
						case 0x0002:
							memcpy(Mcd1Data + (adrL | (adrH << 8)) * 128, &buf[1], 128);
							mcd1Written = 1;
							break;
						case 0x2002:
							memcpy(Mcd2Data + (adrL | (adrH << 8)) * 128, &buf[1], 128);
							mcd2Written = 1;
							break;
					}
				}
			}
			if (padst == 2) padst = 0;
			if (mcdst == 1) {
				mcdst = 2;
				StatReg|= RX_RDY;
			}
		}
	}

#ifdef PAD_LOG
	PAD_LOG("sio read8 ;ret = %x\n", ret);
#endif
	return ret;
}

void netError() {
	ClosePlugins();
	SysMessage(_("Connection closed!\n"));

	CdromId[0] = '\0';
	CdromLabel[0] = '\0';

	SysRunGui();
}

void sioInterrupt() {
#ifdef PAD_LOG
	PAD_LOG("Sio Interrupt (CP0.Status = %x)\n", psxRegs.CP0.n.Status);
#endif
	PERF_INC(sio_irq);
//	SysPrintf("Sio Interrupt\n");
	if (!(StatReg & IRQ)) {
		StatReg |= IRQ;
		psxHu32ref(0x1070) |= SWAPu32(0x80);
	}
}

// Detect common memory-card containers: raw 128 KiB, +64 B header
// (e.g. single .mcr header), +3904 B header (e.g. Adrenaline/PSP exports).
// Returns the data offset for a given file size, or 0 for raw/unknown.
static unsigned int McdDataOffsetForSize(long size) {
	if (size == (long)(MCD_SIZE + 64)) return 64;
	if (size == (long)(MCD_SIZE + 3904)) return 3904;
	return 0;
}

/* The file one card lives in. Per game it carries the disc's own id, so each game has its
 * own card; shared, it has a fixed name that every game opens. Card 2's shared name stays
 * "slot2.mcd" and card 1's per-game name stays "<CdromId>.mcd", so nobody's existing card
 * is left behind by this setting. */
static void McdFileName(char *out, int size, int mcd, const char *dir)
{
	int perGame = memCardFile[mcd - 1] == MEMCARDFILE_PER_GAME;
	if (mcd == 1)
		snprintf(out, size, perGame ? "%s/%s.mcd" : "%s/shared1.mcd", dir, CdromId);
	else
		snprintf(out, size, perGame ? "%s/%s-2.mcd" : "%s/slot2.mcd", dir, CdromId);
}

//call me from menu, takes slot and save path as args
/* A chained autoboot (Gamecube/GamecubeMain.cpp) deletes, when it ends, every memory card
 * file its games loaded or saved: it runs on a development card, and it should leave the
 * next chain the same fresh start it had. Off until mcd_track_begin(). */
#define MCD_TRACK_MAX 16
static char mcd_track_name[MCD_TRACK_MAX][256];
static int mcd_track_n = -1;

void mcd_track_begin(void)
{
	if (mcd_track_n < 0)
		mcd_track_n = 0;
}

static void mcd_track_note(const char *name)
{
	int i;
	if (mcd_track_n < 0)
		return;
	for (i = 0; i < mcd_track_n; i++)
		if (!strcmp(mcd_track_name[i], name))
			return;
	if (mcd_track_n < MCD_TRACK_MAX)
		snprintf(mcd_track_name[mcd_track_n++], sizeof mcd_track_name[0], "%s", name);
}

void mcd_track_delete(void)
{
	int i;
	for (i = 0; i < mcd_track_n; i++)
		remove(mcd_track_name[i]);
	mcd_track_n = -1;
}

int LoadMcd(int mcd, fileBrowser_file *savepath) {
	int temp = 0;
	bool ret = 0;
	char *data = NULL;
  fileBrowser_file saveFile;
	memcpy(&saveFile, savepath, sizeof(fileBrowser_file));
	memset(&saveFile.name[0],0,FILE_BROWSER_MAX_PATH_LEN);

	McdFileName((char*)saveFile.name, FILE_BROWSER_MAX_PATH_LEN, mcd, savepath->name);
	mcd_track_note((char*)saveFile.name);
	if(mcd == 1) {
	  data = &Mcd1Data[0];
	  cardh1[1] |= 8; // mark as new
	}
	if (mcd == 2) {
	  data = &Mcd2Data[0];
	  cardh2[1] |= 8;
	}

	if(saveFile_readFile(&saveFile, &temp, 4) == 4) {  //file exists
		struct stat st;
		unsigned int dataOff = 0;
		if (stat((char*)saveFile.name, &st) == 0)
			dataOff = McdDataOffsetForSize((long)st.st_size);
		saveFile.offset = dataOff;
		if(saveFile_readFile(&saveFile, data, MCD_SIZE)==MCD_SIZE)
		  ret = 1;
	}
	else {
		if(CreateMcd(mcd, &saveFile)) {  //created ok
		  saveFile.offset = 0;
			if(saveFile_readFile(&saveFile, data, MCD_SIZE)==MCD_SIZE)
			  ret = 1;
		}
	}
	return ret;
}

// add xjsxjs197 start
int SaveMcdByNum(int mcd) {
    if (saveFile_dir)
	{
	    return SaveMcd(mcd, saveFile_dir);
	}
    else
	{
	    return -1;
	}
}
// add xjsxjs197 end

//call me from menu, takes slot and save path as args
int SaveMcd(int mcd, fileBrowser_file *savepath) {
  unsigned long long t0 = perf_now_us();
  bool ret = 0;
  char *data = NULL;
  fileBrowser_file saveFile;

	memcpy(&saveFile, savepath, sizeof(fileBrowser_file));
	memset(&saveFile.name[0],0,FILE_BROWSER_MAX_PATH_LEN);

	McdFileName((char*)saveFile.name, FILE_BROWSER_MAX_PATH_LEN, mcd, savepath->name);
	mcd_track_note((char*)saveFile.name);
	if(mcd == 1) data = &Mcd1Data[0];
	if (mcd == 2) data = &Mcd2Data[0];

  /* Preserve an existing +64/+3904 header so imported saves keep their
   * container format; new cards stay raw MCD_SIZE. */
  {
    struct stat st;
    unsigned int dataOff = 0;
    if (stat((char*)saveFile.name, &st) == 0)
      dataOff = McdDataOffsetForSize((long)st.st_size);
    if (dataOff != 0) {
      fileBrowser_file hdrFile;
      memcpy(&hdrFile, &saveFile, sizeof(hdrFile));
      hdrFile.offset = 0;
      /* Read-modify-write via a temp: keep header bytes, replace card data. */
      char *tmp = (char*)malloc(dataOff + MCD_SIZE);
      if (tmp) {
        if (saveFile_readFile(&hdrFile, tmp, dataOff) == (int)dataOff) {
          memcpy(tmp + dataOff, data, MCD_SIZE);
          hdrFile.offset = 0;
          if (saveFile_writeFile(&hdrFile, tmp, dataOff + MCD_SIZE) == (int)(dataOff + MCD_SIZE))
            ret = 1;
          free(tmp);
          return ret;
        }
        free(tmp);
      }
      /* The header could not be read, or there was no memory for it. Write the card in
       * the plain format instead: the container is worth keeping, the save is worth more.
       * This path used to return failure and write nothing at all. */
    }
  }

  if(saveFile_writeFile(&saveFile, data, MCD_SIZE)==MCD_SIZE)
    ret = 1;

  PERF_INC(mcd_saves);
  PERF_ADD(mcd_save_us, perf_now_us() - t0);
  if (!ret) PERF_INC(mcd_fails);
  return ret;
}

bool CreateMcd(int slot, fileBrowser_file *mcd) {
	char *cardData;
	if (slot == 1) cardData = Mcd1Data;
	else /*(slot == 2)*/ cardData = Mcd2Data;

	int i=0, j=0, curPos =0;

	// setup header
	cardData[curPos++] = 'M';
	cardData[curPos++] = 'C';
	for(i=0; i<125; i++)
	  cardData[curPos++] = 0;
	cardData[curPos++] = 0x0E;

	// 15 blocks
	for(i=0;i<15;i++) {
		cardData[curPos++] = 0xA0;
		for(j=0;j<126;j++) {
			cardData[curPos++] = 0;
		}
		cardData[curPos++] = 0xA0;
	}

	//blank out the rest
	for(i = curPos; i < MCD_SIZE; i++)
	  cardData[i] = 0;
	if(saveFile_writeFile(mcd, cardData, MCD_SIZE)==MCD_SIZE)
	  return 1;
	return 0;
}

void ConvertMcd(char *mcd, char *data) {
	/*FILE *f;
	int i=0;
	int s = MCD_SIZE;

	if (strstr(mcd, ".gme")) {
		f = fopen(mcd, "wb");
		if (f != NULL) {
			fwrite(data-3904, 1, MCD_SIZE+3904, f);
			fclose(f);
		}
		f = fopen(mcd, "r+");
		s = s + 3904;
		fputc('1', f); s--;
		fputc('2', f); s--;
		fputc('3', f); s--;
		fputc('-', f); s--;
		fputc('4', f); s--;
		fputc('5', f); s--;
		fputc('6', f); s--;
		fputc('-', f); s--;
		fputc('S', f); s--;
		fputc('T', f); s--;
		fputc('D', f); s--;
		for(i=0;i<7;i++) {
			fputc(0, f); s--;
		}
		fputc(1, f); s--;
		fputc(0, f); s--;
		fputc(1, f); s--;
		fputc('M', f); s--;
		fputc('Q', f); s--;
		for(i=0;i<14;i++) {
			fputc(0xa0, f); s--;
		}
		fputc(0, f); s--;
		fputc(0xff, f);
		while (s-- > (MCD_SIZE+1)) fputc(0, f);
		fclose(f);
	} else if(strstr(mcd, ".mem") || strstr(mcd,".vgs")) {
		f = fopen(mcd, "wb");
		if (f != NULL) {
			fwrite(data-64, 1, MCD_SIZE+64, f);
			fclose(f);
		}
		f = fopen(mcd, "r+");
		s = s + 64;
		fputc('V', f); s--;
		fputc('g', f); s--;
		fputc('s', f); s--;
		fputc('M', f); s--;
		for(i=0;i<3;i++) {
			fputc(1, f); s--;
			fputc(0, f); s--;
			fputc(0, f); s--;
			fputc(0, f); s--;
		}
		fputc(0, f); s--;
		fputc(2, f);
		while (s-- > (MCD_SIZE+1)) fputc(0, f);
		fclose(f);
	} else {
		f = fopen(mcd, "wb");
		if (f != NULL) {
			fwrite(data, 1, MCD_SIZE, f);
			fclose(f);
		}
	}*/
}

//void GetMcdBlockInfo(int mcd, int block, McdBlock *Info) {
//	char *data = NULL, *ptr, *str;
//	unsigned short clut[16];
//	int i, x;
//
//	memset(Info, 0, sizeof(McdBlock));
//
//	str = Info->Title;
//
//	if (mcd == 1) data = Mcd1Data;
//	if (mcd == 2) data = Mcd2Data;
//
//	ptr = data + block * 8192 + 2;
//
//	Info->IconCount = *ptr & 0x3;
//
//	ptr+= 2;
//
//	i=0;
//	memcpy(Info->sTitle, ptr, 48*2);
//
//	for (i=0; i < 48; i++) {
//		unsigned short c = *(ptr) << 8;
//		c|= *(ptr+1);
//		if (!c) break;
//
//		// Convert ASCII characters to half-width
//		if (c >= 0x8281 && c <= 0x829A)
//			c = (c - 0x8281) + 'a';
//		else if (c >= 0x824F && c <= 0x827A)
//			c = (c - 0x824F) + '0';
//		else if (c == 0x8140) c = ' ';
//		else if (c == 0x8143) c = ',';
//		else if (c == 0x8144) c = '.';
//		else if (c == 0x8146) c = ':';
//		else if (c == 0x8147) c = ';';
//		else if (c == 0x8148) c = '?';
//		else if (c == 0x8149) c = '!';
//		else if (c == 0x815E) c = '/';
//		else if (c == 0x8168) c = '"';
//		else if (c == 0x8169) c = '(';
//		else if (c == 0x816A) c = ')';
//		else if (c == 0x816D) c = '[';
//		else if (c == 0x816E) c = ']';
//		else if (c == 0x817C) c = '-';
//		else {
//			c = ' ';
//		}
//
//		str[i] = c;
//		ptr+=2;
//	}
//	str[i] = 0;
//
//	ptr = data + block * 8192 + 0x60; // icon palete data
//
//	for (i=0; i<16; i++) {
//		clut[i] = *((unsigned short*)ptr);
//		ptr+=2;
//	}
//
//	for (i=0; i<Info->IconCount; i++) {
//		short *icon = &Info->Icon[i*16*16];
//
//		ptr = data + block * 8192 + 128 + 128 * i; // icon data
//
//		for (x=0; x<16*16; x++) {
//			icon[x++] = clut[*ptr & 0xf];
//			icon[x]   = clut[*ptr >> 4];
//			ptr++;
//		}
//	}
//
//	ptr = data + block * 128;
//
//	Info->Flags = *ptr;
//
//	ptr+= 0xa;
//	strncpy(Info->ID, ptr, 12);
//	Info->ID[12] = 0;
//	ptr+= 12;
//	strncpy(Info->Name, ptr, 16);
//}

/* Called from psxHwReset(). Puts the port back as it is at power-on: nothing reset it before,
 * so a game started while the last one was part way through a pad or card transfer. */
void sioReset(void) {
#ifdef PERF_PROF
	g_carry.sio[0] = StatReg;
	g_carry.sio[1] = CtrlReg;
	g_carry.sio[2] = padst;
	g_carry.sio[3] = parp;
#endif
	memset(buf, 0, sizeof(buf));
	StatReg = TX_RDY | TX_EMPTY;
	ModeReg = CtrlReg = BaudReg = 0;
	bufcount = parp = mcdst = rdwr = padst = 0;
	adrH = adrL = 0;
	memset(&pad, 0, sizeof(pad));
}

int sioFreeze(gzFile f, int Mode) {
	char Unused[4096];

	gzfreezel(buf);
	gzfreezel(&StatReg);
	gzfreezel(&ModeReg);
	gzfreezel(&CtrlReg);
	gzfreezel(&BaudReg);
	gzfreezel(&bufcount);
	gzfreezel(&parp);
	gzfreezel(&mcdst);
	gzfreezel(&rdwr);
	gzfreezel(&adrH);
	gzfreezel(&adrL);
	gzfreezel(&padst);
	gzfreezel(Unused);

	return 0;
}
