/*
 * Definitions for the Coraid Ethenret Console protocol
 *
 * Linux Ethernet Console
 *
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
 *
 */
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

enum {
	Tinita = 0,
	Tinitb,
	Tinitc,
	Tdata,
	Tack,
	Tdiscover,
	Toffer,
	Treset,
	
	HDRSIZ = 18,
	WAITSECS= 2,	// seconds to wait for various ops (probe, connection, etc)

	CEC_ETYPE = 0xBCBC,
	Ntab = 1000,
	MAX_PAYLOAD = 255,
};

/*
 * CEC packet format
 */
struct Pkt {
	uchar		dst[6];
	uchar		src[6];
	unsigned short	etype;
	uchar		type;
	uchar		conn;
	uchar		seq;
	uchar		len;
	uchar		data[MAX_PAYLOAD+1];
};

struct Shelf {
  char	ea[6];
  int	shelfno;
  char	*str;
  struct Shelf *next;
};

/* For sysdep */
int netopen(char *name);
int netclose(void);
int netsend(void *, int);
int netrecv(void);
int netget(void *, int);
int netup(char *);
void rawon(void);
void rawoff(void);

/* For utils */
void dump(char *, int);

enum { FQUOTE = (1<<0), FEMPTY = (1<<1) };
int getfields(char *, char **, int, char *, int);
char *htoa(char *, char *, uint);
int parseether(char *, char *);

#define tokenize(A, B, C) getfields((A), (B), (C), " \t\r\n", FQUOTE)
void fatal(char *);

/* cec.c */
void timewait(int);
void freeprobe(struct Shelf *s);
struct Shelf *cec_probe(int waitsecs,int shelf,char *shelfea);
int cec_Treset(uchar *ea,int conn);
int cec_Tdata(uchar *ea,int conn,int seq,char *str);
