#include "etpan-gtk-tree-model.h"

#include "carray.h"
#include "chash.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define ETPAN_LOG_MEMORY_ERROR do {} while (0)
#define ETPAN_LOG printf
#define etpan_crash();

#if 0
#define CACHE_DEBUG
#endif

#if DEBUG_WITH_INTERNAL
typedef enum
{
  GTK_RBNODE_BLACK = 1 << 0,
  GTK_RBNODE_RED = 1 << 1,
  GTK_RBNODE_IS_PARENT = 1 << 2,
  GTK_RBNODE_IS_SELECTED = 1 << 3,
  GTK_RBNODE_IS_PRELIT = 1 << 4,
  GTK_RBNODE_IS_SEMI_COLLAPSED = 1 << 5,
  GTK_RBNODE_IS_SEMI_EXPANDED = 1 << 6,
  GTK_RBNODE_INVALID = 1 << 7,
  GTK_RBNODE_COLUMN_INVALID = 1 << 8,
  GTK_RBNODE_DESCENDANTS_INVALID = 1 << 9,
  GTK_RBNODE_NON_COLORS = GTK_RBNODE_IS_PARENT |
  			  GTK_RBNODE_IS_SELECTED |
  			  GTK_RBNODE_IS_PRELIT |
                          GTK_RBNODE_IS_SEMI_COLLAPSED |
                          GTK_RBNODE_IS_SEMI_EXPANDED |
                          GTK_RBNODE_INVALID |
                          GTK_RBNODE_COLUMN_INVALID |
                          GTK_RBNODE_DESCENDANTS_INVALID
} GtkRBNodeColor;

typedef struct _GtkRBTree GtkRBTree;
typedef struct _GtkRBNode GtkRBNode;
typedef struct _GtkRBTreeView GtkRBTreeView;

typedef void (*GtkRBTreeTraverseFunc) (GtkRBTree  *tree,
                                       GtkRBNode  *node,
                                       gpointer  data);

struct _GtkRBTree
{
  GtkRBNode *root;
  GtkRBNode *nil;
  GtkRBTree *parent_tree;
  GtkRBNode *parent_node;
};

struct _GtkRBNode
{
  guint flags : 14;

  /* We keep track of whether the aggregate count of children plus 1
   * for the node itself comes to an even number.  The parity flag is
   * the total count of children mod 2, where the total count of
   * children gets computed in the same way that the total offset gets
   * computed. i.e. not the same as the "count" field below which
   * doesn't include children. We could replace parity with a
   * full-size int field here, and then take % 2 to get the parity flag,
   * but that would use extra memory.
   */

  guint parity : 1;
  
  GtkRBNode *left;
  GtkRBNode *right;
  GtkRBNode *parent;

  /* count is the number of nodes beneath us, plus 1 for ourselves.
   * i.e. node->left->count + node->right->count + 1
   */
  gint count;
  
  /* this is the total of sizes of
   * node->left, node->right, our own height, and the height
   * of all trees in ->children, iff children exists because
   * the thing is expanded.
   */
  gint offset;

  /* Child trees */
  GtkRBTree *children;
};

typedef struct _GtkTreeViewColumnReorder GtkTreeViewColumnReorder;
struct _GtkTreeViewColumnReorder
{
  gint left_align;
  gint right_align;
  GtkTreeViewColumn *left_column;
  GtkTreeViewColumn *right_column;
};

typedef void (*GtkTreeViewSearchDialogPositionFunc) (GtkTreeView *tree_view,
						     GtkWidget   *search_dialog);

struct _GtkTreeViewPrivate
{
  GtkTreeModel *model;

  guint flags;
  /* tree information */
  GtkRBTree *tree;

  GtkRBNode *button_pressed_node;
  GtkRBTree *button_pressed_tree;

  GList *children;
  gint width;
  gint height;
  gint expander_size;

  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;

  GdkWindow *bin_window;
  GdkWindow *header_window;
  GdkWindow *drag_window;
  GdkWindow *drag_highlight_window;
  GtkTreeViewColumn *drag_column;

  GtkTreeRowReference *last_button_press;
  GtkTreeRowReference *last_button_press_2;

  /* bin_window offset */
  GtkTreeRowReference *top_row;
  gint top_row_dy;
  /* dy == y pos of top_row + top_row_dy */
  /* we cache it for simplicity of the code */
  gint dy;
  gint drag_column_x;

  GtkTreeViewColumn *expander_column;
  GtkTreeViewColumn *edited_column;
  guint presize_handler_timer;
  guint validate_rows_timer;
  guint scroll_sync_timer;

  /* Focus code */
  GtkTreeViewColumn *focus_column;

  /* Selection stuff */
  GtkTreeRowReference *anchor;
  GtkTreeRowReference *cursor;

  /* Column Resizing */
  gint drag_pos;
  gint x_drag;

  /* Prelight information */
  GtkRBNode *prelight_node;
  GtkRBTree *prelight_tree;

  /* The node that's currently being collapsed or expanded */
  GtkRBNode *expanded_collapsed_node;
  GtkRBTree *expanded_collapsed_tree;
  guint expand_collapse_timeout;

  /* Selection information */
  GtkTreeSelection *selection;

  /* Header information */
  gint n_columns;
  GList *columns;
  gint header_height;

  GtkTreeViewColumnDropFunc column_drop_func;
  gpointer column_drop_func_data;
  GtkDestroyNotify column_drop_func_data_destroy;
  GList *column_drag_info;
  GtkTreeViewColumnReorder *cur_reorder;

