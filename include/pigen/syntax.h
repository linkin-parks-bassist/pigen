#ifndef PIGEN_SYNTAX_H
#define PIGEN_SYNTAX_H

#include <stddef.h>

#include "pigen/expression.h"
#include "pigen/transfer_type.h"

typedef enum {
	PIGEN_SYNTAX_COMPILATION_UNIT,
	PIGEN_SYNTAX_MODULE,
	PIGEN_SYNTAX_PARAMETER,
	PIGEN_SYNTAX_TYPEDEF,
	PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATION,
	PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATOR,
	PIGEN_SYNTAX_SIGNAL_DECLARATION,
	PIGEN_SYNTAX_SIGNAL_DECLARATOR,
	PIGEN_SYNTAX_CLOCKED_PROCESS,
	PIGEN_SYNTAX_PROCEDURAL_BLOCK,
	PIGEN_SYNTAX_IF_STATEMENT,
	PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT,
	PIGEN_SYNTAX_OPAQUE
} pigen_syntax_kind;

typedef enum {
	PIGEN_DIRECTION_INTERNAL,
	PIGEN_DIRECTION_INPUT,
	PIGEN_DIRECTION_OUTPUT,
	PIGEN_DIRECTION_INOUT
} pigen_syntax_direction;

typedef enum {
	PIGEN_EDGE_POSEDGE,
	PIGEN_EDGE_NEGEDGE
} pigen_syntax_edge;

typedef enum {
	PIGEN_SYNTAX_SIGN_IMPLICIT,
	PIGEN_SYNTAX_SIGN_UNSIGNED,
	PIGEN_SYNTAX_SIGN_SIGNED
} pigen_syntax_signedness;

typedef struct {
	pigen_syntax_location location;
	pigen_syntax_expr_id left;
	pigen_syntax_expr_id right;
} pigen_syntax_dimension;

typedef enum {
	PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT,
	PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE
} pigen_syntax_shape_dimension_form;

typedef struct {
	pigen_syntax_location location;
	pigen_syntax_shape_dimension_form form;
	union {
		pigen_syntax_expr_id count;
		struct {
			pigen_syntax_expr_id left;
			pigen_syntax_expr_id right;
		} range;
	} as;
} pigen_syntax_shape_dimension;

typedef struct {
	pigen_token_id name;
	size_t first_shape_dimension;
	size_t dimension_count;
} pigen_syntax_signal_declarator;

typedef struct {
	pigen_token_id base;
	pigen_syntax_signedness signedness;
	pigen_syntax_location location;
	size_t first_dimension;
	size_t dimension_count;
} pigen_syntax_type;

typedef struct {
	pigen_syntax_kind kind;
	pigen_syntax_location location;
	pigen_syntax_id parent;
	pigen_syntax_id first_child;
	pigen_syntax_id last_child;
	pigen_syntax_id next_sibling;
	union {
		struct { pigen_token_id name; } module;
		struct {
			pigen_token_id name;
			pigen_syntax_expr_id value;
			int is_local;
		} parameter;
		struct {
			pigen_token_id name;
			pigen_syntax_type type;
		} type_definition;
		struct {
			pigen_syntax_direction direction;
			pigen_transfer_type transfer_type;
			pigen_syntax_type type;
		} static_signal_declaration;
		pigen_syntax_signal_declarator static_signal_declarator;
		struct {
			pigen_transfer_type transfer_type;
			pigen_syntax_direction direction;
			pigen_syntax_type payload;
			pigen_syntax_expr_id transfer_argument;
		} signal_declaration;
		pigen_syntax_signal_declarator signal_declarator;
		struct {
			pigen_syntax_edge edge;
			pigen_syntax_expr_id clock;
		} clocked_process;
		struct {
			pigen_syntax_expr_id condition;
			int has_else;
		} if_statement;
		struct {
			pigen_syntax_expr_id destination;
			pigen_syntax_expr_id value;
		} nonblocking_assignment;
	} as;
} pigen_syntax_node;

typedef struct {
	const pigen_expanded_source *expanded;
	pigen_syntax_node *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_syntax_dimension *dimensions;
	size_t dimension_count;
	size_t dimension_capacity;
	pigen_syntax_shape_dimension *shape_dimensions;
	size_t shape_dimension_count;
	size_t shape_dimension_capacity;
	pigen_syntax_expr_arena expressions;
} pigen_syntax_tree;

int pigen_parse_syntax(const pigen_expanded_source *source,
	pigen_syntax_tree *tree, pigen_syntax_error *error);
const pigen_syntax_node *pigen_syntax_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node);
const pigen_syntax_dimension *pigen_syntax_type_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_type *type);
const pigen_syntax_shape_dimension *pigen_syntax_declarator_shape_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_node *declarator);
void pigen_free_syntax_tree(pigen_syntax_tree *tree);

#endif
