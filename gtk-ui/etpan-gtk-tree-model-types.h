#ifndef ETPAN_GTK_TREE_MODEL_TYPES_H

#define ETPAN_GTK_TREE_MODEL_TYPES_H

#include <gtk/gtk.h>

struct _etpan_gtk_tree_model;
struct _etpan_gtk_tree_model_class;

typedef struct _etpan_gtk_tree_model etpan_gtk_tree_model;
typedef struct _etpan_gtk_tree_model_class  etpan_gtk_tree_model_class;

struct etpan_gtk_tree_data_source {
  void * data;
  
  unsigned int (* get_n_columns)(struct etpan_gtk_tree_data_source *
      datasource);
  
  GType (* get_column_type)(struct etpan_gtk_tree_data_source * datasource,
      unsigned int column_index);
  
  unsigned int (* get_children_count)(struct etpan_gtk_tree_data_source *
      datasource, void * item);

  int (* item_has_child)(struct etpan_gtk_tree_data_source *
      datasource, void * item);
  
  void * (* get_child_item)(struct etpan_gtk_tree_data_source *
      datasource, void * item, unsigned int index);
  
  void (* get_item_value)(struct etpan_gtk_tree_data_source *
      datasource, void * item, unsigned int column_index,
      GValue * value);
};

struct etpan_gtk_tree_drag_source {
  int (* item_draggable)(struct etpan_gtk_tree_data_source *
      datasource, void * item);
  
  int (* delete_item)(struct etpan_gtk_tree_data_source *
      datasource, void * item);
  
  int (* drag_data_get)(struct etpan_gtk_tree_data_source *
      datasource, void * item,
      GtkSelectionData * selection_data);
};

struct etpan_gtk_tree_drag_dest {
  int (* drag_data_received)(struct etpan_gtk_tree_data_source *
      datasource, void * item, GtkSelectionData * selection_data);
  
  int (* drop_possible)(struct etpan_gtk_tree_data_source * datasource,
      void * item, GtkSelectionData * selection_data);
};


#define ETPAN_GTK_TYPE_TREE_MODEL (gtk_tree_store_get_type())

#define ETPAN_GTK_TREE_MODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
  ETPAN_GTK_TYPE_TREE_MODEL, etpan_gtk_tree_model))

#define ETPAN_GTK_TREE_MODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
  ETPAN_GTK_TYPE_TREE_MODEL, etpan_gtk_tree_model_class))

#define ETPAN_GTK_IS_TREE_MODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
  ETPAN_GTK_TYPE_TREE_MODEL))

#define ETPAN_GTK_IS_TREE_MODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), ETPAN_GTK_TYPE_TREE_MODEL))

#define ETPAN_GTK_TREE_MODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), \
  ETPAN_GTK_TYPE_TREE_MODEL, etpan_gtk_tree_model_class))


#endif
