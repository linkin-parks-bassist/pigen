/* Declaration resolution from structured syntax into stable semantic IDs. */
#include <stdlib.h>

#include "pigen/expression_resolve.h"
#include "pigen/resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	pigen_resolve_error *error;
} resolver;

static int fail(resolver *resolver, pigen_source_span span, const char *message)
{
	if (resolver->error)
	{
		resolver->error->origin = INVALID_ID(pigen_origin_id);
		resolver->error->span = span;
		resolver->error->message = message;
	}
	return 0;
}

static pigen_source_span token_spelling(const resolver *resolver,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	return known ? pigen_origin_spelling_span(resolver->syntax->expanded,
		known->origin) : (pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static pigen_source_span token_expansion(const resolver *resolver,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	return known ? pigen_origin_expansion_span(resolver->syntax->expanded,
		known->origin) : (pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static int fail_token(resolver *resolver, pigen_token_id token,
	const char *message)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	if (resolver->error)
	{
		resolver->error->origin = known ? known->origin :
			INVALID_ID(pigen_origin_id);
		resolver->error->span = token_expansion(resolver, token);
		resolver->error->message = message;
	}
	return 0;
}

static int fail_location(resolver *resolver, pigen_syntax_location location,
	const char *message)
{
	if (resolver->error)
	{
		resolver->error->origin = location.origin;
		resolver->error->span = location.source_span;
		resolver->error->message = message;
	}
	return 0;
}

static pigen_expr_id resolve_constant(resolver *resolver, pigen_scope_id scope,
	pigen_syntax_expr_id syntax_id)
{
	return pigen_resolve_constant_expression(resolver->syntax, resolver->model,
		scope, syntax_id);
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
			token_spelling(resolver, syntax_type->base_name));
		symbol = pigen_symbol_get(resolver->model, named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
		{
			fail_token(resolver, syntax_type->base_name, "unknown type name");
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
			pigen_expr_id left = resolve_constant(resolver, scope,
				syntax_dimensions[i].left);
			pigen_expr_id right = resolve_constant(resolver, scope,
				syntax_dimensions[i].right);
			if (left.index == PIGEN_INVALID_ID ||
				right.index == PIGEN_INVALID_ID)
			{
				fail_location(resolver, syntax_dimensions[i].location,
					"packed bounds require constant expressions");
				free(dimensions);
				return INVALID_ID(pigen_type_id);
			}
			dimensions[i].left = pigen_expr_constant(resolver->model, left);
			dimensions[i].right = pigen_expr_constant(resolver->model, right);
		}
	}
	result = pigen_type_intern(resolver->model, kind, signedness,
		named_symbol, dimensions, syntax_type->dimension_count);
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

static int add_parameter(resolver *resolver, pigen_module_id module_id,
	pigen_syntax_id syntax_id, const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_expr_id value;
	const pigen_semantic_expr *expression;
	pigen_symbol_id symbol;
	pigen_parameter_id parameter;
	pigen_declare_result declared;

	value = resolve_constant(resolver, module->scope,
		syntax_node->as.parameter.value);
	expression = pigen_expr_get(model, value);
	if (!expression)
		return fail_location(resolver, syntax_node->location,
			"parameter value requires a supported constant integer expression");
	declared = pigen_symbol_declare(model, module->scope,
		PIGEN_SYMBOL_PARAMETER, expression->type,
		token_spelling(resolver, syntax_node->as.parameter.name),
		syntax_node->location.source_span, &symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail_token(resolver, syntax_node->as.parameter.name,
			"duplicate parameter name");
	if (declared != PIGEN_DECLARE_OK)
		return fail_location(resolver, syntax_node->location,
			"invalid parameter declaration");
	parameter = pigen_parameter_add(model, syntax_id, module_id, symbol, value,
		syntax_node->as.parameter.is_local, syntax_node->location.source_span);
	if (parameter.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid parameter semantic object");
	return 1;
}

static int add_transport_declaration(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_type_id payload_type;
	pigen_expr_id depth = INVALID_ID(pigen_expr_id);
	pigen_syntax_id declarator_id;

	if (syntax_node->as.transport_declaration.direction == PIGEN_DIRECTION_INOUT)
		return fail_location(resolver, syntax_node->location,
			"transport ports must be input or output, not inout");
	payload_type = resolve_type(resolver, module->scope,
		&syntax_node->as.transport_declaration.payload);
	if (payload_type.index == PIGEN_INVALID_ID) return 0;
	if (syntax_node->as.transport_declaration.kind == PIGEN_TRANSPORT_FIFO)
	{
		const pigen_syntax_expr *depth_syntax = pigen_syntax_expr_get(
			&resolver->syntax->expressions,
			syntax_node->as.transport_declaration.fifo_depth);
		depth = resolve_constant(resolver, module->scope,
			syntax_node->as.transport_declaration.fifo_depth);
		if (depth.index == PIGEN_INVALID_ID)
			return fail_location(resolver, depth_syntax ? depth_syntax->location :
				syntax_node->location,
				"fifo depth requires a constant expression");
	}
	for (declarator_id = syntax_node->first_child;
		declarator_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax,
			declarator_id);
		pigen_symbol_id symbol;
		pigen_transport_id transport;
		pigen_declare_result declared;

		if (!declarator ||
			declarator->kind != PIGEN_SYNTAX_TRANSPORT_DECLARATOR)
			return fail_location(resolver, syntax_node->location,
				"invalid transport declarator");
		declared = pigen_symbol_declare(model, module->scope,
			PIGEN_SYMBOL_TRANSPORT, payload_type,
			token_spelling(resolver,
				declarator->as.transport_declarator.name),
			syntax_node->location.source_span,
			&symbol, NULL);
		if (declared == PIGEN_DECLARE_DUPLICATE)
			return fail_token(resolver,
				declarator->as.transport_declarator.name,
				"duplicate module declaration");
		if (declared != PIGEN_DECLARE_OK)
			return fail_location(resolver, syntax_node->location,
				"invalid transport declaration");
		transport = pigen_transport_add(model, declarator_id, module_id, symbol,
			payload_type, depth,
			semantic_transport_kind(
				syntax_node->as.transport_declaration.kind),
			semantic_direction(
				syntax_node->as.transport_declaration.direction),
			syntax_node->location.source_span);
		if (transport.index == PIGEN_INVALID_ID)
			return fail_location(resolver, declarator->location,
				"invalid transport semantic object");
		declarator_id = declarator->next_sibling;
	}
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
		underlying, token_spelling(resolver,
			syntax_node->as.type_definition.name),
		syntax_node->location.source_span,
		&symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail_token(resolver, syntax_node->as.type_definition.name,
			"duplicate typedef name");
	if (declared != PIGEN_DECLARE_OK)
		return fail_location(resolver, syntax_node->location,
			"invalid typedef declaration");
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
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_type_id),
		token_spelling(resolver, syntax_node->as.module.name),
		syntax_node->location.source_span, &symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail_token(resolver, syntax_node->as.module.name,
			"duplicate module name");
	if (declared != PIGEN_DECLARE_OK)
		return fail_location(resolver, syntax_node->location,
			"invalid module declaration");
	scope = pigen_scope_add(model, model->compilation_scope,
		syntax_node->location.source_span);
	if (scope.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"cannot create module scope");
	module_id = pigen_module_add(model, syntax_id, symbol, scope,
		syntax_node->location.source_span);
	if (module_id.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid module semantic object");
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if (node->kind == PIGEN_SYNTAX_PARAMETER &&
			!add_parameter(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_TYPEDEF &&
			!add_typedef(resolver, scope, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_TRANSPORT_DECLARATION &&
			!add_transport_declaration(resolver, module_id, node)) return 0;
		child = node->next_sibling;
	}
	return 1;
}

int pigen_resolve_declarations(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,
	pigen_resolve_error *error)
{
	const pigen_source_manager *sources = syntax && syntax->expanded ?
		syntax->expanded->sources : NULL;
	resolver resolver = {syntax, model, error};
	const pigen_syntax_node *root;
	pigen_syntax_id child;
	pigen_source_span root_span;

	pigen_semantic_init(model, sources);
	if (error) *error = (pigen_resolve_error){INVALID_ID(pigen_origin_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0}, NULL};
	root = pigen_syntax_get(syntax, (pigen_syntax_id){0});
	if (!root || root->kind != PIGEN_SYNTAX_COMPILATION_UNIT)
		return 0;
	root_span = root->location.source_span;
	model->compilation_scope = pigen_scope_add(model,
		INVALID_ID(pigen_scope_id), root_span);
	if (model->compilation_scope.index == PIGEN_INVALID_ID)
		return fail(&resolver, root_span, "cannot create compilation-unit scope");
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
