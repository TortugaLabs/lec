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
 * = SLED(8)
 * :Revision: REVISION
 * :Author: A Liu Ly
 *
 * == NAME
 *
 * sled - Serial Linux Ethernet Daemon
 *
 * == SYNOPSIS
 *
 * sled _[options]_ _eth_ _[--]_ _[cmd_ _cmd_ _args]_
 *
 * == DESCRIPTION
 *
 * The *sled* command uses raw sockets to connect to present CEC compatible
 * server for console access.  All clients share the same session.
 *
 * It does this by spawning a couple of process, one to manage
 * a PTY and another to manage an Ethernet Console by exec'ing the
 * *ec-drv* command.
 *
 * After that is done, then it will arrange for *init* to replace
 * it as the INIT process, however, any processes spawned from
 * that point on will have the PTY as its controlling terminal.
 *
 * I/O from the PTY is then send to Ethernet Console Clients.
 *
 * == OPTIONS
 *
 * * *-e* _ec-drv_::
 *   Path to the *ec-drv* command.
 * * *-i* _secs_::
 *   Disconnect sesions that have been inactive for more than _secs_
 *   seconds.
 * * *-s* _shelf_::
 *   Assign the _shelf_ number to this *ec-drv* instance.
 * * *-v*::
 *   Print version and exit.
 * * *-w* _secs_::
 *   The -w flag takes an argument, the number of seconds to use as a
 *   timeout.  This timeout defaults to 2, and governs how long to wait 
 *   on probe, connection, and communication timeout.  It must be greater
 *   than 0.
 * * *--*::
 *   Use this to signal the end of *ec-drv* options and start of 
 *   command line.
 *
 * == SEE ALSO
 *
 *    *cec(8)*
 *--
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pty.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "cec.h"


#ifndef VERSION
#define VERSION "0.00"
#endif

char *progname = "sled";
char *lecdcmd = "/sbin/ec-drv";
char *shelf= NULL;
char *waitsecs = NULL;
char *idle_timer = NULL;
int debug = 0;

int ptyfd;	/* Pty pair */
int slave;

void status(char *p) {
  char buf[1000];
  if (p) fprintf(stderr,"%s ",p);
  sprintf(buf,"ps h -otty,pid,pgid,sid,comm %d",getpid());
  system(buf);
}


int compat_getpty(int *ptyfd,int *slave) {
  char name[16];
  int i,j, o_error;
  struct winsize winsz = {
    .ws_row = 25,
    .ws_col = 80,
  };

  if (!openpty(ptyfd,slave,NULL,NULL,&winsz)) return 0;
  o_error = errno;

  /* OpenPTY failed, try to do the old fashion method... */
  strcpy(name,"/dev/ptyXX");
  for (i = 0; i < 16; i++) {
    name[8] = "pqrstuvwxyzabcde"[i];
    name[9] = '0';

    for (j = 0; j < 16; j++) {
      name[9] = j < 10 ? j + '0' : j - 10 + 'a';
      *ptyfd = open(name, O_RDWR | O_NOCTTY);
      if (*ptyfd >= 0) {
	name[5] = 't';
	*slave = open(name,O_RDWR | O_NOCTTY);
	if (*slave >= 0) {
	  if (ioctl(*slave,TIOCSWINSZ,&winsz))
	    perror("warning-ioctl(TIOCSWINSZ)");
	  return 0;
	} else
	  close(*ptyfd);
      }
    }
  }
  fprintf(stderr,"Failed\n");
  errno = o_error;
  return -1;
}

void usage(void) {
  fprintf(stderr,"Usage:\n\t%s [-e lecd][-w wait][-s shelf][-v][-?] eth cmd\n",
	  progname);
  exit(1);
}

