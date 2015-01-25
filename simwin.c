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

static void
swin_init(struct swin *c, enum view view, GtkBuilder *b)
{

	c->window = win_init_window(b, "window1");
	c->menu = win_init_menubar(b, "menubar1");
	c->draw = win_init_draw(b, "drawingarea1");
	c->boxconfig = win_init_box(b, "box2");
	c->menufile = win_init_menuitem(b, "menuitem1");
	c->menuview = win_init_menuitem(b, "menuitem2");
	c->menutools = win_init_menuitem(b, "menuitem3");
	c->viewclone = win_init_menuitem(b, "menuitem15");
	c->viewpause = win_init_menuitem(b, "menuitem20");
	c->viewunpause = win_init_menuitem(b, "menuitem21");
	c->views[VIEW_ISLANDMEAN] = win_init_menucheck(b, "menuitem45");
	c->views[VIEW_MEAN] = win_init_menucheck(b, "menuitem8");
	c->views[VIEW_SMEAN] = win_init_menucheck(b, "menuitem37");
	c->views[VIEW_SEXTM] = win_init_menucheck(b, "menuitem43");
	c->views[VIEW_EXTM] = win_init_menucheck(b, "menuitem25");
	c->views[VIEW_EXTMMAXPDF] = win_init_menucheck(b, "menuitem28");
	c->views[VIEW_EXTMMAXCDF] = win_init_menucheck(b, "menuitem29");
	c->views[VIEW_EXTI] = win_init_menucheck(b, "menuitem26");
	c->views[VIEW_EXTIMINPDF] = win_init_menucheck(b, "menuitem27");
	c->views[VIEW_EXTIMINCDF] = win_init_menucheck(b, "menuitem30");
	c->views[VIEW_SMEANMINPDF] = win_init_menucheck(b, "menuitem38");
	c->views[VIEW_SMEANMINCDF] = win_init_menucheck(b, "menuitem39");
	c->views[VIEW_SEXTMMAXPDF] = win_init_menucheck(b, "menuitem52");
	c->views[VIEW_SEXTMMAXCDF] = win_init_menucheck(b, "menuitem51");
	c->views[VIEW_SMEANMINQ] = win_init_menucheck(b, "menuitem41");
	c->views[VIEW_SMEANMINS] = win_init_menucheck(b, "menuitem40");
	c->views[VIEW_EXTIMINS] = win_init_menucheck(b, "menuitem35");
	c->views[VIEW_DEV] = win_init_menucheck(b, "menuitem6");
	c->views[VIEW_POLY] = win_init_menucheck(b, "menuitem7");
	c->views[VIEW_POLYMINPDF] = win_init_menucheck(b, "menuitem9");
	c->views[VIEW_POLYMINCDF] = win_init_menucheck(b, "menuitem11");
	c->views[VIEW_MEANMINPDF] = win_init_menucheck(b, "menuitem10");
	c->views[VIEW_MEANMINCDF] = win_init_menucheck(b, "menuitem12");
	c->views[VIEW_MEANMINQ] = win_init_menucheck(b, "menuitem13");
	c->views[VIEW_MEANMINS] = win_init_menucheck(b, "menuitem22");
	c->views[VIEW_POLYMINS] = win_init_menucheck(b, "menuitem31");
	c->views[VIEW_EXTMMAXS] = win_init_menucheck(b, "menuitem33");
	c->views[VIEW_POLYMINQ] = win_init_menucheck(b, "menuitem14");
	c->views[VIEW_CONFIG] = win_init_menucheck(b, "menuitem36");
	c->views[VIEW_STATUS] = win_init_menucheck(b, "menuitem46");
	c->menuquit = win_init_menuitem(b, "menuitem5");
	c->menuautoexport = win_init_menuitem(b, "menuitem49");
	c->menuunautoexport = win_init_menuitem(b, "menuitem50");
	c->menuclose = win_init_menuitem(b, "menuitem24");
	c->menusave = win_init_menuitem(b, "menuitem34");
	c->menusavekml = win_init_menuitem(b, "menuitem17");
	c->menusaveall = win_init_menuitem(b, "menuitem47");

	gtk_widget_show_all(GTK_WIDGET(c->window));

	gtk_window_set_title(GTK_WINDOW(c->window),
		gtk_menu_item_get_label
		(GTK_MENU_ITEM(c->views[view])));

	gtk_widget_hide(GTK_WIDGET(c->menuunautoexport));

#ifdef MAC_INTEGRATION
	gtk_widget_hide(GTK_WIDGET(c->menu));
	gtk_widget_hide(GTK_WIDGET(c->menuquit));
	gtkosx_application_set_menu_bar
		(gtkosx_application_get(), 
		 GTK_MENU_SHELL(c->menu));
	gtkosx_application_sync_menubar
		(gtkosx_application_get());
#endif
}

