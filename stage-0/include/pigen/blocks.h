#ifndef PIGEN_BLOCKS_H
#define PIGEN_BLOCKS_H

#include "pigen/model.h"

typedef struct {
	char *name;
	char *svg;
} pigen_fabric_diagram;

typedef struct {
	pigen_fabric_diagram *items;
	size_t count;
} pigen_fabric_diagrams;

/* Remove Pigen-owned top-level blocks from SOURCE (preserving line layout)
 * and append their SystemVerilog lowering to GENERATED. Fabric diagrams are
 * returned in source order when DIAGRAMS is non-NULL. */
char *pigen_lower_blocks(const char *source, size_t length, pigen_string *generated,
	pigen_fabric_diagrams *diagrams);
void pigen_free_fabric_diagrams(pigen_fabric_diagrams *diagrams);

#endif
