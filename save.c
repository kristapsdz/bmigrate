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
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include <cairo.h>
#include <gtk/gtk.h>

#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

static void
write_cqueue(FILE *f, const struct sim *sim, size_t simnum, 
	const struct cqueue *q, const struct hstats *st)
{
	size_t	 i, j;
	
	for (i = 0; i < CQUEUESZ; i++) {
		fprintf(f, "%zu ", simnum);
		fprintf(f, "%zd ", (ssize_t)i - CQUEUESZ);
		j = (q->pos + i) % CQUEUESZ;
		fprintf(f, "%g ", GETS(sim, q->vals[j]));
		fprintf(f, "%g ", st->mode);
		fprintf(f, "%g\n", st->mean);
	}
}

static void
write_mins(FILE *f, size_t simnum, const struct hstats *st)
{

	fprintf(f, "%zu ", simnum);
	fprintf(f, "%g ", st->mean);
	fprintf(f, "%g ", st->mean - st->stddev < 0.0 ?
		0.0 : st->mean - st->stddev);
	fprintf(f, "%g", st->mean + st->stddev);
}

static void
write_cdf(FILE *f, size_t simnum, 
	const struct sim *sim, const gsl_histogram *p)
{
	size_t	 i;
	double	 v, sum;

	sum = gsl_histogram_max(p);
	for (v = 0.0, i = 0; i < sim->dims; i++) {
		fprintf(f, "%zu ", simnum);
		fprintf(f, "%g ", GETS(sim, i));
		v += gsl_histogram_get(p, i) / sum;
		fprintf(f, "%g\n", v);
	}
}

static void
write_pdf(FILE *f, size_t simnum, 
	const struct sim *sim, const gsl_histogram *p)
{
	size_t	 i;

	for (i = 0; i < sim->dims; i++) {
		fprintf(f, "%zu ", simnum);
		fprintf(f, "%g ", GETS(sim, i));
		fprintf(f, "%g\n", gsl_histogram_get(p, i));
	}
}

