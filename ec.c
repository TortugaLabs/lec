/*
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
 *++
 * = LECD(8)
 * :Revision: REVISION
 * :Author: A Liu Ly
 *
 * == NAME
 *
 *    lecd - Ethernet Console Daemon
 *
 * == SYNOPSIS
 *
 * *lecd* _[options]_ _eth_
 *
 * == DESCRIPTION
 * The *lecd* command uses raw sockets to present a CEC compatible
 * server for console access.
 *
 * A *lecd* starts by probing the specified network interface to either
 * pick a free shelf number or check if the specified shelf number is
 * not in use.
 *
 * == OPTIONS
 *
 * * *-d*::
 *   The -d flag causes *lecd* to output copious debugging information.
 *   Only for the strong of heart.
 * * *-i* _secs_::
 *   Disconnect sesions that have been inactive for more than _secs_
 *   seconds.
 * * *-s* _shelf_::
 *   Assign the _shelf_ number to this *lecd* instance.
 * * *-v*::
 *   Print version and exit.
 * * *-w* _secs_::
 *    The -w flag takes an argument, the number of seconds to use as a
 *    timeout.  This timeout defaults to 2, and governs how long to wait 
 *    on probe, connection, and communication timeout.  It must be greater
 *    than 0.
 *
 * == SEE ALSO
 *
 * *cec(8)*
 *
 * == AUTHORS
 *
 * - Alejandro Liu Ly
 * - NCA code by James McKenzie.
 *--
 */
#include "cec.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/wait.h>


#ifndef VERSION
#define VERSION "0.00"
#endif

void nca_main(void);

enum {
  MAX_CLIENTS = 4,	/* We are not too ambitious */
  IDLE_TIMER = 300,	/* We clear clients after this many seconds */
};

struct client_t {
  uchar addr[6];
  time_t last;
  uchar conn;
  uchar seq;
  
  // NCA process
  pid_t dpid;
  int ifd,ofd;
};

extern int netfd;
struct client_t clients[MAX_CLIENTS];

int debug = 0;
int shelf= -1;
int waitsecs = WAITSECS;
int idle_timer = IDLE_TIMER;

char *progname = "ec";


#define TRC { fprintf(stderr,"TRC: %s,%d\r\n",__func__,__LINE__); }

void usage(void) {
  fprintf(stderr,"Usage:\n\t%s [-l][-w wait][-s shelf][-v][-?] eth [cmd]\n",
	  progname);
  exit(1);
}

int find_client(struct Pkt *p) {
  int i;
  for (i=0;i< MAX_CLIENTS;i++) {
    if (clients[i].last 
	&& memcmp(clients[i].addr,p->src,6) == 0 
	&& clients[i].conn == p->conn) 
      return i;
  }
  return -1;
}


/*
 * Reset client connection
 */
void client_reset(int c) {
  if (cec_Treset(clients[c].addr, clients[c].conn)) perror("cec_Treset");
  clients[c].last = 0;
  close(clients[c].ifd);
  close(clients[c].ofd);
  clients[c].ifd = clients[c].ofd = clients[c].dpid = 0;
}

/*
 * Initialise a new client connection
 */
