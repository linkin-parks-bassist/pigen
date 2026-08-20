#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_resolve.h"
#include "pigen/expression_use.h"
#include "pigen/resolve.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int token_is(const pigen_expanded_source *source, size_t at,
	const char *expected)
{
	const pigen_expanded_token *token = pigen_expanded_token_get(source,
		(pigen_token_id){(uint32_t)at});
	const char *text;
	size_t length;
	if (!token) return 0;
	text = pigen_expanded_token_text(source, token, &length);
	return text && length == strlen(expected) &&
		!memcmp(text, expected, length);
}

int main(void)
{
	const char text[] =
		"module uses;\n"
		"  buf [7:0] left, right;\n"
		"endmodule\n"
		"left ? right : left\n"
		"(right)\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "uses.pigen", text,
		strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_error syntax_error = {0};
	pigen_resolve_error resolve_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_syntax_expr_id syntax_expression = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_lvalue = INVALID_ID(pigen_syntax_expr_id);
	pigen_expr_id expression;
	pigen_expr_id lvalue_expression;
	pigen_lvalue_id lvalue;
	pigen_predicate_id always;
	pigen_expression_use_analysis analysis = {0};
	const pigen_semantic_expr *conditional;
	const pigen_predicate_atom *true_atoms;
	const pigen_predicate_atom *false_atoms;
	size_t first;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded, &syntax, &syntax_error));
	assert(pigen_resolve_declarations(&syntax, &model, &resolve_error));
	for (first = 0; first < preprocessed.expanded.token_count; first++)
		if (token_is(&preprocessed.expanded, first, "endmodule")) break;
	assert(first + 1 < preprocessed.expanded.token_count);
	first++;
	assert(preprocessed.expanded.token_count - 1 == first + 8);
	assert(pigen_parse_expression(&preprocessed.expanded, first, first + 5,
		&syntax.expressions,
		&syntax_expression, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 5, first + 8,
		&syntax.expressions, &syntax_lvalue, &syntax_error));
	expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_expression);
	lvalue_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope, syntax_lvalue);
	conditional = pigen_expr_get(&model, expression);
	assert(conditional && conditional->kind == PIGEN_EXPR_CONDITIONAL);
	always = pigen_predicate_true(&model);
	assert(pigen_analyze_expression_uses(&model, expression, always,
		PIGEN_EXPRESSION_USE_READ, &analysis));

	assert(analysis.use_count == 3);
	assert(analysis.transport_count == 2);
	assert(analysis.uses[0].symbol.index ==
		pigen_expr_get(&model, conditional->as.conditional.condition)->
		as.symbol.index);
	assert(analysis.uses[0].predicate.index == always.index);
	assert(analysis.uses[0].transport.index ==
		analysis.uses[2].transport.index);
	assert(analysis.uses[0].expression.index !=
		analysis.uses[2].expression.index);
	assert(analysis.uses[1].transport.index !=
		analysis.uses[0].transport.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.transports[0].contexts == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.transports[1].contexts == PIGEN_EXPRESSION_USE_READ);

	true_atoms = pigen_predicate_atoms(&model, analysis.uses[1].predicate);
	false_atoms = pigen_predicate_atoms(&model, analysis.uses[2].predicate);
	assert(pigen_predicate_get(&model,
		analysis.uses[1].predicate)->atom_count == 1);
	assert(pigen_predicate_get(&model,
		analysis.uses[2].predicate)->atom_count == 1);
	assert(true_atoms && false_atoms);
	assert(true_atoms[0].condition.index ==
		conditional->as.conditional.condition.index);
	assert(false_atoms[0].condition.index ==
		conditional->as.conditional.condition.index);
	assert(true_atoms[0].expected && !false_atoms[0].expected);
	assert(pigen_predicates_mutually_exclusive(&model,
		analysis.uses[1].predicate, analysis.uses[2].predicate));

	lvalue = pigen_lvalue_resolve(&model, lvalue_expression);
	assert(lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_lvalue_resolve(&model, lvalue_expression).index == lvalue.index);
	assert(pigen_lvalue_resolve(&model, expression).index == PIGEN_INVALID_ID);
	assert(pigen_lvalue_get(&model, lvalue)->expression.index ==
		lvalue_expression.index);
	assert(pigen_lvalue_get(&model, lvalue)->transport.index ==
		analysis.uses[1].transport.index);
	assert(pigen_analyze_lvalue_uses(&model, lvalue, always, &analysis));
	assert(analysis.use_count == 4);
	assert(analysis.transport_count == 2);
	assert(analysis.uses[3].expression.index == lvalue_expression.index);
	assert(analysis.uses[3].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[3].transport.index == analysis.uses[1].transport.index);
	assert(analysis.transports[1].contexts ==
		(PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_LVALUE));

	pigen_free_expression_use_analysis(&analysis);
	pigen_free_semantic_model(&model);
	pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: expression uses retain identity, projection, and predicate");
	return 0;
}
