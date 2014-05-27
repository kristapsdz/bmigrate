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
#include <errno.h>
#include <math.h>
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

#include <gsl/gsl_multifit.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>

#include "extern.h"

/*
 * These are all widgets that may be or are visible.
 */
struct	hwin {
	GtkWindow	 *config;
	GtkMenuBar	 *menu;
	GtkMenuItem	 *menuquit;
	GtkMenuItem	 *menufile;
	GtkStatusbar	 *status;
	GtkCheckMenuItem *viewdev;
	GtkCheckMenuItem *viewpoly;
	GtkCheckMenuItem *viewpolyminpdf;
	GtkCheckMenuItem *viewpolymincdf;
	GtkCheckMenuItem *viewmeanminpdf;
	GtkCheckMenuItem *viewmeanmincdf;
	GtkToggleButton	 *weighted;
	GtkEntry	 *stop;
	GtkEntry	 *input;
	GtkEntry	 *payoff;
	GtkEntry	 *xmin;
	GtkEntry	 *xmax;
	GtkNotebook	 *inputs;
	GtkNotebook	 *payoffs;
	GtkLabel	 *error;
	GtkEntry	 *func;
	GtkAdjustment	 *nthreads;
	GtkAdjustment	 *fitpoly;
	GtkAdjustment	 *pop;
	GtkAdjustment	 *islands;
	GtkEntry	 *totalpop;
	GtkEntry	 *alpha;
	GtkEntry	 *delta;
	GtkEntry	 *migrate;
	GtkEntry	 *incumbents;
	GtkLabel	 *curthreads;
	GtkToggleButton	 *analsingle;
	GtkToggleButton	 *analmultiple;
#define	SIZE_COLOURS	  12 
	GdkRGBA		  colours[SIZE_COLOURS];
};

struct	curwin {
	int		  viewdev;
	int		  viewpoly;
	int		  viewpolyminpdf;
	int		  viewpolymincdf;
	int		  viewmeanminpdf;
	int		  viewmeanmincdf;
};

/*
 * Main structure governing general state of the system.
 */
struct	bmigrate {
	struct hwin	  wins; /* GUI components */
	size_t		  nextcolour; /* next colour to assign */
	GList		 *sims; /* active simulations */
	GTimer		 *status_elapsed; /* elapsed since status update */
	uint64_t	  lastmatches; /* last seen number of matches */
};

static	const char *const inputs[INPUT__MAX] = {
	"uniform",
	"mapped",
};

static	const char *const payoffs[PAYOFF__MAX] = {
	"continuum",
	"finite two-player two-strategy",
};

/*
 * Initialise the fixed widgets.
 * Some widgets (e.g., "processing" dialog) are created dynamically and
 * will not be marshalled here.
 */
