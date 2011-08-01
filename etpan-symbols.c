#include "etpan-symbols.h"

#include <bfd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

struct debug_symbol {
  bfd_vma pc;
  const char *filename;
  const char *functionname;
  unsigned int line;
  int found;
  asymbol ** syms;
  unsigned long start;
  int reloc;
};

static asymbol ** 
slurp_symtab(bfd * abfd, char * filename)
{
  long symcount;
  unsigned int size;
  asymbol ** syms;
  
  if ((bfd_get_file_flags (abfd) & HAS_SYMS) == 0) {
    return NULL;
  }
  
  symcount = bfd_read_minisymbols (abfd, FALSE, (void **) &syms, &size);
  if (symcount == 0)
    symcount = bfd_read_minisymbols (abfd, TRUE /* dynamic */, (void **) &syms, &size);
  
  if (symcount < 0) {
    return NULL;
  }
  
  return syms;
}

static void
find_address_in_section(bfd * abfd, asection *section,
    void * data)
{
  bfd_vma vma;
  bfd_size_type size;
  struct debug_symbol * symbol_value;
  flagword flags;
  bfd_vma pc;
  
  symbol_value = data;
  
  if (symbol_value->found)
    return;
  
  flags = bfd_get_section_flags (abfd, section);
  if ((flags & SEC_ALLOC) == 0)
    return;
  
  if (symbol_value->reloc) {
    pc = (unsigned long) symbol_value->pc -
      (unsigned long) symbol_value->start;
  }
  else {
    pc = symbol_value->pc;
  }
  
  vma = bfd_get_section_vma(abfd, section);
  
  if (pc < vma)
    return;
  
  size = bfd_get_section_size(section);
  if (pc >= vma + size)
    return;
  
  symbol_value->found = bfd_find_nearest_line(abfd, section,
      symbol_value->syms,
      pc - vma,
      &symbol_value->filename,
      &symbol_value->functionname,
      &symbol_value->line);
}

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
static int init_done = 0;

static void bootstrap(void)
{
  pthread_mutex_lock(&init_lock);
  if (!init_done) {
    bfd_init();
    init_done = 1;
  }
  pthread_mutex_unlock(&init_lock);
}

static bfd * get_bfd(char * filename)
{
  bfd * abfd;
  char ** matching;
  
  abfd = bfd_openr(filename, NULL);
  if (abfd == NULL)
    goto err;
  
  
  if (bfd_check_format (abfd, bfd_archive)) {
    goto close_abfd;
  }
    
  if (!bfd_check_format_matches (abfd, bfd_object, &matching)) {
    if (bfd_get_error () == bfd_error_file_ambiguously_recognized) {
      free(matching);
    }
    goto close_abfd;
  }
  
  slurp_symtab(abfd, filename);
  
  return abfd;
  
 close_abfd:
  bfd_close(abfd);
 err:
  return NULL;
}

static int symbol_get(bfd * abfd,
    asymbol ** syms,
    void * start, void * ptr,
    struct etpan_debug_symbol * result)
{
  char addr_hex[100];
  struct debug_symbol data;
  
  if (syms == NULL)
    return 0;
  
  snprintf(addr_hex, sizeof(addr_hex), "%p", ptr);
  data.pc = bfd_scan_vma(addr_hex, NULL, 16);
  data.found = 0;
  data.syms = syms;
  data.start = (unsigned long) start;
  data.reloc = 1;
  
  bfd_map_over_sections(abfd, find_address_in_section, (PTR) &data);

  if (data.found)
    goto found;
  
  data.reloc = 0;
  bfd_map_over_sections(abfd, find_address_in_section, (PTR) &data);
  
  if (data.found)
    goto found;
  
  return 0;
  
 found:
  result->functionname = data.functionname;
  result->filename = data.filename;
  result->line = data.line;
  result->libname = NULL;
  
  return 1;
}

struct symtable_elt {
  char * filename;
  bfd * abfd;
  asymbol ** syms;
  unsigned long start;
  unsigned long end;
};

