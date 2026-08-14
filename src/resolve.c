/* Declaration resolution from structured syntax into stable semantic IDs. */
#include <stdlib.h>
#include <string.h>

#include "pigen/resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_source_manager *sources;
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	pigen_resolve_error *error;
	pigen_type_id integer_type;
} resolver;

static int fail(resolver *resolver, pigen_source_span span, const char *message)
{
	if (resolver->error)
	{
		resolver->error->span = span;
		resolver->error->message = message;
	}
	return 0;
}

static int decimal_value(resolver *resolver, pigen_source_span span,
	uint64_t *value)
{
	const char *text;
	size_t length;
	size_t i;
	uint64_t result = 0;

	text = pigen_source_span_text(resolver->sources, span, &length);
	if (!text || !length)
		return 0;
	for (i = 0; i < length; i++)
	{
		unsigned digit;
		if (text[i] < '0' || text[i] > '9') return 0;
		digit = (unsigned)(text[i] - '0');
		if (result > (UINT64_MAX - digit) / 10) return 0;
		result = result * 10 + digit;
	}
	*value = result;
	return 1;
}

static pigen_expr_id resolve_constant(resolver *resolver,
	pigen_source_span span)
{
	uint64_t value;
	if (!decimal_value(resolver, span, &value))
		return INVALID_ID(pigen_expr_id);
	return pigen_expr_intern_integer(resolver->model, value,
		resolver->integer_type, span);
}

