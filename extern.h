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

struct	cqueue {
	size_t		 pos; /* current queue position */
#define	CQUEUESZ	 256
	double		 vals[CQUEUESZ];
	size_t		 maxpos; /* position of maximum */
};

struct	simbuf {
	struct kdata	*hot;
	struct kdata	*hotlsb;
	struct kdata	*warm;
	struct kdata	*cold;
};

struct	simbufs {
	/* Mutant fraction per incumbent. */
	struct kdata	*fractions;
	/* Mutant extinction per incumbent. */
	struct kdata	*mutants;
	/* Incumbent extinction per incumbent. */
	struct kdata	*incumbents;
	/* PMF of "fractions" minimum. */
	struct kdata	*meanmins;
	/* PMF of "mutants" maximum. */
	struct kdata	*mextinctmaxs;
	/* PMF of "incumbents" minimum. */
	struct kdata	*iextinctmins;
	/* "Hot" poly-smoothed mutant fraction per incumbent. */
	struct kdata	*fitpoly;
	/* "Cold" poly-smoothed mutant fraction per incumbent. */
	struct kdata	*fitpolybuf;
	/* PMF of "fitpolybuf" minimum. */
	struct kdata	*fitpolymins;
	/* Serialisation of "meanminq". */
	struct kdata	*meanminqbuf;
	/* Serialisation of "fitminq". */
	struct kdata	*fitminqbuf;
	/* Mutant fraction when starting on an island. */
	struct kdata	*ifractions;
	/* Mutant fraction per island (all incumbents). */
	struct kdata	*islands;

	struct simbuf	*times;
	struct simbuf	*imeans;
	struct simbuf	*istddevs;
	struct simbuf	*islandmeans;
	struct simbuf	*islandstddevs;
	struct simbuf	*means;
	struct simbuf	*stddevs;
	struct simbuf	*mextinct;
	struct simbuf	*iextinct;
	struct cqueue	 meanminq; 
	struct cqueue	 fitminq; 
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
	double		*coeffs;
	gsl_matrix	*X; /* matrix of independent variables */
	gsl_matrix	*cov; /* covariance matrix */
	gsl_vector	*y; /* vector dependent variables */
	gsl_vector	*c; /* output vector of coefficients */
	gsl_vector	*w; /* vector of weights */
	gsl_multifit_linear_workspace *work; /* workspace */
};

/*
 * Instead of operating on the simulation results themselves, we copy
 * output from "struct simwarm" into a "cold" buffer for viewing.
 * See "struct simwarm" for matching fields. 
 * This is done in the main thread of execution, so it is not locked.
 */