void
savewin(FILE *f, const GList *sims, const struct curwin *cur)
{
	double		 v;
	const struct sim *sim;
	size_t		 i, j;
	const GList	*l;

	for (i = 1, l = sims; NULL != l; l = l->next, i++) {
		sim = l->data;
		g_assert(NULL != sim);
		fprintf(f, "# simulation %zu: %s\n", i, sim->name);
		fprintf(f, "## N=%zu, n=%suniform, m=%g "
			"(%suniform), T=%zu\n", sim->islands, 
			NULL != sim->pops ?  "non-" : "",
			sim->m, NULL != sim->ms ? "non-" : "",
			sim->stop);
		fprintf(f, "## %g(1 + %g * pi)\n",
			sim->alpha, sim->delta);
		fprintf(f, "## pi(x,X,n) = %s, x=[%g, %g)\n",
			sim->func, sim->continuum.xmin,
			sim->continuum.xmax);
		if (MUTANTS_DISCRETE == sim->mutants) 
			fprintf(f, "## mutants: discrete\n");
		else
			fprintf(f, "## mutants: Gaussian "
				"(sigma=%g, [%g, %g])\n",
				sim->mutantsigma,
				sim->continuum.ymin,
				sim->continuum.ymax);
		fprintf(f, "## fitdegree: %zu (%s)\n",
			sim->fitpoly, sim->weighted ? 
			"weighted" : "unweighted");
		fprintf(f, "## runs: %" PRIu64 " (%" PRIu64 
			" generations)\n", sim->cold.truns, 
			sim->cold.tgens);
	}

	if (VIEW_CONFIG == cur->view || VIEW_STATUS == cur->view)
		return;

	for (i = 1, l = sims; NULL != l; l = l->next, i++) {
		sim = l->data;
		assert(NULL != sim);
		switch (cur->view) {
		case (VIEW_DEV):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_mean(&sim->cold.stats[j]);
				fprintf(f, "%g ", v);
				v = stats_mean(&sim->cold.stats[j]) -
				    stats_stddev(&sim->cold.stats[j]);
				if (v < 0.0)
					v = 0.0;
				fprintf(f, "%g ", v);
				v = stats_mean(&sim->cold.stats[j]) +
				    stats_stddev(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_POLY):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_mean(&sim->cold.stats[j]);
				if (v < 0.0)
					v = 0.0;
				fprintf(f, "%g ", v);
				v = sim->cold.fits[j];
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_POLYMINPDF):
			write_pdf(f, i, sim, sim->cold.fitmins);
			break;
		case (VIEW_POLYMINCDF):
			write_cdf(f, i, sim, sim->cold.fitmins);
			break;
		case (VIEW_MEANMINPDF):
			write_pdf(f, i, sim, sim->cold.meanmins);
			break;
		case (VIEW_MEANMINCDF):
			write_cdf(f, i, sim, sim->cold.meanmins);
			break;
		case (VIEW_MEANMINQ):
			write_cqueue(f, sim, i, 
				&sim->cold.meanminq, 
				&sim->cold.meanminst);
			break;
		case (VIEW_POLYMINS):
			write_mins(f, i, &sim->cold.fitminst);
			break;
		case (VIEW_SMEANMINS):
			write_mins(f, i, &sim->cold.smeanminst);
			break;
		case (VIEW_EXTIMINS):
			write_mins(f, i, &sim->cold.extiminst);
			break;
		case (VIEW_EXTMMAXS):
			write_mins(f, i, &sim->cold.extmmaxst);
			break;
		case (VIEW_MEANMINS):
			write_mins(f, i, &sim->cold.meanminst);
			break;
		case (VIEW_SMEANMINQ):
			write_cqueue(f, sim, i, 
				&sim->cold.smeanminq, 
				&sim->cold.smeanminst);
			break;
		case (VIEW_POLYMINQ):
			write_cqueue(f, sim, i, 
				&sim->cold.fitminq, 
				&sim->cold.fitminst);
			break;
		case (VIEW_EXTI):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_extincti(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTM):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_extinctm(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTIMINCDF):
			write_cdf(f, i, sim, sim->cold.extimins);
			break;
		case (VIEW_EXTIMINPDF):
			write_pdf(f, i, sim, sim->cold.extimins);
			break;
		case (VIEW_EXTMMAXCDF):
			write_cdf(f, i, sim, sim->cold.extmmaxs);
			break;
		case (VIEW_EXTMMAXPDF):
			write_pdf(f, i, sim, sim->cold.extmmaxs);
			break;
		case (VIEW_SEXTMMAXCDF):
			write_cdf(f, i, sim, sim->cold.sextmmaxs);
			break;
		case (VIEW_SEXTMMAXPDF):
			write_pdf(f, i, sim, sim->cold.sextmmaxs);
			break;
		case (VIEW_SMEANMINCDF):
			write_cdf(f, i, sim, sim->cold.smeanmins);
			break;
		case (VIEW_SMEANMINPDF):
			write_pdf(f, i, sim, sim->cold.smeanmins);
			break;
		case (VIEW_SEXTM):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = sim->cold.sextms[j];
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_SMEAN):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = sim->cold.smeans[j];
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_ISLANDMEAN):
			for (j = 0; j < sim->islands; j++) {
				fprintf(f, "%zu ", i);
				fprintf(f, "%zu ", j);
				v = stats_mean(&sim->cold.islands[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		default:
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", i);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_mean(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		}

		fputc('\n', f);
	}
}

void
save(FILE *f, struct bmigrate *b)
{
	struct curwin	*cur;
	GList		*sims;

	sims = g_object_get_data(G_OBJECT(b->current), "sims");
	g_assert(NULL != sims);
	cur = g_object_get_data(G_OBJECT(b->current), "cfg");
	g_assert(NULL != cur);
	savewin(f, sims, cur);
}
