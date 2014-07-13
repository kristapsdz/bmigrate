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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

enum	kmltype {
	KML_INIT = 0,
	KML_KML,
	KML_DOCUMENT,
	KML_FOLDER,
	KML_PLACEMARK,
	KML_NAME,
	KML_POINT,
	KML_COORDINATES,
	KML_DESCRIPTION,
	KML__MAX
};

struct	kmlparse {
	enum kmltype	 elem; /* current element */
	struct kmlplace	*cur; /* currently-parsed kmlplace */
	gchar		*buf; /* current parse text buffer */
	size_t	 	 bufsz; /* size in parse buffer */
	size_t	 	 bufmax; /* maximum sized buffer */
	GList		*places; /* parsed places */
	gchar		*ign; /* element we're currently ignoring */
	size_t		 ignstack; /* stack of "ign" while ignoring */
#define	KML_STACKSZ	 128
	enum kmltype	 stack[128];
	size_t		 stackpos;
};

static	const char *const kmltypes[KML__MAX] = {
	NULL, /* KML_INIT */
	"kml", /* KML_KML */
	"Document", /* KML_DOCUMENT */
	"Folder", /* KML_FOLDER */
	"Placemark", /* KML_PLACEMARK */
	"name", /* KML_NAME */
	"Point", /* KML_POINT */
	"coordinates", /* KML_COORDINATES */
	"description", /* KML_DESCRIPTION */
};

static enum kmltype
kml_lookup(const gchar *name)
{
	enum kmltype	i;

	for (i = 0; i < KML__MAX; i++) {
		if (NULL == kmltypes[i])
			continue;
		if (0 == g_strcmp0(kmltypes[i], name))
			break;
	}

	return(i);
}

void
kml_free(gpointer dat)
{
	struct kmlplace	*place = dat;

	if (NULL == place)
		return;

	free(place->name);
	free(place);
}

static void	
kml_elem_end(GMarkupParseContext *ctx, 
	const gchar *name, gpointer dat, GError **er)
{
	struct kmlparse	 *p = dat;
	enum kmltype	  t;
	gchar		**set;

	if (NULL != p->ign) {
		g_assert(p->ignstack > 0);
		if (0 == g_strcmp0(p->ign, name))
			p->ignstack--;
		if (p->ignstack > 0)
			return;
		g_free(p->ign);
		p->ign = NULL;
		return;
	} 

	t = kml_lookup(name);
	g_assert(p->stackpos > 0);
	g_assert(t == p->stack[p->stackpos - 1]);
	p->stackpos--;

	switch (p->stack[p->stackpos]) {
	case (KML_PLACEMARK):
		g_assert(NULL != p->cur);
		p->cur->pop = 2; /* FIXME */
		p->places = g_list_prepend(p->places, p->cur);
		p->cur = NULL;
		break;
	case (KML_NAME):
		if (NULL == p->cur)
			break;
		g_free(p->cur->name);
		p->cur->name = g_strdup(p->buf);
		break;
	case (KML_COORDINATES):
		set = g_strsplit(p->buf, ",", 3);
		p->cur->lng = g_ascii_strtod(set[0], NULL);
		if (ERANGE == errno)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "");
		p->cur->lat = g_ascii_strtod(set[1], NULL);
		if (ERANGE == errno)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "");
		g_strfreev(set);
		break;
	default:
		break;
	}
}

static void
kml_elem_start(GMarkupParseContext *ctx, 
	const gchar *name, const gchar **attrn, 
	const gchar **attrv, gpointer dat, GError **er)
{
	enum kmltype	 t;
	struct kmlparse	*p = dat;

	if (NULL != p->ign) {
		g_assert(p->ignstack > 0);
		if (0 == g_strcmp0(p->ign, name))
			p->ignstack++;
		return;
	}
	
	if (KML__MAX == (t = kml_lookup(name))) {
		assert(0 == p->ignstack);
		p->ign = g_strdup(name);
		p->ignstack = 1;
		return;
	}

	p->stack[p->stackpos++] = t;
	p->bufsz = 0;
	
	switch (p->stack[p->stackpos - 1]) {
	case (KML_PLACEMARK):
		if (NULL == p->cur) {
			p->cur = g_malloc0(sizeof(struct kmlplace));
			break;
		}
		*er = g_error_new_literal
			(G_MARKUP_ERROR, 
			 G_MARKUP_ERROR_INVALID_CONTENT, 
			 "");
		break;
	default:
		break;
	}
}

static void
kml_text(GMarkupParseContext *ctx, 
	const gchar *txt, gsize sz, gpointer dat, GError **er)
{
	struct kmlparse	*p = dat;

	/* No collection w/o element or while ignoring. */
	if (NULL != p->ign || 0 == p->stackpos)
		return;

	/* Each element for which we're going to collect text. */
	switch (p->stack[p->stackpos - 1]) {
	case (KML_NAME):
		/* FALLTHROUGH */
	case (KML_COORDINATES):
		break;
	default:
		return;
	}

	/* XXX: no check for overflow... */
	if (p->bufsz + sz + 1 > p->bufmax) {
		p->bufmax = p->bufsz + sz + 1024;
		p->buf = g_realloc(p->buf, p->bufmax);
	}

	memcpy(p->buf + p->bufsz, txt, sz);
	p->bufsz += sz;
	p->buf[p->bufsz] = '\0';
	g_assert(p->bufsz <= p->bufmax);
}

