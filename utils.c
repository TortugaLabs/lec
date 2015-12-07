/*
 *
 * Some utility functions
 *
 * Linux Ethernet Console
 *
 * Copyright Coraid, Inc. 2006.  All rights reserved.
 * Copyright (C) 2009-2011 Alejandro Liu Ly <alejandro_liu@hotmail.com>
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <termios.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "cec.h"

void
dump(char *ap, int len)
{
	int i;
	unsigned char *p = (unsigned char *) ap;

	for (i = 0; i < len; i++)
		fprintf(stderr, "%2.2X%s", p[i], (i+1)%16 ? " " : "\r\n");
	fprintf(stderr, "\r\n");
}

char *
htoa(char *to, char *frm, uint len)
{
	char *cp = to;
	uchar ch;

	for(; len > 0; len--, frm++) {
		ch = *frm;
		*to++ = (ch>>4) + (ch>>4 > 9 ? '7' : '0');
		*to++ = (ch&0x0f) + ((ch&0x0f) > 9 ? '7' : '0');
	}
	*to = 0;
	return cp;
}

/* parseether from plan 9 */
int
parseether(char *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for(i = 0; i < 6; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}

/* rc style quoting and/or empty fields */
int
getfields(char *p, char **argv, int max, char *delims, int flags)
{
	uint n;

	n=0;
loop:
	if(n >= max || *p == '\0')
		return n;
	if(strchr(delims, *p)) {
		if(flags & FEMPTY) {
			*p = '\0';
			argv[n++] = p;
		}
		p++;
		goto loop;
	}

	switch(*p) {
	case '\'':
		if(flags & FQUOTE) {
			argv[n++] = ++p;
unq:
			p = strchr(p, '\'');
			if(p == NULL)
				return n;
			if(p[1] == '\'') {
				strcpy(p, p+1); /* too inefficient? */
				p++;
				goto unq;
			}
			break;
		}
	default:
		argv[n++] = p;
		do {
			if(*++p == '\0')
				return n;
		} while(!strchr(delims, *p));
	}
	*p++ = '\0';
	goto loop;
}

void fatal(char *s) {
  perror(s);
  exit(1);
}
