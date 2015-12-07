/*
 * nca.c:
 *
 * License
 * -------
 * 
 * Copyright (c) 2002-2004 James McKenzie <james@fishsoup.dhs.org>,
 *               2004      Julian T. J. Midgley <jtjm@xenoclast.org>
 * Copyright (c) 2011 Alejandro Liu Ly <alejandro_liu@hotmail.com>
 * 
 * nca (being everything included in this package other than the openssh
 * daemon itself) is distributed under the terms of a 2-part BSD licence,
 * as follows:
 * 
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions
 *    are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <syscall.h>
#include <endian.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/select.h>

/*nca reads the screen of a VT and reproduces it in on an ANSI terminal*/
/*it also takes keypresses and injects them into the coreesponding VT*/

/*Reading the screen is done with the /dev/vcs... devices see vcs(4) */
/*and injecting keypresses is done with TIOCSTI */

/*nca polls the screen looking for changes and updates only the changed*/
/*characters.*/

/*nca assumes an ANSI terminal, it could use curses, but that would*/
/*require at lot more to be working in your system than nca currently needs*/

/*nothing prevents you from running multiple nca sessions on the */
/*same VT infact they can be very useful, running nca on a VT might*/
/*not be good for your health.*/

/*I should thank Chris Lightfoot for thinking that this would */
/*be a sane solution to the problem of headless machines*/

/*Set the escape character for the user currently CTRL-A*/
#define ESCAPE '\001'

/*How often to poll the screen in us*/
#define POLL_INTERVAL 200000

/*Wrapper for syscalls for lazy people like me*/
#define MOAN(a) do_moan(a,#a,__LINE__)

/*struct to hold a state of the screen */
typedef struct ScreenState_struct
{
  int x;                        /*X pos of cursor */
  int y;                        /*Y pos of cursor */
  int w;                        /*Width of screen */
  int h;                        /*Height of screen */
  unsigned char *data;          /*Screen data, from malloc */
}
 *ScreenState;

int noquit = 0;

void quit(int x);

int fdi = -1;                   /*FD of the tty we are putting chars into */
int fdo = -1;                   /*FD of the VC we are looking at (for reading the screen) */

int tty = 1;                    /*VT we are looking at */
int oldtty = -1;		/*Old TTY... */
int moaning = 0;

char buf[1024];                 /*buf[1023] is an remains 0 */

/*Moan about something which might fail and say where, what and why*/
/*can be used as a wrapper around syscalls */
int
do_moan (int ret, char *what, int where)
{
  if (ret < 0)
    {
      snprintf (buf, sizeof (buf) - 1,
                __FILE__ ", line %d: %s == %d (err=%d:%s)\n\r\n\r",
                where, what, ret, errno, strerror (errno));
      write (2, buf, strlen (buf));
      moaning = 2;                /*give user chance to see the message */
    }

  return ret;
}

/*write a C-string to an fd*/
void
writestr (int fd, char *s)
{
  MOAN (write (fd, s, strlen (s)));
}


/* (re-)open fdi and fdo to corespond to the tty in tty */
void
reopen_ttys (void)
{
  if (fdi >= 0)
    close (fdi);
  snprintf (buf, sizeof (buf) - 1, "/dev/tty%d", tty);
  fdi = MOAN (open (buf, O_WRONLY | O_NOCTTY));

  if (fdo >= 0)
    close (fdo);
  snprintf (buf, sizeof (buf) - 1, "/dev/vcsa%d", tty);
  fdo = MOAN (open (buf, O_RDONLY));

}

/*Send a simulated keypress to a tty, if we fail try */
/*reopening the devices a few times*/
void send_char (unsigned char c)
{
  int n = 10;
  while (ioctl (fdi, TIOCSTI, &c) && (n--))
    {
      reopen_ttys ();
    }
}

/*Allocate a new state*/
ScreenState
new_state (void)
{
  ScreenState s = (ScreenState) malloc (sizeof (struct ScreenState_struct));
  s->data = malloc (1);
  s->w = -1;
  s->h = -1;

  return s;
}

