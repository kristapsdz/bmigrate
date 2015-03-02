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
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include <cairo.h>
#include <cairo-pdf.h>
#include <cairo-ps.h>
#include <gtk/gtk.h>
#include <gsl/gsl_multifit.h>
#include <kplot.h>

#include "extern.h"

enum	savetype {
	SAVE_PDF,
	SAVE_PS,
	SAVE_EPS
};

static int
savepng(const gchar *fname, const struct curwin *c)
{
	cairo_surface_t	*surf;
	cairo_t		*cr;
	cairo_status_t	 st;

	g_debug("%p: Saving: %s", c, fname);
	surf = cairo_image_surface_create
		(CAIRO_FORMAT_ARGB32, 600, 400);
	st = cairo_surface_status(surf);
	if (CAIRO_STATUS_SUCCESS != st) {
		g_debug("%s", cairo_status_to_string(st));
		cairo_surface_destroy(surf);
		return(0);
	}

	cr = cairo_create(surf);
	cairo_surface_destroy(surf);

	st = cairo_status(cr);
	if (CAIRO_STATUS_SUCCESS != st) {
		g_debug("%s", cairo_status_to_string(st));
		cairo_destroy(cr);
		return(0);

	}

	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, 600.0, 400.0);
	cairo_fill(cr);
	kplot_draw(c->views[c->view], 600.0, 400.0, cr);

	st = cairo_surface_write_to_png(cairo_get_target(cr), fname);

	if (CAIRO_STATUS_SUCCESS != st) {
		g_debug("%s", cairo_status_to_string(st));
		cairo_destroy(cr);
		return(0);
	}

	cairo_destroy(cr);
	return(1);
}

static int
savepdf(const gchar *fname, struct curwin *c, enum savetype type)
{
	cairo_surface_t	*surf;
	cairo_t		*cr;
	cairo_status_t	 st;
	struct kplotcfg	*cfg;
	struct kdatacfg	*datas;
	size_t		 datasz, i, j;
	int		 rc;
	double		 svtic, svaxis, svborder, svgrid, sv, svticln;
	const double	 w = 72 * 6, h = 72 * 5;

	g_debug("%p: Saving: %s", c, fname);
	switch (type) {
	case (SAVE_PDF):
		surf = cairo_pdf_surface_create(fname, w, h);
		break;
	case (SAVE_EPS):
		surf = cairo_ps_surface_create(fname, w, h);
		cairo_ps_surface_set_eps(surf, 1);
		break;
	case (SAVE_PS):
		surf = cairo_ps_surface_create(fname, w, h);
		cairo_ps_surface_set_eps(surf, 0);
		break;
	default:
		abort();
	}

	st = cairo_surface_status(surf);
	if (CAIRO_STATUS_SUCCESS != st) {
		g_debug("%s", cairo_status_to_string(st));
		cairo_surface_destroy(surf);
		return(0);
	}

	cr = cairo_create(surf);
	cairo_surface_destroy(surf);

	st = cairo_status(cr);
	if (CAIRO_STATUS_SUCCESS != st) {
		g_debug("%s", cairo_status_to_string(st));
		cairo_destroy(cr);
		return(0);

	}

	cfg = kplot_get_plotcfg(c->views[c->view]);

	svtic = cfg->ticlabelfont.sz;
	svaxis = cfg->axislabelfont.sz;
	svborder = cfg->borderline.sz;
	svgrid = cfg->gridline.sz;
	svticln = cfg->ticline.sz;
	sv = 0.0; /* Silence compiler. */

	cfg->ticlabelfont.sz = 9.0;
	cfg->axislabelfont.sz = 9.0;
	cfg->borderline.sz = 0.5;
	cfg->gridline.sz = 0.5;
	cfg->ticline.sz = 0.5;

	for (i = 0; ; i++) {
		rc = kplot_get_datacfg(c->views[c->view],
			i, &datas, &datasz);
		if (0 == rc)
			break;
		for (j = 0; j < datasz; j++) {
			sv = datas[j].line.sz;
			datas[j].line.sz = 1.0;
		}
	}

	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, w, h);
	cairo_fill(cr);
	kplot_draw(c->views[c->view], w, h, cr);
	cairo_destroy(cr);

	cfg->ticlabelfont.sz = svtic;
	cfg->axislabelfont.sz = svaxis;
	cfg->borderline.sz = svborder;
	cfg->gridline.sz = svgrid;
	cfg->ticline.sz = svticln;

	for (i = 0; ; i++) {
		rc = kplot_get_datacfg(c->views[c->view],
			i, &datas, &datasz);
		if (0 == rc)
			break;
		for (j = 0; j < datasz; j++)
			datas[j].line.sz = sv;
	}

	return(1);
}

