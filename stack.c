#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <limits.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <bfd.h>
#include <libgen.h>

#include "etpan-symbols.h"
#include "chash.h"
#include "carray.h"

static int attach(pid_t pid)
{
  int status;
  long r;
  
  r = ptrace(PTRACE_ATTACH, pid, 0, 0);
  if (r < 0) {
    fprintf(stderr, "could not attach\n");
    return r;
  }
  
  r = waitpid(pid, &status, WUNTRACED);
  if (r < 0) {
    fprintf(stderr, "could not be stopped\n");
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return r;
  }
  
  return 0;
}

static int attach_thread(pid_t pid)
{
  int status;
  long r;
  
  r = ptrace(PTRACE_ATTACH, pid, 0, 0);
  if (r < 0) {
    fprintf(stderr, "could not attach\n");
    return r;
  }
  
  r = waitpid(pid, &status, __WCLONE);
  if (r < 0) {
    fprintf(stderr, "could not be stopped\n");
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return r;
  }
  
  return 0;
}

static void detach(pid_t pid)
{
  ptrace(PTRACE_DETACH, pid, 0, 0);
}

static int get_thread_list(pid_t pid,
    pid_t ** p_thread_list, unsigned int * p_thread_count)
{
  char dirname[PATH_MAX];
  DIR * dir;
  struct dirent * ent;
  unsigned int count;
  pid_t * thread_list;
  unsigned int thread_count;
  
  snprintf(dirname, sizeof(dirname), "/proc/%i/task", pid);
  
  dir = opendir(dirname);
  if (dir == NULL)
    return -1;
  
  count = 0;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] != '.')
      count ++;
  }
  
  thread_count = count;
  thread_list = malloc(thread_count * sizeof(* thread_list));
  
  rewinddir(dir);
  count = 0;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] != '.') {
      thread_list[count] = strtoul(ent->d_name, NULL, 10);
      count ++;
    }
  }
  
  closedir(dir);
  
  * p_thread_list = thread_list;
  * p_thread_count = thread_count;

  return 0;
}

#define MAX_FRAME 512

static int get_stack(pid_t pid,
    unsigned long ** p_stackframe, unsigned int * p_stackframe_count)
{
  unsigned long pc;
  unsigned long fp;
  unsigned long stackframe[MAX_FRAME];
  unsigned int stackframe_count;
  unsigned long * result;
  
#ifdef __x86_64
  pc = ptrace(PTRACE_PEEKUSER, pid, RIP, 0);
#else
  pc = ptrace(PTRACE_PEEKUSER, pid, EIP * 4, 0);
#endif
  if (errno != 0) {
    fprintf(stderr, "error peek eip\n");
    return -1;
  }
  
#ifdef __x86_64
  fp = ptrace(PTRACE_PEEKUSER, pid, RBP, 0);
#else
  fp = ptrace(PTRACE_PEEKUSER, pid, EBP * 4, 0);
#endif
  if (errno != 0) {
    fprintf(stderr, "error peek ebp\n");
    return -1;
  }
  
  stackframe_count = 0;
  while (1) {
    unsigned long nextfp;
    
    stackframe[stackframe_count] = pc;
    stackframe_count ++;
    nextfp = ptrace(PTRACE_PEEKDATA, pid, fp, 0);
    if (errno != 0)
      break;
    
#ifdef __x86_64
    pc = ptrace(PTRACE_PEEKDATA, pid, fp + 8, 0);
#else
    pc = ptrace(PTRACE_PEEKDATA, pid, fp + 4, 0);
#endif
    
    fp = nextfp;
    if (fp == 0)
      break;
  }
  
  result = malloc(stackframe_count * sizeof(* result));
  
  memcpy(result, stackframe, stackframe_count * sizeof(* result));
  
  * p_stackframe = result;
  * p_stackframe_count = stackframe_count;
  
  return 0;
}

struct stackframe_elt {
  unsigned long * stackframe;
  unsigned int stackframe_count;
  unsigned int sample_count;
};

