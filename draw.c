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
drawgrid(cairo_t *cr, double width, double height)
{
	static const double dash[] = {6.0};

	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_set_line_width(cr, 0.2);

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

	cairo_stroke(cr);
	cairo_set_dash(cr, dash, 1, 0);

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

	cairo_stroke(cr);
	cairo_set_dash(cr, dash, 0, 0);
}

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
	case (VIEW_EXTIMINCDF):
		if (gsl_histogram_sum(s->cold.extimins) > *maxy)
			*maxy = gsl_histogram_sum(s->cold.extimins);
		break;
	case (VIEW_EXTMMAXPDF):
		if (gsl_histogram_max_val(s->cold.extmmaxs) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.extmmaxs);
		break;
	case (VIEW_EXTMMAXCDF):
		if (gsl_histogram_sum(s->cold.extmmaxs) > *maxy)
			*maxy = gsl_histogram_sum(s->cold.extmmaxs);
		break;
	case (VIEW_POLYMINPDF):
		if (gsl_histogram_max_val(s->cold.fitmins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.fitmins);
		break;
	case (VIEW_POLYMINCDF):
		if (gsl_histogram_sum(s->cold.fitmins) > *maxy)
			*maxy = gsl_histogram_sum(s->cold.fitmins);
		break;
	case (VIEW_MEANMINPDF):
		if (gsl_histogram_max_val(s->cold.meanmins) > *maxy)
			*maxy = gsl_histogram_max_val(s->cold.meanmins);
		break;
	case (VIEW_MEANMINCDF):
		if (gsl_histogram_sum(s->cold.meanmins) > *maxy)
			*maxy = gsl_histogram_sum(s->cold.meanmins);
		break;
	case (VIEW_MEANMINQ):
		for (i = 0; i < MINQSZ; i++) {
			v = GETS(s, s->cold.meanminq[i]);
			if (v > *maxy)
				*maxy = v;
		}
		break;
	case (VIEW_POLYMINS):
		v = s->cold.fitminsmean + s->cold.fitminsstddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_MEANMINS):
		v = s->cold.meanminsmean + s->cold.meanminsstddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_EXTMMAXS):
		v = s->cold.extmmaxsmean + s->cold.extmmaxsstddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_EXTIMINS):
		v = s->cold.extiminsmean + s->cold.extiminsstddev;
		if (v > *maxy)
			*maxy = v;
		break;
	case (VIEW_POLYMINQ):
		for (i = 0; i < MINQSZ; i++) {
			v = GETS(s, s->cold.fitminq[i]);
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
	case (VIEW_POLYDEV):
		for (i = 0; i < s->dims; i++) {
			v = s->cold.fits[i] > 
				stats_mean(&s->cold.stats[i]) + 
				stats_stddev(&s->cold.stats[i]) ?
				s->cold.fits[i] : 
				stats_mean(&s->cold.stats[i]) +
				stats_stddev(&s->cold.stats[i]);
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
	}

	if (*xmin > s->d.continuum.xmin)
		*xmin = s->d.continuum.xmin;
	if (*xmax < s->d.continuum.xmax)
		*xmax = s->d.continuum.xmax;
}

/*
 * Main draw event.
 * There's lots we can do to make this more efficient, e.g., computing
 * the stddev/fitpoly arrays in the worker threads instead of here.
 */
void
draw(GtkWidget *w, cairo_t *cr, struct bmigrate *b)
{
	struct curwin	*cur;
	double		 width, height, maxy, v, xmin, xmax;
	GtkWidget	*top;
	struct sim	*sim;
	size_t		 j, k, simnum, simmax;
	GList		*sims, *list;
	cairo_text_extents_t e;
	gchar		 buf[1024];
	static const double dash[] = {6.0};

	/* 
	 * Get our window configuration.
	 * Then get our list of simulations.
	 * Both of these are stored as pointers to the top-level window.
	 */
	top = gtk_widget_get_toplevel(w);
	cur = g_object_get_data(G_OBJECT(top), "cfg");
	assert(NULL != cur);
	sims = g_object_get_data(G_OBJECT(top), "sims");
	assert(NULL != sims);

	/* 
	 * Initialise the window view to be all white. 
	 */
	width = gtk_widget_get_allocated_width(w);
	height = gtk_widget_get_allocated_height(w);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, width, height);
	cairo_fill(cr);

	/*
	 * These macros shorten the drawing routines by automatically
	 * computing X and Y coordinates as well as colour.
	 */