  /* ATK Hack */
  GtkTreeDestroyCountFunc destroy_count_func;
  gpointer destroy_count_data;
  GtkDestroyNotify destroy_count_destroy;

  /* Scroll timeout (e.g. during dnd) */
  guint scroll_timeout;

  /* Row drag-and-drop */
  GtkTreeRowReference *drag_dest_row;
  GtkTreeViewDropPosition drag_dest_pos;
  guint open_dest_timeout;

  gint pressed_button;
  gint press_start_x;
  gint press_start_y;

  /* fixed height */
  gint fixed_height;

  /* Scroll-to functionality when unrealized */
  GtkTreeRowReference *scroll_to_path;
  GtkTreeViewColumn *scroll_to_column;
  gfloat scroll_to_row_align;
  gfloat scroll_to_col_align;
  guint scroll_to_use_align : 1;

  guint fixed_height_mode : 1;
  guint fixed_height_check : 1;

  guint reorderable : 1;
  guint header_has_focus : 1;
  guint drag_column_window_state : 3;
  /* hint to display rows in alternating colors */
  guint has_rules : 1;
  guint mark_rows_col_dirty : 1;

  /* for DnD */
  guint empty_view_drop : 1;

  guint ctrl_pressed : 1;
  guint shift_pressed : 1;


  guint init_hadjust_value : 1;

  /* interactive search */
  guint enable_search : 1;
  guint disable_popdown : 1;
  
  guint hover_selection : 1;
  guint hover_expand : 1;
  guint imcontext_changed : 1;

  /* Auto expand/collapse timeout in hover mode */
  guint auto_expand_timeout;

  gint selected_iter;
  gint search_column;
  GtkTreeViewSearchDialogPositionFunc search_dialog_position_func;
  GtkTreeViewSearchEqualFunc search_equal_func;
  gpointer search_user_data;
  GtkDestroyNotify search_destroy;
  GtkWidget *search_window;
  GtkWidget *search_entry;
  guint search_entry_changed_id;
  guint typeselect_flush_timeout;

  gint prev_width;

  GtkTreeViewRowSeparatorFunc row_separator_func;
  gpointer row_separator_data;
  GtkDestroyNotify row_separator_destroy;
};

gboolean
_gtk_tree_view_find_node (GtkTreeView  *tree_view,
			  GtkTreePath  *path,
			  GtkRBTree   **tree,
			  GtkRBNode   **node)
{
  GtkRBNode *tmpnode = NULL;
  GtkRBTree *tmptree = tree_view->priv->tree;
  gint *indices = gtk_tree_path_get_indices (path);
  gint depth = gtk_tree_path_get_depth (path);
  gint i = 0;

  *node = NULL;
  *tree = NULL;

  if (depth == 0 || tmptree == NULL)
    return FALSE;
  do
    {
      tmpnode = _gtk_rbtree_find_count (tmptree, indices[i] + 1);
      ++i;
      if (tmpnode == NULL)
	{
	  *tree = NULL;
	  *node = NULL;
	  return FALSE;
	}
      if (i >= depth)
	{
	  *tree = tmptree;
	  *node = tmpnode;
	  return FALSE;
	}
      *tree = tmptree;
      *node = tmpnode;
      tmptree = tmpnode->children;
      if (tmptree == NULL)
	return TRUE;
    }
  while (1);
}

GtkRBNode *
_gtk_rbtree_find_count (GtkRBTree *tree,
			gint       count)
{
  GtkRBNode *node;

  node = tree->root;
  while (node != tree->nil && (node->left->count + 1 != count))
    {
      if (node->left->count >= count)
	node = node->left;
      else
	{
	  count -= (node->left->count + 1);
	  node = node->right;
	}
    }
  if (node == tree->nil)
    return NULL;
  return node;
}
#endif


static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct _etpan_gtk_tree_model {
  GObject parent;
  struct etpan_gtk_tree_data_source * datasource;
  chash * cache;
};

struct _etpan_gtk_tree_model_class {
  GObjectClass parent_class;
};

static void etpan_gtk_tree_model_class_init(etpan_gtk_tree_model_class * class);
static void tree_model_init(GtkTreeModelIface *iface);
static void tree_model_finalize(etpan_gtk_tree_model * tree_model);
static void drag_source_init(GtkTreeDragSourceIface * iface);
static void drag_dest_init(GtkTreeDragDestIface * iface);
static void etpan_gtk_tree_model_init(etpan_gtk_tree_model * tree_model);
static GtkTreeModelFlags get_flags(GtkTreeModel * tree_model);
static gint get_n_columns(GtkTreeModel * tree_model);
static GType get_column_type(GtkTreeModel * tree_model, gint index);
static gboolean get_iter(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreePath * path);
static GtkTreePath * get_path(GtkTreeModel * tree_model,
    GtkTreeIter * iter);
static void get_value(GtkTreeModel * tree_model,
    GtkTreeIter * iter, gint column, GValue * value);
static gboolean iter_next(GtkTreeModel * tree_model,
    GtkTreeIter * iter);
static gboolean iter_children(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * parent);
static gboolean iter_has_child(GtkTreeModel * tree_model,
    GtkTreeIter * iter);
static gint iter_n_children(GtkTreeModel * tree_model,
    GtkTreeIter * iter);
static gboolean iter_nth_child (GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * parent, gint n);
static gboolean iter_parent(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * child);
static gboolean row_draggable(GtkTreeDragSource * drag_source,
    GtkTreePath * path);
static gboolean drag_data_delete(GtkTreeDragSource * drag_source,
    GtkTreePath * path);
