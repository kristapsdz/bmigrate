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

void
save(FILE *f, struct bmigrate *b)
{
	struct curwin	*cur;
	double		 v, sum;
	struct sim	*sim;
	size_t		 j, k, simnum;
	GList		*sims, *list;

	cur = g_object_get_data(G_OBJECT(b->current), "cfg");
	assert(NULL != cur);
	sims = g_object_get_data(G_OBJECT(b->current), "sims");
	assert(NULL != sims);

	simnum = 1;
	for (list = sims; NULL != list; list = list->next, simnum++) {
		sim = list->data;
		assert(NULL != sim);
		fprintf(f, "# Simulation %zu: %s\n", 
			simnum, sim->name);
		fprintf(f, "#   N=%zu, n=%zu, m=%g, T=%zu\n", 
			sim->islands, sim->pops[0], 
			sim->m, sim->stop);
		fprintf(f, "#   %g(1 + %g * pi)\n",
			sim->alpha, sim->delta);
		fprintf(f, "#   pi(x,X,n) = %s, x=[%g, %g)\n",
			sim->func, sim->d.continuum.xmin,
			sim->d.continuum.xmax);
		fprintf(f, "#   fit-poly degree: %zu (%s)\n",
			sim->fitpoly, sim->weighted ? 
			"weighted" : "unweighted");
		if (MUTANTS_DISCRETE == sim->mutants) 
			fprintf(f, "#   mutants: discrete\n");
		else
			fprintf(f, "#   mutants: Gaussian "
				"(sigma=%g, [%g, %g])\n",
				sim->mutantsigma,
				sim->d.continuum.ymin,
				sim->d.continuum.ymax);
	}

	if (VIEW_CONFIG == cur->view)
		return;

	simnum = 1;
	for (list = sims; NULL != list; list = list->next, simnum++) {
		sim = list->data;
		assert(NULL != sim);
		switch (cur->view) {
		case (VIEW_DEV):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
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
				fprintf(f, "%zu ", simnum);
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
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = gsl_histogram_get
					(sim->cold.fitmins, j);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_POLYMINCDF):
			sum = gsl_histogram_sum(sim->cold.fitmins);
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				fprintf(f, "%g ", GETS(sim, j));
				v += gsl_histogram_get
					(sim->cold.fitmins, j) / sum;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_MEANMINPDF):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = gsl_histogram_get
					(sim->cold.meanmins, j);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_MEANMINCDF):
			sum = gsl_histogram_sum(sim->cold.meanmins);
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				fprintf(f, "%g ", GETS(sim, j));
				v += gsl_histogram_get
					(sim->cold.meanmins, j) / sum;
				fprintf(stderr, "%g\n", v);
			}
			break;
		case (VIEW_MEANMINQ):
			for (j = 0; j < CQUEUESZ; j++) {
				fprintf(f, "%zu ", simnum);
				v = j - CQUEUESZ;
				fprintf(f, "%g ", v);
				k = (sim->cold.meanminq.pos + j) % CQUEUESZ;
				v = GETS(sim, sim->cold.meanminq.vals[k]);
				fprintf(f, "%g ", v);
				v = sim->cold.meanminst.mode;
				fprintf(f, "%g ", v);
				v = sim->cold.meanminst.mean;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_POLYMINS):
			fprintf(f, "%zu ", simnum);
			v = sim->cold.fitminst.mean;
			fprintf(f, "%g ", v);
			v = sim->cold.fitminst.mean - 
			    sim->cold.fitminst.stddev;
			if (v < 0.0)
				v = 0.0;
			fprintf(f, "%g ", v);
			v = sim->cold.fitminst.mean +
			    sim->cold.fitminst.stddev;
			fprintf(f, "%g", v);
			break;
		case (VIEW_SMOOTHMINS):
			fprintf(f, "%zu ", simnum);
			v = sim->cold.smoothminst.mean;
			fprintf(f, "%g ", v);
			v = sim->cold.smoothminst.mean - 
			    sim->cold.smoothminst.stddev;
			if (v < 0.0)
				v = 0.0;
			fprintf(f, "%g ", v);
			v = sim->cold.smoothminst.mean +
			    sim->cold.smoothminst.stddev;
			fprintf(f, "%g", v);
			break;
		case (VIEW_EXTIMINS):
			fprintf(f, "%zu ", simnum);
			v = sim->cold.extiminst.mean;
			fprintf(f, "%g ", v);
			v = sim->cold.extiminst.mean - 
			    sim->cold.extiminst.stddev;
			if (v < 0.0)
				v = 0.0;
			fprintf(f, "%g ", v);
			v = sim->cold.extiminst.mean +
			    sim->cold.extiminst.stddev;
			fprintf(f, "%g", v);
			break;
		case (VIEW_EXTMMAXS):
			fprintf(f, "%zu ", simnum);
			v = sim->cold.extmmaxst.mean;
			fprintf(f, "%g ", v);
			v = sim->cold.extmmaxst.mean - 
			    sim->cold.extmmaxst.stddev;
			if (v < 0.0)
				v = 0.0;
			fprintf(f, "%g ", v);
			v = sim->cold.extmmaxst.mean +
			    sim->cold.extmmaxst.stddev;
			fprintf(f, "%g", v);
			break;
		case (VIEW_MEANMINS):
			fprintf(f, "%zu ", simnum);
			v = sim->cold.meanminst.mean;
			fprintf(f, "%g ", v);
			v = sim->cold.meanminst.mean - 
			    sim->cold.meanminst.stddev;
			if (v < 0.0)
				v = 0.0;
			fprintf(f, "%g ", v);
			v = sim->cold.meanminst.mean +
			    sim->cold.meanminst.stddev;
			fprintf(f, "%g", v);
			break;
		case (VIEW_SMOOTHMINQ):
			for (j = 0; j < CQUEUESZ; j++) {
				fprintf(f, "%zu ", simnum);
				v = j - CQUEUESZ;
				fprintf(f, "%g ", v);
				k = (sim->cold.smoothminq.pos + j) % CQUEUESZ;
				v = GETS(sim, sim->cold.smoothminq.vals[k]);
				fprintf(f, "%g ", v);
				v = sim->cold.smoothminst.mode;
				fprintf(f, "%g ", v);
				v = sim->cold.smoothminst.mean;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_POLYMINQ):
			for (j = 0; j < CQUEUESZ; j++) {
				fprintf(f, "%zu ", simnum);
				v = j - CQUEUESZ;
				fprintf(f, "%g ", v);
				k = (sim->cold.fitminq.pos + j) % CQUEUESZ;
				v = GETS(sim, sim->cold.fitminq.vals[k]);
				fprintf(f, "%g ", v);
				v = sim->cold.fitminst.mode;
				fprintf(f, "%g ", v);
				v = sim->cold.fitminst.mean;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTI):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_extincti(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTM):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = stats_extinctm(&sim->cold.stats[j]);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTIMINCDF):
			sum = gsl_histogram_max(sim->cold.extimins);
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				fprintf(f, "%g ", GETS(sim, j));
				v += gsl_histogram_get
					(sim->cold.extimins, j) / sum;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTIMINPDF):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = gsl_histogram_get
					(sim->cold.extimins, j);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTMMAXCDF):
			sum = gsl_histogram_sum(sim->cold.extmmaxs);
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				fprintf(f, "%g ", GETS(sim, j));
				v += gsl_histogram_get
					(sim->cold.extmmaxs, j) / sum;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_EXTMMAXPDF):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = gsl_histogram_get
					(sim->cold.extmmaxs, j);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_SMOOTHMINCDF):
			sum = gsl_histogram_sum(sim->cold.smoothmins);
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				fprintf(f, "%g ", GETS(sim, j));
				v += gsl_histogram_get
					(sim->cold.smoothmins, j) / sum;
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_SMOOTHMINPDF):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = gsl_histogram_get
					(sim->cold.smoothmins, j);
				fprintf(f, "%g\n", v);
			}
			break;
		case (VIEW_SMOOTH):
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
				v = GETS(sim, j);
				fprintf(f, "%g ", v);
				v = sim->cold.smooth[j];
				fprintf(f, "%g\n", v);
			}
			break;
		default:
			for (j = 0; j < sim->dims; j++) {
				fprintf(f, "%zu ", simnum);
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

