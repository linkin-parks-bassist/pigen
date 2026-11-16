#ifndef PIGEN_SYNTAX_H
#define PIGEN_SYNTAX_H

#include <stddef.h>

#include "pigen/expression.h"
#include "pigen/operation.h"
#include "pigen/transfer_type.h"
#include "pigen/protocol.h"

typedef enum {
	PIGEN_SYNTAX_COMPILATION_UNIT,
	PIGEN_SYNTAX_PACKAGE,
	PIGEN_SYNTAX_IMPORT,
	PIGEN_SYNTAX_EXPORT,
	PIGEN_SYNTAX_MODULE,
	PIGEN_SYNTAX_PARAMETER,
	PIGEN_SYNTAX_GENVAR,
	PIGEN_SYNTAX_TYPEDEF,
	PIGEN_SYNTAX_SIGNAL_DECLARATION,
	PIGEN_SYNTAX_SIGNAL_DECLARATOR,
	PIGEN_SYNTAX_CLOCKED_PROCESS,
	PIGEN_SYNTAX_COMBINATIONAL_PROCESS,
	PIGEN_SYNTAX_BLOCKING_ASSIGNMENT,
	PIGEN_SYNTAX_PROCEDURAL_DO_WHILE,
	PIGEN_SYNTAX_PROCEDURAL_FOREVER,
	PIGEN_SYNTAX_PROCEDURAL_BREAK,
	PIGEN_SYNTAX_PROCEDURAL_CONTINUE,
	PIGEN_SYNTAX_PROCEDURAL_FOR,
	PIGEN_SYNTAX_PROCEDURAL_FOREACH,
	PIGEN_SYNTAX_FOREACH_INDEX,
	PIGEN_SYNTAX_PROCEDURAL_WHILE,
	PIGEN_SYNTAX_PROCEDURAL_REPEAT,
	PIGEN_SYNTAX_PROCEDURAL_CASE,
	PIGEN_SYNTAX_PROCEDURAL_CASE_ITEM,
	PIGEN_SYNTAX_PROCEDURAL_CASE_LABEL,
	PIGEN_SYNTAX_PROCEDURAL_BLOCK,
	PIGEN_SYNTAX_IF_STATEMENT,
	PIGEN_SYNTAX_WHEN_STATEMENT,
	PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT,
	PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT,
	PIGEN_SYNTAX_PIPELINE,
	PIGEN_SYNTAX_STAGE,
	PIGEN_SYNTAX_FSM,
	PIGEN_SYNTAX_FSM_STATE,
	PIGEN_SYNTAX_GOTO,
	PIGEN_SYNTAX_INSTANCE,
	PIGEN_SYNTAX_FABRIC,
	PIGEN_SYNTAX_FABRIC_CONNECTION,
	PIGEN_SYNTAX_CONNECTION,
	PIGEN_SYNTAX_PARAMETER_CONNECTION,
	PIGEN_SYNTAX_ACTION,
	PIGEN_SYNTAX_GENERATE_REGION,
	PIGEN_SYNTAX_GENERATE_BLOCK,
	PIGEN_SYNTAX_GENERATE_IF,
	PIGEN_SYNTAX_GENERATE_CASE,
	PIGEN_SYNTAX_GENERATE_CASE_ITEM,
	PIGEN_SYNTAX_GENERATE_CASE_LABEL,
	PIGEN_SYNTAX_GENERATE_FOR,
	PIGEN_SYNTAX_RETAINED,
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
	int has_initializer;
	pigen_syntax_expr_id initializer;
} pigen_syntax_signal_declarator;

typedef struct {
	pigen_transfer_type transfer_type;
	pigen_syntax_location location;
	pigen_syntax_expr_id argument;
} pigen_syntax_transfer_type_occurrence;