static void sample(pid_t pid, chash * thread_hash)
{
  pid_t * tab;
  unsigned int count;
  unsigned int i;
  int r;
  
  r = attach(pid);
  if (r < 0)
    exit(EXIT_FAILURE);
    
  r = get_thread_list(pid, &tab, &count);
  if (r < 0)
    exit(EXIT_FAILURE);
    
  for(i = 0 ; i < count ; i ++) {
    if (tab[i] != pid)
      attach_thread(tab[i]);
  }
    
  for(i = 0 ; i < count ; i ++) {
    unsigned long * stackframe;
    unsigned int stackframe_count;
    chashdatum key;
    chashdatum value;
    struct stackframe_elt * elt;
    chash * stack_hash;
    unsigned int k;
      
    r = get_stack(tab[i], &stackframe, &stackframe_count);
    if (r < 0)
      exit(EXIT_FAILURE);
      
    key.data = &tab[i];
    key.len = sizeof(tab[i]);
    r = chash_get(thread_hash, &key, &value);
    if (r < 0) {
      stack_hash = chash_new(CHASH_DEFAULTSIZE, CHASH_COPYKEY);
      value.data = stack_hash;
      value.len = 0;
      chash_set(thread_hash, &key, &value, NULL);
    }
    else {
      stack_hash = value.data;
    }
      
    for(k = 1 ; k <= stackframe_count ; k ++) {
      key.data = stackframe + (stackframe_count - k);
      key.len = k * sizeof(* stackframe);
      r = chash_get(stack_hash, &key, &value);
      if (r < 0) {
        elt = malloc(sizeof(* elt));
        elt->stackframe = malloc(sizeof(* elt->stackframe) * k);
        memcpy(elt->stackframe, stackframe + (stackframe_count - k),
            k * sizeof(* stackframe));
        elt->stackframe_count = k;
        elt->sample_count = 1;
        value.data = elt;
        value.len = 0;
        chash_set(stack_hash, &key, &value, NULL);
      }
      else {
        elt = value.data;
        elt->sample_count ++;
      }
    }
  }
    
  for(i = 0 ; i < count ; i ++) {
    if (tab[i] != pid)
      detach(tab[i]);
  }
  detach(pid);
  free(tab);
}

static int compare_stack(const void * a, const void * b)
{
  struct stackframe_elt * const  * p_elt_a;
  struct stackframe_elt * const * p_elt_b;
  struct stackframe_elt * elt_a;
  struct stackframe_elt * elt_b;
  unsigned int min_stack;
  unsigned int frame_index;
  unsigned int offset_a;
  unsigned int offset_b;
  
  p_elt_a = a;
  p_elt_b = b;
  elt_a = * p_elt_a;
  elt_b = * p_elt_b;
  min_stack = elt_a->stackframe_count;
  if (elt_b->stackframe_count < min_stack)
    min_stack = elt_b->stackframe_count;
  offset_a = elt_a->stackframe_count - min_stack;
  offset_b = elt_b->stackframe_count - min_stack;
  
  frame_index = min_stack - 1;
  while (1) {
    if (elt_a->stackframe[offset_a + frame_index] ==
        elt_b->stackframe[offset_b + frame_index]) {
      /* do nothing */
    }
    else if (elt_a->stackframe[offset_a + frame_index] <
        elt_b->stackframe[offset_b + frame_index]) {
      return -1;
    }
    else {
      return 1;
    }
    if (frame_index == 0)
      break;
    
    frame_index --;
  }
  
  if (elt_a->stackframe_count < elt_b->stackframe_count)
    return -1;
  else if (elt_a->stackframe_count > elt_b->stackframe_count)
    return 1;
  
  return 0;
}

static unsigned int common_count(struct stackframe_elt * elt_a,
    struct stackframe_elt * elt_b)
{
  unsigned int min_stack;
  unsigned int frame_index;
  unsigned int offset_a;
  unsigned int offset_b;
  
  min_stack = elt_a->stackframe_count;
  if (elt_b->stackframe_count < min_stack)
    min_stack = elt_b->stackframe_count;
  offset_a = elt_a->stackframe_count - min_stack;
  offset_b = elt_b->stackframe_count - min_stack;
  
  frame_index = min_stack - 1;
  while (1) {
    if (elt_a->stackframe[offset_a + frame_index] ==
        elt_b->stackframe[offset_b + frame_index]) {
      /* do nothing */
    }
    else {
        return min_stack - 1 - frame_index;
    }
    if (frame_index == 0)
      break;
    
    frame_index --;
  }
  
  return min_stack;
}

static void print_stackframe(struct stackframe_elt * elt)
{
  unsigned int frame_index;
  
  printf("%u: ", elt->sample_count);
  frame_index = elt->stackframe_count - 1;
  while (1) {
    printf("%p ", (void *) elt->stackframe[frame_index]);
    if (frame_index == 0)
      break;
        
    frame_index --;
  }
  printf("\n");
}

struct stackframe_node {
  struct stackframe_elt * elt;
  carray * children;
};

static int compare_sample(const void * a, const void * b)
{
  struct stackframe_node * const  * p_elt_a;
  struct stackframe_node * const * p_elt_b;
  struct stackframe_node * elt_a;
  struct stackframe_node * elt_b;
  
  p_elt_a = a;
  p_elt_b = b;
  elt_a = * p_elt_a;
  elt_b = * p_elt_b;
  
  return elt_b->elt->sample_count - elt_a->elt->sample_count;
}

static void sort_tree(struct stackframe_node * node)
{
  void ** data;
  unsigned int i;
  
  data = carray_data(node->children);
  qsort(data, carray_count(node->children), sizeof(* data), compare_sample);
  for(i = 0 ; i < carray_count(node->children) ; i ++) {
    struct stackframe_node * child;
    
    child = carray_get(node->children, i);
    sort_tree(child);
  }
}

static const char * my_basename(const char * basename)
{
  const char * result;
  const char * p;
  
  result = basename;
  p = result;
  
  while ((p = strchr(result, '/')) != NULL) {
    result = p + 1;
  }
  
  return result;
}

