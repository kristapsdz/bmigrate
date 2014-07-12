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
#ifndef EXTERN_H
#define EXTERN_H

/*
 * Maximum number of elements in expression parse stack.
 * It's really, really unlikely we'll hit this, and because a fixed
 * stack reduces complexity, let's just use it.
 */
#define STACKSZ 128

enum 	htype {
	HNODE_P1, /* player strategy */
	HNODE_PN, /* sum of players' strategies */
	HNODE_N, /* n players */
	HNODE_NUMBER, /* a real number */
	HNODE_SQRT, /* sqrt() */
	HNODE_EXPF, /* exp() */
	HNODE_ADD, /* basic algebra... */
	HNODE_SUB,
	HNODE_MUL,
	HNODE_DIV,
	HNODE_EXP,
	HNODE_POSITIVE, /* unary operations... */
	HNODE_NEGATIVE,
	HNODE__MAX
};

/*
 * A node in the postfix-ordered expression list.
 */
struct 	hnode {
	enum htype	  type; /* type of operation */
	double		  real; /* HNODE_NUMBER, if applicable */
};

/*
 * Statistics collection.
 */
struct	stats {
	uint64_t	n;
	uint64_t	extm;
	uint64_t	exti;
	double		M1;
	double		M2;
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
	double		  ymin; /* minimum Gaussian mutant strategy */
	double		  ymax; /* maximum Gaussian mutant strategy */
};

/*
 * This structure is maintained by a thread group for a particular
 * running simulation.
 * All reads/writes must lock the mutex before modifications.
 * This data is periodically snapshotted to "struct simwarm", which is
 * triggered by the first thread encountering copyout == 1.
 */
struct	simhot {
	GMutex		 mux; /* lock for changing data */
	GCond		 cond; /* mutex for waiting on snapshot */
	uint64_t	 truns; /* total number of runs */
	uint64_t	 tgens; /* total number of generations */
	struct stats	*stats; /* statistics per incumbent */
	struct stats	*statslsb; /* lookaside for stats */
	struct stats	*islands; /* statistics per island */
	struct stats	*islandslsb; /* lookaside for islands */
	int		 copyout; /* do we need to snapshot? */
	int		 pause; /* should we pause? */
	size_t		 copyblock; /* threads blocking on copy */
	size_t		 incumbent; /* current incumbent index */
	size_t		 mutant; /* current mutant index */
	size_t		 island; /* current island index */
};

/*
 * A simulation thread group will snapshot its "struct simhot" structure
 * to this when its "copyout" is set to 1.
 * The fields are the same but with the addition "coeffs", "fits", and
 * "fitmin" are set (if applicable) to the fitted polynomial.
 */
struct	simwarm {
	size_t		 meanmin; /* min sample mean */
	size_t		 smeanmin; /* min smoothed sample mean */
	size_t	 	 sextmmax; /* max smoothed mutant extinct */
	size_t		 fitmin; /* index of min fitpoly point */
	size_t		 extmmax; /* index of max mutant extinction */
	size_t		 extimin; /* index of min incumb extinction */
	double		*smeans; /* smoothed mean */
	double		*sextms; /* smoothed mutant extinctions */
	double	   	*coeffs; /* fitpoly coefficients */
	double	   	*fits; /* fitpoly points */
	struct stats	*stats; /* statistics per incumbent */
	struct stats	*islands; /* statistics per island */
	uint64_t	 truns; /* total number of runs */
	uint64_t	 tgens; /* total number of generations */
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
	gsl_multifit_linear_workspace *work; /* workspace */
};

struct	hstats {
	double		 mode;
	double		 mean;
	double		 stddev;
};

#define	CQUEUESZ	 256

struct	cqueue {
	size_t		 pos; /* current queue position */
	size_t		 vals[CQUEUESZ];
	size_t		 maxpos; /* position of maximum */
};

/*
 * Instead of operating on the simulation results themselves, we copy
 * output from "struct simwarm" into a "cold" buffer for viewing.
 * See "struct simwarm" for matching fields. 
 * This is done in the main thread of execution, so it is not locked.
 */
