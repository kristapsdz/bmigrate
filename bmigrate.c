/*	$Id$ */
/*
 * Copyright (c) 2014 Kristaps Dzonsons <kristaps@kcons.eu>
 *
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef MAC_INTEGRATION
#include <gtkosxapplication.h>
#endif
#include <cairo.h>
#include <cairo-pdf.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "extern.h"

enum	page {
	PAGE_UNIFORM,
	PAGE_MAPPED,
	PAGE__MAX
};

enum	payoff {
	PAYOFF_CONTINUUM,
	PAYOFF_SYMMETRIC2,
	PAYOFF__MAX
};

/*
 * These are all widgets that may be or are visible.
 */
struct	hwin {
	GtkWindow	*config;
	GtkMenuBar	*menu;
	GtkMenuItem	*menuquit;
	GtkEntry	*cfg;
	GtkBox		*poff[PAYOFF__MAX];
	GtkToggleButton	*toggle[PAYOFF__MAX];
	GtkNotebook	*pages;
	GtkLabel	*error;
	GtkComboBoxText	*preset;
	GtkEntry	*func;
};

/*
 * Main structure governing general state of the system.
 */
struct	bmigrate {
	struct hwin	  wins; /* GUI components */
};

static	const char *const cfgpages[PAGE__MAX] = {
	"uniform",
	"mapped",
};

/*
 * Initialise the fixed widgets.
 * Some widgets (e.g., "processing" dialog) are created dynamically and
 * will not be marshalled here.
 */
static void
windows_init(struct bmigrate *b, GtkBuilder *builder)
{
	enum payoff	 p;

	b->wins.config = GTK_WINDOW
		(gtk_builder_get_object(builder, "window1"));
	b->wins.menu = GTK_MENU_BAR
		(gtk_builder_get_object(builder, "menubar1"));
	b->wins.menuquit = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem5"));
	b->wins.cfg = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry3"));
	b->wins.pages = GTK_NOTEBOOK
		(gtk_builder_get_object(builder, "notebook1"));
	b->wins.poff[PAYOFF_CONTINUUM] = GTK_BOX
		(gtk_builder_get_object(builder, "box17"));
	b->wins.poff[PAYOFF_SYMMETRIC2] = GTK_BOX
		(gtk_builder_get_object(builder, "box14"));
	b->wins.toggle[PAYOFF_CONTINUUM] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton1"));
	b->wins.toggle[PAYOFF_SYMMETRIC2] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton2"));
	b->wins.error = GTK_LABEL
		(gtk_builder_get_object(builder, "label8"));
	b->wins.preset = GTK_COMBO_BOX_TEXT
		(gtk_builder_get_object(builder, "comboboxtext1"));
	b->wins.func = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry2"));

	/* Set the initially-selected notebook. */
	gtk_entry_set_text(b->wins.cfg, cfgpages
		[gtk_notebook_get_current_page(b->wins.pages)]);

	/* Set the initially-selected payoff type. */
	for (p = 0; p < PAYOFF__MAX; p++)
		if (gtk_toggle_button_get_active(b->wins.toggle[p]))
			gtk_widget_set_sensitive
				(GTK_WIDGET(b->wins.poff[p]), TRUE);

	/* Builder doesn't do this. */
	gtk_combo_box_set_active(GTK_COMBO_BOX(b->wins.preset), 0);
}

/*
 * Free up all memory.
 * This can be reentrant (due to gtk-osx's funny handling of
 * termination), so be careful to nullify things so that if recalled it
 * doesn't puke.
 */
static void
bmigrate_free(struct bmigrate *p)
{

	memset(p, 0, sizeof(struct bmigrate));
}

/*
 * Quit everything (gtk_main returns).
 */
void
onquit(GtkMenuItem *menuitem, gpointer dat)
{

	bmigrate_free(dat);
	gtk_main_quit();
}

/*
 * Window destroy, quit everything (gtk_main returns).
 */
void 
ondestroy(GtkWidget *object, gpointer dat)
{
	
	bmigrate_free(dat);
	gtk_main_quit();
}

/*
 * We've toggled a given payoff class.
 * First, set all payoff classes to have insensitive input.
 * Then make the current one sensitive.
 */
void
on_toggle_payoff(GtkToggleButton *button, gpointer dat)
{
	struct bmigrate	*b = dat;
	enum payoff	 p;

	for (p = 0; p < PAYOFF__MAX; p++)
		gtk_widget_set_sensitive
			(GTK_WIDGET(b->wins.poff[p]), FALSE);

	for (p = 0; p < PAYOFF__MAX; p++) {
		if (button != b->wins.toggle[p])
			continue;
		gtk_widget_set_sensitive
			(GTK_WIDGET(b->wins.poff[p]), TRUE);
		break;
	}
}

