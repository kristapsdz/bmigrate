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
	KML_POINT,
	KML_COORDINATES,
	KML_DESCRIPTION,
	KML__MAX
};

enum	kmlkey {
	KMLKEY_MEAN,
	KMLKEY_STDDEV,
	KMLKEY_POPULATION,
	KMLKEY__MAX
};

struct	kmlsave {
	FILE		*f;
	struct sim	*cursim;
	size_t		 curisland;
	gchar		*buf;
	gsize		 bufsz;
	gsize		 bufmax;
	int		 buffering;
};

struct	kmlparse {
	enum kmltype	 elem; /* current element */
	struct kmlplace	*cur; /* currently-parsed kmlplace */
	gchar		*buf; /* current parse text buffer */
	gsize	 	 bufsz; /* size in parse buffer */
	gsize	 	 bufmax; /* maximum sized buffer */
	gchar		*altbuf; 
	gsize	 	 altbufsz; 
	gsize	 	 altbufmax; 
	GList		*places; /* parsed places */
	gchar		*ign; /* element we're currently ignoring */
	size_t		 ignstack; /* stack of "ign" while ignoring */
#define	KML_STACKSZ	 128
	enum kmltype	 stack[128];
	size_t		 stackpos;
};

static	const char *const kmlkeys[KMLKEY__MAX] = {
	"mean", /* KMLKEY_MEAN */
	"stddev", /* KMLKEY_STDDEV */
	"population", /* KMLKEY_POPULATION */
};

static	const char *const kmltypes[KML__MAX] = {
	NULL, /* KML_INIT */
	"kml", /* KML_KML */
	"Document", /* KML_DOCUMENT */
	"Folder", /* KML_FOLDER */
	"Placemark", /* KML_PLACEMARK */
	"Point", /* KML_POINT */
	"coordinates", /* KML_COORDINATES */
	"description", /* KML_DESCRIPTION */
};

static void
kml_append(gchar **buf, gsize *bufsz, 
	gsize *bufmax, const char *text, gsize sz)
{

	/* XXX: no check for overflow... */
	if (*bufsz + sz + 1 > *bufmax) {
		*bufmax = *bufsz + sz + 1024;
		*buf = g_realloc(*buf, *bufmax);
	}

	memcpy(*buf + *bufsz, text, sz);
	*bufsz += sz;
	(*buf)[*bufsz] = '\0';
	g_assert(*bufsz <= *bufmax);
}

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

static void
kmlparse_free(gpointer dat)
{
	struct kmlplace	*place = dat;

	if (NULL == place)
		return;

	free(place);
}

void
kml_free(struct kml *kml)
{

	if (NULL == kml)
		return;

	g_list_free_full(kml->kmls, kmlparse_free);
	g_mapped_file_unref(kml->file);
}

/*
 * Try to find the string "@@population=NN@@", which stipulates the
 * population for this particular island.
 * If we don't find it, just return--we'll use the default.
 * If we find a bad population, raise an error.
 */
static int
kml_placemark(const gchar *buf, struct kmlplace *place)
{
	const gchar	*cp;
	gchar		*ep;
	gsize		 keysz;
	gchar		 nbuf[22];

	keysz = strlen("@@population=");
	while (NULL != (cp = strstr(buf, "@@population="))) {
		buf = cp + keysz;
		if (NULL == (cp = strstr(buf, "@@")))
			break;
		if ((gsize)(cp - buf) >= sizeof(nbuf) - 1)
			return(0);
		memcpy(nbuf, buf, cp - buf);
		nbuf[cp - buf] = '\0';
		place->pop = g_ascii_strtoull(buf, &ep, 10);
		if (ERANGE == errno || EINVAL == errno || ep == buf)
			return(0);
		break;
	}

	return(1);
}

