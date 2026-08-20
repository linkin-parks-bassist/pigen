/* Resolution from structured syntax into stable semantic identities. */
#include <stdlib.h>

#include "pigen/expression_resolve.h"
#include "pigen/expression_use.h"
#include "pigen/predicate.h"
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

static pigen_semantic_value_storage semantic_value_storage(
	pigen_syntax_value_storage storage)
{
	switch (storage)
	{
		case PIGEN_VALUE_NET: return PIGEN_SEMANTIC_VALUE_NET;
		case PIGEN_VALUE_VARIABLE: return PIGEN_SEMANTIC_VALUE_VARIABLE;
	}
	pigen_fail("invalid syntax value storage");
	return PIGEN_SEMANTIC_VALUE_NET;
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

static int add_value_declaration(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_type_id type = resolve_type(resolver, module->scope,
		&syntax_node->as.value_declaration.type);
	pigen_syntax_id declarator_id;

	if (type.index == PIGEN_INVALID_ID) return 0;
	for (declarator_id = syntax_node->first_child;
		declarator_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax,
			declarator_id);
		pigen_symbol_id symbol;
		pigen_value_id value;
		pigen_declare_result declared;

		if (!declarator || declarator->kind != PIGEN_SYNTAX_VALUE_DECLARATOR)
			return fail_location(resolver, syntax_node->location,
				"invalid value declarator");
		declared = pigen_symbol_declare(model, module->scope,
			PIGEN_SYMBOL_VALUE, type,
			token_spelling(resolver, declarator->as.value_declarator.name),
			syntax_node->location.source_span, &symbol, NULL);
		if (declared == PIGEN_DECLARE_DUPLICATE)
			return fail_token(resolver, declarator->as.value_declarator.name,
				"duplicate module declaration");
		if (declared != PIGEN_DECLARE_OK)
			return fail_location(resolver, syntax_node->location,
				"invalid value declaration");
		value = pigen_value_add(model, declarator_id, module_id, symbol, type,
			semantic_value_storage(syntax_node->as.value_declaration.storage),
			semantic_direction(syntax_node->as.value_declaration.direction),
			syntax_node->location.source_span);
		if (value.index == PIGEN_INVALID_ID)
			return fail_location(resolver, declarator->location,
				"invalid value semantic object");
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

static int lvalue_is_assignable(const pigen_semantic_model *model,
	pigen_lvalue_id lvalue_id)
{
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model, lvalue_id);
	size_t i;

	if (!lvalue) return 0;
	if (lvalue->kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children = pigen_lvalue_children(model,
			lvalue->as.sequence.first_child, lvalue->as.sequence.child_count);
		if (!children) return 0;
		for (i = 0; i < lvalue->as.sequence.child_count; i++)
			if (!lvalue_is_assignable(model, children[i])) return 0;
		return 1;
	}
	if (lvalue->as.projection.transport.index != PIGEN_INVALID_ID)
	{
		const pigen_semantic_transport *transport = pigen_transport_get(model,
			lvalue->as.projection.transport);
		return transport && transport->direction != PIGEN_SEMANTIC_INPUT &&
			transport->direction != PIGEN_SEMANTIC_INOUT;
	}
	else
	{
		pigen_value_id value_id = pigen_symbol_value(model,
			lvalue->as.projection.base_symbol);
		const pigen_semantic_value *value = pigen_value_get(model, value_id);
		return value && value->storage == PIGEN_SEMANTIC_VALUE_VARIABLE &&
			value->direction != PIGEN_SEMANTIC_INPUT &&
			value->direction != PIGEN_SEMANTIC_INOUT;
	}
}

static int bind_transfer_domain(resolver *resolver, pigen_lvalue_id destination,
	pigen_expr_id value, pigen_predicate_id guard,
	pigen_clock_domain_id domain, pigen_source_span span)
{
	pigen_expression_use_analysis destination_uses = {0};
	pigen_expression_use_analysis value_uses = {0};
	size_t i;
	size_t j;
	int valid = pigen_analyze_lvalue_uses(resolver->model, destination, guard,
		&destination_uses) &&
		pigen_analyze_expression_uses(resolver->model, value, guard,
			PIGEN_EXPRESSION_USE_READ, &value_uses);

	if (!valid)
		fail(resolver, span, "cannot analyze transfer uses");
	for (i = 0; valid && i < destination_uses.transport_count; i++)
		if (!pigen_transport_bind_domain(resolver->model,
			destination_uses.transports[i].transport, domain))
		{
			fail(resolver, span, "transport used across clock domains");
			valid = 0;
		}
	for (i = 0; valid && i < value_uses.transport_count; i++)
		if (!pigen_transport_bind_domain(resolver->model,
			value_uses.transports[i].transport, domain))
		{
			fail(resolver, span, "transport used across clock domains");
			valid = 0;
		}
	for (i = 0; valid && i < destination_uses.transport_count; i++)
	{
		if (!(destination_uses.transports[i].contexts &
			PIGEN_EXPRESSION_USE_LVALUE)) continue;
		if (destination_uses.transports[i].contexts &
			PIGEN_EXPRESSION_USE_INDEX)
		{
			fail(resolver, span, "buffered transfer cannot source its destination");
			valid = 0;
			break;
		}
		for (j = 0; j < value_uses.transport_count; j++)
			if (destination_uses.transports[i].transport.index ==
				value_uses.transports[j].transport.index)
			{
				fail(resolver, span,
					"buffered transfer cannot source its destination");
				valid = 0;
				break;
			}
	}
	pigen_free_expression_use_analysis(&value_uses);
	pigen_free_expression_use_analysis(&destination_uses);
	return valid;
}

static int add_clocked_process(resolver *resolver,
	pigen_module_id module_id, pigen_syntax_id syntax_id,
	const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_expr_id clock = pigen_resolve_expression(resolver->syntax, model,
		module->scope, syntax_node->as.clocked_process.clock);
	const pigen_semantic_expr *clock_expression = pigen_expr_get(model, clock);
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_predicate_id guard;
	pigen_syntax_id block_id = syntax_node->first_child;
	const pigen_syntax_node *block = pigen_syntax_get(resolver->syntax, block_id);
	pigen_syntax_id assignment_id;

	if (!clock_expression || clock_expression->kind != PIGEN_EXPR_SYMBOL ||
		pigen_symbol_value(model, clock_expression->as.symbol).index ==
			PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"clock edge requires one ordinary value name");
	domain = pigen_clock_domain_intern(model, clock_expression->as.symbol,
		syntax_node->as.clocked_process.edge == PIGEN_EDGE_POSEDGE ?
			PIGEN_SEMANTIC_POSEDGE : PIGEN_SEMANTIC_NEGEDGE);
	if (domain.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid clock domain");
	process = pigen_process_add(model, syntax_id, module_id, domain, clock,
		syntax_node->location.source_span);
	if (process.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid clocked process");
	if (!block || block->kind != PIGEN_SYNTAX_PROCEDURAL_BLOCK ||
		block->next_sibling.index != PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid procedural block");
	guard = pigen_predicate_true(model);
	for (assignment_id = block->first_child;
		assignment_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *assignment = pigen_syntax_get(resolver->syntax,
			assignment_id);
		pigen_expr_id destination_expression;
		pigen_lvalue_id destination;
		pigen_expr_id value;
		pigen_transfer_id transfer;

		if (!assignment ||
			assignment->kind != PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
			return fail_location(resolver, block->location,
				"invalid procedural statement");
		destination_expression = pigen_resolve_expression(resolver->syntax,
			model, module->scope,
			assignment->as.nonblocking_assignment.destination);
		destination = pigen_lvalue_resolve(model, destination_expression);
		if (destination.index == PIGEN_INVALID_ID)
			return fail_location(resolver, assignment->location,
				"transfer destination requires a supported lvalue");
		value = pigen_resolve_expression(resolver->syntax, model, module->scope,
			assignment->as.nonblocking_assignment.value);
		if (value.index == PIGEN_INVALID_ID)
			return fail_location(resolver, assignment->location,
				"transfer value requires a supported expression");
		if (!lvalue_is_assignable(model, destination))
			return fail_location(resolver, assignment->location,
				"transfer destination is not a writable variable or transport");
		if (!bind_transfer_domain(resolver, destination, value, guard, domain,
			assignment->location.source_span)) return 0;
		transfer = pigen_transfer_add(model, assignment_id, module_id, process,
			destination, value, guard, domain, assignment->location.source_span);
		if (transfer.index == PIGEN_INVALID_ID)
			return fail_location(resolver, assignment->location,
				"invalid transfer semantic object");
		assignment_id = assignment->next_sibling;
	}
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
		if (node->kind == PIGEN_SYNTAX_VALUE_DECLARATION &&
			!add_value_declaration(resolver, module_id, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_TRANSPORT_DECLARATION &&
			!add_transport_declaration(resolver, module_id, node)) return 0;
		child = node->next_sibling;
	}
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS &&
			!add_clocked_process(resolver, module_id, child, node)) return 0;
		child = node->next_sibling;
	}
	return 1;
}

int pigen_resolve_semantics(const pigen_syntax_tree *syntax,
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