#define	GETX(_j) (width * (_j) / (double)(sim->dims - 1))
#define	GETY(_v) (height - ((_v) / maxy * height))
#define	GETC(_a) b->wins.colours[sim->colour].red, \
		 b->wins.colours[sim->colour].green, \
		 b->wins.colours[sim->colour].blue, (_a)

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
		case (VIEW_POLYMINCDF):
		case (VIEW_POLYMINPDF):
		case (VIEW_POLYMINQ):
		case (VIEW_POLYMINS):
			(void)g_snprintf(buf, sizeof(buf), 
				"%s: mode %g, mean %g (+-%g), "
				"runs %" PRIu64, sim->name,
				sim->cold.fitminsmode,
				sim->cold.fitminsmean, 
				sim->cold.fitminsstddev, 
				sim->cold.truns);
			break;
		case (VIEW_EXTIMINCDF):
		case (VIEW_EXTIMINPDF):
		case (VIEW_EXTIMINS):
			(void)g_snprintf(buf, sizeof(buf), 
				"%s: mode %g, mean %g (+-%g), "
				"runs %" PRIu64, sim->name,
				sim->cold.extiminsmode,
				sim->cold.extiminsmean, 
				sim->cold.extiminsstddev, 
				sim->cold.truns);
			break;
		case (VIEW_EXTMMAXCDF):
		case (VIEW_EXTMMAXPDF):
		case (VIEW_EXTMMAXS):
			(void)g_snprintf(buf, sizeof(buf), 
				"%s: mode %g, mean %g (+-%g), "
				"runs %" PRIu64, sim->name,
				sim->cold.extmmaxsmode,
				sim->cold.extmmaxsmean, 
				sim->cold.extmmaxsstddev, 
				sim->cold.truns);
			break;
		case (VIEW_MEANMINCDF):
		case (VIEW_MEANMINPDF):
		case (VIEW_MEANMINQ):
		case (VIEW_MEANMINS):
			(void)g_snprintf(buf, sizeof(buf), 
				"%s: mode %g, mean %g (+-%g), "
				"runs %" PRIu64, sim->name,
				sim->cold.meanminsmode,
				sim->cold.meanminsmean, 
				sim->cold.meanminsstddev, 
				sim->cold.truns);
			break;
		default:
			(void)g_snprintf(buf, sizeof(buf), 
				"%s: runs %" PRIu64, 
				sim->name, sim->cold.truns);
			break;
		}
		cairo_show_text(cr, buf);
	}

	/* Make space for our legend. */
	height -= simmax * e.height * 1.75 + e.height * 0.5;

	/*
	 * Determine the boundaries of the graph.
	 * These will, for all graphs in our set, compute the range and
	 * domain extrema (the range minimum is always zero).
	 * Buffer the maximum range by 110%.
	 */
	xmin = FLT_MAX;
	xmax = maxy = -FLT_MAX;
	for (list = sims; NULL != list; list = list->next)
		max_sim(cur, list->data, &xmin, &xmax, &maxy);

	/* CDF's don't get their windows scaled. */
	switch (cur->view) {
	case (VIEW_POLYMINCDF):
	case (VIEW_MEANMINCDF):
		break;
	default:
		maxy += maxy * 0.1;
	}

	/*
	 * History views show the last n updates instead of the domain
	 * of the x-axis.
	 */
	switch (cur->view) {
	case (VIEW_POLYMINQ):
	case (VIEW_MEANMINQ):
		drawlabels(cur, cr, "%g", &width, 
			&height, 0.0, maxy, -256.0, 0.0);
		break;
	default:
		drawlabels(cur, cr, "%.2g", &width, 
			&height, 0.0, maxy, xmin, xmax);
		break;
	}

	/*
	 * Finally, draw our curves.
	 * There are many ways of doing this.
	 */
	simnum = 0;
	cairo_save(cr);
	for (list = sims; NULL != list; list = list->next, simnum++) {
		sim = list->data;
		assert(NULL != sim);
		assert(simnum < simmax);
		cairo_set_line_width(cr, 2.0);
		switch (cur->view) {
		case (VIEW_DEV):
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.5);
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				v -= stats_stddev(&sim->cold.stats[j - 1]);
				if (v < 0.0)
					v = 0.0;
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				v -= stats_stddev(&sim->cold.stats[j]);
				if (v < 0.0)
					v = 0.0;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				v += stats_stddev(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				v += stats_stddev(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			break;
		case (VIEW_POLY):
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.5);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.fits[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.fits[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			break;
		case (VIEW_POLYDEV):
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.5);
			for (j = 1; j < sim->dims; j++) {
				v = sim->cold.fits[j - 1];
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = sim->cold.fits[j];
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.5);
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				v -= stats_stddev(&sim->cold.stats[j - 1]);
				if (v < 0.0)
					v = 0.0;
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				v -= stats_stddev(&sim->cold.stats[j]);
				if (v < 0.0)
					v = 0.0;
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				v += stats_stddev(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				v += stats_stddev(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_stroke(cr);
			break;
		case (VIEW_POLYMINPDF):
			for (j = 1; j < sim->dims; j++) {
				v = gsl_histogram_get
					(sim->cold.fitmins, j - 1);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = gsl_histogram_get
					(sim->cold.fitmins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_POLYMINCDF):
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += gsl_histogram_get
					(sim->cold.fitmins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_MEANMINPDF):
			for (j = 1; j < sim->dims; j++) {
				v = gsl_histogram_get
					(sim->cold.meanmins, j - 1);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = gsl_histogram_get
					(sim->cold.meanmins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_MEANMINCDF):
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += gsl_histogram_get
					(sim->cold.meanmins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_MEANMINQ):
			for (j = 1; j < MINQSZ; j++) {
				k = (sim->cold.meanminqpos + j - 1) % 
					MINQSZ;
				v = GETS(sim, sim->cold.meanminq[k]);
				cairo_move_to(cr, width * (j - 1) / 
					(double)MINQSZ, GETY(v));
				k = (sim->cold.meanminqpos + j) % MINQSZ;
				v = GETS(sim, sim->cold.meanminq[k]);
				cairo_line_to(cr, width * j / 
					(double)MINQSZ, GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.0);
			v = sim->cold.meanminsmode;
			cairo_move_to(cr, 0.0, GETY(v));
			cairo_line_to(cr, width, GETY(v));
			cairo_set_dash(cr, dash, 1, 0);
			cairo_set_source_rgba(cr, GETC(0.75));
			cairo_stroke(cr);
			v = sim->cold.meanminsmean;
			cairo_move_to(cr, 0.0, GETY(v));
			cairo_line_to(cr, width, GETY(v));
			cairo_set_dash(cr, dash, 0, 0);
			cairo_set_source_rgba(cr, GETC(0.75));
			cairo_stroke(cr);
			break;
		case (VIEW_POLYMINS):
			v = width * (simnum + 1) / (double)(simmax + 1);
			cairo_move_to(cr, v, GETY
				(sim->cold.fitminsmean -
				 sim->cold.fitminsstddev));
			cairo_line_to(cr, v, GETY
				(sim->cold.fitminsmean +
				 sim->cold.fitminsstddev));
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_new_path(cr);
			cairo_arc(cr, v, GETY(sim->cold.fitminsmean),
				4.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_fill(cr);
			break;
		case (VIEW_EXTIMINS):
			v = width * (simnum + 1) / (double)(simmax + 1);
			cairo_move_to(cr, v, GETY
				(sim->cold.extiminsmean -
				 sim->cold.extiminsstddev));
			cairo_line_to(cr, v, GETY
				(sim->cold.extiminsmean +
				 sim->cold.extiminsstddev));
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_new_path(cr);
			cairo_arc(cr, v, GETY(sim->cold.extiminsmean),
				4.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_fill(cr);
			break;
		case (VIEW_EXTMMAXS):
			v = width * (simnum + 1) / (double)(simmax + 1);
			cairo_move_to(cr, v, GETY
				(sim->cold.extmmaxsmean -
				 sim->cold.extmmaxsstddev));
			cairo_line_to(cr, v, GETY
				(sim->cold.extmmaxsmean +
				 sim->cold.extmmaxsstddev));
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_new_path(cr);
			cairo_arc(cr, v, GETY(sim->cold.extmmaxsmean),
				4.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_fill(cr);
			break;
		case (VIEW_MEANMINS):
			v = width * (simnum + 1) / (double)(simmax + 1);
			cairo_move_to(cr, v, GETY
				(sim->cold.meanminsmean -
				 sim->cold.meanminsstddev));
			cairo_line_to(cr, v, GETY
				(sim->cold.meanminsmean +
				 sim->cold.meanminsstddev));
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_new_path(cr);
			cairo_arc(cr, v, GETY(sim->cold.meanminsmean),
				4.0, 0.0, 2.0 * M_PI);
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke_preserve(cr);
			cairo_set_source_rgba(cr, GETC(0.5));
			cairo_fill(cr);
			break;
		case (VIEW_POLYMINQ):
			for (j = 1; j < MINQSZ; j++) {
				k = (sim->cold.fitminqpos + j - 1) % 
					MINQSZ;
				v = GETS(sim, sim->cold.fitminq[k]);
				cairo_move_to(cr, width * (j - 1) / 
					(double)MINQSZ, GETY(v));
				k = (sim->cold.fitminqpos + j) % MINQSZ;
				v = GETS(sim, sim->cold.fitminq[k]);
				cairo_line_to(cr, width * j / 
					(double)MINQSZ, GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 1.0);
			v = sim->cold.fitminsmode;
			cairo_move_to(cr, 0.0, GETY(v));
			cairo_line_to(cr, width, GETY(v));
			cairo_set_dash(cr, dash, 1, 0);
			cairo_set_source_rgba(cr, GETC(0.75));
			cairo_stroke(cr);
			v = sim->cold.fitminsmean;
			cairo_move_to(cr, 0.0, GETY(v));
			cairo_line_to(cr, width, GETY(v));
			cairo_set_dash(cr, dash, 0, 0);
			cairo_set_source_rgba(cr, GETC(0.75));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTI):
			for (j = 1; j < sim->dims; j++) {
				v = stats_extincti(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_extincti(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTM):
			for (j = 1; j < sim->dims; j++) {
				v = stats_extinctm(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_extinctm(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTIMINCDF):
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += gsl_histogram_get
					(sim->cold.extimins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTIMINPDF):
			for (j = 1; j < sim->dims; j++) {
				v = gsl_histogram_get
					(sim->cold.extimins, j - 1);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = gsl_histogram_get
					(sim->cold.extimins, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTMMAXCDF):
			cairo_move_to(cr, GETX(0), GETY(0.0));
			for (v = 0.0, j = 0; j < sim->dims; j++) {
				v += gsl_histogram_get
					(sim->cold.extmmaxs, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		case (VIEW_EXTMMAXPDF):
			for (j = 1; j < sim->dims; j++) {
				v = gsl_histogram_get
					(sim->cold.extmmaxs, j - 1);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = gsl_histogram_get
					(sim->cold.extmmaxs, j);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		default:
			for (j = 1; j < sim->dims; j++) {
				v = stats_mean(&sim->cold.stats[j - 1]);
				cairo_move_to(cr, GETX(j-1), GETY(v));
				v = stats_mean(&sim->cold.stats[j]);
				cairo_line_to(cr, GETX(j), GETY(v));
			}
			cairo_set_source_rgba(cr, GETC(1.0));
			cairo_stroke(cr);
			break;
		}
	}
	cairo_restore(cr);

	/* Draw a little grid for reference points. */
	drawgrid(cr, width, height);
}