void init_client(struct Pkt *q) {
  int n;
  int io1[2],io2[2];
  pid_t tt;

  /* Find an empty slot */
  for (n=0;n<MAX_CLIENTS;n++)
    if (!clients[n].last) break;
	
  q->type = Treset;
  if (n == MAX_CLIENTS) {
    strcpy((char *)q->data,"no free ports");
    return;
  }

  if (pipe(io1) != -1) {
    if (pipe(io2) != -1) {
      if ((tt = fork()) != -1) {
	if (tt) {
	  /* Parent process */
	  clients[n].last = time(NULL);
	  memcpy(clients[n].addr,q->src,6);
	  clients[n].conn = q->conn;
	  clients[n].seq = q->seq;

	  clients[n].dpid = tt;
	  clients[n].ifd = io2[0]; close(io2[1]);
	  clients[n].ofd = io1[1]; close(io1[0]);
	  q->type = Tdata;
	  strcpy((char *)q->data,"[Connected]\r\n");
	  return;
	} else {
	  /* Child process */
	  dup2(io1[0],STDIN_FILENO);
	  dup2(io2[1],STDOUT_FILENO);
	  close(netfd);
	  close(io1[0]);close(io1[1]);
	  close(io2[0]);close(io2[1]);
	  for (n = 0; n<MAX_CLIENTS;n++) {
	    if (clients[n].last) {
	      //close(clients[n].ifd);
	      //close(clients[n].ofd);
	    }
	  }
	  /* RUN NCA */
	  nca_main();
	  exit(1);
	}
      } else 
	strcpy((char *)q->data,"fork failed");    
      close(io2[0]);close(io2[1]);
    } else 
      strcpy((char *)q->data,"pipe(2) error");
    close(io1[0]);close(io1[1]);
  } else
    strcpy((char *)q->data,"pipe(1) error");
}

/*
 * Read screen dta
 */
void ifd_data(int n) {
  struct Pkt q;
  int c;

  c = read(clients[n].ifd,q.data,MAX_PAYLOAD);
  if (c == -1) {
    if (errno == EINTR) return;
    //TRC;
    fatal("read");
  }

  if (c==0) {
    /* Ooops ... EOF */
    cec_Tdata(clients[n].addr,clients[n].conn,++clients[n].seq,"[EOF]");
    cec_Treset(clients[n].addr,clients[n].conn);
    client_reset(n);
    return;
  }

  q.etype = ntohs(CEC_ETYPE);
  q.type = Tdata;
  q.len = c;

  q.conn = clients[n].conn;
  q.seq = ++clients[n].seq;

  memcpy(q.dst,clients[n].addr,6);
  netsend(&q,HDRSIZ+q.len);
}

/*
 * Handle network events
 */
void net_data(void) {
  struct Pkt q;
  int n;

  if ((n = netget(&q,sizeof q)) > 0) {
    if (n < 60) return;
    if (ntohs(q.etype) != CEC_ETYPE) return;
    memcpy(q.dst,q.src,6);

    switch (q.type) {
    case Tinita:
      /* We always say yes... */
      q.type = Tinitb;
      netsend(&q,60);
      break;
    case Tinitc:
      n = find_client(&q);
      if (n != -1) {
	/* Already connected */
	cec_Tdata(q.src,q.conn,++clients[n].seq,"[Connected]\n\n");
	break;
      }
      init_client(&q);
      netsend(&q,HDRSIZ + q.len);
      break;
    case Tdata:
      n = find_client(&q);
      if (n == -1) {
	q.type = Treset;
	strcpy((char *)q.data,"connection closed");
	q.len = strlen((char *)q.data);
	netsend(&q,HDRSIZ + q.len);	
      } else {
	clients[n].last = time(NULL);
	write(clients[n].ofd,q.data,q.len);
	q.len = 0;
	q.type = Tack;
	netsend(&q,60);
      }
      break;
    case Tack:
      n = find_client(&q);
      if (n != -1) clients[n].last = time(NULL);
      break;
    case Treset:
      n = find_client(&q);
      if (n != -1) client_reset(n);
      break;
    case Tdiscover:
      {
	struct utsname u;
	uname(&u);
	snprintf((char *)q.data,MAX_PAYLOAD,"%d\t%s %s %s %s",
		 shelf,u.nodename,u.sysname,u.release,u.machine);
		 
	q.type = Toffer;
	q.len = strlen((char *)q.data);
	netsend(&q,HDRSIZ+q.len);
      }
      break;
    }
  } else {
    fputs("netrecv: EOF\r\n",stderr);
    exit(1);
  }
}



/*
 * console server
 */
