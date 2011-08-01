#ifndef ETPAN_SYMBOL_TYPES_H

#define ETPAN_SYMBOL_TYPES_H

#include "chash.h"
#include "carray.h"

struct etpan_debug_symbol {
  const char * libname;
  const char * functionname;
  const char * filename;
  unsigned int line;
};

struct etpan_symbol_table {
  carray * list;
};

#endif