int etpan_get_symbol(struct etpan_symbol_table * symtable,
    void * ptr, struct etpan_debug_symbol * result)
{
  unsigned int i;
  
  for(i = 0 ; i < carray_count(symtable->list) ; i ++) {
    struct symtable_elt * elt;
    int r;
    
    elt = carray_get(symtable->list, i);
    if (((unsigned long) ptr >= elt->start) &&
        ((unsigned long) ptr < elt->end)) {
      r = symbol_get(elt->abfd,
          elt->syms,
          (void *) elt->start,
          ptr, result);
      if (r) {
        result->libname = bfd_get_filename(elt->abfd);
        return 1;
      }
      return 0;
    }
  }
  
  return 0;
}

struct etpan_symbol_table * etpan_get_symtable(pid_t pid)
{
  char dirname[PATH_MAX];
  FILE * f;
  char buf[PATH_MAX];
  carray * list;
  struct etpan_symbol_table * symtable;
  unsigned int i;
  int r;
  
  bootstrap();
  
  list = carray_new(16);
  if (list == NULL)
    goto err;
  
  snprintf(dirname, sizeof(dirname), "/proc/%i/maps", pid);
  f = fopen(dirname, "r");
  while (fgets(buf, sizeof(buf), f)) {
    char * zone;
    char * zone_end;
    char * attr;
    char * offset;
    char * device;
    char * size;
    char * filename;
    char * p;
    char * next;
    unsigned long zone_value;
    unsigned long zone_end_value;
    unsigned long offset_value;
    unsigned long size_value;
    struct symtable_elt * elt;
    
    p = buf;
    
    zone = buf;
    next = strchr(p, ' ');
    if (next == NULL)
      continue;
    * next = '\0';
    
    p = next + 1;
    attr = p;
    next = strchr(p, ' ');
    if (next == NULL)
      continue;
    * next = '\0';
    
    p = next + 1;
    offset = p;
    next = strchr(p, ' ');
    if (next == NULL)
      continue;
    * next = '\0';
    
    p = next + 1;
    device = p;
    next = strchr(p, ' ');
    if (next == NULL)
      continue;
    * next = '\0';

    p = next + 1;
    size = p;
    next = strchr(p, ' ');
    if (next == NULL)
      continue;
    * next = '\0';
    
    p = next + 1;
    while ((* p == ' ') || (*p == '\t'))
      p ++;
    
    filename = p;
    if (filename[0] != '/')
      continue;
    
    if (attr[2] != 'x')
      continue;
    
    p = strchr(zone, '-');
    if (p == NULL)
      continue;
    * p = '\0';
    p ++;
    zone_end = p;
    
    p = strchr(filename, '\n');
    if (p == NULL)
      continue;
    * p = '\0';
    
    zone_value = strtoul(zone, NULL, 16);
    zone_end_value = strtoul(zone_end, NULL, 16);
    offset_value = strtoul(offset, NULL, 16);
    size_value = strtoul(size, NULL, 16);
    
    elt = malloc(sizeof(* elt));
    if (elt == NULL)
      goto free_list;
    
    elt->filename = strdup(filename);
    elt->abfd = get_bfd(elt->filename);
    if (elt->abfd == NULL) {
      free(elt->filename);
      free(elt);
      continue;
    }
    
    elt->syms = slurp_symtab(elt->abfd, elt->filename);
    if (elt->syms == NULL) {
      bfd_close(elt->abfd);
      free(elt->filename);
      free(elt);
      continue;
    }
    elt->start = zone_value + offset_value;
    elt->end = zone_end_value;
    
    r = carray_add(list, elt, NULL);
    if (r < 0)
      goto free_list;
  }
  
  symtable = malloc(sizeof(* symtable));
  if (symtable == NULL)
    goto free_list;
  
  symtable->list = list;
  
  return symtable;
  
 free_list:
  for(i = 0 ; i < carray_count(list) ; i ++) {
    struct symtable_elt * elt;
    
    elt = carray_get(list, i);
    bfd_close(elt->abfd);
    free(elt->filename);
    free(elt);
  }
  carray_free(list);
 err:
  return NULL;
}

void etpan_symbol_table_free(struct etpan_symbol_table * symtable)
{
  unsigned int i;
  
  for(i = 0 ; i < carray_count(symtable->list) ; i ++) {
    struct symtable_elt * elt;
    
    elt = carray_get(symtable->list, i);
    free(elt->syms);
    bfd_close(elt->abfd);
    free(elt->filename);
    free(elt);
  }
  carray_free(symtable->list);
  
  free(symtable);
}
