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
	"raw-mean-smooth", /* VIEW_SMEAN */
	"status", /* VIEW_STATUS */
};

static void
hwin_init(struct hwin *c, GtkBuilder *b)
{
	GObject		*w;
	gchar		 buf[1024];
	gchar		*bufp;
	gboolean	 val;
	GTimeVal	 gt;
	size_t		 i, nprocs;

	c->mapfromfile = win_init_toggle(b, "radiobutton10");
	c->mapfromrand = win_init_toggle(b, "radiobutton11");
	c->mapfromtorus = win_init_toggle(b, "radiobutton13");
	c->rangeminlambda = win_init_label(b, "label55");
	c->rangemaxlambda = win_init_label(b, "label52");
	c->rangemeanlambda = win_init_label(b, "label58");
	c->rangeerrorbox = win_init_box(b, "box39");
	c->rangeerror = win_init_label(b, "label48");
	c->rangemin = win_init_label(b, "label42");
	c->rangemax = win_init_label(b, "label40");
	c->rangemean = win_init_label(b, "label44");
	c->rangestatus = win_init_label(b, "label46");
	c->rangefunc = win_init_label(b, "label50");
	c->buttonrange = win_init_button(b, "button4");
	c->namefill[NAMEFILL_DATE] = win_init_toggle(b, "radiobutton3");
	c->namefill[NAMEFILL_M] = win_init_toggle(b, "radiobutton4");
	c->namefill[NAMEFILL_T] = win_init_toggle(b, "radiobutton7");
	c->namefill[NAMEFILL_MUTANTS] = win_init_toggle(b, "radiobutton8");
	c->namefill[NAMEFILL_NONE] = win_init_toggle(b, "radiobutton9");
	c->mapbox = win_init_box(b, "box31");
	c->config = win_init_window(b, "window1");
	c->rangefind = win_init_window(b, "window2");
	c->status = win_init_status(b, "statusbar1");
	c->menu = win_init_menubar(b, "menubar1");
	c->mutants[MUTANTS_DISCRETE] = win_init_radio(b, "radiobutton1");
	c->mutants[MUTANTS_GAUSSIAN] = win_init_radio(b, "radiobutton2");
	c->weighted = win_init_toggle(b, "checkbutton1");
	c->menuquit = win_init_menuitem(b, "menuitem5");
	c->input = win_init_entry(b, "entry3");
	c->mutantsigma = win_init_entry(b, "entry17");
	c->name = win_init_entry(b, "entry16");
	c->stop = win_init_entry(b, "entry9");
	c->xmin = win_init_entry(b, "entry8");
	c->xmax = win_init_entry(b, "entry10");
	c->ymin = win_init_entry(b, "entry18");
	c->ymax = win_init_entry(b, "entry19");
	c->inputs = win_init_notebook(b, "notebook1");
	c->error = win_init_label(b, "label8");
	c->func = win_init_entry(b, "entry2");
	c->nthreads = win_init_adjustment(b, "adjustment3");
	c->fitpoly = win_init_adjustment(b, "adjustment4");
	c->pop = win_init_adjustment(b, "adjustment1");
	c->totalpop = win_init_entry(b, "entry12");
	c->islands = win_init_adjustment(b, "adjustment2");
	c->resprocs = win_init_label(b, "label3");
	c->onprocs = win_init_label(b, "label36");
	c->alpha = win_init_entry(b, "entry13");
	c->delta = win_init_entry(b, "entry14");
	c->migrate[INPUT_UNIFORM] = win_init_entry(b, "entry1");
	c->migrate[INPUT_VARIABLE] = win_init_entry(b, "entry20");
	c->migrate[INPUT_MAPPED] = win_init_entry(b, "entry4");
	c->incumbents = win_init_entry(b, "entry15");
	c->mapfile = win_init_filechoose(b, "filechooserbutton1");
	c->mapmigrants[MAPMIGRANT_UNIFORM] = win_init_toggle(b, "radiobutton5");
	c->mapmigrants[MAPMIGRANT_DISTANCE] = win_init_toggle(b, "radiobutton6");
	c->mapmigrants[MAPMIGRANT_NEAREST] = win_init_toggle(b, "radiobutton12");
	c->maprandislands = win_init_adjustment(b, "adjustment6");
	c->maprandislanders = win_init_adjustment(b, "adjustment7");
	c->maptorusislands = win_init_adjustment(b, "adjustment8");
	c->maptorusislanders = win_init_adjustment(b, "adjustment9");

	gtk_widget_show_all(GTK_WIDGET(c->config));

	/* Hide our error message. */
	gtk_widget_hide(GTK_WIDGET(c->error));

	/* Set the initially-selected notebooks. */
	gtk_entry_set_text(c->input, inputs
		[gtk_notebook_get_current_page(c->inputs)]);

	/* XXX: builder doesn't do this. */
	w = gtk_builder_get_object(b, "comboboxtext1");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);

