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
#ifdef __linux__
#include <bsd/stdlib.h> /* arc4random() */
#endif
#include <string.h>

#include <gtk/gtk.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

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
 * Copy the "hot" data into "warm" holding.
 * While here, set our simulation's "work" parameter (if "fitpoly" is
 * set) to contain the necessary dependent variable.
 * Do this all as quickly as possible, as we're holding both the
 * simulation "hot" lock and the "warm" lock as well.
 */
static void
snapshot(struct sim *sim, struct simwarm *warm, 
	uint64_t truns, uint64_t tgens)
{
	double	 min, max, v, chisq, x;
	size_t	 i, j, k;

	/*
	 * If we have the same number of runs, don't bother snapshotting
	 * us--we're already up to date.
	 */
	if (warm->truns == truns) {
		assert(warm->tgens == tgens);
		return;
	}

	memcpy(warm->stats, sim->hot.statslsb, 
		sim->dims * sizeof(struct stats));
	memcpy(warm->islands, sim->hot.islandslsb, 
		sim->islands * sizeof(struct stats));
	warm->truns = truns;
	warm->tgens = tgens;

	v = stats_mean(&warm->stats[0]) +
		stats_mean(&warm->stats[1]);
	min = warm->smeans[0] = v / 2.0;
	for (i = 1; i < sim->dims - 1; i++) {
		v += stats_mean(&warm->stats[i + 1]);
		warm->smeans[i] = v / 3.0;
		v -= stats_mean(&warm->stats[i - 1]);
		if (warm->smeans[i] < min) {
			min = warm->smeans[i];
			warm->smeanmin = i;
		}
	}
	warm->smeans[i] = v / 2.0;
	if (warm->smeans[i] < min)
		warm->smeanmin = i;

	v = stats_extinctm(&warm->stats[0]) +
		stats_extinctm(&warm->stats[1]);
	max = warm->sextms[0] = v / 2.0;
	for (i = 1; i < sim->dims - 1; i++) {
		v += stats_extinctm(&warm->stats[i + 1]);
		warm->sextms[i] = v / 3.0;
		v -= stats_extinctm(&warm->stats[i - 1]);
		if (warm->sextms[i] > max) {
			max = warm->sextms[i];
			warm->sextmmax = i;
		}
	}
	warm->sextms[i] = v / 2.0;
	if (warm->sextms[i] > max)
		warm->sextmmax = i;

	/* Compute the empirical minimum. */
	for (min = FLT_MAX, i = 0; i < sim->dims; i++)
		if (stats_mean(&warm->stats[i]) < min) {
			min = stats_mean(&warm->stats[i]);
			warm->meanmin = i;
		}

	/* Find the extinct mutant maximum. */
	for (min = -FLT_MAX, i = 0; i < sim->dims; i++)
		if (stats_extinctm(&warm->stats[i]) > min) {
			min = stats_extinctm(&warm->stats[i]);
			warm->extmmax = i;
		}

	/* Find the extinct incumbent minimum. */
	for (min = FLT_MAX, i = 0; i < sim->dims; i++)
		if (stats_extincti(&warm->stats[i]) < min) {
			min = stats_extincti(&warm->stats[i]);
			warm->extimin = i;
		}

	/*
	 * If we're going to fit to a polynomial, set the dependent
	 * variable within the conditional.
	 */
	if (sim->fitpoly)
		for (i = 0; i < sim->dims; i++)
			gsl_vector_set(sim->work.y, i,
				 stats_mean(&warm->stats[i]));

	/*
	 * If we're going to run a weighted polynomial multifit, then
	 * use the variance as the weight.
	 */
	if (sim->fitpoly && sim->weighted)
		for (i = 0; i < sim->dims; i++) 
			gsl_vector_set(sim->work.w, i,
				 stats_stddev(&warm->stats[i]));

	/*
	 * If we're not fitting to a polynomial, simply notify that
	 * we've copied out and continue on our way.
	 */
	if (0 == sim->fitpoly)
		return;

	/* 
	 * If we're fitting to a polynomial, initialise our independent
	 * variables now.
	 */
	for (i = 0; i < sim->dims; i++) {
		gsl_matrix_set(sim->work.X, i, 0, 1.0);
		for (j = 0; j < sim->fitpoly; j++) {
			v = sim->continuum.xmin +
				(sim->continuum.xmax -
				 sim->continuum.xmin) *
				(i / (double)sim->dims);
			for (k = 0; k < j; k++)
				v *= v;
			gsl_matrix_set(sim->work.X, i, j + 1, v);
		}
	}

	/*
	 * Now perform the actual fitting.
	 * We either use weighted (weighted with the variance) or
	 * non-weighted fitting algorithms.
	 */
	if (sim->weighted) 
		gsl_multifit_wlinear(sim->work.X, sim->work.w, 
			sim->work.y, sim->work.c, sim->work.cov, 
			&chisq, sim->work.work);
	else
		gsl_multifit_linear(sim->work.X, sim->work.y, 
			sim->work.c, sim->work.cov, &chisq, 
			sim->work.work);

	/*
	 * Now snapshot the fitted polynomial coefficients and compute
	 * the fitted approximation for all incumbents (along with the
	 * minimum value).
	 */
	for (i = 0; i < sim->fitpoly + 1; i++)
		warm->coeffs[i] = gsl_vector_get(sim->work.c, i);
	min = FLT_MAX;
	for (i = 0; i < sim->dims; i++) {
		x = sim->continuum.xmin + 
			(sim->continuum.xmax -
			 sim->continuum.xmin) *
			i / (double)sim->dims;
		warm->fits[i] = fitpoly
			(warm->coeffs, sim->fitpoly + 1, x);
		if (warm->fits[i] < min) {
			warm->fitmin = i;
			min = warm->fits[i];
		}
	}
}