typedef enum {
	PIGEN_PROTOCOL_OPTION_ADDR,
	PIGEN_PROTOCOL_OPTION_ID,
	PIGEN_PROTOCOL_OPTION_DEST,
	PIGEN_PROTOCOL_OPTION_USER,
	PIGEN_PROTOCOL_OPTION_AWUSER,
	PIGEN_PROTOCOL_OPTION_WUSER,
	PIGEN_PROTOCOL_OPTION_BUSER,
	PIGEN_PROTOCOL_OPTION_ARUSER,
	PIGEN_PROTOCOL_OPTION_RUSER,
	PIGEN_PROTOCOL_OPTION_KEEP,
	PIGEN_PROTOCOL_OPTION_STRB,
	PIGEN_PROTOCOL_OPTION_LAST,
	PIGEN_PROTOCOL_OPTION_CLOCK,
	PIGEN_PROTOCOL_OPTION_RESET,
	PIGEN_PROTOCOL_OPTION_RESETN,
	PIGEN_PROTOCOL_OPTION_COUNT
} pigen_syntax_protocol_option;

typedef struct {
	pigen_protocol_family family;
	pigen_syntax_location location;
	pigen_syntax_expr_id options[PIGEN_PROTOCOL_OPTION_COUNT];
	unsigned present;
} pigen_syntax_protocol_occurrence;

typedef struct {
	pigen_syntax_kind kind;
	pigen_syntax_location location;
	pigen_syntax_id parent;
	pigen_syntax_id first_child;
	pigen_syntax_id last_child;
	pigen_syntax_id next_sibling;
	union {
		struct { pigen_token_id name; int is_external; } module;
		struct { pigen_token_id name; } generate_block;
		struct {
			pigen_syntax_expr_id condition;
			int has_else;
		} generate_if;
		struct { pigen_syntax_expr_id selector; } generate_case;
		struct { pigen_syntax_expr_id control; } procedural_loop;
		struct { pigen_token_id name; } foreach_index;
		struct { pigen_syntax_expr_id selector; int wildcard; pigen_case_qualifier qualifier; } procedural_case;
		struct { int is_default; } generate_case_item;
		struct { pigen_syntax_expr_id value; } generate_case_label;
		struct {
			pigen_token_id iterator;
			int declares_iterator;
			pigen_syntax_expr_id initial;
			pigen_syntax_expr_id condition;
			/* Assignment expression, or +/-1 for increment/decrement. */
			pigen_syntax_expr_id next_value;
			int increment;
			int compound_step;
			pigen_syntax_operator compound_operator;
		} generate_for;
		struct {
			pigen_token_id name;
			pigen_syntax_expr_id value;
			int is_local;
			int has_type;
			pigen_syntax_type_id data_type;
		} parameter;
		struct { pigen_token_id name; } genvar;
		struct { pigen_token_id package,member; int wildcard,is_structured; } package_import;
		struct {
			int has_name;
			pigen_token_id name;
			size_t first_scope,scope_count,first_name,name_count;
			size_t formal_count;
			int signature_known;
		} retained_subroutine;
		struct {
			pigen_token_id name;
			pigen_syntax_type_id type;
			pigen_syntax_protocol_occurrence protocol;
		} type_definition;
		struct {
			pigen_syntax_direction direction;
			int is_automatic;
			pigen_syntax_type_id data_type;
			int has_transfer_type;
			pigen_syntax_transfer_type_occurrence transfer_type;
			pigen_syntax_protocol_occurrence protocol;
		} signal_declaration;
		pigen_syntax_signal_declarator signal_declarator;
		struct {
			pigen_syntax_edge edge;
			pigen_syntax_expr_id clock;
		} clocked_process;
		struct {
			pigen_syntax_expr_id condition;
			int has_else;
			int else_if; /* Authored else-if, rather than else begin if. */
			pigen_case_qualifier qualifier;
		} if_statement;
		struct {
			int has_name;
			pigen_token_id name;
		} procedural_block;
		struct {
			pigen_syntax_expr_id destination;
			pigen_syntax_expr_id value;
		} when_statement;
		struct {
			pigen_syntax_expr_id destination;
			pigen_syntax_expr_id value;
			/* BLOCKING_ASSIGNMENT only; standalone ++/-- has no RHS. */
			int compound;
			int increment;
			pigen_syntax_operator compound_operator;
		} nonblocking_assignment;
		struct {
			pigen_token_id name;
			pigen_syntax_expr_id yield;
		} pipeline;
		struct {
			pigen_token_id name;
			int has_name;
		} stage;
		struct {
			pigen_token_id name;
			int has_name;
			pigen_syntax_expr_id clock;
		} fsm;
		struct {
			pigen_token_id name;
		} fsm_state;
		struct {
			pigen_token_id target;
		} goto_statement;
		struct {
			pigen_token_id module;
			pigen_token_id name;
			/* Overrides are distinct ordered children, never data ports. */
			int has_parameters;
		} instance;
		struct {
			pigen_token_id port;
			pigen_syntax_expr_id value;
		} connection;
		struct {
			int action;
			pigen_token_id target;
		} action;
		struct {
			pigen_token_id name;
		} fabric;
		struct {
			/* A missing port token denotes a native parent endpoint; the
			 * corresponding instance token then names that endpoint. */
			pigen_token_id left_instance;
			pigen_token_id left_port;
			pigen_token_id right_instance;
			pigen_token_id right_port;
			pigen_syntax_expr_id left_expression;
			pigen_syntax_expr_id right_expression;
			int direct;
			int tier;
			int has_window;
			pigen_syntax_expr_id window_base,window_limit;
		} fabric_connection;
	} as;
} pigen_syntax_node;

