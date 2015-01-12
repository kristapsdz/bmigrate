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
#include <string.h>

#include <cairo.h>
#include <gtk/gtk.h>

#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

/*
 * These macros shorten the drawing routines by automatically computing
 * X and Y coordinates as well as colour.
 */
#define GETX(_j) \
	(width * (sim->continuum.xmin - minx) / (maxx - minx) + \
	(_j) / (double)(sim->dims - 1) * width * \
	 (sim->continuum.xmax - sim->continuum.xmin) / (maxx - minx))
#define	GETY(_v) (height - ((_v) / maxy * height))
#define	GETC(_a) b->wins.colours[sim->colour].red, \
		 b->wins.colours[sim->colour].green, \
		 b->wins.colours[sim->colour].blue, (_a)

/* 
 * Draw a grid containing the data.
 * FIXME: snapping pixels to position for fine lines.
 */
static void
drawgrid(cairo_t *cr, double width, double height)
{
	static const double dash[] = {6.0};

	/* Top line. */
	cairo_move_to(cr, 0.0, 0.0);
	cairo_line_to(cr, width, 0.0);
	/* Bottom line. */
	cairo_move_to(cr, 0.0, height);
	cairo_line_to(cr, width, height);
	/* Left line. */
	cairo_move_to(cr, 0.0, 0.0);
	cairo_line_to(cr, 0.0, height);
	/* Right line. */
	cairo_move_to(cr, width, 0.0);
	cairo_line_to(cr, width, height);
	/* Horizontal middle. */
	cairo_move_to(cr, width * 0.5, 0.0);
	cairo_line_to(cr, width * 0.5, height);
	/* Vertical middle. */
	cairo_move_to(cr, 0.0, height * 0.5);
	cairo_line_to(cr, width, height * 0.5);

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

	cairo_set_line_width(cr, 0.5);
	cairo_stroke(cr);

	/* Vertical left. */
	cairo_move_to(cr, 0.0, height * 0.25);
	cairo_line_to(cr, width, height * 0.25);
	/* Vertical right. */
	cairo_move_to(cr, 0.0, height * 0.75);
	cairo_line_to(cr, width, height * 0.75);
	/* Horizontal top. */
	cairo_move_to(cr, width * 0.75, 0.0);
	cairo_line_to(cr, width * 0.75, height);
	/* Horizontal bottom. */
	cairo_move_to(cr, width * 0.25, 0.0);
	cairo_line_to(cr, width * 0.25, height);

	cairo_set_line_width(cr, 0.5);
	cairo_set_dash(cr, dash, 1, 0);
	cairo_stroke(cr);

	cairo_set_dash(cr, dash, 0, 0);
}

/*
 * Draw axis labels.
 * The choice of label depends on what we're looking at.
 */
