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

static pigen_const_expr_id analyze_constant(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	pigen_analyzed_expr_arena arena = {0};
	pigen_analyzed_expr_id result;
	pigen_const_expr_id constant = INVALID_ID(pigen_const_expr_id);

	if (pigen_analyze_expression(syntax, model, scope, expression, 1,
		literal_domain, policy, &arena, &result, error))
		constant = pigen_analyzed_expr_get(&arena, result)->constant;
	pigen_free_analyzed_expr_arena(&arena);
	return constant;
}

pigen_symbol_id pigen_resolve_type_symbol(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_type_id type)
{
 if(!syntax || !model) return INVALID_ID(pigen_symbol_id);
 const pigen_syntax_type *written=pigen_syntax_type_get(&syntax->types,type);
 if(!written || written->base.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_symbol_id);
 pigen_source_span name=token_spelling(syntax,written->base);
 return written->has_package ? pigen_symbol_lookup_package_member(model,scope,token_spelling(syntax,written->package),name) :
  pigen_symbol_resolve_at(model,scope,name,written->location.extent.first.index);
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
	pigen_literal_domain literal_domain = PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG;
	pigen_type_spelling_domain spelling_domain = PIGEN_TYPE_SPELLING_UNKNOWN;
	pigen_symbol_id alias = INVALID_ID(pigen_symbol_id);
	const pigen_symbol *alias_symbol = NULL;
	pigen_source_span spelling = {INVALID_ID(pigen_source_id), 0, 0};
	pigen_data_type_id result;
	size_t i;

	if (!syntax || !model || !syntax_type)
		return INVALID_ID(pigen_data_type_id);
	if (syntax_type->is_systemverilog_int)
		return pigen_data_type_sized_integral(model,32,
			syntax_type->signedness==PIGEN_SYNTAX_SIGN_UNSIGNED ? PIGEN_SIGN_UNSIGNED : PIGEN_SIGN_SIGNED,
			PIGEN_DATA_TYPE_STATE_TWO);
	if (syntax_type->is_packed_struct)
	{
		for (size_t n = 0; n < model->record_type_count; n++)
		{
			const pigen_semantic_record_type *known = &model->record_types[n];
			pigen_source_span a = known->declaration, b = syntax_type->location.source_span;
			if (known->scope.index == scope.index &&
				a.source.index == b.source.index && a.start == b.start && a.end == b.end &&
				known->first.index == syntax_type->location.extent.first.index &&
				known->after.index == syntax_type->location.extent.after.index)
				return known->data_type;
		}
		const pigen_syntax_struct_member *members =
			pigen_syntax_type_members(&syntax->types,
				syntax_type->first_member, syntax_type->member_count);
		pigen_data_type_id *member_types;
		pigen_data_type_id flat;

		if (syntax_type->member_count && !members)
			return INVALID_ID(pigen_data_type_id);
		member_types = pigen_resize(NULL,
			syntax_type->member_count * sizeof(*member_types));
		for (i = 0; i < syntax_type->member_count; i++)
		{
			member_types[i] = pigen_resolve_type(syntax,
				model, scope, members[i].type, error);

			if (member_types[i].index == PIGEN_INVALID_ID)
			{
				free(member_types);
				return INVALID_ID(pigen_data_type_id);
			}
			if (!pigen_data_type_is_integral(model, member_types[i]))
			{
				free(member_types);
				return fail(error, syntax_type->location, "packed record members require integral DATA types");
			}
		}
		flat = pigen_data_type_concatenation(model, member_types,
			syntax_type->member_count);
		if (flat.index == PIGEN_INVALID_ID) { free(member_types); return flat; }
		/* The alias lives in an unreachable child scope: it supplies nominal
		 * schema identity without introducing a user-visible type name. */
		pigen_source_span name = token_spelling(syntax, syntax_type->base);
		pigen_scope_id private_scope = pigen_scope_add(model, scope, syntax_type->location.source_span);
		pigen_symbol_id owner;
		if (private_scope.index == PIGEN_INVALID_ID ||
			pigen_symbol_declare(model, private_scope, PIGEN_SYMBOL_TYPEDEF, flat,
				name, name, &owner, NULL) != PIGEN_DECLARE_OK)
		{ free(member_types); return INVALID_ID(pigen_data_type_id); }
		pigen_data_type_id record = pigen_data_type_alias(model, owner, flat,
			PIGEN_SIGN_UNSIGNED, NULL, 0);
		uint64_t offset;
		if (record.index == PIGEN_INVALID_ID ||
			!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, flat), &offset))
		{ free(member_types); return INVALID_ID(pigen_data_type_id); }
		for (i = 0; i < syntax_type->member_count; i++)
		{
			uint64_t width;
			if (!pigen_const_expr_evaluate_u64(model,
				pigen_data_type_packed_width(model, member_types[i]), &width) ||
				!width || width > offset || width > SIZE_MAX)
			{ free(member_types); return INVALID_ID(pigen_data_type_id); }
			offset -= width;
			if (offset > SIZE_MAX || !pigen_struct_member_add(model, owner,
				token_spelling(syntax, members[i].name), member_types[i], (size_t)offset))
			{ free(member_types); return INVALID_ID(pigen_data_type_id); }
		}
		free(member_types);
		if (model->record_type_count == model->record_type_capacity)
		{
			model->record_type_capacity = model->record_type_capacity ? model->record_type_capacity * 2 : 8;
			model->record_types = pigen_resize(model->record_types,
				model->record_type_capacity * sizeof(*model->record_types));
		}
		model->record_types[model->record_type_count++] =
			(pigen_semantic_record_type){scope, syntax_type->location.source_span,
				syntax_type->location.extent.first, syntax_type->location.extent.after, record};
		return record;
	}
	signedness = syntax_type->signedness == PIGEN_SYNTAX_SIGN_SIGNED ?
		PIGEN_SIGN_SIGNED : syntax_type->signedness == PIGEN_SYNTAX_SIGN_UNSIGNED ?
		PIGEN_SIGN_UNSIGNED : PIGEN_SIGN_IMPLICIT;
	if (syntax_type->base.index != PIGEN_INVALID_ID)
	{
		spelling = token_spelling(syntax, syntax_type->base);
		spelling_domain = pigen_data_type_spelling_domain(model, spelling);
		if (spelling_domain == PIGEN_TYPE_SPELLING_UNKNOWN)
		{
			alias = pigen_resolve_type_symbol(syntax,model,scope,syntax_type_id);
			alias_symbol = pigen_symbol_get(model, alias);
			if(!syntax_type->has_package && alias.index==PIGEN_INVALID_ID &&
			 pigen_symbol_import_ambiguous(model,scope,spelling,syntax_type->location.extent.first.index))
			 return fail(error,syntax_type->location,"ambiguous wildcard package import");
			if(alias_symbol && alias_symbol->kind==PIGEN_SYMBOL_PROTOCOL_TYPE)
				return fail(error,syntax_type->location,"native AXI endpoint type is not a packed data type");
		}
		if (spelling_domain == PIGEN_TYPE_SPELLING_PIGEN ||
			(alias_symbol && alias_symbol->kind == PIGEN_SYMBOL_TYPEDEF &&
			pigen_data_type_domain(model, alias_symbol->data_type) ==
				PIGEN_TYPE_SPELLING_PIGEN))
			literal_domain = PIGEN_LITERAL_DOMAIN_PIGEN;
	}
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
				syntax_arguments[i].as.count, literal_domain, NULL, error);
			arguments[i].kind = PIGEN_DATA_TYPE_ARGUMENT_COUNT;
		}
		else
		{
			left = analyze_constant(syntax, model, scope,
				syntax_arguments[i].as.range.left, literal_domain, NULL, error);
			right = analyze_constant(syntax, model, scope,
				syntax_arguments[i].as.range.right, literal_domain, NULL, error);
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
			arguments[i].as.count = pigen_const_expr_normalize_count(model, left);
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
		result = pigen_data_type_from_spelling(model, spelling, signedness,
			arguments, syntax_type->argument_count);
		if (result.index == PIGEN_INVALID_ID &&
			spelling_domain == PIGEN_TYPE_SPELLING_UNKNOWN)
		{
			if (alias_symbol && alias_symbol->kind == PIGEN_SYMBOL_TYPEDEF)
				result = pigen_data_type_alias_with_arguments(model, alias,
					alias_symbol->data_type, signedness, arguments,
					syntax_type->argument_count);
		}
		if (result.index == PIGEN_INVALID_ID)
		{
			free(arguments);
			return fail(error, syntax_type->location,
				spelling_domain == PIGEN_TYPE_SPELLING_UNKNOWN ? "unknown type name" :
				"invalid type arguments");
		}
	}
	free(arguments);
	return result;
}

