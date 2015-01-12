/*	$Id$ */
/*
 * Copyright (c) 2014 Kristaps Dzonsons <kristaps@kcons.eu>
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

#include "extern.h"

static	const char *const inputs[INPUT__MAX] = {
	"uniform",
	"variable",
	"mapped",
};

static	const char *const colours[SIZE_COLOURS] = {
	"#9400d3",
	"#009e73",
	"#56b4e9",
	"#e69f00",
	"#f0e442",
	"#0072b2",
	"#e51e10",
	"black",
	"gray50"
};

static	const char *const views[VIEW__MAX] = {
	"config", /* VIEW_CONFIG */
	"raw-mean-stddev", /* VIEW_DEV */
	"extinct-incumbent", /* VIEW_EXTI */
	"extinct-incumbent-min-cdf", /* VIEW_EXTIMINCDF */
	"extinct-incumbent-min-pdf", /* VIEW_EXTIMINPDF */
	"extinct-incumbent-min-mean", /* VIEW_EXTIMINS */
	"extinct-mutant", /* VIEW_EXTM */
	"extinct-mutant-max-cdf", /* VIEW_EXTMMAXCDF */
	"extinct-mutant-max-pdf", /* VIEW_EXTMMAXPDF */
	"extinct-mutant-max-mean", /* VIEW_EXTMMAXS */
	"island-mean", /* VIEW_ISLANDMEAN */
	"raw-mean", /* VIEW_MEAN */
	"raw-mean-min-cdf", /* VIEW_MEANMINCDF */
	"raw-mean-min-pdf", /* VIEW_MEANMINPDF */
	"raw-mean-min-history", /* VIEW_MEANMINQ */
	"raw-mean-min-mean", /* VIEW_MEANMINS */
	"fitted-mean", /* VIEW_POLY */
	"fitted-mean-min-cdf", /* VIEW_POLYMINCDF */
	"fitted-mean-min-pdf", /* VIEW_POLYMINPDF */
	"fitted-mean-min-history", /* VIEW_POLYMINQ */
	"fitted-mean-min-mean", /* VIEW_POLYMINS */
	"extinct-mutant-smooth", /* VIEW_SEXTM */
	"extinct-mutant-smooth-max-cdf", /* VIEW_SEXTMMAXCDF */
	"extinct-mutant-smooth-max-pdf", /* VIEW_SEXTMMAXPDF */
	"raw-mean-smooth", /* VIEW_SMEAN */
	"raw-mean-smooth-min-cdf", /* VIEW_SMEANMINCDF */
	"raw-mean-smooth-min-pdf", /* VIEW_SMEANMINPDF */
	"raw-mean-smooth-min-history", /* VIEW_SMEANMINQ */
	"raw-mean-smooth-min-mean", /* VIEW_SMEANMINS */
	"status", /* VIEW_STATUS */
};

/*
 * We have various ways of auto-setting the name of the simulation.
 * Perform all of them here.
 * (By default, it's set to the current date-time.)
 */
static void
donamefill(struct bmigrate *b)
{
	enum namefill	 v;
	GTimeVal	 gt;
	enum input	 input;
	gchar		 buf[1024];
	const gchar	*bufp;
	enum mutants	 mutants;

	/*
	 * We need some context information for filling in these values.
	 *  (1) which input tab (for m)
	 *  (2) which mutants (for mutants)
	 */
	input = gtk_notebook_get_current_page(b->wins.inputs);
	g_assert(input < INPUT__MAX);

	for (mutants = 0; mutants < MUTANTS__MAX; mutants++)
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON(b->wins.mutants[mutants])))
			break;

	for (v = 0; v < NAMEFILL__MAX; v++)
		if (gtk_toggle_button_get_active
			(b->wins.namefill[v]))
			break;

	/*
	 * Now fill in the name.
	 */
	switch (v) {
	case (NAMEFILL_M):
		g_snprintf(buf, sizeof(buf), "m=%s", 
			gtk_entry_get_text(b->wins.migrate[input]));
		bufp = buf;
		break;
	case (NAMEFILL_T):
		g_snprintf(buf, sizeof(buf), "T=%s", 
			gtk_entry_get_text(b->wins.stop));
		bufp = buf;
		break;
	case (NAMEFILL_MUTANTS):
		if (MUTANTS_DISCRETE == mutants)
			g_snprintf(buf, sizeof(buf), 
				"discrete [%s,%s)", 
				gtk_entry_get_text(b->wins.xmin),
				gtk_entry_get_text(b->wins.xmax));
		else
			g_snprintf(buf, sizeof(buf), 
				"Gaussian s=%s, [%s,%s)", 
				gtk_entry_get_text(b->wins.mutantsigma),
				gtk_entry_get_text(b->wins.ymin),
				gtk_entry_get_text(b->wins.ymax));
		bufp = buf;
		break;
	case (NAMEFILL_DATE):
		g_get_current_time(&gt);
		bufp = g_time_val_to_iso8601(&gt);
		break;
	case (NAMEFILL_NONE):
		return;
	default:
		abort();
	}

	gtk_entry_set_text(b->wins.name, bufp);
}

