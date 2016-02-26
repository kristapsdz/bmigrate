.SUFFIXES: .xml .html .in.bib .bib

VERSION 	 = 0.2.2
VDATE 		 = 2016-02-26
PREFIX 		 = /usr/local
VYEAR 		 = 2016
VMONTH 		 = February
VERSIONXML 	 = version_0_1_3.xml \
	   	   version_0_1_4.xml \
	   	   version_0_1_5.xml \
	   	   version_0_1_6.xml \
	   	   version_0_2_0.xml \
	   	   version_0_2_1.xml \
	   	   version_0_2_2.xml
DATADIR 	 = ${PREFIX}/share/bmigrate
CFLAGS 		+= -O3 -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DVERSION=\"$(VERSION)\" -DDATADIR=\"$(DATADIR)\"
GTK_OBJS 	 = bmigrate.o \
		   buf.o \
		   draw.o \
		   kml.o \
		   parser.o \
		   rangefind.o \
		   save.o \
		   simulation.o \
		   simwin.o \
		   widgets.o
SRCS	 	 = bmigrate.c \
		   buf.c \
		   draw.c \
		   kml.c \
		   parser.c \
		   rangefind.c \
		   save.c \
		   simulation.c \
		   simwin.c \
		   widgets.c
IMAGES 		 = screen-config.png
SHARE 		 = bmigrate.glade \
		   simwin.glade
ifeq ($(shell uname),Darwin)
GTK_CFLAGS 	:= $(shell pkg-config --cflags gsl gtk+-3.0 gtk-mac-integration-gtk3)
GTK_LIBS 	:= $(shell pkg-config --libs gsl gtk+-3.0 gtk-mac-integration-gtk3)
GTK_PREFIX 	 = ${HOME}/gtk/inst
else
GTK_LIBS 	:= $(shell pkg-config --libs gsl gtk+-3.0) -export-dynamic
GTK_CFLAGS 	:= $(shell pkg-config --cflags gsl gtk+-3.0)
endif
ifeq ($(shell uname),Linux)
BSDLIB 		 = -lbsd
else
BSDLIB 		 = 
endif

all: bmigrate manual.html

install: all 
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/bmigrate
	install -m 0755 bmigrate $(PREFIX)/bin
	install -m 0444 $(SHARE) $(PREFIX)/share/bmigrate

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

www: manual.html index.html bmigrate.bib bmigrate.tgz bmigrate.tgz.sha512

app: bmigrate.app.zip bmigrate.app.zip.sha512

installwww: www
	mkdir -p $(PREFIX)/snapshots
	install -m 0644 bmigrate.tgz $(PREFIX)/snapshots
	install -m 0644 bmigrate.tgz $(PREFIX)/snapshots/bmigrate-$(VERSION).tgz
	install -m 0644 bmigrate.tgz.sha512 $(PREFIX)/snapshots
	install -m 0644 bmigrate.tgz.sha512 $(PREFIX)/snapshots/bmigrate-$(VERSION).tgz.sha512
	install -m 0644 $(IMAGES) manual.css index.html evolve.png index.css manual.html bmigrate.bib $(PREFIX)

bmigrate.tgz.sha512: bmigrate.tgz
	openssl dgst -sha512 bmigrate.tgz >$@

bmigrate.app.zip.sha512: bmigrate.app.zip
	openssl dgst -sha512 bmigrate.app.zip >$@

$(GTK_OBJS): extern.h

bmigrate: $(GTK_OBJS)
	$(CC) -o $@ $(GTK_OBJS) $(GTK_LIBS) $(BSDLIB) -lkplot

bmigrate.tgz:
	mkdir -p .dist/bmigrate-$(VERSION)
	cp GNUmakefile .dist/bmigrate-$(VERSION)
	cp $(SRCS) $(SHARE) .dist/bmigrate-$(VERSION)
	(cd .dist && tar zcf ../$@ bmigrate-$(VERSION))
	rm -rf .dist

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

.xml.html: 
	sed -e "s!@VERSION@!$(VERSION)!g" \
	    -e "s!@VDATE@!$(VDATE)!g" \
	    -e "s!@VMONTH@!$(VMONTH)!g" \
	    -e "s!@VYEAR@!$(VYEAR)!g" $< >$@

index.html: index.xml $(VERSIONXML)
	sblg -o- -t index.xml $(VERSIONXML) | \
		sed -e "s!@VERSION@!$(VERSION)!g" \
		    -e "s!@VDATE@!$(VDATE)!g" \
		    -e "s!@VMONTH@!$(VMONTH)!g" \
		    -e "s!@VYEAR@!$(VYEAR)!g" >$@

.in.bib.bib:
	sed -e "s!@VERSION@!$(VERSION)!g" \
	    -e "s!@VDATE@!$(VDATE)!g" \
	    -e "s!@VMONTH@!$(VMONTH)!g" \
	    -e "s!@VYEAR@!$(VYEAR)!g" $< >$@

clean:
	rm -f bmigrate $(GTK_OBJS) manual.html bmigrate.xml index.html bmigrate.bib bmigrate.tgz bmigrate.tgz.sha512
	rm -rf bmigrate.app *.dSYM
	rm -f bmigrate.app.zip Info.plist bmigrate.app.zip.sha512