int
save(const gchar *fname, struct curwin *cur)
{

	if (g_str_has_suffix(fname, ".pdf")) 
		return(savepdf(fname, cur, SAVE_PDF));
	else if (g_str_has_suffix(fname, ".ps")) 
		return(savepdf(fname, cur, SAVE_PS));
	else if (g_str_has_suffix(fname, ".eps")) 
		return(savepdf(fname, cur, SAVE_EPS));
	else
		return(savepng(fname, cur));
}

int
saveconfig(const gchar *fname, const struct curwin *cur)
{
	FILE		*f;
	struct sim	*sim;
	GList		*l;

	g_debug("%p: Saving configuration: %s", cur, fname);

	if (NULL == (f = fopen(fname, "rw")))
		return(0);

	for (l = cur->sims; NULL != l; l = g_list_next(l)) {
		sim = l->data;
		fprintf(f, "Name: %s\n", sim->name);
		fprintf(f, "Colour: #%.2x%.2x%.2x\n", 
			(unsigned int)(cur->b->clrs[sim->colour].rgba[0] * 255),
			(unsigned int)(cur->b->clrs[sim->colour].rgba[1] * 255),
			(unsigned int)(cur->b->clrs[sim->colour].rgba[2] * 255));
		fprintf(f, "Function: %s\n", sim->func);
		fprintf(f, "Threads: %zu\n", sim->nprocs);
		fprintf(f, "Multiplier: %g(1 + %g lambda)\n", 
			sim->alpha, sim->delta);
		fprintf(f, "Max generations: %zu\n", sim->stop);
		fprintf(f, "Migration: %g (%suniform)\n", 
			sim->m, NULL != sim->ms ? "non-" : "");
		fprintf(f, "Incumbents: %zu, [%g,%g)\n", 
			sim->dims, sim->xmin, sim->xmax);
		fprintf(f, "Rolling average window: %zu\n", sim->smoothing);
		switch (sim->maptop) {
		case (MAPTOP_RECORD):
			fprintf(f, "Map: record-based\n");
			break;
		case (MAPTOP_RAND):
			fprintf(f, "Map: random\n");
			break;
		case (MAPTOP_TORUS):
			fprintf(f, "Map: torus\n");
			break;
		default:
			abort();
		}
		switch (sim->migrant) {
		case (MAPMIGRANT_UNIFORM):
			fprintf(f, "Migration: uniform\n");
			break;
		case (MAPMIGRANT_DISTANCE):
			fprintf(f, "Migration: distance\n");
			break;
		case (MAPMIGRANT_NEAREST):
			fprintf(f, "Migration: nearest\n");
			break;
		case (MAPMIGRANT_TWONEAREST):
			fprintf(f, "Migration: two nearest\n");
			break;
		default:
			abort();
		}
		if (MAPINDEX_STRIPED == sim->mapindex)
			fprintf(f, "Mutant index case: striped\n");
		else
			fprintf(f, "Mutant index case: fixed (%zu)\n",
				sim->mapindexfix);
		if (MUTANTS_DISCRETE == sim->mutants)
			fprintf(f, "Mutants: %zu, [%g,%g)\n", 
				sim->dims, sim->ymin, sim->ymax);
		else
			fprintf(f, "Mutants: N(sigma=%g), [%g,%g)\n", 
				sim->mutantsigma, sim->ymin, sim->ymax);
		fprintf(f, "Islands: %zu (%zu islanders)\n", 
			sim->islands, sim->totalpop);
		if (NULL != sim->pops)
			fprintf(f, "Island populations: non-uniform\n");
		else
			fprintf(f, "Island populations: %zu\n", sim->pop);
		fprintf(f, "Fit polynomial: %zu (%sweighted)\n",
			sim->fitpoly, 0 == sim->weighted ? "un" : "");

		fprintf(f, "\n");
	}

	fclose(f);
	return(1);
}
