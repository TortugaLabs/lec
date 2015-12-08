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
 *++
 * = NCA(8)
 * :Revision: REVISION
 * :Author: A Liu Ly
 *
 * == NAME
 *
 * nca - Network Console on Acid
 *
 * == SYNOPSIS
 *
 *  nca _[-1]_
 *
 * == DESCRIPTION
 *
 * nca connects a stdio to the console's virtual terminals
 * (/dev/tty1...n) that are normally only accessible to someone with
 * local keyboard access.  It can only be run by the super user (more
 * strictly, by a user that can read and write from /dev/vcs and
 * /dev/tty*).
 *
 * It is particularly useful in conjunction with cecd(8) since it then
 * provides remote console access; with some appropriate system start up
 * scripts, nca can be started immediately the root filesystem has been
 * mounted read-only, providing a cheap, and only marginally inferior
 * alternative to a serial console.
 *
 * == OPTIONS
 *
 *  * *-1*
 *    Enable single run mode.  (Disables =quit= option).
 *
 * == USAGE
 *
 * When run as the super user, nca will connect your virtual terminal to
 * /dev/tty1.  Keypresses are sent to the terminal, and all changes to
 * the screen are sent back to your terminal.  
 *
 * All additional functions are accessed by pressing CTRL-a ('C-a'
 * hereafter) followed by one of the following:
 *
 * C-a can be send to the terminal by pressing it twice.
 * 
 * * *<1-9>*::
 *   select virtual terminal 1-9
 * * *-/+*::
 *   select the previous or next virtual terminal
 * * *R*::
 *   hard-reboot (after prompting for confirmation).  Note that this is
 *   direct call of the reboot system call, so disks will not be synced or
 *   unmounted.
 * * *r*::
 *   Executes the =/sbin/reboot= command.
 * * *q*::
 *   quit nca. This command is disabled by the *-1* command line option.
 *
 * == BUGS
 *
 * To describe the error handling as crude would be to be very harsh on
 * those programs that actually possess crude error handling.  Errors are
 * not so much handled as reported and ignored.
 *
 * == AUTHORS
 *
 * - Written by James McKenzie.
 * - Packaged by Julian Midgley.
 * - Later modified by Alejandro Liu Ly.
 *--
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>

extern int noquit;
struct termios oldtios;

/*Wrapper for syscalls for lazy people like me*/
#define MOAN(a) do_moan(a,#a,__LINE__)
int do_moan (int ret, char *what, int where);
void nca_main(void);

void setcooked() {
  if (!isatty(0)) return;
  MOAN (tcsetattr(0,TCSANOW,&oldtios));
}

void setraw() {
  struct termios tios;

  if (!isatty(0)) return;

  MOAN(tcgetattr (0, &oldtios));
  MOAN(tcgetattr (0, &tios));

  tios.c_iflag = IGNPAR;
  tios.c_oflag = 0;
  tios.c_lflag = 0;
  tios.c_cflag = CLOCAL | CS8 | CREAD;

  tios.c_cc[VMIN] = 1;
  tios.c_cc[VTIME] = 0;

  MOAN(tcsetattr(0,TCSANOW,&tios));
}

void quit(int x) {
  setcooked();
  exit(x);
}

void usage() {
  fprintf(stderr,"Usage: nca [1]\n");
  exit(1);
}

int
main (int argc, char *argv[])
{
  int ch;

  while ((ch=getopt(argc,argv,"1")) != -1) {
    switch (ch) {
    case '1':
      noquit = 1;
      break;
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;
  if (argc) usage();

  setraw();
  signal (SIGPIPE, quit);       /*FIXME: */

  nca_main();
  exit(1);
}