static void
windows_init(struct bmigrate *b, GtkBuilder *builder)
{
	GObject		*w;
	gchar		 buf[1024];
	int		 onlprocs;
#ifdef	__linux__
	onlprocs = sysconf(_SC_NPROCESSORS_ONLN);
#else
	onlprocs = g_get_num_processors();
#endif
	b->wins.config = GTK_WINDOW
		(gtk_builder_get_object(builder, "window1"));
	b->wins.status = GTK_STATUSBAR
		(gtk_builder_get_object(builder, "statusbar1"));
	b->wins.menu = GTK_MENU_BAR
		(gtk_builder_get_object(builder, "menubar1"));
	b->wins.menufile = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem1"));
	b->wins.viewdev = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem6"));
	b->wins.viewpoly = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem7"));
	b->wins.viewpolyminpdf = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem9"));
	b->wins.viewpolymincdf = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem11"));
	b->wins.viewmeanminpdf = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem10"));
	b->wins.viewmeanmincdf = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem12"));
	b->wins.weighted = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "checkbutton1"));
	b->wins.menuquit = GTK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem5"));
	b->wins.input = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry3"));
	b->wins.payoff = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry11"));
	b->wins.stop = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry9"));
	b->wins.xmin = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry8"));
	b->wins.xmax = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry10"));
	b->wins.inputs = GTK_NOTEBOOK
		(gtk_builder_get_object(builder, "notebook1"));
	b->wins.payoffs = GTK_NOTEBOOK
		(gtk_builder_get_object(builder, "notebook2"));
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
	b->wins.curthreads = GTK_LABEL
		(gtk_builder_get_object(builder, "label10"));
	b->wins.analsingle = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton3"));
	b->wins.analmultiple = GTK_TOGGLE_BUTTON
		(gtk_builder_get_object(builder, "radiobutton4"));
	b->wins.alpha = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry13"));
	b->wins.delta = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry14"));
	b->wins.migrate = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry1"));
	b->wins.incumbents = GTK_ENTRY
		(gtk_builder_get_object(builder, "entry15"));

	/* Set the initially-selected notebooks. */
	gtk_entry_set_text(b->wins.input, inputs
		[gtk_notebook_get_current_page(b->wins.inputs)]);
	gtk_entry_set_text(b->wins.payoff, payoffs
		[gtk_notebook_get_current_page(b->wins.payoffs)]);

	/* Builder doesn't do this. */
	w = gtk_builder_get_object(builder, "comboboxtext1");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);

	/* Maximum number of processors. */
	gtk_adjustment_set_upper(b->wins.nthreads, onlprocs);
	w = gtk_builder_get_object(builder, "label12");
	(void)g_snprintf(buf, sizeof(buf), "%d", onlprocs);
	gtk_label_set_text(GTK_LABEL(w), buf);

	/* Compute initial total population. */
	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(b->wins.pop) *
		gtk_adjustment_get_value(b->wins.islands));
	gtk_entry_set_text(b->wins.totalpop, buf);

	/* Initialise our helpful run-load. */
	gtk_label_set_text(b->wins.curthreads, "(0% active)");
	gtk_widget_queue_draw(GTK_WIDGET(b->wins.curthreads));

	gdk_rgba_parse(&b->wins.colours[0], "red");
	gdk_rgba_parse(&b->wins.colours[1], "green");
	gdk_rgba_parse(&b->wins.colours[2], "purple");
	gdk_rgba_parse(&b->wins.colours[3], "yellow green");
	gdk_rgba_parse(&b->wins.colours[4], "violet");
	gdk_rgba_parse(&b->wins.colours[5], "yellow");
	gdk_rgba_parse(&b->wins.colours[6], "blue violet");
	gdk_rgba_parse(&b->wins.colours[7], "goldenrod");
	gdk_rgba_parse(&b->wins.colours[8], "blue");
	gdk_rgba_parse(&b->wins.colours[9], "orange");
	gdk_rgba_parse(&b->wins.colours[10], "turquoise");
	gdk_rgba_parse(&b->wins.colours[11], "orange red");
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

	switch (p->type) {
	case (PAYOFF_CONTINUUM2):
		hnode_free(p->d.continuum.exp);
		break;
	default:
		break;
	}

	g_mutex_clear(&p->hot.mux);
	g_mutex_clear(&p->warm.mux);
	g_cond_clear(&p->hot.cond);
	g_free(p->hot.means);
	g_free(p->hot.meandiff);
	g_free(p->hot.runs);
	g_free(p->warm.means);
	g_free(p->warm.variances);
	g_free(p->warm.coeffs);
	g_free(p->warm.fits);
	g_free(p->warm.runs);
	g_free(p->cold.means);
	g_free(p->cold.variances);
	g_free(p->cold.fitmins);
	g_free(p->cold.meanmins);
	g_free(p->cold.coeffs);
	g_free(p->cold.fits);
	g_free(p->cold.runs);
	g_free(p->pops);
	g_free(p->threads);
	g_free(p);
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

	if (p->terminate)
		return;
	g_debug("Stopping simulation %p", p);
	p->terminate = 1;
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
	if (NULL != p->status_elapsed)
		g_timer_destroy(p->status_elapsed);
	p->status_elapsed = NULL;
}

/*
 * Dereference a simulation.
 * This means that we've closed an output window that's looking at a
 * particular simulation.
 */
static void
on_sim_deref(gpointer dat)
{
	struct sim	*sim = dat;

	g_debug("Simulation %p deref (now %zu)", sim, sim->refs);
	if (0 != --sim->refs) 
		return;
	g_debug("Simulation %p deref triggering termination", sim);
	sim->terminate = 1;
}

