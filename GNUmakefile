.SUFFIXES: .xml .html .dbk 

VERSION = 0.0.10
PREFIX = /usr/local
DATADIR = ${PREFIX}/share/bmigrate
CFLAGS += -O3 -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DVERSION=\"$(VERSION)\" -DDATADIR=\"$(DATADIR)\"
GTK_OBJS = bmigrate.o parser.o stats.o simulation.o draw.o
IMAGES = screen-config.png \
	 screen-win1.png \
	 screen-win2.png \
	 screen-win3.png \
	 screen-win4.png \
	 screen-win5.png \
	 screen-win6.png
SHARE = $(IMAGES) bmigrate.css bmigrate.glade bmigrate.html
ifeq ($(shell uname),Darwin)
GTK_CFLAGS := $(shell pkg-config --cflags gsl gtk-mac-integration)
GTK_LIBS := $(shell pkg-config --libs gsl gtk-mac-integration)
GTK_PREFIX = ${HOME}/gtk/inst
DOCBOOK_PREFIX = ${HOME}/gtk/inst
else
GTK_LIBS := $(shell pkg-config --libs gsl gtk+-3.0) -export-dynamic
GTK_CFLAGS := $(shell pkg-config --cflags gsl gtk+-3.0)
DOCBOOK_PREFIX = $(PREFIX)
endif
ifeq ($(shell uname),Linux)
BSDLIB = -lbsd
DOCBOOK_PREFIX = /usr
else
BSDLIB = 
endif
DOCBOOK = $(DOCBOOK_PREFIX)/share/xml/docbook/stylesheet/nwalsh/xhtml/docbook.xsl

all: bmigrate bmigrate.html

install: all
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(PREFIX)/bin
	install -m 0444 $(SHARE) $(PREFIX)/share/bmigrate

ifeq ($(shell uname),Darwin)
bmigrate.app.zip: bmigrate.app
	zip -r -q bmigrate.app.zip bmigrate.app

Info.plist: Info.plist.xml
	sed "s!@VERSION@!$(VERSION)!g" Info.plist.xml >$@

bmigrate.app: all Info.plist
	mkdir -p $(GTK_PREFIX)/bin
	mkdir -p $(GTK_PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(GTK_PREFIX)/bin
	install -m 0444 $(SHARE) $(GTK_PREFIX)/share/bmigrate
	rm -rf bmigrate.app
	gtk-mac-bundler bmigrate.bundle

www: bmigrate.app.zip index.html

installwww: www
	mkdir -p $(PREFIX)
	install -m 0644 bmigrate.app.zip $(PREFIX)
	install -m 0644 $(IMAGES) index.html index.css bmigrate.html bmigrate.css $(PREFIX)
endif

$(GTK_OBJS): extern.h

bmigrate: $(GTK_OBJS)
	$(CC) -o $@ $(GTK_OBJS) $(GTK_LIBS) $(BSDLIB)

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -c -o $@ $<

.dbk.html:
	xsltproc --stringparam html.stylesheet bmigrate.css -o $@~ $(DOCBOOK) $<
	( echo '<!DOCTYPE html>' ; cat $@~ ) | sed "s!@VERSION@!$(VERSION)!g" >$@
	rm -f $@~

index.html: index.xml
	sed "s!@VERSION@!$(VERSION)!g" index.xml >$@

clean:
	rm -f bmigrate $(GTK_OBJS) bmigrate.html bmigrate.html~ index.html
	rm -rf bmigrate.app *.dSYM
	rm -f bmigrate.app.zip Info.plist
