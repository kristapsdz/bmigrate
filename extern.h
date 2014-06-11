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
 * It's really, really unlikely we'll hit this...
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
	HNODE_POSITIVE, /* unary operations */
	HNODE_NEGATIVE,
	HNODE__MAX
};

/*
 * A node in the ordered expression list.
 */
struct 	hnode {
	enum htype	  type; /* type of operation */
	double		  real; /* HNODE_NUMBER value */
};

struct	stats {
	size_t	n;
	size_t	extm;
	size_t	exti;
	double	M1;
	double	M2;
	double	M3;
	double	M4;
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
 * triggered by the first thread encountering copyout == 1.
 */
struct	simhot {
	GMutex		 mux; /* lock for changing data */
	GCond		 cond; /* mutex for waiting on snapshot */
	size_t		 truns; /* total number of runs */
	struct stats	*stats; /* statistics per incumbent */
	struct stats	*statslsb; /* lookaside for stats */
	int		 copyout; /* do we need to snapshot? */
	int		 pause; /* should we pause? */
	size_t		 copyblock; /* threads blocking on copy */
	size_t		 incumbent; /* current incumbent index */
	size_t		 mutant; /* current mutant index */
};

/*
 * A simulation thread group will snapshot its "struct simhot" structure
 * to this when its "copyout" is set to 1.
 * The fields are the same but with the addition "coeffs", "fits", and
 * "fitmin" are set (if applicable) to the fitted polynomial.
 */
struct	simwarm {
	size_t		 meanmin; /* minimum sample mean */
	size_t		 fitmin; /* index of minimum fitpoly point */
	size_t		 extinctmmax;
	double	   	*coeffs; /* fitpoly coefficients */
	double	   	*fits; /* fitpoly points */
	struct stats	*stats; /* statistics per incumbent */
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
	gsl_multifit_linear_workspace *work; /* workspace */
};

/*
 * Instead of operating on the simulation results themselves, we copy
 * output from "struct simwarm" into a "cold" buffer for viewing.
 * See "struct simwarm" for matching fields. 
 * This is done in the main thread of execution, so it is not locked.
 */
struct	simcold {
	size_t		 meanmin; /* minimum sample mean */
	struct stats	*stats; /* statistics per incumbent */
	double	   	*coeffs; /* fitpoly coefficients */
	double	   	*fits; /* fitpoly points */
	gsl_histogram	*fitmins; /* fitted minimum dist */
	gsl_histogram	*meanmins; /* mean minimum dist */
	gsl_histogram	*extinctmmaxs;
	size_t		 extinctmmax;
	size_t		 fitmin; /* current minimum */
	double		 fitminsmode; /* mode of fitmins */ 
	double		 fitminsmean; /* mean of fitmins */
	double		 fitminsstddev; /* stddev of fitmins */
	double		 meanminsmode; /* mode value of meanmins */
	double		 meanminsmean; /* mean value of meanmins */
	double		 meanminsstddev; /* stddev value of meanmins */
	size_t		 truns; /* total runs */
#define	MINQSZ		 256
	size_t		 meanminq[MINQSZ]; /* circleq of raw minima */
	size_t		 meanminqpos; /* current (ahead) in meanminq */
	size_t		 fitminq[MINQSZ]; /* circleq of raw minima */
	size_t		 fitminqpos; /* current (ahead) in meanminq */
};

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
	double		  mutantsigma; /* mutant gaussian sigma */
	size_t		  stop; /* when to stop */
	gchar		 *name; /* name of simulation */
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

__BEGIN_DECLS

struct hnode	**hnode_parse(const char **v);
void		  hnode_free(struct hnode **p);
struct hnode	**hnode_copy(struct hnode **p);
double		  hnode_exec(const struct hnode *const *p,
			double x, double X, size_t n);
void		  hnode_test(void);
#if 0
void		  hnode_print(struct hnode **p);
#endif
void		*simulation(void *arg);

struct stats	*stats_alloc0(size_t sz);
void		 stats_push(struct stats *p, double x);
size_t		 stats_samples(const struct stats *p);
double		 stats_mean(const struct stats *p);
double		 stats_variance(const struct stats *p);
double		 stats_stddev(const struct stats *p);
double		 stats_skewness(const struct stats *p);
double		 stats_kurtosis(const struct stats *p);
double		 stats_extinctm(const struct stats *p);
double		 stats_extincti(const struct stats *p);

__END_DECLS

#endif