static void
on_sims_deref(gpointer dat)
{

	g_list_free_full(dat, on_sim_deref);
}

/*
 * This copies data from the threads into local storage.
 * TODO: use reader locks.
 */
static gboolean
on_sim_copyout(gpointer dat)
{
	struct bmigrate	*b = dat;
	GList		*list;
	struct sim	*sim;
	size_t		 changed;
	int		 nocopy;

	changed = 0;
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
		nocopy = sim->hot.copyout > 0;
		if ( ! nocopy)
			sim->hot.copyout = 1;
		g_mutex_unlock(&sim->hot.mux);
		if (nocopy) {
			g_debug("Simulation %p still in copyout", sim);
			continue;
		}
		g_mutex_lock(&sim->warm.mux);
		memcpy(sim->cold.means, 
			sim->warm.means,
			sizeof(double) * sim->dims);
		sim->cold.meanmin = sim->warm.meanmin;
		memcpy(sim->cold.variances, 
			sim->warm.variances,
			sizeof(double) * sim->dims);
		memcpy(sim->cold.fits, 
			sim->warm.fits,
			sizeof(double) * sim->dims);
		sim->cold.fitmin = sim->warm.fitmin;
		memcpy(sim->cold.coeffs, 
			sim->warm.coeffs,
			sizeof(double) * (sim->fitpoly + 1));
		memcpy(sim->cold.runs, 
			sim->warm.runs,
			sizeof(size_t) * sim->dims);
		sim->cold.truns = sim->warm.truns;
		g_mutex_unlock(&sim->warm.mux);
		sim->cold.fitmins[sim->cold.fitmin]++;
		sim->cold.meanmins[sim->cold.meanmin]++;
		sim->cold.distsz++;
		changed++;
	}

	if (0 == changed)
		return(TRUE);

	/* Update our windows IFF we've changed some data. */
	list = gtk_window_list_toplevels();
	for ( ; list != NULL; list = list->next)
		gtk_widget_queue_draw(GTK_WIDGET(list->data));

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
	struct sim	*sim;
	gchar		 buf[1024];
	GList		*list;
	uint64_t	 runs;
	size_t		 i, nprocs;
	double		 elapsed, onlprocs;

#ifdef	__linux__
	onlprocs = sysconf( _SC_NPROCESSORS_ONLN );
#else
	onlprocs = g_get_num_processors();
#endif
	nprocs = runs = 0;
	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = (struct sim *)list->data;
		runs += sim->cold.truns * sim->stop;
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
		} else if ( ! sim->terminate)
			nprocs += sim->nprocs;
	}

	/* Remind us of how many threads we're running. */
	(void)g_snprintf(buf, sizeof(buf),
		"(%g%% active)", 100 * (nprocs / onlprocs));
	gtk_label_set_text(b->wins.curthreads, buf);
	
	/* Tell us how many generations have transpired. */
	if (0.0 == (elapsed = g_timer_elapsed(b->status_elapsed, NULL)))
		elapsed = DBL_MIN;
	(void)g_snprintf(buf, sizeof(buf), "Running "
		"%zu threads, %.0f matches/second.", 
		nprocs, (runs - b->lastmatches) / elapsed);
	gtk_statusbar_pop(b->wins.status, 0);
	gtk_statusbar_push(b->wins.status, 0, buf);
	g_timer_start(b->status_elapsed);
	b->lastmatches = runs;
	return(TRUE);
}

static int
entry2size(GtkEntry *entry, size_t *sz)
{
	guint64	 v;
	gchar	*ep;

	v = g_ascii_strtoull(gtk_entry_get_text(entry), &ep, 10);
	if (ERANGE == errno || '\0' != *ep || v > SIZE_MAX)
		return(0);

	*sz = (size_t)v;
	return(1);
}

static int
entry2double(GtkEntry *entry, gdouble *sz)
{
	gchar	*ep;

	*sz = g_ascii_strtod(gtk_entry_get_text(entry), &ep);
	return(ERANGE != errno && '\0' == *ep);
}

