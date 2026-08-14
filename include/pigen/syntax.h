#ifndef PIGEN_SYNTAX_H
#define PIGEN_SYNTAX_H

#include <stddef.h>

#include "pigen/source.h"

typedef enum {
	PIGEN_SYNTAX_COMPILATION_UNIT,
	PIGEN_SYNTAX_MODULE,
	PIGEN_SYNTAX_TRANSPORT,
	PIGEN_SYNTAX_OPAQUE
} pigen_syntax_kind;

typedef enum {
	PIGEN_TRANSPORT_BUF,
	PIGEN_TRANSPORT_PORT,
	PIGEN_TRANSPORT_SKID,
	PIGEN_TRANSPORT_FIFO
} pigen_syntax_transport_kind;

typedef enum {
	PIGEN_DIRECTION_INTERNAL,
	PIGEN_DIRECTION_INPUT,
	PIGEN_DIRECTION_OUTPUT,
	PIGEN_DIRECTION_INOUT
} pigen_syntax_direction;

typedef enum {
	PIGEN_SYNTAX_SIGN_IMPLICIT,
	PIGEN_SYNTAX_SIGN_UNSIGNED,
	PIGEN_SYNTAX_SIGN_SIGNED
} pigen_syntax_signedness;

typedef enum {
	PIGEN_SYNTAX_TYPE_IMPLICIT_LOGIC,
	PIGEN_SYNTAX_TYPE_LOGIC,
	PIGEN_SYNTAX_TYPE_BIT,
	PIGEN_SYNTAX_TYPE_NAMED
} pigen_syntax_type_base;

typedef struct {
	pigen_source_span span;
	pigen_source_span left;
	pigen_source_span right;
} pigen_syntax_dimension;

typedef struct {
	pigen_syntax_type_base base;
	pigen_syntax_signedness signedness;
	pigen_source_span span;
	pigen_source_span base_name;
	size_t first_dimension;
	size_t dimension_count;
} pigen_syntax_type;

typedef struct {
	pigen_syntax_kind kind;
	pigen_source_span span;
	pigen_syntax_id parent;
	pigen_syntax_id first_child;
	pigen_syntax_id last_child;
	pigen_syntax_id next_sibling;
	union {
		struct { pigen_source_span name; } module;
		struct {
			pigen_syntax_transport_kind kind;
			pigen_syntax_direction direction;
			pigen_source_span name;
			pigen_syntax_type payload;
			pigen_source_span fifo_depth;
		} transport;
	} as;
} pigen_syntax_node;

typedef struct {
	pigen_source_id source;
	pigen_syntax_node *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_syntax_dimension *dimensions;
	size_t dimension_count;
	size_t dimension_capacity;
} pigen_syntax_tree;

typedef struct {
	pigen_source_span span;
	const char *message;
} pigen_syntax_error;

int pigen_parse_syntax(const pigen_source_manager *sources,
	pigen_source_id source, pigen_syntax_tree *tree, pigen_syntax_error *error);
const pigen_syntax_node *pigen_syntax_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node);
const pigen_syntax_dimension *pigen_syntax_type_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_type *type);
void pigen_free_syntax_tree(pigen_syntax_tree *tree);

#endif
