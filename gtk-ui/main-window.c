#include "main-window.h"
#include "file-reader.h"
#include "etpan-gtk-tree-model.h"

#include <gtk/gtk.h>


/* data source */

static unsigned int get_n_columns(struct etpan_gtk_tree_data_source *
    datasource)
{
  (void) datasource;
  
  return 1;
}

static GType get_column_type(struct etpan_gtk_tree_data_source * datasource,
    unsigned int column_index)
{
  static GType column_types[] = {
    G_TYPE_STRING,
  };
  (void) datasource;
  
  return column_types[column_index];
}

static unsigned int get_children_count(struct etpan_gtk_tree_data_source *
    datasource, void * item)
{
  struct sample_node * node;
  
  node = item;
  if (node == NULL)
    node = datasource->data;
  
  return carray_count(node->children);
}

static int item_has_child(struct etpan_gtk_tree_data_source *
    datasource, void * item)
{
  return get_children_count(datasource, item);
}

static void * get_child_item(struct etpan_gtk_tree_data_source *
    datasource, void * item, unsigned int index)
{
  struct sample_node * node;
  
  node = item;
  if (node == NULL)
    node = datasource->data;
  
  if (index >= carray_count(node->children))
    return NULL;
  
  return carray_get(node->children, index);
}

static void get_item_value_description(struct etpan_gtk_tree_data_source *
    datasource, void * item,
    GValue * value)
{
  struct sample_node * node;
  char * description;
  
  node = item;
  if (node == NULL)
    node = datasource->data;
  
  description = node->description;
  
  if (description == NULL)
    description = "";
  
  g_value_init(value, G_TYPE_STRING);
  g_value_set_string(value, description);
}

static void get_item_value(struct etpan_gtk_tree_data_source *
    datasource, void * item, unsigned int column_index,
    GValue * value)
{
  get_item_value_description(datasource, item, value);
}

static struct etpan_gtk_tree_data_source datasource;

static gboolean close_handler(GtkWidget * widget,
    GdkEvent * event,
    gpointer user_data)
{
  gtk_main_quit();
  
  return TRUE;
}

void sample_main_window_init(struct sample_node * node)
{
  GtkWidget * window;
  GtkWidget * treeview;
  GtkWidget * scrolledwindow;
  etpan_gtk_tree_model * model;
  GtkTreeViewColumn * col_description;
  GtkCellRenderer * col_description_renderer;
  
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "sample");

  gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);

  model = etpan_gtk_tree_model_new();
  
  datasource.data = node;
  datasource.get_n_columns = get_n_columns;
  datasource.get_column_type = get_column_type;
  datasource.get_children_count = get_children_count;
  datasource.item_has_child = item_has_child;
  datasource.get_child_item = get_child_item;
  datasource.get_item_value = get_item_value;
  
  etpan_gtk_tree_model_set_datasource(model, &datasource);
  
  scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_show(scrolledwindow);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow),
      GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
      GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  
  gtk_container_add(GTK_CONTAINER(window), scrolledwindow);
  
  treeview = gtk_tree_view_new();
  
  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeview), 0);
  gtk_tree_view_set_model(GTK_TREE_VIEW(treeview),
      GTK_TREE_MODEL(model));
  
  gtk_widget_show(treeview);
  gtk_container_add(GTK_CONTAINER(scrolledwindow), treeview);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), TRUE);
  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(treeview), FALSE);
  
  col_description = gtk_tree_view_column_new();
  col_description_renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col_description, col_description_renderer, TRUE);
  gtk_tree_view_column_set_attributes(col_description, col_description_renderer,
      "text", 0,
      NULL);
  gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col_description);
  
  gtk_tree_view_set_expander_column(GTK_TREE_VIEW(treeview), col_description);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
  
  gtk_widget_show(treeview);
  gtk_widget_show(scrolledwindow);
  gtk_widget_show(window);
  
  etpan_gtk_tree_model_reload(model);
  gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));
  
  g_signal_connect(window, "delete-event",
      G_CALLBACK(close_handler), NULL);
}