static void
swin_init(struct curwin *cur, GtkBuilder *builder)
{

	cur->wins.window = GTK_WINDOW
		(gtk_builder_get_object(builder, "window1"));
	cur->wins.menu = GTK_MENU_BAR
		(gtk_builder_get_object(builder, "menubar1"));
	cur->wins.notebook = GTK_NOTEBOOK
		(gtk_builder_get_object(builder, "notebook1"));
	cur->wins.menufile = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem1"));
	cur->wins.menuview = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem2"));
	cur->wins.menutools = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem3"));
	cur->wins.viewclone = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem15"));
	cur->wins.viewpause = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem20"));
	cur->wins.viewunpause = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem21"));
	cur->wins.views[VIEW_ISLANDMEAN] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem45"));
	cur->wins.views[VIEW_MEAN] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem8"));
	cur->wins.views[VIEW_SMEAN] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem37"));
	cur->wins.views[VIEW_SEXTM] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem43"));
	cur->wins.views[VIEW_EXTM] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem25"));
	cur->wins.views[VIEW_EXTMMAXPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem28"));
	cur->wins.views[VIEW_EXTMMAXCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem29"));
	cur->wins.views[VIEW_EXTI] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem26"));
	cur->wins.views[VIEW_EXTIMINPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem27"));
	cur->wins.views[VIEW_EXTIMINCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem30"));
	cur->wins.views[VIEW_SMEANMINPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem38"));
	cur->wins.views[VIEW_SMEANMINCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem39"));
	cur->wins.views[VIEW_SEXTMMAXPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem52"));
	cur->wins.views[VIEW_SEXTMMAXCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem51"));
	cur->wins.views[VIEW_SMEANMINQ] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem41"));
	cur->wins.views[VIEW_SMEANMINS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem40"));
	cur->wins.views[VIEW_EXTIMINS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem35"));
	cur->wins.views[VIEW_DEV] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem6"));
	cur->wins.views[VIEW_POLY] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem7"));
	cur->wins.views[VIEW_POLYMINPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem9"));
	cur->wins.views[VIEW_POLYMINCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem11"));
	cur->wins.views[VIEW_MEANMINPDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem10"));
	cur->wins.views[VIEW_MEANMINCDF] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem12"));
	cur->wins.views[VIEW_MEANMINQ] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem13"));
	cur->wins.views[VIEW_MEANMINS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem22"));
	cur->wins.views[VIEW_POLYMINS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem31"));
	cur->wins.views[VIEW_EXTMMAXS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem33"));
	cur->wins.views[VIEW_POLYMINQ] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem14"));
	cur->wins.views[VIEW_CONFIG] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem36"));
	cur->wins.views[VIEW_STATUS] = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem46"));
	cur->wins.menuquit = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem5"));
	cur->wins.menuautoexport = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem49"));
	cur->wins.menuunautoexport = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem50"));
	cur->wins.menuclose = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem24"));
	cur->wins.menusave = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem34"));
	cur->wins.menusavekml = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem17"));
	cur->wins.menusaveall = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem47"));

	gtk_widget_show_all(GTK_WIDGET(cur->wins.window));

	gtk_window_set_title(GTK_WINDOW(cur->wins.window),
		gtk_menu_item_get_label
		(GTK_MENU_ITEM(cur->wins.views[cur->view])));

	gtk_widget_hide(GTK_WIDGET(cur->wins.menuunautoexport));

#ifdef MAC_INTEGRATION
	gtk_widget_hide(GTK_WIDGET(cur->wins.menu));
	gtk_widget_hide(GTK_WIDGET(cur->wins.menuquit));
	g_debug("cwins.menu = %p", cur->wins.menu);
	gtkosx_application_set_menu_bar
		(gtkosx_application_get(), 
		 GTK_MENU_SHELL(cur->wins.menu));
	gtkosx_application_sync_menubar
		(gtkosx_application_get());
#endif
}

/*
 * Extract the widgets we want to know about from the builder.
 */
static void
hwin_init(struct bmigrate *b, GtkBuilder *builder)
{
	GObject		*w;
	gchar		 buf[1024];
	gboolean	 val;
	size_t		 i;

	b->wins.rangeminlambda = GTK_LABEL
		(gtk_builder_get_object(builder, "label55"));
	b->wins.rangemaxlambda = GTK_LABEL
		(gtk_builder_get_object(builder, "label52"));
	b->wins.rangemeanlambda = GTK_LABEL
		(gtk_builder_get_object(builder, "label58"));
	b->wins.rangeerrorbox = GTK_BOX
		(gtk_builder_get_object(builder, "box39"));
	b->wins.rangeerror = GTK_LABEL
		(gtk_builder_get_object(builder, "label48"));
	b->wins.rangemin = GTK_LABEL
		(gtk_builder_get_object(builder, "label42"));
	b->wins.rangemax = GTK_LABEL
		(gtk_builder_get_object(builder, "label40"));
	b->wins.rangemean = GTK_LABEL
		(gtk_builder_get_object(builder, "label44"));
	b->wins.rangestatus = GTK_LABEL
		(gtk_builder_get_object(builder, "label46"));
	b->wins.rangefunc = GTK_LABEL
		(gtk_builder_get_object(builder, "label50"));
	b->wins.buttonrange = GTK_BUTTON
		(gtk_builder_get_object(builder, "button4"));
	b->wins.namefill[NAMEFILL_DATE] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton3"));
	b->wins.namefill[NAMEFILL_M] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton4"));
	b->wins.namefill[NAMEFILL_T] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton7"));
	b->wins.namefill[NAMEFILL_MUTANTS] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton8"));
	b->wins.namefill[NAMEFILL_NONE] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton9"));
	b->wins.mapbox = GTK_BOX
		(gtk_builder_get_object(builder, "box31"));
	b->wins.config = GTK_WINDOW
		(gtk_builder_get_object(builder, "window1"));
	b->wins.rangefind = GTK_WINDOW
		(gtk_builder_get_object(builder, "window2"));
	b->wins.status = GTK_STATUSBAR
		(gtk_builder_get_object(builder, "statusbar1"));
	b->wins.menu = GTK_MENU_BAR
		(gtk_builder_get_object(builder, "menubar1"));
	b->wins.mutants[MUTANTS_DISCRETE] = GTK_RADIO_BUTTON
		(gtk_builder_get_object(builder, "radiobutton1"));
	b->wins.mutants[MUTANTS_GAUSSIAN] = GTK_RADIO_BUTTON
		(gtk_builder_get_object(builder, "radiobutton2"));
	b->wins.weighted = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "checkbutton1"));
	b->wins.menuquit = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem5"));
	b->wins.input = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry3"));
	b->wins.mutantsigma = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry17"));
	b->wins.name = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry16"));
	b->wins.stop = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry9"));
	b->wins.xmin = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry8"));
	b->wins.xmax = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry10"));
	b->wins.ymin = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry18"));
	b->wins.ymax = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry19"));
	b->wins.inputs = GTK_NOTEBOOK
		(gtk_builder_get_object(builder, "notebook1"));
	b->wins.error = GTK_LABEL
		(gtk_builder_get_object(builder, "label8"));
	b->wins.func = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry2"));
	b->wins.nthreads = GTK_ADJUSTMENT
		(gtk_builder_get_object(builder, "adjustment3"));
	b->wins.fitpoly = GTK_ADJUSTMENT
		(gtk_builder_get_object(builder, "adjustment4"));
	b->wins.pop = GTK_ADJUSTMENT
		(gtk_builder_get_object(builder, "adjustment1"));
	b->wins.totalpop = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry12"));
	b->wins.islands = GTK_ADJUSTMENT
		(gtk_builder_get_object(builder, "adjustment2"));
	b->wins.resprocs = GTK_LABEL
		(gtk_builder_get_object(builder, "label3"));
	b->wins.onprocs = GTK_LABEL
		(gtk_builder_get_object(builder, "label36"));
	b->wins.alpha = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry13"));
	b->wins.delta = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry14"));
	b->wins.migrate[INPUT_UNIFORM] = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry1"));
	b->wins.migrate[INPUT_VARIABLE] = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry20"));
	b->wins.migrate[INPUT_MAPPED] = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry4"));
	b->wins.incumbents = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry15"));
	b->wins.mapfile = GTK_FILE_CHOOSER
		(gtk_builder_get_object(builder, "filechooserbutton1"));
	b->wins.mapmigrants[MAPMIGRANT_UNIFORM] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton5"));
	b->wins.mapmigrants[MAPMIGRANT_DISTANCE] = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton6"));

	gtk_widget_show_all(GTK_WIDGET(b->wins.config));

	/* Hide our error message. */
	gtk_widget_hide(GTK_WIDGET(b->wins.error));

	/* Set the initially-selected notebooks. */
	gtk_entry_set_text(b->wins.input, inputs
		[gtk_notebook_get_current_page(b->wins.inputs)]);

	/* XXX: builder doesn't do this. */
	w = gtk_builder_get_object(builder, "comboboxtext1");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);

	/* Maximum number of processors. */
	gtk_adjustment_set_upper(b->wins.nthreads, b->nprocs);
	w = gtk_builder_get_object(builder, "label12");
	(void)g_snprintf(buf, sizeof(buf), "%zu", b->nprocs);
	gtk_label_set_text(GTK_LABEL(w), buf);

	/* Compute initial total population. */
	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(b->wins.pop) *
		gtk_adjustment_get_value(b->wins.islands));
	gtk_entry_set_text(b->wins.totalpop, buf);

	donamefill(b);

	/* Initialise our colour matrix. */
	for (i = 0; i < SIZE_COLOURS; i++) {
		val = gdk_rgba_parse(&b->wins.colours[i], colours[i]);
		g_assert(val);
	}

	/* Hide the rangefinder when we start up. */
	gtk_widget_set_visible(GTK_WIDGET(b->wins.rangefind), FALSE);

#ifdef MAC_INTEGRATION
	gtk_widget_hide(GTK_WIDGET(b->wins.menu));
	gtk_widget_hide(GTK_WIDGET(b->wins.menuquit));
	gtkosx_application_set_menu_bar
		(gtkosx_application_get(), 
		 GTK_MENU_SHELL(b->wins.menu));
	gtkosx_application_sync_menubar
		(gtkosx_application_get());
#endif
}

/*
 * Free a given simulation, possibly waiting for the simulation threads
 * to exit. 
 * The simulation is presumed to have been set as terminating before
 * running this.
 * Yes, we can wait if the simulation takes a long time between updates.
 */
static void
sim_free(gpointer arg)
{
	struct sim	*p = arg;
	size_t		 i;

	if (NULL == p)
		return;

	g_debug("Freeing simulation %p", p);
	/*
	 * Join all of our running threads.
	 * They were stopped in the sim_stop function, which was called
	 * before this one.
	 */
	for (i = 0; i < p->nprocs; i++) 
		if (NULL != p->threads[i].thread) {
			g_debug("Freeing joining thread %p "
				"(simulation %p)", 
				p->threads[i].thread, p);
			g_thread_join(p->threads[i].thread);
		}
	p->nprocs = 0;

	hnode_free(p->continuum.exp);
	g_mutex_clear(&p->hot.mux);
	g_cond_clear(&p->hot.cond);
	g_free(p->name);
	g_free(p->func);
	g_free(p->hot.stats);
	g_free(p->hot.statslsb);
	g_free(p->hot.islands);
	g_free(p->hot.islandslsb);
	g_free(p->warm.stats);
	g_free(p->warm.islands);
	g_free(p->warm.coeffs);
	g_free(p->warm.smeans);
	g_free(p->warm.sextms);
	g_free(p->warm.fits);
	g_free(p->cold.stats);
	g_free(p->cold.islands);
	if (NULL != p->ms)
		for (i = 0; i < p->islands; i++)
			g_free(p->ms[i]);
	g_free(p->ms);
	g_free(p->pops);
	kml_free(p->kml);
	gsl_histogram_free(p->cold.smeanmins);
	gsl_histogram_free(p->cold.sextmmaxs);
	gsl_histogram_free(p->cold.fitmins);
	gsl_histogram_free(p->cold.meanmins);
	gsl_histogram_free(p->cold.extmmaxs);
	gsl_histogram_free(p->cold.extimins);
	if (p->fitpoly) {
		gsl_matrix_free(p->work.X);
		gsl_vector_free(p->work.y);
		gsl_vector_free(p->work.w);
		gsl_vector_free(p->work.c);
		gsl_matrix_free(p->work.cov);
		gsl_multifit_linear_free(p->work.work);
		p->fitpoly = 0;
	}
	g_free(p->cold.coeffs);
	g_free(p->cold.smeans);
	g_free(p->cold.sextms);
	g_free(p->cold.fits);
	g_free(p->threads);
	g_free(p);
	g_debug("Simulation %p freed", p);
}

/*
 * Set a given simulation to stop running.
 * This must be invoked before sim_free() or simulation threads will
 * still be running.
 */
static void
sim_stop(gpointer arg, gpointer unused)
{
	struct sim	*p = arg;
	int		 pause;

	if (p->terminate)
		return;
	g_debug("Stopping simulation %p", p);
	p->terminate = 1;
	g_mutex_lock(&p->hot.mux);
	if (0 != (pause = p->hot.pause)) {
		p->hot.pause = 0;
		g_cond_broadcast(&p->hot.cond);
	}
	g_mutex_unlock(&p->hot.mux);
	if (pause)
		g_debug("Unpausing simulation %p for stop", p);
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

	g_debug("Freeing main");
	g_list_foreach(p->sims, sim_stop, NULL);
	g_list_free_full(p->sims, sim_free);
	p->sims = NULL;
	hnode_free(p->range.exp);
	p->range.exp = NULL;
	if (NULL != p->status_elapsed)
		g_timer_destroy(p->status_elapsed);
	p->status_elapsed = NULL;
}

/*
 * Pause or unpause (unset pause and broadcast to condition) a given
 * simulation depending on its current pause status.
 */
static void
on_sim_pause(struct sim *sim, int dopause)
{
	int		 pause = -1;

	g_mutex_lock(&sim->hot.mux);
	if (0 == dopause && sim->hot.pause) {
		sim->hot.pause = pause = 0;
		g_cond_broadcast(&sim->hot.cond);
	} else if (dopause && 0 == sim->hot.pause)
		sim->hot.pause = pause = 1;
	g_mutex_unlock(&sim->hot.mux);

	if (0 == pause && 0 == dopause)
		g_debug("Unpausing simulation %p", sim);
	else if (pause && dopause)
		g_debug("Pausing simulation %p", sim);
}

/*
 * Dereference a simulation.
 * This means that we've closed an output window that's looking at a
 * particular simulation.
 * When the simulation has no more references (i.e., no more windows are
 * painting that simulation), then the simulation destroys itself.
 */
static void
on_sim_deref(gpointer dat)
{
	struct sim	*sim = dat;

	g_debug("Simulation %p deref (now %zu)", sim, sim->refs - 1);
	if (0 != --sim->refs) 
		return;
	g_debug("Simulation %p deref triggering termination", sim);
	sim_stop(sim, NULL);
}

/*
 * Indicate that a given simulation is now being referenced by a new
 * window.
 */
static void
sim_ref(gpointer dat, gpointer arg)
{
	struct sim	*sim = dat;

	++sim->refs;
	g_debug("Simulation %p ref (now %zu)", sim, sim->refs);
}

/*
 * Dereference all simulations owned by a given window.
 * This happens when a window is closing.
 */
static void
on_sims_deref(gpointer dat)
{

	g_debug("Window destroying simulation copies.");
	g_list_free_full(dat, on_sim_deref);
}

/*
 * Add an element to a histogram (there are several, e.g., minimum
 * mean-mean, minimum mutant-extinction, etc.) that record strategy.
 * This will update all elements of the applicable hstats.
 */
static void
hist_update(const struct sim *sim, 
	gsl_histogram *p, struct hstats *st, size_t strat)
{

	gsl_histogram_increment(p, GETS(sim, strat));
	st->mode = GETS(sim, gsl_histogram_max_bin(p));
	st->mean = gsl_histogram_mean(p);
	st->stddev = gsl_histogram_sigma(p);
}

/*
 * Push a value onto the circular queue.
 * This will update our current location and the maximum value, too.
 */
static void
cqueue_push(struct cqueue *q, size_t val)
{

	q->vals[q->pos] = val;
	if (val > q->vals[q->maxpos])
		q->maxpos = q->pos;
	q->pos = (q->pos + 1) % CQUEUESZ;
}

/*
 * Brute-force scan all possible pi values (and Poisson means) by
 * scanning through the strategy space.
 */
static gboolean
on_rangefind_idle(gpointer dat)
{
	struct bmigrate	*b = dat;
	size_t		 mutants;
	double		 mstrat, istrat, v;
	gchar		 buf[22];

	g_assert(b->rangeid);

	/*
	 * Set the number of mutants on a given island, then see what
	 * the utility function would yield given that number of mutants
	 * and incumbents, setting the current player to be one or the
	 * other..
	 */
	mstrat = istrat = 0.0;
	for (mutants = 0; mutants <= b->range.n; mutants++) {
		mstrat = b->range.ymin + 
			(b->range.slicey / (double)b->range.slices) * 
			(b->range.ymax - b->range.ymin);
		istrat = b->range.xmin + 
			(b->range.slicex / (double)b->range.slices) * 
			(b->range.xmax - b->range.xmin);
		/*
		 * Only check for a given mutant/incumbent individual's
		 * strategy if the population is going to support the
		 * existence of that individual.
		 */
		if (mutants > 0) {
			v = hnode_exec
				((const struct hnode *const *) 
				 b->range.exp, 
				 istrat, mstrat * mutants + istrat * 
				 (b->range.n - mutants), b->range.n);
			if (0.0 != v && ! isnormal(v))
				break;
			if (v < b->range.pimin)
				b->range.pimin = v;
			if (v > b->range.pimax)
				b->range.pimax = v;
			b->range.piaggr += v;
			b->range.picount++;
		}
		if (mutants != b->range.n) {
			v = hnode_exec
				((const struct hnode *const *) 
				 b->range.exp, 
				 mstrat, mstrat * mutants + istrat * 
				 (b->range.n - mutants), b->range.n);
			if (0.0 != v && ! isnormal(v))
				break;
			if (v < b->range.pimin)
				b->range.pimin = v;
			if (v > b->range.pimax)
				b->range.pimax = v;
			b->range.piaggr += v;
			b->range.picount++;
		}
	}

	/*
	 * We might have hit a discontinuous number.
	 * If we did, then print out an error and don't continue.
	 * If not, update to the next mutant and incumbent.
	 */
	if (mutants <= b->range.n) {
		g_snprintf(buf, sizeof(buf), 
			"%zu mutants, mutant=%g, incumbent=%g",
			mutants, mstrat, istrat);
		gtk_label_set_text(b->wins.rangeerror, buf);
		gtk_widget_show_all(GTK_WIDGET(b->wins.rangeerrorbox));
		g_debug("Range-finder idle event complete (error)");
		b->rangeid = 0;
	} else {
		if (++b->range.slicey == b->range.slices) {
			b->range.slicey = 0;
			b->range.slicex++;
		}
		if (b->range.slicex == b->range.slices) {
			g_debug("Range-finder idle event complete");
			b->rangeid = 0;
		}
	}

	/*
	 * Set our current extrema.
	 */
	g_snprintf(buf, sizeof(buf), "%g", b->range.pimin);
	gtk_label_set_text(b->wins.rangemin, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * b->range.pimin));
	gtk_label_set_text(b->wins.rangeminlambda, buf);

	g_snprintf(buf, sizeof(buf), "%g", b->range.pimax);
	gtk_label_set_text(b->wins.rangemax, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * b->range.pimax));
	gtk_label_set_text(b->wins.rangemaxlambda, buf);

	v = b->range.piaggr / (double)b->range.picount;
	g_snprintf(buf, sizeof(buf), "%g", v);
	gtk_label_set_text(b->wins.rangemean, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * v));
	gtk_label_set_text(b->wins.rangemeanlambda, buf);

	v = (b->range.slicex * b->range.slices + b->range.slicey) /
		(double)(b->range.slices * b->range.slices);
	g_snprintf(buf, sizeof(buf), "%.1f%%", v * 100.0);
	gtk_label_set_text(b->wins.rangestatus, buf);

	return(0 != b->rangeid);
}

/*
 * This copies data from the threads into local ("cold") storage.
 * It does so by checking whether the threads have copied out of "hot"
 * storage (locking their mutex in the process) and, if so, indicates
 * that we're about to read it (and thus to do so again), then after
 * reading, reset that the data is stale.
 */
static gboolean
on_sim_copyout(gpointer dat)
{
	struct bmigrate	*b = dat;
	GList		*list, *wins, *sims;
	struct sim	*sim;
	struct curwin	*cur;
	int		 copy;

	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = list->data;
		if (0 == sim->nprocs)
			continue;
		/*
		 * Instruct the simulation to copy out its data into warm
		 * storage.
		 * If it hasn't already done so, then don't copy into
		 * cold storage, as nothing has changed.
		 */
		g_mutex_lock(&sim->hot.mux);
		copy = 0 == sim->hot.copyout;
		g_mutex_unlock(&sim->hot.mux);

		if ( ! copy)
			continue;

		/* 
		 * Don't copy stale data.
		 * Trigger the simulation to copy-out when it gets a
		 * chance.
		 */
		if (sim->cold.truns == sim->warm.truns) {
			assert(sim->cold.tgens == sim->warm.tgens);
			g_mutex_lock(&sim->hot.mux);
			g_assert(0 == sim->hot.copyout);
			sim->hot.copyout = 1;
			g_mutex_unlock(&sim->hot.mux);
			continue;
		}

		/*
		 * Since we're updating this particular simulation, make
		 * sure that all windows tied to this simulation are
		 * also going to be redrawn when the redrawer is called.
		 */
		wins = gtk_window_list_toplevels();
		for ( ; wins != NULL; wins = wins->next) {
			cur = g_object_get_data
				(G_OBJECT(wins->data), "cfg");
			if (NULL == cur)
				continue;
			sims = cur->sims;
			g_assert(NULL != sims);
			for ( ; NULL != sims; sims = sims->next)
				if (sim == sims->data) {
					cur->redraw = 1;
					break;
				}
		}

		/*
		 * Most strutures we simply copy over.
		 */
		memcpy(sim->cold.stats, 
			sim->warm.stats,
			sizeof(struct stats) * sim->dims);
		memcpy(sim->cold.islands, 
			sim->warm.islands,
			sizeof(struct stats) * sim->islands);
		memcpy(sim->cold.fits, 
			sim->warm.fits,
			sizeof(double) * sim->dims);
		memcpy(sim->cold.coeffs, 
			sim->warm.coeffs,
			sizeof(double) * (sim->fitpoly + 1));
		memcpy(sim->cold.smeans, 
			sim->warm.smeans,
			sizeof(double) * sim->dims);
		memcpy(sim->cold.sextms, 
			sim->warm.sextms,
			sizeof(double) * sim->dims);
		sim->cold.meanmin = sim->warm.meanmin;
		sim->cold.smeanmin = sim->warm.smeanmin;
		sim->cold.sextmmax = sim->warm.sextmmax;
		sim->cold.fitmin = sim->warm.fitmin;
		sim->cold.extmmax = sim->warm.extmmax;
		sim->cold.extimin = sim->warm.extimin;
		sim->cold.truns = sim->warm.truns;
		sim->cold.tgens = sim->warm.tgens;
		/*
		 * Now we compute data that's managed by this function
		 * and thread alone (no need for locking).
		 */
		cqueue_push(&sim->cold.meanminq, sim->cold.meanmin);
		cqueue_push(&sim->cold.fitminq, sim->cold.fitmin);
		cqueue_push(&sim->cold.smeanminq, sim->cold.smeanmin);
		/*
		 * Now update our histogram and statistics.
		 */
		hist_update(sim, sim->cold.fitmins, 
			&sim->cold.fitminst, sim->cold.fitmin);
		hist_update(sim, sim->cold.smeanmins, 
			&sim->cold.smeanminst, sim->cold.smeanmin);
		hist_update(sim, sim->cold.sextmmaxs, 
			&sim->cold.sextmmaxst, sim->cold.sextmmax);
		hist_update(sim, sim->cold.meanmins, 
			&sim->cold.meanminst, sim->cold.meanmin);
		hist_update(sim, sim->cold.extmmaxs, 
			&sim->cold.extmmaxst, sim->cold.extmmax);
		hist_update(sim, sim->cold.extimins, 
			&sim->cold.extiminst, sim->cold.extimin);

		/* Copy-out when convenient. */
		g_mutex_lock(&sim->hot.mux);
		g_assert(0 == sim->hot.copyout);
		sim->hot.copyout = 1;
		g_mutex_unlock(&sim->hot.mux);
	}

	return(TRUE);
}

static gboolean
on_sim_autosave(gpointer dat)
{
	struct curwin	*cur;
	GList		*list;
	GtkWidget	*dialog;
	enum view	 sv, view;
	gchar		*file;
	FILE		*f;

	list = gtk_window_list_toplevels();

	for ( ; list != NULL; list = list->next) {
		cur = g_object_get_data
			(G_OBJECT(list->data), "cfg");
		if (NULL == cur)
			continue;
		for (view = 0; view < VIEW__MAX; view++) {
			file = g_strdup_printf
				("%s" G_DIR_SEPARATOR_S "%s",
				 cur->autosave, views[view]);
			if (NULL != (f = fopen(file, "w+"))) {
				sv = cur->view;
				cur->view = view;
				savewin(f, cur->sims, cur);
				cur->view = sv;
				fclose(f);
				g_free(file);
				continue;
			} 
			dialog = gtk_message_dialog_new
				(GTK_WINDOW(cur->wins.window),
				 GTK_DIALOG_DESTROY_WITH_PARENT, 
				 GTK_MESSAGE_ERROR, 
				 GTK_BUTTONS_CLOSE, 
				 "Error auto-saving %s: %s", 
				 file, strerror(errno));
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			g_free(file);
			g_free(cur->autosave);
			cur->autosave = NULL;
			gtk_widget_hide(GTK_WIDGET
				(cur->wins.menuunautoexport));
			gtk_widget_show(GTK_WIDGET
				(cur->wins.menuautoexport));
			break;
		}
	}
	return(TRUE);
}

/*
 * Run this fairly often to see if we need to join any worker threads.
 * Worker threads are joined when they have zero references are in the
 * terminating state.
 */
static gboolean
on_sim_timer(gpointer dat)
{
	struct bmigrate	*b = dat;
	struct curwin	*cur;
	struct sim	*sim;
	gchar		 buf[1024];
	GList		*list;
	uint64_t	 runs;
	size_t		 i, onprocs, resprocs;
	double		 elapsed;

	onprocs = resprocs = runs = 0;
	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = (struct sim *)list->data;
		runs += sim->cold.tgens;
		/*
		 * If "terminate" is set, then the thread is (or already
		 * did finish) exiting, so wait for it.
		 * If we wait, it should take only a very small while.
		 */
		if (sim->terminate && sim->nprocs > 0) {
			for (i = 0; i < sim->nprocs; i++)  {
				if (NULL == sim->threads[i].thread)
					continue;
				g_debug("Timeout handler joining "
					"thread %p (simulation %p)", 
					sim->threads[i].thread, sim);
				g_thread_join(sim->threads[i].thread);
				sim->threads[i].thread = NULL;
			}
			sim->nprocs = 0;
			assert(0 == sim->refs); 
		} else if ( ! sim->terminate && ! sim->hot.pause) {
			onprocs += sim->nprocs;
			resprocs += sim->nprocs;
		} else if ( ! sim->terminate)
			resprocs += sim->nprocs;
	}

	/* 
	 * Remind us of how many threads we're running. 
	 * FIXME: this shows the number of allocated threads, not
	 * necessarily the number of running threads.
	 */
	(void)g_snprintf(buf, sizeof(buf), "%zu", resprocs);
	gtk_label_set_text(b->wins.resprocs, buf);

	(void)g_snprintf(buf, sizeof(buf), "%zu", onprocs);
	gtk_label_set_text(b->wins.onprocs, buf);
	
	/* 
	 * Tell us how many generations have transpired (if no time has
	 * elapsed, then make sure we don't divide by zero).
	 * Then update the status bar.
	 * This will in turn redraw that window portion.
	 */
	elapsed = g_timer_elapsed(b->status_elapsed, NULL);
	if (0.0 == elapsed)
		elapsed = DBL_MIN;
	(void)g_snprintf(buf, sizeof(buf), 
		"Running %.0f generations/second.", 
		(runs - b->lastmatches) / elapsed);
	gtk_statusbar_pop(b->wins.status, 0);
	gtk_statusbar_push(b->wins.status, 0, buf);
	g_timer_start(b->status_elapsed);
	b->lastmatches = runs;

	/* 
	 * Conditionally update our windows.
	 * We do this by iterating through all simulation windows and
	 * seeing if they have the "update" flag set to true.
	 */
	list = gtk_window_list_toplevels();
	for ( ; list != NULL; list = list->next) {
		cur = g_object_get_data
			(G_OBJECT(list->data), "cfg");
		if (NULL == cur || 0 == cur->redraw)
			continue;
		gtk_widget_queue_draw(GTK_WIDGET(list->data));
	}

	return(TRUE);
}

static int
entry2func(GtkEntry *entry, struct hnode ***exp, GtkLabel *error)
{
	const gchar	*txt;
	GdkRGBA	 	 bad = { 1.0, 0.0, 0.0, 0.5 };

	txt = gtk_entry_get_text(entry);
	*exp = hnode_parse((const gchar **)&txt);
	if (NULL != *exp) {
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: not a function.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entry2size(GtkEntry *entry, size_t *sz, GtkLabel *error, size_t min)
{
	guint64	 v;
	gchar	*ep;
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	v = g_ascii_strtoull(gtk_entry_get_text(entry), &ep, 10);
	if (ERANGE == errno || '\0' != *ep || v >= SIZE_MAX) {
		gtk_label_set_text
			(error, "Error: not a natural number.");
		gtk_widget_show_all(GTK_WIDGET(error));
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, &bad);
		return(0);
	} else if (v < min) {
		gtk_label_set_text
			(error, "Error: number too small.");
		gtk_widget_show_all(GTK_WIDGET(error));
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, &bad);
		return(0);
	}

	*sz = (size_t)v;
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), 
		 GTK_STATE_FLAG_NORMAL, NULL);
	return(1);
}

static int
entryworder(GtkEntry *mine, GtkEntry *maxe, 
	double min, double max, GtkLabel *error)
{
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	if (min < max) {
		gtk_widget_override_background_color
			(GTK_WIDGET(mine), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		gtk_widget_override_background_color
			(GTK_WIDGET(maxe), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: bad weak ordering.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(mine), GTK_STATE_FLAG_NORMAL, &bad);
	gtk_widget_override_background_color
		(GTK_WIDGET(maxe), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entryorder(GtkEntry *mine, GtkEntry *maxe, 
	double min, double max, GtkLabel *error)
{
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	if (min <= max) {
		gtk_widget_override_background_color
			(GTK_WIDGET(mine), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		gtk_widget_override_background_color
			(GTK_WIDGET(maxe), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: bad ordering.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(mine), GTK_STATE_FLAG_NORMAL, &bad);
	gtk_widget_override_background_color
		(GTK_WIDGET(maxe), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entry2double(GtkEntry *entry, gdouble *sz, GtkLabel *error)
{
	gchar	*ep;
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	*sz = g_ascii_strtod(gtk_entry_get_text(entry), &ep);

	if (ERANGE != errno && '\0' == *ep) {
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: not a decimal number.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

/*
 * Validate the contents of a "mapbox", that is, an island
 * configuration.
 * For the moment, this simply checks the island population.
 */
static int
mapbox2pair(GtkLabel *err, GtkWidget *w, size_t *n)
{
	GList		*list, *cur;

	list = gtk_container_get_children(GTK_CONTAINER(w));
	g_assert(NULL != list);
	cur = list->next;
	g_assert(NULL != cur);
#if 0
	if ( ! entry2double(GTK_ENTRY(cur->data), m, err)) {
		g_list_free(list);
		return(0);
	}
	cur = cur->next;
	g_assert(NULL != cur);
	cur = cur->next;
	g_assert(NULL != cur);
#endif
	if ( ! entry2size(GTK_ENTRY(cur->data), n, err, 2))  {
		g_list_free(list);
		return(0);
	}

	g_list_free(list);
	return(1);
}

/*
 * Transfer the other widget's data into our own.
 */
void
on_drag_recv(GtkWidget *widget, GdkDragContext *ctx, 
	gint x, gint y, GtkSelectionData *sel, 
	guint target, guint time, gpointer dat)
{
	GObject		*srcptr, *dstptr;
	struct curwin	*cur;
	GList		*srcsims, *l, *ll;

	/* Get pointers to our and the other's window. */
	assert(NULL != sel);
	dstptr = G_OBJECT(gtk_widget_get_toplevel(widget));
	srcptr = G_OBJECT(*(void **)
		gtk_selection_data_get_data(sel));
	assert(NULL != srcptr);
	gtk_drag_finish(ctx, TRUE, FALSE, time);

	/* Don't copy into ourselves. */
	if (dstptr == srcptr)
		return;

	/* Get the simulation lists. */
	cur = g_object_get_data(srcptr, "cfg");
	srcsims = cur->sims;
	assert(NULL != srcsims);
	cur = g_object_get_data(dstptr, "cfg");

	/* Concatenate the simulation lists. */
	/* XXX: use g_list_concat? */
	for (l = srcsims; NULL != l; l = l->next) {
		g_debug("Copying simulation %p", l->data);
		for (ll = cur->sims; NULL != ll; ll = ll->next)
			if (ll->data == l->data)
				break;

		if (NULL != ll) {
			g_debug("Simulation %p duplicate", l->data);
			continue;
		}

		sim_ref(l->data, NULL);
		cur->sims = g_list_append(cur->sims, l->data);
	}
}

/*
 * Signifity sight-unseen that we can transfer data.
 * We're only using DnD for one particular thing, so there's no need for
 * elaborate security measures.
 */
gboolean
on_drag_drop(GtkWidget *widget, GdkDragContext *ctx, 
	gint x, gint y, guint time, gpointer dat)
{

	gtk_drag_get_data(widget, ctx, 0, time);
	return(TRUE);
}

/*
 * Send our identifier to the destination of the DnD.
 * They'll query our data separately.
 */
void
on_drag_get(GtkWidget *widget, GdkDragContext *ctx, 
	GtkSelectionData *sel, guint targ, guint time, gpointer dat)
{
	void	*ptr;

	ptr = gtk_widget_get_toplevel(widget);
	gtk_selection_data_set(sel, 
		gtk_selection_data_get_target(sel), 
		sizeof(intptr_t) * 8, 
		(const guchar *)&ptr, sizeof(intptr_t));
}

gboolean
onfocussim(GtkWidget *w, GdkEvent *event, gpointer dat)
{
#ifdef MAC_INTEGRATION
	struct curwin	  *c = dat;

	g_debug("cwins.menu = %p", c->wins.menu);
	gtkosx_application_set_menu_bar
		(gtkosx_application_get(), 
		 GTK_MENU_SHELL(c->wins.menu));
	gtkosx_application_sync_menubar
		(gtkosx_application_get());
#endif
	return(TRUE);
}

gboolean
onfocusmain(GtkWidget *w, GdkEvent *event, gpointer dat)
{
#ifdef MAC_INTEGRATION
	struct bmigrate	  *b = dat;
	GtkosxApplication *theApp;

	theApp = gtkosx_application_get();
	g_debug("wins.menu = %p (%s)", b->wins.menu, gtk_widget_get_name(GTK_WIDGET(b->wins.menu)));
	gtkosx_application_set_menu_bar
		(theApp,
		 GTK_MENU_SHELL(b->wins.menu));
	gtkosx_application_sync_menubar
		(theApp);
#endif
	return(TRUE);
}

gboolean
ondraw(GtkWidget *w, cairo_t *cr, gpointer dat)
{

	draw(w, cr, dat);
	return(TRUE);
}

static void
curwin_free(gpointer dat)
{
	struct curwin	*cur = dat;

	on_sims_deref(cur->sims);
	g_free(cur->autosave);
	g_free(cur);
}

/*
 * Initialise a simulation (or simulations) window.
 * This is either called when we've just made a new simulation 
 */
static void
window_init(struct bmigrate *b, struct curwin *cur, GList *sims)
{
#ifdef	MAC_INTEGRATION
	GtkApplication	*theApp;
	gchar		*dir;
#endif
	GError		*err;
	GtkBuilder	*builder;
	GtkTargetEntry   target;
	gchar		*file;

	/* Set us to redraw. */
	cur->redraw = 1;

#ifdef	MAC_INTEGRATION
	theApp = g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
	if (NULL != (dir = gtkosx_application_get_bundle_id())) {
		g_free(dir);
		dir = gtkosx_application_get_resource_path();
		file = g_strdup_printf
			("%s" G_DIR_SEPARATOR_S
			 "share" G_DIR_SEPARATOR_S
			 "bmigrate" G_DIR_SEPARATOR_S
			 "simulation.glade", dir);
		g_free(dir);
	}
#endif
	if (NULL == file)
		file = g_strdup_printf(DATADIR 
			G_DIR_SEPARATOR_S "%s", "simulation.glade");

	builder = gtk_builder_new();
	assert(NULL != builder);

	/*
	 * This should be gtk_builder_new_from_file(), but we don't
	 * support that on older versions of GTK, so do it like this.
	 */
	err = NULL;
	if ( ! gtk_builder_add_from_file(builder, file, &err)) {
		g_error("%s: %s", file, 
			NULL == err ? "(no error)" : err->message);
		if (NULL != err)
			g_error_free(err);
		g_free(file);
		g_object_unref(G_OBJECT(builder));
		return;
	}

	swin_init(cur, builder);
	gtk_builder_connect_signals(builder, cur);
	g_object_unref(G_OBJECT(builder));
	cur->sims = sims;
	cur->b = b;

	g_object_set_data_full(G_OBJECT(cur->wins.window), 
		"cfg", cur, curwin_free);

	/* 
	 * Coordinate drag-and-drop. 
	 * We only allow drag-and-drop between us and other windows of
	 * this same simulation.
	 */
	target.target = g_strdup("integer");
	target.flags = GTK_TARGET_SAME_APP|GTK_TARGET_OTHER_WIDGET;
	target.info = 0;

	gtk_drag_dest_set(GTK_WIDGET(cur->wins.notebook),
		GTK_DEST_DEFAULT_ALL, &target, 1, GDK_ACTION_COPY);
	gtk_drag_source_set(GTK_WIDGET(cur->wins.notebook),
		GDK_BUTTON1_MASK, &target, 1, GDK_ACTION_COPY);
	g_free(target.target);

	/* Reset our auto-save status. */
	g_free(cur->autosave);
	cur->autosave = NULL;
}

void
onunautoexport(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;

	g_assert(NULL != cur->autosave);
	g_debug("Disabling auto-exporting: %s", cur->autosave);
	g_free(cur->autosave);
	cur->autosave = NULL;
	gtk_widget_show(GTK_WIDGET(cur->wins.menuautoexport));
	gtk_widget_hide(GTK_WIDGET(cur->wins.menuunautoexport));
}

void
onautoexport(GtkMenuItem *menuitem, gpointer dat)
{
	GtkWidget	*dialog;
	gint		 res;
	struct curwin	*cur = dat;
	GtkFileChooser	*chooser;

	g_assert(NULL == cur->autosave);
	dialog = gtk_file_chooser_dialog_new
		("Create Data Folder", GTK_WINDOW(cur->wins.window),
		 GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER,
		 "_Cancel", GTK_RESPONSE_CANCEL,
		 "_Create", GTK_RESPONSE_ACCEPT, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_current_name(chooser, "bmigrate");
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	cur->autosave = gtk_file_chooser_get_filename(chooser);
	gtk_widget_destroy(dialog);
	gtk_widget_hide(GTK_WIDGET(cur->wins.menuautoexport));
	gtk_widget_show(GTK_WIDGET(cur->wins.menuunautoexport));
	g_debug("Auto-exporting: %s", cur->autosave);
}

/*
 * Clone the current window.
 * We create a new window, intialised in the same way as for a new
 * single simulation, but give it an existing list of simulations.
 */
void
onclone(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*oldcur = dat, *newcur;
	GList		*oldsims, *newsims;

	oldsims = oldcur->sims;
	g_list_foreach(oldsims, sim_ref, NULL);
	newsims = g_list_copy(oldsims);
	newcur = g_malloc0(sizeof(struct curwin));
	newcur->view = oldcur->view;
	window_init(oldcur->b, newcur, newsims);
}

/*
 * Toggle a different view of the current window.
 */
void
onviewtoggle(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;

	/*
	 * First, set the "view" indicator to be the current view as
	 * found in the drop-down menu.
	 */
	for (cur->view = 0; cur->view < VIEW__MAX; cur->view++) 
		if (gtk_check_menu_item_get_active
			(cur->wins.views[cur->view]))
			break;
	/*
	 * Next, set the window name to be the label associated with the
	 * respective menu check item.
	 */
	g_assert(cur->view < VIEW__MAX);
	gtk_window_set_title(GTK_WINDOW(cur->wins.window),
		gtk_menu_item_get_label
		(GTK_MENU_ITEM(cur->wins.views[cur->view])));

	/* Redraw the window. */
	gtk_widget_queue_draw(GTK_WIDGET(cur->wins.window));
}

/*
 * Pause all simulations connect to a view.
 */
void
onpause(GtkMenuItem *menuitem, gpointer dat)
{
	GList		*l;
	struct curwin	*cur = dat;

	for (l = cur->sims; NULL != l; l = l->next)
		on_sim_pause(l->data, 1);
}

/*
 * Unpause all simulations connect to a view.
 */
void
onunpause(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;
	GList		*l;

	for (l = cur->sims ; NULL != l; l = l->next)
		on_sim_pause(l->data, 0);
}

gboolean
onrangedelete(GtkWidget *widget, GdkEvent *event, gpointer dat)
{
	struct bmigrate	*b = dat;

	gtk_widget_set_visible
		(GTK_WIDGET(b->wins.rangefind), FALSE);
	g_debug("Disabling rangefinder (user request)");
	g_source_remove(b->rangeid);
	b->rangeid = 0;
	return(TRUE);
}

void
onrangeclose(GtkButton *button, gpointer dat)
{
	struct bmigrate	*b = dat;

	gtk_widget_set_visible
		(GTK_WIDGET(b->wins.rangefind), FALSE);
	g_debug("Disabling rangefinder (user request)");
	g_source_remove(b->rangeid);
	b->rangeid = 0;
}

/*
 * We want to run the given simulation.
 * First, verify all data; then, start the simulation; lastly, open a
 * window assigned specifically to that simulation.
 */
void
onactivate(GtkButton *button, gpointer dat)
{
	gint	 	  input;
	struct bmigrate	 *b = dat;
	struct hnode	**exp;
	enum mapmigrant	  migrants;
	GtkWidget	 *w;
	struct kml	 *kml;
	GList		 *list;
	GtkLabel	 *err = b->wins.error;
	const gchar	 *name, *func;
	gchar	  	 *file;
	gdouble		**ms;
	gdouble		  xmin, xmax, delta, alpha, m, sigma,
			  ymin, ymax;
	enum mutants	  mutants;
	size_t		  i, totalpop, islands, stop, 
			  slices, islandpop;
	size_t		 *islandpops;
	struct sim	 *sim;
	struct curwin	 *cur;
	struct kmlplace	 *kmlp;
	GError		 *er;

	islandpops = NULL;
	islandpop = 0;
	ms = NULL;
	kml = NULL;

	if ( ! entry2size(b->wins.stop, &stop, err, 1))
		goto cleanup;

	input = gtk_notebook_get_current_page(b->wins.inputs);
	migrants = MAPMIGRANT_UNIFORM;
	if (gtk_toggle_button_get_active
		(b->wins.mapmigrants[MAPMIGRANT_DISTANCE]))
		migrants = MAPMIGRANT_DISTANCE;

	switch (input) {
	case (INPUT_UNIFORM):
		/*
		 * In the simplest possible case, we use uniform values
		 * for both migration (no inter-island migration) and
		 * populations.
		 */
		islands = (size_t)gtk_adjustment_get_value
			(b->wins.islands);
		islandpop = (size_t)gtk_adjustment_get_value(b->wins.pop);
		break;
	case (INPUT_VARIABLE):
		/*
		 * Variable number of islands.
		 * We also add a check to see if we're really running
		 * with different island sizes or not.
		 */
		list = gtk_container_get_children
			(GTK_CONTAINER(b->wins.mapbox));
		islands = (size_t)g_list_length(list);
		islandpops = g_malloc0_n(islands, sizeof(size_t));
		for (i = 0; i < islands; i++) {
			w = GTK_WIDGET(g_list_nth_data(list, i));
			if ( ! mapbox2pair(err, w, &islandpops[i]))
				goto cleanup;
		}
		g_list_free(list);
		break;
	case (INPUT_MAPPED):
		/*
		 * Possibly variable island sizes, possibly variable
		 * inter-island migration.
		 */
		file = gtk_file_chooser_get_filename
			(b->wins.mapfile);
		if (NULL == file) {
			gtk_label_set_text(err, 
				"Error: map file not specified.");
			gtk_widget_show_all(GTK_WIDGET(err));
			goto cleanup;
		} 
		er = NULL;
		kml = kml_parse(file, &er);
		g_free(file);
		
		if (NULL == kml) {
			/* Re-use pointer. */
			file = g_strdup_printf("Error: "
				"bad map file: %s", 
				NULL != er ? er->message :
				"cannot load file");
			gtk_label_set_text(err, file);
			gtk_widget_show_all(GTK_WIDGET(err));
			g_free(file);
			g_error_free(er);
			goto cleanup;
		}

		/*
		 * Grok our island populations from the input file.
		 * This will have a reasonable default, but make sure
		 * anyway with some assertions.
		 */
		islands = (size_t)g_list_length(kml->kmls);
		islandpops = g_malloc0_n(islands, sizeof(size_t));
		for (i = 0; i < islands; i++) {
			kmlp = g_list_nth_data(kml->kmls, i);
			islandpops[i] = kmlp->pop;
		}

		/*
		 * If we're uniformly migrating, then stop processing
		 * right now.
		 * If we're distance-migrating, then have the kml file
		 * set the inter-island migration probabilities.
		 */
		if (MAPMIGRANT_UNIFORM != migrants)
			ms = kml_migration_distance(kml->kmls);
		break;
	default:
		abort();
	}

	/*
	 * Base check: we need at least two islands.
	 */
	if (islands < 2) {
		gtk_label_set_text(err, 
			"Error: need at least two islands.");
		gtk_widget_show_all(GTK_WIDGET(err));
		goto cleanup;
	}

	/*
	 * If we have an array of island populations, make sure that
	 * each has more than two islanders.
	 * While here, revert to a single island population size if our
	 * sizes are, in fact, uniform.
	 * Calculate our total population size as well.
	 */
	if (NULL != islandpops) {
		g_assert(0 == islandpop);
		for (i = 0; i < islands; i++) {
			if (islandpops[i] > 1)
				continue;
			gtk_label_set_text(err, "Error: need at "
				"least two islanders per island.");
			gtk_widget_show_all(GTK_WIDGET(err));
			goto cleanup;
		}
		for (totalpop = i = 0; i < islands; i++)
			totalpop += islandpops[i];

		/* Check for uniformity. */
		for (i = 1; i < islands; i++)
			if (islandpops[i] != islandpops[0])
				break;
		if (i == islands) {
			g_debug("Reverting to uniform island "
				"populations: all islands have "
				"the same: %zu", islandpops[0]);
			islandpop = islandpops[0];
			g_free(islandpops);
			islandpops = NULL;
		}
	} 
	
	/*
	 * Handle uniform island sizes.
	 * (This isn't part of the conditional above because we might
	 * set ourselves to be uniform whilst processing.)
	 */
	if (NULL == islandpops && islandpop < 2) {
		gtk_label_set_text(err, "Error: need at "
			"least two islanders per island.");
		gtk_widget_show_all(GTK_WIDGET(err));
		goto cleanup;
	} else if (NULL == islandpops)
		totalpop = islands * islandpop;

	for (mutants = 0; mutants < MUTANTS__MAX; mutants++)
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON(b->wins.mutants[mutants])))
			break;

	if ( ! entry2double(b->wins.xmin, &xmin, err))
		goto cleanup;
	if ( ! entry2double(b->wins.xmax, &xmax, err))
		goto cleanup;
	if ( ! entryworder(b->wins.xmin, b->wins.xmax, xmin, xmax, err))
		goto cleanup;
	if (MUTANTS_GAUSSIAN == mutants) {
		if ( ! entry2double(b->wins.ymin, &ymin, err))
			goto cleanup;
		if ( ! entry2double(b->wins.ymax, &ymax, err))
			goto cleanup;
		if ( ! entryorder(b->wins.ymin, 
				b->wins.xmin, ymin, xmin, err))
			goto cleanup;
		if ( ! entryorder(b->wins.xmax, 
				b->wins.ymax, xmax, ymax, err))
			goto cleanup;
		if ( ! entryworder(b->wins.ymin, 
				b->wins.ymax, ymin, ymax, err))
			goto cleanup;
		if ( ! entry2double(b->wins.mutantsigma, &sigma, err))
			goto cleanup;
	}
	if ( ! entry2double(b->wins.alpha, &alpha, err))
		goto cleanup;
	if ( ! entry2double(b->wins.delta, &delta, err))
		goto cleanup;
	if ( ! entry2double(b->wins.migrate[input], &m, err))
		goto cleanup;
	if ( ! entry2size(b->wins.incumbents, &slices, err, 1))
		goto cleanup;
	if ( ! entry2func(b->wins.func, &exp, err))
		goto cleanup;

	func = gtk_entry_get_text(b->wins.func);
	name = gtk_entry_get_text(b->wins.name);

	if ('\0' == *name)
		name = "unnamed";

	/* 
	 * All parameters check out!
	 * Allocate the simulation now.
	 */
	if (button == b->wins.buttonrange) {
		if (0 == b->rangeid) {
			g_debug("Starting rangefinder");
			b->rangeid = g_idle_add
				((GSourceFunc)on_rangefind_idle, b);
		} else 
			g_debug("Re-using rangefinder");

		hnode_free(b->range.exp);
		if (NULL != islandpops) {
			b->range.n = 0;
			for (i = 0; i < islands; i++)
				b->range.n = islandpops[i] > b->range.n ? 
					islandpops[i] : b->range.n;
		} else
			b->range.n = islandpop;

		b->range.exp = exp;
		b->range.alpha = alpha;
		b->range.delta = delta;
		b->range.slices = slices;
		b->range.slicex = b->range.slicey = 0;
		b->range.piaggr = 0.0;
		b->range.picount = 0;
		b->range.pimin = DBL_MAX;
		b->range.pimax = -DBL_MAX;
		b->range.xmin = b->range.ymin = xmin;
		b->range.xmax = b->range.ymax = xmax;
		if (MUTANTS_GAUSSIAN == mutants) {
			b->range.ymin = ymin;
			b->range.ymax = ymax;
		}
		exp = NULL;
		file = g_strdup_printf
			("%s, X=[%g, %g), Y=[%g, %g), n=%zu",
			func, b->range.xmin, b->range.xmax, 
			b->range.ymin, b->range.ymax,
			b->range.n);
		gtk_label_set_text(b->wins.rangefunc, file);
		gtk_widget_hide(GTK_WIDGET(b->wins.rangeerrorbox));
		g_free(file);
		gtk_widget_set_visible
			(GTK_WIDGET(b->wins.rangefind), TRUE);
		goto cleanup;
	}

	gtk_widget_hide(GTK_WIDGET(err));

	sim = g_malloc0(sizeof(struct sim));
	sim->dims = slices;
	sim->islands = islands;
	sim->mutants = mutants;
	sim->mutantsigma = sigma;
	sim->func = g_strdup(func);
	sim->name = g_strdup(name);
	sim->fitpoly = gtk_adjustment_get_value(b->wins.fitpoly);
	sim->weighted = gtk_toggle_button_get_active(b->wins.weighted);
	sim->kml = kml;
	g_mutex_init(&sim->hot.mux);
	g_cond_init(&sim->hot.cond);
	sim->hot.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->hot.statslsb = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->hot.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->hot.islandslsb = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->warm.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->warm.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->cold.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->warm.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->warm.smeans = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.sextms = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->cold.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.smeans = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.sextms = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.fitmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.fitmins);
	sim->cold.smeanmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.smeanmins);
	sim->cold.sextmmaxs = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.sextmmaxs);
	sim->cold.meanmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.meanmins);
	sim->cold.extmmaxs = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.extmmaxs);
	sim->cold.extimins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.extimins);
	gsl_histogram_set_ranges_uniform
		(sim->cold.fitmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.smeanmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.sextmmaxs, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.meanmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.extmmaxs, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.extimins, xmin, xmax);

	/*
	 * Conditionally allocate our fitness polynomial structures.
	 * These are per-simulation as they're only run by one thread at
	 * any one time during the simulation.
	 */
	if (sim->fitpoly) {
		sim->work.X = gsl_matrix_alloc
			(sim->dims, sim->fitpoly + 1);
		sim->work.y = gsl_vector_alloc(sim->dims);
		sim->work.w = gsl_vector_alloc(sim->dims);
		sim->work.c = gsl_vector_alloc(sim->fitpoly + 1);
		sim->work.cov = gsl_matrix_alloc
			(sim->fitpoly + 1, sim->fitpoly + 1);
		sim->work.work = gsl_multifit_linear_alloc
			(sim->dims, sim->fitpoly + 1);
	}

	sim->nprocs = gtk_adjustment_get_value(b->wins.nthreads);
	sim->totalpop = totalpop;
	sim->stop = stop;
	sim->alpha = alpha;
	sim->colour = b->nextcolour;
	b->nextcolour = (b->nextcolour + 1) % SIZE_COLOURS;
	sim->delta = delta;
	sim->m = m;
	sim->ms = ms;
	sim->pop = islandpop;
	sim->pops = islandpops;
	sim->input = input;
	sim->continuum.exp = exp;
	sim->continuum.xmin = xmin;
	sim->continuum.xmax = xmax;
	sim->continuum.ymin = ymin;
	sim->continuum.ymax = ymax;
	b->sims = g_list_append(b->sims, sim);
	sim_ref(sim, NULL);
	sim->threads = g_malloc0_n(sim->nprocs, sizeof(struct simthr));
	g_debug("New simulation: %zu islands, %zu total members "
		"(%s per island) (%zu generations)", 
		sim->islands, sim->totalpop, 
		NULL != sim->pops ? "variable" : "uniform", 
		sim->stop);
	g_debug("New %s migration, %g probability, %g(1 + %g pi)", 
		NULL != sim->ms ? "variable" : "uniform", 
		sim->m, sim->alpha, sim->delta);
	g_debug("New function %s, x = [%g, %g)", sim->func,
		sim->continuum.xmin, sim->continuum.xmax);
	g_debug("New threads: %zu", sim->nprocs);
	g_debug("New polynomial: %zu (%s)", 
		sim->fitpoly, sim->weighted ? 
		"weighted" : "unweighted");
	if (MUTANTS_GAUSSIAN == sim->mutants)
		g_debug("New Gaussian mutants: "
			"%g in [%g, %g]", sim->mutantsigma,
			sim->continuum.ymin, sim->continuum.ymax);
	else
		g_debug("New discrete mutants");

	/* Create the simulation threads. */
	for (i = 0; i < sim->nprocs; i++) {
		sim->threads[i].rank = i;
		sim->threads[i].sim = sim;
		sim->threads[i].thread = g_thread_new
			(NULL, simulation, &sim->threads[i]);
	}

	/* Create the simulation window. */
	cur = g_malloc0(sizeof(struct curwin));
	cur->view = VIEW_MEAN;
	window_init(b, cur, g_list_append(NULL, sim));

	/* 
	 * Initialise the name of our simulation. 
	 */
	donamefill(b);
	return;
cleanup:
	if (NULL != ms)
		for (i = 0; i < islands; i++)
			g_free(ms[i]);
	g_free(islandpops);
	g_free(ms);
	hnode_free(exp);
	kml_free(kml);
}

/*
 * One of the preset continuum functions for our continuum game
 * possibility.
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
		gtk_entry_set_text(b->wins.func, "(1 - X) * x");
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
on_change_input(GtkNotebook *notebook, 
	GtkWidget *page, gint pnum, gpointer dat)
{
	struct bmigrate	*b = dat;

	assert(pnum < INPUT__MAX);
	gtk_entry_set_text(b->wins.input, inputs[pnum]);
	return(TRUE);
}

/*
 * We've changed either the island population or the number of islands,
 * so set our unmodifiable "total population" field.
 */
void
on_change_totalpop(GtkSpinButton *spinbutton, gpointer dat)
{
	struct bmigrate	*b = dat;
	gchar	 	 buf[1024];

	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(b->wins.pop) *
		gtk_adjustment_get_value(b->wins.islands));
	gtk_entry_set_text(b->wins.totalpop, buf);
}

void
onsavekml(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;
	GtkWidget	*dialog;
	gint		 res;
	GtkFileChooser	*chooser;
	FILE		*f;
	struct sim	*sim;
	char 		*file, *dir;
	GList		*sims;

	dialog = gtk_file_chooser_dialog_new
		("Create KML Data Folder", 
		 GTK_WINDOW(cur->wins.window),
		 GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER,
		 "_Cancel", GTK_RESPONSE_CANCEL,
		 "_Create", GTK_RESPONSE_ACCEPT, NULL);

	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_current_name(chooser, "bmigrate");
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	dir = gtk_file_chooser_get_filename(chooser);
	gtk_widget_destroy(dialog);
	g_assert(NULL != dir);
	g_assert('\0' != *dir);

	sims = cur->sims;
	for ( ; NULL != sims; sims = g_list_next(sims)) {
		sim = sims->data;
		file = g_strdup_printf
			("%s" G_DIR_SEPARATOR_S "%s.kml",
			 dir, sim->name);
		if (NULL != (f = fopen(file, "w+"))) {
			kml_save(f, sim);
			g_debug("Saved KML: %s", file);
			fclose(f);
		} else {
			dialog = gtk_message_dialog_new
				(GTK_WINDOW(cur->wins.window),
				 GTK_DIALOG_DESTROY_WITH_PARENT, 
				 GTK_MESSAGE_ERROR, 
				 GTK_BUTTONS_CLOSE, 
				 "Error saving %s: %s", 
				 file, strerror(errno));
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			g_free(file);
			break;
		}
		g_free(file);
	}
	g_free(dir);
}

void
onsaveall(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;
	GtkWidget	*dialog;
	gint		 res;
	GtkFileChooser	*chooser;
	char 		*dir, *file;
	enum view	 view, sv;
	FILE		*f;

	dialog = gtk_file_chooser_dialog_new
		("Create View Data Folder", 
		 GTK_WINDOW(cur->wins.window),
		 GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER,
		 "_Cancel", GTK_RESPONSE_CANCEL,
		 "_Create", GTK_RESPONSE_ACCEPT, NULL);

	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_current_name(chooser, "bmigrate");
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	dir = gtk_file_chooser_get_filename(chooser);
	gtk_widget_destroy(dialog);
	g_assert(NULL != dir);
	g_assert('\0' != *dir);

	sv = cur->view;
	for (view = 0; view < VIEW__MAX; view++) {
		file = g_strdup_printf
			("%s" G_DIR_SEPARATOR_S "%s",
			 dir, views[view]);
		cur->view = view;
		if (NULL != (f = fopen(file, "w+"))) {
			save(f, cur);
			g_debug("Saved View: %s", file);
			fclose(f);
		} else {
			dialog = gtk_message_dialog_new
				(GTK_WINDOW(cur->wins.window),
				 GTK_DIALOG_DESTROY_WITH_PARENT, 
				 GTK_MESSAGE_ERROR, 
				 GTK_BUTTONS_CLOSE, 
				 "Error saving %s: %s", 
				 file, strerror(errno));
			gtk_dialog_run(GTK_DIALOG(dialog));
			gtk_widget_destroy(dialog);
			g_free(file);
			break;
		}
		g_free(file);
	}

	cur->view = sv;
	g_free(dir);
}

/*
 * Run when we quit from a simulation window.
 */
void
onsave(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;
	GtkWidget	*dialog;
	gint		 res;
	GtkFileChooser	*chooser;
	FILE		*f;
	char 		*file;

	dialog = gtk_file_chooser_dialog_new
		("Save View Data", 
		 GTK_WINDOW(cur->wins.window),
		 GTK_FILE_CHOOSER_ACTION_SAVE,
		 "_Cancel", GTK_RESPONSE_CANCEL,
		 "_Save", GTK_RESPONSE_ACCEPT, NULL);
	chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
	gtk_file_chooser_set_current_name(chooser, "bmigrate.dat");
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}
	file = gtk_file_chooser_get_filename(chooser);
	gtk_widget_destroy(dialog);
	g_assert(NULL != file);
	g_assert('\0' != *file);

	if (NULL != (f = fopen(file, "w+"))) {
		save(f, cur);
		g_debug("Saved View: %s", file);
		fclose(f);
	} else {
		dialog = gtk_message_dialog_new
			(GTK_WINDOW(cur->wins.window),
			 GTK_DIALOG_DESTROY_WITH_PARENT, 
			 GTK_MESSAGE_ERROR, 
			 GTK_BUTTONS_CLOSE, 
			 "Error saving %s: %s", 
			 file, strerror(errno));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	g_free(file);
}

/*
 * Like onquit() but from the Mac quit menu.
 */
#ifdef MAC_INTEGRATION
gboolean
onterminate(GtkosxApplication *action, gpointer dat)
{

	bmigrate_free(dat);
	gtk_main_quit();
	return(FALSE);
}
#endif

/*
 * Run when we quit from a simulation window.
 */
void
onclose(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;

	g_debug("Simulation window closing");
	gtk_widget_destroy(GTK_WIDGET(cur->wins.window));
}

/*
 * Run when we quit from a simulation window.
 */
void
onquitsim(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*cur = dat;

	bmigrate_free(cur->b);
	gtk_main_quit();
}

/*
 * Run when we quit from a simulation window.
 */
void
onquitmain(GtkMenuItem *menuitem, gpointer dat)
{

	bmigrate_free(dat);
	gtk_main_quit();
}

/*
 * Run when we destroy the config screen.
 */
void 
ondestroy(GtkWidget *object, gpointer dat)
{
	
	bmigrate_free(dat);
	gtk_main_quit();
}

/*
 * Run when we press "Quit" on the config screen.
 */
void
on_deactivate(GtkButton *button, gpointer dat)
{

	bmigrate_free(dat);
	gtk_main_quit();
}

/*
 * Add an island configuration.
 * For the time being, we only allow the population size of the island
 * to be specified.
 * (Inter-island migration probabilities may not yet be assigned.)
 */
static void
mapbox_add(struct bmigrate *b, size_t sz)
{
	GtkWidget	*box, *entry, *label;
	gchar		 buf[64];

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	g_snprintf(buf, sizeof(buf), "Population %zu:", sz);
	label = gtk_label_new(buf);
	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	gtk_label_set_width_chars(GTK_LABEL(label), 18);
	gtk_container_add(GTK_CONTAINER(box), label);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "10");
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 7);
	gtk_container_add(GTK_CONTAINER(box), entry);

	gtk_container_add(GTK_CONTAINER(b->wins.mapbox), box);
	gtk_widget_show_all(box);
}

/*
 * Remove the last island configuration.
 */
static void
mapbox_rem(struct bmigrate *b)
{
	GList		*list, *last;

	list = gtk_container_get_children(GTK_CONTAINER(b->wins.mapbox));
	last = g_list_last(list);
	gtk_widget_destroy(GTK_WIDGET(last->data));
	g_list_free(list);
}

/*
 * We've requested more or fewer islands for the mapped scenario.
 */
void
onislandspin(GtkSpinButton *spinbutton, gpointer dat)
{
	struct bmigrate	*b = dat;
	guint		 oldsz, newsz;
	GList		*list;

	list = gtk_container_get_children(GTK_CONTAINER(b->wins.mapbox));
	oldsz = g_list_length(list);
	g_list_free(list);
	newsz = (guint)gtk_spin_button_get_value(spinbutton);

	if (newsz > oldsz) {
		while (oldsz++ < newsz)
			mapbox_add(b, oldsz);
	} else if (oldsz > newsz) {
		while (oldsz-- > newsz)
			mapbox_rem(b);
	}

}

void
onnametoggle(GtkToggleButton *editable, gpointer dat)
{

	donamefill(dat);
}

void
onnameupdate(GtkEditable *editable, gpointer dat)
{

	donamefill(dat);
}

int 
main(int argc, char *argv[])
{
	GtkBuilder	  *builder;
	struct bmigrate	   b;
	gchar	 	  *file;
#ifdef	MAC_INTEGRATION
	gchar	 	  *dir;
#endif
	file = NULL;
	memset(&b, 0, sizeof(struct bmigrate));
	gtk_init(&argc, &argv);

	/*
	 * Right now, the only system I have with this older version of
	 * glibc is Debian (stable?).
	 * So use the sysconf() in assuming we're handling that.
	 */
#if GLIB_CHECK_VERSION(2, 36, 0)
	b.nprocs = g_get_num_processors();
#else
	b.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
#endif
	/*
	 * Sanity-check to make sure that the hnode expression evaluator
	 * is working properly.
	 * You'll need to actually look at the debugging output to see
	 * if that's the case, of course.
	 */
	hnode_test();

	/*
	 * Look up our `glade' file as follows.
	 * If we're in MAC_INTEGRATION, then intuit whether we're in a
	 * bundle and, if so, look up within the bundle.
	 * If we're not in a bundle, look in DATADIR.
	 */
#ifdef	MAC_INTEGRATION
	g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
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

	/*
	 * This should be gtk_builder_new_from_file(), but we don't
	 * support that on older versions of GTK, so do it like this.
	 */
	if ( ! gtk_builder_add_from_file(builder, file, NULL))
		return(EXIT_FAILURE);

	hwin_init(&b, builder);
	gtk_builder_connect_signals(builder, &b);
	g_object_unref(G_OBJECT(builder));

	/*
	 * Have two running timers: once per second, force a refresh of
	 * the window system.
	 * Four times per second, update our cold statistics.
	 */
	b.status_elapsed = g_timer_new();
	g_timeout_add_seconds(1, (GSourceFunc)on_sim_timer, &b);
	g_timeout_add_seconds(60, (GSourceFunc)on_sim_autosave, &b);
	g_timeout_add(250, (GSourceFunc)on_sim_copyout, &b);
	gtk_statusbar_push(b.wins.status, 0, "No simulations.");

#ifdef MAC_INTEGRATION
	g_signal_connect(gtkosx_application_get(), 
		"NSApplicationWillTerminate",
		G_CALLBACK(onterminate), &b);
	gtkosx_application_ready(gtkosx_application_get());
#endif
	gtk_main();
	bmigrate_free(&b);
	return(EXIT_SUCCESS);
}