/*
 * We want to run the given simulation.
 * First, verify all data.
 * Then actually run.
 */
void
on_activate(GtkButton *button, gpointer dat)
{
	gint	 	 page;
	struct bmigrate	*b = dat;

	page = gtk_notebook_get_current_page(b->wins.pages);
	if (PAGE_MAPPED == page) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: mapped type not supported.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}

	if (gtk_toggle_button_get_active
		(b->wins.toggle[PAYOFF_SYMMETRIC2])) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: symmetric payoffs not supported.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}
}

/*
 * One of the preset continuum functions.
 */
void
onpreset(GtkComboBox *widget, gpointer dat)
{
	struct bmigrate	*b = dat;

	switch (gtk_combo_box_get_active(widget)) {
	case (1):
		/* Tullock */
		gtk_entry_set_text(b->wins.func, 
			"x * (1 / X) - x");
		break;
	case (2):
		/* Cournot */
		gtk_entry_set_text(b->wins.func, 
			"x - (X - x) * x - x^2");
		break;
	case (3):
		/* Exponential Public Goods */
		gtk_entry_set_text(b->wins.func, 
			"(1 - exp(-X)) - x");
		break;
	case (4):
		/* Quadratic Public Goods */
		gtk_entry_set_text(b->wins.func, 
			"sqrt(1 / n * X) - 0.5 * x^2");
		break;
	default:
		gtk_entry_set_text(b->wins.func, "");
		break;
	}
}

/*
 * Run this whenever we select a page from the configuration notebook.
 * This sets (for the user) the current configuration in an entry.
 */
gboolean
on_change_page(GtkNotebook *notebook, GtkWidget *page, gint pnum, gpointer dat)
{
	struct bmigrate	*b = dat;

	assert(pnum < PAGE__MAX);
	gtk_entry_set_text(b->wins.cfg, cfgpages[pnum]);

	return(TRUE);
}

#ifdef MAC_INTEGRATION
void
onterminate(GtkosxApplication *action, gpointer dat)
{

	bmigrate_free(dat);
	gtk_main_quit();
}
#endif

int 
main(int argc, char *argv[])
{
	GtkBuilder	  *builder;
	struct bmigrate	   b;
	guint		   rc;
	gchar	 	  *file;
#ifdef	MAC_INTEGRATION
	gchar	 	  *dir;
	GtkosxApplication *theApp;
#endif

	file = NULL;
	memset(&b, 0, sizeof(struct bmigrate));
	gtk_init(&argc, &argv);

	/*
	 * Look up our `glade' file as follows.
	 * If we're in MAC_INTEGRATION, then intuit whether we're in a
	 * bundle and, if so, look up within the bundle.
	 * If we're not in a bundle, look in DATADIR.
	 */
#ifdef	MAC_INTEGRATION
	theApp = g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
	if (NULL != (dir = gtkosx_application_get_bundle_id())) {
		g_free(dir);
		dir = gtkosx_application_get_resource_path();
		file = g_strdup_printf
			("%s" G_DIR_SEPARATOR_S
			 "share" G_DIR_SEPARATOR_S
			 "bmigrate" G_DIR_SEPARATOR_S
			 "bmigrate.glade", dir);
		g_free(dir);
	}
#endif
	if (NULL == file)
		file = g_strdup_printf(DATADIR 
			G_DIR_SEPARATOR_S "%s", "bmigrate.glade");

	builder = gtk_builder_new();
	assert(NULL != builder);

	/* If we fail this, just exit and good-bye. */
	rc = gtk_builder_add_from_file(builder, file, NULL);
	g_free(file);
	if (0 == rc)
		return(EXIT_FAILURE);

	windows_init(&b, builder);

	gtk_builder_connect_signals(builder, &b);
	g_object_unref(G_OBJECT(builder));

	/*
	 * Start up the window system.
	 * First, show all windows.
	 * If we're on the Mac, do a little dance with menus as
	 * prescribed in the GTK+OSX manual.
	 */
	gtk_widget_show_all(GTK_WIDGET(b.wins.config));
	gtk_widget_hide(GTK_WIDGET(b.wins.error));
#ifdef	MAC_INTEGRATION
	theApp = gtkosx_application_get();
	gtk_widget_hide(GTK_WIDGET(b.wins.menu));
	gtk_widget_hide(GTK_WIDGET(b.wins.menuquit));
	gtkosx_application_set_menu_bar
		(theApp, GTK_MENU_SHELL(b.wins.menu));
	gtkosx_application_sync_menubar(theApp);
	g_signal_connect(theApp, "NSApplicationWillTerminate",
		G_CALLBACK(onterminate), &b);
	gtkosx_application_ready(theApp);
#endif
	gtk_main();
	bmigrate_free(&b);
	return(EXIT_SUCCESS);
}