struct	simcold {
	struct stats	*stats; /* statistics per incumbent */
	struct stats	*islands; /* statistics per island */
	double	   	*smeans; /* smoothed mean */
	double	   	*sextms; /* smoothed mutant extinctions */
	double	   	*coeffs; /* fitpoly coefficients */
	double	   	*fits; /* fitpoly points */
	gsl_histogram	*fitmins; /* fitted minimum dist */
	gsl_histogram	*smeanmins; /* smoothed mean minimum dist */
	gsl_histogram	*meanmins; /* mean minimum dist */
	gsl_histogram	*extmmaxs; /* mutant extinction dist */
	gsl_histogram	*extimins; /* incumbent extinction dist */
	size_t		 extmmax; /* current mutant extinct max */
	size_t		 extimin; /* current incumbent extinct min */
	size_t		 fitmin; /* current fitpoly minimum */
	size_t		 meanmin; /* current sample mean min */
	size_t		 smeanmin; /* min smoothed sample mean */
	size_t	 	 sextmmax; /* max smoothed mutant extinct */
	struct hstats	 fitminst; /* fitmins statistics */
	struct hstats	 meanminst; /* meanmins statistics */
	struct hstats	 extmmaxst; /* extmmaxs statistics */
	struct hstats	 extiminst; /* extimins statistics */
	struct hstats	 smeanminst; /* smeanmins statistics */
	uint64_t	 truns; /* total runs */
	uint64_t	 tgens; /* total generations */
	struct cqueue	 meanminq; /* circleq of raw minima */
	struct cqueue	 fitminq; /* circleq of poly minima */
	struct cqueue	 smeanminq; /* circleq of smoothed minima */
};

/*
 * Pages in the configuration notebook.
 * These correspond to the way that a simulation is going to be
 * prepared, i.e., whether island populations are going to be evenly
 * distributed from a given population size or manually set, etc.
 */
enum	input {
	INPUT_UNIFORM = 0,
	INPUT_VARIABLE,
	INPUT_MAPPED,
	INPUT__MAX
};

enum	mutants {
	MUTANTS_DISCRETE = 0,
	MUTANTS_GAUSSIAN,
	MUTANTS__MAX
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
	enum mutants	  mutants; /* mutant assignation */
	enum input	  input; /* input structure type */
	double		  mutantsigma; /* mutant gaussian sigma */
	size_t		  stop; /* when to stop */
	gchar		 *name; /* name of simulation */
	gchar		 *func; /* payoff function */
	double		  alpha; /* outer multiplier */
	double		  delta; /* inner multiplier */
	double		  m; /* migration probability */
	size_t		  colour; /* graph colour */
	struct sim_continuum continuum;
	struct simhot	  hot; /* current results */
	struct simwarm	  warm; /* current results */
	struct simcold	  cold; /* graphed results */
	struct simwork	  work; /* worker data */
};

/*
 * Given a current simulation "_s", compute where a given index "_v"
 * (out if the simulation's dimensions) lies within the domain.
 */
#define	GETS(_s, _v) \
	((_s)->continuum.xmin + \
	 ((_s)->continuum.xmax - (_s)->continuum.xmin) * \
	 (_v) / (double)((_s)->dims))

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
 * Different views of simulation data.
 */
enum	view {
	VIEW_CONFIG,
	VIEW_DEV, 
	VIEW_EXTI,
	VIEW_EXTIMINCDF,
	VIEW_EXTIMINPDF,
	VIEW_EXTIMINS,
	VIEW_EXTM,
	VIEW_EXTMMAXCDF,
	VIEW_EXTMMAXPDF,
	VIEW_EXTMMAXS,
	VIEW_ISLANDMEAN,
	VIEW_MEAN,
	VIEW_MEANMINCDF,
	VIEW_MEANMINPDF,
	VIEW_MEANMINQ,
	VIEW_MEANMINS,
	VIEW_POLY,
	VIEW_POLYMINCDF,
	VIEW_POLYMINPDF,
	VIEW_POLYMINQ,
	VIEW_POLYMINS,
	VIEW_SEXTM,
	VIEW_SMEAN,
	VIEW_SMEANMINCDF,
	VIEW_SMEANMINPDF,
	VIEW_SMEANMINQ,
	VIEW_SMEANMINS,
	VIEW__MAX
};

