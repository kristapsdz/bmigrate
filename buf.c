/*	$Id$ */
/*
 * Copyright (c) 2015 Kristaps Dzonsons <kristaps@kcons.eu>
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
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>
#include <kplot.h>

#include "extern.h"

struct simbuf *
simbuf_alloc(struct kdata *hot, size_t bufsz)
{
	struct simbuf	*buf;

	g_assert(NULL != hot);
	buf = g_malloc0(sizeof(struct simbuf));
	buf->hot = hot;
	buf->hotlsb = kdata_buffer_alloc(bufsz);
	g_assert(NULL != buf->hotlsb);
	buf->warm = kdata_buffer_alloc(bufsz);
	g_assert(NULL != buf->warm);
	buf->cold = kdata_buffer_alloc(bufsz);
	g_assert(NULL != buf->cold);
	return(buf);
}

void
simbuf_free(struct simbuf *buf)
{

	kdata_destroy(buf->hot);
	kdata_destroy(buf->hotlsb);
	kdata_destroy(buf->warm);
	kdata_destroy(buf->cold);
	free(buf);
}

void
simbuf_copy_hotlsb(struct simbuf *buf)
{
	int	rc;

	rc = kdata_buffer_copy(buf->hotlsb, buf->hot);
	g_assert(0 != rc);
}

void
simbuf_copy_warm(struct simbuf *buf)
{
	int	rc;

	rc = kdata_buffer_copy(buf->warm, buf->hotlsb);
	g_assert(0 != rc);
}

void
simbuf_copy_cold(struct simbuf *buf)
{
	int	rc;

	rc = kdata_buffer_copy(buf->cold, buf->warm);
	g_assert(0 != rc);
}