static void
drawgrid(cairo_t *cr, double width, double height)
{
	static const double dash[] = {6.0};

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_set_line_width(cr, 0.2);

	/* Top line. */
	cairo_move_to(cr, 0.0, 0.0);
	cairo_line_to(cr, width, 0.0);
	/* Bottom line. */
	cairo_move_to(cr, 0.0, height);
	cairo_line_to(cr, width, height);
	/* Left line. */
	cairo_move_to(cr, 0.0, 0.0);
	cairo_line_to(cr, 0.0, height);
	/* Right line. */
	cairo_move_to(cr, width, 0.0);
	cairo_line_to(cr, width, height);
	/* Horizontal middle. */
	cairo_move_to(cr, width * 0.5, 0.0);
	cairo_line_to(cr, width * 0.5, height);
	/* Vertical middle. */
	cairo_move_to(cr, 0.0, height * 0.5);
	cairo_line_to(cr, width, height * 0.5);

	cairo_stroke(cr);
	cairo_set_dash(cr, dash, 1, 0);

	/* Vertical left. */
	cairo_move_to(cr, 0.0, height * 0.25);
	cairo_line_to(cr, width, height * 0.25);
	/* Vertical right. */
	cairo_move_to(cr, 0.0, height * 0.75);
	cairo_line_to(cr, width, height * 0.75);
	/* Horizontal top. */
	cairo_move_to(cr, width * 0.75, 0.0);
	cairo_line_to(cr, width * 0.75, height);
	/* Horizontal bottom. */
	cairo_move_to(cr, width * 0.25, 0.0);
	cairo_line_to(cr, width * 0.25, height);

	cairo_stroke(cr);
	cairo_set_dash(cr, dash, 0, 0);
}

static void
drawlabels(const struct curwin *cur, cairo_t *cr, 
	double *widthp, double *heightp,
	double miny, double maxy, double minx, double maxx)
{
	cairo_text_extents_t e;
	char	 	 buf[1024];
	double		 width, height;

	width = *widthp;
	height = *heightp;

	cairo_text_extents(cr, "-10.00", &e);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); 

	/* CDF and PDF graphs have an opaque y-axis. */
	if ( ! cur->viewpolyminpdf &&
		! cur->viewpolymincdf &&
		! cur->viewmeanminpdf &&
		! cur->viewmeanmincdf) {
		/* Bottom right. */
		cairo_move_to(cr, width - e.width, 
			height - e.height * 3.0);
		(void)snprintf(buf, sizeof(buf), "%.2g", miny);
		cairo_show_text(cr, buf);

		/* Middle-bottom right. */
		cairo_move_to(cr, width - e.width, 
			height * 0.75 - 1.5 * e.height);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", miny + (maxy + miny) * 0.25);
		cairo_show_text(cr, buf);

		/* Middle right. */
		cairo_move_to(cr, width - e.width, 
			height * 0.5 - 0.5 * e.height);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", (maxy + miny) * 0.5);
		cairo_show_text(cr, buf);

		/* Middle-top right. */
		cairo_move_to(cr, width - e.width, height * 0.25);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", miny + (maxy + miny) * 0.75);
		cairo_show_text(cr, buf);

		/* Top right. */
		cairo_move_to(cr, width - e.width, e.height * 1.5);
		(void)snprintf(buf, sizeof(buf), "%.2g", maxy);
		cairo_show_text(cr, buf);

		/* Right bottom. */
		cairo_move_to(cr, width - e.width * 1.5, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), "%.2g", maxx);
		cairo_show_text(cr, buf);

		/* Middle-left bottom. */
		cairo_move_to(cr, width * 0.25 - e.width * 0.5, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", minx + (maxx + minx) * 0.25);
		cairo_show_text(cr, buf);

		/* Middle bottom. */
		cairo_move_to(cr, width * 0.5 - e.width * 0.75, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", (maxx + minx) * 0.5);
		cairo_show_text(cr, buf);

		/* Middle-right bottom. */
		cairo_move_to(cr, width * 0.75 - e.width, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			"%.2g", minx + (maxx + minx) * 0.75);
		cairo_show_text(cr, buf);

		/* Left bottom. */
		cairo_move_to(cr, e.width * 0.25, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), "%.2g", minx);
		cairo_show_text(cr, buf);

		*widthp -= e.width * 1.3;
		*heightp -= e.height * 3.0;
		return;
	}

	/* Right bottom. */
	cairo_move_to(cr, width - e.width * 1.5, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", maxx);
	cairo_show_text(cr, buf);

	/* Middle-left bottom. */
	cairo_move_to(cr, width * 0.25 - e.width * 0.5, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), 
		"%.2g", minx + (maxx + minx) * 0.25);
	cairo_show_text(cr, buf);

	/* Middle bottom. */
	cairo_move_to(cr, width * 0.5 - e.width * 0.75, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", (maxx + minx) * 0.5);
	cairo_show_text(cr, buf);

	/* Middle-right bottom. */
	cairo_move_to(cr, width * 0.75 - e.width, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), 
		"%.2g", minx + (maxx + minx) * 0.75);
	cairo_show_text(cr, buf);

	/* Left bottom. */
	cairo_move_to(cr, e.width * 0.25, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", minx);
	cairo_show_text(cr, buf);

	*heightp -= e.height * 3.0;
}

