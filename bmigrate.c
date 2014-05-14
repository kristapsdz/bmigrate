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

#define ARC4RANDOM_INTERVAL \
	(arc4random() / (double)UINT32_MAX)

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
	GtkLabel	 *curthreads;
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

struct	sim {
	GThread		 *thread; /* thread of execution */
	size_t		  nprocs; /* processors reserved */
	size_t		  totalpop; /* total population */
	size_t		 *pops; /* per-island population */
	size_t		  islands; /* island population */
	size_t		  stop; /* when to stop */
	enum payoff	  type; /* type of game */
	union {
		struct sim_continuum2 continuum2;
	} d;
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
	switch (p->type) {
	case (PAYOFF_CONTINUUM2):
		hnode_free(p->d.continuum2.exp);
		break;
	default:
		break;
	}
	if (NULL != p->thread)
		g_thread_join(p->thread);
	g_free(p->pops);
	g_free(p);
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


static void *
on_sim_new(void *arg)
{
	struct sim	*sim = arg;
	double		 mutant, incumbent, mult, delta, m;
	double		 payoffs[4];
	size_t		*kids[2], *migrants[2], *imutants;
	size_t		 t, j, k, new, mutants, incumbents,
			 len1, len2;
	int		 mutant_old, mutant_new;
	gsl_rng		*rng;

	rng = gsl_rng_alloc(gsl_rng_default);

	mult = 20.0;
	delta = 0.1;
	m = 0.5;

	mutant = sim->d.continuum2.xmin + 
		(sim->d.continuum2.xmax - sim->d.continuum2.xmin) * 
		ARC4RANDOM_INTERVAL;
	incumbent = sim->d.continuum2.xmin + 
		(sim->d.continuum2.xmax - sim->d.continuum2.xmin) * 
		ARC4RANDOM_INTERVAL;

	payoffs[0] = mult * (1.0 + delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			mutant, mutant + mutant, 2));
	payoffs[1] = mult * (1.0 + delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			mutant, mutant + incumbent, 2));
	payoffs[2] = mult * (1.0 + delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			incumbent, mutant + incumbent, 2));
	payoffs[3] = mult * (1.0 + delta * 
		hnode_exec((const struct hnode *const *)
			sim->d.continuum2.exp, 
			incumbent, incumbent + incumbent, 2));

	kids[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	kids[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	imutants = g_malloc0_n(sim->islands, sizeof(size_t));

	/* 
	 * Initialise a random island to have one mutant. 
	 * The rest are all incumbents.
	 */
	imutants[arc4random_uniform(sim->islands)] = 1;
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
				if (ARC4RANDOM_INTERVAL < m) do 
					new = arc4random_uniform
						(sim->islands);
				while (new == j);
				migrants[0][new]++;
			}
			for (k = 0; k < kids[1][j]; k++) {
				new = j;
				if (ARC4RANDOM_INTERVAL < m) do
					new = arc4random_uniform
						(sim->islands);
				while (new == j);
				migrants[1][new]++;
			}
			kids[0][j] = kids[1][j] = 0;
		}

		for (j = 0; j < sim->islands; j++) {
			len1 = migrants[0][j] + migrants[1][j];
			if (0 == len1)
				continue;

			len2 = arc4random_uniform(sim->pops[j]);
			mutant_old = len2 < imutants[j];
			len2 = arc4random_uniform(len1);
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

	g_free(imutants);
	g_free(kids[0]);
	g_free(kids[1]);
	g_free(migrants[0]);
	g_free(migrants[1]);
	sim->nprocs = 0;
	return(NULL);
}

static gboolean
on_timer(gpointer dat)
{
	struct bmigrate	*b = dat;
	GList		*list;
	size_t		 nprocs;
	struct sim	*sim;
	gchar		 buf[1024];

	nprocs = 0;
	for (list = b->sims; NULL != list; list = g_list_next(list)) {
		sim = (struct sim *)list->data;
		if (0 == sim->nprocs && NULL != sim->thread) {
			g_thread_join(sim->thread);
			sim->thread = NULL;
		}
		nprocs += sim->nprocs;
	}

	(void)g_snprintf(buf, sizeof(buf),
		"(%g%% active)", 100 * (nprocs / 
			(double)g_get_num_processors()));
	gtk_label_set_text(b->wins.curthreads, buf);
	gtk_widget_queue_draw(GTK_WIDGET(b->wins.curthreads));
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
	gchar		 *ep;
	gdouble		  xmin, xmax;
	size_t		  i, totalpop, islandpop, islands, stop;
	struct sim	 *sim;

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
			 "Error: bad total population.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	} else if ( ! entry2size(b->wins.stop, &stop)) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: bad stopping time.");
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

	xmin = g_ascii_strtod(gtk_entry_get_text(b->wins.xmin), &ep);
	if (ERANGE == errno || '\0' != *ep) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: minimum strategy out of range.");
		gtk_widget_show_all(GTK_WIDGET(b->wins.error));
		return;
	}
	xmax = g_ascii_strtod(gtk_entry_get_text(b->wins.xmax), &ep);
	if (ERANGE == errno || '\0' != *ep) {
		gtk_label_set_text
			(b->wins.error,
			 "Error: maximum strategy out of range.");
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

	sim = g_malloc0(sizeof(struct sim));
	sim->type = PAYOFF_CONTINUUM2;
	sim->nprocs = gtk_adjustment_get_value(b->wins.nthreads);
	sim->totalpop = totalpop;
	sim->islands = islands;
	sim->stop = stop;
	sim->pops = g_malloc0_n(sim->islands, sizeof(size_t));
	for (i = 0; i < sim->islands; i++)
		sim->pops[i] = islandpop;
	sim->d.continuum2.exp = exp;
	sim->d.continuum2.xmin = xmin;
	sim->d.continuum2.xmax = xmax;
	b->sims = g_list_append(b->sims, sim);
	sim->thread = g_thread_new(NULL, on_sim_new, sim);
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
on_change_payoff(GtkNotebook *notebook, GtkWidget *page, gint pnum, gpointer dat)
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
on_change_input(GtkNotebook *notebook, GtkWidget *page, gint pnum, gpointer dat)
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