/*Read the current state of the screen into a ScreenState*/
ScreenState
get_state (int fd, ScreenState s)
{
  unsigned char v[4] = { 0, 0, 0, 0 };

  if (!s)
    s = new_state ();

  /*/dev/vcs.. is formatted as follows */
  /*one octet   height of screen */
  /*one octet   width of screen */
  /*one octet   x position of cursor */
  /*one octet   y position of cursor */
  /*(w*h*2) octets screen data */

  /*The screen data is orginized as words containing atribute */
  /*value pairs - like the old MPDA and CGA screen modes */
  /*care is needed to get the endianness right */

  MOAN (lseek (fd, SEEK_SET, 0));
  MOAN (read (fd, v, 4));

  if ((v[1] != s->w) || (v[0] != s->h))
    {
      free (s->data);
      s->w = v[1];
      s->h = v[0];
      s->data = malloc (s->w * s->h * 2);
    }

  MOAN (read (fd, s->data, s->w * s->h * 2));

  s->x = v[2];
  s->y = v[3];

  return s;
}

/* Vile code, given a character and an attribute, generate */
/* the required ansi horrors to produce it on the terminal */

void
put_char (unsigned char *c, int a)
{
  static int ca = -1;           /*Cache the last attribute set */

  /*Get terminal to match attribute */
  if (ca != a)
    {
      char buf[1024];

      /*VGA attributes are */
      /*BLINK BG2 BG1 BG0 HIGHLIGHT FG2 FG1 FG0 */

      /*VGA foreground -> ANSI foreground mapping */
      /*BLACK,BLUE,GREEN,CYAN,RED,MAGENTA,YELLOW(BROWN),WHITE */
      int mapf[8] = { 0, 34, 32, 36, 31, 35, 33, 37 };

      /*VGA background -> ANSI background mapping */
      /*BLACK,BLUE,GREEN,CYAN,RED,MAGENTA,YELLOW(BROWN),WHITE */
      int mapb[8] = { 40, 44, 42, 46, 41, 45, 43, 47 };

      /*ignore blink for sanity's sake - anyhow xterm ignores it */
      int h = (a & 0x8) ? 1 : 0;
      int f = mapf[a & 0x7];
      int b = mapb[(a >> 4) & 0x7];

      snprintf (buf, sizeof (buf) - 1, "\033[%d;%d;%dm", h, f, b);
      writestr (1, buf);
    }

  /*send the char, henously inefficient */
  MOAN (write (1, c, 1));
}


/*Move the cursor to x,y*/
void
moveto (int x, int y)
{
  char buf[1024];
  snprintf (buf, sizeof (buf) - 1, "\033[%d;%dH", y + 1, x + 1);
  writestr (1, buf);
}

/*Clear the screen*/
void
cls (int w,int h)
{
  if (moaning) {
    sleep(moaning);
    moaning=0;
  }
  moveto (0, 0);
  put_char ((unsigned char *)" ", 0x7);
  /* Make sure xterm's are of 80x25... */
  if (w && h) {
    char buf[1024];
    snprintf(buf,sizeof(buf)-1,"\033[8;%d;%dt",h,w);
    writestr(1,buf);
  }
  MOAN (write (1, "\033[2J", 4));
  moveto (0, 0);
}


/*Given the old and new states of the screen update the */
/*user's terminal*/

void
update_output (ScreenState old, ScreenState new)
{
  int x, y;
  unsigned char *nrptr = new->data;

  /*If the size of the screen has changed eg we are on one */
  /*of those SuSE graphics cards... or we are starting up */
  /*Walk the whole screen and redraw everything */
  if ((old->w != new->w) || (old->h != new->h))
    {
      for (y = 0; y < new->h; ++y)
        {
          unsigned char *nptr = nrptr;
          moveto (0, y);
          for (x = 0; x < new->w; ++x)
            {
#if ( BYTE_ORDER == BIG_ENDIAN )
              put_char ((nptr + 1), *nptr);
#else
              put_char (nptr, *(nptr + 1));
#endif

              nptr += 2;
            }
          nrptr += 2 * new->w;
        }


    }
  else
    {
      /* Update only changes */
      int cx = -1, cy = -1;     /*keep track of the cursor position to avoid unnecessary moves */
      unsigned char *orptr = old->data;

      for (y = 0; y < new->h; ++y)
        {
          unsigned char *nptr = nrptr; /*row pointers */
          unsigned char *optr = orptr;

          for (x = 0; x < new->w; ++x)
            {

              if ((*nptr != *optr) || (*(nptr + 1) != *(optr + 1)))
                {

                  if ((cx != x) || (cy != y))
                    {
                      moveto (x, y);
                      cx = x;
                      cy = y;
                    }

#if ( BYTE_ORDER == BIG_ENDIAN )
                  put_char ((nptr + 1), *nptr);
#else
                  put_char (nptr, *(nptr + 1));
#endif
                  cx++;

                }
              nptr += 2;
              optr += 2;
            }
          nrptr += 2 * new->w;
          orptr += 2 * old->w;
        }
    }
  moveto (new->x, new->y);
}