/*
 * For a given simulation "s", compute the maximum "y" value in a graph
 * given the type of graph we want to produce.
 */
static void
max_sim(const struct curwin *cur, const struct sim *s, 
	double *xmin, double *xmax, double *maxy)
{
	size_t	 i;
	double	 v;

	if (cur->viewdev) {
		for (i = 0; i < s->dims; i++) {
			v = s->cold.means[i] + s->cold.variances[i];
			if (v > *maxy)
				*maxy = v;
		}
	} else if (cur->viewpolyminpdf) {
		for (i = 0; i < s->dims; i++) {
			v = s->cold.fitmins[i] / 
				(double)s->cold.distsz;
			if (v > *maxy)
				*maxy = v;
		}
	} else if (cur->viewpolymincdf) {
		for (v = 0.0, i = 0; i < s->dims; i++)
			v += s->cold.fitmins[i] / 
				(double)s->cold.distsz;
		if (v > *maxy)
			*maxy = v;
	} else if (cur->viewmeanminpdf) {
		for (i = 0; i < s->dims; i++) {
			v = s->cold.meanmins[i] / 
				(double)s->cold.distsz;
			if (v > *maxy)
				*maxy = v;
		}
	} else if (cur->viewmeanmincdf) {
		for (v = 0.0, i = 0; i < s->dims; i++)
			v += s->cold.meanmins[i] / 
				(double)s->cold.distsz;
		if (v > *maxy)
			*maxy = v;
	} else if (cur->viewpoly) {
		for (i = 0; i < s->dims; i++) {
			v = s->cold.fits[i] > s->cold.means[i] ?
				s->cold.fits[i] : s->cold.means[i];
			if (v > *maxy)
				*maxy = v;
		}
	} else {
		for (i = 0; i < s->dims; i++) {
			v = s->cold.means[i];
			if (v > *maxy)
				*maxy = v;
		}
	}

	if (*xmin > s->d.continuum.xmin)
		*xmin = s->d.continuum.xmin;
	if (*xmax < s->d.continuum.xmax)
		*xmax = s->d.continuum.xmax;
}

/*
 * Main draw event.
 * There's lots we can do to make this more efficient, e.g., computing
 * the stddev/fitpoly arrays in the worker threads instead of here.
 */
