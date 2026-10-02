/*	PADwin
 *	Copyright (C) 2002-2004  PADwin Team
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __PADSSS_H__
#define __PADSSS_H__

typedef struct
{
	u32 key;
	u32 event;
} keyEvent;

typedef struct
{
	u32 keys[2][21];
} SSSConfig;

void lightgunInterrupt(void);

/* sio.c: a transfer on PlayStation port 0 or 1, a byte at a time. SSS_PortStart takes the
 * first (address) byte, SSS_PortByte each byte after it; both return the reply, and *ack
 * says whether the device acknowledged, that is, takes another byte. A plain port answers
 * address 01h; a multitap answers 01h..04h and the long read of all four slots. */
unsigned char SSS_PortStart(int port, unsigned char addr, int *ack);
unsigned char SSS_PortByte(int port, unsigned char value, int *ack);

#endif