static void
kml_error(GMarkupParseContext *ctx, GError *er, gpointer dat)
{

	g_warning("%s", er->message);
}

void
kml_write(FILE *f, const gchar *p)
{

	for ( ; '\0' != *p; p++) 
		switch (*p) {
		case ('<'):
			fputs("&lt;", f);
			break;
		case ('>'):
			fputs("&gt;", f);
			break;
		case ('&'):
			fputs("&amp;", f);
			break;
		case ('\''):
			fputs("&apos;", f);
			break;
		case ('"'):
			fputs("&quot;", f);
			break;
		default:
			fputc(*p, f);
			break;
		}
}

void
kml_save(FILE *f, GList *sims)
{
	struct sim	*sim;
	size_t		 i;
	GList		*kmls;
	struct kmlplace	*kml;

	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", f);
	fputs("<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n", f);
	fputs("\t<Document>\n", f);

	for ( ; NULL != sims; sims = g_list_next(sims)) {
		sim = sims->data;
		if (NULL == sim->kml)
			continue;
		fputs("\t\t<Folder>\n", f);
		fputs("\t\t\t<name>\n\t\t\t\t", f);
		kml_write(f, sim->name);
		fputs("\n\t\t\t</name>\n", f);
		i = 0;
		for (kmls = sim->kml; NULL != kmls; kmls = g_list_next(kmls)) {
			assert(i < sim->islands);
			kml = kmls->data;
			fputs("\t\t\t<Placemark>\n", f);
			fputs("\t\t\t\t<name>", f);
			kml_write(f, kml->name);
			fputs("</name>\n", f);
			fprintf(f, "\t\t\t\t<description>Mean: %g</description>\n", 
				stats_mean(&sim->cold.islands[i]));
			fputs("\t\t\t\t<Point>\n", f);
			fprintf(f, "\t\t\t\t\t<coordinates>%g,%g</coordinates>\n", 
				kml->lng, kml->lat);
			fputs("\t\t\t\t</Point>\n", f);
			fputs("\t\t\t</Placemark>\n", f);
			i++;
		}
		fputs("\t\t</Folder>\n", f);
	}

	fputs("\t</Document>\n", f);
	fputs("</kml>\n", f);
}

GList *
kml_parse(const gchar *file, GError **er)
{
	GMarkupParseContext	*ctx;
	GMarkupParser	  	 parse;
	GMappedFile		*f;
	int			 rc;
	struct kmlparse		 data;

	memset(&parse, 0, sizeof(GMarkupParser));
	memset(&data, 0, sizeof(struct kmlparse));

	data.elem = KML_INIT;

	parse.start_element = kml_elem_start;
	parse.end_element = kml_elem_end;
	parse.text = kml_text;
	parse.error = kml_error;

	if (NULL == (f = g_mapped_file_new(file, FALSE, er)))
		return(NULL);

	ctx = g_markup_parse_context_new
		(&parse, 0, &data, NULL);
	g_assert(NULL != ctx);
	rc = g_markup_parse_context_parse
		(ctx, g_mapped_file_get_contents(f),
		 g_mapped_file_get_length(f), er);
	g_mapped_file_unref(f);
	g_markup_parse_context_free(ctx);
	g_free(data.buf);
	g_free(data.ign);
	kml_free(data.cur);

	if (0 == rc) {
		g_assert(NULL != er);
		g_list_free_full(data.places, kml_free);
		data.places = NULL;
	}

	return(data.places);
}

/*
 * Probabilities being the normalised inverse square distance.
 */
double **
kml_migration_distance(GList *list)
{
	double		**p;
	double		  dist, sum;
	size_t		  i, j, len;
	struct kmlplace	 *pl1, *pl2;

	len = (size_t)g_list_length(list);
	p = g_malloc0_n(len, sizeof(double *));
	for (i = 0; i < len; i++) {
		pl1 = g_list_nth_data(list, i);
		p[i] = g_malloc0_n(len, sizeof(double));
		for (sum = 0.0, j = 0; j < len; j++) {
			if (i == j) {
				p[i][j] = 0.0;
				continue;
			}
			pl2 = g_list_nth_data(list, j);
			dist = sqrt((pl1->lat - pl2->lat) * 
				(pl1->lat - pl2->lat) + 
				(pl1->lng - pl2->lng) * 
				(pl1->lng - pl2->lng));
			p[i][j] = 1.0 / (dist * dist);
			sum += p[i][j];
		}
		for (j = 0; j < len; j++) 
			if (i != j)
				p[i][j] = p[i][j] / sum;
	}

	return(p);
}