void con_server(char *addr) {
  memset(clients,0,sizeof clients);

  for (;;) {
    int maxfd;
    fd_set rfds;
    int c;
    time_t now;
    struct timeval tv, *tvp = NULL;

    FD_ZERO(&rfds);
    FD_SET(netfd,&rfds);

    maxfd = netfd;
    /* Scan client table and expire idle users */
    now = time(NULL);

    for (c=0;c < MAX_CLIENTS;c++) {
      if (!clients[c].last) continue;

      if (now - clients[c].last > idle_timer) {
	/* Client timed-out */
	client_reset(c);
      } else {
	/* Active client... figure out when to expire them... */
	int secs = clients[c].last + idle_timer - now;
	
	if (clients[c].ifd > maxfd) maxfd = clients[c].ifd;
	FD_SET(clients[c].ifd, &rfds);

	if (tvp) {
	  if (tv.tv_sec > secs) tv.tv_sec = secs;
	} else {
	  tv.tv_sec = secs;
	  tv.tv_usec = 0;
	  tvp = &tv;
	}
      }
    }
    ++maxfd;

    c = select(maxfd,&rfds,NULL,NULL,tvp);
    if (c == -1 && errno != EINTR) {
      fatal("select");
    } else if (c > 0) {
      if (FD_ISSET(netfd,&rfds)) {
	c = netrecv();
	if (c < 0) {
	  if (errno == EINTR) continue;
	  if (errno == ENETDOWN) {
	    /*
	     * IF went down...
	     *	re-up...
	     */
	    if (netup(addr)) fatal("netup");
	    if (netopen(addr)) fatal("netopen");
	    continue;
	  }
	  fatal("netrecv");
	}
	net_data();
      }
      for (c = 0; c <MAX_CLIENTS;c++) {
	if (!clients[c].last) continue;
	if (FD_ISSET(clients[c].ifd,&rfds)) {
	  ifd_data(c);
	}
      }
    }
  }
}

int main(int argc,char **argv) {
  int ch;
  progname = *argv;

  while ((ch=getopt(argc,argv,"di:s:vw:?")) != -1) {
    switch (ch) {
    case 'd':
      debug=1;
      break;
    case 'i':
      idle_timer=atoi(optarg);
      if (idle_timer <= 0) {
	fputs("invalid i value, ignoring.\n",stderr);
	idle_timer = IDLE_TIMER;
      }
      break;
    case 's':
      shelf = atoi(optarg);
      break;
    case 'v':
      printf("lecd v%s\n",VERSION);
      exit(0);
      break;
    case 'w':
      waitsecs = atoi(optarg);
      if (waitsecs <= 0) {
	fputs("Invalid w value, ignoring.\n",stderr);
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
  if (debug) fputs("debug is on\n",stderr);
  if (argc < 1) usage();

  //TRC;
  if (netup(argv[0])) fatal("netup");
  if (netopen(argv[0])) fatal("netopen");
  //TRC;

  if (shelf == -1) {
    /* Auto assign shelf number */
    struct Shelf *s,*r = cec_probe(waitsecs,0,NULL);
    int count = 0;

    for (s=r; s ; s = s->next) {
      if (s->shelfno > shelf) shelf = s->shelfno;
      count++;
    }
    if (shelf >= count) {
      /* OK, there should be un-assigned numbers in between...*/
      int again;
      again = 1;
      shelf = 0;	/* Start searching from zero ... */

      while (again) {
	again = 0;
	for (s=r; s ; s = s->next) {
	  if (s->shelfno == shelf) {
	    ++shelf; /* Shelf number in use... search the next one */
	    again = 1;
	    break;
	  }
	}
      }
    } else 
      ++shelf;

    freeprobe(s);
    fprintf(stderr,"Will use shelfno %d\n",shelf);
  } else {
    struct Shelf *s = cec_probe(waitsecs,shelf,NULL);
    if (s) {
      char aea[16];
      htoa(aea,s->ea,6);
      fprintf(stderr,"shelf %d (%s) already exists at %s\n",
	      shelf,s->str,aea);
      freeprobe(s);
      exit(1);
    }
  }

  signal(SIGCHLD,SIG_IGN);
  con_server(*argv);
  return 0;
}

void quit(int x) {
  exit(x);
}