/*The user pressed the escape char, handle this -- menu */
void
do_escape (void)
{
  unsigned char c;
  char buf[1024];
  int i;

  cls(0,0);
  writestr (1, "Escape:\n\r");
  writestr (1, "\n\r");
  writestr (1, "Type escape char again to send it\n\r");
  snprintf (buf, sizeof (buf) - 1, "Current tty is: %d\n\r", tty);
  writestr (1, buf);
  writestr (1, "\n\r");
  writestr (1, "1-9 select VT 1-9\n\r");
  writestr (1, "0 select VT10\n\r");
  writestr (1, "-/+ select prev/next VT\n\r");
  writestr (1, "r   Reboot system\n\r");
  writestr (1, "R   hard-reboot\n\r");

  if (!noquit) writestr (1, "q   quit\n\r");

  writestr(1,"\n\n");
  writestr (1, "NCA");
#ifdef VERSION
  writestr (1, " v");
  writestr (1,VERSION);
#endif
  writestr (1,"\r\n");
  writestr(1,"Copyright (c) 2002-2004 James McKenzie <james@fishsoup.dhs.org>,\r\n");
  writestr(1,"              2004      Julian T. J. Midgley <jtjm@xenoclast.org>\r\n");
  writestr(1,"Copyright (c) 2011 Alejandro Liu Ly <alejandro_liu@hotmail.com>\r\n");
  writestr (1,"All Rights reserved\r\n");

  read (0, &c, 1);

  switch (c)
    {
    case ESCAPE:
      send_char (c);
      break;
    case '0':
      tty = 10;
      break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      tty = c - '0';
      break;
    case '+':
      tty += 2;
    case '-':
      tty--;
      if (tty < 1)
        tty = 1;
      if (tty > 12)
	tty = 12;
      break;
    case 'R':
      cls (0,0);
      writestr (1, "Are you sure (y/n)?\n\r");
      writestr (1, "\n\r");
      read (0, &c, 1);

      if (c == 'y')
        syscall (__NR_reboot, 0xfee1dead, 0x28121969, 0x1234567, NULL);
      break;
    case 'r':
      MOAN (write (1, "\033[m\033[2J", 7));
      moveto (0, 0);
      if (!fork()) {
	MOAN (execl ("/sbin/reboot", "reboot", (char *) 0));
	quit(1);
      }
      while (wait(&i) != -1);
      break;
    case 'q':
      if (!noquit) {
	MOAN (write (1, "\033[m\033[2J", 7));
	moveto (0, 0);
	quit(0);
      }
    }

}

void nca_main(void) {
  struct timeval tv = { 0 };
  fd_set rfds;
  ScreenState old, new;

  FD_ZERO (&rfds);

  old = new_state ();
  new = new_state ();

  cls (new->w,new->h);

  for (;;)
    {
      if ((fdo < 0) || (fdi < 0))
        {
          reopen_ttys ();
          if ((fdo >= 0) && (fdi >= 0))
            {
              cls (new->w,new->h);
              old->w = -1;
	      oldtty = tty;
            }
          else
            {
              cls (0,0);
              writestr (1, "Failed to gain access to console\n\r");
              writestr (1, "did you login as root?\n\r");
	      if (geteuid()) quit(-1);
	      if (oldtty != -1) {
		tty = oldtty;
		continue;
	      }
            }
        }

      FD_SET (0, &rfds);

      tv.tv_sec = 0;
      tv.tv_usec = POLL_INTERVAL;

      if (moaning) {
	sleep(moaning);
	moaning=2;
      }

      MOAN (select (1, &rfds, NULL, NULL, &tv));


      if (FD_ISSET (0, &rfds))
        {
          unsigned char c;
          read (0, &c, 1);

          if (c == ESCAPE)
            {
              do_escape ();
              reopen_ttys ();
	      cls(old->w,old->h);
              old->w = -1;
            }
          else
            {
              send_char (c);
            }

        }

      get_state (fdo, new);
      if (new->w != old->w || new->h != old->h) {
	cls(new->w,new->h);
      }
      update_output (old, new);

      {
        ScreenState temp = old;
        old = new;
        new = temp;
      }
    }
}
