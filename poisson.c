/*	$Id$ */
/*
 * Copyright (c) 2012, 2014 Kristaps Dzonsons <kristaps@kcons.eu>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

#define	CACHE_BUCKETS	 2048
#define	CACHE_SIZE	 1024

struct	cache {
	double	 l;
	size_t	 bucketsz;
	size_t	 buckets[CACHE_BUCKETS];
};

size_t
poisson(const gsl_rng *rng, double l)
{
	uint64_t		 hash;
	char			*byte;
	size_t			 key;
	size_t		 	 mmax, i, j, k;
	double			 v, a, b;
	static struct cache	*ccache;

	assert(l >= 0.0);

	if (0.0 == l)
		return(0);
	if (NULL == ccache) {
		ccache = g_malloc0_n(CACHE_SIZE, sizeof(struct cache));
		g_debug("Poisson table: %d size, "
			"%d buckets", CACHE_SIZE, CACHE_BUCKETS);
	}

	byte = (char *)&l;
	hash = *(uint64_t *)byte;
	key = hash % CACHE_SIZE;

	if (l != ccache[key].l) {
		/*g_debug("Poisson table cache miss: "
			"%g (%zu: %g)", l, key, ccache[key].l);*/
		ccache[key].l = l;
		k = i = 0;
		while (k < CACHE_BUCKETS) {
			a = i * log(l);
			b = -l;
			v = a + b - gsl_sf_lnfact(i);
			mmax = exp(v) * CACHE_BUCKETS;
			for (j = 0; j < mmax; j++, k++) {
				assert(k < CACHE_BUCKETS);
				ccache[key].buckets[k] = i;
			}
			if (i++ > (size_t)l && 0 == mmax)
				break;
		}
		ccache[key].bucketsz = k;
	} 
	
	return(ccache[key].buckets
		[gsl_rng_uniform_int
		(rng, ccache[key].bucketsz)]);
}