static pigen_type_id resolve_type(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_type *syntax_type)
{
	pigen_semantic_type_kind kind;
	pigen_signedness signedness;
	pigen_symbol_id named_symbol = INVALID_ID(pigen_symbol_id);
	pigen_packed_dimension *dimensions = NULL;
	const pigen_syntax_dimension *syntax_dimensions;
	pigen_type_id result;
	size_t i;

	if (syntax_type->base == PIGEN_SYNTAX_TYPE_NAMED)
	{
		const pigen_symbol *symbol;
		named_symbol = pigen_symbol_lookup(resolver->model, scope,
			syntax_type->base_name);
		symbol = pigen_symbol_get(resolver->model, named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
		{
			fail(resolver, syntax_type->base_name, "unknown type name");
			return INVALID_ID(pigen_type_id);
		}
		kind = PIGEN_TYPE_NAMED;
	}
	else
		kind = syntax_type->base == PIGEN_SYNTAX_TYPE_BIT ?
			PIGEN_TYPE_BIT : PIGEN_TYPE_LOGIC;
	if (syntax_type->signedness == PIGEN_SYNTAX_SIGN_SIGNED)
		signedness = PIGEN_SIGN_SIGNED;
	else if (syntax_type->signedness == PIGEN_SYNTAX_SIGN_UNSIGNED)
		signedness = PIGEN_SIGN_UNSIGNED;
	else
		signedness = PIGEN_SIGN_IMPLICIT;
	if (syntax_type->dimension_count)
	{
		dimensions = pigen_resize(NULL,
			syntax_type->dimension_count * sizeof(*dimensions));
		syntax_dimensions = pigen_syntax_type_dimensions(resolver->syntax,
			syntax_type);
		if (!syntax_dimensions)
		{
			free(dimensions);
			return INVALID_ID(pigen_type_id);
		}
		for (i = 0; i < syntax_type->dimension_count; i++)
		{
			dimensions[i].left = resolve_constant(resolver, syntax_dimensions[i].left);
			dimensions[i].right = resolve_constant(resolver, syntax_dimensions[i].right);
			dimensions[i].span = syntax_dimensions[i].span;
			if (dimensions[i].left.index == PIGEN_INVALID_ID ||
				dimensions[i].right.index == PIGEN_INVALID_ID)
			{
				fail(resolver, syntax_dimensions[i].span,
					"packed bounds currently require decimal constants");
				free(dimensions);
				return INVALID_ID(pigen_type_id);
			}
		}
	}
	result = pigen_type_intern(resolver->model, kind, signedness,
		named_symbol, dimensions, syntax_type->dimension_count,
		syntax_type->span);
	free(dimensions);
	return result;
}

static pigen_semantic_transport_kind semantic_transport_kind(
	pigen_syntax_transport_kind kind)
{
	switch (kind)
	{
		case PIGEN_TRANSPORT_BUF: return PIGEN_SEMANTIC_BUF;
		case PIGEN_TRANSPORT_PORT: return PIGEN_SEMANTIC_PORT;
		case PIGEN_TRANSPORT_SKID: return PIGEN_SEMANTIC_SKID;
		case PIGEN_TRANSPORT_FIFO: return PIGEN_SEMANTIC_FIFO;
	}
	pigen_fail("invalid syntax transport kind");
	return PIGEN_SEMANTIC_BUF;
}

static pigen_semantic_direction semantic_direction(pigen_syntax_direction direction)
{
	switch (direction)
	{
		case PIGEN_DIRECTION_INTERNAL: return PIGEN_SEMANTIC_INTERNAL;
		case PIGEN_DIRECTION_INPUT: return PIGEN_SEMANTIC_INPUT;
		case PIGEN_DIRECTION_OUTPUT: return PIGEN_SEMANTIC_OUTPUT;
		case PIGEN_DIRECTION_INOUT: return PIGEN_SEMANTIC_INOUT;
	}
	pigen_fail("invalid syntax transport direction");
	return PIGEN_SEMANTIC_INTERNAL;
}

static int add_transport(resolver *resolver, pigen_module_id module_id,
	const pigen_syntax_node *syntax_node, pigen_syntax_id syntax_id)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_type_id payload_type;
	pigen_expr_id depth = INVALID_ID(pigen_expr_id);
	pigen_symbol_id symbol;
	pigen_declare_result declared;

	if (syntax_node->as.transport.direction == PIGEN_DIRECTION_INOUT)
		return fail(resolver, syntax_node->span,
			"transport ports must be input or output, not inout");
	payload_type = resolve_type(resolver, module->scope,
		&syntax_node->as.transport.payload);
	if (payload_type.index == PIGEN_INVALID_ID) return 0;
	if (syntax_node->as.transport.kind == PIGEN_TRANSPORT_FIFO)
	{
		depth = resolve_constant(resolver, syntax_node->as.transport.fifo_depth);
		if (depth.index == PIGEN_INVALID_ID)
			return fail(resolver, syntax_node->as.transport.fifo_depth,
				"fifo depth currently requires a decimal constant");
	}
	declared = pigen_symbol_declare(model, module->scope, PIGEN_SYMBOL_TRANSPORT,
		payload_type, syntax_node->as.transport.name, syntax_node->span,
		&symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail(resolver, syntax_node->as.transport.name,
			"duplicate module declaration");
	if (declared != PIGEN_DECLARE_OK)
		return fail(resolver, syntax_node->span, "invalid transport declaration");
	if (model->transport_count == PIGEN_INVALID_ID)
		pigen_fail("too many transports");
	if (model->transport_count == model->transport_capacity)
	{
		model->transport_capacity = model->transport_capacity ?
			model->transport_capacity * 2 : 32;
		model->transports = pigen_resize(model->transports,
			model->transport_capacity * sizeof(*model->transports));
	}
	model->transports[model->transport_count++] = (pigen_semantic_transport){
		syntax_id, module_id, symbol, payload_type, depth,
		semantic_transport_kind(syntax_node->as.transport.kind),
		semantic_direction(syntax_node->as.transport.direction), syntax_node->span};
	return 1;
}

static int add_typedef(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_node *syntax_node)
{
	pigen_type_id underlying = resolve_type(resolver, scope,
		&syntax_node->as.type_definition.type);
	pigen_symbol_id symbol;
	pigen_declare_result declared;

	if (underlying.index == PIGEN_INVALID_ID) return 0;
	declared = pigen_symbol_declare(resolver->model, scope, PIGEN_SYMBOL_TYPEDEF,
		underlying, syntax_node->as.type_definition.name, syntax_node->span,
		&symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail(resolver, syntax_node->as.type_definition.name,
			"duplicate typedef name");
	if (declared != PIGEN_DECLARE_OK)
		return fail(resolver, syntax_node->span, "invalid typedef declaration");
	return 1;
}

static int add_module(resolver *resolver, const pigen_syntax_node *syntax_node,
	pigen_syntax_id syntax_id)
{
	pigen_semantic_model *model = resolver->model;
	pigen_symbol_id symbol;
	pigen_declare_result declared;
	pigen_scope_id scope;
	pigen_module_id module_id;
	pigen_syntax_id child;

	declared = pigen_symbol_declare(model, model->compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_type_id), syntax_node->as.module.name,
		syntax_node->span, &symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail(resolver, syntax_node->as.module.name, "duplicate module name");
	if (declared != PIGEN_DECLARE_OK)
		return fail(resolver, syntax_node->span, "invalid module declaration");
	scope = pigen_scope_add(model, model->compilation_scope, syntax_node->span);
	if (scope.index == PIGEN_INVALID_ID)
		return fail(resolver, syntax_node->span, "cannot create module scope");
	if (model->module_count == PIGEN_INVALID_ID)
		pigen_fail("too many modules");
	if (model->module_count == model->module_capacity)
	{
		model->module_capacity = model->module_capacity ? model->module_capacity * 2 : 8;
		model->modules = pigen_resize(model->modules,
			model->module_capacity * sizeof(*model->modules));
	}
	module_id = (pigen_module_id){(uint32_t)model->module_count};
	model->modules[model->module_count++] = (pigen_semantic_module){
		syntax_id, symbol, scope, syntax_node->span};
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if (node->kind == PIGEN_SYNTAX_TYPEDEF &&
			!add_typedef(resolver, scope, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_TRANSPORT &&
			!add_transport(resolver, module_id, node, child)) return 0;
		child = node->next_sibling;
	}
	return 1;
}

int pigen_resolve_declarations(const pigen_source_manager *sources,
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_resolve_error *error)
{
	resolver resolver = {sources, syntax, model, error, INVALID_ID(pigen_type_id)};
	const pigen_syntax_node *root;
	pigen_syntax_id child;
	pigen_source_span root_span;

	pigen_semantic_init(model, sources);
	if (error) *error = (pigen_resolve_error){0};
	root = pigen_syntax_get(syntax, (pigen_syntax_id){0});
	if (!root || root->kind != PIGEN_SYNTAX_COMPILATION_UNIT)
		return 0;
	root_span = root->span;
	model->compilation_scope = pigen_scope_add(model,
		INVALID_ID(pigen_scope_id), root_span);
	if (model->compilation_scope.index == PIGEN_INVALID_ID)
		return fail(&resolver, root_span, "cannot create compilation-unit scope");
	resolver.integer_type = pigen_type_intern(model, PIGEN_TYPE_INTEGER,
		PIGEN_SIGN_SIGNED, INVALID_ID(pigen_symbol_id), NULL, 0, root_span);
	if (resolver.integer_type.index == PIGEN_INVALID_ID)
		return fail(&resolver, root_span, "cannot create integer type");
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		if (node->kind == PIGEN_SYNTAX_TYPEDEF &&
			!add_typedef(&resolver, model->compilation_scope, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_MODULE &&
			!add_module(&resolver, node, child)) return 0;
		child = node->next_sibling;
	}
	return 1;
}