/* External generate-scope names, including retained/unselected templates.
 * These identifiers do not participate in authored hierarchical lookup. */
typedef struct {
	pigen_syntax_id module;
	size_t parent; /* Index in generate_names, or SIZE_MAX for module scope. */
	pigen_token_id control;
	pigen_token_id block;
	pigen_token_id explicit_name;
	size_t ordinal;
	char *external_name;
} pigen_syntax_generate_name;

/* Lexical namespace facts for retained SV; these never describe executable IR. */
typedef struct {
	size_t parent; /* Index in retained_scopes, or SIZE_MAX. */
	pigen_syntax_location location;
} pigen_syntax_retained_scope;

typedef enum {
	PIGEN_RETAINED_REFERENCE,
	PIGEN_RETAINED_CALL,
	PIGEN_RETAINED_BARE_CALL,
	PIGEN_RETAINED_DECLARATION,
	PIGEN_RETAINED_IMPORT,
	PIGEN_RETAINED_ARGUMENT
} pigen_syntax_retained_name_kind;

typedef struct {
	pigen_syntax_retained_name_kind kind;
	size_t scope;
	pigen_token_id name,package;
	int wildcard;
	/* Parsed primary rooted at this lexical use; no executable retained IR. */
	pigen_syntax_expr_id primary;
	int written;
	/* Declaration formal: 0 ordinary, 1 readonly, 2 writable; index is 1-based. */
	int formal_mode,formal_explicit;
	size_t formal_index;
	/* ARGUMENT facts: primary is the callee; [first,after) owns the actual. */
	size_t first,after,argument_index;
	pigen_token_id argument_name;
	pigen_syntax_expr_id argument_value;
} pigen_syntax_retained_name;

typedef struct {
	const pigen_expanded_source *expanded;
	pigen_syntax_node *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_syntax_shape_dimension *shape_dimensions;
	size_t shape_dimension_count;
	size_t shape_dimension_capacity;
	pigen_syntax_expr_arena expressions;
	pigen_syntax_type_arena types;
	pigen_syntax_generate_name *generate_names;
	size_t generate_name_count, generate_name_capacity;
	pigen_syntax_retained_scope *retained_scopes;
	size_t retained_scope_count,retained_scope_capacity;
	pigen_syntax_retained_name *retained_names;
	size_t retained_name_count,retained_name_capacity;
} pigen_syntax_tree;

int pigen_parse_syntax(const pigen_expanded_source *source,
	pigen_syntax_tree *tree, pigen_syntax_error *error);
const pigen_syntax_node *pigen_syntax_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node);
const pigen_syntax_shape_dimension *pigen_syntax_declarator_shape_dimensions(
	const pigen_syntax_tree *tree, const pigen_syntax_node *declarator);
void pigen_free_syntax_tree(pigen_syntax_tree *tree);

#endif
