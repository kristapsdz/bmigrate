/*	$Id$ */
/*
 * Copyright (c) 2014, 2015 Kristaps Dzonsons <kristaps@kcons.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef MAC_INTEGRATION
#include <gtkosxapplication.h>
#endif
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>
#include <kplot.h>

#include "extern.h"

#define DEFINE_GTK_INIT(_class, _type, _name) \
_class * \
win_init_ ##_name (GtkBuilder *b, const gchar *name) \
{ \
	g_assert(gtk_builder_get_object(b, name)); \
	g_assert(GTK_IS_##_type (gtk_builder_get_object(b, name))); \
	return(GTK_ ##_type (gtk_builder_get_object(b, name))); \
}

DEFINE_GTK_INIT(GtkAdjustment, ADJUSTMENT, adjustment)
DEFINE_GTK_INIT(GtkStatusbar, STATUSBAR, status)
DEFINE_GTK_INIT(GtkDrawingArea, DRAWING_AREA, draw)
DEFINE_GTK_INIT(GtkMenuBar, MENU_BAR, menubar)
DEFINE_GTK_INIT(GtkCheckMenuItem, CHECK_MENU_ITEM, menucheck)
DEFINE_GTK_INIT(GtkMenuItem, MENU_ITEM, menuitem)
DEFINE_GTK_INIT(GtkWindow, WINDOW, window)
DEFINE_GTK_INIT(GtkLabel, LABEL, label)
DEFINE_GTK_INIT(GtkBox, BOX, box)
DEFINE_GTK_INIT(GtkButton, BUTTON, button)
DEFINE_GTK_INIT(GtkRadioButton, RADIO_BUTTON, radio)
DEFINE_GTK_INIT(GtkNotebook, NOTEBOOK, notebook)
DEFINE_GTK_INIT(GtkToggleButton, TOGGLE_BUTTON, toggle)
DEFINE_GTK_INIT(GtkEntry, ENTRY, entry)
DEFINE_GTK_INIT(GtkFileChooser, FILE_CHOOSER, filechoose)

GtkBuilder *
builder_get(const gchar *name)
{
	gchar		*file = NULL;
#ifdef MAC_INTEGRATION
	gchar		*dir;
#endif
	GtkBuilder	*builder;

	/*
	 * Look up our `glade' file as follows.
	 * If we're in MAC_INTEGRATION, then intuit whether we're in a
	 * bundle and, if so, look up within the bundle.
	 * If we're not in a bundle, look in DATADIR.
	 */
#ifdef MAC_INTEGRATION
	g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
	if (NULL != (dir = gtkosx_application_get_bundle_id())) {
		g_free(dir);
		dir = gtkosx_application_get_resource_path();
		file = g_strdup_printf
			("%s" G_DIR_SEPARATOR_S
			 "share" G_DIR_SEPARATOR_S
			 "bmigrate" G_DIR_SEPARATOR_S
			 "%s", dir, name);
		g_free(dir);
	}
#endif
	if (NULL == file)
		file = g_strdup_printf(DATADIR 
			G_DIR_SEPARATOR_S "%s", name);

	builder = gtk_builder_new();
	assert(NULL != builder);

	/*
	 * This should be gtk_builder_new_from_file(), but we don't
	 * support that on older versions of GTK, so do it like this.
	 */
	if ( ! gtk_builder_add_from_file(builder, file, NULL)) {
		g_free(file);
		g_object_unref(G_OBJECT(builder));
		return(NULL);
	}

	g_free(file);
	return(builder);
}

