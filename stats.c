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
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>

#include "extern.h"

void
stats_push(struct stats *p, double x)
{
	double	delta, delta_n, term1;
	size_t	n1;

	n1 = p->n;

	if (0.0 == x)
		p->extm++;
	else if (1.0 == x)
		p->exti++;

	p->n++;

	delta = x - p->M1;
	delta_n = delta / (double)p->n;
	term1 = delta * delta_n * (double)n1;

	p->M1 += delta_n;
	p->M2 += term1;
}

double
stats_extinctm(const struct stats *p)
{

	if (0 == p->n)
		return(0.0);
	return(p->extm / (double)p->n);
}

double
stats_extincti(const struct stats *p)
{

	if (0 == p->n)
		return(0.0);
	return(p->exti / (double)p->n);
}

double
stats_mean(const struct stats *p)
{
	
	return(p->M1);
}

double 
stats_variance(const struct stats *p)
{

	if (0 == p->n)
		return(0.0);
	return(p->M2 / ((double)p->n - 1.0));
}

double 
stats_stddev(const struct stats *p)
{
	
	if (0 == p->n)
		return(0.0);
	return(sqrt(stats_variance(p)));
}
