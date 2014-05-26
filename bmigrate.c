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
	GtkMenuItem	 *menufile;
	GtkStatusbar	 *status;
	GtkCheckMenuItem *viewdev;
	GtkCheckMenuItem *viewpoly;
	GtkCheckMenuItem *viewpolymin;
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

/*
 * Configuration for a running an n-player continuum game.
 * This is associated with a function that's executed with real-valued
 * player strategies, e.g., public goods.
 */
struct	sim_continuum {
	struct hnode	**exp; /* n-player function */
	double		  xmin; /* minimum strategy */
	double		  xmax; /* maximum strategy */
};

/*
 * This structure is maintained by a thread group for a particular
 * running simulation.
 * All reads/writes must lock the mutex before modifications.
 * This data is periodically snapshotted to "struct simwarm", which is
 * triggered by the last thread to encounter a "copyout" = 1 (threads
 * prior to that will wait on "cond").
 */
struct	simhot {
	GMutex		 mux; /* lock for changing data */
	GCond		 cond; /* mutex for waiting on snapshot */
	double		*means; /* sample mean per incumbent */
	double		*meandiff; /* sum of squares difference */
	size_t		*runs; /* runs per mutant */
	size_t		 truns; /* total number of runs */
	int		 copyout; /* do we need to snapshot? */
	size_t		 copyblock; /* threads blocking on copy */
};

/*
 * A simulation thread group will snapshot its "struct simhot" structure
 * to this when its "copyout" is set to 1.
 * The fields are the same but that "mutants" is set to the fraction and
 * "coeffs" is set (if applicable) to the fitted polynomial coefficients.
 */
struct	simwarm {
	GMutex		 mux; /* lock to change fields */
	double		*means; /* sample mean per incumbent */
	double		*variances; /* sample variance */
	double	   	*coeffs; /* fitpoly coefficients */
	size_t		 fitmin;
	size_t		*runs; /* runs per mutant */
	size_t		 truns; /* total number of runs */
};

/*
 * If fitting to a polynomial, each worker thread may be required to do
 * the polynomial fitting.
 * As such, this is set (if "fitpoly") to contain the necessary
 * parameters for the fitting.
 */
struct	simwork {
	gsl_matrix	*X; /* matrix of independent variables */
	gsl_matrix	*cov; /* covariance matrix */
	gsl_vector	*y; /* vector dependent variables */
	gsl_vector	*c; /* output vector of coefficients */
	gsl_vector	*w; /* vector of weights */
	gsl_multifit_linear_workspace *work;
};

/*
 * Instead of operating on the simulation results themselves, we copy
 * output from "struct simwarm" into a "cold" buffer for viewing.
 * See "struct simwarm" for matching fields. 
 * This is done in the main thread of execution, so it is not locked.
 */
struct	simcold {
	double		*means;
	double		*variances;
	size_t		*runs;
	double	   	*coeffs;
	size_t		 fitmin;
	size_t		*fitmins;
	size_t		 truns;
};

struct	simthr;

/*
 * A single simulation.
 * This can be driven by "nprocs" threads.
 */
struct	sim {
	struct simthr	 *threads; /* threads of execution */
	size_t		  nprocs; /* processors reserved */
	size_t		  dims; /* number of incumbents sampled */
	size_t		  fitpoly; /* fitting polynomial */
	int		  weighted; /* weighted fit poly */
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
	size_t		  colour; /* graph colour */
	union {
		struct sim_continuum continuum;
	} d;
	struct simhot	  hot; /* current results */
	struct simwarm	  warm; /* current results */
	struct simcold	  cold; /* graphed results */
};

/*
 * Each thread of a simulation consists of the simulation and the rank
 * of the thread in its threadgroup.
 */
struct	simthr {
	struct sim	 *sim;
	GThread		 *thread;
	size_t		  rank;
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
	b->wins.viewpolymin = GTK_CHECK_MENU_ITEM
		(gtk_builder_get_object(builder, "menuitem9"));
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
	gtk_adjustment_set_upper
		(b->wins.nthreads, g_get_num_processors());
	w = gtk_builder_get_object(builder, "label12");
	(void)g_snprintf(buf, sizeof(buf), 
		"%d", g_get_num_processors());
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
	g_free(p->warm.runs);
	g_free(p->cold.means);
	g_free(p->cold.variances);
	g_free(p->cold.fitmins);
	g_free(p->cold.coeffs);
	g_free(p->cold.runs);
	g_free(p->pops);
	g_free(p->threads);
	g_free(p);
}

