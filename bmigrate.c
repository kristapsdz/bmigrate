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

#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>

#include "extern.h"

/*
 * Pages in the configuration notebook.
 * These correspond to the way that a simulation is going to be
 * prepared, i.e., whether island populations are going to be evenly
 * distributed from a given population size or manually set, etc.
 */
enum	input {
	INPUT_UNIFORM,
	INPUT_MAPPED,
	INPUT__MAX
};

/*
 * This defines the game itself.
 * There can be lots of different kinds of games--two-player bimatrix,
 * continuum games with n-players, etc.
 */
enum	payoff {
	PAYOFF_CONTINUUM2,
	PAYOFF_SYMMETRIC2,
	PAYOFF__MAX
};

/*
 * These are all widgets that may be or are visible.
 */
struct	hwin {
	GtkWindow	 *config;
	GtkMenuBar	 *menu;
	GtkMenuItem	 *menuquit;
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
	GtkAdjustment	 *pop;
	GtkAdjustment	 *islands;
	GtkEntry	 *totalpop;
	GtkEntry	 *alpha;
	GtkEntry	 *delta;
	GtkEntry	 *migrate;
	GtkLabel	 *curthreads;
	GtkToggleButton	 *analsingle;
	GtkToggleButton	 *analmultiple;
};

/*
 * Configuration for a running two-player continuum game.
 * This is associated with a function that's executed with real-valued
 * player strategies, e.g., public goods.
 */
struct	sim_continuum2 {
	struct hnode	**exp;
	double		  xmin;
	double		  xmax;
};

struct	simres {
	GMutex		 mux;
	double		*mutants;
	size_t		*runs;
	size_t		 truns;
	size_t		 dims;
};

struct	sim {
	GThread		 *thread; /* thread of execution */
	size_t		  nprocs; /* processors reserved */
	size_t		  totalpop; /* total population */
	size_t		 *pops; /* per-island population */
	size_t		  islands; /* island population */
	size_t		  refs; /* GUI references */
	int		  terminate; /* terminate the process */
	size_t		  stop; /* when to stop */
	double		  alpha; /* outer multiplier */
	double		  delta; /* inner multiplier */
	double		  m; /* migration probability */
	enum payoff	  type; /* type of game */
	union {
		struct sim_continuum2 continuum2;
	} d;
	struct simres	  results; /* results of simulation */
};

/*
 * Main structure governing general state of the system.
 */
struct	bmigrate {
	struct hwin	  wins; /* GUI components */
	GList		 *sims; /* active simulations */
};

static	const char *const inputs[INPUT__MAX] = {
	"uniform",
	"mapped",
};

static	const char *const payoffs[PAYOFF__MAX] = {
	"continuum two-player",
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

	b->wins.config = GTK_WINDOW
		(gtk_builder_get_object(builder, "window1"));
	b->wins.menu = GTK_MENU_BAR
		(gtk_builder_get_object(builder, "menubar1"));
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

	/* Set the initially-selected notebooks. */
	gtk_entry_set_text(b->wins.input, inputs
		[gtk_notebook_get_current_page(b->wins.inputs)]);
	gtk_entry_set_text(b->wins.payoff, payoffs
		[gtk_notebook_get_current_page(b->wins.payoffs)]);

	/* Builder doesn't do this. */
	w = gtk_builder_get_object(builder, "comboboxtext1");
	gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);

	/* Maximum number of processors. */
	gtk_adjustment_set_upper
		(b->wins.nthreads, g_get_num_processors());

	gtk_label_set_text(b->wins.curthreads, "(0% active)");
	gtk_widget_queue_draw(GTK_WIDGET(b->wins.curthreads));

	w = gtk_builder_get_object(builder, "label12");
	(void)g_snprintf(buf, sizeof(buf), 
		"%d", g_get_num_processors());
	gtk_label_set_text(GTK_LABEL(w), buf);

	(void)g_snprintf(buf, sizeof(buf),
		"%g", gtk_adjustment_get_value(b->wins.pop) *
		gtk_adjustment_get_value(b->wins.islands));
	gtk_entry_set_text(b->wins.totalpop, buf);
}