/*
 * In a given simulation, compute the next mutant/incumbent pair.
 * We make sure that incumbents are striped evenly in any given
 * simulation but that mutants are randomly selected from within the
 * strategy domain.
 */
static int
on_sim_next(struct sim *sim, const gsl_rng *rng, 
	size_t *islandidx, double *mutantp, double *incumbentp, 
	size_t *incumbentidx, double *vp, size_t gen)
{
	int		 fit = 0;
	size_t		 mutant;
	uint64_t	 truns, tgens;

	if (sim->terminate)
		return(0);

	truns = tgens = 0; /* Silence compiler. */

	assert(*incumbentidx < sim->dims);
	assert(*islandidx < sim->islands);
	g_mutex_lock(&sim->hot.mux);

	/*
	 * If we're entering this with a result value, then plug it into
	 * the index associated with the run and increment our count.
	 * This prevents us from overwriting others' results.
	 */
	if (NULL != vp) {
		stats_push(&sim->hot.stats[*incumbentidx], *vp);
		stats_push(&sim->hot.islands[*islandidx], *vp);
		sim->hot.tgens += gen;
		sim->hot.truns++;
	}

	/*
	 * Check if we've been requested to pause.
	 * If so, wait for a broadcast on our condition.
	 * This will unlock the mutex for others to process.
	 */
	if (1 == sim->hot.pause)
		g_cond_wait(&sim->hot.cond, &sim->hot.mux);

	/*
	 * Check if we've been instructed by the main thread of
	 * execution to snapshot hot data into warm storage.
	 * We do this in two parts: first, we register that we're in a
	 * copyout (it's now 2) and then actually do the copyout outside
	 * of the hot mutex.
	 */
	if (1 == sim->hot.copyout) {
		memcpy(sim->hot.statslsb, sim->hot.stats,
			sim->dims * sizeof(struct stats));
		memcpy(sim->hot.islandslsb, sim->hot.islands,
			sim->islands * sizeof(struct stats));
		truns = sim->hot.truns;
		tgens = sim->hot.tgens;
		sim->hot.copyout = fit = 2;
	} 

	/*
	 * Reassign our mutant and incumbent from the ring sized by the
	 * configured lattice dimensions.
	 * These both increment in single movements until the end of the
	 * lattice, then wrap around.
	 */
	*islandidx = sim->hot.island;
	mutant = sim->hot.mutant;
	*incumbentidx = sim->hot.incumbent;
	sim->hot.mutant++;
	if (sim->hot.mutant == sim->dims) {
		sim->hot.incumbent++;
		if (sim->hot.incumbent == sim->dims) {
			sim->hot.incumbent = 0;
			sim->hot.island = (sim->hot.island + 1) % 
				sim->islands;
		}
		sim->hot.mutant = 0;
	}
	
	g_mutex_unlock(&sim->hot.mux);

	/*
	 * Assign our incumbent and mutant.
	 * The incumbent just gets the current lattice position,
	 * ensuring an even propogation.
	 * The mutant is either assigned the same way or from a Gaussian
	 * distribution around the current incumbent.
	 */
	*incumbentp = sim->continuum.xmin + 
		(sim->continuum.xmax - 
		 sim->continuum.xmin) * 
		(*incumbentidx / (double)sim->dims);

	if (MUTANTS_GAUSSIAN == sim->mutants) {
		do {
			*mutantp = *incumbentp + 
				gsl_ran_gaussian
				(rng, sim->mutantsigma);
		} while (*mutantp < sim->continuum.ymin ||
			 *mutantp >= sim->continuum.ymax);
	} else
		*mutantp = sim->continuum.xmin + 
			(sim->continuum.xmax - 
			 sim->continuum.xmin) * 
			(mutant / (double)sim->dims);

	/*
	 * If we were the ones to set the copyout bit, then do the
	 * copyout right now.
	 * When we're finished, lower the copyout semaphor.
	 */
	if (fit) {
		snapshot(sim, &sim->warm, truns, tgens);
		g_mutex_lock(&sim->hot.mux);
		assert(2 == sim->hot.copyout);
		sim->hot.copyout = 0;
		g_mutex_unlock(&sim->hot.mux);
	}

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
		 sim->continuum.exp, x,
		 (mutants * mutant) + ((pop - mutants) * incumbent),
		 pop);
	assert( ! (isnan(v) || isinf(v)));
	return(sim->alpha * (1.0 + sim->delta * v));
}