static gboolean drag_data_get(GtkTreeDragSource * drag_source,
    GtkTreePath * path, GtkSelectionData  * selection_data);
static gboolean drag_data_received(GtkTreeDragDest * drag_dest,
    GtkTreePath * dest, GtkSelectionData * selection_data);
static gboolean row_drop_possible (GtkTreeDragDest * drag_dest,
    GtkTreePath * dest_path, GtkSelectionData * selection_data);


GType etpan_gtk_tree_model_get_type(void)
{
  static GType etpan_gtk_tree_model_type = 0;
  
  pthread_mutex_lock(&lock);
  
  if (etpan_gtk_tree_model_type == 0) {
    static const GTypeInfo etpan_gtk_tree_model_info = {
      .class_size = sizeof(etpan_gtk_tree_model_class),
      .base_init = NULL,
      .base_finalize = NULL,
      .class_init = (GClassInitFunc) etpan_gtk_tree_model_class_init,
      .class_finalize = NULL,
      .class_data = NULL,
      .instance_size = sizeof(etpan_gtk_tree_model),
      .n_preallocs = 0,
      .instance_init = (GInstanceInitFunc) etpan_gtk_tree_model_init,
      .value_table = NULL
    };
    
    static const GInterfaceInfo tree_model_info = {
      .interface_init = (GInterfaceInitFunc) tree_model_init,
      .interface_finalize = NULL,
      .interface_data = NULL,
    };
    
    static const GInterfaceInfo drag_source_info = {
      .interface_init = (GInterfaceInitFunc) drag_source_init,
      .interface_finalize = NULL,
      .interface_data = NULL,
    };
    
    static const GInterfaceInfo drag_dest_info = {
      .interface_init = (GInterfaceInitFunc) drag_dest_init,
      .interface_finalize = NULL,
      .interface_data = NULL,
    };
    
    etpan_gtk_tree_model_type = g_type_register_static(G_TYPE_OBJECT,
        "etpan_gtk_tree_model",
        &etpan_gtk_tree_model_info, 0);
    
    g_type_add_interface_static(etpan_gtk_tree_model_type,
        GTK_TYPE_TREE_MODEL, &tree_model_info);
    
    g_type_add_interface_static(etpan_gtk_tree_model_type,
        GTK_TYPE_TREE_DRAG_SOURCE, &drag_source_info);
    
    g_type_add_interface_static(etpan_gtk_tree_model_type,
        GTK_TYPE_TREE_DRAG_DEST, &drag_dest_info);
  }
  
  pthread_mutex_unlock(&lock);
  
  return etpan_gtk_tree_model_type;
}

static GObjectClass * parent_class = NULL;

static void etpan_gtk_tree_model_class_init(etpan_gtk_tree_model_class * class)
{
  GObjectClass * object_class;
  
  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass *) class;
  
  object_class->finalize = (GObjectFinalizeFunc) tree_model_finalize;
}

static void tree_model_init(GtkTreeModelIface *iface)
{
  iface->get_flags = get_flags;
  iface->get_n_columns = get_n_columns;
  iface->get_column_type = get_column_type;
  iface->get_iter = get_iter;
  iface->get_path = get_path;
  iface->get_value = get_value;
  iface->iter_next = iter_next;
  iface->iter_children = iter_children;
  iface->iter_has_child = iter_has_child;
  iface->iter_n_children = iter_n_children;
  iface->iter_nth_child = iter_nth_child;
  iface->iter_parent = iter_parent;
}

static void drag_source_init(GtkTreeDragSourceIface * iface)
{
  iface->row_draggable = row_draggable;
  iface->drag_data_delete = drag_data_delete;
  iface->drag_data_get = drag_data_get;
}

static void drag_dest_init(GtkTreeDragDestIface * iface)
{
  iface->drag_data_received = drag_data_received;
  iface->row_drop_possible = row_drop_possible;
}