static void
sim_free(gpointer arg)
{
	struct sim	*p = arg;

	if (NULL == p)
		return;

	g_debug("Freeing simulation %p", p);

	switch (p->type) {
	case (PAYOFF_CONTINUUM2):
		hnode_free(p->d.continuum2.exp);
		break;
	default:
		break;
	}

	if (NULL != p->thread) {
		g_debug("Freeing joining thread %p "
			"(simulation %p)", p->thread, p);
		g_thread_join(p->thread);
	}

	g_free(p->results.mutants);
	g_free(p->results.runs);
	g_free(p->pops);
	g_free(p);
}

static void
sim_stop(gpointer arg, gpointer unused)
{
	struct sim	*p = arg;

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
}

static double
pi_tau(size_t i, size_t n, const double *m)
{

	return(((i - 1) / (double)(n - 1) * m[3]) + 
		((n - i) / (double)(n - 1) * m[2]));
}

static double
pi_theta(size_t i, size_t n, const double *m)
{

	return((i / (double)(n - 1) * m[1]) + 
		((n - i - 1) / (double)(n - 1) * m[0]));
}

static int
on_sim_next(const gsl_rng *rng, struct sim *sim, 
	double *mutant, double *incumbent, 
	size_t *mutantidx, size_t *incumbentidx)
{

	if (sim->terminate)
		return(0);

	*mutantidx = gsl_rng_uniform_int(rng, sim->results.dims);
	*incumbentidx = gsl_rng_uniform_int(rng, sim->results.dims);
	*mutant = sim->d.continuum2.xmin + 
		(sim->d.continuum2.xmax - 
		 sim->d.continuum2.xmin) * 
		(*mutantidx / (double)sim->results.dims);
	*incumbent = sim->d.continuum2.xmin + 
		(sim->d.continuum2.xmax - 
		 sim->d.continuum2.xmin) * 
		(*incumbentidx / (double)sim->results.dims);
	return(1);
}

static void *
on_sim_new(void *arg)
{
	struct sim	*sim = arg;
	double		 mutant, incumbent, m;
	double		 payoffs[4];
	size_t		*kids[2], *migrants[2], *imutants;
	size_t		 t, j, k, new, mutants, incumbents,
			 len1, len2, mutantidx, incumbentidx;
	int		 mutant_old, mutant_new;
	gsl_rng		*rng;

	rng = gsl_rng_alloc(gsl_rng_default);

	g_debug("Thread %p (simulation %p) using RNG %s", 
		g_thread_self(), sim, gsl_rng_name(rng));

	m = 0.5;

	kids[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	kids[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	imutants = g_malloc0_n(sim->islands, sizeof(size_t));

again:
	if ( ! on_sim_next(rng, sim, &mutant, 
		&incumbent, &mutantidx, &incumbentidx)) {
		g_free(imutants);
		g_free(kids[0]);
		g_free(kids[1]);
		g_free(migrants[0]);
		g_free(migrants[1]);
		sim->nprocs = 0;
		g_debug("Thread %p (simulation %p) exiting", 
			g_thread_self(), sim);
		return(NULL);
	}

	payoffs[0] = sim->alpha * (1.0 + sim->delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			incumbent, incumbent + incumbent, 2));
	payoffs[1] = sim->alpha * (1.0 + sim->delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			incumbent, incumbent + mutant, 2));
	payoffs[2] = sim->alpha * (1.0 + sim->delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			mutant, mutant + incumbent, 2));
	payoffs[3] = sim->alpha * (1.0 + sim->delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			mutant, mutant + mutant, 2));

	/* 
	 * Initialise a random island to have one mutant. 
	 * The rest are all incumbents.
	 */
	memset(imutants, 0, sim->islands * sizeof(size_t));
	imutants[gsl_rng_uniform_int(rng, sim->islands)] = 1;
	mutants = 1;
	incumbents = sim->totalpop - mutants;

	for (t = 0; t < sim->stop; t++) {
		/*
		 * Birth process: have each individual (first mutants,
		 * then incumbents) give birth.
		 * Use a Poisson process with the given mean in order to
		 * properly compute this.
		 */
		for (j = 0; j < sim->islands; j++) {
			for (k = 0; k < imutants[j]; k++) 
				kids[0][j] += gsl_ran_poisson
					(rng, 
					 pi_tau(imutants[j], 
					 sim->pops[j], payoffs));
			for ( ; k < sim->pops[j]; k++)
				kids[1][j] += gsl_ran_poisson
					(rng, 
					 pi_theta(imutants[j], 
					 sim->pops[j], payoffs));
		}

		/*
		 * Determine whether we're going to migrate and, if
		 * migration is stipulated, to where.
		 */
		for (j = 0; j < sim->islands; j++) {
			for (k = 0; k < kids[0][j]; k++) {
				new = j;
				if (gsl_rng_uniform(rng) < m) do 
					new = gsl_rng_uniform_int
						(rng, sim->islands);
				while (new == j);
				migrants[0][new]++;
			}
			for (k = 0; k < kids[1][j]; k++) {
				new = j;
				if (gsl_rng_uniform(rng) < m) do 
					new = gsl_rng_uniform_int
						(rng, sim->islands);
				while (new == j);
				migrants[1][new]++;
			}
			kids[0][j] = kids[1][j] = 0;
		}

		for (j = 0; j < sim->islands; j++) {
			len1 = migrants[0][j] + migrants[1][j];
			if (0 == len1)
				continue;

			len2 = gsl_rng_uniform_int(rng, sim->pops[j]);
			mutant_old = len2 < imutants[j];
			len2 = gsl_rng_uniform_int(rng, len1);
			mutant_new = len2 < migrants[0][j];

			if (mutant_old && ! mutant_new) {
				imutants[j]--;
				mutants--;
				incumbents++;
			} else if ( ! mutant_old && mutant_new) {
				imutants[j]++;
				mutants++;
				incumbents--;
			}

			migrants[0][j] = migrants[1][j] = 0;
		}

		if (0 == mutants || 0 == incumbents) 
			break;
	}

	g_mutex_lock(&sim->results.mux);
	sim->results.mutants[incumbentidx] += 
		(mutants / (double)sim->totalpop);
	sim->results.runs[incumbentidx]++;
	sim->results.truns++;
	g_mutex_unlock(&sim->results.mux);
	goto again;
}

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

