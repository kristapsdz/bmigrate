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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef MAC_INTEGRATION
#include <gtkosxapplication.h>
#endif
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_histogram.h>
#include <kplot.h>

#include "extern.h"

/*
 * Brute-force scan all possible pi values (and Poisson means) by
 * scanning through the strategy space.
 */
int
rangefind(struct bmigrate *b)
{
	size_t		 mutants;
	double		 mstrat, istrat, v;
	gchar		 buf[22];

	g_assert(b->rangeid);

	/*
	 * Set the number of mutants on a given island, then see what
	 * the utility function would yield given that number of mutants
	 * and incumbents, setting the current player to be one or the
	 * other..
	 */
	mstrat = istrat = 0.0;
	for (mutants = 0; mutants <= b->range.n; mutants++) {
		mstrat = b->range.ymin + 
			(b->range.slicey / (double)b->range.slices) * 
			(b->range.ymax - b->range.ymin);
		istrat = b->range.xmin + 
			(b->range.slicex / (double)b->range.slices) * 
			(b->range.xmax - b->range.xmin);
		/*
		 * Only check for a given mutant/incumbent individual's
		 * strategy if the population is going to support the
		 * existence of that individual.
		 */
		if (mutants > 0) {
			v = hnode_exec
				((const struct hnode *const *) 
				 b->range.exp, 
				 istrat, mstrat * mutants + istrat * 
				 (b->range.n - mutants), b->range.n);
			if (0.0 != v && ! isnormal(v))
				break;
			if (v < b->range.pimin)
				b->range.pimin = v;
			if (v > b->range.pimax)
				b->range.pimax = v;
			b->range.piaggr += v;
			b->range.picount++;
		}
		if (mutants != b->range.n) {
			v = hnode_exec
				((const struct hnode *const *) 
				 b->range.exp, 
				 mstrat, mstrat * mutants + istrat * 
				 (b->range.n - mutants), b->range.n);
			if (0.0 != v && ! isnormal(v))
				break;
			if (v < b->range.pimin)
				b->range.pimin = v;
			if (v > b->range.pimax)
				b->range.pimax = v;
			b->range.piaggr += v;
			b->range.picount++;
		}
	}

	/*
	 * We might have hit a discontinuous number.
	 * If we did, then print out an error and don't continue.
	 * If not, update to the next mutant and incumbent.
	 */
	if (mutants <= b->range.n) {
		g_snprintf(buf, sizeof(buf), 
			"%zu mutants, mutant=%g, incumbent=%g",
			mutants, mstrat, istrat);
		gtk_label_set_text(b->wins.rangeerror, buf);
		gtk_widget_show_all(GTK_WIDGET(b->wins.rangeerrorbox));
		g_debug("Range-finder idle event complete (error)");
		b->rangeid = 0;
	} else {
		if (++b->range.slicey == b->range.slices) {
			b->range.slicey = 0;
			b->range.slicex++;
		}
		if (b->range.slicex == b->range.slices) {
			g_debug("Range-finder idle event complete");
			b->rangeid = 0;
		}
	}

	/*
	 * Set our current extrema.
	 */
	g_snprintf(buf, sizeof(buf), "%g", b->range.pimin);
	gtk_label_set_text(b->wins.rangemin, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * b->range.pimin));
	gtk_label_set_text(b->wins.rangeminlambda, buf);

	g_snprintf(buf, sizeof(buf), "%g", b->range.pimax);
	gtk_label_set_text(b->wins.rangemax, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * b->range.pimax));
	gtk_label_set_text(b->wins.rangemaxlambda, buf);

	v = b->range.piaggr / (double)b->range.picount;
	g_snprintf(buf, sizeof(buf), "%g", v);
	gtk_label_set_text(b->wins.rangemean, buf);
	g_snprintf(buf, sizeof(buf), "%g", b->range.alpha * 
		(1.0 + b->range.delta * v));
	gtk_label_set_text(b->wins.rangemeanlambda, buf);

	v = (b->range.slicex * b->range.slices + b->range.slicey) /
		(double)(b->range.slices * b->range.slices);
	g_snprintf(buf, sizeof(buf), "%.1f%%", v * 100.0);
	gtk_label_set_text(b->wins.rangestatus, buf);

	return(0 != b->rangeid);
}