gboolean
ondraw(GtkWidget *w, cairo_t *cr, gpointer dat)
{
	struct bmigrate	*b = dat;
	struct curwin	*cur;
	double		 width, height, maxy, v, xmin, xmax;
	GtkWidget	*top;
	struct sim	*sim;
	size_t		 j;
	GList		*sims, *list;

	/* 
	 * Get our window configuration.
	 * Then get our list of simulations.
	 * Both of these are stored as pointers to the top-level window.
	 */
	top = gtk_widget_get_toplevel(w);
	cur = g_object_get_data(G_OBJECT(top), "cfg");
	assert(NULL != cur);
	sims = g_object_get_data(G_OBJECT(top), "sims");
	assert(NULL != sims);

	/* Initialise the window view to be all white. */
	width = gtk_widget_get_allocated_width(w);
	height = gtk_widget_get_allocated_height(w);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, width, height);
	cairo_fill(cr);

	/*
	 * Determine the boundaries of the graph.
	 * These will, for all graphs in our set, compute the range and
	 * domain extrema (the range minimum is always zero).
	 * Buffer the maximum range by 110%.
	 */
	xmin = FLT_MAX;
	xmax = maxy = -FLT_MAX;
	for (list = sims; NULL != list; list = list->next)
		max_sim(cur, list->data, &xmin, &xmax, &maxy);

	/* CDF's don't get their windows scaled. */
	if ( ! cur->viewpolymincdf && ! cur->viewmeanmincdf)
		maxy += maxy * 0.1;

	/* Draw our labels. */
	drawlabels(cur, cr, &width, &height, 0.0, maxy, xmin, xmax);

	/*
	 * Draw curves as specified: either the "raw" curve (just the
	 * data), the raw curve and its standard deviation above and
	 * below, or the raw curve plus the polynomial fitting.
	 */