/*
 * Dereference a simulation.
 * This means that we've closed an output window that's looking at a
 * particular simulation.
 * When the simulation has no more references (i.e., no more windows are
 * painting that simulation), then the simulation destroys itself.
 */
static void
on_sim_deref(gpointer dat)
{
	struct sim	*sim = dat;

	g_debug("%p: Simulation deref (now %zu)", sim, sim->refs - 1);
	if (0 != --sim->refs) 
		return;
	g_debug("%p: Simulation terminating", sim);
	sim_stop(sim, NULL);
}

/*
 * Dereference all simulations owned by a given window.
 * This happens when a window is closing.
 */
static void
on_sims_deref(gpointer dat)
{

	g_list_free_full(dat, on_sim_deref);
}

static void
curwin_free(gpointer dat)
{
	struct curwin	*cur = dat;

	g_debug("%p: Simwin freeing", cur);
	kplot_free(cur->view_mean);
	kplot_free(cur->view_smean);
	kplot_free(cur->view_smextinct);
	kplot_free(cur->view_stddev);
	kplot_free(cur->view_mextinct);
	kplot_free(cur->view_iextinct);
	cur->b->windows = g_list_remove(cur->b->windows, cur);
	on_sims_deref(cur->sims);
	g_free(cur->autosave);
	g_free(cur);
}

static void
window_add_sim(struct curwin *cur, struct sim *sim)
{
	GtkWidget	*box, *w;
	struct kdata	*stats[2];
	enum kplottype	 ts[2];

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

	w = gtk_label_new(sim->name);
	gtk_container_add(GTK_CONTAINER(box), w);
	gtk_container_add(GTK_CONTAINER(cur->wins.boxconfig), box);
	gtk_widget_show_all(GTK_WIDGET(cur->wins.boxconfig));

	stats[0] = sim->bufs.means->cold;
	stats[1] = sim->bufs.stddevs->cold;
	ts[0] = ts[1] = KPLOT_LINES;

	/* FIXME: colour of lines */
	kplot_attach_data(cur->view_mean, 
		sim->bufs.means->cold, KPLOT_LINES, NULL);

	kplot_attach_data(cur->view_smextinct, 
		sim->bufs.mextinct->cold, KPLOT_LINES, NULL);
	kplot_attach_smooth(cur->view_smextinct, 
		sim->bufs.mextinct->cold, KPLOT_LINES, NULL,
		KSMOOTH_MOVAVG, NULL);

	kplot_attach_data(cur->view_smean, 
		sim->bufs.means->cold, KPLOT_LINES, NULL);
	kplot_attach_smooth(cur->view_smean, 
		sim->bufs.means->cold, KPLOT_LINES, NULL,
		KSMOOTH_MOVAVG, NULL);

	kplot_attach_datas(cur->view_stddev, 2,
		stats, ts, NULL, KPLOTS_YERRORLINE);

	kplot_attach_data(cur->view_mextinct, 
		sim->bufs.mextinct->cold, KPLOT_LINES, NULL);

	kplot_attach_data(cur->view_iextinct, 
		sim->bufs.iextinct->cold, KPLOT_LINES, NULL);
}

/*
 * Indicate that a given simulation is now being referenced by a new
 * window.
 */
static void
sim_ref(gpointer dat, gpointer unused)
{
	struct sim	*sim = dat;

	++sim->refs;
	g_debug("%p: Simulation ref (now %zu)", sim, sim->refs);
}

/*
 * Transfer the other widget's data into our own.
 */
