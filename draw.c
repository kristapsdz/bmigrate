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
#include <string.h>

#include <cairo.h>
#include <gtk/gtk.h>
#include <gsl/gsl_multifit.h>
#include <kplot.h>

#include "extern.h"

void
draw(GtkWidget *w, cairo_t *cr, struct curwin *cur)
{
	double		 x, y;
	int		 rc;
	struct kplotcfg	 cfg;

	cur->redraw = 0;

	/* White-out the view. */
	x = gtk_widget_get_allocated_width(w);
	y = gtk_widget_get_allocated_height(w);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); 
	cairo_rectangle(cr, 0.0, 0.0, x, y);
	cairo_fill(cr);

	kplotcfg_defaults(&cfg);

	switch (cur->view) {
	case (VIEW_DEV):
	case (VIEW_POLY):
	case (VIEW_EXTIMINS):
	case (VIEW_EXTMMAXS):
	case (VIEW_MEANMINS):
	case (VIEW_POLYMINS):
	case (VIEW_ISLANDMEAN):
	case (VIEW_ISLANDERMEAN):
		cfg.extrema_ymin = 0.0;
		cfg.extrema = EXTREMA_YMIN;
		break;
	default:
		break;
	}

	switch (cur->view) {
	case (VIEW_DEV): 
	case (VIEW_EXTI):
	case (VIEW_EXTIMINCDF):
	case (VIEW_EXTIMINPDF):
	case (VIEW_EXTM):
	case (VIEW_EXTMMAXCDF):
	case (VIEW_EXTMMAXPDF):
	case (VIEW_MEAN):
	case (VIEW_MEANMINCDF):
	case (VIEW_MEANMINPDF):
	case (VIEW_POLY):
	case (VIEW_POLYMINCDF):
	case (VIEW_POLYMINPDF):
	case (VIEW_SEXTM):
	case (VIEW_SEXTI):
	case (VIEW_SMEAN):
		cfg.xaxislabel = "incumbent";
		break;
	case (VIEW_ISLANDMEAN):
	case (VIEW_ISLANDERMEAN):
		cfg.xaxislabel = "island";
		break;
	case (VIEW_TIMESCDF):
	case (VIEW_TIMESPDF):
		cfg.xaxislabel = "exit time";
		break;
	case (VIEW_EXTIMINS):
	case (VIEW_EXTMMAXS):
	case (VIEW_MEANMINS):
	case (VIEW_POLYMINS):
		cfg.xaxislabel = "simulation";
		break;
	case (VIEW_MEANMINQ):
	case (VIEW_POLYMINQ):
		cfg.xaxislabel = "relative time";
		break;
	default:
		abort();
	}

	cfg.yaxislabelrot = M_PI / 2;

	switch (cur->view) {
	case (VIEW_POLY):
	case (VIEW_DEV): 
	case (VIEW_MEAN):
	case (VIEW_SMEAN):
		cfg.y2axislabel = "mutant fraction";
		break;
	case (VIEW_SEXTI):
	case (VIEW_EXTI):
		cfg.y2axislabel = "incumbent extinction";
		break;
	case (VIEW_SEXTM):
	case (VIEW_EXTM):
		cfg.y2axislabel = "mutant extinction";
		break;
	case (VIEW_EXTIMINCDF):
	case (VIEW_EXTIMINPDF):
	case (VIEW_EXTMMAXCDF):
	case (VIEW_EXTMMAXPDF):
	case (VIEW_MEANMINCDF):
	case (VIEW_MEANMINPDF):
	case (VIEW_POLYMINCDF):
	case (VIEW_POLYMINPDF):
	case (VIEW_TIMESCDF):
	case (VIEW_TIMESPDF):
		break;
	case (VIEW_ISLANDMEAN):
	case (VIEW_ISLANDERMEAN):
		cfg.y2axislabel = "mutant fraction mean";
		break;
	case (VIEW_EXTMMAXS):
	case (VIEW_EXTIMINS):
	case (VIEW_MEANMINS):
	case (VIEW_POLYMINS):
	case (VIEW_MEANMINQ):
	case (VIEW_POLYMINQ):
		cfg.y2axislabel = "incumbent";
		break;
	default:
		abort();
	}

	/* Draw our plot. */
	rc = kplot_draw(cur->views[cur->view], x, y, cr, &cfg);
	g_assert(0 != rc);
}
