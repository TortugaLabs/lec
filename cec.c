/*
 * Linux Ethernet Console
 *  ethernet console client for Coraid storage products and LEC
 *  simple command line version 
 *
 * Copyright Coraid, Inc. 2006.  All Rights Reserved.
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
 *++
 * = CEC(8)
 * :Revision: 2.0
 * :Author: A Liu Ly
 * 
 * == NAME
 *
 * cec - Coraid Ethernet Console (Client)
 *
 * == SYNOPSIS
 *
 * *cec* _[-s shelf]_ _[-m mac]_ _eth_
 *
 * == DESCRIPTION
 *
 * The *cec* command uses raw sockets to connect to a CEC
 * server for console access.  All clients share the same session.
 *
 * A *cec* process starts by probing the specified network interface
 * (e.g., "eth0") for available CEC servers.  If a cec server is
 * specified using the *-s* or *-m* flags, the connection will
 * proceed immediately upon discovery of the first CEC server
 * matching the specification.  Otherwise, a selection prompt
 * will be displayed showing the discovered cec servers
 * available for communication.  Servers found on multiple
 * destination mac addresses will be listed in a comma
 * separated list as follows.  If no servers are discovered,
 * cec exits.
 *
 *    :  SHELF | EA
 *    :  5       003048865F1E,003048865F1F
 *    :  [#qp]: 
 *
 * The selection prompt accepts ...
 *
 * *shelf,* for the shelf number of the CEC server to connect to
 * (from the first column)
 *
 * *shelf* *ea,* for the shelf and specific mac interface to connect
 * to.
 *
 * *p* directs *cec* to probe the interface again.
 *
 * *q* directs *cec* to quit.
 *
 * If a mac address is not specified, the first mac address
 * listed for a shelf will be used.
 *
 * Once connected to a cec server, entering the escape sequence
 * will drop the user into an escape prompt where the user may
 * type 'q' to quit the connection, 'i' to send the escape sequence
 * across the connection, or '.' to continue the connection.
 * The escape sequence is printed on connection.
 *
 * == OPTIONS
 *
 * * *-d*::
 *   The -d flag causes *cec* to output copious debugging information.
 *   Only for the strong of heart.
 * * *-e*::
 *   The -e flag takes an argument, a character e to be used as the base for
 *   the escape sequence, e.g., ^e.  The character must be a-y, inclusive.
 * * *-m* _mac_::
 *   The -m flag takes an argument, the mac address of the desired CEC 
 *   server.
 * * *-p*::
 *    The -p flag causes *cec* to probe the specified interface, print
 *    the list of discovered servers as formatted in the selection 
 *    interface, and exit.
 *    The -p flag also sets the -q flag to make the output easier to parse
 *    in scripts.
 * * *-q*::
 *    The -q flag causes \fBcec\fP to operate in quiet mode, removing output
 *    letting the user know about actions being performed (probing, 
 *    connecting, etc).
 * * *-s* _shelf_::
 *   The -s flag takes an argument, the shelf address of the desired CEC
 *   server.
 * * *-v*::
 *   The -v flag reports the cec version and exits.
 * *-w* _secs_::
 *    The -w flag takes an argument, the number of seconds to use as a
 *    timeout.  This timeout defaults to 2, and governs how long to wait 
 *    on probe, connection, and communication timeout.  It must be greater
 *    than 0.
 * *-?*::
 *    The -? flag prints the cec usage and exits.
 *
 * == NOTES
 *
 * The _cec_ command must be run as root to obtain raw ethernet access.
 *
 * If the -s or -m flags are used cec will exit upon closing the
 * connection.  Otherwise, cec will return to the selection prompt 
 * upon connection close.
 *
 * Some say that *cec* is pronounced _"kick."_
 *
 *== SEE ALSO
 *
 * *cecd(8)*
 *
 * ==  AUTHORS
 *
 * Brantley Coile, Ed Cashin, Erik Quanstrom, Sam Hopkins
 *--
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/errno.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "cec.h"

#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define nil ((void *)0)
#define vprintf(...) if (qflag) ; else fprintf(stderr, __VA_ARGS__)

typedef struct Shelf Shelf;
typedef struct Pkt Pkt;

void	exits(char *);
void 	probe(void);
int 	pickone(void);
void 	conn(int);
void 	gettingkilled(int);
void	sethdr(Pkt *, int);
void	showtable(int);

extern int errno;

Shelf	tab[Ntab];
int	ntab;
uchar	contag;
int	shelf;
Shelf	*connp;
char 	esc = '';
int	mflag;
int	sflag;
int	pflag;
int	qflag;
char	shelfea[6];
int	waitsecs = WAITSECS;

#ifndef VERSION
#define VERSION "0.00"
#endif
int debug = 0;
char *progname = "cec";
extern int netfd;	/* set in netopen */