static size_t
migrate(const struct sim *sim, const gsl_rng *rng, size_t cur)
{
	double	 v;
	size_t	 i;

again:
	v = gsl_rng_uniform(rng);
	for (i = 0; i < sim->islands - 1; i++)
		if ((v -= sim->ms[cur][i]) <= 0.0)
			break;

	/*
	 * This can occur due to floating-point rounding.
	 * If it does, re-run the algorithm.
	 */
	if (i == sim->islands - 1 && i == cur) {
		g_debug("Degenerate probability: re-running");
		goto again;
	}

	g_assert(cur != i);
	return(i);
}

/*
 * Run a simulation.
 * This can be one thread of many within the same simulation.
 */
void *
simulation(void *arg)
{
	struct simthr	 *thr = arg;
	struct sim	 *sim = thr->sim;
	double		  mutant, incumbent, v, lambda;
	unsigned int	  offs;
	unsigned long	  seed;
	double		 *vp, *icache, *mcache;
	double		**icaches, **mcaches;
	size_t		 *kids[2], *migrants[2], *imutants;
	size_t		  t, i, j, k, new, mutants, incumbents,
			  len1, len2, incumbentidx, islandidx;
	int		  mutant_old, mutant_new;
	gsl_rng		 *rng;

	rng = gsl_rng_alloc(gsl_rng_default);
	seed = arc4random();
	gsl_rng_set(rng, seed);

	g_debug("Thread %p (simulation %p) using RNG %s, "
		"seed %lu, initial %lu", 
		g_thread_self(), sim, gsl_rng_name(rng),
		seed, gsl_rng_get(rng));

	icache = mcache = NULL;
	icaches = mcaches = NULL;
	kids[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	kids[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	imutants = g_malloc0_n(sim->islands, sizeof(size_t));
	vp = NULL;
	incumbentidx = islandidx = 0;
	t = 0;

	/*
	 * Set up our mutant and incumbent payoff caches.
	 * These consist of all possible payoffs with a given number of
	 * mutants and incumbents on an island.
	 * We have two ways of doing this: with non-uniform island sizes
	 * (icaches and mcaches) and uniform sizes (icache and mcache,
	 * notice the singular).
	 */
	if (NULL != sim->pops) {
		g_debug("Thread %p (simulation %p) has non-uniform "
			"populations", g_thread_self(), sim);
		assert(0 == sim->pop);
		icaches = g_malloc0_n(sim->islands, sizeof(double *));
		mcaches = g_malloc0_n(sim->islands, sizeof(double *));
		for (i = 0; i < sim->islands; i++) {
			icaches[i] = g_malloc0_n
				(sim->pops[i] + 1, sizeof(double));
			mcaches[i] = g_malloc0_n
				(sim->pops[i] + 1, sizeof(double));
		}
	} else {
		g_debug("Thread %p (simulation %p) has "
			"uniform populations: %zu", 
			g_thread_self(), sim, sim->pop);
		assert(sim->pop > 0);
		icache = g_malloc0_n(sim->pop + 1, sizeof(double));
		mcache = g_malloc0_n(sim->pop + 1, sizeof(double));
	}
again:
	/* 
	 * Repeat til we're instructed to terminate. 
	 * We also pass in our last result for processing.
	 */
	if ( ! on_sim_next(sim, rng, &islandidx, &mutant, 
		&incumbent, &incumbentidx, vp, t)) {
		g_debug("Thread %p (simulation %p) exiting", 
			g_thread_self(), sim);
		/*
		 * Upon termination, free up all of the memory
		 * associated with our simulation.
		 */
		g_free(imutants);
		g_free(kids[0]);
		g_free(kids[1]);
		g_free(migrants[0]);
		g_free(migrants[1]);
		if (NULL != sim->pops)
			for (i = 0; i < sim->islands; i++) {
				g_free(icaches[i]);
				g_free(mcaches[i]);
			}
		g_free(icaches);
		g_free(mcaches);
		g_free(icache);
		g_free(mcache);
		return(NULL);
	}

	/* 
	 * Initialise a random island to have one mutant. 
	 * The rest are all incumbents.
	 */
	memset(imutants, 0, sim->islands * sizeof(size_t));
	imutants[islandidx] = 1;
	mutants = 1;
	incumbents = sim->totalpop - mutants;

	/*
	 * Precompute all possible payoffs.
	 * This allows us not to re-run the lambda calculation for each
	 * individual.
	 * If we have only a single island size, then avoid allocating
	 * for each island by using only the first mcaches index.
	 */
	if (NULL != sim->pops)
		for (i = 0; i < sim->islands; i++) {
			for (j = 0; j <= sim->pops[i]; j++) {
				mcaches[i][j] = continuum_lambda
					(sim, mutant, mutant, 
					 incumbent, j, sim->pops[i]);
				icaches[i][j] = continuum_lambda
					(sim, incumbent, mutant, 
					 incumbent, j, sim->pops[i]);
			}
		}
	else
		for (i = 0; i <= sim->pop; i++) {
			mcache[i] = continuum_lambda(sim, mutant,
				mutant, incumbent, i, sim->pop);
			icache[i] = continuum_lambda(sim, incumbent,
				mutant, incumbent, i, sim->pop);
		}

	for (t = 0; t < sim->stop; t++) {
		/*
		 * Birth process: have each individual (first mutants,
		 * then incumbents) give birth.
		 * Use a Poisson process with the given mean in order to
		 * properly compute this.
		 * We need two separate versions for whichever type of
		 * mcache we decide to use.
		 */
		if (NULL != sim->pops)
			for (j = 0; j < sim->islands; j++) {
				assert(0 == kids[0][j]);
				assert(0 == kids[1][j]);
				assert(0 == migrants[0][j]);
				assert(0 == migrants[1][j]);
				lambda = mcaches[j][imutants[j]];
				for (k = 0; k < imutants[j]; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[0][j] += offs;
				}
				lambda = icaches[j][imutants[j]];
				for ( ; k < sim->pops[j]; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[1][j] += offs;
				}
			}
		else
			for (j = 0; j < sim->islands; j++) {
				assert(0 == kids[0][j]);
				assert(0 == kids[1][j]);
				assert(0 == migrants[0][j]);
				assert(0 == migrants[1][j]);
				lambda = mcache[imutants[j]];
				for (k = 0; k < imutants[j]; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[0][j] += offs;
				}
				lambda = icache[imutants[j]];
				for ( ; k < sim->pop; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[1][j] += offs;
				}
			}

		/*
		 * Determine whether we're going to migrate and, if
		 * migration is stipulated, to where.
		 */
		if (NULL != sim->ms)
			for (j = 0; j < sim->islands; j++) {
				for (k = 0; k < kids[0][j]; k++) {
					new = j;
					if (gsl_rng_uniform(rng) < sim->m)
						new = migrate(sim, rng, j);
					migrants[0][new]++;
				}
				for (k = 0; k < kids[1][j]; k++) {
					new = j;
					if (gsl_rng_uniform(rng) < sim->m)
						new = migrate(sim, rng, j);
					migrants[1][new]++;
				}
				kids[0][j] = kids[1][j] = 0;
			}
		else
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
		if (NULL != sim->pops) 
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
		else
			for (j = 0; j < sim->islands; j++) {
				len1 = migrants[0][j] + migrants[1][j];
				if (0 == len1)
					continue;

				len2 = gsl_rng_uniform_int(rng, sim->pop);
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

	/*
	 * Assign the result pointer to the last population fraction.
	 * This will be processed by on_sim_next().
	 */
	if (incumbents == 0) {
		assert(mutants == sim->totalpop);
		v = 1.0;
	} else if (mutants == 0) {
		assert(incumbents == sim->totalpop);
		v = 0.0;
	} else
		v = mutants / (double)sim->totalpop;

	vp = &v;
	goto again;
}