struct	simcold {
	uint64_t	 truns; /* total runs */
	uint64_t	 tgens; /* total generations */
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

enum	maptop {
	MAPTOP_RECORD,
	MAPTOP_RAND,
	MAPTOP_TORUS,
	MAPTOP__MAX
};

enum	mapmigrant {
	MAPMIGRANT_UNIFORM = 0,
	MAPMIGRANT_DISTANCE,
	MAPMIGRANT_NEAREST,
	MAPMIGRANT_TWONEAREST,
	MAPMIGRANT__MAX
};

enum	mapindex {
	MAPINDEX_STRIPED = 0,
	MAPINDEX_FIXED,
	MAPINDEX__MAX
};

enum	mutants {
	MUTANTS_DISCRETE = 0,
	MUTANTS_GAUSSIAN,
	MUTANTS__MAX
};

struct	simthr;
struct	kml;

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
	size_t		  pop; /* island population */
	size_t		 *pops; /* non-uniform island population */
	size_t		  islands; /* island population */
	size_t		  refs; /* GUI references */
	int		  terminate; /* terminate the process */
	enum mutants	  mutants; /* mutant assignation */
	enum input	  input; /* input structure type */
	double		  mutantsigma; /* mutant gaussian sigma */
	size_t		  stop; /* when to stop */
	size_t		  smoothing;
	gchar		 *name; /* name of simulation */
	gchar		 *func; /* payoff function */
	double		  alpha; /* outer multiplier */
	double		  delta; /* inner multiplier */
	double		  m; /* migration probability */
	double		**ms; /* nonuniform migration probability */
	struct kml	 *kml; /* KML places */
	enum mapmigrant	  migrant;
	enum mapindex	  mapindex;
	size_t		  mapindexfix;
	enum maptop	  maptop;
	size_t		  colour; /* graph colour */
	struct hnode	**exp; /* n-player function */
	double		  xmin; /* minimum strategy */
	double		  xmax; /* maximum strategy */
	double		  ymin; /* minimum Gaussian mutant strategy */
	double		  ymax; /* maximum Gaussian mutant strategy */
	struct simbufs	  bufs; /* kdata buffers */
	struct simhot	  hot; /* current results */
	struct simwarm	  warm; /* current results */
	struct simcold	  cold; /* graphed results */
	struct simwork	  work; /* worker data */
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
 * Different views of simulation data.
 */
enum	view {
	VIEW_TIMESCDF,
	VIEW_TIMESPDF,
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
	VIEW_ISLANDERMEAN,
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
	VIEW_SEXTI,
	VIEW_SMEAN,
	VIEW__MAX
};

/*
 * These govern how we auto-fill the name of the next/current
 * simulation during configuration.
 */
enum	namefill {
	NAMEFILL_DATE,
	NAMEFILL_M,
	NAMEFILL_T,
	NAMEFILL_MUTANTS,
	NAMEFILL_NONE,
	NAMEFILL__MAX
};

struct	swin {
	GtkWindow	 *window;
	GtkDrawingArea	 *draw;
	GtkBox		 *boxconfig;
	GtkMenuBar	 *menu;
	GtkMenuItem	 *menuquit;
	GtkMenuItem	 *menuautoexport;
	GtkMenuItem	 *menuunautoexport;
	GtkMenuItem	 *menuclose;
	GtkMenuItem	 *menusave;
	GtkMenuItem	 *menusavekml;
	GtkMenuItem	 *menusaveall;
	GtkMenuItem	 *menufile;
	GtkMenuItem	 *menuview;
	GtkMenuItem	 *menutools;
	GtkMenuItem	 *viewclone;
	GtkMenuItem	 *viewpause;
	GtkMenuItem	 *viewunpause;
	GtkCheckMenuItem *views[VIEW__MAX];
};

/*
 * These are all widgets that may be or are visible.
 */
struct	hwin {
	GtkWindow	 *config;
	GtkWindow	 *rangefind;
	GtkMenuBar	 *menu;
	GtkMenuItem	 *menuquit;
	GtkStatusbar	 *status;
	GtkEntry	 *mutantsigma;
	GtkRadioButton   *mutants[MUTANTS__MAX];
	GtkToggleButton	 *namefill[NAMEFILL__MAX];
	GtkToggleButton	 *mapmigrants[MAPMIGRANT__MAX];
	GtkToggleButton	 *weighted;
	GtkToggleButton	 *mapindices[MAPINDEX__MAX];
	GtkAdjustment	 *mapindexfix;
	GtkEntry	 *stop;
	GtkLabel	 *input;
	GtkButton	 *buttonrange;
	GtkLabel	 *rangemax;
	GtkLabel	 *rangemin;
	GtkLabel	 *rangemean;
	GtkLabel	 *rangemaxlambda;
	GtkLabel	 *rangeminlambda;
	GtkLabel	 *rangemeanlambda;
	GtkLabel	 *rangestatus;
	GtkBox		 *rangeerrorbox;
	GtkLabel	 *rangeerror;
	GtkLabel	 *rangefunc;
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
	GtkToggleButton	 *maptop[MAPTOP__MAX];
	GtkAdjustment	 *maprandislands;
	GtkAdjustment	 *maprandislanders;
	GtkAdjustment	 *maptorusislands;
	GtkAdjustment	 *maptorusislanders;
	GtkAdjustment	 *smoothing;
	GtkLabel	 *totalpop;
	GtkEntry	 *alpha;
	GtkEntry	 *delta;
	GtkEntry	 *migrate[INPUT__MAX];
	GtkEntry	 *incumbents;
	GtkLabel	 *onprocs;
	GtkLabel	 *resprocs;
	GtkFileChooser	 *mapfile;
#define	SIZE_COLOURS	  9 
	GdkRGBA		  colours[SIZE_COLOURS];
};

/*
 * This describes a window.
 * Windows manage one or more simulations that might be in multiple
 * other windows, too.
 */
struct	curwin {
	struct swin	  wins; /* windows in view */
	enum view	  view; /* what view are we seeing? */
	struct kdata	 *winmean;
	struct kdata	 *winstddev;
	struct kdata	 *winfitmean;
	struct kdata	 *winfitstddev;
	struct kdata	 *winmextinctmean;
	struct kdata	 *winmextinctstddev;
	struct kdata	 *winiextinctmean;
	struct kdata	 *winiextinctstddev;
	struct kplot	 *views[VIEW__MAX];
	int		  redraw; /* window is stale? */
	GList		 *sims; /* simulations in window */
	gchar		 *autosave; /* directory or NULL */
	struct bmigrate	 *b; /* up-reference */
};

/*
 * Data used to range-find the Pi function.
 * (I.e., we want to see the maxima and minima.)
 */
struct	range {
	struct hnode	**exp;
	double		  alpha; /* norm outer multiplier */
	double		  delta; /* norm inner multiplier */
	double		  xmin; /* incumbent minimum */
	double		  xmax; /* incumbent maximum */
	double		  ymin; /* mutant minimum */
	double		  ymax; /* mutant maximum */
	size_t		  n; /* number of players */
	size_t		  slices; /* number of slices */
	size_t		  slicex; /* current x-slice */
	size_t		  slicey; /* current y-slice */
	double		  pimin; /* minimum result */
	double		  pimax; /* maximum result */
	double		  piaggr; /* aggregate result */
	size_t		  picount; /* aggregate count */
};

/*
 * Main structure governing general state of the system.
 */
struct	bmigrate {
	struct hwin	  wins; /* GUI components */
	size_t		  nextcolour; /* next colour to assign */
	GList		 *sims; /* active simulations */
	GList		 *windows; /* active curwin windows */
	GTimer		 *status_elapsed; /* elapsed since update */
	uint64_t	  lastmatches; /* last seen no. matches */
	GtkWidget	 *current; /* the current window or NULL */
	guint		  rangeid; /* range-finding process */
	struct range	  range; /* range-finding data */
	cairo_pattern_t	**clrs;
	size_t		  clrsz;
};

struct	kmlplace {
	size_t	 pop; /* population always > 1 */
	double	 lat; /* KML latitude */
	double	 lng; /* KML longitude */
};

struct	kml {
	GMappedFile	*file; /* if not NULL, input */
	GList		*kmls; /* mapped places */
};

__BEGIN_DECLS

struct hnode	**hnode_parse(const char **v);
void		  hnode_free(struct hnode **p);
struct hnode	**hnode_copy(struct hnode **p);
double		  hnode_exec(const struct hnode *const *p,
			double x, double X, size_t n);
void		  hnode_test(void);

void		  draw(GtkWidget *, cairo_t *, struct curwin *);
int		  save(const gchar *, const struct curwin *);
void		 *simulation(void *);

int		  rangefind(struct bmigrate *);

void		  sim_stop(gpointer, gpointer);

void		  simbuf_copy_cold(struct simbuf *);
void		  simbuf_copy_warm(struct simbuf *);
void		  simbuf_copy_hotlsb(struct simbuf *);
void		  simbuf_free(struct simbuf *);
struct simbuf	 *simbuf_alloc(struct kdata *, size_t);
struct simbuf	 *simbuf_alloc_warm(struct kdata *, size_t);

struct kml	 *kml_parse(const gchar *file, GError **er);
struct kml	 *kml_rand(size_t, size_t);
struct kml	 *kml_torus(size_t, size_t);
void		  kml_free(struct kml *kml);
double		**kml_migration_distance(GList *, enum maptop);
double		**kml_migration_nearest(GList *, enum maptop);
double		**kml_migration_twonearest(GList *, enum maptop);

GtkAdjustment	 *win_init_adjustment(GtkBuilder *, const gchar *);
GtkStatusbar	 *win_init_status(GtkBuilder *, const gchar *);
GtkDrawingArea	 *win_init_draw(GtkBuilder *, const gchar *);
GtkMenuBar	 *win_init_menubar(GtkBuilder *, const gchar *);
GtkCheckMenuItem *win_init_menucheck(GtkBuilder *, const gchar *);
GtkMenuItem 	 *win_init_menuitem(GtkBuilder *, const gchar *);
GtkWindow 	 *win_init_window(GtkBuilder *, const gchar *);
GtkLabel 	 *win_init_label(GtkBuilder *, const gchar *);
GtkBox 	 	 *win_init_box(GtkBuilder *, const gchar *);
GtkButton 	 *win_init_button(GtkBuilder *, const gchar *);
GtkRadioButton 	 *win_init_radio(GtkBuilder *, const gchar *);
GtkNotebook 	 *win_init_notebook(GtkBuilder *, const gchar *);
GtkToggleButton  *win_init_toggle(GtkBuilder *, const gchar *);
GtkEntry	 *win_init_entry(GtkBuilder *, const gchar *);
GtkFileChooser	 *win_init_filechoose(GtkBuilder *, const gchar *);
GtkBuilder	 *builder_get(const gchar *);

__END_DECLS

#endif
