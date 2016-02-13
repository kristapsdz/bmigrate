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
#include <kplot.h>

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
 * Copy the "hotlsb" data into "warm" holding.
 * While here, set our simulation's "work" parameter (if "fitpoly" is
 * set) to contain the necessary dependent variable.
 * We're guaranteed to be the only ones in here, and the only ones with
 * a lock on the LBS data.
 */
static void
snapshot(struct sim *sim, struct simwarm *warm, 
	uint64_t truns, uint64_t tgens)
{
	double	 	v, chisq, x, y;
	struct kpair	kp;
	int		rc;
	size_t	 	i, j, k;

	/* Warm copy is already up to date. */
	if (warm->truns == truns) {
		g_assert(warm->tgens == tgens);
		return;
	}

	/* Copy out: we have a lock. */
	simbuf_copy_warm(sim->bufs.times);
	simbuf_copy_warm(sim->bufs.imeans);
	simbuf_copy_warm(sim->bufs.istddevs);
	simbuf_copy_warm(sim->bufs.islandmeans);
	simbuf_copy_warm(sim->bufs.islandstddevs);
	simbuf_copy_warm(sim->bufs.means);
	simbuf_copy_warm(sim->bufs.stddevs);
	simbuf_copy_warm(sim->bufs.mextinct);
	simbuf_copy_warm(sim->bufs.iextinct);
	warm->truns = truns;
	warm->tgens = tgens;

	/*
	 * If we're going to fit to a polynomial, set the dependent
	 * variable within the conditional.
	 */
	if (sim->fitpoly)
		for (i = 0; i < sim->dims; i++) {
			rc = kdata_get(sim->bufs.means->warm, i, &kp);
			g_assert(0 != rc);
			gsl_vector_set(sim->work.y, i, kp.y);
		}

	/*
	 * If we're going to run a weighted polynomial multifit, then
	 * use the variance as the weight.
	 */
	if (sim->fitpoly && sim->weighted)
		for (i = 0; i < sim->dims; i++) {
			rc = kdata_get(sim->bufs.stddevs->warm, i, &kp);
			g_assert(0 != rc);
			gsl_vector_set(sim->work.w, i, kp.y);
		}

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
			v = sim->xmin +
				(sim->xmax -
				 sim->xmin) *
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
		sim->work.coeffs[i] = gsl_vector_get(sim->work.c, i);

	for (i = 0; i < sim->dims; i++) {
		x = sim->xmin + (sim->xmax - sim->xmin) *
			i / (double)sim->dims;
		y = fitpoly(sim->work.coeffs, 
			sim->fitpoly + 1, x);
		kdata_array_set(sim->bufs.fitpoly, i, x, y);
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
	size_t *incumbentidx, double *vp, const size_t *islands,
	size_t gen)
{
	int		 dosnap, rc;
	size_t		 mutant;
	uint64_t	 truns, tgens;

	if (sim->terminate)
		return(0);

	truns = tgens = 0; /* Silence compiler. */

	/* This is set if we "own" the LSB->warm process. */
	dosnap = 0;

	g_assert(*incumbentidx < sim->dims);
	g_assert(*islandidx < sim->islands);
	g_mutex_lock(&sim->hot.mux);

	/*
	 * If we're entering this with a result value, then plug it into
	 * the index associated with the run and increment our count.
	 * This prevents us from overwriting others' results.
	 */
	if (NULL != vp) {
		rc = kdata_array_add
			(sim->bufs.times->hot, gen, 1.0);
		g_assert(0 != rc);
		rc = kdata_array_fill_ysizes
			(sim->bufs.islands, islands);
		g_assert(0 != rc);
		rc = kdata_array_set(sim->bufs.fractions, 
			*incumbentidx, *incumbentp, *vp);
		g_assert(0 != rc);
		rc = kdata_array_set(sim->bufs.ifractions, 
			*islandidx, *islandidx, *vp);
		g_assert(0 != rc);
		rc = kdata_array_set(sim->bufs.mutants, 
			*incumbentidx, *incumbentp, 0.0 == *vp);
		g_assert(0 != rc);
		rc = kdata_array_set(sim->bufs.incumbents, 
			*incumbentidx, *incumbentp, 1.0 == *vp);
		g_assert(0 != rc);
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
		simbuf_copy_hotlsb(sim->bufs.times);
		simbuf_copy_hotlsb(sim->bufs.islandmeans);
		simbuf_copy_hotlsb(sim->bufs.islandstddevs);
		simbuf_copy_hotlsb(sim->bufs.imeans);
		simbuf_copy_hotlsb(sim->bufs.istddevs);
		simbuf_copy_hotlsb(sim->bufs.means);
		simbuf_copy_hotlsb(sim->bufs.stddevs);
		simbuf_copy_hotlsb(sim->bufs.mextinct);
		simbuf_copy_hotlsb(sim->bufs.iextinct);
		truns = sim->hot.truns;
		tgens = sim->hot.tgens;
		sim->hot.copyout = dosnap = 2;
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
			if (MAPINDEX_STRIPED == sim->mapindex)
				sim->hot.island = 
					(sim->hot.island + 1) % 
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
	*incumbentp = sim->xmin + (sim->xmax - sim->xmin) * 
		(*incumbentidx / (double)sim->dims);

	if (MUTANTS_GAUSSIAN == sim->mutants) {
		do {
			*mutantp = *incumbentp + 
				gsl_ran_gaussian
				(rng, sim->mutantsigma);
		} while (*mutantp < sim->ymin ||
			 *mutantp >= sim->ymax);
	} else
		*mutantp = sim->xmin + 
			(sim->xmax - 
			 sim->xmin) * 
			(mutant / (double)sim->dims);

	/*
	 * If we were the ones to set the copyout bit, then do the
	 * copyout right now.
	 * When we're finished, lower the copyout semaphor.
	 */
	if (dosnap) {
		snapshot(sim, &sim->warm, truns, tgens);
		g_mutex_lock(&sim->hot.mux);
		g_assert(2 == sim->hot.copyout);
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
reproduce(const struct sim *sim, double x, 
	double mutant, double incumbent, size_t mutants, size_t pop)
{
	double	 v;

	if (0 == pop)
		return(0.0);

	v = hnode_exec
		((const struct hnode *const *)
		 sim->exp, x,
		 (mutants * mutant) + ((pop - mutants) * incumbent),
		 pop);
	g_assert( ! (isnan(v) || isinf(v)));
	return(sim->alpha * (1.0 + sim->delta * v));
}

static size_t
migrate(const struct sim *sim, const gsl_rng *rng, size_t cur)
{
	double	 v;
	size_t	 i;

again:
	while (0.0 == (v = gsl_rng_uniform(rng)))
		/* Loop. */ ;

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
	struct simthr	  *thr = arg;
	struct sim	  *sim = thr->sim;
	double		   mutant, incumbent, v, lambda;
	unsigned int	   offs;
	unsigned long	   seed;
	double		  *vp, *icache, *mcache;
	double		***icaches, ***mcaches;
	size_t		  *kids[2], *migrants[2], *imutants, *npops;
	size_t		   t, i, j, k, new, mutants, incumbents,
			   len1, len2, incumbentidx, islandidx,
			   ntotalpop, ndeath;
	int		   mutant_old, mutant_new;
	gsl_rng		  *rng;

	rng = gsl_rng_alloc(gsl_rng_default);
	seed = arc4random();
	gsl_rng_set(rng, seed);

	g_debug("%p: Thread (simulation %p) "
		"start", g_thread_self(), sim);

	icache = mcache = NULL;
	icaches = mcaches = NULL;
	kids[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	kids[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[0] = g_malloc0_n(sim->islands, sizeof(size_t));
	migrants[1] = g_malloc0_n(sim->islands, sizeof(size_t));
	imutants = g_malloc0_n(sim->islands, sizeof(size_t));
	vp = NULL;
	npops = NULL;
	incumbentidx = 0;
	islandidx = MAPINDEX_FIXED == sim->mapindex ? 
		sim->mapindexfix : 0;
	mutant = incumbent = 0.0;
	t = 0;

	/*
	 * Set up our mutant and incumbent payoff caches.
	 * These consist of all possible payoffs with a given number of
	 * mutants and incumbents on an island.
	 * We have two ways of doing this: with non-uniform island sizes
	 * (icaches and mcaches) and uniform sizes (icache and mcache,
	 * notice the singular).
	 * The non-uniform island size can also change, so we precompute
	 * for all possible populations as well.
	 */
	if (NULL != sim->pops) {
		g_assert(0 == sim->pop);
		npops = g_malloc0_n(sim->islands, sizeof(size_t));
		for (i = 0; i < sim->islands; i++) 
			npops[i] = sim->pops[i];
		icaches = g_malloc0_n(sim->islands, sizeof(double **));
		mcaches = g_malloc0_n(sim->islands, sizeof(double **));
		for (i = 0; i < sim->islands; i++) {
			icaches[i] = g_malloc0_n
				(sim->pops[i] + 1, sizeof(double *));
			mcaches[i] = g_malloc0_n
				(sim->pops[i] + 1, sizeof(double *));
			for (j = 0; j <= sim->pops[i]; j++) {
				icaches[i][j] = g_malloc0_n(j + 1, sizeof(double));
				mcaches[i][j] = g_malloc0_n(j + 1, sizeof(double));
			}
		}
	} else {
		g_assert(sim->pop > 0);
		icache = g_malloc0_n(sim->pop + 1, sizeof(double));
		mcache = g_malloc0_n(sim->pop + 1, sizeof(double));
	}
again:
	/* 
	 * Repeat til we're instructed to terminate. 
	 * We also pass in our last result for processing.
	 */
	if ( ! on_sim_next(sim, rng, &islandidx, &mutant, 
		&incumbent, &incumbentidx, vp, imutants, t)) {
		g_debug("%p: Thread (simulation %p) exiting", 
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
				for (j = 0; j <= sim->pops[i]; j++) {
					g_free(icaches[i][j]);
					g_free(mcaches[i][j]);
				}
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
	ntotalpop = sim->totalpop;
	ndeath = 0;

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
				for (k = 0; k <= j; k++) {
					mcaches[i][j][k] = reproduce
						(sim, mutant, mutant, 
						 incumbent, k, j);
					icaches[i][j][k] = reproduce
						(sim, incumbent, mutant, 
						 incumbent, k, j);
				}
			}
		}
	else
		for (i = 0; i <= sim->pop; i++) {
			mcache[i] = reproduce(sim, mutant,
				mutant, incumbent, i, sim->pop);
			icache[i] = reproduce(sim, incumbent,
				mutant, incumbent, i, sim->pop);
		}

	for (t = 0; t < sim->stop; t++) {
		/*
		 * If we're a non-uniform population and have an island
		 * death mean, then see if we're supposed to kill off a
		 * random island, then re-set the shot clock.
		 */
		if (NULL != sim->pops && sim->ideathmean > 0) {
			if (ndeath && t == ndeath) {
				i = gsl_rng_uniform_int(rng, sim->islands);
				mutants -= imutants[i];
				incumbents -= (npops[i] - imutants[i]);
				ntotalpop -= npops[i];
				npops[i] = 0;
				imutants[i] = 0;
				ndeath = t + gsl_ran_poisson
					(rng, sim->ideathmean);
			} else if (0 == ndeath) 
				ndeath = t + gsl_ran_poisson
					(rng, sim->ideathmean);
		}

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
				if (0 == npops[j])
					continue;
				g_assert(0 == kids[0][j]);
				g_assert(0 == kids[1][j]);
				g_assert(0 == migrants[0][j]);
				g_assert(0 == migrants[1][j]);
				g_assert(imutants[j] <= npops[j]);
				lambda = mcaches[j]
					[npops[j]][imutants[j]];
				for (k = 0; k < imutants[j]; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[0][j] += offs;
				}
				lambda = icaches[j]
					[npops[j]][imutants[j]];
				for ( ; k < npops[j]; k++) {
					offs = gsl_ran_poisson
						(rng, lambda);
					kids[1][j] += offs;
				}
			}
		else
			for (j = 0; j < sim->islands; j++) {
				g_assert(0 == kids[0][j]);
				g_assert(0 == kids[1][j]);
				g_assert(0 == migrants[0][j]);
				g_assert(0 == migrants[1][j]);
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
				if (npops[j] < sim->pops[j]) {
					/*
					 * This is the case where a
					 * given island has had its
					 * population killed off.
					 * Try to fill it up: don't
					 * replace anybody.
					 */
					while (npops[j] < sim->pops[j]) {
						len1 = migrants[0][j] + 
							migrants[1][j];
						if (0 == len1)
							break;
						len2 = gsl_rng_uniform_int
							(rng, len1);
						if (len2 < migrants[0][j]) {
							migrants[0][j]--;
							imutants[j]++;
							mutants++;
						} else {
							migrants[1][j]--;
							incumbents++;
						} 
						npops[j]++;
						ntotalpop++;
					}
				} else {
					len1 = migrants[0][j] + 
						migrants[1][j];
					if (0 == len1)
						continue;
					len2 = gsl_rng_uniform_int
						(rng, npops[j]);
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
		g_assert(mutants == ntotalpop);
		v = 1.0;
	} else if (mutants == 0) {
		g_assert(incumbents == ntotalpop);
		v = 0.0;
	} else
		v = mutants / (double)ntotalpop;
	
	vp = &v;
	goto again;
}