pigen_data_type_id pigen_resolve_cast_type(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	const pigen_syntax_expr *cast, pigen_data_type_id operand,
	const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	if (!syntax || !model || !cast || cast->kind != PIGEN_SYNTAX_EXPR_CAST)
		return INVALID_ID(pigen_data_type_id);
	pigen_const_expr_id size = INVALID_ID(pigen_const_expr_id);
	int is_size = cast->as.cast.is_size;
	if (is_size)
		size = analyze_constant(syntax, model, scope, cast->as.cast.size,
			PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, policy, error);
	else
	{
		const pigen_syntax_type *type = pigen_syntax_type_get(&syntax->types, cast->as.cast.type);
		if (type && type->base.index != PIGEN_INVALID_ID && !type->argument_count &&
			!type->is_packed_struct && type->signedness == PIGEN_SYNTAX_SIGN_IMPLICIT)
		{
			pigen_symbol_id symbol = pigen_resolve_type_symbol(syntax,model,scope,cast->as.cast.type);
			const pigen_symbol *known = pigen_symbol_get(model, symbol);
			if (known && known->kind == PIGEN_SYMBOL_PARAMETER)
			{
				is_size = 1;
				size = pigen_const_expr_intern_symbol(model, symbol, known->data_type);
			}
		}
		if (!is_size) return pigen_resolve_type(syntax, model, scope, cast->as.cast.type, error);
	}
	int64_t width;
	if (!pigen_const_expr_evaluate_i64(model, size, &width) || width <= 0 ||
		(uint64_t)width > SIZE_MAX)
		return fail(error, cast->location, "size cast requires a known positive integral width");
	if (policy && (uint64_t)width > policy->maximum_generated_bits)
		return fail(error, cast->location, "size cast exceeds generated width limit");
	pigen_numerical_interpretation interpretation = pigen_data_type_numerical_interpretation(model, operand);
	if (!pigen_data_type_is_integral(model, operand) && interpretation != PIGEN_NUMERICAL_EXACT_INTEGER)
		return fail(error, cast->location, "size cast requires an integral operand");
	pigen_signedness sign = pigen_data_type_signedness(model, operand);
	if (sign == PIGEN_SIGN_IMPLICIT)
		sign = interpretation == PIGEN_NUMERICAL_EXACT_INTEGER ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED;
	return pigen_data_type_sized_integral(model, (size_t)width, sign,
		pigen_data_type_state_domain(model, operand));
}
