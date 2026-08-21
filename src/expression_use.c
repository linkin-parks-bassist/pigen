/* Identity-based symbol and signal use analysis over semantic expressions. */
#include <stdlib.h>

#include "pigen/expression_use.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	pigen_semantic_model *model;
	pigen_expression_use_analysis *analysis;
} use_analyzer;

static int add_signal_summary(use_analyzer *analyzer,
	pigen_signal_id signal, pigen_expression_use_context context)
{
	pigen_expression_use_analysis *analysis = analyzer->analysis;
	size_t i;
	for (i = 0; i < analysis->signal_count; i++)
		if (analysis->signals[i].signal.index == signal.index)
		{
			analysis->signals[i].contexts |= (unsigned)context;
			return 1;
		}
	if (analysis->signal_count == analysis->signal_capacity)
	{
		analysis->signal_capacity = analysis->signal_capacity ?
			analysis->signal_capacity * 2 : 8;
		analysis->signals = pigen_resize(analysis->signals,
			analysis->signal_capacity * sizeof(*analysis->signals));
	}
	analysis->signals[analysis->signal_count++] =
		(pigen_expression_signal_use){signal, (unsigned)context};
	return 1;
}

static int add_resolved_use(use_analyzer *analyzer, pigen_expr_id projection,
	pigen_symbol_id symbol_id, pigen_signal_id signal,
	pigen_predicate_id predicate, pigen_expression_use_context context)
{
	const pigen_symbol *symbol = pigen_symbol_get(analyzer->model, symbol_id);
	pigen_expression_use_analysis *analysis = analyzer->analysis;

	if (!symbol) return 0;
	if (signal.index != PIGEN_INVALID_ID)
	{
		if (pigen_symbol_signal(analyzer->model, symbol_id).index !=
			signal.index ||
			!add_signal_summary(analyzer, signal, context))
			return 0;
	}
	else if (symbol->kind == PIGEN_SYMBOL_SIGNAL)
		return 0;
	if (analysis->use_count == analysis->use_capacity)
	{
		analysis->use_capacity = analysis->use_capacity ?
			analysis->use_capacity * 2 : 16;
		analysis->uses = pigen_resize(analysis->uses,
			analysis->use_capacity * sizeof(*analysis->uses));
	}
	analysis->uses[analysis->use_count++] = (pigen_expression_use){
		projection, symbol_id, signal, predicate, context};
	return 1;
}

static int add_symbol_use(use_analyzer *analyzer, pigen_expr_id expression,
	pigen_expr_id projection, pigen_predicate_id predicate,
	pigen_expression_use_context context)
{
	const pigen_semantic_expr *known = pigen_expr_get(analyzer->model,
		expression);
	const pigen_symbol *symbol;
	pigen_signal_id signal = INVALID_ID(pigen_signal_id);

	if (!known || known->kind != PIGEN_EXPR_SYMBOL) return 0;
	symbol = pigen_symbol_get(analyzer->model, known->as.symbol);
	if (!symbol) return 0;
	if (symbol->kind == PIGEN_SYMBOL_SIGNAL)
		signal = pigen_symbol_signal(analyzer->model, known->as.symbol);
	return add_resolved_use(analyzer,
		projection.index == PIGEN_INVALID_ID ? expression : projection,
		known->as.symbol, signal, predicate, context);
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
		case PIGEN_EXPR_SELECT:
			return visit(analyzer, known->as.select.base,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate, context) &&
				visit(analyzer, known->as.select.left,
					INVALID_ID(pigen_expr_id), predicate,
					known->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE ?
						PIGEN_EXPRESSION_USE_TYPE :
						PIGEN_EXPRESSION_USE_INDEX) &&
				visit(analyzer, known->as.select.right,
					INVALID_ID(pigen_expr_id), predicate,
					PIGEN_EXPRESSION_USE_TYPE);
		case PIGEN_EXPR_CONCATENATION:
		{
			const pigen_expr_id *children = pigen_expr_children(
				analyzer->model, known->as.sequence.first_child,
				known->as.sequence.child_count);
			size_t i;
			if (!children) return 0;
			for (i = 0; i < known->as.sequence.child_count; i++)
				if (!visit(analyzer, children[i],
					INVALID_ID(pigen_expr_id), predicate, context))
					return 0;
			return 1;
		}
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
		case PIGEN_EXPR_SELECT:
			return visit_lvalue(analyzer, known->as.select.base,
				projection.index == PIGEN_INVALID_ID ? expression : projection,
				predicate) &&
				visit(analyzer, known->as.select.left,
					INVALID_ID(pigen_expr_id), predicate,
					known->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE ?
						PIGEN_EXPRESSION_USE_TYPE :
						PIGEN_EXPRESSION_USE_INDEX) &&
				visit(analyzer, known->as.select.right,
					INVALID_ID(pigen_expr_id), predicate,
					PIGEN_EXPRESSION_USE_TYPE);
		case PIGEN_EXPR_INTEGER:
		case PIGEN_EXPR_BITS:
		case PIGEN_EXPR_UNARY:
		case PIGEN_EXPR_BINARY:
		case PIGEN_EXPR_CONDITIONAL:
		case PIGEN_EXPR_CONCATENATION:
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
	size_t original_signal_count;

	if (!model || !analysis || context != PIGEN_EXPRESSION_USE_READ)
		return 0;
	original_use_count = analysis->use_count;
	original_signal_count = analysis->signal_count;
	if (visit(&analyzer, expression, INVALID_ID(pigen_expr_id), predicate,
		context))
		return 1;
	analysis->use_count = original_use_count;
	analysis->signal_count = original_signal_count;
	return 0;
}

static int analyze_lvalue(use_analyzer *analyzer,
	pigen_lvalue_id lvalue_id, pigen_predicate_id predicate)
{
	const pigen_semantic_lvalue *lvalue =
		pigen_lvalue_get(analyzer->model, lvalue_id);
	const pigen_lvalue_id *children;
	size_t i;

	if (!lvalue) return 0;
	if (lvalue->kind == PIGEN_LVALUE_PROJECTION)
		return visit_lvalue(analyzer, lvalue->expression,
			INVALID_ID(pigen_expr_id), predicate);
	if (lvalue->kind != PIGEN_LVALUE_CONCATENATION) return 0;
	children = pigen_lvalue_children(analyzer->model,
		lvalue->as.sequence.first_child, lvalue->as.sequence.child_count);
	if (!children) return 0;
	for (i = 0; i < lvalue->as.sequence.child_count; i++)
		if (!analyze_lvalue(analyzer, children[i], predicate))
			return 0;
	return 1;
}

int pigen_analyze_lvalue_uses(pigen_semantic_model *model,
	pigen_lvalue_id lvalue_id, pigen_predicate_id predicate,
	pigen_expression_use_analysis *analysis)
{
	const pigen_predicate *guard = pigen_predicate_get(model, predicate);
	use_analyzer analyzer = {model, analysis};
	if (!model || !analysis || !pigen_lvalue_get(model, lvalue_id) || !guard)
		return 0;
	if (guard->impossible) return 1;
	return analyze_lvalue(&analyzer, lvalue_id, predicate);
}

void pigen_free_expression_use_analysis(
	pigen_expression_use_analysis *analysis)
{
	if (!analysis) return;
	free(analysis->uses);
	free(analysis->signals);
	*analysis = (pigen_expression_use_analysis){0};
}
