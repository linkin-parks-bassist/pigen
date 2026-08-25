/* One syntax-type to semantic-data-type resolution boundary. */
#include <stdlib.h>

#include "pigen/expression_analysis.h"
#include "pigen/type_resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_data_type_id fail(pigen_semantic_error *error,
	pigen_syntax_location location, const char *message)
{
	if (error)
	{
		error->origin = location.origin;
		error->span = location.source_span;
		error->message = message;
	}
	return INVALID_ID(pigen_data_type_id);
}

static pigen_source_span token_spelling(const pigen_syntax_tree *syntax,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		syntax->expanded, token);

	return known ? pigen_origin_spelling_span(syntax->expanded, known->origin) :
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static pigen_const_expr_id normalize_count(pigen_semantic_model *model,
	pigen_const_expr_id value)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, value);

	if (!known) return INVALID_ID(pigen_const_expr_id);
	if (known->kind == PIGEN_CONST_EXPR_EXACT_INTEGER)
	{
		pigen_integer_id integer = known->as.exact_integer;
		const pigen_integer *exact = &model->integers[integer.index];
		uint64_t normalized;

		if (exact->negative || !exact->limb_count || exact->limb_count > 2)
			return INVALID_ID(pigen_const_expr_id);
		normalized = model->integer_limbs[exact->first_limb];
		if (exact->limb_count == 2)
			normalized |= (uint64_t)model->integer_limbs[exact->first_limb + 1]
				<< 32;
		return pigen_const_expr_intern_integer(model, normalized,
			pigen_data_type_unsized_integer(model));
	}
	if (known->kind == PIGEN_CONST_EXPR_INTEGER && !known->as.integer)
		return INVALID_ID(pigen_const_expr_id);
	return value;
}

static pigen_const_expr_id analyze_constant(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, pigen_semantic_error *error)
{
	pigen_analyzed_expr_arena arena = {0};
	pigen_analyzed_expr_id result;
	pigen_const_expr_id constant = INVALID_ID(pigen_const_expr_id);

	if (pigen_analyze_expression(syntax, model, scope, expression, 1, &arena,
		&result, error))
		constant = pigen_analyzed_expr_get(&arena, result)->constant;
	pigen_free_analyzed_expr_arena(&arena);
	return constant;
}

pigen_data_type_id pigen_resolve_type(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_type_id syntax_type_id, pigen_semantic_error *error)
{
	const pigen_syntax_type *syntax_type = pigen_syntax_type_get(&syntax->types,
		syntax_type_id);
	const pigen_syntax_type_argument *syntax_arguments;
	pigen_data_type_argument *arguments = NULL;
	pigen_signedness signedness;
	pigen_data_type_id result;
	size_t i;

	if (!syntax || !model || !syntax_type)
		return INVALID_ID(pigen_data_type_id);
	signedness = syntax_type->signedness == PIGEN_SYNTAX_SIGN_SIGNED ?
		PIGEN_SIGN_SIGNED : syntax_type->signedness == PIGEN_SYNTAX_SIGN_UNSIGNED ?
		PIGEN_SIGN_UNSIGNED : PIGEN_SIGN_IMPLICIT;
	syntax_arguments = pigen_syntax_type_arguments(&syntax->types,
		syntax_type->first_argument, syntax_type->argument_count);
	if (syntax_type->argument_count && !syntax_arguments)
		return INVALID_ID(pigen_data_type_id);
	if (syntax_type->argument_count)
		arguments = pigen_resize(NULL,
			syntax_type->argument_count * sizeof(*arguments));
	for (i = 0; i < syntax_type->argument_count; i++)
	{
		pigen_const_expr_id left;
		pigen_const_expr_id right = INVALID_ID(pigen_const_expr_id);

		if (syntax_arguments[i].kind == PIGEN_SYNTAX_TYPE_COUNT)
		{
			left = analyze_constant(syntax, model, scope,
				syntax_arguments[i].as.count, error);
			arguments[i].kind = PIGEN_DATA_TYPE_ARGUMENT_COUNT;
		}
		else
		{
			left = analyze_constant(syntax, model, scope,
				syntax_arguments[i].as.range.left, error);
			right = analyze_constant(syntax, model, scope,
				syntax_arguments[i].as.range.right, error);
			arguments[i].kind = PIGEN_DATA_TYPE_ARGUMENT_RANGE;
		}
		if (left.index == PIGEN_INVALID_ID ||
			(syntax_arguments[i].kind == PIGEN_SYNTAX_TYPE_RANGE &&
			right.index == PIGEN_INVALID_ID))
		{
			free(arguments);
			return fail(error, syntax_arguments[i].location,
				"type arguments require constant expressions");
		}
		if (arguments[i].kind == PIGEN_DATA_TYPE_ARGUMENT_COUNT)
		{
			arguments[i].as.count = normalize_count(model, left);
			if (arguments[i].as.count.index == PIGEN_INVALID_ID)
			{
				free(arguments);
				return fail(error, syntax_arguments[i].location,
					"type count must be a positive integer");
			}
		}
		else
		{
			arguments[i].as.range.left = left;
			arguments[i].as.range.right = right;
		}
	}
	if (syntax_type->base.index == PIGEN_INVALID_ID)
		result = pigen_data_type_implicit_with_arguments(model, signedness,
			arguments, syntax_type->argument_count);
	else
	{
		pigen_source_span spelling = token_spelling(syntax, syntax_type->base);
		pigen_type_spelling_domain domain = pigen_data_type_spelling_domain(model,
			spelling);

		result = pigen_data_type_from_spelling(model, spelling, signedness,
			arguments, syntax_type->argument_count);
		if (result.index == PIGEN_INVALID_ID &&
			domain == PIGEN_TYPE_SPELLING_UNKNOWN)
		{
			pigen_symbol_id alias = pigen_symbol_lookup(model, scope, spelling);
			const pigen_symbol *symbol = pigen_symbol_get(model, alias);

			if (symbol && symbol->kind == PIGEN_SYMBOL_TYPEDEF)
				result = pigen_data_type_alias_with_arguments(model, alias,
					symbol->data_type, signedness, arguments,
					syntax_type->argument_count);
		}
		if (result.index == PIGEN_INVALID_ID)
		{
			free(arguments);
			return fail(error, syntax_type->location,
				domain == PIGEN_TYPE_SPELLING_UNKNOWN ? "unknown type name" :
				"invalid type arguments");
		}
	}
	free(arguments);
	return result;
}
