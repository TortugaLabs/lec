/*
 * Coraid Ethernet Console protocol bits
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
#include "cec.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

extern int debug;

void
catch(int sig)
{
}

void
timewait(int secs)	/* arrange for a sig_alarm signal after `secs' seconds */
{
	struct sigaction sa;
	
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = catch;
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGALRM, &sa, NULL);
	alarm(secs);
}


void freeprobe(struct Shelf *s) {
  struct Shelf *p;

  while (s) {
    p = s->next;
    if (s->str[0]) free(s->str);
    free(s);
    s = p;
  }
}


/*
 * Do a probe...
 */
struct Shelf *cec_probe(int waitsecs,int shelf,char *shelfea) {
  struct Pkt q;
  struct Shelf *r=NULL, *s;
  int n;
  char *sh, *other;
  
  /* Initiliase probe packet... */
  memset(q.dst, 0xff, 6);
  memset(q.src, 0, 6);
  q.etype = htons(CEC_ETYPE);
  q.type = Tdiscover;
  q.len = 0;
  q.conn = 0;
  q.seq = 0;
  netsend(&q, 60);
  timewait(waitsecs);

  /* Wait for packets */
  for (;;) {
    n = netrecv();
    if (n < 0 && errno == EINTR) {
      alarm(0);
      return r;
    }
    while ((n = netget(&q, sizeof q)) > 0) {
      if (n < 60) {
	fputs("RUNT\n",stderr);
	continue;
      }
      if (ntohs(q.etype) != CEC_ETYPE) {
	fputs("!CEC_ETYPE\n",stderr);
	continue;
      }
      if (q.type != Toffer) {
	fprintf(stderr,"!Toffer (%d should be %d)\n",q.type,Toffer);
	continue;
      }
      if (q.len == 0) {
	fprintf(stderr,"q.len=%d\n",q.len);
	continue;
      }
      if (memcmp(q.dst, "\xff\xff\xff\xff\xff\xff", 6) == 0) {
	char aea[16];
	htoa(aea,(char *)q.dst,6);
	fprintf(stderr,"q.dst=%s\n",aea);
	continue;
      }

      q.data[q.len] = 0;
      sh = strtok((char *)q.data, " \t");
      if (sh == NULL)
	continue;
      if (shelf != -1 && (n=atoi(sh)) != shelf)
	continue;
      if (shelfea && memcmp(shelfea, q.src, 6))
	continue;

      other = strtok(NULL, "\x1");

      /* OK set-up a shelf entry */
      s = (struct Shelf *)malloc(sizeof(struct Shelf));
      if (s == NULL) continue;
      s->next = r;
      r = s;

      memcpy(s->ea, q.src, 6);
      s->shelfno = n;
      s->str = other && other[0] ? strdup(other) : "";
      if (!s->str) s->str = "";

      if (shelf != -1 || shelfea) {
	alarm(0);
	return r;
      }
    }
  }

  alarm(0);
  return r;
}

int cec_Treset(uchar *ea,int conn) {
  struct Pkt q;
  memset(q.src,0,6);
  memcpy(q.dst,ea,6);
  q.etype = htons(CEC_ETYPE);
  q.type = Treset;
  q.len = 0;
  q.conn = conn;
  q.seq = 0;
  return netsend(&q,60);
}

int cec_Tdata(uchar *ea,int conn,int seq,char *str) {
  struct Pkt q;
  memset(q.src,0,6);
  memcpy(q.dst,ea,6);
  q.etype = htons(CEC_ETYPE);
  q.type = Tdata;
  q.seq = seq;
  q.conn = conn;
  q.len = strlen(str);
  strcpy((char *)q.data,str);
  return netsend(&q,HDRSIZ+q.len);
}
