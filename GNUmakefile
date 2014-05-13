.SUFFIXES: .xml .html

VERSION = 0.0.4
PREFIX = /usr/local
DATADIR = ${PREFIX}/share/bmigrate
CFLAGS += -O3 -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DVERSION=\"$(VERSION)\" -DDATADIR=\"$(DATADIR)\"
GTK_OBJS = bmigrate.o 
ifeq ($(shell uname),Darwin)
GTK_CFLAGS := $(shell pkg-config --cflags gtk-mac-integration)
GTK_LIBS := $(shell pkg-config --libs gtk-mac-integration)
GTK_PREFIX = ${HOME}/gtk/inst
else
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0) -export-dynamic
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
endif

all: bmigrate 

install: all
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(PREFIX)/bin
	install -m 0444 bmigrate.glade $(PREFIX)/share/bmigrate

ifeq ($(shell uname),Darwin)
bmigrate.app.zip: bmigrate.app
	zip -r bmigrate.app.zip bmigrate.app

Info.plist: Info.plist.xml
	sed "s!@VERSION@!$(VERSION)!g" Info.plist.xml >$@

bmigrate.app: all Info.plist
	mkdir -p $(GTK_PREFIX)/bin
	mkdir -p $(GTK_PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(GTK_PREFIX)/bin
	install -m 0444 bmigrate.glade $(GTK_PREFIX)/share/bmigrate
	rm -rf bmigrate.app
	gtk-mac-bundler bmigrate.bundle
endif

bmigrate: $(GTK_OBJS)
	$(CC) -o $@ $(GTK_OBJS) $(GTK_LIBS)

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -c -o $@ $<

clean:
	rm -f bmigrate $(GTK_OBJS) 
	rm -rf bmigrate.app *.dSYM
	rm -f bmigrate.app.zip Info.plist
