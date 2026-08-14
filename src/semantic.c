/* Stable semantic identities, lexical scopes, symbols, and structured types. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int id_capacity_available(size_t count)
{
	return count < PIGEN_INVALID_ID;
}

static int span_contains(pigen_source_span outer, pigen_source_span inner)
{
	return outer.source.index == inner.source.index &&
		outer.start <= inner.start && inner.end <= outer.end;
}

static int same_id(uint32_t left, uint32_t right)
{
	return left == right;
}

static int same_name(const pigen_semantic_model *model, pigen_source_span left,
	pigen_source_span right)
{
	const char *left_text;
	const char *right_text;
	size_t left_length;
	size_t right_length;

	left_text = pigen_source_span_text(model->sources, left, &left_length);
	right_text = pigen_source_span_text(model->sources, right, &right_length);
	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
}

static int dimensions_equal(const pigen_semantic_model *model,
	const pigen_semantic_type *type, const pigen_packed_dimension *dimensions,
	size_t count)
{
	size_t i;

	if (type->dimension_count != count)
		return 0;
	for (i = 0; i < count; i++)
	{
		const pigen_packed_dimension *known =
			&model->dimensions[type->first_dimension + i];
		if (!same_id(known->left.index, dimensions[i].left.index) ||
			!same_id(known->right.index, dimensions[i].right.index))
			return 0;
	}
	return 1;
}

void pigen_semantic_init(pigen_semantic_model *model,
	const pigen_source_manager *sources)
{
	*model = (pigen_semantic_model){0};
	model->sources = sources;
	model->compilation_scope = INVALID_ID(pigen_scope_id);
}

pigen_type_id pigen_type_intern(pigen_semantic_model *model,
	pigen_semantic_type_kind kind, pigen_signedness signedness,
	pigen_symbol_id named_symbol, const pigen_packed_dimension *dimensions,
	size_t dimension_count, pigen_source_span span)
{
	size_t i;
	pigen_semantic_type *type;
	pigen_type_id result;

	if (!model->sources || !pigen_source_span_valid(model->sources, span) ||
		kind < PIGEN_TYPE_LOGIC || kind > PIGEN_TYPE_NAMED ||
		signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
		(dimension_count && !dimensions) || !id_capacity_available(model->type_count))
		return INVALID_ID(pigen_type_id);
	if (kind == PIGEN_TYPE_NAMED)
	{
		const pigen_symbol *symbol = pigen_symbol_get(model, named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
			return INVALID_ID(pigen_type_id);
	}
	else if (named_symbol.index != PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	for (i = 0; i < dimension_count; i++)
		if (!pigen_source_span_valid(model->sources, dimensions[i].span) ||
			!span_contains(span, dimensions[i].span) ||
			!pigen_expr_get(model, dimensions[i].left) ||
			!pigen_expr_get(model, dimensions[i].right))
			return INVALID_ID(pigen_type_id);

	for (i = 0; i < model->type_count; i++)
	{
		type = &model->types[i];
		if (type->kind == kind && type->signedness == signedness &&
			type->named_symbol.index == named_symbol.index &&
			dimensions_equal(model, type, dimensions, dimension_count))
			return (pigen_type_id){(uint32_t)i};
	}

	if (model->type_count == model->type_capacity)
	{
		model->type_capacity = model->type_capacity ? model->type_capacity * 2 : 16;
		model->types = pigen_resize(model->types,
			model->type_capacity * sizeof(*model->types));
	}
	if (model->dimension_count + dimension_count > model->dimension_capacity)
	{
		size_t capacity = model->dimension_capacity ? model->dimension_capacity * 2 : 16;
		while (capacity < model->dimension_count + dimension_count)
			capacity *= 2;
		model->dimensions = pigen_resize(model->dimensions,
			capacity * sizeof(*model->dimensions));
		model->dimension_capacity = capacity;
	}

	result = (pigen_type_id){(uint32_t)model->type_count};
	type = &model->types[model->type_count++];
	*type = (pigen_semantic_type){kind, signedness, named_symbol,
		model->dimension_count, dimension_count, span};
	if (dimension_count)
		memcpy(model->dimensions + model->dimension_count, dimensions,
			dimension_count * sizeof(*dimensions));
	model->dimension_count += dimension_count;
	return result;
}

const pigen_semantic_type *pigen_type_get(const pigen_semantic_model *model,
	pigen_type_id type)
{
	if (type.index == PIGEN_INVALID_ID || type.index >= model->type_count)
		return NULL;
	return &model->types[type.index];
}

const pigen_packed_dimension *pigen_type_dimensions(
	const pigen_semantic_model *model, pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(model, type);
	if (!known || !known->dimension_count)
		return NULL;
	return model->dimensions + known->first_dimension;
}

pigen_expr_id pigen_expr_intern_integer(pigen_semantic_model *model,
	uint64_t value, pigen_type_id type, pigen_source_span span)
{
	size_t i;
	pigen_expr_id result;

	if (!pigen_type_get(model, type) ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->expression_count))
		return INVALID_ID(pigen_expr_id);
	for (i = 0; i < model->expression_count; i++)
		if (model->expressions[i].kind == PIGEN_EXPR_INTEGER &&
			model->expressions[i].type.index == type.index &&
			model->expressions[i].integer == value)
			return (pigen_expr_id){(uint32_t)i};
	if (model->expression_count == model->expression_capacity)
	{
		model->expression_capacity = model->expression_capacity ?
			model->expression_capacity * 2 : 16;
		model->expressions = pigen_resize(model->expressions,
			model->expression_capacity * sizeof(*model->expressions));
	}
	result = (pigen_expr_id){(uint32_t)model->expression_count};
	model->expressions[model->expression_count++] = (pigen_semantic_expr){
		PIGEN_EXPR_INTEGER, type, span, value};
	return result;
}

const pigen_semantic_expr *pigen_expr_get(const pigen_semantic_model *model,
	pigen_expr_id expression)
{
	if (expression.index == PIGEN_INVALID_ID ||
		expression.index >= model->expression_count)
		return NULL;
	return &model->expressions[expression.index];
}

pigen_scope_id pigen_scope_add(pigen_semantic_model *model,
	pigen_scope_id parent, pigen_source_span span)
{
	pigen_scope_id result;

	if (!model->sources || !pigen_source_span_valid(model->sources, span) ||
		(parent.index != PIGEN_INVALID_ID && parent.index >= model->scope_count) ||
		!id_capacity_available(model->scope_count))
		return INVALID_ID(pigen_scope_id);
	if (model->scope_count == model->scope_capacity)
	{
		model->scope_capacity = model->scope_capacity ? model->scope_capacity * 2 : 16;
		model->scopes = pigen_resize(model->scopes,
			model->scope_capacity * sizeof(*model->scopes));
	}
	result = (pigen_scope_id){(uint32_t)model->scope_count};
	model->scopes[model->scope_count++] = (pigen_scope){parent,
		INVALID_ID(pigen_symbol_id), span};
	return result;
}

const pigen_scope *pigen_scope_get(const pigen_semantic_model *model,
	pigen_scope_id scope)
{
	if (scope.index == PIGEN_INVALID_ID || scope.index >= model->scope_count)
		return NULL;
	return &model->scopes[scope.index];
}

static pigen_symbol_id lookup_local(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name)
{
	pigen_symbol_id at = model->scopes[scope.index].last_symbol;

	while (at.index != PIGEN_INVALID_ID)
	{
		const pigen_symbol *symbol = &model->symbols[at.index];
		if (same_name(model, symbol->name, name))
			return at;
		at = symbol->previous_in_scope;
	}
	return INVALID_ID(pigen_symbol_id);
}

pigen_declare_result pigen_symbol_declare(pigen_semantic_model *model,
	pigen_scope_id scope, pigen_symbol_kind kind, pigen_type_id type,
	pigen_source_span name, pigen_source_span declaration,
	pigen_symbol_id *declared, pigen_symbol_id *shadowed)
{
	pigen_symbol_id existing;
	pigen_symbol_id result;
	pigen_scope *owner;

	if (declared) *declared = INVALID_ID(pigen_symbol_id);
	if (shadowed) *shadowed = INVALID_ID(pigen_symbol_id);
	if (!pigen_scope_get(model, scope) ||
		((kind == PIGEN_SYMBOL_VALUE || kind == PIGEN_SYMBOL_PARAMETER ||
			kind == PIGEN_SYMBOL_TYPEDEF || kind == PIGEN_SYMBOL_TRANSPORT) &&
			!pigen_type_get(model, type)) ||
		((kind == PIGEN_SYMBOL_MODULE || kind == PIGEN_SYMBOL_PIPELINE ||
			kind == PIGEN_SYMBOL_STAGE || kind == PIGEN_SYMBOL_FSM ||
			kind == PIGEN_SYMBOL_FABRIC) && type.index != PIGEN_INVALID_ID) ||
		kind < PIGEN_SYMBOL_VALUE || kind > PIGEN_SYMBOL_FABRIC ||
		!pigen_source_span_valid(model->sources, name) || name.start == name.end ||
		!pigen_source_span_valid(model->sources, declaration) ||
		!span_contains(declaration, name) || !id_capacity_available(model->symbol_count))
		return PIGEN_DECLARE_INVALID;
	existing = lookup_local(model, scope, name);
	if (existing.index != PIGEN_INVALID_ID)
	{
		if (declared) *declared = existing;
		return PIGEN_DECLARE_DUPLICATE;
	}
	owner = &model->scopes[scope.index];
	if (shadowed && owner->parent.index != PIGEN_INVALID_ID)
		*shadowed = pigen_symbol_lookup(model, owner->parent, name);
	if (model->symbol_count == model->symbol_capacity)
	{
		model->symbol_capacity = model->symbol_capacity ? model->symbol_capacity * 2 : 32;
		model->symbols = pigen_resize(model->symbols,
			model->symbol_capacity * sizeof(*model->symbols));
	}
	result = (pigen_symbol_id){(uint32_t)model->symbol_count};
	model->symbols[model->symbol_count++] = (pigen_symbol){kind, scope,
		owner->last_symbol, type, name, declaration};
	owner->last_symbol = result;
	if (declared) *declared = result;
	return PIGEN_DECLARE_OK;
}

pigen_symbol_id pigen_symbol_lookup(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name)
{
	while (pigen_scope_get(model, scope))
	{
		pigen_symbol_id result = lookup_local(model, scope, name);
		if (result.index != PIGEN_INVALID_ID)
			return result;
		scope = model->scopes[scope.index].parent;
	}
	return INVALID_ID(pigen_symbol_id);
}

const pigen_symbol *pigen_symbol_get(const pigen_semantic_model *model,
	pigen_symbol_id symbol)
{
	if (symbol.index == PIGEN_INVALID_ID || symbol.index >= model->symbol_count)
		return NULL;
	return &model->symbols[symbol.index];
}

const pigen_semantic_module *pigen_module_get(const pigen_semantic_model *model,
	pigen_module_id module)
{
	if (module.index == PIGEN_INVALID_ID || module.index >= model->module_count)
		return NULL;
	return &model->modules[module.index];
}

const pigen_semantic_transport *pigen_transport_get(
	const pigen_semantic_model *model, pigen_transport_id transport)
{
	if (transport.index == PIGEN_INVALID_ID ||
		transport.index >= model->transport_count)
		return NULL;
	return &model->transports[transport.index];
}

void pigen_free_semantic_model(pigen_semantic_model *model)
{
	free(model->types);
	free(model->dimensions);
	free(model->scopes);
	free(model->symbols);
	free(model->expressions);
	free(model->modules);
	free(model->transports);
	*model = (pigen_semantic_model){0};
}
