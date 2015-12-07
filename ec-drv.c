/*
 * Linux Ethernet Console Driver
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
 * = ECDRV(8)
 * :Revision: 2.0
 * :Author: A Liu Ly
 *
 * == NAME
 *
 * ecdrv - Linux Ethernet Console Driver
 *
 * == SYNOPSIS
 *
 * ec-drv _[options]_ _eth_ _[--]_ _[cmd_ _cmd_ _args]_
 *
 * == DESCRIPTION
 *
 * The *ec-drv* command uses raw sockets to present a CEC compatible
 * server for access to a serial console.  All clients share the same
 * session.
 *
 * A *ec-drv* starts by probing the specified network interface to either
 * pick a free shelf number or check if the specified shelf number is
 * not in use.
 *
 * If a command was specified then *ec-drv* 
 * will spawn the specified command and wait for *cec*
 * client sessions to connect.  When that happens all the I/O
 * is send between the client and the provided command.
 *
 * If no command was specifed, it will simply forward all stdio
 * to incoming client sessions.
 *
 * == OPTIONS
 *
 * * *-d*::
 *   The -d flag causes *ec-drv* to output copious debugging information.
 *   Only for the strong of heart.
 * * *-i* _secs_::
 *   Disconnect sesions that have been inactive for more than _secs_
 *   seconds.
 * * *-l*::
 *   Enable local mode.  The run command can be used interactively.
 * * *-s* _shelf_::
 *   Assign the _shelf_ number to this *ec-drv* instance.
 * * *-v*::
 *   Print version and exit.
 * * *-w* _secs_::
 *   The -w flag takes an argument, the number of seconds to use as a
 *   timeout.  This timeout defaults to 2, and governs how long to wait 
 *   on probe, connection, and communication timeout.  It must be greater
 *   than 0.
 * * *-f* _output_::
 *   When a USR1 signal is received, will write its shelfno and 
 *   srcaddr to _output_ file.
 * * *--*::
 *   Use this to signal the end of *ec-drv* options and start of 
 *   command line.
 *
 * == SEE ALSO
 *
 * *cec(8)*
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

enum {
  MAX_CLIENTS = 4,	/* We are not too ambitious */
  IDLE_TIMER = 300,	/* We clear clients after this many seconds */
};

struct client_t {
  uchar addr[6];
  time_t last;
  uchar conn;
  uchar seq;
};

extern int netfd;
int ifd, ofd;	/* Input/output fd */
struct client_t clients[MAX_CLIENTS];

int debug = 0;
int shelf= -1;
int lconsole = 0;
int waitsecs = WAITSECS;
int idle_timer = IDLE_TIMER;

char *progname = "ec-drv";
char *outfile = NULL;

char ring_buffer[MAX_PAYLOAD];
unsigned long ring_ptr = 0;

#define TRC { fprintf(stderr,"TRC: %s,%d\r\n",__func__,__LINE__); }

/*
 * Init PTY
 */
pid_t init_fds(void) {
  pid_t pid;
  int fd0[2], fd1[2],i;
  
  if (pipe(fd0) == -1) fatal("pipe0");
  if (pipe(fd1) == -1) fatal("pipe1");

  if ((pid = fork()) == -1) {
    //TRC;
    fatal("fork");
  }
  if (pid) {
    /* This is the CEC driver... */
    ifd = fd1[0]; close(fd1[1]);
    ofd = fd0[1]; close(fd0[0]);
    return 0;
  } else {
    /* This is the child process */
    dup2(fd0[0],STDIN_FILENO);
    dup2(fd1[1],STDOUT_FILENO);
    dup2(fd1[1],STDERR_FILENO);
    for (i=0;i<2;i++) {
      close(fd0[i]);
      close(fd0[i]);
    }
    return 1;
  }
}