static void
drawlabels(const struct curwin *cur, cairo_t *cr, 
	const char *fmt, double *widthp, double *heightp,
	double miny, double maxy, double minx, double maxx)
{
	cairo_text_extents_t e;
	char	 	 buf[1024];
	double		 width, height;

	width = *widthp;
	height = *heightp;

	cairo_text_extents(cr, "-10.00", &e);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); 

	switch (cur->view) {
	case (VIEW_POLYMINPDF):
	case (VIEW_POLYMINCDF):
	case (VIEW_MEANMINPDF):
	case (VIEW_MEANMINCDF):
	case (VIEW_EXTMMAXPDF):
	case (VIEW_EXTMMAXCDF):
	case (VIEW_EXTIMINPDF):
	case (VIEW_EXTIMINCDF):
		break;
	default:
		/* Bottom right. */
		cairo_move_to(cr, width - e.width, 
			height - e.height * 3.0);
		(void)snprintf(buf, sizeof(buf), fmt, miny);
		cairo_show_text(cr, buf);

		/* Middle-bottom right. */
		cairo_move_to(cr, width - e.width, 
			height * 0.75 - 1.5 * e.height);
		(void)snprintf(buf, sizeof(buf), 
			fmt, miny + (maxy - miny) * 0.25);
		cairo_show_text(cr, buf);

		/* Middle right. */
		cairo_move_to(cr, width - e.width, 
			height * 0.5 - 0.5 * e.height);
		(void)snprintf(buf, sizeof(buf), 
			fmt, miny + (maxy - miny) * 0.5);
		cairo_show_text(cr, buf);

		/* Middle-top right. */
		cairo_move_to(cr, width - e.width, height * 0.25);
		(void)snprintf(buf, sizeof(buf), 
			fmt, miny + (maxy - miny) * 0.75);
		cairo_show_text(cr, buf);

		/* Top right. */
		cairo_move_to(cr, width - e.width, e.height * 1.5);
		(void)snprintf(buf, sizeof(buf), fmt, maxy);
		cairo_show_text(cr, buf);

		*widthp -= e.width * 1.3;
		break;
	}

	switch (cur->view) {
	case (VIEW_POLYMINS):
	case (VIEW_MEANMINS):
	case (VIEW_EXTMMAXS):
	case (VIEW_EXTIMINS):
	case (VIEW_SMEANMINS):
	case (VIEW_ISLANDMEAN):
		break;
	default:
		/* Right bottom. */
		cairo_move_to(cr, width - e.width * 1.5, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), fmt, maxx);
		cairo_show_text(cr, buf);

		/* Middle-left bottom. */
		cairo_move_to(cr, width * 0.25 - e.width * 0.5, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			fmt, minx + (maxx - minx) * 0.25);
		cairo_show_text(cr, buf);

		/* Middle bottom. */
		cairo_move_to(cr, width * 0.5 - e.width * 0.75, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			fmt, minx + (maxx - minx) * 0.5);
		cairo_show_text(cr, buf);

		/* Middle-right bottom. */
		cairo_move_to(cr, width * 0.75 - e.width, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), 
			fmt, minx + (maxx - minx) * 0.75);
		cairo_show_text(cr, buf);

		/* Left bottom. */
		cairo_move_to(cr, e.width * 0.25, 
			height - e.height * 0.5);
		(void)snprintf(buf, sizeof(buf), fmt, minx);
		cairo_show_text(cr, buf);

		*heightp -= e.height * 3.0;
		break;
	}
}

/*
 * For a given simulation "s", compute the maximum "y" value in a graph
 * given the type of graph we want to produce.
 */
