#ifndef ETPAN_SYMBOLS_H

#define ETPAN_SYMBOLS_H

#include "etpan-symbols-types.h"

#include <sys/types.h>

struct etpan_symbol_table * etpan_get_symtable(pid_t pid);
void etpan_symbol_table_free(struct etpan_symbol_table * symtable);

int etpan_get_symbol(struct etpan_symbol_table * symtable,
    void * ptr, struct etpan_debug_symbol * result);

#endif