void ifd_data(void) {
  struct Pkt q;
  int i, c;

  c = read(ifd,q.data,MAX_PAYLOAD);
  if (c == -1) {
    if (errno == EINTR) return;
    rawoff();
    //TRC;
    fatal("read");
  }

  if (c==0) {
    /* Ooops ... EOF */
    for (i=0;i < MAX_CLIENTS;i++) {
      if (clients[i].last) {
	cec_Tdata(clients[i].addr,clients[i].conn,++clients[i].seq,
		  "[System shutdown]");
	cec_Treset(clients[i].addr,clients[i].conn);
      }
    }
    fputs("[EOF]\r\n",stderr);
    rawoff();
    exit(1);
  }

  if (debug || lconsole) write(STDERR_FILENO,q.data,c);
  /*
   * Save output to ring buffer
   */
  if (((ring_ptr % sizeof(ring_buffer))+c) > sizeof(ring_buffer)) {
    /* OK, this wraps around... */
    int l = sizeof(ring_buffer) - (ring_ptr % sizeof(ring_buffer));
    memcpy(ring_buffer+(ring_ptr % sizeof(ring_buffer)),q.data,l);
    memcpy(ring_buffer,q.data+l,c-l);
  } else {
    /* This is the trivial case... */
    memcpy(ring_buffer+(ring_ptr % sizeof(ring_buffer)),q.data,c);
  } 
  ring_ptr += c;
  if (ring_ptr > sizeof(ring_buffer)) 
    ring_ptr = sizeof(ring_buffer) + (ring_ptr % sizeof(ring_buffer));

  q.etype = ntohs(CEC_ETYPE);
  q.type = Tdata;
  q.len = c;

  for (i=0;i<MAX_CLIENTS;i++) {
    if (!clients[i].last) continue;

    q.conn = clients[i].conn;
    q.seq = ++clients[i].seq;

    memcpy(q.dst,clients[i].addr,6);
    netsend(&q,HDRSIZ+q.len);
  }
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

      for (n=0;n<MAX_CLIENTS;n++)
	if (!clients[n].last) break;
	
      if (n == MAX_CLIENTS) {
	q.type = Treset;
	strcpy((char *)q.data,"no free ports");
      } else {
	int i;
	char msg[MAX_PAYLOAD];
	char aea[16];
	htoa(aea,(char *)q.src,6);
	snprintf(msg,MAX_PAYLOAD,"\r\n[New console %d attached (%s-%d)]\r\n",
		 n,aea,q.conn);

	if (debug || lconsole) fputs(msg,stderr);

	for (i=0; i<MAX_CLIENTS;i++) {
	  if (clients[i].last) {
	    cec_Tdata(clients[i].addr,clients[i].conn,++clients[i].seq,msg);
	  }
	}
	clients[n].last = time(NULL);
	memcpy(clients[n].addr,q.src,6);
	clients[n].conn = q.conn;
	clients[n].seq = q.seq;

	/*
	 * Send the ring buffer...
	 */
	q.type = Tdata;
	strcpy((char *)q.data,"[Connected]\r\n");
	q.len = strlen((char *)q.data);
	if (ring_ptr > sizeof(ring_buffer)) {
	  /* Wrapped around.... */
	  int addsz = MAX_PAYLOAD - q.len;
	  int s=(ring_ptr + sizeof(ring_buffer) - addsz) % sizeof(ring_buffer);
	  int l=sizeof(ring_buffer) - s;
	  memcpy(q.data + q.len, ring_buffer+s, l);
	  q.len += l;
	  l = ring_ptr % sizeof(ring_buffer);
	  memcpy(q.data + q.len, ring_buffer, l);
	  q.len += l;
	} else if (ring_ptr) {
	  int addsz = ring_ptr + q.len > MAX_PAYLOAD ? 
	    MAX_PAYLOAD - q.len :
	    ring_ptr;
	  memcpy(q.data + q.len, ring_buffer,addsz);
	  q.len += addsz;
	}
      }
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
	write(ofd,q.data,q.len);
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
      if (n != -1) {
	int i;
	char msg[MAX_PAYLOAD];
	char aea[16];

	clients[n].last = 0;

	htoa(aea,(char *)q.src,6);
	snprintf(msg,MAX_PAYLOAD,"\r\n[Console (%d) disconnected (%s-%d)]\r\n",
		 n,aea,clients[n].conn);
	if (debug || lconsole) fputs(msg,stderr);
	for (i=0; i<MAX_CLIENTS;i++) 
	  if (clients[i].last) 
	    cec_Tdata(clients[i].addr,clients[i].conn,++clients[i].seq,msg);
      }
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
    rawoff();
    exit(1);
  }
}

