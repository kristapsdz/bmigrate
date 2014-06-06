.SUFFIXES: .xml .html .docbook 

VERSION = 0.0.7
PREFIX = /usr/local
DATADIR = ${PREFIX}/share/bmigrate
CFLAGS += -O3 -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DVERSION=\"$(VERSION)\" -DDATADIR=\"$(DATADIR)\"
GTK_OBJS = bmigrate.o parser.o simulation.o
ifeq ($(shell uname),Darwin)
GTK_CFLAGS := $(shell pkg-config --cflags gsl gtk-mac-integration)
GTK_LIBS := $(shell pkg-config --libs gsl gtk-mac-integration)
GTK_PREFIX = ${HOME}/gtk/inst
else
GTK_LIBS := $(shell pkg-config --libs gsl gtk+-3.0) -export-dynamic
GTK_CFLAGS := $(shell pkg-config --cflags gsl gtk+-3.0)
endif
ifeq ($(shell uname),Linux)
BSDLIB = -lbsd
else
BSDLIB = 
endif

all: bmigrate bmigrate.html

install: all
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(PREFIX)/bin
	install -m 0444 bmigrate.glade bmigrate.html bmigrate.css $(PREFIX)/share/bmigrate

ifeq ($(shell uname),Darwin)
bmigrate.app.zip: bmigrate.app
	zip -r bmigrate.app.zip bmigrate.app

Info.plist: Info.plist.xml
	sed "s!@VERSION@!$(VERSION)!g" Info.plist.xml >$@

bmigrate.app: all Info.plist
	mkdir -p $(GTK_PREFIX)/bin
	mkdir -p $(GTK_PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(GTK_PREFIX)/bin
	install -m 0444 bmigrate.glade bmigrate.html bmigrate.css $(GTK_PREFIX)/share/bmigrate
	rm -rf bmigrate.app
	gtk-mac-bundler bmigrate.bundle

installwww: bmigrate.app.zip
	mkdir -p $(PREFIX)
	install -m 0644 bmigrate.app.zip $(PREFIX)
	install -m 0644 bmigrate.html bmigrate.css $(PREFIX)
endif

$(GTK_OBJS): extern.h

bmigrate: $(GTK_OBJS)
	$(CC) -o $@ $(GTK_OBJS) $(GTK_LIBS) $(BSDLIB)

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -c -o $@ $<

.docbook.html:
	xsltproc --stringparam html.stylesheet bmigrate.css -o $@~ /Users/kristaps/gtk/inst/share/xml/docbook/stylesheet/nwalsh/html/docbook.xsl $<
	( echo '<!DOCTYPE html>' ; cat $@~ ) >$@
	rm -f $@~

clean:
	rm -f bmigrate $(GTK_OBJS) bmigrate.html bmigrate.html~
	rm -rf bmigrate.app *.dSYM
	rm -f bmigrate.app.zip Info.plist
