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
#include <gtk/gtk.h>
#include <gsl/gsl_multifit.h>
#include <kplot.h>

#include "extern.h"

static int
savepng(const gchar *fname, const struct curwin *c)
{
	cairo_surface_t	*surf;
	cairo_t		*cr;
	cairo_status_t	 st;
	int		 rc;

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
	rc = kplot_draw(c->views[c->view], 600.0, 400.0, cr, NULL);
	g_assert(0 != rc);

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
savepdf(const gchar *fname, const struct curwin *c)
{
	cairo_surface_t	*surf;
	cairo_t		*cr;
	cairo_status_t	 st;
	const double	 w = 72 * 6, h = 72 * 4;
	int		 rc;

	g_debug("%p: Saving: %s", c, fname);
	surf = cairo_pdf_surface_create(fname, w, h);
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
	cairo_rectangle(cr, 0.0, 0.0, w, h);
	cairo_fill(cr);
	rc = kplot_draw(c->views[c->view], w, h, cr, NULL);
	g_assert(0 != rc);
	cairo_destroy(cr);
	return(1);
}

int
save(const gchar *fname, const struct curwin *cur)
{

	if (g_str_has_suffix(fname, ".pdf")) 
		return(savepdf(fname, cur));
	else
		return(savepng(fname, cur));
}