void local_data(void) {
  int c;
  char buf[MAX_PAYLOAD];

  c = read(0,buf,MAX_PAYLOAD);
  if (c > 0) {
    write(ofd,buf,c);
    return;
  }
  if (c < 0) {
    if (errno == EINTR) return;

    for (c=0;c < MAX_CLIENTS;c++) {
      if (clients[c].last) {
	cec_Tdata(clients[c].addr,clients[c].conn,++clients[c].seq,
		  "\r\n[process error]\r\n");
	cec_Treset(clients[c].addr,clients[c].conn);
      }
    }

    rawoff();
    //TRC;
    fatal("read(stdin)");
  }
  fputs("[EOF] read(stdin)",stderr);
  lconsole = 0;

}

void usage(void) {
  fprintf(stderr,"Usage:\n\t%s [-l][-w wait][-s shelf][-v][-?] eth [cmd]\n",
	  progname);
  exit(1);
}

/*
 * console server
 */
void con_server(char *addr) {
  int maxfd;

  maxfd = (netfd > ifd ? netfd : ifd) + 1;
  memset(clients,0,sizeof clients);

  for (;;) {
    fd_set rfds;
    int c;
    time_t now;
    struct timeval tv, *tvp = NULL;

    FD_ZERO(&rfds);
    FD_SET(netfd,&rfds);
    FD_SET(ifd,&rfds);
    if (lconsole) FD_SET(STDIN_FILENO,&rfds);

    /* Scan client table and expire idle users */
    now = time(NULL);
    for (c=0;c < MAX_CLIENTS;c++) {
      if (!clients[c].last) continue;
      if (now - clients[c].last > idle_timer) {
	/* Client timed-out */
	if (cec_Treset(clients[c].addr, clients[c].conn)) perror("cec_Treset");
	clients[c].last = 0;
      } else {
	/* Active client... figure out when to expire them... */
	int secs = clients[c].last + idle_timer - now;
	
	if (tvp) {
	  if (tv.tv_sec > secs) tv.tv_sec = secs;
	} else {
	  tv.tv_sec = secs;
	  tv.tv_usec = 0;
	  tvp = &tv;
	}
      }
    }

    c = select(maxfd,&rfds,NULL,NULL,tvp);
    if (c == -1 && errno != EINTR) {
      rawoff();
      fatal("select");
    } else if (c > 0) {
      if (FD_ISSET(ifd,&rfds)) {
	ifd_data();
      }
      if (FD_ISSET(netfd,&rfds)) {
	c = netrecv();
	if (c < 0) {
	  if (errno == EINTR) continue;
	  rawoff();
	  if (errno == ENETDOWN) {
	    /*
	     * IF went down...
	     *	re-up...
	     */
	    if (netup(addr)) fatal("netup");
	    if (netopen(addr)) fatal("netopen");
	    if (lconsole) rawon();
	    continue;
	  }
	  fatal("netrecv");
	}
	net_data();
      }
      if (lconsole && FD_ISSET(0,&rfds)) {
	local_data();
      }
    }
  }
}

//void sighup(int n) {
//  if (lconsole) rawon();
//}

void sigchld(int n) {
  while (wait(&n) != -1);
  rawoff();
  exit(n);
}

void sigusr1(int n) {
  FILE *fp;
  char aea[16];
  extern char srcaddr[6];

  if (!outfile) return;
  fp = fopen(outfile,"w");
  if (!fp) return;
  htoa(aea,(char *)srcaddr,6);
  fprintf(fp,"%d %s\n",shelf,aea);
  fclose(fp);
}


int main(int argc,char **argv) {
  int ch;
  progname = *argv;

  while ((ch=getopt(argc,argv,"df:i:ls:vw:?")) != -1) {
    switch (ch) {
    case 'f':
      outfile = optarg;
      break;
    case 'd':
      debug=1;
      break;
    case 'l':
      lconsole=1;
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
      printf("ec-drv v%s\n",VERSION);
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

  if (argc == 1 && lconsole) {
    fprintf(stderr,"%s: Option -l requires a command to be specified\n",
	    progname);
    exit(1);
  }

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

  if (argc <= 1) {
    /* No command is needed */
    ifd = STDIN_FILENO;
    ofd = STDOUT_FILENO;
    signal(SIGTERM,SIG_IGN);
    if (outfile) signal(SIGUSR1,sigusr1);
    con_server(*argv);
  } else {
    if (init_fds()) {
      /* exec process... */
      netclose();
      execvp(argv[1],argv+1);
      //TRC;
      fatal("exec");
    } else {
      if (outfile) signal(SIGUSR1,sigusr1);
      signal(SIGCHLD,sigchld);
      if (lconsole) rawon();
      con_server(*argv);
    }
  }
  return 0;
}