void
on_drag_recv(GtkWidget *widget, GdkDragContext *ctx, 
	gint x, gint y, GtkSelectionData *sel, 
	guint target, guint time, gpointer dat)
{
	GObject		*srcptr, *dstptr;
	struct curwin	*cur;
	GList		*srcsims, *l, *ll;

	/* Get pointers to our and the other's window. */
	assert(NULL != sel);
	dstptr = G_OBJECT(gtk_widget_get_toplevel(widget));
	srcptr = G_OBJECT(*(void **)
		gtk_selection_data_get_data(sel));
	assert(NULL != srcptr);
	gtk_drag_finish(ctx, TRUE, FALSE, time);

	/* Don't copy into ourselves. */
	if (dstptr == srcptr)
		return;

	/* Get the simulation lists. */
	cur = g_object_get_data(srcptr, "cfg");
	srcsims = cur->sims;
	assert(NULL != srcsims);
	cur = g_object_get_data(dstptr, "cfg");

	/* Concatenate the simulation lists. */
	/* XXX: use g_list_concat? */
	for (l = srcsims; NULL != l; l = l->next) {
		g_debug("Copying simulation %p", l->data);
		for (ll = cur->sims; NULL != ll; ll = ll->next)
			if (ll->data == l->data)
				break;

		if (NULL != ll) {
			g_debug("Simulation %p duplicate", l->data);
			continue;
		}

		sim_ref(l->data, NULL);
		cur->sims = g_list_append(cur->sims, l->data);
		window_add_sim(cur, l->data);
	}
}

/*
 * Signifity sight-unseen that we can transfer data.
 * We're only using DnD for one particular thing, so there's no need for
 * elaborate security measures.
 */
gboolean
on_drag_drop(GtkWidget *widget, GdkDragContext *ctx, 
	gint x, gint y, guint time, gpointer dat)
{

	gtk_drag_get_data(widget, ctx, 0, time);
	return(TRUE);
}

/*
 * Send our identifier to the destination of the DnD.
 * They'll query our data separately.
 */
void
on_drag_get(GtkWidget *widget, GdkDragContext *ctx, 
	GtkSelectionData *sel, guint targ, guint time, gpointer dat)
{
	void	*ptr;

	ptr = gtk_widget_get_toplevel(widget);
	gtk_selection_data_set(sel, 
		gtk_selection_data_get_target(sel), 
		sizeof(intptr_t) * 8, 
		(const guchar *)&ptr, sizeof(intptr_t));
}

/*
 * Initialise a simulation (or simulations) window.
 * This is either called when we've just made a new simulation 
 */
static void
window_init(struct bmigrate *b, struct curwin *cur, GList *sims)
{
	GtkBuilder	*builder;
	GtkTargetEntry   target;
	GList		*l;

	builder = builder_get("simwin.glade");
	g_assert(NULL != builder);
	swin_init(&cur->wins, cur->view, builder);
	gtk_builder_connect_signals(builder, cur);
	g_object_unref(G_OBJECT(builder));

	cur->view_mean = kplot_alloc();
	g_assert(NULL != cur->view_mean);
	cur->view_smean = kplot_alloc();
	g_assert(NULL != cur->view_smean);
	cur->view_smextinct = kplot_alloc();
	g_assert(NULL != cur->view_smextinct);
	cur->view_stddev = kplot_alloc();
	g_assert(NULL != cur->view_stddev);
	cur->view_mextinct = kplot_alloc();
	g_assert(NULL != cur->view_mextinct);
	cur->view_iextinct = kplot_alloc();
	g_assert(NULL != cur->view_iextinct);

	cur->redraw = 1;
	cur->sims = sims;
	cur->b = b;

	g_object_set_data_full(G_OBJECT
		(cur->wins.window), "cfg", cur, curwin_free);
	b->windows = g_list_append(b->windows, cur);

	/* 
	 * Coordinate drag-and-drop. 
	 * We only allow drag-and-drop between us and other windows of
	 * this same simulation.
	 */
	target.target = g_strdup("integer");
	target.flags = GTK_TARGET_SAME_APP|GTK_TARGET_OTHER_WIDGET;
	target.info = 0;

	gtk_drag_dest_set(GTK_WIDGET(cur->wins.draw),
		GTK_DEST_DEFAULT_ALL, &target, 1, GDK_ACTION_COPY);
	gtk_drag_source_set(GTK_WIDGET(cur->wins.draw),
		GDK_BUTTON1_MASK, &target, 1, GDK_ACTION_COPY);
	g_free(target.target);

	for (l = cur->sims; NULL != l; l = g_list_next(l))
		window_add_sim(cur, l->data);
}

/*
 * Clone the current window.
 * We create a new window, intialised in the same way as for a new
 * single simulation, but give it an existing list of simulations.
 */
void
onclone(GtkMenuItem *menuitem, gpointer dat)
{
	struct curwin	*oldcur = dat, *newcur;
	GList		*oldsims, *newsims;

	oldsims = oldcur->sims;
	g_list_foreach(oldsims, sim_ref, NULL);
	newsims = g_list_copy(oldsims);
	newcur = g_malloc0(sizeof(struct curwin));
	newcur->view = oldcur->view;
	window_init(oldcur->b, newcur, newsims);
}

