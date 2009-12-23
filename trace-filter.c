#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "trace-cmd.h"
#include "trace-local.h"
#include "trace-view-store.h"

#define DIALOG_WIDTH	400
#define DIALOG_HEIGHT	600

static void cpu_mask_set(guint64 *mask, gint cpu)
{
	mask += (cpu >> 6);
	*mask |= 1ULL << (cpu & ((1ULL << 6) - 1));
}

static void cpu_mask_clear(guint64 *mask, gint cpu)
{
	mask += (cpu >> 6);
	*mask &= ~(1ULL << (cpu & ((1ULL << 6) - 1)));
}

static gboolean cpu_mask_isset(guint64 *mask, gint cpu)
{
	mask += (cpu >> 6);
	return *mask & (1ULL << (cpu & ((1ULL << 6) - 1)));
}

struct dialog_helper {
	GtkWidget		*dialog;
	GtkWidget		*trace_tree;
	gpointer		data;
};

enum {
	COL_EVENT,
	NUM_EVENT_COLS,
};

static GtkTreeModel *
create_tree_event_model(GtkWidget *tree_view)
{
	GtkTreeModel *model;
	TraceViewStore *trace_view;
	GtkTreeStore *treestore;
	GtkTreeIter iter_all, iter_sys, iter_events;
	struct pevent *pevent;
	struct event_format **events;
	struct event_format *event;
	char *last_system = NULL;
	gint i;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
	trace_view = TRACE_VIEW_STORE(model);

	pevent = tracecmd_get_pevent(trace_view->handle);

	treestore = gtk_tree_store_new(NUM_EVENT_COLS, G_TYPE_STRING);

	gtk_tree_store_append(treestore, &iter_all, NULL);
	gtk_tree_store_set(treestore, &iter_all,
			   COL_EVENT,	"All",
			   -1);

	events = pevent_list_events(pevent, EVENT_SORT_SYSTEM);
	if (!events)
		return GTK_TREE_MODEL(treestore);

	for (i = 0; events[i]; i++) {
		event = events[i];
		if (!last_system || strcmp(last_system, event->system) != 0) {
			gtk_tree_store_append(treestore, &iter_sys, &iter_all);
			gtk_tree_store_set(treestore, &iter_sys,
					   COL_EVENT, event->system,
					   -1);
			last_system = event->system;
		}

		gtk_tree_store_append(treestore, &iter_events, &iter_sys);
		gtk_tree_store_set(treestore, &iter_events,
				   COL_EVENT, event->name,
				   -1);

	}

	return GTK_TREE_MODEL(treestore);
}

static GtkWidget *create_event_list_view(GtkWidget *tree_view)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkWidget *view;
	GtkTreeModel *model;

	view = gtk_tree_view_new();

	/* --- events column --- */

	col = gtk_tree_view_column_new();

	gtk_tree_view_column_set_title(col, "Events");

	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	renderer  = gtk_cell_renderer_text_new();

	gtk_tree_view_column_pack_start(col, renderer, TRUE);

	gtk_tree_view_column_add_attribute(col, renderer, "text", COL_EVENT);

	model = create_tree_event_model(tree_view);

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);

	g_object_unref(model);

	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
				    GTK_SELECTION_MULTIPLE);

	gtk_tree_view_expand_all(GTK_TREE_VIEW(view));

	return view;
}


/* Callback for the clicked signal of the Events filter button */
static void
event_dialog_response (gpointer data, gint response_id)
{
	struct dialog_helper *helper = data;

	switch (response_id) {
	case GTK_RESPONSE_ACCEPT:
		printf("accept!\n");
		break;
	case GTK_RESPONSE_REJECT:
		printf("reject!\n");
		break;
	default:
		break;
	};

	gtk_widget_destroy(GTK_WIDGET(helper->dialog));

	g_free(helper);
}

void trace_filter_event_dialog(void *trace_tree)
{
	GtkWidget *tree_view = GTK_WIDGET(trace_tree);
	struct dialog_helper *helper;
	GtkWidget *dialog;
	GtkWidget *scrollwin;
	GtkWidget *view;

	helper = g_malloc(sizeof(*helper));

	/* --- Make dialog window --- */

	dialog = gtk_dialog_new_with_buttons("Filter Events",
					     NULL,
					     GTK_DIALOG_MODAL,
					     "Apply",
					     GTK_RESPONSE_ACCEPT,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_REJECT,
					     NULL);

	helper->dialog = dialog;
	helper->trace_tree = tree_view;

	/* We can attach the Quit menu item to our exit function */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (event_dialog_response),
				  (gpointer) helper);

	scrollwin = gtk_scrolled_window_new(NULL, NULL);
	view = create_event_list_view(tree_view);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), scrollwin, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(scrollwin), view);

	gtk_widget_set_size_request(GTK_WIDGET(dialog),
				    DIALOG_WIDTH, DIALOG_HEIGHT);

	gtk_widget_show_all(dialog);
}

struct cpu_filter_helper {
	gboolean allcpus;
	guint64 *cpu_mask;
};

