VERSION = 0.2.0
RELNUM = 1
#OPTFLAGS = -O2
# We default to -g... our spec file overrides this setting anyway...
OPTFLAGS = -g
CFLAGS	= -Wall '-DVERSION="$(VERSION)"' $(OPTFLAGS)
LDFLAGS	=  -g #-s
EXES=lecd sled nca ecdrv cec

HFILES=cec.h
COMMON=system.o utils.o cec-common.o

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
SBINDIR=$(PREFIX)/sbin
MANDIR=$(PREFIX)/share/man

all: $(EXES)

lecd:	ec.o $(COMMON) nca.o
	$(CC) $(LDFLAGS) -o $@ $^

sled: sled.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $< $(COMMON) -lutil

nca:	nca.o nca-main.o
	$(CC) $(LDFLAGS) -o $@ $^

ecdrv:	ec-drv.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $< $(COMMON)

cec:	cec.o $(COMMON)
	$(CC) $(LDFLAGS) -o $@ $< $(COMMON)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $<


.PHONY: clean install

clean:
	rm -rf *~ *.rpm TMP *.o

distclean:
	rm -f *.o *~ *.t $(TARGETS) core *.rpm *.tar.gz *.log log.* $(EXES)
	rm -rf TMP

rpm:	all
	rm -rf TMP
	make PREFIX=/usr DESTDIR=$$(pwd)/TMP install
	fpm -s dir -t rpm \
		--name lec \
		--version $(VERSION) \
		--iteration $(RELNUM) \
		--license GPL,BSD2 \
		--epoch 0 \
		--category "System Environment/Base" \
		--maintainer "alejandro_liu@hotmail.com" \
		--description "Linux Ethernet Console" \
		--url "http://www.0ink.net/" \
		-C TMP .

install: all
	mkdir -p $(DESTDIR)$(PREFIX)
	for f in $(EXES) ; do \
		install -D -m 755 $$f $(DESTDIR)$(SBINDIR)/$$f ; done

	for m in `seq 0 9` ; do \
	  for f in *.$$m ; do \
	    [ ! -f $$f ] && continue ; \
	    mkdir -p $(DESTDIR)$(MANDIR)/man$$m ; \
	    cp -a $$f $(DESTDIR)$(MANDIR)/man$$m/$$f ; \
	    gzip -v $(DESTDIR)$(MANDIR)/man$$m/$$f ; done ; done