static void etpan_gtk_tree_model_init(etpan_gtk_tree_model * tree_model)
{
  tree_model->cache = chash_new(CHASH_DEFAULTSIZE, CHASH_COPYKEY);
  if (tree_model->cache == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  tree_model->datasource = NULL;
}

etpan_gtk_tree_model * etpan_gtk_tree_model_new(void)
{
  GType type;
  etpan_gtk_tree_model * tree_model;
  
  type = etpan_gtk_tree_model_get_type();
  
  tree_model = g_object_new(type, NULL);
  
  return tree_model;
}

static void flush_cache(etpan_gtk_tree_model * ep_model);

static void tree_model_finalize(etpan_gtk_tree_model * tree_model)
{
  flush_cache(tree_model);
  chash_free(tree_model->cache);
}

/* helpers - begin */

struct cache_info {
  void * parent_item;
  void * item;
  unsigned int index;
  
  int cached_has_child;
  int has_child;
  carray * children;
};

static struct cache_info * cache_children(etpan_gtk_tree_model * ep_model,
    void * item);

static struct cache_info * get_cache_info(etpan_gtk_tree_model * ep_model,
    void * item);

static struct cache_info * store_cache_info(etpan_gtk_tree_model * ep_model,
    void * item)
{
  struct cache_info * info;
  chashdatum key;
  chashdatum value;
  int r;
  
  if (ep_model->datasource == NULL)
    return NULL;
  
  info = get_cache_info(ep_model, item);
  if (info != NULL)
    return info;
  
  info = malloc(sizeof(* info));
  if (info == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  info->parent_item = NULL;
  info->item = item;
  info->index = 0;
  info->cached_has_child = 0;
  info->has_child = 0;
  info->children = NULL;
  
  key.data = &item;
  key.len = sizeof(item);
  value.data = info;
  value.len = 0;
  
  r = chash_set(ep_model->cache, &key, &value, NULL);
  if (r < 0) {
    free(info);
    ETPAN_LOG_MEMORY_ERROR;
  }
  
  return info;
}

static struct cache_info * get_cache_info(etpan_gtk_tree_model * ep_model,
    void * item)
{
  chashdatum key;
  chashdatum value;
  int r;
  struct cache_info * info;
  
  if (ep_model->datasource == NULL)
    return NULL;
  
  key.data = &item;
  key.len = sizeof(item);
  
  r = chash_get(ep_model->cache, &key, &value);
  if (r < 0)
    return NULL;
  
  info = value.data;
  
  return info;
}

static void remove_cache_info(etpan_gtk_tree_model * ep_model,
    void * item)
{
  chashdatum key;
  struct cache_info * info;
  struct cache_info * parent_info;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return;
  
  parent_info = get_cache_info(ep_model, info->parent_item);
  if (info->children != NULL)
    carray_free(info->children);
  free(info);
  
  key.data = &item;
  key.len = sizeof(item);
  
  chash_delete(ep_model->cache, &key, NULL);
}

static void flush_cache(etpan_gtk_tree_model * ep_model)
{
  chashiter * iter;
  
  for(iter = chash_begin(ep_model->cache) ; iter != NULL ;
      iter = chash_next(ep_model->cache, iter)) {
    struct cache_info * info;
    chashdatum value;
    
    chash_value(iter, &value);
    info = value.data;
    
    if (info->children != NULL)
      carray_free(info->children);
    free(info);
  }
  
  chash_clear(ep_model->cache);
}


/* real access - begin */

static unsigned int real_get_count(etpan_gtk_tree_model * ep_model,
    void * item)
{
  int res;
  
  if (ep_model->datasource == NULL)
    return 0;
  
  res = ep_model->datasource->get_children_count(ep_model->datasource, item);
  return res;
}

static int real_get_has_child(etpan_gtk_tree_model * ep_model,
    void * item)
{
  int res;
  
  if (ep_model->datasource == NULL)
    return 0;
  
  res = ep_model->datasource->item_has_child(ep_model->datasource, item);
  return res;
}

static void * real_get_nth_child(etpan_gtk_tree_model * ep_model,
    void * item, unsigned int index)
{
  if (ep_model->datasource == NULL)
    return NULL;
  
  return ep_model->datasource->get_child_item(ep_model->datasource,
      item, index);
}

/* real access - end */


/* cached info - begin */

static int get_current_index(etpan_gtk_tree_model * ep_model,
    void * item)
{
  struct cache_info * info;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return -1;
  
  return info->index;
}

static unsigned int get_count(etpan_gtk_tree_model * ep_model,
    void * item)
{
  struct cache_info * info;
  
  info = cache_children(ep_model, item);
  if (info == NULL)
    return 0;
  
  if (info->children == NULL)
    return 0;

  return carray_count(info->children);
}

static void * get_nth_child(etpan_gtk_tree_model * ep_model,
    void * item, unsigned int index)
{
  void * child;
  struct cache_info * info;
  
  info = cache_children(ep_model, item);
  if (info == NULL)
    return NULL;
  
  if (index >= carray_count(info->children))
    return NULL;
  
  child = carray_get(info->children, index);

  info = get_cache_info(ep_model, child);
  if (info == NULL)
    return NULL;
  
  return child;
}

static struct cache_info * cache_children(etpan_gtk_tree_model * ep_model,
    void * item)
{
  unsigned int i;
  unsigned int count;
  struct cache_info * info;
  int r;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return NULL;
#if 0
  info = store_cache_info(ep_model, item);
  if (info == NULL)
    return NULL;
#endif
  
  if (info->children != NULL)
    return info;
  
  info->children = carray_new(16);
  if (info->children == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  count = real_get_count(ep_model, item);
  for(i = 0 ; i < count ; i ++) {
    void * child;
    struct cache_info * child_info;
    
    child = real_get_nth_child(ep_model, item, i);
    r = carray_add(info->children, child, NULL);
    if (r < 0)
      ETPAN_LOG_MEMORY_ERROR;
    
    child_info = store_cache_info(ep_model, child);
    child_info->parent_item = item;
    child_info->index = i;
#ifdef CACHE_DEBUG
    {
      GtkTreeIter iter;
      GtkTreePath * path;
      
      iter.user_data = child;
      path = gtk_tree_model_get_path(GTK_TREE_MODEL(ep_model), &iter);
      ETPAN_LOG("row cache (children) : %p %s", child, gtk_tree_path_to_string(path));
    }
#endif
  }
  
  return info;
}

static void * get_parent(etpan_gtk_tree_model * ep_model, void * item)
{
  struct cache_info * info;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return NULL;
  
  return info->parent_item;
}

static int get_has_child(etpan_gtk_tree_model * ep_model, void * item)
{
  struct cache_info * info;
  int has_child;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return 0;
  
  if (info->cached_has_child) {
    return info->has_child;
  }
  
  has_child = real_get_has_child(ep_model, item);
  info->has_child = has_child;
  info->cached_has_child = 1;
  
  return has_child;
}

/* cached info - end */

/* helpers - end */

static GtkTreeModelFlags get_flags(GtkTreeModel * tree_model)
{
  (void) tree_model;
  return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint get_n_columns(GtkTreeModel * tree_model)
{
  etpan_gtk_tree_model * ep_model;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (ep_model->datasource == NULL)
    return 0;
  
  return ep_model->datasource->get_n_columns(ep_model->datasource);
}

static GType get_column_type(GtkTreeModel * tree_model,
    gint index)
{
  etpan_gtk_tree_model * ep_model;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (ep_model->datasource == NULL)
    return G_TYPE_INVALID;
  
  return ep_model->datasource->get_column_type(ep_model->datasource,
      index);
}

static gboolean get_iter(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreePath * path)
{
  etpan_gtk_tree_model * ep_model;
  void * current_item;
  gint *indices;
  unsigned depth;
  unsigned int i;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  
  indices = gtk_tree_path_get_indices(path);
  depth = gtk_tree_path_get_depth(path);
  if (depth <= 0) {
    return FALSE;
  }
  
  if (get_count(ep_model, NULL) == 0)
    return FALSE;
  
  current_item = NULL;
  for(i = 0 ; i < depth; i++) {
    current_item = get_nth_child(ep_model, current_item, indices[i]);
    if (current_item == NULL)
      return FALSE;
  }
  
  iter->user_data = current_item;
  
  return TRUE;
}


static GtkTreePath * get_path(GtkTreeModel * tree_model,
    GtkTreeIter * iter)
{
  etpan_gtk_tree_model * ep_model;
  GtkTreePath * path;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  
  if (iter == NULL) {
    path = gtk_tree_path_new();
  }
  else {
    int index;
    GtkTreeIter parent_iter;
    void * item;
    void * parent_item;
    
    item = iter->user_data;
    parent_item = get_parent(ep_model, item);
    
    if (parent_item == NULL) {
      path = get_path(tree_model, NULL);
    }
    else {
      parent_iter.user_data = parent_item;
      path = get_path(tree_model, &parent_iter);
    }
    
    if (path == NULL)
      return NULL;
    
    index = get_current_index(ep_model, item);
    if (index < 0) {
      gtk_tree_path_free(path);
      return NULL;
    }
    
    gtk_tree_path_append_index(path, index);
  }
  
  return path;
}

static void get_value(GtkTreeModel * tree_model,
    GtkTreeIter * iter, gint column, GValue * value)
{
  etpan_gtk_tree_model * ep_model;
  void * item;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (ep_model->datasource == NULL)
    return;
  
  item = iter->user_data;
  
  ep_model->datasource->get_item_value(ep_model->datasource,
      item, column, value);
}

static gboolean iter_next(GtkTreeModel * tree_model,
    GtkTreeIter * iter)
{
  etpan_gtk_tree_model * ep_model;
  void * item;
  void * next_item;
  void * parent_item;
  unsigned int count;
  int index;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  item = iter->user_data;
  
  index = get_current_index(ep_model, item);
  
  parent_item = get_parent(ep_model, item);
  
  count = get_count(ep_model, parent_item);
  if (((unsigned int) (index + 1)) >= count)
    return FALSE;
  
  next_item = get_nth_child(ep_model, parent_item, index + 1);
  if (next_item == NULL)
    return FALSE;
  
  iter->user_data = next_item;
  
  return TRUE;
}

static gboolean iter_children(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * parent)
{
  etpan_gtk_tree_model * ep_model;
  void * child;
  void * item;
  
  if (!iter_has_child(tree_model, iter))
    return FALSE;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (parent == NULL)
    item = NULL;
  else
    item = parent->user_data;
  
  child = get_nth_child(ep_model, item, 0);
  if (child == NULL)
    return FALSE;
  
  iter->user_data = child;
  
  return TRUE;
}

static gboolean iter_has_child(GtkTreeModel * tree_model,
    GtkTreeIter * iter)
{
  etpan_gtk_tree_model * ep_model;
  void * item;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (iter == NULL)
    item = NULL;
  else
    item = iter->user_data;
  
  return get_has_child(ep_model, item);
}

static gint iter_n_children(GtkTreeModel * tree_model,
    GtkTreeIter * iter)
{
  etpan_gtk_tree_model * ep_model;
  void * item;
  unsigned int count;

  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (iter == NULL)
    item = NULL;
  else
    item = iter->user_data;
  
  count = get_count(ep_model, item);
  
  return count;
}

static gboolean iter_nth_child (GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * parent, gint n)
{
  etpan_gtk_tree_model * ep_model;
  void * item;
  void * child;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  if (parent == NULL)
    item = NULL;
  else
    item = parent->user_data;
  
  child = get_nth_child(ep_model, item, n);
  if (child == NULL)
    return FALSE;
  
  iter->user_data = child;
  
  return TRUE;
}

static gboolean iter_parent(GtkTreeModel * tree_model,
    GtkTreeIter * iter, GtkTreeIter * child)
{
  etpan_gtk_tree_model * ep_model;
  void * parent_item;
  void * item;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  item = child->user_data;
  parent_item = get_parent(ep_model, item);
  if (parent_item == NULL)
    return FALSE;
  
  iter->user_data = parent_item;
  
  return TRUE;
}

/* drag source */

/* need to be implemented */
static gboolean row_draggable(GtkTreeDragSource * drag_source,
    GtkTreePath * path)
{
  (void) drag_source;
  (void) path;
#if 0
  etpan_gtk_tree_model * ep_model;
  GtkTreeIter * iter;
  void * item;
  
  if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(drag_source),
          &iter, path))
    return;
  
  ep_model = (etpan_gtk_tree_model *) tree_model;
  item = iter->user_data;
  
  ep_model->datasource->item_draggable(struct etpan_gtk_tree_data_source *
      datasource, void * item);
#endif
  return FALSE;
}

static gboolean drag_data_delete(GtkTreeDragSource * drag_source,
    GtkTreePath * path)
{
  (void) drag_source;
  (void) path;
  return FALSE;
}

static gboolean drag_data_get(GtkTreeDragSource * drag_source,
    GtkTreePath * path, GtkSelectionData  * selection_data)
{
  (void) drag_source;
  (void) path;
  (void) selection_data;
  return FALSE;
}

/* drag dest */

static gboolean drag_data_received(GtkTreeDragDest * drag_dest,
    GtkTreePath * dest, GtkSelectionData * selection_data)
{
  (void) drag_dest;
  (void) dest;
  (void) selection_data;
  return FALSE;
}

static gboolean row_drop_possible (GtkTreeDragDest * drag_dest,
    GtkTreePath * dest_path, GtkSelectionData * selection_data)
{
  (void) drag_dest;
  (void) dest_path;
  (void) selection_data;
  return FALSE;
}


void etpan_gtk_tree_model_set_datasource(etpan_gtk_tree_model * tree_model,
    struct etpan_gtk_tree_data_source * datasource)
{
  tree_model->datasource = datasource;
}

void etpan_gtk_tree_model_get_iter_from_item(etpan_gtk_tree_model * ep_model,
    GtkTreeIter * iter, void * item)
{
  (void) ep_model;
  
  iter->user_data = item;
}

void * etpan_gtk_tree_model_get_item(etpan_gtk_tree_model * ep_model,
    GtkTreeIter * iter)
{
  (void) ep_model;
  
  if (iter == NULL)
    return NULL;
  
  return iter->user_data;
}

int etpan_gtk_tree_model_get_item_index(etpan_gtk_tree_model * ep_model,
    void * item)
{
  return get_current_index(ep_model, item);
}

static void get_deletion(etpan_gtk_tree_model * ep_model, void * item,
    chash * deleted)
{
  struct cache_info * info;
  chash * new_children_hash;
  unsigned int count;
  unsigned int real_count;
  unsigned int i;
  int r;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return;
  
  /* deletion */
  
  if (info->children == NULL)
    return;
  
  real_count = real_get_count(ep_model, item);
  new_children_hash = chash_new(real_count, CHASH_COPYKEY);
  for(i = 0 ; i < real_count ; i ++) {
    void * child;
    chashdatum key;
    chashdatum value;
    
    child = real_get_nth_child(ep_model, item, i);
    
    key.data = &child;
    key.len = sizeof(child);
    value.data = NULL;
    value.len = 0;
    
    r = chash_set(new_children_hash, &key, &value, NULL);
    if (r < 0)
      ETPAN_LOG_MEMORY_ERROR;
  }

  count = get_count(ep_model, item);
  for(i = 0 ; i < count ; i ++) {
    void * child;
    chashdatum key;
    chashdatum value;
    
    child = get_nth_child(ep_model, item, i);
    key.data = &child;
    key.len = sizeof(child);
    r = chash_get(new_children_hash, &key, &value);
    if (r < 0) {
      value.data = NULL;
      value.len = 0;
      r = chash_set(deleted, &key, &value, NULL);
      if (r < 0)
        ETPAN_LOG_MEMORY_ERROR;
    }
  }
  
  chash_free(new_children_hash);
}

/* fill iter and get path */
static inline GtkTreePath * get_tree_path(etpan_gtk_tree_model * ep_model,
    GtkTreeIter * iter, void * item)
{
  iter->user_data = item;
  return gtk_tree_model_get_path(GTK_TREE_MODEL(ep_model), iter);
}

static void notify_recursive_deletion(etpan_gtk_tree_model * ep_model,
    void * item)
{
  struct cache_info * info;
  unsigned int count;
  unsigned int i;
  GtkTreeIter iter;
  GtkTreePath * path;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return;
  
  path = get_tree_path(ep_model, &iter, item);
  
  /* delete children */
  if (info->children != NULL) {
    count = get_count(ep_model, item);
    for(i = 0 ; i < count ; i ++) {
      void * child;
      struct cache_info * child_info;
    
      child = get_nth_child(ep_model, item, i);
      child_info = get_cache_info(ep_model, child);
      child_info->index = 0;
      notify_recursive_deletion(ep_model, child);
    }
    
    /* if number of children was not 0 */
    if (count != 0) {
      info->cached_has_child = 1;
      info->has_child = 0;
      carray_set_size(info->children, 0);
      if (item != NULL) {
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
      }
    }
  }
  else {
    if (info->cached_has_child) {
      if (info->has_child) {
        info->has_child = 0;
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
      }
    }
  }
  
#ifdef CACHE_DEBUG
  ETPAN_LOG("row deleted : %s", gtk_tree_path_to_string(path));
#endif
  gtk_tree_model_row_deleted(GTK_TREE_MODEL(ep_model), path);
  remove_cache_info(ep_model, item);
  gtk_tree_path_free(path);
}

static void reload_node_deletion(etpan_gtk_tree_model * ep_model, void * item)
{
  chash * deleted;
  unsigned int i;
  unsigned int count;
  unsigned int previous_count;
  unsigned int new_index;
  struct cache_info * info;
  
  deleted = chash_new(CHASH_DEFAULTSIZE, CHASH_COPYKEY);
  if (deleted == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  get_deletion(ep_model, item, deleted);
  
  info = get_cache_info(ep_model, item);
  
  if (info == NULL) {
    chash_free(deleted);
    return;
  }
  
  if (info->children == NULL) {
    if (info->cached_has_child) {
      int has_child;
      
      has_child = real_get_has_child(ep_model, item);
      if (has_child != info->has_child) {
        GtkTreeIter iter;
        GtkTreePath * path;
        
        path = get_tree_path(ep_model, &iter, item);
        info->has_child = has_child;
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
        gtk_tree_path_free(path);
      }
    }
    chash_free(deleted);
    return;
  }
  
  new_index = 0;
  count = get_count(ep_model, item);
  previous_count = count;
  for(i = 0 ; i < count ; i ++) {
    void * child;
    chashdatum key;
    chashdatum value;
    int r;
    struct cache_info * child_info;
    
    child = get_nth_child(ep_model, item, i);
    child_info = get_cache_info(ep_model, child);
    
    key.data = &child;
    key.len = sizeof(child);
    r = chash_get(deleted, &key, &value);
    if (r == 0) {
      /* deleted */
      child_info->index = new_index;
      notify_recursive_deletion(ep_model, child);
    }
    else {
      child_info->index = new_index;
      carray_set(info->children, new_index, child);
      
      reload_node_deletion(ep_model, child);
      new_index ++;
    }
  }
  carray_set_size(info->children, new_index);
  
  if ((new_index == 0) && (previous_count != 0)) {
    info->cached_has_child = 1;
    info->has_child = 0;
    if (item != NULL) {
      GtkTreeIter iter;
      GtkTreePath * path;
      
      path = get_tree_path(ep_model, &iter, item);
      gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
          path, &iter);
      gtk_tree_path_free(path);
    }
  }
  
  chash_free(deleted);
}

static void get_addition(etpan_gtk_tree_model * ep_model, void * item,
    carray * added)
{
  struct cache_info * info;
  chash * old_children_hash;
  unsigned int count;
  unsigned int real_count;
  unsigned int i;
  int r;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return;
  
  /* addition */
  
  if (info->children == NULL)
    return;
  
  count = get_count(ep_model, item);
  old_children_hash = chash_new(count, CHASH_COPYKEY);
  for(i = 0 ; i < count ; i ++) {
    void * child;
    chashdatum key;
    chashdatum value;
    
    child = get_nth_child(ep_model, item, i);
    
    key.data = &child;
    key.len = sizeof(child);
    value.data = NULL;
    value.len = 0;
    
    r = chash_set(old_children_hash, &key, &value, NULL);
    if (r < 0)
      ETPAN_LOG_MEMORY_ERROR;
  }

  real_count = real_get_count(ep_model, item);
  for(i = 0 ; i < real_count ; i ++) {
    void * child;
    chashdatum key;
    chashdatum value;
    
    child = real_get_nth_child(ep_model, item, i);
    key.data = &child;
    key.len = sizeof(child);
    r = chash_get(old_children_hash, &key, &value);
    if (r < 0) {
      r = carray_add(added, child, NULL);
      if (r < 0)
        ETPAN_LOG_MEMORY_ERROR;
    }
  }
  
  chash_free(old_children_hash);
}

static void reload_node_reorder(etpan_gtk_tree_model * ep_model, void * item);
static void reload_node(etpan_gtk_tree_model * ep_model, void * item);

static void reload_node_addition(etpan_gtk_tree_model * ep_model, void * item)
{
  carray * addition;
  unsigned int i;
  unsigned int count;
  unsigned int new_index;
  unsigned int previous_count;
  struct cache_info * info;
  int r;
  
#if 0
  ETPAN_LOG("reload node : %p", item);
#endif
  info = get_cache_info(ep_model, item);
  if (info->children == NULL) {
#if 0
    ETPAN_LOG("reload node no children : %p", info->item);
#endif
    
    if (info->cached_has_child) {
      int has_child;
      
      has_child = real_get_has_child(ep_model, item);
      if (has_child != info->has_child) {
        GtkTreeIter iter;
        GtkTreePath * path;
        
        path = get_tree_path(ep_model, &iter, item);
        info->has_child = has_child;
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
        gtk_tree_path_free(path);
      }
    }
    else {
      int has_child;
      
      has_child = real_get_has_child(ep_model, item);
      if (has_child) {
        GtkTreeIter iter;
        GtkTreePath * path;
        
        path = get_tree_path(ep_model, &iter, item);
        info->cached_has_child = 1;
        info->has_child = 1;
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
        gtk_tree_path_free(path);
      }
    }
    
    return;
  }
  
  addition = carray_new(16);
  if (addition == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  get_addition(ep_model, item, addition);
  
  count = get_count(ep_model, item);
  new_index = count;
  previous_count = count;
  
  for(i = 0 ; i < count ; i ++) {
    void * child;
    
    child = get_nth_child(ep_model, item, i);
    reload_node(ep_model, child);
  }
  
  for(i = 0 ; i < carray_count(addition) ; i ++) {
    void * child;
    struct cache_info * child_info;
    
    child = carray_get(addition, i);
    r = carray_add(info->children, child, NULL);
    if (r < 0)
      ETPAN_LOG_MEMORY_ERROR;
    child_info = store_cache_info(ep_model, child);
    child_info->parent_item = item;
    child_info->index = new_index;
#ifdef CACHE_DEBUG
    {
      GtkTreeIter iter;
      GtkTreePath * path;
      
      iter.user_data = child;
      path = gtk_tree_model_get_path(GTK_TREE_MODEL(ep_model), &iter);
      ETPAN_LOG("row cache (reload) : %i/%i %p %s", i, carray_count(addition), child, gtk_tree_path_to_string(path));
    }
#endif
    
    {
      GtkTreeIter iter;
      GtkTreePath * path;
      
      path = get_tree_path(ep_model, &iter, child);
      gtk_tree_model_row_inserted(GTK_TREE_MODEL(ep_model),
          path, &iter);
      if (real_get_has_child(ep_model, child) && (child != NULL)) {
        child_info->cached_has_child = 1;
        child_info->has_child = 1;
        gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
            path, &iter);
      }
      gtk_tree_path_free(path);
    }
    
    new_index ++;
  }
  
  if ((previous_count == 0) && (new_index != 0)) {
    info->cached_has_child = 1;
    info->has_child = 1;
    if (item != NULL) {
      GtkTreeIter iter;
      GtkTreePath * path;
      
      path = get_tree_path(ep_model, &iter, item);
      gtk_tree_model_row_has_child_toggled(GTK_TREE_MODEL(ep_model),
          path, &iter);
      gtk_tree_path_free(path);
    }
  }
  
  carray_free(addition);
}

/* does not operate recursively */
static void reload_node_reorder(etpan_gtk_tree_model * ep_model, void * item)
{
  unsigned int i;
  chash * item_index;
  gint * new_order;
  GtkTreeIter iter;
  GtkTreePath * path;
  unsigned int count;
  int reorder;
  struct cache_info * info;
  int r;
  
  info = get_cache_info(ep_model, item);
  if (info == NULL)
    return;
  if (info->children == NULL)
    return;
  
  reorder = 0;
  item_index = chash_new(CHASH_DEFAULTSIZE, CHASH_COPYALL);
  if (item_index == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  count = get_count(ep_model, item);
  new_order = malloc(sizeof(* new_order) * count);
  if (new_order == NULL)
    ETPAN_LOG_MEMORY_ERROR;
  
  for(i = 0 ; i < count ; i ++) {
    void * real_child;
    chashdatum key;
    chashdatum value;
    
    real_child = real_get_nth_child(ep_model, item, i);
    key.data = &real_child;
    key.len = sizeof(real_child);
    value.data = &i;
    value.len = sizeof(i);
    r = chash_set(item_index, &key, &value, NULL);
    if (r < 0)
      ETPAN_LOG_MEMORY_ERROR;
  }
  
  for(i = 0 ; i < count ; i ++) {
    void * child;
    unsigned int new_index;
    chashdatum key;
    chashdatum value;
    struct cache_info * child_info;
    
    child = get_nth_child(ep_model, item, i);
    child_info = get_cache_info(ep_model, child);
    
    key.data = &child;
    key.len = sizeof(child);
    r = chash_get(item_index, &key, &value);
    if (r < 0) {
      ETPAN_LOG("could not reorder tree");
      etpan_crash();
    }
    memcpy(&new_index, value.data, sizeof(new_index));
    new_order[new_index] = i;
    child_info->index = new_index;
    if (i != new_index)
      reorder = 1;
  }

  for(i = 0 ; i < count ; i ++) {
    void * real_child;
    
    real_child = real_get_nth_child(ep_model, item, i);
    carray_set(info->children, i, real_child);
  }
  
  if (reorder) {
    path = get_tree_path(ep_model, &iter, item);
    if (item == NULL) {
      gtk_tree_model_rows_reordered(GTK_TREE_MODEL(ep_model),
          gtk_tree_path_new(), NULL, new_order);
    }
    else {
      gtk_tree_model_rows_reordered(GTK_TREE_MODEL(ep_model),
          path, &iter, new_order);
    }
    gtk_tree_path_free(path);
  }
  
  chash_free(item_index);
  free(new_order);
}

static void reload_node_modification(etpan_gtk_tree_model * ep_model,
    void * item)
{
  GtkTreeIter iter;
  GtkTreePath * path;
  
  path = get_tree_path(ep_model, &iter, item);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(ep_model),
      path, &iter);
  gtk_tree_path_free(path);
}

static void log_tree(etpan_gtk_tree_model * ep_model, void * item, char * space)
{
  char dup_space[50];
  int count;
  struct cache_info * info;
  unsigned int i;
  
  ETPAN_LOG("%s%p", space, item);
  count = get_count(ep_model, item);
  info = get_cache_info(ep_model, item);
  if (info->children == NULL)
    return;
  
  snprintf(dup_space, sizeof(dup_space), "%s ", space);
  for(i = 0 ; i < carray_count(info->children) ; i ++) {
    log_tree(ep_model, carray_get(info->children, i), dup_space);
  }
}

static void reload_node(etpan_gtk_tree_model * ep_model, void * item)
{
  /* addition or modified children */
  
  reload_node_addition(ep_model, item);
  reload_node_reorder(ep_model, item);
  reload_node_modification(ep_model, item);
}

void etpan_gtk_tree_model_reload(etpan_gtk_tree_model * ep_model)
{
  struct cache_info * info;
  
  info = get_cache_info(ep_model, NULL);
  if (info == NULL) {
    /* bootstrap */
    info = store_cache_info(ep_model, NULL);
    info->cached_has_child = 1;
    info->has_child = 0;
    info->children = carray_new(16);
    
#ifdef CACHE_DEBUG
    {
      GtkTreeIter iter;
      GtkTreePath * path;
    
      iter.user_data = NULL;
      path = gtk_tree_model_get_path(GTK_TREE_MODEL(ep_model), &iter);
      ETPAN_LOG("row cache : %p %s", NULL, gtk_tree_path_to_string(path));
    }
#endif
  }
  reload_node_deletion(ep_model, NULL);
  reload_node(ep_model, NULL);
#if 0
  ETPAN_LOG("--- dump tree ---");
  log_tree(ep_model, NULL, "");
  ETPAN_LOG("--- end dump tree ---");
#endif
}