void
usage(void)
{
	fprintf(stderr, "usage: %s [-s shelf] [-m mac] interface\n", progname);
	exits("usage");
}

int
main(int argc, char **argv)
{
	int ch, r, n;
	
	progname = *argv;
	while ((ch = getopt(argc, argv, "de:m:pqs:vw:?")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'e':
			esc = toupper(*optarg) - 'A' + 1;
			if(esc < 1 || esc > 0x19) {
				fprintf(stderr, "Escape character out of range.\n");
				usage();
			}
			break;
		case 'm':
			mflag++;
			if (parseether(shelfea, optarg) < 0) {
				fprintf(stderr, "Bad mac address %s.\n", optarg);
				usage();
			}
			break;
		case 'p':
			qflag++;	// assume non chatty
			pflag++;
			break;
		case 'q':		// quiet
			qflag++;
			break;
		case 's':
			sflag++;
			shelf = atoi(optarg);
			break;
		case 'v':
			fprintf(stderr, "%s\n", VERSION);
			return 0;
		case 'w':
			waitsecs = atoi(optarg);
			if (waitsecs <= 0) {
				fprintf(stderr, "Invalid w value, ignoring.\n");
				waitsecs = WAITSECS;
			}
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (debug)
		fprintf(stderr, "debug is on\n");
	if (argc != 1)
		usage();
	r = netopen(*argv);
	if (r == -1){
		fprintf(stderr, "%s: can't netopen %s\n", progname, *argv);
		exits("open");
	}
	probe();
	if (pflag) {
		showtable(0);
		return 0;
	}
loop:
	n = sflag|mflag ? 0 : pickone();
	rawon();
	signal(SIGTERM, gettingkilled);
	signal(SIGHUP, gettingkilled);
	signal(SIGKILL, gettingkilled);
	conn(n);
	rawoff();
	if (sflag|mflag)
		return 0;
	goto loop;
}


void
shinsert(Shelf *s)
{
	Shelf *p, *e;

	if (ntab >= Ntab)
		return;
	p = tab;
	e = p + ntab;
	for (; p<e; p++) {
		if (p->shelfno == s->shelfno
		|| (p->shelfno > s->shelfno))
			break;
	}
	memmove(p+1, p, (char *)e - (char *)p);
	*p = *s;
	ntab++;
}

void
probe(void)
{
	uchar buf[1500];
	Pkt q;
	char *sh, *other;
	Shelf s;
	int n;
	
	ntab = 0;
	memset(buf, 0xff, 6);
	memset(q.dst, 0xff, 6);
	memset(q.src, 0, 6);
	q.etype = htons(CEC_ETYPE);
	q.type = Tdiscover;
	q.len = 0;
	q.conn = 0;
	q.seq = 0;
	netsend(&q, 60);
	vprintf("Probing for shelves ... ");
	fflush(stderr);
	timewait(waitsecs);
	for (;;) {
		n = netrecv();
		if (n < 0 && errno == EINTR) {
			alarm(0);
			break;
		}
		while ((n = netget(&q, sizeof q)) > 0) {
			if (n < 60)
				continue;
			if (ntohs(q.etype) != CEC_ETYPE)
				continue;
			if (ntab >= nelem(tab))
				continue;
			if (memcmp(q.dst, "\xff\xff\xff\xff\xff\xff", 6) == 0)
				continue;
			if (q.type != Toffer)
				continue;
			if (q.len == 0)
				continue;
			q.data[q.len] = 0;
			sh = strtok((char *)q.data, " \t");
			if (sh == NULL)
				continue;
			if (sflag && atoi(sh) != shelf)
				continue;
			if (mflag && memcmp(shelfea, q.src, 6))
				continue;
			other = strtok(NULL, "\x1");
			memcpy(s.ea, q.src, 6);
			s.shelfno = atoi(sh);
			s.str = other ? strdup(other) : "";
			shinsert(&s);
			if (sflag || mflag) {
				vprintf("shelf %d found.\n", s.shelfno);
				return;
			}
		}
	}
	if (ntab == 0) {
		vprintf("none found.\n");
		exits("none found");
	}
	vprintf("done.\n");
}

void
showtable(int header)
{
	Shelf *sh, *e;
	char aea[16];

	if (header)
		printf("SHELF | EA            | DESC\n");
	if (ntab == 0)
		return;

	aea[12] = '\0';
	sh = tab;
	e = sh + ntab;
	for (; sh<e; sh++) {
		htoa(aea, sh->ea, 6);
		switch (sh == tab) {
		case 0:
			if (sh[-1].shelfno != sh->shelfno) {
				printf("\n");
		default:	
		  printf("%-5d   %s    %s", sh->shelfno, aea,sh->str);
				continue;
			}
			break;
		}
		printf(",%s", aea);
	}
	printf("\n");
}

// shspec is a shelf id followed by an optional mac
int
shelfid(char *shspec)
{
	int argc, i, sh;
	char *argv[2];
	char ea[6];

	argc = tokenize(shspec, argv, nelem(argv));	
	switch (argc) {
	case 2:
		if (parseether(ea, argv[1]) < 0)
			break;
	case 1:
		sh = atoi(argv[0]);
		for (i=0; i<ntab; i++) {
			if (tab[i].shelfno == sh)
			if (argc == 1 || !memcmp(ea, tab[i].ea, sizeof ea))
				return i;
		}
	default:
		break;
	}
	return -1;
}
		
int
pickone(void)
{
	char buf[80];
	int n;

	showtable(1);
	for (;;) {
		printf("[#qp]: ");
		fflush(stdout);
		n = read(0, buf, sizeof buf);
		switch (n) {
		case 1:
			if (buf[0] == '\n')
				continue;
		case 2:
			if (buf[0] == 'p') {
				probe();
				break;
			}
			if (buf[0] == 'q')
		case 0:
		case -1:
				exits(0);
		}
		buf[n] = 0;
		n = shelfid(buf);
		if (n >= 0)
			return n;
		showtable(1);
	}
}

void
sethdr(Pkt *pp, int type)
{
	memmove(pp->dst, connp->ea, 6);
	memset(pp->src, 0, 6);
	pp->etype = htons(CEC_ETYPE);
	pp->type = type;
	pp->len = 0;
	pp->conn = contag;
}
	
void
ethclose(void)
{
	Pkt msg;
	
	sethdr(&msg, Treset);
	timewait(waitsecs);
	netsend(&msg, 60);
	alarm(0);
	connp = 0;
}

int
cecconnect(void)
{
	Pkt pk;
	int cnt, n;

	sethdr(&pk, Tinita);
	netsend(&pk, 60);
	
	/*  wait for INITB */
	
	fflush(stdout);
	timewait(waitsecs);
	cnt = 3;
	do {
		n = netrecv();
		if (n < 0 && errno == EINTR) {
			alarm(0);
			return 0;
		}
		while ((n = netget(&pk, 1000)) > 0) {
			if (n < 0 && errno == EINTR) {
				if (--cnt > 0) {
					timewait(waitsecs);
					netsend(&pk, 60);
				} else {
					alarm(0);
					return 0;
				}
			}
		}
	} while (pk.type != Tinitb);
	
	alarm(0);	
	sethdr(&pk, Tinitc);
	netsend(&pk, 60);
	return 1;
}

int
ethopen(void)
{
	contag = (getpid() >> 8) ^ (getpid() & 0xff);
	return cecconnect();
}

void
gettingkilled(int x)
{
	exits("killed");
}

void
prmem(char *label, void *p, int len)	/* debugging print */
{
	uchar *cp;
	int i;
	
	cp = (uchar *)p;
	fprintf(stderr, "%s", label);
	for (i = 0; i < len; i++)
		fprintf(stderr, "%02x", *cp++);
	fprintf(stderr, "; ");
}

int
readln(int fd, char *buf, int len)
{
	int n;
	char ch;

	rawoff();
	for(n = 0; n < len; ){
		if(read(0, &ch, 1) != 1)
			return n;
		buf[n++] = ch;
		if(ch == '\n')
			break;
	}
	rawon();
	return n;
}

char
escape(void)
{
	char c, buf[64];
	int n;
loop:
	fprintf(stderr, ">>> ");
	fflush(stdout);
	n = readln(0, buf, sizeof buf - 1);
	if (n <= 0)
		return '.';
	c = buf[0];
	switch (c) {
	case 'i':
	case 'q':
	case '.':
		return c;
	}
	fprintf(stderr, "	(q)uit, (i)nterrupt, (.)continue\r\n");
	goto loop;
}

void
doloop(void)
{
	fd_set rfds;
	char c;
	int n, unacked = 0, retries, state;
	uchar sndseq = 0, rcvseq = -1;
	Pkt sndpkt, rcvpkt;
	uchar ea[6];
	struct timeval *tvp, timout;

	memmove(ea, connp->ea, 6);
	state = 0;
	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(netfd, &rfds);
		if (unacked == 0)
			FD_SET(0, &rfds);
		if (unacked) {
			tvp = &timout;
			tvp->tv_sec = 1;
			tvp->tv_usec = 0;
		} else
			tvp = NULL;
		n = select(netfd+1, &rfds, nil, nil, tvp);
		if (n < 0) {
			perror("select failed");
			exits("select");
		}
		if (n == 0) { 	/* timeout */
			if (--retries == 0) {
				fprintf(stderr, "Connection timed out\r\n");
				return;
			}
			netsend(&sndpkt, HDRSIZ + n < 60 ? 60 : HDRSIZ + n);
			continue;
		}
		if (FD_ISSET(0, &rfds)) {
			sethdr(&sndpkt, Tdata);
			n = read(0, &c, 1);
			if (n < 0) {
				perror("read failed");
				exits("read");
			}
			if (c == esc) {
				switch(c = escape()) {
				case 'q':
					sndpkt.len = 0;
					sndpkt.type = Treset;
					netsend(&sndpkt, 60);
					return;
				case '.':
					continue;
				case 'i':
					break;
				}
			}
			sndpkt.data[0] = c;
			sndpkt.len = n;
			sndpkt.seq = ++sndseq;
			unacked = 1;
			retries = 3;
			netsend(&sndpkt, HDRSIZ + n < 60 ? 60 : HDRSIZ + n);
		} else if (FD_ISSET(netfd, &rfds)) {
			n = netrecv();
			if (n < 0) {
				perror("netread failed");
				exits("netread");
			}
			while ((n = netget(&rcvpkt, sizeof rcvpkt)) > 0) {
				if (n < 60)
					continue;
				if (memcmp(rcvpkt.src, ea, 6) != 0)
					continue;
				if (ntohs(rcvpkt.etype) != CEC_ETYPE)
					continue;
				switch (rcvpkt.type) {
				case Tinita: 
				case Tinitb: 
				case Tinitc:
				case Tdiscover:
					break;
				case Toffer:
					cecconnect();
					break;
				case Tdata:
					if (rcvpkt.conn != contag)
						break;
					if (rcvpkt.seq == rcvseq)	/* ignore */
						break;
					write(1, rcvpkt.data, rcvpkt.len);
					
					/* ack data packet */
					
					memmove(rcvpkt.dst, rcvpkt.src, 6);
					memset(rcvpkt.src, 0, 6);
					rcvpkt.type = Tack;
					rcvpkt.len = 0;
					rcvseq = rcvpkt.seq;
					netsend(&rcvpkt, 60);
					break;
				case Tack:
					if (rcvpkt.seq == sndseq)
						unacked = 0;
					break;
				case Treset:
					return;
				}
			}
		}
	}
}

void
conn(int n)
{
	connp = &tab[n];
	vprintf("connecting ... ");
	if (ethopen() == 0) {
		vprintf("connection failed.\r\n");
		return;
	}
	vprintf("done.\r\n");
	vprintf("Escape is Ctrl-%c\r\n", tolower(esc+'A'-1));
	doloop();
	ethclose();
}

void
exits(char *s)
{
	if (connp != nil)
		ethclose();
	rawoff();
	if(!s || !*s)
		exit(0);
	exit(1);
}