/*
 * These are all widgets that may be or are visible.
 */
struct	hwin {
	GtkWindow	 *config;
#ifndef	MAC_INTEGRATION
	GtkMenu		 *allmenus;
#endif
	GtkMenuBar	 *menu;
	GtkMenuItem	 *menuquit;
	GtkMenuItem	 *menuclose;
	GtkMenuItem	 *menusave;
	GtkMenuItem	 *menufile;
	GtkMenuItem	 *menuview;
	GtkMenuItem	 *menutools;
	GtkStatusbar	 *status;
	GtkCheckMenuItem *views[VIEW__MAX];
	GtkEntry	 *mutantsigma;
	GtkRadioButton   *mutants[MUTANTS__MAX];
	GtkMenuItem	 *viewclone;
	GtkMenuItem	 *viewpause;
	GtkMenuItem	 *viewunpause;
	GtkToggleButton	 *weighted;
	GtkEntry	 *stop;
	GtkEntry	 *input;
	GtkBox		 *mapbox;
	GtkEntry	 *name;
	GtkEntry	 *xmin;
	GtkEntry	 *xmax;
	GtkEntry	 *ymin;
	GtkEntry	 *ymax;
	GtkNotebook	 *inputs;
	GtkLabel	 *error;
	GtkEntry	 *func;
	GtkAdjustment	 *nthreads;
	GtkAdjustment	 *fitpoly;
	GtkAdjustment	 *pop;
	GtkAdjustment	 *islands;
	GtkEntry	 *totalpop;
	GtkEntry	 *alpha;
	GtkEntry	 *delta;
	GtkEntry	 *migrate[INPUT__MAX];
	GtkEntry	 *incumbents;
	GtkLabel	 *curthreads;
	GtkToggleButton	 *analsingle;
	GtkToggleButton	 *analmultiple;
#define	SIZE_COLOURS	  9 
	GdkRGBA		  colours[SIZE_COLOURS];
	GList		 *menus;
};

/*
 * This describes a window.
 * There's very little in here right now, which is fine.
 */
struct	curwin {
	enum view	  view; /* what view are we seeing? */
	int		  redraw; /* window is stale? */
};

/*
 * Main structure governing general state of the system.
 */
struct	bmigrate {
	struct hwin	  wins; /* GUI components */
	size_t		  nextcolour; /* next colour to assign */
	GList		 *sims; /* active simulations */
	GTimer		 *status_elapsed; /* elapsed since update */
	uint64_t	  lastmatches; /* last seen no. matches */
	GtkWidget	 *current; /* the current window or NULL */
	size_t		  nprocs; /* total number processors */
};

__BEGIN_DECLS

struct hnode	**hnode_parse(const char **v);
void		  hnode_free(struct hnode **p);
struct hnode	**hnode_copy(struct hnode **p);
double		  hnode_exec(const struct hnode *const *p,
			double x, double X, size_t n);
void		  hnode_test(void);

void		  draw(GtkWidget *w, cairo_t *cr,
			struct bmigrate *b);
void		  save(FILE *f, struct bmigrate *b);
void		 *simulation(void *arg);

struct stats	 *stats_alloc0(size_t sz);
void		  stats_push(struct stats *p, double x);
double		  stats_mean(const struct stats *p);
double		  stats_variance(const struct stats *p);
double		  stats_stddev(const struct stats *p);
double		  stats_extinctm(const struct stats *p);
double		  stats_extincti(const struct stats *p);

void		  kml_parse(const gchar *file);

__END_DECLS

#endif