/*
 * Set a given simulation to stop running.
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
	g_timer_destroy(p->status_elapsed);
	p->status_elapsed = NULL;
}

/*
 * Copy the "hot" data into "warm" holding.
 * While here, set our "work" parameter (if "fitpoly" is set) to contain
 * the necessary dependent variable.
 */
static void
on_sim_snapshot(struct simwork *work, struct sim *sim)
{
	size_t	 i;

	g_mutex_lock(&sim->warm.mux);
	memcpy(sim->warm.means, 
		sim->hot.means,
		sizeof(double) * sim->dims);
	memcpy(sim->warm.runs, 
		sim->hot.runs,
		sizeof(size_t) * sim->dims);
	for (i = 0; i < sim->dims; i++)
		if (sim->hot.runs[i] > 1)
			sim->warm.variances[i] = 
				sim->hot.meandiff[i] /
				(double)(sim->hot.runs[i] - 1);
	if (sim->fitpoly)
		for (i = 0; i < sim->dims; i++)
			gsl_vector_set(work->y, i, 
				sim->warm.means[i]);
	if (sim->weighted)
		for (i = 0; i < sim->dims; i++) 
			gsl_vector_set(work->w, i, 
				sim->warm.variances[i]);
	sim->warm.truns = sim->hot.truns;
	g_mutex_unlock(&sim->warm.mux);
}

/*
 * For a given point "x" in the domain, fit ourselves to the polynomial
 * coefficients of degree "fitpoly + 1".
 */
static double
fitpoly(const double *fits, size_t poly, double x)
{
	double	 y, v;
	size_t	 i, j;

	for (y = 0.0, i = 0; i < poly; i++) {
		v = fits[i];
		for (j = 0; j < i; j++)
			v *= x;
		y += v;
	}
	return(y);
}

/*
 * In a given simulation, compute the next mutant/incumbent pair.
 * We make sure that incumbents are striped evenly in any given
 * simulation but that mutants are randomly selected from within the
 * strategy domain.
 */
static int
on_sim_next(struct simwork *work, struct sim *sim, 
	const gsl_rng *rng, double *mutant, 
	double *incumbent, size_t *incumbentidx)
{
	int		 fit;
	size_t		 i, j, k;
	double		 v, chisq, x, min;

	if (sim->terminate)
		return(0);

	fit = 0;
	g_mutex_lock(&sim->hot.mux);
	sim->hot.truns++;
	if (1 == sim->hot.copyout) {
		/*
		 * If this happens, we've been instructed by the main
		 * thread of execution to snapshot hot data into warm
		 * storage.
		 * To do this synchronously, all threads will wait to
		 * finish their work.
		 * The last thread will then do the actual snapshot and
		 * broadcast to the wait condition.
		 */
		if (++sim->hot.copyblock == sim->nprocs) {
			fit = 1;
			sim->hot.copyout = 2;
			sim->hot.copyblock = 0;
			on_sim_snapshot(work, sim);
			g_cond_broadcast(&sim->hot.cond);
		} else
			g_cond_wait(&sim->hot.cond, &sim->hot.mux);
	}
	g_mutex_unlock(&sim->hot.mux);

	/* Increment over a ring. */
	*incumbentidx = (*incumbentidx + sim->nprocs) % sim->dims;
	*mutant = sim->d.continuum.xmin + 
		(sim->d.continuum.xmax - 
		 sim->d.continuum.xmin) * gsl_rng_uniform(rng);
	*incumbent = sim->d.continuum.xmin + 
		(sim->d.continuum.xmax - 
		 sim->d.continuum.xmin) * 
		(*incumbentidx / (double)sim->dims);

	/* If we weren't the last thread blocking, return now. */
	if ( ! fit)
		return(1);

	/*
	 * If we're not fitting to a polynomial, simply notify that
	 * we've copied out and continue on our way.
	 */
	if (0 == sim->fitpoly) {
		g_mutex_lock(&sim->warm.mux);
		sim->hot.copyout = 0;
		g_mutex_unlock(&sim->warm.mux);
		return(1);
	}

	/* 
	 * If we're fitting to a polynomial, initialise our
	 * polynomial structures here.
	 */
	for (i = 0; i < sim->dims; i++) {
		gsl_matrix_set(work->X, i, 0, 1.0);
		for (j = 0; j < sim->fitpoly; j++) {
			v = sim->d.continuum.xmin +
				(sim->d.continuum.xmax -
				 sim->d.continuum.xmin) *
				(i / (double)sim->dims);
			for (k = 0; k < j; k++)
				v *= v;
			gsl_matrix_set(work->X, i, j + 1, v);
		}
	}

	/*
	 * Now perform the actual fitting.
	 * We use the linear non-weighted version for now.
	 */
	if (sim->weighted) 
		gsl_multifit_wlinear(work->X, work->w, work->y, 
			work->c, work->cov, &chisq, work->work);
	else
		gsl_multifit_linear(work->X, work->y, 
			work->c, work->cov, &chisq, work->work);

	/* Lastly, snapshot and notify the main thread. */
	g_mutex_lock(&sim->warm.mux);
	for (i = 0; i < sim->fitpoly + 1; i++)
		sim->warm.coeffs[i] = gsl_vector_get(work->c, i);
	sim->hot.copyout = 0;
	min = FLT_MAX;
	for (i = 0; i < sim->dims; i++) {
		x = sim->d.continuum.xmin + 
			(sim->d.continuum.xmax -
			 sim->d.continuum.xmin) *
			i / (double)sim->dims;
		v = fitpoly(sim->warm.coeffs, sim->fitpoly + 1, x);
		if (v < min) {
			sim->warm.fitmin = i;
			min = v;
		}
	}
	g_mutex_unlock(&sim->warm.mux);
	return(1);
}

