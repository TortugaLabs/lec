LEC - Linux Ethernet Console
============================

```
 *   Copyright (C) 2009,2011 Alejandro Liu Ly
 *
 *   This is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation; either version 2 of 
 *   the License, or (at your option) any later version.
 *
 *   THis is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public
 *   License along with this program.  If not, see 
 *   <http://www.gnu.org/licenses/>
 *
 *   This software makes use of the uthash library.  See
 *   LICENSE.uthash for the uthash library specific license
 *   conditions

Original CEC client coyright:
/* Copyright Coraid, Inc. 2006.  All Rights Reserved. */
CEC was distributed with GNU license.

Original NCA Copyright:
Copyright (c) 2002-2004 James McKenzie <james@fishsoup.dhs.org>,
	      2004      Julian T. J. Midgley <jtjm@xenoclast.org>
nca was originally distributed under the terms of a 2-part BSD licence,
```

* * *

The Coraid Ethernet Console (cec) is a lightweight protocol for
connecting two endpoints using raw ethernet frames.  The communication
is not secure, but it is not routed.

Cec is also the name of the client used to connect to cec servers.
Coraid's Cec client will run on linux, and bsd flavors supporting bpf
(including OSX).

This software implements a CEC client and a CEC server.  It is mostly
compatible with the Coraid CEC client implementation.

It can be used to implement a very light weight network console which
does not need a file system nor full IP networking.

Included are the following:

* cec - The cec client
* lecd - Linux Ethernet Console daemon
* sled - Serial console redirector (Used for headless embedded devices)
* nca - Used to access the physical Linux console
* ec-drv - cec driver for serial console

Examples
--------

To use on a PC Linux server on Ethernet0 add this line (as early as you 
can in the init scripts)

: lecd eth0 &

To connect to a remote Console, enter on the command line:

: cec eth0

To use with headless server with a Serial console, replace your init
program with this command line:

: /sbin/sled eth0 -- /sbin/init "$@"

Which expects to find /sbin/ec-drv or use "-e" to override.

To simply look at a Linux console:

: nca

An alternative way to have a Ethernet console on a PC Linux server:

: ec-drv eth0 -- /sbin/nca -1 &

This has the advantage over the first example, in that the nca process
will be pre-forked and running so in the event of a process table
full, it would still work. The disadvantage is that nca is polling the
screen at regular intervals so this option will drive the system load
up.

TODO
----

* Implement a pre-forking model for lecd.

Related Projects
----------------

This software makes use of code from the following projects:

* cec-11: Coraid Ethernet Console (client)  
  [AoE Tools](http://aoetools.sourceforge.net/)  
  This software package contained the client utility but not server
  component.  There was a however a document describing the protocol.
  The LEC daemon is based on that description.
  See README.cec for the details on cec-11.
* NCA: Network Console on Acid  
  [NCA](http://www.xenoclast.org/nca/)  
  This package was the source for the *nca* executable.  The *nca*
  executable found here has been modified so that it does not
  depend on having a pty available.  Also, the networking component
  in NCA was openssh.  This has been replaced with LEC in order to
  reduce the run-time footprint of this application.
  See README.nca for the details on README.nca.

Hacking
-------

tcpdump options:

: tcpdump -i eth0 -n -nn ether proto 0xbcbc