static void
kml_replace(const gchar *buf, gsize sz, struct kmlsave *p)
{
	gsize		 i, len, start, end, vend;
	void		*cp;
	enum kmlkey	 j;

	for (i = 0; i < sz - 1; i++) {
		/* Look for the starting "@@" marker. */
		if ('@' != buf[i]) {
			fputc(buf[i], p->f);
			continue;
		} else if ('@' != buf[i + 1]) {
			fputc(buf[i], p->f);
			continue;
		} 

		/* Seek to find the end "@@" marker. */
		start = i + 2;
		for (end = start + 2; end < sz - 1; end++)
			if ('@' == buf[end] && '@' == buf[end + 1])
				break;

		/* Continue printing if not found of 0-length. */
		if (end == sz - 1 || end == start) {
			fputc(buf[i], p->f);
			continue;
		}

		vend = end;
		if (NULL != (cp = memchr(&buf[start], '=', vend)))
			vend = start + (cp - (void *)&buf[start]);

		/* Look for a matching key. */
		for (j = 0; j < KMLKEY__MAX; j++) {
			len = strlen(kmlkeys[j]);
			if (len != vend - start)
				continue;
			else if (memcmp(&buf[start], kmlkeys[j], len))
				continue;

			switch (j) {
			case (KMLKEY_MEAN):
				fprintf(p->f, "%g", stats_mean
					(&p->cursim->cold.islands
					 [p->curisland]));
				break;
			case (KMLKEY_STDDEV):
				fprintf(p->f, "%g", stats_stddev
					(&p->cursim->cold.islands
					 [p->curisland]));
			case (KMLKEY_POPULATION):
				assert(NULL != p->cursim->pops);
				fprintf(p->f, "%zu", 
					p->cursim->pops[p->curisland]);
			default:
				break;
			}

			break;
		}

		/* Didn't find it... */
		if (j == KMLKEY__MAX)
			fputc(buf[i], p->f);
		else
			i = end + 1;
	}

	if (i < sz)
		fputc(buf[i], p->f);
}

static void	
kml_save_elem_end(GMarkupParseContext *ctx, 
	const gchar *name, gpointer dat, GError **er)
{
	struct kmlsave	*p = dat;

	if (p->buffering) {
		kml_replace(p->buf, p->bufsz, p);
		p->bufsz = 0;
	}

	if (0 == g_strcmp0(name, kmltypes[KML_PLACEMARK])) {
		g_assert(p->buffering);
		p->curisland++;
		p->buffering = 0;
	}

	fprintf(p->f, "</%s>", name);
}

static void
kml_save_elem_start(GMarkupParseContext *ctx, 
	const gchar *name, const gchar **attrn, 
	const gchar **attrv, gpointer dat, GError **er)
{
	struct kmlsave	*p = dat;

	if (p->buffering) {
		kml_replace(p->buf, p->bufsz, p);
		p->bufsz = 0;
	}

	if (0 == g_strcmp0(name, kmltypes[KML_PLACEMARK])) {
		g_assert(p->curisland < p->cursim->islands);
		p->buffering = 1;
	}

	fputc('<', p->f);
	fputs(name, p->f);
	while (NULL != *attrn) {
		g_assert(NULL != *attrv);
		fprintf(p->f, " %s=\"%s\"", *attrn, *attrv);
		attrn++;
		attrv++;
	}
	fputc('>', p->f);
}

