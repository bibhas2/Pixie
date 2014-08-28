#include <gtk/gtk.h>

static gboolean delete_event( GtkWidget *widget,
                              GdkEvent  *event,
                              gpointer   data ) {
    gtk_main_quit ();

    return FALSE;
}

static void add_column(GtkWidget *view, const char *label, int width) {
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
	int count = gtk_tree_view_get_n_columns (GTK_TREE_VIEW (view));

	//printf("Num columns %d\n", count);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
		-1, label, renderer, "text", count, NULL);

	GtkTreeViewColumn *col = gtk_tree_view_get_column (GTK_TREE_VIEW (view),
			  count);
	if (width > 0) {
		gtk_tree_view_column_set_fixed_width(col, width);
	}
	gtk_tree_view_column_set_resizable (col, TRUE);
}

void add_request(GtkWidget *list, 
	const char* num, 
	const char* host, 
	const char* method, 
	const char* url) {

	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(list));

	gtk_list_store_append(GTK_LIST_STORE(model), &iter);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
		0, num,
		1, host,
		2, method,
		3, url,
		-1);
}

int main(int   argc, char *argv[] ) {
	GtkWidget *window;
    
	gtk_init (&argc, &argv);
    
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
	GtkWidget *topFrame = gtk_scrolled_window_new (NULL, NULL);
	GtkWidget *requestList = gtk_tree_view_new ();
	GtkWidget *bottomFrame = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
	GtkWidget *requestFrame = gtk_frame_new ("Request");
	GtkWidget *responseFrame = gtk_frame_new ("Response");

	gtk_widget_set_size_request (topFrame, -1, 240);
	gtk_widget_set_size_request (bottomFrame, -1, 240);
	gtk_widget_set_size_request (requestFrame, 320, -1);
	gtk_widget_set_size_request (responseFrame, 320, -1);
	gtk_widget_set_size_request (window, 640, 480);

	gtk_frame_set_shadow_type (GTK_FRAME (requestFrame), GTK_SHADOW_IN);
	gtk_frame_set_shadow_type (GTK_FRAME (responseFrame), GTK_SHADOW_IN);

	gtk_container_add (GTK_CONTAINER (topFrame), requestList);
	gtk_paned_add1(GTK_PANED(paned), topFrame);
	gtk_paned_add2(GTK_PANED(paned), bottomFrame);
	
	gtk_paned_add1(GTK_PANED(bottomFrame), requestFrame);
	gtk_paned_add2(GTK_PANED(bottomFrame), responseFrame);
	gtk_container_add (GTK_CONTAINER (window), paned);

	g_signal_connect (window, "delete-event",
		      G_CALLBACK (delete_event), NULL);

	add_column(requestList, "#", -1);
	add_column(requestList, "Host", 350);
	add_column(requestList, "Method", -1);
	add_column(requestList, "URL", 300);

	GtkListStore *store = gtk_list_store_new(4, 
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(requestList), 
		GTK_TREE_MODEL(store));
	g_object_unref(store);

	add_request(requestList, "1", "host1", "POST", "/mama/papa");
	add_request(requestList, "1", "host1", "POST", "/mama/papa");

	//Create the tabs
	GtkWidget *notebook, *tabLabel, *scroll;
	GtkWidget *requestRaw, *requestFormatted, *responseRaw, *responseFormatted;

	notebook = gtk_notebook_new();

	scroll = gtk_scrolled_window_new(NULL, NULL);
	requestRaw = gtk_text_view_new();
	gtk_container_add (GTK_CONTAINER (scroll), requestRaw);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
		scroll, gtk_label_new("Raw"));
	scroll = gtk_scrolled_window_new(NULL, NULL);
	requestFormatted = gtk_text_view_new();
	gtk_container_add (GTK_CONTAINER (scroll), requestFormatted);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
		scroll, gtk_label_new("Formatted"));

	gtk_container_add (GTK_CONTAINER (requestFrame), notebook);


	notebook = gtk_notebook_new();

	scroll = gtk_scrolled_window_new(NULL, NULL);
	responseRaw = gtk_text_view_new();
	gtk_container_add (GTK_CONTAINER (scroll), responseRaw);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
		scroll, gtk_label_new("Raw"));
	scroll = gtk_scrolled_window_new(NULL, NULL);
	responseFormatted = gtk_text_view_new();
	gtk_container_add (GTK_CONTAINER (scroll), responseFormatted);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), 
		scroll, gtk_label_new("Formatted"));

	gtk_container_add (GTK_CONTAINER (responseFrame), notebook);

	gtk_widget_show_all  (window);
    
	gtk_main ();

	return 0;
}
