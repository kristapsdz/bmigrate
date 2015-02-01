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
#include <kplot.h>

#include "extern.h"

#define	GETC(_a) b->wins.colours[sim->colour].red, \
		 b->wins.colours[sim->colour].green, \
		 b->wins.colours[sim->colour].blue, (_a)

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
	double		 width, height, v;
	struct bmigrate	*b;
	struct sim	*sim;
	size_t		 simnum;
	GList		*sims, *list;
	cairo_text_extents_t e;
	gchar		 buf[1024];

	/* 
	 * Get our window configuration.
	 * Then get our list of simulations.
	 * Both of these are stored as pointers to the top-level window.
	 */
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

	switch (cur->view) {
	case (VIEW_POLY):
		kplot_draw(cur->view_poly, width, height, cr, NULL);
		return;
	case (VIEW_MEAN):
		kplot_draw(cur->view_mean, width, height, cr, NULL);
		return;
	case (VIEW_EXTM):
		kplot_draw(cur->view_mextinct, width, height, cr, NULL);
		return;
	case (VIEW_EXTI):
		kplot_draw(cur->view_iextinct, width, height, cr, NULL);
		return;
	case (VIEW_DEV):
		kplot_draw(cur->view_stddev, width, height, cr, NULL);
		return;
	case (VIEW_SMEAN):
		kplot_draw(cur->view_smean, width, height, cr, NULL);
		return;
	case (VIEW_SEXTM):
		kplot_draw(cur->view_smextinct, width, height, cr, NULL);
		return;
	case (VIEW_MEANMINCDF):
		kplot_draw(cur->view_meanmins_cdf, width, height, cr, NULL);
		return;
	case (VIEW_MEANMINPDF):
		kplot_draw(cur->view_meanmins_pdf, width, height, cr, NULL);
		return;
	case (VIEW_EXTMMAXCDF):
		kplot_draw(cur->view_mextinctmaxs_cdf, width, height, cr, NULL);
		return;
	case (VIEW_EXTMMAXPDF):
		kplot_draw(cur->view_mextinctmaxs_pdf, width, height, cr, NULL);
		return;
	case (VIEW_EXTIMINCDF):
		kplot_draw(cur->view_iextinctmins_cdf, width, height, cr, NULL);
		return;
	case (VIEW_EXTIMINPDF):
		kplot_draw(cur->view_iextinctmins_pdf, width, height, cr, NULL);
		return;
	case (VIEW_POLYMINCDF):
		kplot_draw(cur->view_fitpolymins_cdf, width, height, cr, NULL);
		return;
	case (VIEW_POLYMINPDF):
		kplot_draw(cur->view_fitpolymins_pdf, width, height, cr, NULL);
		return;
	case (VIEW_MEANMINQ):
		kplot_draw(cur->view_meanminq, width, height, cr, NULL);
		return;
	case (VIEW_POLYMINQ):
		kplot_draw(cur->view_fitminq, width, height, cr, NULL);
		return;
	case (VIEW_ISLANDMEAN):
		kplot_draw(cur->view_islands, width, height, cr, NULL);
		return;
	case (VIEW_MEANMINS):
		kplot_draw(cur->view_winmeans, width, height, cr, NULL);
		return;
	case (VIEW_POLYMINS):
		kplot_draw(cur->view_winfitmeans, width, height, cr, NULL);
		return;
	case (VIEW_EXTMMAXS):
		kplot_draw(cur->view_winmextinctmeans, width, height, cr, NULL);
		return;
	case (VIEW_EXTIMINS):
		kplot_draw(cur->view_winiextinctmeans, width, height, cr, NULL);
		return;
	default:
		break;
	}

	cairo_set_font_size(cr, 12.0);

	memset(&e, 0, sizeof(cairo_text_extents_t));
	cairo_text_extents(cr, "lj", &e);

	simnum = 0;
	v = e.height;
	cairo_save(cr);
	for (list = sims; NULL != list; list = list->next, simnum++) {
		sim = list->data;
		assert(NULL != sim);
		cairo_set_line_width(cr, 2.0);
		switch (cur->view) {
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
		default:
			abort();
			break;
		}
	}

	cairo_restore(cr);
}

