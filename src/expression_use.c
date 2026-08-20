/* Identity-based symbol and transport use analysis over semantic expressions. */
#include <stdlib.h>

#include "pigen/expression_use.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	pigen_semantic_model *model;
	pigen_expression_use_analysis *analysis;
} use_analyzer;

static int add_transport_summary(use_analyzer *analyzer,
	pigen_transport_id transport, pigen_expression_use_context context)
{
	pigen_expression_use_analysis *analysis = analyzer->analysis;
	size_t i;
	for (i = 0; i < analysis->transport_count; i++)
		if (analysis->transports[i].transport.index == transport.index)
		{
			analysis->transports[i].contexts |= (unsigned)context;
			return 1;
		}
	if (analysis->transport_count == analysis->transport_capacity)
	{
		analysis->transport_capacity = analysis->transport_capacity ?
			analysis->transport_capacity * 2 : 8;
		analysis->transports = pigen_resize(analysis->transports,
			analysis->transport_capacity * sizeof(*analysis->transports));
	}
	analysis->transports[analysis->transport_count++] =
		(pigen_expression_transport_use){transport, (unsigned)context};
	return 1;
}

static int add_resolved_use(use_analyzer *analyzer, pigen_expr_id projection,
	pigen_symbol_id symbol_id, pigen_transport_id transport,
	pigen_predicate_id predicate, pigen_expression_use_context context)
{
	const pigen_symbol *symbol = pigen_symbol_get(analyzer->model, symbol_id);
	pigen_expression_use_analysis *analysis = analyzer->analysis;

	if (!symbol) return 0;
	if (transport.index != PIGEN_INVALID_ID)
	{
		if (pigen_symbol_transport(analyzer->model, symbol_id).index !=
			transport.index ||
			!add_transport_summary(analyzer, transport, context))
			return 0;
	}
	else if (symbol->kind == PIGEN_SYMBOL_TRANSPORT)
		return 0;
	if (analysis->use_count == analysis->use_capacity)
	{
		analysis->use_capacity = analysis->use_capacity ?
			analysis->use_capacity * 2 : 16;
		analysis->uses = pigen_resize(analysis->uses,
			analysis->use_capacity * sizeof(*analysis->uses));
	}
	analysis->uses[analysis->use_count++] = (pigen_expression_use){
		projection, symbol_id, transport, predicate, context};
	return 1;
}

static int add_symbol_use(use_analyzer *analyzer, pigen_expr_id expression,
	pigen_expr_id projection, pigen_predicate_id predicate,
	pigen_expression_use_context context)
{
	const pigen_semantic_expr *known = pigen_expr_get(analyzer->model,
		expression);
	const pigen_symbol *symbol;
	pigen_transport_id transport = INVALID_ID(pigen_transport_id);

	if (!known || known->kind != PIGEN_EXPR_SYMBOL) return 0;
	symbol = pigen_symbol_get(analyzer->model, known->as.symbol);
	if (!symbol) return 0;
	if (symbol->kind == PIGEN_SYMBOL_TRANSPORT)
		transport = pigen_symbol_transport(analyzer->model, known->as.symbol);
	return add_resolved_use(analyzer,
		projection.index == PIGEN_INVALID_ID ? expression : projection,
		known->as.symbol, transport, predicate, context);
}

