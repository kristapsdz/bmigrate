.SUFFIXES: .xml .html .dbk .in.bib .bib

VERSION = 0.1.6
VDATE = 2014-09-22
PREFIX = /usr/local
VYEAR = 2014
VMONTH = August
VERSIONXML = version_0_1_3.xml \
	     version_0_1_4.xml \
	     version_0_1_5.xml \
	     version_0_1_6.xml
DATADIR = ${PREFIX}/share/bmigrate
CFLAGS += -O3 -g -W -Wall -Wstrict-prototypes -Wno-unused-parameter -Wwrite-strings -DVERSION=\"$(VERSION)\" -DDATADIR=\"$(DATADIR)\"
GTK_OBJS = bmigrate.o parser.o stats.o simulation.o draw.o save.o kml.o
IMAGES = screen-config.png screen1.png screen2.png screen3.png screen4.png screen5.png
SHARE = $(IMAGES) bmigrate.css bmigrate.glade simulation.glade bmigrate.html
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

ifeq ($(shell uname),Linux)
DOCBOOK = /usr/share/xml/docbook/stylesheet/docbook-xsl/xhtml/docbook.xsl
else ifeq ($(shell uname),Darwin)
DOCBOOK = ${HOME}/gtk/inst/share/xml/docbook/stylesheet/nwalsh/xhtml/docbook.xsl
else
DOCBOOK = /usr/local/share/xsl/docbook/xhtml/docbook.xsl
endif

all: bmigrate bmigrate.html

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

www: bmigrate.app.zip index.html bmigrate.bib bmigrate-$(VERSION).tgz 

installwww: www
	mkdir -p $(PREFIX)/snapshots
	install -m 0644 bmigrate.app.zip $(PREFIX)/snapshots
	install -m 0644 bmigrate-$(VERSION).tgz $(PREFIX)/snapshots
	install -m 0644 bmigrate-$(VERSION).tgz $(PREFIX)/snapshots/bmigrate.tgz
	install -m 0644 $(IMAGES) index.html evolve.png index.css bmigrate.html bmigrate.css bmigrate.bib $(PREFIX)

$(GTK_OBJS): extern.h

bmigrate: $(GTK_OBJS)
	$(CC) -o $@ $(GTK_OBJS) $(GTK_LIBS) $(BSDLIB)

bmigrate-$(VERSION).tgz:
	mkdir -p .dist/bmigrate-$(VERSION)
	cp GNUmakefile .dist/bmigrate-$(VERSION)
	cp bmigrate.c draw.c extern.h kml.c parser.c save.c simulation.c stats.c .dist/bmigrate-$(VERSION)
	cp bmigrate.dbk screen-config.png bmigrate.glade simulation.glade bmigrate.css .dist/bmigrate-$(VERSION)
	(cd .dist && tar zcf ../$@ bmigrate-$(VERSION))
	rm -rf .dist

.c.o:
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED -c -o $@ $<

.dbk.xml:
	xsltproc --stringparam section.autolabel 1 --stringparam html.stylesheet bmigrate.css -o $@ $(DOCBOOK) $<

.xml.html: 
	sed -e "s!@VERSION@!$(VERSION)!g" -e "s!@VDATE@!$(VDATE)!g" -e "s!@VMONTH@!$(VMONTH)!g" -e "s!@VYEAR@!$(VYEAR)!g" $< >$@

index.xml: index-template.xml $(VERSIONXML)
	sblg -o $@ -t index-template.xml $(VERSIONXML)

.in.bib.bib:
	sed -e "s!@VERSION@!$(VERSION)!g" -e "s!@VDATE@!$(VDATE)!g" -e "s!@VMONTH@!$(VMONTH)!g" -e "s!@VYEAR@!$(VYEAR)!g" $< >$@

clean:
	rm -f bmigrate $(GTK_OBJS) bmigrate.html bmigrate.xml index.html bmigrate.bib bmigrate-$(VERSION).tgz index.xml
	rm -rf bmigrate.app *.dSYM
	rm -f bmigrate.app.zip Info.plist
