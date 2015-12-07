/*
 * System dependant stuff
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

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <features.h>    /* for the glibc version number */
#if __GLIBC__ >= 2 && __GLIBC_MINOR >= 1
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */
#else
#include <asm/types.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#endif

#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <termios.h>

#include "cec.h"

extern int debug;
int netfd;
char net_bytes[1<<14];
int net_len;
char srcaddr[6];

int
getindx(int s, char *name)	// return the index of device 'name'
{
	struct ifreq xx;
	int n;

	strcpy(xx.ifr_name, name);
	n = ioctl(s, SIOCGIFINDEX, &xx);
	if (n == -1)
		return -1;
	return xx.ifr_ifindex;
}

int netclose(void) {
  return close(netfd);
}


int
netopen(char *eth)		// get us a raw connection to an interface
{
	int i, n;
	struct sockaddr_ll sa;
	struct ifreq xx;

	memset(&sa, 0, sizeof sa);
	netfd = socket(PF_PACKET, SOCK_RAW, htons(CEC_ETYPE));
	if (netfd == -1) {
		perror("got bad socket");
		return -1;
	}
	i = getindx(netfd, eth);
	sa.sll_family = AF_PACKET;
	sa.sll_protocol = htons(CEC_ETYPE);
	sa.sll_ifindex = i;
	n = bind(netfd, (struct sockaddr *)&sa, sizeof sa);
	if (n == -1) {
		perror("bind funky");
		return -1;
	}
        strcpy(xx.ifr_name, eth);
	n = ioctl(netfd, SIOCGIFHWADDR, &xx);
	if (n == -1) {
		perror("Can't get hw addr");
		return -1;
	}
	memmove(srcaddr, xx.ifr_hwaddr.sa_data, 6);
	return 0;
}

int
netrecv(void)
{
	net_len = read(netfd, net_bytes, sizeof net_bytes);
	if (debug) {
		printf("read %d bytes\r\n", net_len);
		dump(net_bytes, net_len);
	}
	return net_len;
}

int
netget(void *ap, int len)
{
	if (net_len <= 0)
		return 0;
	if (len > net_len)
		len = net_len;
	memcpy(ap, net_bytes, len);
	net_len = 0;
	return len;
}

int
netsend(void *p, int len)
{
	memcpy(p+6, srcaddr, 6);
	if (debug) {
		printf("sending %d bytes\r\n", len);
		dump(p, len);
	}
	if (len < 60)
		len = 60;
	return write(netfd, p, len);
}


int _netup(int fd,char *eth) {
  struct ifreq xx;
  struct sockaddr_in sin;

  strncpy(xx.ifr_name,eth,IFNAMSIZ);

  /* Check if interface is running... */
  if (ioctl(fd,SIOCGIFFLAGS,&xx) == -1) return -1;

  /* If running, we do an early exit */
  if ((xx.ifr_flags & (IFF_UP|IFF_RUNNING)) == (IFF_UP|IFF_RUNNING))
    return 0;

  /* Set interface to up ... */
  xx.ifr_flags |= IFF_UP|IFF_RUNNING;
  if (ioctl(fd,SIOCSIFFLAGS, &xx) == -1) return -1;

  /* Set up a dummy internet address... */
  memset(&sin,0,sizeof sin);
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = 0;
  memcpy(&xx.ifr_addr,&sin,sizeof sin);
  if (ioctl(fd,SIOCSIFADDR, &xx) == -1) return -1;

  return 0;
}

int netup(char *eth) {
  int rr, fd = socket(AF_INET,SOCK_DGRAM,IPPROTO_IP);
  if (fd == -1) return -1;
  rr = _netup(fd,eth);
  close(fd);
  return rr;
}
static struct  termios attr;
static int raw = 0;

static void
_cfmakeraw(struct termios *t)
{
	t->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	t->c_cflag &= ~(CSIZE|PARENB);
	t->c_cc[VMIN] = 1;
	t->c_cflag |= CS8;
}

void
rawon(void)
{
	struct termios nattr;
	
	if (tcgetattr(STDIN_FILENO, &attr) != 0) {
		fprintf(stderr, "Can't make raw\n");
		return;
	}
	nattr = attr;
	_cfmakeraw(&nattr);
	tcsetattr(0, TCSAFLUSH, &nattr);
	raw = 1;
}

void
rawoff(void)
{
	if(raw)
		tcsetattr(0,TCSAFLUSH, &attr);
	raw = 0;
}