static int visit(use_analyzer *analyzer, pigen_expr_id expression,
	pigen_expr_id projection, pigen_predicate_id predicate,
	pigen_expression_use_context context)
{
	const pigen_semantic_expr *known = pigen_expr_get(analyzer->model,
		expression);
	const pigen_predicate *guard = pigen_predicate_get(analyzer->model,
		predicate);
	if (!known || !guard) return 0;
	if (guard->impossible) return 1;
	switch (known->kind)
	{
		case PIGEN_EXPR_INTEGER:
		case PIGEN_EXPR_BITS:
			return 1;
		case PIGEN_EXPR_SYMBOL:
			return add_symbol_use(analyzer, expression, projection, predicate,
				context);
		case PIGEN_EXPR_GROUP:
			return visit(analyzer, known->as.group.operand,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate, context);
		case PIGEN_EXPR_UNARY:
			return visit(analyzer, known->as.unary.operand,
				INVALID_ID(pigen_expr_id), predicate, context);
		case PIGEN_EXPR_BINARY:
			return visit(analyzer, known->as.binary.left,
				INVALID_ID(pigen_expr_id), predicate, context) &&
				visit(analyzer, known->as.binary.right,
					INVALID_ID(pigen_expr_id), predicate, context);
		case PIGEN_EXPR_CONDITIONAL:
		{
			pigen_predicate_id when_true;
			pigen_predicate_id when_false;
			if (!visit(analyzer, known->as.conditional.condition,
				INVALID_ID(pigen_expr_id), predicate,
				PIGEN_EXPRESSION_USE_READ))
				return 0;
			when_true = pigen_predicate_and_condition(analyzer->model,
				predicate, known->as.conditional.condition, 1);
			when_false = pigen_predicate_and_condition(analyzer->model,
				predicate, known->as.conditional.condition, 0);
			return visit(analyzer, known->as.conditional.when_true,
				INVALID_ID(pigen_expr_id), when_true, context) &&
				visit(analyzer, known->as.conditional.when_false,
					INVALID_ID(pigen_expr_id), when_false, context);
		}
		case PIGEN_EXPR_INDEX:
			return visit(analyzer, known->as.index.base,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate, context) &&
				visit(analyzer, known->as.index.index,
					INVALID_ID(pigen_expr_id), predicate,
					PIGEN_EXPRESSION_USE_INDEX);
	}
	return 0;
}

static int visit_lvalue(use_analyzer *analyzer, pigen_expr_id expression,
	pigen_expr_id projection, pigen_predicate_id predicate)
{
	const pigen_semantic_expr *known = pigen_expr_get(analyzer->model,
		expression);

	if (!known) return 0;
	switch (known->kind)
	{
		case PIGEN_EXPR_SYMBOL:
			return add_symbol_use(analyzer, expression, projection, predicate,
				PIGEN_EXPRESSION_USE_LVALUE);
		case PIGEN_EXPR_GROUP:
			return visit_lvalue(analyzer, known->as.group.operand,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate);
		case PIGEN_EXPR_INDEX:
			return visit_lvalue(analyzer, known->as.index.base,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate) &&
				visit(analyzer, known->as.index.index,
					INVALID_ID(pigen_expr_id), predicate,
					PIGEN_EXPRESSION_USE_INDEX);
		case PIGEN_EXPR_INTEGER:
		case PIGEN_EXPR_BITS:
		case PIGEN_EXPR_UNARY:
		case PIGEN_EXPR_BINARY:
		case PIGEN_EXPR_CONDITIONAL:
			return 0;
	}
	return 0;
}

int pigen_analyze_expression_uses(pigen_semantic_model *model,
	pigen_expr_id expression, pigen_predicate_id predicate,
	pigen_expression_use_context context,
	pigen_expression_use_analysis *analysis)
{
	use_analyzer analyzer = {model, analysis};
	size_t original_use_count;
	size_t original_transport_count;

	if (!model || !analysis || context != PIGEN_EXPRESSION_USE_READ)
		return 0;
	original_use_count = analysis->use_count;
	original_transport_count = analysis->transport_count;
	if (visit(&analyzer, expression, INVALID_ID(pigen_expr_id), predicate,
		context))
		return 1;
	analysis->use_count = original_use_count;
	analysis->transport_count = original_transport_count;
	return 0;
}

int pigen_analyze_lvalue_uses(pigen_semantic_model *model,
	pigen_lvalue_id lvalue_id, pigen_predicate_id predicate,
	pigen_expression_use_analysis *analysis)
{
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model, lvalue_id);
	const pigen_predicate *guard = pigen_predicate_get(model, predicate);
	use_analyzer analyzer = {model, analysis};
	if (!model || !analysis || !lvalue || !guard) return 0;
	if (guard->impossible) return 1;
	return visit_lvalue(&analyzer, lvalue->expression,
		INVALID_ID(pigen_expr_id), predicate);
}

void pigen_free_expression_use_analysis(
	pigen_expression_use_analysis *analysis)
{
	if (!analysis) return;
	free(analysis->uses);
	free(analysis->transports);
	*analysis = (pigen_expression_use_analysis){0};
}