#if GLIB_CHECK_VERSION(2, 36, 0)
	nprocs = g_get_num_processors();
#else
	nprocs = sysconf(_SC_NPROCESSORS_ONLN);
#endif

	/* Maximum number of processors. */
	gtk_adjustment_set_upper(c->nthreads, nprocs);
	w = gtk_builder_get_object(b, "label12");
	(void)g_snprintf(buf, sizeof(buf), "%zu", nprocs);
	gtk_label_set_text(GTK_LABEL(w), buf);

	/* Compute initial total population. */
	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(c->pop) *
		gtk_adjustment_get_value(c->islands));
	gtk_entry_set_text(c->totalpop, buf);

	g_get_current_time(&gt);
	bufp = g_time_val_to_iso8601(&gt);
	gtk_entry_set_text(c->name, bufp);
	g_free(bufp);

	/* Initialise our colour matrix. */
	for (i = 0; i < SIZE_COLOURS; i++) {
		val = gdk_rgba_parse(&c->colours[i], colours[i]);
		g_assert(val);
	}

	/* Hide the rangefinder when we start up. */
	gtk_widget_set_visible(GTK_WIDGET(c->rangefind), FALSE);

#ifdef MAC_INTEGRATION
	gtk_widget_hide(GTK_WIDGET(c->menu));
	gtk_widget_hide(GTK_WIDGET(c->menuquit));
	gtkosx_application_set_menu_bar
		(gtkosx_application_get(), 
		 GTK_MENU_SHELL(c->menu));
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

	g_debug("%p: Freeing simulation", p);

	/*
	 * Join all of our running threads.
	 * They were stopped in the sim_stop function, which was called
	 * before this one.
	 */
	for (i = 0; i < p->nprocs; i++) 
		if (NULL != p->threads[i].thread) {
			g_debug("%p: Freeing joining thread "
				"(simulation %p)", 
				p->threads[i].thread, p);
			g_thread_join(p->threads[i].thread);
		}
	p->nprocs = 0;

	simbuf_free(p->bufs.means);
	simbuf_free(p->bufs.stddevs);
	simbuf_free(p->bufs.mextinct);
	simbuf_free(p->bufs.iextinct);
	kdata_destroy(p->bufs.fractions);
	kdata_destroy(p->bufs.mutants);
	kdata_destroy(p->bufs.incumbents);
	kdata_destroy(p->bufs.meanmins);
	kdata_destroy(p->bufs.mextinctmaxs);
	kdata_destroy(p->bufs.fitpoly);
	kdata_destroy(p->bufs.fitpolybuf);
	kdata_destroy(p->bufs.iextinctmins);
	kdata_destroy(p->bufs.fitpolymins);

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
	gsl_histogram_free(p->cold.fitmins);
	gsl_histogram_free(p->cold.meanmins);
	gsl_histogram_free(p->cold.extmmaxs);
	gsl_histogram_free(p->cold.extimins);
	if (p->fitpoly) {
		g_free(p->work.coeffs);
		gsl_matrix_free(p->work.X);
		gsl_vector_free(p->work.y);
		gsl_vector_free(p->work.w);
		gsl_vector_free(p->work.c);
		gsl_matrix_free(p->work.cov);
		gsl_multifit_linear_free(p->work.work);
		p->fitpoly = 0;
	}
	g_free(p->cold.sextms);
	g_free(p->cold.fits);
	g_free(p->threads);
	g_free(p);
	g_debug("%p: Simulation freed", p);
}

/*
 * Set a given simulation to stop running.
 * This must be invoked before sim_free() or simulation threads will
 * still be running.
 */
