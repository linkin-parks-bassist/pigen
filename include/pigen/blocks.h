#ifndef PIGEN_BLOCKS_H
#define PIGEN_BLOCKS_H

#include "pigen/model.h"

/* Remove Pigen-owned top-level blocks from SOURCE (preserving line layout)
 * and append their SystemVerilog lowering to GENERATED. */
char *pigen_lower_blocks(const char *source, size_t length, pigen_string *generated);

#endif