static void
max_sim(const struct curwin *cur, const struct sim *s, 
	double *xmin, double *xmax, double *maxy)
{
	size_t	 i;
	double	 v;

	switch (cur->view) {
	case (VIEW_CONFIG):
	case (VIEW_STATUS):
		return;
	case (VIEW_EXTIMINCDF):
	case (VIEW_EXTMMAXCDF):
	case (VIEW_POLYMINCDF):
	case (VIEW_SMEANMINCDF):
	case (VIEW_SEXTMMAXCDF):
	case (VIEW_MEANMINCDF):
		*maxy = 1.0;
		break;
	case (VIEW_DEV):
		for (i = 0; i < s->dims; i++) {
			v = stats_mean(&s->cold.stats[i]) +
				stats_stddev(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_EXTM):
		for (i = 0; i < s->dims; i++) {
			v = stats_extinctm(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_EXTI):
		for (i = 0; i < s->dims; i++) {
			v = stats_extincti(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_EXTIMINPDF):
		if (gsl_histogram_max_val(s->cold.extimins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.extimins);
		break;
	case (VIEW_EXTMMAXPDF):
		if (gsl_histogram_max_val(s->cold.extmmaxs) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.extmmaxs);
		break;
	case (VIEW_POLYMINPDF):
		if (gsl_histogram_max_val(s->cold.fitmins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.fitmins);
		break;
	case (VIEW_SEXTMMAXPDF):
		if (gsl_histogram_max_val(s->cold.sextmmaxs) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.sextmmaxs);
		break;
	case (VIEW_SMEANMINPDF):
		if (gsl_histogram_max_val(s->cold.smeanmins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.smeanmins);
		break;
	case (VIEW_MEANMINPDF):
		if (gsl_histogram_max_val(s->cold.meanmins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.meanmins);
		break;
	case (VIEW_MEANMINQ):
		v = GETS(s, s->cold.meanminq.vals[s->cold.meanminq.maxpos]);
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_POLYMINS):
		v = s->cold.fitminst.mean + s->cold.fitminst.stddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_MEANMINS):
		v = s->cold.meanminst.mean + s->cold.meanminst.stddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_EXTMMAXS):
		v = s->cold.extmmaxst.mean + s->cold.extmmaxst.stddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_EXTIMINS):
		v = s->cold.extiminst.mean + s->cold.extiminst.stddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_SMEANMINS):
		v = s->cold.smeanminst.mean + s->cold.smeanminst.stddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_SMEANMINQ):
		v = GETS(s, s->cold.smeanminq.vals[s->cold.smeanminq.maxpos]);
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_POLYMINQ):
		v = GETS(s, s->cold.fitminq.vals[s->cold.fitminq.maxpos]);
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_SEXTM):
		/* FIXME: keep this maxima */
		for (i = 0; i < s->dims; i++) {
			v = s->cold.sextms[i] > 
				stats_mean(&s->cold.stats[i]) ?
				s->cold.sextms[i] : 
				stats_mean(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_SMEAN):
		/* FIXME: keep this maxima */
		for (i = 0; i < s->dims; i++) {
			v = s->cold.smeans[i] > 
				stats_mean(&s->cold.stats[i]) ?
				s->cold.smeans[i] : 
				stats_mean(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_POLY):
		for (i = 0; i < s->dims; i++) {
			v = s->cold.fits[i] > 
				stats_mean(&s->cold.stats[i]) ?
				s->cold.fits[i] : 
				stats_mean(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_ISLANDMEAN):
		for (i = 0; i < s->islands; i++) {
			v = stats_mean(&s->cold.islands[i]) +
				stats_stddev(&s->cold.islands[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	default:
		for (i = 0; i < s->dims; i++) {
			v = stats_mean(&s->cold.stats[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	}

	if (*xmin > s->continuum.xmin)
		*xmin = s->continuum.xmin;
	if (*xmax < s->continuum.xmax)
		*xmax = s->continuum.xmax;
}

static void
drawlegendst(gchar *buf, size_t sz, 
	const struct sim *sim, const struct hstats *st)
{

	(void)g_snprintf(buf, sz,
		"%s: mode %g, mean %g +-%g", sim->name,
		st->mode, st->mean, st->stddev);
}

static void
drawlegendmax(gchar *buf, size_t sz,
	const struct sim *sim, size_t strat)
{

	(void)g_snprintf(buf, sz, "%s: max %g",
		sim->name, GETS(sim, strat));
}

static void
drawlegendmin(gchar *buf, size_t sz,
	const struct sim *sim, size_t strat)
{

	(void)g_snprintf(buf, sz, "%s: min %g",
		sim->name, GETS(sim, strat));
}

static double
drawlegend(struct bmigrate *b, struct curwin *cur, 
	cairo_t *cr, GList *sims, double height)
{
	GList		*list;
	gchar		 buf[1024];
	struct sim	*sim;
	size_t		 simmax;
	cairo_text_extents_t e;
	double		 v;

	if (VIEW_CONFIG == cur->view || VIEW_STATUS == cur->view)
		return(height);

	/*
	 * Create by drawing a legend.
	 * To do so, draw the colour representing the current simulation
	 * followed by the name of the simulation.
	 */
	simmax = 0;
	cairo_text_extents(cr, "lj", &e);
	for (list = sims; NULL != list; list = list->next, simmax++) {
		sim = list->data;
		/* Draw a line with the simulation's colour. */
		cairo_set_source_rgba(cr, GETC(1.0));
		v = height - simmax * e.height * 1.75 -
			e.height * 0.5 + 1.0;
		v -= e.height * 0.5;
		cairo_move_to(cr, 0.0, v);
		cairo_line_to(cr, 20.0, v);
		cairo_stroke(cr);
		/* Write the simulation name next to it. */
		v = height - simmax * e.height * 1.75;
		v -= e.height * 0.5;
		cairo_move_to(cr, 25.0, v);
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
		/*
		 * What we write depends on our simulation view.
		 * Sometimes we just write the name and number of runs.
		 * Sometimes we write more.
		 */
		switch (cur->view) {
		case (VIEW_DEV):
			drawlegendmin(buf, sizeof(buf),
				sim, sim->cold.meanmin);
			break;
		case (VIEW_EXTI):
			drawlegendmin(buf, sizeof(buf),
				sim, sim->cold.extimin);
			break;
		case (VIEW_EXTIMINCDF):
		case (VIEW_EXTIMINPDF):
		case (VIEW_EXTIMINS):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.extiminst);
			break;
		case (VIEW_EXTM):
			drawlegendmax(buf, sizeof(buf),
				sim, sim->cold.extmmax);
			break;
		case (VIEW_EXTMMAXCDF):
		case (VIEW_EXTMMAXPDF):
		case (VIEW_EXTMMAXS):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.extmmaxst);
			break;
		case (VIEW_ISLANDMEAN):
			g_strlcpy(buf, sim->name, sizeof(buf));
			break;
		case (VIEW_MEAN):
		case (VIEW_MEANMINQ):
			drawlegendmin(buf, sizeof(buf),
				sim, sim->cold.meanmin);
			break;
		case (VIEW_MEANMINCDF):
		case (VIEW_MEANMINPDF):
		case (VIEW_MEANMINS):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.meanminst);
			break;
		case (VIEW_POLY):
		case (VIEW_POLYMINQ):
			drawlegendmin(buf, sizeof(buf),
				sim, sim->cold.fitmin);
			break;
		case (VIEW_POLYMINCDF):
		case (VIEW_POLYMINPDF):
		case (VIEW_POLYMINS):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.fitminst);
			break;
		case (VIEW_SEXTM):
			drawlegendmax(buf, sizeof(buf),
				sim, sim->cold.sextmmax);
			break;
		case (VIEW_SEXTMMAXPDF):
		case (VIEW_SEXTMMAXCDF):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.sextmmaxst);
			break;
		case (VIEW_SMEAN):
		case (VIEW_SMEANMINQ):
			drawlegendmin(buf, sizeof(buf),
				sim, sim->cold.smeanmin);
			break;
		case (VIEW_SMEANMINCDF):
		case (VIEW_SMEANMINPDF):
		case (VIEW_SMEANMINS):
			drawlegendst(buf, sizeof(buf), 
				sim, &sim->cold.smeanminst);
			break;
		default:
			abort();
		}
		cairo_show_text(cr, buf);
	}

	/* Make space for our legend. */
	return(height - simmax * e.height * 1.75 - e.height);
}

static void
draw_set(const struct sim *sim, const struct bmigrate *b, cairo_t *cr, 
	double width, double height, double maxy, 
	size_t simnum, size_t simmax, const struct hstats *st)
{

	double	 v;

	v = width * (simnum + 1) / (double)(simmax + 1);
	cairo_move_to(cr, v, GETY(st->mean - st->stddev));
	cairo_line_to(cr, v, GETY(st->mean + st->stddev));
	cairo_set_source_rgba(cr, GETC(1.0));
	cairo_stroke(cr);
	cairo_new_path(cr);
	cairo_arc(cr, v, GETY(st->mean), 4.0, 0.0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, GETC(1.0));
	cairo_stroke_preserve(cr);
	cairo_set_source_rgba(cr, GETC(0.5));
	cairo_fill(cr);
}

/*
 * Draw a histogram CDF.
 */
static void
draw_cdf(const struct sim *sim, const struct bmigrate *b, 
	cairo_t *cr, double width, double height, double maxy, 
	const gsl_histogram *p, double minx, double maxx)
{
	double	v, sum;
	size_t	i;

	sum = gsl_histogram_sum(p);
	cairo_move_to(cr, GETX(0), GETY(0.0));
	for (v = 0.0, i = 0; i < sim->dims; i++) {
		v += gsl_histogram_get(p, i) / sum;
		cairo_line_to(cr, GETX(i), GETY(v));
	}
	cairo_set_source_rgba(cr, GETC(1.0));
	cairo_stroke(cr);
}

/*
 * Draw a histogram PDF.
 */
static void
draw_pdf(const struct sim *sim, const struct bmigrate *b, 
	cairo_t *cr, double width, double height, double maxy, 
	const gsl_histogram *p, double minx, double maxx)
{
	double	v;
	size_t	i;

	for (i = 1; i < sim->dims; i++) {
		v = gsl_histogram_get(p, i - 1);
		cairo_move_to(cr, GETX(i-1), GETY(v));
		v = gsl_histogram_get(p, i);
		cairo_line_to(cr, GETX(i), GETY(v));
	}
	cairo_set_source_rgba(cr, GETC(1.0));
	cairo_stroke(cr);
}

/*
 * Draw the standard deviation line.
 */
static void
draw_stddev(const struct sim *sim, const struct bmigrate *b,
	cairo_t *cr, double width, double height, double maxy,
	double minx, double maxx)
{
	size_t	 i;
	double	 v;

	for (i = 1; i < sim->dims; i++) {
		v = stats_mean(&sim->cold.stats[i - 1]) -
			stats_stddev(&sim->cold.stats[i - 1]);
		if (v < 0.0)
			v = 0.0;
		cairo_move_to(cr, GETX(i-1), GETY(v));
		v = stats_mean(&sim->cold.stats[i]) -
			stats_stddev(&sim->cold.stats[i]);
		if (v < 0.0)
			v = 0.0;
		cairo_line_to(cr, GETX(i), GETY(v));
		v = stats_mean(&sim->cold.stats[i - 1]) +
			stats_stddev(&sim->cold.stats[i - 1]);
		cairo_move_to(cr, GETX(i-1), GETY(v));
		v = stats_mean(&sim->cold.stats[i]) +
			stats_stddev(&sim->cold.stats[i]);
		cairo_line_to(cr, GETX(i), GETY(v));
	}
}

static void
draw_islandmean(const struct sim *sim, const struct bmigrate *b,
	cairo_t *cr, double width, double height, double maxy)
{
	size_t	 i;
	double	 v;

	for (i = 0; i < sim->islands; i++) {
		v = stats_mean(&sim->cold.islands[i]) -
			stats_stddev(&sim->cold.islands[i]);
		if (v < 0.0)
			v = 0.0;
		cairo_move_to(cr, width * (i + 1) / 
			(double)(sim->islands + 1), GETY(v));
		v = stats_mean(&sim->cold.islands[i]) +
			stats_stddev(&sim->cold.islands[i]);
		cairo_line_to(cr, width * (i + 1) / 
			(double)(sim->islands + 1), GETY(v));
		cairo_set_source_rgba(cr, GETC(1.0));
		cairo_stroke(cr);
		cairo_new_path(cr);
		v = stats_mean(&sim->cold.islands[i]);
		cairo_arc(cr, width * (i + 1) /
			(double)(sim->islands + 1), GETY(v), 
			3.0, 0.0, 2.0 * M_PI);
		cairo_set_source_rgba(cr, GETC(1.0));
		cairo_stroke_preserve(cr);
		cairo_set_source_rgba(cr, GETC(0.5));
		cairo_fill(cr);
	}
}

/*
 * Draw the mean line.
 */
static void
draw_mean(const struct sim *sim, const struct bmigrate *b,
	cairo_t *cr, double width, double height, 
	double maxy, double minx, double maxx)
{
	size_t	 i;
	double	 v;

	for (i = 1; i < sim->dims; i++) {
		v = stats_mean(&sim->cold.stats[i - 1]);
		cairo_move_to(cr, GETX(i-1), GETY(v));
		v = stats_mean(&sim->cold.stats[i]);
		cairo_line_to(cr, GETX(i), GETY(v));
	}
}

/*
 * Draw the fitted polynomial line.
 */
static void
draw_poly(const struct sim *sim, const struct bmigrate *b,
	cairo_t *cr, double width, double height, 
	double maxy, double minx, double maxx)
{
	size_t	 i;
	double	 v;

	for (i = 1; i < sim->dims; i++) {
		v = sim->cold.fits[i - 1];
		cairo_move_to(cr, GETX(i-1), GETY(v));
		v = sim->cold.fits[i];
		cairo_line_to(cr, GETX(i), GETY(v));
	}
}

static void
draw_cqueue(const struct sim *sim, const struct bmigrate *b,
	cairo_t *cr, double width, double height, double maxy,
	const struct cqueue *q, const struct hstats *st)
{
	size_t	 i, j;
	double	 v;
	static const double dash[] = {6.0};

	for (i = 1; i < CQUEUESZ; i++) {
		j = (q->pos + i - 1) % CQUEUESZ;
		v = GETS(sim, q->vals[j]);
		cairo_move_to(cr, width * (i - 1) / 
			(double)CQUEUESZ, GETY(v));
		j = (q->pos + i) % CQUEUESZ;
		v = GETS(sim, q->vals[j]);
		cairo_line_to(cr, width * i / 
			(double)CQUEUESZ, GETY(v));
	}
	cairo_set_source_rgba(cr, GETC(1.0));
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.0);
	v = st->mode;
	cairo_move_to(cr, 0.0, GETY(v));
	cairo_line_to(cr, width, GETY(v));
	cairo_set_dash(cr, dash, 1, 0);
	cairo_set_source_rgba(cr, GETC(0.75));
	cairo_stroke(cr);
	v = st->mean;
	cairo_move_to(cr, 0.0, GETY(v));
	cairo_line_to(cr, width, GETY(v));
	cairo_set_dash(cr, dash, 0, 0);
	cairo_set_source_rgba(cr, GETC(0.75));
	cairo_stroke(cr);
}

static void
drawinfo(cairo_t *cr, double *y, 
	const cairo_text_extents_t *e, const char *fmt, ...)
{
	va_list		 ap;
	char		 buf[1024];

	va_start(ap, fmt);
	(void)g_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	cairo_move_to(cr, 0.0, *y);
	cairo_show_text(cr, buf);
	*y += e->height * 1.5;
}

/*
 * Main draw event.
 * There's lots we can do to make this more efficient, e.g., computing
 * the stddev/fitpoly arrays in the worker threads instead of here.
 */
void
draw(GtkWidget *w, cairo_t *cr, struct curwin *cur)
{
	double		 width, height, maxy, v, minx, maxx;
	struct bmigrate	*b;
	GtkWidget	*top;
	struct sim	*sim;
	size_t		 i, simnum, simmax;
	GList		*sims, *list;
	cairo_text_extents_t e;
	gchar		 buf[1024];

	cairo_set_font_size(cr, 12.0);

	/* 
	 * Get our window configuration.
	 * Then get our list of simulations.
	 * Both of these are stored as pointers to the top-level window.
	 */
	top = gtk_widget_get_toplevel(w);
	cur->redraw = 0;
	sims = cur->sims;
	assert(NULL != sims);
	b = cur->b;

	/* 
	 * Initialise the window view to be all white. 
	 */
	width = gtk_widget_get_allocated_width(w);
	height = gtk_widget_get_allocated_height(w);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, width, height);
	cairo_fill(cr);

	/*
	 * Create by drawing a legend.
	 * To do so, draw the colour representing the current simulation
	 * followed by the name of the simulation.
	 */
	height = drawlegend(b, cur, cr, sims, height);

	/*
	 * Determine the boundaries of the graph.
	 * These will, for all graphs in our set, compute the range and
	 * domain extrema (the range minimum is always zero).
	 * Buffer the maximum range by 110%.
	 */
	minx = FLT_MAX;
	maxx = maxy = -FLT_MAX;
	simmax = 0;
	for (list = sims; NULL != list; list = list->next, simmax++)
		max_sim(cur, list->data, &minx, &maxx, &maxy);

	/* 
	 * See if we should scale the graph to be above the maximum
	 * point, just for aesthetics.
	 * CDFs and the configuration window don't change.
	 */
	switch (cur->view) {
	case (VIEW_EXTMMAXCDF):
	case (VIEW_EXTIMINCDF):
	case (VIEW_POLYMINCDF):
	case (VIEW_MEANMINCDF):
	case (VIEW_SMEANMINCDF):
	case (VIEW_SEXTMMAXCDF):
	case (VIEW_CONFIG):
	case (VIEW_STATUS):
		break;
	default:
		maxy += maxy * 0.1;
	}

	/*
	 * Configure our labels.
	 * History views show the last n updates instead of the domain
	 * of the x-axis, and the format is for non-decimals.
	 * Configuration has no label at all.
	 */
	memset(&e, 0, sizeof(cairo_text_extents_t));
	switch (cur->view) {
	case (VIEW_CONFIG):
	case (VIEW_STATUS):
		cairo_text_extents(cr, "lj", &e);
		break;
	case (VIEW_SMEANMINQ):
	case (VIEW_POLYMINQ):
	case (VIEW_MEANMINQ):
		drawlabels(cur, cr, "%g", &width, 
			&height, 0.0, maxy, -1.0 * CQUEUESZ, 0.0);
		break;
	default:
		drawlabels(cur, cr, "%.2g", &width, 
			&height, 0.0, maxy, minx, maxx);
		break;
	}

	/*
	 * Finally, draw our curves.
	 * There are many ways of doing this.
	 */
	simnum = 0;
	v = e.height;
	cairo_save(cr);
	for (list = sims; NULL != list; list = list->next, simnum++) {
		sim = list->data;
		assert(NULL != sim);
		assert(simnum < simmax);
		cairo_set_line_width(cr, 2.0);
		switch (cur->view) {
		case (VIEW_CONFIG):
			v += e.height;
			cairo_move_to(cr, 0.0, v - e.height * 0.5 - 1.0);
			/* Draw a line with the simulation's colour. */
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_rel_line_to(cr, 20.0, 0.0);
			cairo_stroke(cr);
			/* Write the simulation name next to it. */
			cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
			(void)g_snprintf(buf, sizeof(buf),
				"Name: %s", sim->name);
			cairo_move_to(cr, 25.0, v);
			cairo_show_text(cr, buf);
			v += e.height * 1.5;
			drawinfo(cr, &v, &e, "Function: %s, "
				"x = [%g, %g], T=%zu, lambda=%g(1 + %g * pi)", 
				sim->func, 
				sim->continuum.xmin,
				sim->continuum.xmax, sim->stop,
				sim->alpha, sim->delta);
			drawinfo(cr, &v, &e, "Population: %zu (%zu "
				"islands, %suniform), m=%g (%suniform)", 
				sim->totalpop, sim->islands, 
				NULL != sim->pops ? "non-" : "", 
				sim->m, NULL != sim->ms ? "non-" : "");
			if (MUTANTS_DISCRETE == sim->mutants)
				drawinfo(cr, &v, &e, "Mutants: "
					"discrete (%zu)", sim->dims);
			else
				drawinfo(cr, &v, &e, "Mutants: "
					"Gaussian (sigma=%g, "
					"[%g, %g])", sim->mutantsigma,
					sim->continuum.ymin,
					sim->continuum.ymax);
			drawinfo(cr, &v, &e, "Fit: order %zu (%s)",
				sim->fitpoly, 0 == sim->fitpoly ?
				"disabled" : (sim->weighted ? 
				"weighted" : "unweighted"));
			break;
		case (VIEW_STATUS):
			v += e.height;
			cairo_move_to(cr, 0.0, v - e.height * 0.5 - 1.0);
			/* Draw a line with the simulation's colour. */
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_rel_line_to(cr, 20.0, 0.0);
			cairo_stroke(cr);
			/* Write the simulation name next to it. */
			cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
			(void)g_snprintf(buf, sizeof(buf),
				"Name: %s", sim->name);
			cairo_move_to(cr, 25.0, v);
			cairo_show_text(cr, buf);
			v += e.height * 1.5;
			drawinfo(cr, &v, &e, "Runs: %" 
				PRIu64, sim->cold.truns);
			drawinfo(cr, &v, &e, "Generations: %" 
				PRIu64, sim->cold.tgens);
			break;
		case (VIEW_DEV):
			draw_mean(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			draw_stddev(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_line_width(cr, 1.5);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			break;
		case (VIEW_POLY):
			draw_mean(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			draw_poly(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_line_width(cr, 1.5);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			break;
		case (VIEW_POLYMINPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.fitmins, minx, maxx);
			break;
		case (VIEW_POLYMINCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.fitmins, minx, maxx);
			break;
		case (VIEW_MEANMINPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.meanmins, minx, maxx);
			break;
		case (VIEW_MEANMINCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.meanmins, minx, maxx);
			break;
		case (VIEW_MEANMINQ):
			draw_cqueue(sim, b, cr, width, height, maxy,
				&sim->cold.meanminq, &sim->cold.meanminst);
			break;
		case (VIEW_POLYMINS):
			draw_set(sim, b, cr, width, height, maxy, 
				simnum, simmax, &sim->cold.fitminst);
			break;
		case (VIEW_EXTIMINS):
			draw_set(sim, b, cr, width, height, maxy, 
				simnum, simmax, &sim->cold.extiminst);
			break;
		case (VIEW_EXTMMAXS):
			draw_set(sim, b, cr, width, height, maxy, 
				simnum, simmax, &sim->cold.extmmaxst);
			break;
		case (VIEW_SMEANMINS):
			draw_set(sim, b, cr, width, height, maxy, 
				simnum, simmax, &sim->cold.smeanminst);
			break;
		case (VIEW_MEANMINS):
			draw_set(sim, b, cr, width, height, maxy, 
				simnum, simmax, &sim->cold.meanminst);
			break;
		case (VIEW_SMEANMINQ):
			draw_cqueue(sim, b, cr, width, height, maxy,
				&sim->cold.smeanminq, &sim->cold.smeanminst);
			break;
		case (VIEW_POLYMINQ):
			draw_cqueue(sim, b, cr, width, height, maxy,
				&sim->cold.fitminq, &sim->cold.fitminst);
			break;
		case (VIEW_EXTI):
			for (i = 1; i < sim->dims; i++) {
				v = stats_extincti(&sim->cold.stats[i - 1]);
				cairo_move_to(cr, GETX(i-1), GETY(v));
				v = stats_extincti(&sim->cold.stats[i]);
				cairo_line_to(cr, GETX(i), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTM):
			for (i = 1; i < sim->dims; i++) {
				v = stats_extinctm(&sim->cold.stats[i - 1]);
				cairo_move_to(cr, GETX(i-1), GETY(v));
				v = stats_extinctm(&sim->cold.stats[i]);
				cairo_line_to(cr, GETX(i), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTIMINCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.extimins, minx, maxx);
			break;
		case (VIEW_EXTIMINPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.extimins, minx, maxx);
			break;
		case (VIEW_EXTMMAXCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.extmmaxs, minx, maxx);
			break;
		case (VIEW_EXTMMAXPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.extmmaxs, minx, maxx);
			break;
		case (VIEW_SEXTMMAXCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.sextmmaxs, minx, maxx);
			break;
		case (VIEW_SEXTMMAXPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.sextmmaxs, minx, maxx);
			break;
		case (VIEW_SMEANMINCDF):
			draw_cdf(sim, b, cr, width, height, 
				maxy, sim->cold.smeanmins, minx, maxx);
			break;
		case (VIEW_SMEANMINPDF):
			draw_pdf(sim, b, cr, width, height, 
				maxy, sim->cold.smeanmins, minx, maxx);
			break;
		case (VIEW_SEXTM):
			for (i = 1; i < sim->dims; i++) {
				v = stats_extinctm(&sim->cold.stats[i - 1]);
				cairo_move_to(cr, GETX(i-1), GETY(v));
				v = stats_extinctm(&sim->cold.stats[i]);
				cairo_line_to(cr, GETX(i), GETY(v));
			}
			cairo_set_line_width(cr, 1.5);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (i = 1; i < sim->dims; i++) {
				v = sim->cold.sextms[i - 1];
				cairo_move_to(cr, GETX(i-1), GETY(v));
				v = sim->cold.sextms[i];
				cairo_line_to(cr, GETX(i), GETY(v));
			}
			cairo_set_line_width(cr, 2.0);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_SMEAN):
			draw_mean(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_line_width(cr, 1.5);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (i = 1; i < sim->dims; i++) {
				v = sim->cold.smeans[i - 1];
				cairo_move_to(cr, GETX(i-1), GETY(v));
				v = sim->cold.smeans[i];
				cairo_line_to(cr, GETX(i), GETY(v));
			}
			cairo_set_line_width(cr, 2.0);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_ISLANDMEAN):
			draw_islandmean(sim, b, cr, width, height, maxy);
			break;
		default:
			draw_mean(sim, b, cr, width, 
				height, maxy, minx, maxx);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		}
	}

	cairo_restore(cr);
	if (VIEW_CONFIG != cur->view && VIEW_STATUS != cur->view)
		drawgrid(cr, width, height);
}

