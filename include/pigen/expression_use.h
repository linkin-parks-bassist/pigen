#ifndef PIGEN_EXPRESSION_USE_H
#define PIGEN_EXPRESSION_USE_H

#include "pigen/predicate.h"

typedef enum {
	PIGEN_EXPRESSION_USE_READ = 1u << 0,
	PIGEN_EXPRESSION_USE_LVALUE = 1u << 1,
	PIGEN_EXPRESSION_USE_INDEX = 1u << 2,
	PIGEN_EXPRESSION_USE_TYPE = 1u << 3
} pigen_expression_use_context;

typedef struct {
	pigen_expr_id expression;
	pigen_symbol_id symbol;
	pigen_transport_id transport;
	pigen_predicate_id predicate;
	pigen_expression_use_context context;
} pigen_expression_use;

typedef struct {
	pigen_transport_id transport;
	unsigned contexts;
} pigen_expression_transport_use;

typedef struct {
	pigen_expression_use *uses;
	size_t use_count;
	size_t use_capacity;
	pigen_expression_transport_use *transports;
	size_t transport_count;
	size_t transport_capacity;
} pigen_expression_use_analysis;

/* Root traversal is currently read-only.  Packed subscripts encountered within
 * reads or lvalues use index context; constant range/width expressions use type
 * context. */
int pigen_analyze_expression_uses(pigen_semantic_model *model,
	pigen_expr_id expression, pigen_predicate_id predicate,
	pigen_expression_use_context context,
	pigen_expression_use_analysis *analysis);
int pigen_analyze_lvalue_uses(pigen_semantic_model *model,
	pigen_lvalue_id lvalue, pigen_predicate_id predicate,
	pigen_expression_use_analysis *analysis);
void pigen_free_expression_use_analysis(
	pigen_expression_use_analysis *analysis);

#endif