static void print_tree(struct etpan_symbol_table * symtable,
    struct stackframe_node * node, unsigned int level)
{
  unsigned int i;
  int r;
  
  if (level > 0) {
    struct etpan_debug_symbol symbol;
    
    for(i = 0 ; i < level ; i ++)
      printf(" ");
    
    r = etpan_get_symbol(symtable,
        (void *) node->elt->stackframe[0], &symbol);
    if (r) {
      const char *name;
      char address_str[32];
      
      name = symbol.functionname;
      if (name == NULL || *name == '\0') {
        snprintf(address_str, sizeof(address_str), "%p",
            (void *) node->elt->stackframe[0]);
        name = address_str;
      }
      
      if (symbol.filename != NULL) {
        printf("%u %s (in %s) %s:%u\n", node->elt->sample_count,
            name, my_basename(symbol.libname),
            my_basename(symbol.filename), symbol.line);
      }
      else {
        printf("%u %s (in %s)\n", node->elt->sample_count,
            name, my_basename(symbol.libname));
      }
    }
    else {
      printf("%u %p\n", node->elt->sample_count,
          (void *) node->elt->stackframe[0]);
    }
  }
  
  for(i = 0 ; i < carray_count(node->children) ; i ++) {
    struct stackframe_node * child;
    
    child = carray_get(node->children, i);
    print_tree(symtable, child, level + 1);
  }
}

static void show_tree(struct etpan_symbol_table * symtable,
    struct stackframe_elt ** stack_table, unsigned int count)
{
  unsigned int i;
  struct stackframe_elt * elt;
  unsigned int previous_count;
  struct stackframe_elt * previous;
  unsigned int indent;
  carray * current_node;
  struct stackframe_node * root;
  
  qsort(stack_table, count, sizeof(* stack_table), compare_stack);
  
  current_node = carray_new(16);
  
  root = malloc(sizeof(* root));
  root->elt = NULL;
  root->children = carray_new(4);
  carray_add(current_node, root, NULL);
  
  indent = 0;
  previous = NULL;
  previous_count = 0;
  for(i = 0 ; i < count ; i ++) {
    unsigned int common;
    struct stackframe_node * node;
    struct stackframe_node * parent;
    
    elt = stack_table[i];
    
    common = 0;
    if (previous_count > 0) {
      unsigned int remove_count;
      
      common = common_count(previous, elt);
      remove_count = (previous->stackframe_count - common);
      indent -= remove_count;
      carray_set_size(current_node, carray_count(current_node) - remove_count);
    }
    
    parent = carray_get(current_node, carray_count(current_node) - 1);
    node = malloc(sizeof(* root));
    node->elt = elt;
    node->children = carray_new(4);
    carray_add(parent->children, node, NULL);
    carray_add(current_node, node, NULL);
    indent ++;
    
    previous = elt;
    previous_count = elt->stackframe_count;
  }
  
  sort_tree(root);
  print_tree(symtable, root, 0);
}

int main(int argc, char ** argv)
{
  pid_t pid;
  unsigned int k;
  chash * thread_hash;
  unsigned int sample_count;
  unsigned int sample_delay;
  chashiter * iter;
  struct etpan_symbol_table * symtable;
  
  if (argc < 3) {
    fprintf(stderr, "syntax: sample <pid> <delay>\n");
    exit(EXIT_FAILURE);
  }
  
  pid = strtoul(argv[1], NULL, 10);
  
  thread_hash = chash_new(CHASH_DEFAULTSIZE, CHASH_COPYKEY);
  
  sample_delay = 10 * 1000;
  sample_count = strtoul(argv[2], NULL, 10) * (1000000 / sample_delay);
  printf("sampling %u %u\n", sample_delay, sample_count);
  for(k = 0 ; k < sample_count ; k ++) {
    sample(pid, thread_hash);
    usleep(sample_delay);
  }
  
  symtable = etpan_get_symtable(pid);
  
  for(iter = chash_begin(thread_hash) ; iter != NULL ;
      iter = chash_next(thread_hash, iter)) {
    chashdatum key;
    chashdatum value;
    chashiter * stack_iter;
    chash * stack_hash;
    pid_t pid;
    struct stackframe_elt ** stack_table;
    unsigned int count;
    
    chash_key(iter, &key);
    chash_value(iter, &value);
    memcpy(&pid, key.data, sizeof(pid));
    printf("thread %u:\n", pid);
    
    stack_hash = value.data;
    count = chash_count(stack_hash);
    stack_table = malloc(count * sizeof(* stack_table));
    count = 0;
    for(stack_iter = chash_begin(stack_hash) ; stack_iter != NULL ;
        stack_iter = chash_next(stack_hash, stack_iter)) {
      struct stackframe_elt * elt;
      
      chash_value(stack_iter, &value);
      
      elt = value.data;
      stack_table[count] = elt;
      
      count ++;
    }
    show_tree(symtable, stack_table, count);
    free(stack_table);
  }
  
  etpan_symbol_table_free(symtable);
  
  chash_free(thread_hash);

  exit(EXIT_SUCCESS);
}
