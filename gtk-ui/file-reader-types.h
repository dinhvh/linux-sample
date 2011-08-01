#ifndef FILE_READER_TYPES_H

#define FILE_READER_TYPES_H

#include "carray.h"

struct sample_node {
  char * description;
  carray * children;
};

#endif