int main(int argc,char **argv) {
  int ch;
  progname = *argv;
  pid_t mpid;

  // status("BEGIN");
  while ((ch=getopt(argc,argv,"e:i:s:vw:?")) != -1) {
    switch (ch) {
    case 'e':
      lecdcmd = optarg;
      break;
    case 'i':
      if (atoi(optarg) <= 0) {
	fputs("invalid i value, ignoring.\n",stderr);
	idle_timer = NULL;
      } else
	idle_timer = optarg;
      break;
    case 's':
      shelf = optarg;
      break;
    case 'v':
      printf("%s v%s\n",progname,VERSION);
      exit(0);
      break;
    case 'w':
      if (atoi(optarg) <= 0) {
	fputs("Invalid w value, ignoring.\n",stderr);
	waitsecs = NULL;
      } else 
	waitsecs = optarg;
      break;
    case '?':
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (argc <= 1) usage();

  if (compat_getpty(&ptyfd,&slave)) fatal("openpty");

  printf("SLED %s started...\n",VERSION);

  if ((mpid = fork()) == -1) fatal("fork");
  if (!mpid) {
    /* Start a CON driver and ETH driver processes */
    int io1[2], io2[2];
    if (pipe(io1) == -1) fatal("pipe1");
    if (pipe(io2) == -1) fatal("pipe2");

    if ((mpid = fork()) == -1) fatal("fork");    
    if (!mpid) {
      char *lecd_cmdline[20];
      int j;
      /* This is the network driver process */

      /* child process... */
      close(slave);
      close(ptyfd);

      dup2(io1[0],STDIN_FILENO); close(io1[1]);
      dup2(io2[1],STDOUT_FILENO); close(io2[0]);

      write(STDOUT_FILENO,"\n",1); /* Tell peer that we are ready ... */
      read(STDIN_FILENO,&j,1); /* Wait for peer to reply ready... */
      close(ptyfd);
      if (setsid()==-1) perror("setsid(lec)");

      j = 0;
      lecd_cmdline[j++] = "lecd";
      if (shelf) {
	lecd_cmdline[j++] = "-s";
	lecd_cmdline[j++] = shelf;
      }
      if (waitsecs) {
	lecd_cmdline[j++] = "-w";
	lecd_cmdline[j++] = waitsecs;
      }
      if (idle_timer) {
	lecd_cmdline[j++] = "-i";
	lecd_cmdline[j++] = idle_timer;
      }
      lecd_cmdline[j++] = argv[0];
      lecd_cmdline[j++] = NULL;
      
      execvp(lecdcmd,lecd_cmdline);
      fatal("exec");
    } else {
      int maxfd = 0;
      fd_set rfds;
      int i;
      char buf[1024];

      // This is the CON driver process
      close(io1[0]);close(io2[1]);
      close(slave);


      if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;
      if (ptyfd > maxfd) maxfd = ptyfd;
      if (io2[0] > maxfd) maxfd = io2[0];
      ++maxfd;
      
      rawon();
      // fprintf(stderr,"%d: ENTER MAIN LOOP\n",getpid());
      for (;;) {
	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO,&rfds);
	FD_SET(ptyfd,&rfds);
	FD_SET(io2[0],&rfds);
	select(maxfd,&rfds,NULL,NULL,NULL);
	if (FD_ISSET(ptyfd,&rfds)) {
	  i = read(ptyfd,buf,sizeof buf);
	  if (i <= 0) fatal("I/O error reading pty");
	  write(io1[1],buf,i);
	  write(STDOUT_FILENO,buf,i);
	}
	if (FD_ISSET(STDIN_FILENO,&rfds)) {
	  i = read(STDIN_FILENO,buf,sizeof buf);
	  if (i <= 0) fatal("I/O error reading pty");
	  write(ptyfd,buf,i);
	}

	if (FD_ISSET(io2[0],&rfds)) {
	  i = read(io2[0],buf,sizeof buf);
	  if (i <= 0) fatal("I/O error reading pty");
	  write(ptyfd,buf,i);
	}
      }
    }
  } else {
    /* Init driver... */
    const char devtty[] = "/dev/tty";
    int fd;

    //status("INIT SYNC PEER");
    signal(SIGHUP,SIG_IGN);
    read(slave,&fd,1);	/* Wait for peer to be ready */
    close(ptyfd);
    //status("INIT SYNC DONE");


    /* Drop our current CTTY */
    fd = open(devtty,O_RDWR|O_NOCTTY);
    if (fd != -1) {
      if (ioctl(fd,TIOCNOTTY,NULL) == -1) fatal("ioctl(TIOCNOTTY)");
      close(fd);
    } else {
      perror("Warning, no CTTY");
    }

    /* Switch our current CTTY to slave */
    if (ioctl(slave,TIOCSCTTY,1)==-1) perror("ioctl(TIOCSCTTY)");

    dup2(slave,STDIN_FILENO);
    dup2(slave,STDOUT_FILENO);
    dup2(slave,STDERR_FILENO);
    write(slave,"\n",1);	/* Tell peer that we are ready */
    close(slave);

    execvp(argv[1],argv+1);
    fatal("exec");

  }
  exit(0);
}