#define	GETX(_j) (width * (_j) / (double)(sim->dims - 1))
#define	GETY(_v) (height - ((_v) / maxy * height))
#define	GETC(_a) b->wins.colours[sim->colour].red, \
		 b->wins.colours[sim->colour].green, \
		 b->wins.colours[sim->colour].blue, (_a)
	cairo_save(cr);
	for (list = sims; NULL != list; list = list->next) {
		sim = list->data;
		assert(NULL != sim);

		if (cur->viewdev) {
			/*
			 * If stipulated, draw the standard deviation
			 * above and below the curve.
			 * Obviously, don't go below zero.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.means[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				v -= sim->cold.variances[j - 1];
				if (v < 0.0)
					v = 0.0;
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.means[j];
				v -= sim->cold.variances[j];
				if (v < 0.0)
					v = 0.0;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				v += sim->cold.variances[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.means[j];
				v += sim->cold.variances[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
		} else if (cur->viewpoly) {
			/*
			 * Compute and draw the curve stipulated by the
			 * coefficients our workers have computed.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.means[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.fits[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.fits[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		} else if (cur->viewpolyminpdf) {
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.fitmins[j - 1] / 
					(double)sim->cold.distsz;
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.fitmins[j] / 
					(double)sim->cold.distsz;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		} else if (cur->viewpolymincdf) {
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += sim->cold.fitmins[j] / 
					(double)sim->cold.distsz;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		} else if (cur->viewmeanminpdf) {
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.meanmins[j - 1] / 
					(double)sim->cold.distsz;
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.meanmins[j] / 
					(double)sim->cold.distsz;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		} else if (cur->viewmeanmincdf) {
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += sim->cold.meanmins[j] / 
					(double)sim->cold.distsz;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		} else {
			/* 
			 * Draw just the raw curve.
			 * This is the default.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.means[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);

	/* Draw a little grid for reference points. */
	drawgrid(cr, width, height);
	return(TRUE);
}

/*
 * We've requested a different kind of view.
 * First, determine what kind of view update has occured: whether to
 * change to polynomial minimum, etc.
 * Next, iterate through all top-level windows, extracting their current
 * configuration.
 * Finally, update each individual configuration and redraw.
 */
void
onviewtoggle(GtkMenuItem *menuitem, gpointer dat)
{
	struct bmigrate	*b = dat;
	GList		*list;
	struct curwin	*cur;

	list = gtk_window_list_toplevels();
	for ( ; list != NULL; list = list->next) {
		/* Is this window a simulation view? */
		cur = g_object_get_data(G_OBJECT(list->data), "cfg");
		if (NULL == cur)
			continue;
		cur->viewpoly = gtk_check_menu_item_get_active
			(b->wins.viewpoly);
		cur->viewdev = gtk_check_menu_item_get_active
			(b->wins.viewdev);
		cur->viewpolyminpdf = gtk_check_menu_item_get_active
			(b->wins.viewpolyminpdf);
		cur->viewpolymincdf = gtk_check_menu_item_get_active
			(b->wins.viewpolymincdf);
		cur->viewmeanminpdf = gtk_check_menu_item_get_active
			(b->wins.viewmeanminpdf);
		cur->viewmeanmincdf = gtk_check_menu_item_get_active
			(b->wins.viewmeanmincdf);
		gtk_widget_queue_draw(GTK_WIDGET(list->data));
	}
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
 * We want to run the given simulation.
 * First, verify all data.
 * Then actually run.
 */
void
on_activate(GtkButton *button, gpointer dat)
{
	gint	 	  input, payoff;
	struct bmigrate	 *b = dat;
	struct hnode	**exp;
	const gchar	 *txt;
	gdouble		  xmin, xmax, delta, alpha, m;
	size_t		  i, totalpop, islandpop, islands, 
			  stop, slices;
	struct sim	 *sim;
	GdkRGBA	  	  color = { 1.0, 1.0, 1.0, 1.0 };
	GtkWidget	 *w, *draw;
	struct curwin	 *cur;
	GList		 *sims;

	input = gtk_notebook_get_current_page(b->wins.inputs);
	if (INPUT_UNIFORM != input) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: only uniform input supported.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2size(b->wins.totalpop, &totalpop)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: total population not a number.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2size(b->wins.stop, &stop)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: stopping time not a number.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} 

	if (gtk_toggle_button_get_active(b->wins.analsingle)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: single run not supported.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}
	
	islandpop = (size_t)gtk_adjustment_get_value(b->wins.pop);
	islands = (size_t)gtk_adjustment_get_value(b->wins.islands);

	payoff = gtk_notebook_get_current_page(b->wins.payoffs);
	if (PAYOFF_CONTINUUM2 != payoff) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: only continuum payoff supported.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}

	if ( ! entry2double(b->wins.xmin, &xmin)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: minimum strategy not a number.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2double(b->wins.xmax, &xmax)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: maximum strategy not a number.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if (xmin >= xmax) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: bad minimum/maximum strategy order.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2double(b->wins.alpha, &alpha)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: multiplier parameter not a number");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2double(b->wins.delta, &delta)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: multiplier parameter not a number");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2double(b->wins.migrate, &m)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: migration not a number");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2size(b->wins.incumbents, &slices)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: incumbents not a number");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}

	txt = gtk_entry_get_text(b->wins.func);
	exp = hnode_parse((const gchar **)&txt);
	if (NULL == exp) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: bad continuum function");
		gtk_widget_show(GTK_WIDGET(b->wins.error));
		return;
	}

	/* 
	 * All parameters check out!
	 * Start the simulation now.
	 */
	gtk_widget_hide(GTK_WIDGET(b->wins.error));

	sim = g_malloc0(sizeof(struct sim));
	sim->dims = slices;
	sim->fitpoly = gtk_adjustment_get_value(b->wins.fitpoly);
	sim->weighted = gtk_toggle_button_get_active(b->wins.weighted);
	g_mutex_init(&sim->hot.mux);
	g_mutex_init(&sim->warm.mux);
	g_cond_init(&sim->hot.cond);
	sim->hot.runs = g_malloc0_n
		(sim->dims, sizeof(size_t));
	sim->warm.runs = g_malloc0_n
		(sim->dims, sizeof(size_t));
	sim->cold.runs = g_malloc0_n
		(sim->dims, sizeof(size_t));
	sim->hot.means = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->hot.meandiff = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.means = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.variances = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.means = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.variances = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.fitmins = g_malloc0_n
		(sim->dims, sizeof(size_t));
	sim->cold.meanmins = g_malloc0_n
		(sim->dims, sizeof(size_t));

	sim->type = PAYOFF_CONTINUUM2;
	sim->nprocs = gtk_adjustment_get_value(b->wins.nthreads);
	sim->totalpop = totalpop;
	sim->islands = islands;
	sim->stop = stop;
	sim->alpha = alpha;
	sim->colour = b->nextcolour;
	b->nextcolour = (b->nextcolour + 1) % SIZE_COLOURS;
	sim->delta = delta;
	sim->m = m;
	sim->pops = g_malloc0_n(sim->islands, sizeof(size_t));
	for (i = 0; i < sim->islands; i++)
		sim->pops[i] = islandpop;
	sim->d.continuum.exp = exp;
	sim->d.continuum.xmin = xmin;
	sim->d.continuum.xmax = xmax;
	b->sims = g_list_append(b->sims, sim);
	sim->refs = 1;
	sim->threads = g_malloc0_n(sim->nprocs, sizeof(struct simthr));
	g_debug("New continuum simulation: %zu islands, "
		"%zu total members (%zu per island)", sim->islands,
		sim->totalpop, islandpop);
	g_debug("New continuum migration %g, %g(1 + %g pi)", 
		sim->m, sim->alpha, sim->delta);
	g_debug("New continuum threads: %zu", sim->nprocs);
	g_debug("New continuum polynomial: %zu (%s)", 
		sim->fitpoly, sim->weighted ? "weighted" : "unweighted");

	for (i = 0; i < sim->nprocs; i++) {
		sim->threads[i].rank = i;
		sim->threads[i].sim = sim;
		sim->threads[i].thread = g_thread_new
			(NULL, simulation, &sim->threads[i]);
	}

	sims = g_list_append(NULL, sim);
	cur = g_malloc0(sizeof(struct curwin));
	cur->viewpoly = gtk_check_menu_item_get_active
		(b->wins.viewpoly);
	cur->viewdev = gtk_check_menu_item_get_active
		(b->wins.viewdev);
	cur->viewpolyminpdf = gtk_check_menu_item_get_active
		(b->wins.viewpolyminpdf);
	cur->viewpolymincdf = gtk_check_menu_item_get_active
		(b->wins.viewpolymincdf);
	cur->viewmeanminpdf = gtk_check_menu_item_get_active
		(b->wins.viewmeanminpdf);
	cur->viewmeanmincdf = gtk_check_menu_item_get_active
		(b->wins.viewmeanmincdf);

	/*
	 * Now we create the output window.
	 */
	w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_override_background_color
		(w, GTK_STATE_FLAG_NORMAL, &color);
	draw = gtk_drawing_area_new();
	gtk_widget_set_margin_left(draw, 10);
	gtk_widget_set_margin_right(draw, 10);
	gtk_widget_set_margin_top(draw, 10);
	gtk_widget_set_margin_bottom(draw, 10);
	gtk_widget_set_size_request(draw, 440, 400);
	g_signal_connect(G_OBJECT(draw), 
		"draw", G_CALLBACK(ondraw), b);
	gtk_container_add(GTK_CONTAINER(w), draw);
	gtk_widget_show_all(w);
	g_object_set_data_full(G_OBJECT(w), 
		"cfg", cur, g_free);
	g_object_set_data_full(G_OBJECT(w), 
		"sims", sims, on_sims_deref);
}