/* Callback for the clicked signal of the CPUS filter button */
static void
cpu_dialog_response (gpointer data, gint response_id)
{
	struct dialog_helper *helper = data;
	struct cpu_filter_helper *cpu_helper = helper->data;
	GtkTreeView *view = GTK_TREE_VIEW(helper->trace_tree);
	TraceViewStore *store;
	gint cpus, cpu;

	store = TRACE_VIEW_STORE(gtk_tree_view_get_model(view));

	switch (response_id) {
	case GTK_RESPONSE_ACCEPT:
		g_object_ref(store);
		gtk_tree_view_set_model(view, NULL);

		if (cpu_helper->allcpus) {
			trace_view_store_set_all_cpus(store);
			gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
			g_object_unref(store);
			break;
		}
		cpus = trace_view_store_get_cpus(store);

		for (cpu = 0; cpu < cpus; cpu++) {
			if (cpu_mask_isset(cpu_helper->cpu_mask, cpu))
				trace_view_store_set_cpu(store, cpu);
			else
				trace_view_store_clear_cpu(store, cpu);
		}
		gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
		g_object_unref(store);
		break;

	case GTK_RESPONSE_REJECT:
		break;
	default:
		break;
	};

	gtk_widget_destroy(GTK_WIDGET(helper->dialog));

	g_free(cpu_helper->cpu_mask);
	g_free(cpu_helper);
	g_free(helper);
}

#define CPU_ALL_CPUS_STR "All CPUs"

void cpu_toggle(gpointer data, GtkWidget *widget)
{
	struct cpu_filter_helper *cpu_helper = data;
	const gchar *label;
	gboolean active;
	gint cpu;

	label = gtk_button_get_label(GTK_BUTTON(widget));
	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

	if (strcmp(label, CPU_ALL_CPUS_STR) == 0) {
		cpu_helper->allcpus = active;
		return;
	}

	/* Get the CPU # from the label. Pass "CPU " */
	cpu = atoi(label + 4);
	if (active)
		cpu_mask_set(cpu_helper->cpu_mask, cpu);
	else
		cpu_mask_clear(cpu_helper->cpu_mask, cpu);

}

void trace_filter_cpu_dialog(void *trace_tree)
{
	GtkWidget *tree_view = GTK_WIDGET(trace_tree);
	TraceViewStore *store;
	struct dialog_helper *helper;
	struct cpu_filter_helper *cpu_helper;
	GtkWidget *dialog;
	GtkWidget *scrollwin;
	GtkWidget *viewport;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *check;
	GtkRequisition req;
	gchar	counter[100];
	gint width, height;
	gint allset;
	gint cpus;
	gint cpu;

	store = TRACE_VIEW_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view)));

	cpus = trace_view_store_get_cpus(store);

	helper = g_malloc(sizeof(*helper));
	g_assert(helper != NULL);

	cpu_helper = g_new0(typeof(*cpu_helper), sizeof(*cpu_helper));
	g_assert(cpu_helper != NULL);

	helper->data = cpu_helper;

	/* --- Make dialog window --- */

	dialog = gtk_dialog_new_with_buttons("Filter CPUS",
					     NULL,
					     GTK_DIALOG_MODAL,
					     "Apply",
					     GTK_RESPONSE_ACCEPT,
					     GTK_STOCK_CANCEL,
					     GTK_RESPONSE_REJECT,
					     NULL);

	helper->dialog = dialog;
	helper->trace_tree = tree_view;

	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (cpu_dialog_response),
				  (gpointer) helper);

	scrollwin = gtk_scrolled_window_new(NULL, NULL);

	viewport = gtk_viewport_new(NULL, NULL);
	gtk_widget_show(viewport);

	gtk_container_add(GTK_CONTAINER(scrollwin), viewport);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), scrollwin, TRUE, TRUE, 0);

	/* Add hbox to center buttons. Is there a better way? */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(viewport), hbox);
	gtk_widget_show(hbox);

	vbox = gtk_vbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, FALSE, 0);
	gtk_widget_show(vbox);

	check = gtk_check_button_new_with_label("All CPUs");
	gtk_box_pack_start(GTK_BOX(vbox), check, TRUE, TRUE, 0);

	allset = trace_view_store_get_all_cpus(store);
	if (allset)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);

	g_signal_connect_swapped (check, "toggled",
				  G_CALLBACK (cpu_toggle),
				  (gpointer) cpu_helper);

	cpu_helper->allcpus = allset;
	cpu_helper->cpu_mask = g_new0(guint64, (cpus >> 6) + 1);
	g_assert(cpu_helper->cpu_mask != NULL);

	gtk_widget_show(check);

	for (cpu = 0; cpu < cpus; cpu++) {
		g_snprintf(counter, 100, "CPU %d", cpu);
		check = gtk_check_button_new_with_label(counter);
		gtk_box_pack_start(GTK_BOX(vbox), check, TRUE, FALSE, 0);
		if (allset || trace_view_store_cpu_isset(store, cpu)) {
			cpu_mask_set(cpu_helper->cpu_mask, cpu);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);
		}

		g_signal_connect_swapped (check, "toggled",
					  G_CALLBACK (cpu_toggle),
					  (gpointer) cpu_helper);

		gtk_widget_show(check);
	}

	/* Figure out a good size to show */
	gtk_widget_size_request(hbox, &req);

	height = req.height;

	gtk_widget_size_request(scrollwin, &req);

	height += req.height;

	gtk_widget_size_request(dialog, &req);

	width = req.width;
	height += req.height;

	if (width > DIALOG_WIDTH)
		width = DIALOG_WIDTH;
	if (height > DIALOG_HEIGHT)
		height = DIALOG_HEIGHT;

	gtk_widget_set_size_request(GTK_WIDGET(dialog),
				    width, height);

	gtk_widget_show_all(dialog);
}