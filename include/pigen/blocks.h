#ifndef PIGEN_BLOCKS_H
#define PIGEN_BLOCKS_H
#include <stddef.h>
#include <stdint.h>
#include "pigen/model.h"

typedef struct { char *name; char *svg; } pigen_fabric_diagram;
typedef struct { pigen_fabric_diagram *items; size_t count; } pigen_fabric_diagrams;
typedef struct { const char *instance, *port; int source; } pigen_fabric_port;
typedef struct { size_t source, destination; unsigned tier; int direct; } pigen_fabric_edge;
typedef struct { int kind; size_t target, port; } pigen_fabric_attachment;
typedef struct { pigen_fabric_attachment ports[3]; } pigen_fabric_tree_router;
typedef struct {
 pigen_fabric_tree_router *routers; size_t router_count, port_count, path_width;
 int *port_router, *port_number;
 uint64_t *paths; size_t *hops;
 char *svg;
} pigen_fabric_tree;
/* Inputs are resolved physical ports, never source handles or route selectors.
 * The same checked topology supplies router links, route words and the SVG. */
int pigen_build_fabric_tree(const char *name, const pigen_fabric_port *ports,
 size_t port_count, const pigen_fabric_edge *edges, size_t edge_count,
 pigen_fabric_tree *tree);
void pigen_free_fabric_tree(pigen_fabric_tree *tree);
void pigen_free_fabric_diagrams(pigen_fabric_diagrams *diagrams);
#endif
