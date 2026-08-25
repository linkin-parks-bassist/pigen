#ifndef PIGEN_EXPRESSION_ANALYSIS_H
#define PIGEN_EXPRESSION_ANALYSIS_H

#include "pigen/semantic_error.h"
#include "pigen/semantic.h"
#include "pigen/resolve_policy.h"
#include "pigen/syntax.h"

typedef enum {
	PIGEN_ANALYZED_LITERAL_INVALID,
	PIGEN_ANALYZED_LITERAL_INTEGER,
	PIGEN_ANALYZED_LITERAL_EXACT_INTEGER,
	PIGEN_ANALYZED_LITERAL_BITS
} pigen_analyzed_literal_kind;

typedef struct {
	pigen_syntax_expr_id syntax;
	pigen_syntax_expr_kind kind;
	pigen_analyzed_literal_kind literal_kind;
	pigen_source_span span;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_const_expr_id constant;
	union {
		uint64_t integer;
		pigen_integer_id exact_integer;
		struct { size_t first_state; size_t state_count; } bits;
		pigen_symbol_id symbol;
		struct { pigen_analyzed_expr_id operand; } group;
		struct {
			pigen_unary_resolution resolution;
			pigen_analyzed_expr_id operand;
		} unary;
		struct {
			pigen_binary_resolution resolution;
			pigen_analyzed_expr_id left;
			pigen_analyzed_expr_id right;
		} binary;
		struct {
			pigen_conditional_resolution resolution;
			pigen_analyzed_expr_id condition;
			pigen_analyzed_expr_id when_true;
			pigen_analyzed_expr_id when_false;
		} conditional;
		struct {
			pigen_conversion conversion;
			pigen_analyzed_expr_id operand;
		} conversion;
		struct {
			pigen_analyzed_expr_id base;
			pigen_analyzed_expr_id index;
		} index;
		struct {
			pigen_analyzed_expr_id base;
			pigen_analyzed_expr_id left;
			pigen_analyzed_expr_id right;
			pigen_select_kind kind;
		} select;
		struct { size_t first_child; size_t child_count; } sequence;
	} as;
} pigen_analyzed_expr;

typedef struct {
	pigen_analyzed_expr *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_analyzed_expr_id *children;
	size_t child_count;
	size_t child_capacity;
	pigen_bit_state *literal_states;
	size_t literal_state_count;
	size_t literal_state_capacity;
	pigen_width_constraint *constraints;
	size_t constraint_count;
	size_t constraint_capacity;
} pigen_analyzed_expr_arena;

int pigen_analyze_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, int constant_only,
	const pigen_resolve_policy *policy,
	pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id *result,
	pigen_semantic_error *error);
const pigen_analyzed_expr *pigen_analyzed_expr_get(
	const pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id expression);
void pigen_free_analyzed_expr_arena(pigen_analyzed_expr_arena *arena);

#endif
