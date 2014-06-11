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

#include <glib.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

/*
 * Copy the "hot" data into "warm" holding.
 * While here, set our "work" parameter (if "fitpoly" is set) to contain
 * the necessary dependent variable.
 * Do this all as quickly as possible, as we're holding both the
 * simulation "hot" lock and the "warm" lock as well.
 */
static void
on_sim_snapshot(struct simwork *work, struct sim *sim)
{
	size_t	 i;
	double	 min;

	g_mutex_lock(&sim->warm.mux);
	memcpy(sim->warm.stats, 
		sim->hot.stats,
		sizeof(struct stats) * sim->dims);

	/* Compute the empirical minimum. */
	min = FLT_MAX;
	for (i = 0; i < sim->dims; i++)
		if (stats_mean(&sim->warm.stats[i]) < min) {
			min = stats_mean(&sim->warm.stats[i]);
			sim->warm.meanmin = i;
		}

	/*
	 * If we're going to fit to a polynomial, set the dependent
	 * variable within the conditional.
	 */
	if (sim->fitpoly)
		for (i = 0; i < sim->dims; i++)
			gsl_vector_set(work->y, i,
				 stats_mean(&sim->warm.stats[i]));

	/*
	 * If we're going to run a weighted polynomial multifit, then
	 * use the variance as the weight.
	 */
	if (sim->weighted)
		for (i = 0; i < sim->dims; i++) 
			gsl_vector_set(work->w, i,
				 stats_stddev(&sim->warm.stats[i]));

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
	const gsl_rng *rng, double *mutantp, 
	double *incumbentp, size_t *incumbentidx, double *vp)
{
	int		 fit = 0;
	size_t		 i, j, k, mutant;
	double		 v, chisq, x, min;

	if (sim->terminate) {
		/*
		 * We can have people waiting to copy-out data, so make
		 * them exit right now.
		 */
		g_mutex_lock(&sim->hot.mux);
		g_cond_broadcast(&sim->hot.cond);
		g_mutex_unlock(&sim->hot.mux);
		return(0);
	}

	g_mutex_lock(&sim->hot.mux);

	/*
	 * If we're entering this with a result value, then plug it into
	 * the index associated with the run and increment our count.
	 * This prevents us from overwriting others' results.
	 */
	if (NULL != vp) {
		stats_push(&sim->hot.stats[*incumbentidx], *vp);
		sim->hot.truns++;
	}

	/*
	 * Check if we've been instructed by the main thread of
	 * execution to snapshot hot data into warm storage.
	 * To do this synchronously, all threads will wait to finish
	 * their work.
	 * The last thread will then do the actual snapshot and
	 * broadcast to the wait condition.
	 */
	if (1 == sim->hot.copyout) {
		if (++sim->hot.copyblock == sim->nprocs) {
			fit = 1;
			sim->hot.copyout = 2;
			sim->hot.copyblock = 0;
			on_sim_snapshot(work, sim);
			g_cond_broadcast(&sim->hot.cond);
		} else
			g_cond_wait(&sim->hot.cond, &sim->hot.mux);
	} 

	/*
	 * Check if we've been requested to pause.
	 * If so, go directly into the conditional.
	 */
	if (1 == sim->hot.pause)
		g_cond_wait(&sim->hot.cond, &sim->hot.mux);

	/*
	 * Reassign our mutant and incumbent from the ring sized by the
	 * configured lattice dimensions.
	 * These both increment in single movements until the end of the
	 * lattice, then wrap around.
	 */
	mutant = sim->hot.mutant;
	*incumbentidx = sim->hot.incumbent;
	sim->hot.mutant++;
	if (sim->hot.mutant == sim->dims) {
		sim->hot.incumbent = (sim->hot.incumbent + 1) % sim->dims;
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
	*incumbentp = sim->d.continuum.xmin + 
		(sim->d.continuum.xmax - 
		 sim->d.continuum.xmin) * 
		(*incumbentidx / (double)sim->dims);

	if (MUTANTS_GAUSSIAN == sim->mutants) {
		do {
			*mutantp = *incumbentp + 
				gsl_ran_gaussian
				(rng, sim->mutantsigma);
		} while (*mutantp < sim->d.continuum.xmin ||
			 *mutantp >= sim->d.continuum.xmax);
	} else
		*mutantp = sim->d.continuum.xmin + 
			(sim->d.continuum.xmax - 
			 sim->d.continuum.xmin) * 
			(mutant / (double)sim->dims);

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
	 * If we're fitting to a polynomial, initialise our independent
	 * variables now.
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
	 * We either use weighted (weighted with the variance) or
	 * non-weighted fitting algorithms.
	 */
	if (sim->weighted) 
		gsl_multifit_wlinear(work->X, work->w, work->y, 
			work->c, work->cov, &chisq, work->work);
	else
		gsl_multifit_linear(work->X, work->y, 
			work->c, work->cov, &chisq, work->work);

	/*
	 * Now snapshot the fitted polynomial coefficients and compute
	 * the fitted approximation for all incumbents (along with the
	 * minimum value).
	 */
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
		sim->warm.fits[i] = fitpoly
			(sim->warm.coeffs, sim->fitpoly + 1, x);
		if (sim->warm.fits[i] < min) {
			sim->warm.fitmin = i;
			min = sim->warm.fits[i];
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
void *
simulation(void *arg)
{
	struct simthr	*thr = arg;
	struct sim	*sim = thr->sim;
	double		 mutant, incumbent, v, lambda;
	double		*vp;
	size_t		*kids[2], *migrants[2], *imutants;
	size_t		 t, j, k, new, mutants, incumbents,
			 len1, len2, incumbentidx;
	int		 mutant_old, mutant_new;
	struct simwork	 work;
	gsl_rng		*rng;

	rng = gsl_rng_alloc(gsl_rng_default);
	gsl_rng_set(rng, arc4random());

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
	vp = NULL;

again:
	/* 
	 * Repeat til we're instructed to terminate. 
	 * We also pass in our last result for processing.
	 */
	if ( ! on_sim_next(&work, sim, rng, 
		&mutant, &incumbent, &incumbentidx, vp)) {
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
			assert(0 == kids[0][j]);
			assert(0 == kids[1][j]);
			assert(0 == migrants[0][j]);
			assert(0 == migrants[1][j]);
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

	/*
	 * Assign the result pointer to the last population fraction.
	 * This will be processed by on_sim_next().
	 */
	v = mutants / (double)sim->totalpop;
	vp = &v;
	goto again;
}