/*
 * For a given island (size "pop") player's strategy "x" where mutants
 * (numbering "mutants") have strategy "mutant" and incumbents
 * (numbering "incumbents") have strategy "incumbent", compute the
 * a(1 + delta(pi(x, X)) function.
 */
static double
continuum_lambda(const struct sim *sim, double x, 
	double mutant, double incumbent, size_t mutants, size_t pop)
{
	double	 v;

	v = hnode_exec
		((const struct hnode *const *)
		 sim->d.continuum.exp, x,
		 (mutants * mutant) + ((pop - mutants) * incumbent),
		 pop);
	assert( ! (isnan(v) || isinf(v)));
	return(sim->alpha * (1.0 + sim->delta * v));
}

/*
 * Run a simulation.
 * This can be one thread of many within the same simulation.
 */
static void *
on_sim(void *arg)
{
	struct simthr	*thr = arg;
	struct sim	*sim = thr->sim;
	double		 mutant, incumbent, v, lambda, mold;
	size_t		*kids[2], *migrants[2], *imutants;
	size_t		 t, j, k, new, mutants, incumbents,
			 len1, len2, incumbentidx;
	int		 mutant_old, mutant_new;
	struct simwork	 work;
	gsl_rng		*rng;

	rng = gsl_rng_alloc(gsl_rng_default);
	g_debug("Thread %p (simulation %p) using RNG %s", 
		g_thread_self(), sim, gsl_rng_name(rng));

	/* 
	 * Conditionally allocate polynomial fitting.
	 * There's no need for all of this extra memory allocated if
	 * we're not going to use it!
	 */
	memset(&work, 0, sizeof(struct simwork));

	if (sim->fitpoly) {
		work.X = gsl_matrix_alloc(sim->dims, sim->fitpoly + 1);
		work.y = gsl_vector_alloc(sim->dims);
		work.w = gsl_vector_alloc(sim->dims);
		work.c = gsl_vector_alloc(sim->fitpoly + 1);
		work.cov = gsl_matrix_alloc
			(sim->fitpoly + 1, sim->fitpoly + 1);
		work.work = gsl_multifit_linear_alloc
			(sim->dims, sim->fitpoly + 1);
	}

	kids[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	kids[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	imutants = g_malloc0_n(sim->islands, sizeof(size_t));
	incumbentidx = thr->rank;

again:
	/* Repeat til we're instructed to terminate. */
	if ( ! on_sim_next(&work, sim, rng, 
		&mutant, &incumbent, &incumbentidx)) {
		/*
		 * Upon termination, free up all of the memory
		 * associated with our simulation.
		 */
		g_free(imutants);
		g_free(kids[0]);
		g_free(kids[1]);
		g_free(migrants[0]);
		g_free(migrants[1]);
		if (sim->fitpoly) {
			gsl_matrix_free(work.X);
			gsl_vector_free(work.y);
			gsl_vector_free(work.w);
			gsl_vector_free(work.c);
			gsl_matrix_free(work.cov);
			gsl_multifit_linear_free(work.work);
		}
		g_debug("Thread %p (simulation %p) exiting", 
			g_thread_self(), sim);
		return(NULL);
	}

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
			lambda = continuum_lambda
				(sim, mutant, mutant, 
				 incumbent, imutants[j], sim->pops[j]);
			for (k = 0; k < imutants[j]; k++) 
				kids[0][j] += gsl_ran_poisson
					(rng, lambda);
			lambda = continuum_lambda
				(sim, incumbent, mutant, 
				 incumbent, imutants[j], sim->pops[j]);
			for ( ; k < sim->pops[j]; k++)
				kids[1][j] += gsl_ran_poisson
					(rng, lambda);
		}

		/*
		 * Determine whether we're going to migrate and, if
		 * migration is stipulated, to where.
		 */
		for (j = 0; j < sim->islands; j++) {
			for (k = 0; k < kids[0][j]; k++) {
				new = j;
				if (gsl_rng_uniform(rng) < sim->m) do 
					new = gsl_rng_uniform_int
						(rng, sim->islands);
				while (new == j);
				migrants[0][new]++;
			}
			for (k = 0; k < kids[1][j]; k++) {
				new = j;
				if (gsl_rng_uniform(rng) < sim->m) do 
					new = gsl_rng_uniform_int
						(rng, sim->islands);
				while (new == j);
				migrants[1][new]++;
			}
			kids[0][j] = kids[1][j] = 0;
		}

		/*
		 * Perform the migration itself.
		 * We randomly select an individual on the destination
		 * island as well as one from the migrant queue.
		 * We then replace one with the other.
		 */
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

		/* Stop when a population goes extinct. */
		if (0 == mutants || 0 == incumbents) 
			break;
	}

	v = mutants / (double)sim->totalpop;
	mold = sim->hot.means[incumbentidx];
	sim->hot.runs[incumbentidx]++;
	sim->hot.means[incumbentidx] +=
		(v - sim->hot.means[incumbentidx]) /
		sim->hot.runs[incumbentidx];
	sim->hot.meandiff[incumbentidx] +=
		(v - mold) * (v - sim->hot.means[incumbentidx]);
	goto again;
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
		memcpy(sim->cold.variances, 
			sim->warm.variances,
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
	double		 elapsed;

	nprocs = runs = 0;
	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = (struct sim *)list->data;
		runs += sim->cold.truns * sim->stop;
		/*
		 * If "terminate" is set, then the thread is (or already
		 * did) exit, so wait for it.
		 * If we wait, it should take only a very small while.
		 */
		if (sim->terminate && sim->nprocs > 0) {
			for (i = 0; i < sim->nprocs; i++) 
				if (NULL != sim->threads[i].thread) {
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
		"(%g%% active)", 100 * (nprocs / 
			(double)g_get_num_processors()));
	gtk_label_set_text(b->wins.curthreads, buf);

	
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

	/* Middle-top right. */
	cairo_move_to(cr, width - e.width, height * 0.75);
	(void)snprintf(buf, sizeof(buf), 
		"%.2g", miny + (maxy + miny) * 0.25);
	cairo_show_text(cr, buf);

	/* Middle right. */
	cairo_move_to(cr, width - e.width, height * 0.5);
	(void)snprintf(buf, sizeof(buf), "%.2g", (maxy + miny) * 0.5);
	cairo_show_text(cr, buf);

	/* Middle-bottom right. */
	cairo_move_to(cr, width - e.width, height * 0.25);
	(void)snprintf(buf, sizeof(buf), 
		"%.2g", miny + (maxy + miny) * 0.75);
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

	*widthp -= e.width * 1.3;
	*heightp -= e.height * 3.0;
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
	double		 width, height, maxy, v, x, xmin, xmax;
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

	/*
	 * We can be attached to several simulations, so iterate over
	 * all of them when computing our maximum "y" point.
	 * Our minimum "y" point is always at zero: obviously, we'll
	 * never have a negative fraction of mutants.
	 */
	for (objs = 0; objs < 32; objs++) {
		(void)g_snprintf(buf, sizeof(buf), "sim%zu", objs);
		sim = g_object_get_data(G_OBJECT(top), buf);
		if (NULL == sim)
			break;
		for (j = 0; j < sim->dims; j++) {
			if (gtk_check_menu_item_get_active(b->wins.viewdev))
				v = sim->cold.means[j] + sim->cold.variances[j];
			else if (gtk_check_menu_item_get_active(b->wins.viewpolymin))
				v = sim->cold.fitmins[j] / (double)sim->cold.truns;
			else 
				v = sim->cold.means[j];
			if (v > maxy)
				maxy = v;
		}
		if (xmin > sim->d.continuum.xmin)
			xmin = sim->d.continuum.xmin;
		if (xmax < sim->d.continuum.xmax)
			xmax = sim->d.continuum.xmax;
	}
	assert(objs > 0 && objs < 32);

	maxy += maxy * 0.1;
	drawlabels(cr, &width, &height, 0.0, maxy, xmin, xmax);
	cairo_save(cr);

	/*
	 * Draw curves as specified: either the "raw" curve (just the
	 * data), the raw curve and its standard deviation above and
	 * below, or the raw curve plus the polynomial fitting.
	 */
	for (i = 0; i < objs; i++) {
		(void)g_snprintf(buf, sizeof(buf), "sim%zu", i);
		sim = g_object_get_data(G_OBJECT(top), buf);
		assert(NULL != sim);

		if (gtk_check_menu_item_get_active(b->wins.viewdev)) {
			/*
			 * If stipulated, draw the standard deviation
			 * above and below the curve.
			 * Obviously, don't go below zero.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				v = sim->cold.means[j];
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 1.0);
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				v -= sim->cold.variances[j - 1];
				if (v < 0.0)
					v = 0.0;
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				v = sim->cold.means[j];
				v -= sim->cold.variances[j];
				if (v < 0.0)
					v = 0.0;
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 0.5);
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				v += sim->cold.variances[j - 1];
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				v = sim->cold.means[j];
				v += sim->cold.variances[j];
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 0.5);
			cairo_stroke(cr);
		} else if (gtk_check_menu_item_get_active(b->wins.viewpoly)) {
			/*
			 * Compute and draw the curve stipulated by the
			 * coefficients our workers have computed.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				v = sim->cold.means[j];
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 0.5);
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				x = sim->d.continuum.xmin +
					(sim->d.continuum.xmax -
					 sim->d.continuum.xmin) *
					(j - 1) / (double)sim->dims;
				v = fitpoly(sim->cold.coeffs, sim->fitpoly + 1, x);
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				x = sim->d.continuum.xmin +
					(sim->d.continuum.xmax -
					 sim->d.continuum.xmin) *
					j / (double)sim->dims;
				v = fitpoly(sim->cold.coeffs, sim->fitpoly + 1, x);
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 1.0);
			cairo_stroke(cr);
		} else if (gtk_check_menu_item_get_active(b->wins.viewpolymin)) {
			for (j = 1; j < sim->dims; j++) {
				x = sim->d.continuum.xmin +
					(sim->d.continuum.xmax -
					 sim->d.continuum.xmin) *
					(j - 1) / (double)sim->dims;
				v = sim->cold.fitmins[j - 1] / (double)sim->cold.truns;
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				x = sim->d.continuum.xmin +
					(sim->d.continuum.xmax -
					 sim->d.continuum.xmin) *
					j / (double)sim->dims;
				v = sim->cold.fitmins[j] / (double)sim->cold.truns;
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 1.0);
			cairo_stroke(cr);
		} else {
			/* 
			 * Draw just the raw curve.
			 * This is the default.
			 */
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.means[j - 1];
				cairo_move_to(cr, 
					width * (j - 1) / (double)(sim->dims - 1),
					height - (v / maxy * height));
				v = sim->cold.means[j];
				cairo_line_to(cr, 
					width * j / (double)(sim->dims - 1),
					height - (v / maxy * height));
			}
			cairo_set_source_rgba
				(cr, b->wins.colours[sim->colour].red,
				 b->wins.colours[sim->colour].green,
				 b->wins.colours[sim->colour].blue, 1.0);
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
 * Update all of our windows to this effect.
 */
void
onviewtoggle(GtkMenuItem *menuitem, gpointer dat)
{
	GList	*list;

	list = gtk_window_list_toplevels();
	for ( ; list != NULL; list = list->next)
		gtk_widget_queue_draw(GTK_WIDGET(list->data));
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
	sim->warm.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.means = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.variances = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.fitmins = g_malloc0_n
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
			(NULL, on_sim, &sim->threads[i]);
	}

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
