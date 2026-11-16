#ifndef PIGEN_EXPRESSION_ANALYSIS_H
#define PIGEN_EXPRESSION_ANALYSIS_H

#include "pigen/semantic_error.h"
#include "pigen/semantic.h"
#include "pigen/resolve_policy.h"
#include "pigen/syntax.h"

/* Source-position-aware callable namespace inspection; never activates imports. */
pigen_symbol_id pigen_lookup_callee_symbol(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression);

typedef enum {
	PIGEN_ANALYZED_LITERAL_INVALID,
	PIGEN_ANALYZED_LITERAL_INTEGER,
	PIGEN_ANALYZED_LITERAL_EXACT_INTEGER,
	PIGEN_ANALYZED_LITERAL_BITS
} pigen_analyzed_literal_kind;

typedef enum {
	PIGEN_LITERAL_DOMAIN_INVALID,
	PIGEN_LITERAL_DOMAIN_PIGEN,
	PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG,
	PIGEN_LITERAL_DOMAIN_CASE
} pigen_literal_domain;

/* Shared syntax discovery for contextual unbased literals; publishes nothing. */
int pigen_expression_contains_unbased_fill(const pigen_syntax_tree *syntax,pigen_syntax_expr_id expression);

typedef enum {
	PIGEN_ANALYZED_INTRINSIC,
	PIGEN_ANALYZED_EVALUATED_VALUE,
	/* A locally self-determined SV value, stored through as.group. */
	PIGEN_ANALYZED_CASE_VALUE
} pigen_analyzed_expr_form;

typedef struct {
	pigen_syntax_expr_id syntax;
	pigen_syntax_expr_kind kind;
	pigen_analyzed_literal_kind literal_kind;
	int unbased_fill; /* 1 + bit state for a context-sized unbased literal. */
	pigen_source_span span;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_const_expr_id constant;
	/* Non-intrinsic forms select their own union member, independently of kind. */
	pigen_analyzed_expr_form form;
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
			int is_member;
			pigen_source_span member_name;
		} select;
		struct { size_t first_child; size_t child_count; } sequence;
		struct { pigen_analyzed_expr_id value, evaluated; int before; } evaluation;
  struct {
   pigen_semantic_expr_kind kind;
   pigen_analyzed_expr_id first,second;
   pigen_signal_id signal;
  } call;
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

pigen_literal_domain pigen_case_cast_literal_domain(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,const pigen_syntax_expr *expression);
int pigen_binary_operator_from_syntax(pigen_syntax_operator syntax,
	pigen_binary_operator *semantic);
pigen_protocol_array_id pigen_resolve_protocol_array_reference(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, pigen_instance_id *instance);
/* An owned selection table over existing native endpoint identities. Selectors
 * are elaboration constants; indices remove dimensions and one slice may retain
 * a selected dimension. Return 1 for a view, 0 for a non-array expression, -1
 * for an invalid array selection. Free every successful view after use. */
typedef struct {
	pigen_protocol_array_id array;
	pigen_protocol_array_dimension *dimensions;
	size_t dimension_count;
	pigen_protocol_endpoint_id *elements;
	size_t element_count;
} pigen_protocol_array_view;
int pigen_resolve_protocol_array_view(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_protocol_array_view *view);
/* Assignment-like child bindings additionally accept unpacked concatenations and
 * positional assignment patterns (including contextual nested rows).
 * Items must have self-determined native endpoint/collection types; rank is the
 * target rank. Result lanes retain identity and require normal contract checks. */
int pigen_resolve_protocol_array_binding(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	size_t rank,const pigen_resolve_policy *policy,pigen_protocol_array_view *view);
/* Complete target bounds additionally enable constant index/default patterns. */
int pigen_resolve_protocol_array_binding_to_shape(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_protocol_array_dimension *dimensions,size_t rank,
	const pigen_resolve_policy *policy,pigen_protocol_array_view *view);
/* A declared target additionally provides the native contract/payload type for
 * type-key assignment patterns. Returned lanes retain their original identity. */
int pigen_resolve_protocol_array_binding_to_array(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_expr_id expression,
 pigen_protocol_array_id target,const pigen_resolve_policy *policy,pigen_protocol_array_view *view);
void pigen_free_protocol_array_view(pigen_protocol_array_view *view);
/* Structural coordinate evaluation uses the caller's generation policy. */
pigen_protocol_endpoint_id pigen_resolve_protocol_reference(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, const pigen_resolve_policy *policy);
int pigen_analyze_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, int constant_only,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy,
	pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id *result,
	pigen_semantic_error *error);
const pigen_analyzed_expr *pigen_analyzed_expr_get(
	const pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id expression);
void pigen_free_analyzed_expr_arena(pigen_analyzed_expr_arena *arena);

#endif