static gboolean
on_timer(gpointer dat)
{
	struct bmigrate	*b = dat;
	size_t		 nprocs;
	struct sim	*sim;
	gchar		 buf[1024];
	GList		*list;

	nprocs = 0;
	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = (struct sim *)list->data;
		if (0 == sim->nprocs && NULL != sim->thread) {
			g_debug("Timeout handler joining thread "
				"%p (simulation %p)", sim->thread, sim);
			g_thread_join(sim->thread);
			sim->thread = NULL;
		}
		g_debug("Simulation %p runs: %zu", sim, sim->results.truns);
		nprocs += sim->nprocs;
	}

	(void)g_snprintf(buf, sizeof(buf),
		"(%g%% active)", 100 * (nprocs / 
			(double)g_get_num_processors()));
	gtk_label_set_text(b->wins.curthreads, buf);

	list = gtk_window_list_toplevels();
	for ( ; list != NULL; list = list->next)
		gtk_widget_queue_draw(GTK_WIDGET(list->data));

	return(TRUE);
}

static void
drawlabels(cairo_t *cr, double *widthp, double *heightp,
	double miny, double maxy, double minx, double maxx)
{
	cairo_text_extents_t e;
	char	 	 buf[1024];
	double		 width, height;

	width = *widthp;
	height = *heightp;

	cairo_text_extents(cr, "-10.00", &e);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); 

	/* Top right. */
	cairo_move_to(cr, width - e.width, 
		height - e.height * 2.0);
	(void)snprintf(buf, sizeof(buf), "%.2g", miny);
	cairo_show_text(cr, buf);

	/* Middle right. */
	cairo_move_to(cr, width - e.width, height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", (maxy + miny) * 0.5);
	cairo_show_text(cr, buf);

	/* Bottom right. */
	cairo_move_to(cr, width - e.width, e.height * 1.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", maxy);
	cairo_show_text(cr, buf);

	/* Right bottom. */
	cairo_move_to(cr, width - e.width * 1.5, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", maxx);
	cairo_show_text(cr, buf);

	/* Middle bottom. */
	cairo_move_to(cr, width * 0.5 - e.width * 0.5, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", (maxx + minx) * 0.5);
	cairo_show_text(cr, buf);

	/* Left bottom. */
	cairo_move_to(cr, e.width * 0.25, 
		height - e.height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", minx);
	cairo_show_text(cr, buf);

	*widthp -= e.width * 1.3;
	*heightp -= e.height * 3.0;
}

gboolean
ondraw(GtkWidget *w, cairo_t *cr, gpointer dat)
{
	double		 width, height, maxy, v, xmin, xmax;
	GtkWidget	*top;
	struct sim	*sim;
	size_t		 i, j, objs;
	gchar		 buf[128];

	width = gtk_widget_get_allocated_width(w);
	height = gtk_widget_get_allocated_height(w);

	top = gtk_widget_get_toplevel(w);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, width, height);
	cairo_fill(cr);

	xmin = FLT_MAX;
	xmax = maxy = -FLT_MAX;

	for (objs = 0; objs < 32; objs++) {
		(void)g_snprintf(buf, sizeof(buf), "sim%zu", objs);
		sim = g_object_get_data(G_OBJECT(top), buf);
		if (NULL == sim)
			break;
		for (j = 0; j < sim->results.dims; j++) {
			v = (0 == sim->results.runs[j]) ? 0.0 :
				sim->results.mutants[j] / 
				(double)sim->results.runs[j];
			if (v > maxy)
				maxy = v;
		}
		if (xmin > sim->d.continuum2.xmin)
			xmin = sim->d.continuum2.xmin;
		if (xmax < sim->d.continuum2.xmax)
			xmax = sim->d.continuum2.xmax;
	}

	maxy += maxy * 0.1;
	drawlabels(cr, &width, &height, 0.0, maxy, xmin, xmax);
	cairo_save(cr);

	for (i = 0; i < objs; i++) {
		(void)g_snprintf(buf, sizeof(buf), "sim%zu", i);
		sim = g_object_get_data(G_OBJECT(top), buf);
		assert(NULL != sim);
		for (j = 1; j < sim->results.dims; j++) {
			v = (0 == sim->results.runs[j - 1]) ? 0.0 :
				sim->results.mutants[j - 1] / 
				(double)sim->results.runs[j - 1];
			cairo_move_to(cr, 
				width * (j - 1) / (double)(sim->results.dims - 1),
				height - (v / maxy * height));
			v = (0 == sim->results.runs[j]) ? 0.0 :
				sim->results.mutants[j] / 
				(double)sim->results.runs[j];
			cairo_line_to(cr, 
				width * j / (double)(sim->results.dims - 1),
				height - (v / maxy * height));
		}
	}
	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); 
	cairo_stroke(cr);
	cairo_restore(cr);
	return(TRUE);
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
	size_t		  i, totalpop, islandpop, islands, stop;
	struct sim	 *sim;
	GdkRGBA	  	  color = { 1.0, 1.0, 1.0, 1.0 };
	GtkWidget	 *w, *draw;

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
	if (PAYOFF_CONTINUUM2 != input) {
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
	g_mutex_init(&sim->results.mux);
	sim->results.dims = 100;
	sim->results.runs = g_malloc0_n
		(sim->results.dims, sizeof(size_t));
	sim->results.mutants = g_malloc0_n
		(sim->results.dims, sizeof(double));

	sim->type = PAYOFF_CONTINUUM2;
	sim->nprocs = gtk_adjustment_get_value(b->wins.nthreads);
	sim->totalpop = totalpop;
	sim->islands = islands;
	sim->stop = stop;
	sim->alpha = alpha;
	sim->delta = delta;
	sim->m = m;
	sim->pops = g_malloc0_n(sim->islands, sizeof(size_t));
	for (i = 0; i < sim->islands; i++)
		sim->pops[i] = islandpop;
	sim->d.continuum2.exp = exp;
	sim->d.continuum2.xmin = xmin;
	sim->d.continuum2.xmax = xmax;
	b->sims = g_list_append(b->sims, sim);
	sim->thread = g_thread_new(NULL, on_sim_new, sim);
	sim->refs = 1;

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
		"draw", G_CALLBACK(ondraw), sim);
	gtk_container_add(GTK_CONTAINER(w), draw);
	gtk_widget_show_all(w);
	g_object_set_data_full(G_OBJECT(w), 
		"sim0", sim, on_sim_deref);

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
	g_timeout_add(500, (GSourceFunc)on_timer, &b);
	gtk_main();
	bmigrate_free(&b);
	return(EXIT_SUCCESS);
}
