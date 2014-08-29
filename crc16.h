/* crc16.h - crc16 cyclic redundancy check header */

/*
 * LICENSE:
 *
 *  crc16 - crc16 cyclic redundancy check header
 *  Copyright (c) 2003 Portland State Aerospace Society
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA.
 *
 * Portland State Aerospace Society (PSAS) is a student branch chapter
 * of the Institute of Electrical and Electronics Engineers Aerospace
 * and Electronics Systems Society. You can reach PSAS at
 * info@psas.pdx.edu.  See also http://psas.pdx.edu/
 */

/* crc16 GPL, adapted from linux kernel */

unsigned short crc16_byte(unsigned short crc, unsigned char c);
unsigned short crc16(unsigned short crc, const unsigned char *buffer,
    size_t len);
