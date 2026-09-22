/*  Pcsx - Pc Psx Emulator
 *  Copyright (C) 1999-2016  Pcsx Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses>.
 */

#ifndef __GTE_DIVIDER_H__
#define __GTE_DIVIDER_H__

/* stdint, not psxcommon.h: these are the same types (u32 is uint32_t) and this way
 * tests/gte_divider_test.c can build the divider on the host. */
#include <stdint.h>

/* H / SZ as the GTE itself computes it. Returns 0xffffffff when the result does not fit,
 * which the caller's limE() turns into the divide-overflow flag. */
uint32_t DIVIDE(uint16_t n, uint16_t d);

#endif /* __GTE_DIVIDER_H__ */