static void
kml_save_text(GMarkupParseContext *ctx, 
	const gchar *txt, gsize sz, gpointer dat, GError **er)
{
	struct kmlsave	*p = dat;

	if ( ! p->buffering) {
		fwrite(txt, sz, 1, p->f);
		return;
	}

	kml_append(&p->buf, &p->bufsz, &p->bufmax, txt, sz);
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
		/*
		 * if we're ending a Placemark, first check whether
		 * we've listed a population somewhere in any of the
		 * nested text segments.
		 * Also make sure that we have some coordinates (the
		 * default value was 360 for both, which of course isn't
		 * a valid coordinate).
		 */
		g_assert(NULL != p->cur);
		if ( ! kml_placemark(p->altbuf, p->cur)) {
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "Cannot parse population");
			break;
		} else if (p->cur->lat > 180 || p->cur->lng > 180) {
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "No coordinates for placemark");
			break;
		}
		p->places = g_list_append(p->places, p->cur);
		p->cur = NULL;
		break;
	case (KML_COORDINATES):
		/*
		 * Parse coordinates from the mix.
		 * Coordinates are longitude,latitude,altitude.
		 * Parse quickly and just make sure they're valid.
		 */
		set = g_strsplit(p->buf, ",", 3);
		p->cur->lng = g_ascii_strtod(set[0], NULL);
		if (ERANGE == errno)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "Cannot parse longitude");
		else if (p->cur->lng > 180.0 || p->cur->lng < -180.0)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "Invalid longitude");
		p->cur->lat = g_ascii_strtod(set[1], NULL);
		if (ERANGE == errno)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "Cannot parse latitude");
		else if (p->cur->lat > 90.0 || p->cur->lat < -90.0)
			*er = g_error_new_literal
				(G_MARKUP_ERROR, 
				 G_MARKUP_ERROR_INVALID_CONTENT, 
				 "Invalid latitude");
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
		/*
		 * If we're starting a Placemark, initialise ourselves
		 * with bad coorindates and a default population.
		 */
		if (NULL == p->cur) {
			p->cur = g_malloc0(sizeof(struct kmlplace));
			p->cur->lat = p->cur->lng = 360;
			p->cur->pop = 2;
			p->altbufsz = 0;
			break;
		}
		*er = g_error_new_literal
			(G_MARKUP_ERROR, 
			 G_MARKUP_ERROR_INVALID_CONTENT, 
			 "Nested placemarks not allowed.");
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

	if (NULL != p->cur)
		kml_append(&p->altbuf, &p->altbufsz, 
			&p->altbufmax, txt, sz);

	/* No collection w/o element or while ignoring. */
	if (NULL != p->ign || 0 == p->stackpos)
		return;

	/* Each element for which we're going to collect text. */
	switch (p->stack[p->stackpos - 1]) {
	case (KML_COORDINATES):
		break;
	default:
		return;
	}

	kml_append(&p->buf, &p->bufsz, &p->bufmax, txt, sz);
}

static void
kml_error(GMarkupParseContext *ctx, GError *er, gpointer dat)
{

	g_warning("%s", er->message);
}

void
kml_save(FILE *f, struct sim *sim)
{
	GMarkupParseContext	*ctx;
	GMarkupParser	  	 parse;
	struct kmlsave		 data;
	struct kml		*kml;
	int			 rc;

	if (NULL == (kml = sim->kml))
		return;

	memset(&parse, 0, sizeof(GMarkupParser));
	memset(&data, 0, sizeof(struct kmlsave));

	data.f = f;
	parse.end_element = kml_save_elem_end;
	parse.start_element = kml_save_elem_start;
	parse.text = kml_save_text;
	ctx = g_markup_parse_context_new(&parse, 0, &data, NULL);
	g_assert(NULL != ctx);

	data.cursim = sim;
	data.curisland = 0;
	rc = g_markup_parse_context_parse
		(ctx, g_mapped_file_get_contents(kml->file),
		 g_mapped_file_get_length(kml->file), NULL);
	g_assert(0 != rc);

	g_markup_parse_context_free(ctx);
	g_free(data.buf);
}

struct kml *
kml_parse(const gchar *file, GError **er)
{
	GMarkupParseContext	*ctx;
	GMarkupParser	  	 parse;
	GMappedFile		*f;
	int			 rc;
	struct kmlparse		 data;
	struct kml		*kml;

	*er = NULL;

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

	g_markup_parse_context_free(ctx);
	g_free(data.buf);
	g_free(data.altbuf);
	g_free(data.ign);
	kmlparse_free(data.cur);

	if (0 == rc) {
		g_assert(NULL != er);
		g_list_free_full(data.places, kmlparse_free);
		g_mapped_file_unref(f);
		return(NULL);
	}

	g_assert(NULL != data.places);
	kml = g_malloc0(sizeof(struct kml));
	kml->file = f;
	kml->kmls = data.places;
	return(kml);
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