/*
 * Brute-force scan all possible pi values (and Poisson means) by
 * scanning through the strategy space.
 */
static gboolean
on_rangefind_idle(gpointer dat)
{
	struct bmigrate	*b = dat;
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

static int
entry2func(GtkEntry *entry, struct hnode ***exp, GtkLabel *error)
{
	const gchar	*txt;
	GdkRGBA	 	 bad = { 1.0, 0.0, 0.0, 0.5 };

	txt = gtk_entry_get_text(entry);
	*exp = hnode_parse((const gchar **)&txt);
	if (NULL != *exp) {
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: not a function.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entryworder(GtkEntry *mine, GtkEntry *maxe, 
	double min, double max, GtkLabel *error)
{
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	if (min < max) {
		gtk_widget_override_background_color
			(GTK_WIDGET(mine), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		gtk_widget_override_background_color
			(GTK_WIDGET(maxe), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: bad weak ordering.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(mine), GTK_STATE_FLAG_NORMAL, &bad);
	gtk_widget_override_background_color
		(GTK_WIDGET(maxe), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entryorder(GtkEntry *mine, GtkEntry *maxe, 
	double min, double max, GtkLabel *error)
{
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	if (min <= max) {
		gtk_widget_override_background_color
			(GTK_WIDGET(mine), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		gtk_widget_override_background_color
			(GTK_WIDGET(maxe), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: bad ordering.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(mine), GTK_STATE_FLAG_NORMAL, &bad);
	gtk_widget_override_background_color
		(GTK_WIDGET(maxe), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entry2double(GtkEntry *entry, gdouble *sz, GtkLabel *error)
{
	gchar	*ep;
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	*sz = g_ascii_strtod(gtk_entry_get_text(entry), &ep);

	if (ERANGE != errno && '\0' == *ep) {
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, NULL);
		return(1);
	}

	gtk_label_set_text(error, "Error: not a decimal number.");
	gtk_widget_show_all(GTK_WIDGET(error));
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), GTK_STATE_FLAG_NORMAL, &bad);
	return(0);
}

static int
entry2size(GtkEntry *entry, size_t *sz, GtkLabel *error, size_t min)
{
	guint64	 v;
	gchar	*ep;
	GdkRGBA	 bad = { 1.0, 0.0, 0.0, 0.5 };

	v = g_ascii_strtoull(gtk_entry_get_text(entry), &ep, 10);
	if (ERANGE == errno || '\0' != *ep || v >= SIZE_MAX) {
		gtk_label_set_text
			(error, "Error: not a natural number.");
		gtk_widget_show_all(GTK_WIDGET(error));
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, &bad);
		return(0);
	} else if (v < min) {
		gtk_label_set_text
			(error, "Error: number too small.");
		gtk_widget_show_all(GTK_WIDGET(error));
		gtk_widget_override_background_color
			(GTK_WIDGET(entry), 
			 GTK_STATE_FLAG_NORMAL, &bad);
		return(0);
	}

	*sz = (size_t)v;
	gtk_widget_override_background_color
		(GTK_WIDGET(entry), 
		 GTK_STATE_FLAG_NORMAL, NULL);
	return(1);
}

/*
 * Validate the contents of a "mapbox", that is, an island
 * configuration.
 * For the moment, this simply checks the island population.
 */
static int
mapbox2pair(GtkLabel *err, GtkWidget *w, size_t *n)
{
	GList		*list, *cur;

	list = gtk_container_get_children(GTK_CONTAINER(w));
	g_assert(NULL != list);
	cur = list->next;
	g_assert(NULL != cur);
#if 0
	if ( ! entry2double(GTK_ENTRY(cur->data), m, err)) {
		g_list_free(list);
		return(0);
	}
	cur = cur->next;
	g_assert(NULL != cur);
	cur = cur->next;
	g_assert(NULL != cur);
#endif
	if ( ! entry2size(GTK_ENTRY(cur->data), n, err, 2))  {
		g_list_free(list);
		return(0);
	}

	g_list_free(list);
	return(1);
}

/*
 * We have various ways of auto-setting the name of the simulation.
 * By default, it's set to the current date-time.
 */
static void
donamefill(struct hwin *c)
{
	enum namefill	 v;
	GTimeVal	 gt;
	enum input	 input;
	gchar		 buf[1024];
	gchar		*bufp;
	enum mutants	 mutants;

	input = gtk_notebook_get_current_page(c->inputs);
	g_assert(input < INPUT__MAX);

	for (mutants = 0; mutants < MUTANTS__MAX; mutants++)
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON(c->mutants[mutants])))
			break;

	for (v = 0; v < NAMEFILL__MAX; v++)
		if (gtk_toggle_button_get_active(c->namefill[v]))
			break;

	bufp = buf;
	switch (v) {
	case (NAMEFILL_M):
		g_snprintf(buf, sizeof(buf), "m=%s", 
			gtk_entry_get_text(c->migrate[input]));
		break;
	case (NAMEFILL_T):
		g_snprintf(buf, sizeof(buf), "T=%s", 
			gtk_entry_get_text(c->stop));
		break;
	case (NAMEFILL_MUTANTS):
		if (MUTANTS_DISCRETE == mutants) {
			g_snprintf(buf, sizeof(buf), 
				"discrete [%s,%s)", 
				gtk_entry_get_text(c->xmin),
				gtk_entry_get_text(c->xmax));
			break;
		}
		g_snprintf(buf, sizeof(buf), 
			"Gaussian s=%s, [%s,%s)", 
			gtk_entry_get_text(c->mutantsigma),
			gtk_entry_get_text(c->ymin),
			gtk_entry_get_text(c->ymax));
		break;
	case (NAMEFILL_DATE):
		g_get_current_time(&gt);
		bufp = g_time_val_to_iso8601(&gt);
		break;
	case (NAMEFILL_NONE):
		return;
	default:
		abort();
	}

	gtk_entry_set_text(c->name, bufp);
	if (bufp != buf)
		g_free(bufp);
}


void
onnametoggle(GtkToggleButton *editable, gpointer dat)
{
	struct bmigrate	*b = dat;

	donamefill(&b->wins);
}

void
onnameupdate(GtkEditable *editable, gpointer dat)
{
	struct bmigrate	*b = dat;

	donamefill(&b->wins);
}


/*
 * We want to run the given simulation.
 * First, verify all data; then, start the simulation; lastly, open a
 * window assigned specifically to that simulation.
 */
void
onactivate(GtkButton *button, gpointer dat)
{
	gint	 	  input;
	struct bmigrate	 *b = dat;
	struct hnode	**exp;
	enum mapmigrant	  migrants;
	GtkWidget	 *w;
	struct kml	 *kml;
	GList		 *list;
	GtkLabel	 *err = b->wins.error;
	const gchar	 *name, *func;
	gchar	  	 *file;
	gdouble		**ms;
	gdouble		  xmin, xmax, delta, alpha, m, sigma,
			  ymin, ymax;
	enum mutants	  mutants;
	size_t		  i, totalpop, islands, stop, 
			  slices, islandpop;
	size_t		 *islandpops;
	struct sim	 *sim;
	struct curwin	 *cur;
	struct kmlplace	 *kmlp;
	GError		 *er;

	islandpops = NULL;
	islandpop = 0;
	ms = NULL;
	kml = NULL;

	if ( ! entry2size(b->wins.stop, &stop, err, 1))
		goto cleanup;

	input = gtk_notebook_get_current_page(b->wins.inputs);
	migrants = MAPMIGRANT_UNIFORM;
	if (gtk_toggle_button_get_active
		(b->wins.mapmigrants[MAPMIGRANT_DISTANCE]))
		migrants = MAPMIGRANT_DISTANCE;

	switch (input) {
	case (INPUT_UNIFORM):
		/*
		 * In the simplest possible case, we use uniform values
		 * for both migration (no inter-island migration) and
		 * populations.
		 */
		islands = gtk_adjustment_get_value(b->wins.islands);
		islandpop = gtk_adjustment_get_value(b->wins.pop);
		break;
	case (INPUT_VARIABLE):
		/*
		 * Variable number of islands.
		 * We also add a check to see if we're really running
		 * with different island sizes or not.
		 */
		list = gtk_container_get_children
			(GTK_CONTAINER(b->wins.mapbox));
		islands = g_list_length(list);
		islandpops = g_malloc0_n(islands, sizeof(size_t));
		for (i = 0; i < islands; i++) {
			w = GTK_WIDGET(g_list_nth_data(list, i));
			if ( ! mapbox2pair(err, w, &islandpops[i]))
				goto cleanup;
		}
		g_list_free(list);
		break;
	case (INPUT_MAPPED):
		/*
		 * Possibly variable island sizes, possibly variable
		 * inter-island migration.
		 */
		if (gtk_toggle_button_get_active(b->wins.mapfromfile)) {
			/* Try to read KML from file. */
			file = gtk_file_chooser_get_filename
				(b->wins.mapfile);
			if (NULL == file) {
				gtk_label_set_text(err, "Error: "
					"map file not specified.");
				gtk_widget_show_all(GTK_WIDGET(err));
				goto cleanup;
			} 
			er = NULL;
			kml = kml_parse(file, &er);
			g_free(file);
			if (NULL == kml) {
				file = g_strdup_printf("Error: "
					"bad map file: %s", 
					NULL != er ? er->message :
					"cannot load file");
				gtk_label_set_text(err, file);
				gtk_widget_show_all(GTK_WIDGET(err));
				g_free(file);
				g_error_free(er);
				goto cleanup;
			}
		} else {
			islands = gtk_adjustment_get_value
				(b->wins.mapislands);
			islandpop = gtk_adjustment_get_value
				(b->wins.mapislanders);
			kml = kml_rand(islands, islandpop);
			islandpop = islands = 0;
		}

		/*
		 * Grok our island populations from the input file.
		 * This will have a reasonable default, but make sure
		 * anyway with some assertions.
		 */
		islands = (size_t)g_list_length(kml->kmls);
		islandpops = g_malloc0_n(islands, sizeof(size_t));
		for (i = 0; i < islands; i++) {
			kmlp = g_list_nth_data(kml->kmls, i);
			islandpops[i] = kmlp->pop;
		}

		/*
		 * If we're uniformly migrating, then stop processing
		 * right now.
		 * If we're distance-migrating, then have the kml file
		 * set the inter-island migration probabilities.
		 */
		switch (migrants) {
		case (MAPMIGRANT_UNIFORM):
			ms = kml_migration_distance(kml->kmls);
			break;
		case (MAPMIGRANT_NEAREST):
			ms = kml_migration_nearest(kml->kmls);
			break;
		default:
			break;
		}
		break;
	default:
		abort();
	}

	/*
	 * Base check: we need at least two islands.
	 */
	if (islands < 2) {
		gtk_label_set_text(err, 
			"Error: need at least two islands.");
		gtk_widget_show_all(GTK_WIDGET(err));
		goto cleanup;
	}

	/*
	 * If we have an array of island populations, make sure that
	 * each has more than two islanders.
	 * While here, revert to a single island population size if our
	 * sizes are, in fact, uniform.
	 * Calculate our total population size as well.
	 */
	if (NULL != islandpops) {
		g_assert(0 == islandpop);
		for (i = 0; i < islands; i++) {
			if (islandpops[i] > 1)
				continue;
			gtk_label_set_text(err, "Error: need at "
				"least two islanders per island.");
			gtk_widget_show_all(GTK_WIDGET(err));
			goto cleanup;
		}
		for (totalpop = i = 0; i < islands; i++)
			totalpop += islandpops[i];

		/* Check for uniformity. */
		for (i = 1; i < islands; i++)
			if (islandpops[i] != islandpops[0])
				break;
		if (i == islands) {
			g_debug("Reverting to uniform island "
				"populations: all islands have "
				"the same: %zu", islandpops[0]);
			islandpop = islandpops[0];
			g_free(islandpops);
			islandpops = NULL;
		}
	} 
	
	/*
	 * Handle uniform island sizes.
	 * (This isn't part of the conditional above because we might
	 * set ourselves to be uniform whilst processing.)
	 */
	if (NULL == islandpops && islandpop < 2) {
		gtk_label_set_text(err, "Error: need at "
			"least two islanders per island.");
		gtk_widget_show_all(GTK_WIDGET(err));
		goto cleanup;
	} else if (NULL == islandpops)
		totalpop = islands * islandpop;

	for (mutants = 0; mutants < MUTANTS__MAX; mutants++)
		if (gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON(b->wins.mutants[mutants])))
			break;

	if ( ! entry2double(b->wins.xmin, &xmin, err))
		goto cleanup;
	if ( ! entry2double(b->wins.xmax, &xmax, err))
		goto cleanup;
	if ( ! entryworder(b->wins.xmin, b->wins.xmax, xmin, xmax, err))
		goto cleanup;
	if (MUTANTS_GAUSSIAN == mutants) {
		if ( ! entry2double(b->wins.ymin, &ymin, err))
			goto cleanup;
		if ( ! entry2double(b->wins.ymax, &ymax, err))
			goto cleanup;
		if ( ! entryorder(b->wins.ymin, 
				b->wins.xmin, ymin, xmin, err))
			goto cleanup;
		if ( ! entryorder(b->wins.xmax, 
				b->wins.ymax, xmax, ymax, err))
			goto cleanup;
		if ( ! entryworder(b->wins.ymin, 
				b->wins.ymax, ymin, ymax, err))
			goto cleanup;
		if ( ! entry2double(b->wins.mutantsigma, &sigma, err))
			goto cleanup;
	}
	if ( ! entry2double(b->wins.alpha, &alpha, err))
		goto cleanup;
	if ( ! entry2double(b->wins.delta, &delta, err))
		goto cleanup;
	if ( ! entry2double(b->wins.migrate[input], &m, err))
		goto cleanup;
	if ( ! entry2size(b->wins.incumbents, &slices, err, 1))
		goto cleanup;
	if ( ! entry2func(b->wins.func, &exp, err))
		goto cleanup;

	func = gtk_entry_get_text(b->wins.func);
	name = gtk_entry_get_text(b->wins.name);

	if ('\0' == *name)
		name = "unnamed";

	/* 
	 * All parameters check out!
	 * Allocate the simulation now.
	 */
	if (button == b->wins.buttonrange) {
		if (0 == b->rangeid) {
			g_debug("Starting rangefinder");
			b->rangeid = g_idle_add
				((GSourceFunc)on_rangefind_idle, b);
		} else 
			g_debug("Re-using rangefinder");

		hnode_free(b->range.exp);
		if (NULL != islandpops) {
			b->range.n = 0;
			for (i = 0; i < islands; i++)
				b->range.n = islandpops[i] > b->range.n ? 
					islandpops[i] : b->range.n;
		} else
			b->range.n = islandpop;

		b->range.exp = exp;
		b->range.alpha = alpha;
		b->range.delta = delta;
		b->range.slices = slices;
		b->range.slicex = b->range.slicey = 0;
		b->range.piaggr = 0.0;
		b->range.picount = 0;
		b->range.pimin = DBL_MAX;
		b->range.pimax = -DBL_MAX;
		b->range.xmin = b->range.ymin = xmin;
		b->range.xmax = b->range.ymax = xmax;
		if (MUTANTS_GAUSSIAN == mutants) {
			b->range.ymin = ymin;
			b->range.ymax = ymax;
		}
		exp = NULL;
		file = g_strdup_printf
			("%s, X=[%g, %g), Y=[%g, %g), n=%zu",
			func, b->range.xmin, b->range.xmax, 
			b->range.ymin, b->range.ymax,
			b->range.n);
		gtk_label_set_text(b->wins.rangefunc, file);
		gtk_widget_hide(GTK_WIDGET(b->wins.rangeerrorbox));
		g_free(file);
		gtk_widget_set_visible
			(GTK_WIDGET(b->wins.rangefind), TRUE);
		goto cleanup;
	}

	gtk_widget_hide(GTK_WIDGET(err));

	sim = g_malloc0(sizeof(struct sim));
	sim->dims = slices;
	sim->islands = islands;
	sim->mutants = mutants;
	sim->mutantsigma = sigma;
	sim->func = g_strdup(func);
	sim->name = g_strdup(name);
	sim->fitpoly = gtk_adjustment_get_value(b->wins.fitpoly);
	sim->weighted = gtk_toggle_button_get_active(b->wins.weighted);
	sim->kml = kml;
	g_mutex_init(&sim->hot.mux);
	g_cond_init(&sim->hot.cond);

	/* Source for the fraction of mutants. */
	sim->bufs.fractions = kdata_bucket_alloc(0, slices);
	g_assert(NULL != sim->bufs.fractions);
	sim->bufs.mutants = kdata_bucket_alloc(0, slices);
	g_assert(NULL != sim->bufs.mutants);
	sim->bufs.incumbents = kdata_bucket_alloc(0, slices);
	g_assert(NULL != sim->bufs.incumbents);

	sim->bufs.means = simbuf_alloc
		(kdata_mean_alloc(sim->bufs.fractions), slices);
	sim->bufs.stddevs = simbuf_alloc
		(kdata_stddev_alloc(sim->bufs.fractions), slices);
	sim->bufs.mextinct = simbuf_alloc
		(kdata_mean_alloc(sim->bufs.mutants), slices);
	sim->bufs.iextinct = simbuf_alloc
		(kdata_mean_alloc(sim->bufs.incumbents), slices);

	sim->hot.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->hot.statslsb = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->hot.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->hot.islandslsb = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->warm.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->warm.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->cold.stats = g_malloc0_n
		(sim->dims, sizeof(struct stats));
	sim->warm.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->warm.smeans = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->warm.sextms = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.islands = g_malloc0_n
		(sim->islands, sizeof(struct stats));
	sim->cold.fits = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.coeffs = g_malloc0_n
		(sim->fitpoly + 1, sizeof(double));
	sim->cold.smeans = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.sextms = g_malloc0_n
		(sim->dims, sizeof(double));
	sim->cold.fitmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.fitmins);
	sim->cold.smeanmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.smeanmins);
	sim->cold.sextmmaxs = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.sextmmaxs);
	sim->cold.meanmins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.meanmins);
	sim->cold.extmmaxs = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.extmmaxs);
	sim->cold.extimins = gsl_histogram_alloc(sim->dims);
	g_assert(NULL != sim->cold.extimins);
	gsl_histogram_set_ranges_uniform
		(sim->cold.fitmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.smeanmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.sextmmaxs, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.meanmins, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.extmmaxs, xmin, xmax);
	gsl_histogram_set_ranges_uniform
		(sim->cold.extimins, xmin, xmax);

	/*
	 * Conditionally allocate our fitness polynomial structures.
	 * These are per-simulation as they're only run by one thread at
	 * any one time during the simulation.
	 */
	if (sim->fitpoly) {
		sim->work.X = gsl_matrix_alloc
			(sim->dims, sim->fitpoly + 1);
		sim->work.y = gsl_vector_alloc(sim->dims);
		sim->work.w = gsl_vector_alloc(sim->dims);
		sim->work.c = gsl_vector_alloc(sim->fitpoly + 1);
		sim->work.cov = gsl_matrix_alloc
			(sim->fitpoly + 1, sim->fitpoly + 1);
		sim->work.work = gsl_multifit_linear_alloc
			(sim->dims, sim->fitpoly + 1);
	}

	sim->nprocs = gtk_adjustment_get_value(b->wins.nthreads);
	sim->totalpop = totalpop;
	sim->stop = stop;
	sim->alpha = alpha;
	sim->colour = b->nextcolour;
	b->nextcolour = (b->nextcolour + 1) % SIZE_COLOURS;
	sim->delta = delta;
	sim->m = m;
	sim->ms = ms;
	sim->pop = islandpop;
	sim->pops = islandpops;
	sim->input = input;
	sim->continuum.exp = exp;
	sim->continuum.xmin = xmin;
	sim->continuum.xmax = xmax;
	sim->continuum.ymin = ymin;
	sim->continuum.ymax = ymax;
	b->sims = g_list_append(b->sims, sim);
	sim_ref(sim, NULL);
	sim->threads = g_malloc0_n(sim->nprocs, sizeof(struct simthr));
	g_debug("New simulation: %zu islands, %zu total members "
		"(%s per island) (%zu generations)", 
		sim->islands, sim->totalpop, 
		NULL != sim->pops ? "variable" : "uniform", 
		sim->stop);
	g_debug("New %s migration, %g probability, %g(1 + %g pi)", 
		NULL != sim->ms ? "variable" : "uniform", 
		sim->m, sim->alpha, sim->delta);
	g_debug("New function %s, x = [%g, %g)", sim->func,
		sim->continuum.xmin, sim->continuum.xmax);
	g_debug("New threads: %zu", sim->nprocs);
	g_debug("New polynomial: %zu (%s)", 
		sim->fitpoly, sim->weighted ? 
		"weighted" : "unweighted");
	if (MUTANTS_GAUSSIAN == sim->mutants)
		g_debug("New Gaussian mutants: "
			"%g in [%g, %g]", sim->mutantsigma,
			sim->continuum.ymin, sim->continuum.ymax);
	else
		g_debug("New discrete mutants");

	/* Create the simulation threads. */
	for (i = 0; i < sim->nprocs; i++) {
		sim->threads[i].rank = i;
		sim->threads[i].sim = sim;
		sim->threads[i].thread = g_thread_new
			(NULL, simulation, &sim->threads[i]);
	}

	/* Create the simulation window. */
	cur = g_malloc0(sizeof(struct curwin));
	cur->view = VIEW_MEAN;
	window_init(b, cur, g_list_append(NULL, sim));

	/* 
	 * Initialise the name of our simulation. 
	 */
	donamefill(&b->wins);
	return;
cleanup:
	if (NULL != ms)
		for (i = 0; i < islands; i++)
			g_free(ms[i]);
	g_free(islandpops);
	g_free(ms);
	hnode_free(exp);
	kml_free(kml);
}
