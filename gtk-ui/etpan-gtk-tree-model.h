#ifndef ETPAN_GTK_TREE_MODEL_H

#define ETPAN_GTK_TREE_MODEL_H

#include "etpan-gtk-tree-model-types.h"

/* a datasource must be implemented and set to the model
   using etpan_gtk_tree_model_set_datasource() */

/* public API */

etpan_gtk_tree_model * etpan_gtk_tree_model_new(void);

void etpan_gtk_tree_model_set_datasource(etpan_gtk_tree_model * tree_model,
    struct etpan_gtk_tree_data_source * datasource);

#if 0
/* notify after changed */
void etpan_gtk_tree_model_row_changed(etpan_gtk_tree_model * ep_model,
    void * item);

/* notify after insert */
void etpan_gtk_tree_model_row_inserted(etpan_gtk_tree_model * ep_model,
    void * parent_item, void * item);

/* notify after deleting */
void etpan_gtk_tree_model_row_delete(etpan_gtk_tree_model * ep_model,
    void * item);
#endif

void etpan_gtk_tree_model_get_iter_from_item(etpan_gtk_tree_model * ep_model,
    GtkTreeIter * iter, void * item);

void * etpan_gtk_tree_model_get_item(etpan_gtk_tree_model * ep_model,
    GtkTreeIter * iter);

int etpan_gtk_tree_model_get_item_index(etpan_gtk_tree_model * ep_model,
    void * item);

/* currently very inefficient */
void etpan_gtk_tree_model_reload(etpan_gtk_tree_model * ep_model);

/* private API */

GType etpan_gtk_tree_model_get_type(void);

#endif