void
sim_stop(gpointer arg, gpointer unused)
{
	struct sim	*p = arg;
	int		 pause;

	if (p->terminate)
		return;
	g_debug("%p: Simulation stopping", p);
	p->terminate = 1;
	g_mutex_lock(&p->hot.mux);
	if (0 != (pause = p->hot.pause)) {
		p->hot.pause = 0;
		g_cond_broadcast(&p->hot.cond);
	}
	g_mutex_unlock(&p->hot.mux);
	if (pause)
		g_debug("%p: Simulation unpausing to stop", p);
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
	GList		*list, *w, *sims;
	struct sim	*sim;
	struct curwin	*cur;
	int		 copy, rc;

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
		for (w = b->windows ; w != NULL; w = w->next) {
			cur = w->data;
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
		simbuf_copy_cold(sim->bufs.means);
		simbuf_copy_cold(sim->bufs.stddevs);
		simbuf_copy_cold(sim->bufs.mextinct);
		simbuf_copy_cold(sim->bufs.iextinct);
		rc = kdata_buffer_copy
			(sim->bufs.fitpolybuf, 
			 sim->bufs.fitpoly);
		g_assert(0 != rc);

		memcpy(sim->cold.stats, 
			sim->warm.stats,
			sizeof(struct stats) * sim->dims);
		memcpy(sim->cold.islands, 
			sim->warm.islands,
			sizeof(struct stats) * sim->islands);
		memcpy(sim->cold.fits, 
			sim->warm.fits,
			sizeof(double) * sim->dims);
		memcpy(sim->cold.sextms, 
			sim->warm.sextms,
			sizeof(double) * sim->dims);
		sim->cold.meanmin = sim->warm.meanmin;
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
		/*
		 * Now update our histogram and statistics.
		 */
		hist_update(sim, sim->cold.fitmins, 
			&sim->cold.fitminst, sim->cold.fitmin);
		hist_update(sim, sim->cold.meanmins, 
			&sim->cold.meanminst, sim->cold.meanmin);

		hist_update(sim, sim->cold.extmmaxs, 
			&sim->cold.extmmaxst, sim->cold.extmmax);
		hist_update(sim, sim->cold.extimins, 
			&sim->cold.extiminst, sim->cold.extimin);

		kdata_bucket_add(sim->bufs.meanmins, 
			sim->cold.meanmin, 1.0);
		kdata_bucket_add(sim->bufs.mextinctmaxs, 
			sim->cold.extmmax, 1.0);
		kdata_bucket_add(sim->bufs.iextinctmins, 
			sim->cold.extimin, 1.0);
		kdata_bucket_add(sim->bufs.fitpolymins, 
			sim->cold.fitmin, 1.0);


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
	struct bmigrate	*b = dat;
	GList		*list;
	GtkWidget	*dialog;
	enum view	 sv, view;
	gchar		*file;
	FILE		*f;

	for (list = b->windows; list != NULL; list = list->next) {
		cur = list->data;
		if (NULL == cur->autosave)
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
	for (list = b->windows; list != NULL; list = list->next) {
		cur = list->data;
		if (0 == cur->redraw)
			continue;
		gtk_widget_queue_draw(GTK_WIDGET(cur->wins.window));
	}

	return(TRUE);
}


gboolean
onfocussim(GtkWidget *w, GdkEvent *event, gpointer dat)
{
#ifdef MAC_INTEGRATION
	struct curwin	  *c = dat;

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

	gtkosx_application_set_menu_bar
		(gtkosx_application_get(),
		 GTK_MENU_SHELL(b->wins.menu));
	gtkosx_application_sync_menubar
		(gtkosx_application_get());
#endif
	return(TRUE);
}

gboolean
ondraw(GtkWidget *w, cairo_t *cr, gpointer dat)
{

	draw(w, cr, dat);
	return(TRUE);
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
		("Create Data Folder", cur->wins.window,
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
		("Create KML Data Folder", cur->wins.window,
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
		("Create View Data Folder", cur->wins.window,
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
		("Save View Data", cur->wins.window,
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

int 
main(int argc, char *argv[])
{
	GtkBuilder	*builder;
	struct bmigrate	 b;

	memset(&b, 0, sizeof(struct bmigrate));
	gtk_init(&argc, &argv);

	/*
	 * Sanity-check to make sure that the hnode expression evaluator
	 * is working properly.
	 * You'll need to actually look at the debugging output to see
	 * if that's the case, of course.
	 */
	hnode_test();

	builder = builder_get("bmigrate.glade");
	if (NULL == builder) 
		return(EXIT_FAILURE);

	hwin_init(&b.wins, builder);
	gtk_builder_connect_signals(builder, &b);
	g_object_unref(G_OBJECT(builder));

	/*
	 * Have two running timers: once per second, forcing a refresh of
	 * the window system; then another at four times per second
	 * updating our cold statistics.
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