/*
 * One of the preset continuum functions for our two-player continuum
 * game possibility.
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
 * Run this whenever we select a page from the material payoff notebook.
 * This sets (for the user) the current payoff in an entry.
 */
gboolean
on_change_payoff(GtkNotebook *notebook, 
	GtkWidget *page, gint pnum, gpointer dat)
{
	struct bmigrate	*b = dat;

	assert(pnum < PAYOFF__MAX);
	gtk_entry_set_text(b->wins.payoff, payoffs[pnum]);
	return(TRUE);
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

void
on_change_totalpop(GtkSpinButton *spinbutton, gpointer dat)
{
	gchar	 	 buf[1024];
	struct bmigrate	*b = dat;

	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(b->wins.pop) *
		gtk_adjustment_get_value(b->wins.islands));
	gtk_entry_set_text(b->wins.totalpop, buf);
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
	hnode_test();

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
	b.status_elapsed = g_timer_new();

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
	gtk_widget_hide(GTK_WIDGET(b.wins.menufile));
	gtkosx_application_set_menu_bar
		(theApp, GTK_MENU_SHELL(b.wins.menu));
	gtkosx_application_sync_menubar(theApp);
	g_signal_connect(theApp, "NSApplicationWillTerminate",
		G_CALLBACK(onterminate), &b);
	gtkosx_application_ready(theApp);
#endif
	g_timeout_add(1000, (GSourceFunc)on_sim_timer, &b);
	g_timeout_add(1000, (GSourceFunc)on_sim_copyout, &b);
	gtk_statusbar_push(b.wins.status, 0, "No simulations.");
	gtk_main();
	bmigrate_free(&b);
	return(EXIT_SUCCESS);
}
