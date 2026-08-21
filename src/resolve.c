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

static pigen_transfer_type semantic_transfer_type(
	pigen_transfer_type transfer_type)
{
	if (transfer_type < PIGEN_TRANSFER_TYPE_BUF ||
		transfer_type > PIGEN_TRANSFER_TYPE_SKID)
		pigen_fail("invalid transfer type for this signal");
	return transfer_type;
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
	pigen_fail("invalid syntax signal direction");
	return PIGEN_SEMANTIC_INTERNAL;
}

static pigen_transfer_type semantic_static_transfer_type(
	pigen_transfer_type transfer_type)
{
	if (transfer_type != PIGEN_TRANSFER_TYPE_WIRE &&
		transfer_type != PIGEN_TRANSFER_TYPE_REG &&
		transfer_type != PIGEN_TRANSFER_TYPE_LOGIC)
		pigen_fail("invalid static transfer type");
	return transfer_type;
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

static int add_signal_declaration(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_type_id data_type;
	pigen_expr_id depth = INVALID_ID(pigen_expr_id);
	pigen_syntax_id declarator_id;

	if (syntax_node->as.signal_declaration.direction == PIGEN_DIRECTION_INOUT)
		return fail_location(resolver, syntax_node->location,
			"signal ports must be input or output, not inout");
	data_type = resolve_type(resolver, module->scope,
		&syntax_node->as.signal_declaration.payload);
	if (data_type.index == PIGEN_INVALID_ID) return 0;
	if (syntax_node->as.signal_declaration.transfer_type == PIGEN_TRANSFER_TYPE_FIFO)
	{
		const pigen_syntax_expr *depth_syntax = pigen_syntax_expr_get(
			&resolver->syntax->expressions,
			syntax_node->as.signal_declaration.fifo_depth);
		depth = resolve_constant(resolver, module->scope,
			syntax_node->as.signal_declaration.fifo_depth);
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
		pigen_signal_id signal;
		pigen_declare_result declared;

		if (!declarator ||
			declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declarator");
		declared = pigen_symbol_declare(model, module->scope,
			PIGEN_SYMBOL_SIGNAL, data_type,
			token_spelling(resolver,
				declarator->as.signal_declarator.name),
			syntax_node->location.source_span,
			&symbol, NULL);
		if (declared == PIGEN_DECLARE_DUPLICATE)
			return fail_token(resolver,
				declarator->as.signal_declarator.name,
				"duplicate module declaration");
		if (declared != PIGEN_DECLARE_OK)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declaration");
		signal = pigen_signal_add(model, declarator_id, module_id, symbol,
			data_type, pigen_semantic_scalar_shape(model), depth,
			semantic_transfer_type(
				syntax_node->as.signal_declaration.transfer_type),
			semantic_direction(
				syntax_node->as.signal_declaration.direction),
			syntax_node->location.source_span);
		if (signal.index == PIGEN_INVALID_ID)
			return fail_location(resolver, declarator->location,
				"invalid signal semantic object");
		declarator_id = declarator->next_sibling;
	}
	return 1;
}

static int add_static_signal_declaration(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_type_id type = resolve_type(resolver, module->scope,
		&syntax_node->as.static_signal_declaration.type);
	pigen_syntax_id declarator_id;

	if (type.index == PIGEN_INVALID_ID) return 0;
	for (declarator_id = syntax_node->first_child;
		declarator_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax,
			declarator_id);
		pigen_symbol_id symbol;
		pigen_signal_id signal;
		pigen_declare_result declared;

		if (!declarator || declarator->kind != PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATOR)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declarator");
		declared = pigen_symbol_declare(model, module->scope,
			PIGEN_SYMBOL_SIGNAL, type,
			token_spelling(resolver, declarator->as.static_signal_declarator.name),
			syntax_node->location.source_span, &symbol, NULL);
		if (declared == PIGEN_DECLARE_DUPLICATE)
			return fail_token(resolver, declarator->as.static_signal_declarator.name,
				"duplicate module declaration");
		if (declared != PIGEN_DECLARE_OK)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declaration");
		signal = pigen_signal_add(model, declarator_id, module_id, symbol, type,
			pigen_semantic_scalar_shape(model), INVALID_ID(pigen_expr_id),
			semantic_static_transfer_type(syntax_node->as.static_signal_declaration.transfer_type),
			semantic_direction(syntax_node->as.static_signal_declaration.direction),
			syntax_node->location.source_span);
		if (signal.index == PIGEN_INVALID_ID)
			return fail_location(resolver, declarator->location,
				"invalid signal semantic object");
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
	{
		const pigen_semantic_signal *signal = pigen_signal_get(model,
			lvalue->as.projection.signal);
		return signal && signal->transfer_type != PIGEN_TRANSFER_TYPE_WIRE &&
			signal->direction != PIGEN_SEMANTIC_INPUT &&
			signal->direction != PIGEN_SEMANTIC_INOUT;
	}
}

static void add_transfer_signal_use(
	pigen_transfer_signal_use **uses, size_t *count, size_t *capacity,
	pigen_signal_id signal, unsigned roles)
{
	size_t i;
	for (i = 0; i < *count; i++)
		if ((*uses)[i].signal.index == signal.index)
		{
			(*uses)[i].roles |= roles;
			return;
		}
	if (*count == *capacity)
	{
		*capacity = *capacity ? *capacity * 2 : 4;
		*uses = pigen_resize(*uses, *capacity * sizeof(**uses));
	}
	(*uses)[(*count)++] = (pigen_transfer_signal_use){signal, roles};
}

static unsigned transfer_roles_for_context(const pigen_semantic_model *model,
	pigen_signal_id signal_id, unsigned contexts)
{
	const pigen_semantic_signal *signal = pigen_signal_get(model, signal_id);
	const pigen_transfer_type_laws *laws = signal ?
		pigen_transfer_type_get(signal->transfer_type) : NULL;
	unsigned roles = 0;

	if (!laws) return 0;
	if (contexts & PIGEN_EXPRESSION_USE_LVALUE)
	{
		roles |= PIGEN_TRANSFER_SIGNAL_WRITE;
		if (laws->produces_on_write) roles |= PIGEN_TRANSFER_PRODUCER;
	}
	if (contexts & (PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_INDEX))
	{
		roles |= PIGEN_TRANSFER_SIGNAL_READ;
		if (laws->consumes_on_read) roles |= PIGEN_TRANSFER_CONSUMER;
	}
	return roles;
}

static int analyze_transfer(resolver *resolver, pigen_lvalue_id destination,
	pigen_expr_id value, pigen_predicate_id guard,
	pigen_clock_domain_id domain, pigen_source_span span,
	pigen_transfer_signal_use **signal_uses,
	size_t *signal_use_count)
{
	pigen_expression_use_analysis destination_uses = {0};
	pigen_expression_use_analysis value_uses = {0};
	pigen_expression_use_analysis guard_uses = {0};
	const pigen_predicate *predicate = pigen_predicate_get(resolver->model,
		guard);
	const pigen_predicate_atom *atoms = pigen_predicate_atoms(resolver->model,
		guard);
	size_t i;
	size_t capacity = 0;
	int valid = predicate && pigen_analyze_lvalue_uses(resolver->model,
		destination, guard,
		&destination_uses) &&
		pigen_analyze_expression_uses(resolver->model, value, guard,
			PIGEN_EXPRESSION_USE_READ, &value_uses);
	for (i = 0; valid && i < predicate->atom_count; i++)
		valid = pigen_analyze_expression_uses(resolver->model,
			atoms[i].condition, pigen_predicate_true(resolver->model),
			PIGEN_EXPRESSION_USE_READ, &guard_uses);

	if (!valid)
		fail(resolver, span, "cannot analyze transfer uses");
	for (i = 0; valid && i < destination_uses.signal_count; i++)
	{
		unsigned roles = transfer_roles_for_context(resolver->model,
			destination_uses.signals[i].signal,
			destination_uses.signals[i].contexts);
		if (roles) add_transfer_signal_use(signal_uses,
			signal_use_count, &capacity,
			destination_uses.signals[i].signal, roles);
	}
	for (i = 0; valid && i < value_uses.signal_count; i++)
		add_transfer_signal_use(signal_uses, signal_use_count,
			&capacity, value_uses.signals[i].signal,
			transfer_roles_for_context(resolver->model,
				value_uses.signals[i].signal,
				value_uses.signals[i].contexts));
	for (i = 0; valid && i < guard_uses.signal_count; i++)
		add_transfer_signal_use(signal_uses, signal_use_count,
			&capacity, guard_uses.signals[i].signal,
			transfer_roles_for_context(resolver->model,
				guard_uses.signals[i].signal,
				guard_uses.signals[i].contexts));
	for (i = 0; valid && i < *signal_use_count; i++)
		if (((*signal_uses)[i].roles &
			(PIGEN_TRANSFER_CONSUMER | PIGEN_TRANSFER_PRODUCER))
			== (PIGEN_TRANSFER_CONSUMER | PIGEN_TRANSFER_PRODUCER))
		{
			fail(resolver, span, "buffered transfer cannot source its destination");
			valid = 0;
		}
	for (i = 0; valid && i < *signal_use_count; i++)
		if (!pigen_signal_bind_domain(resolver->model,
			(*signal_uses)[i].signal, domain))
		{
			fail(resolver, span, "signal used across clock domains");
			valid = 0;
		}
	pigen_free_expression_use_analysis(&value_uses);
	pigen_free_expression_use_analysis(&destination_uses);
	pigen_free_expression_use_analysis(&guard_uses);
	if (!valid)
	{
		free(*signal_uses);
		*signal_uses = NULL;
		*signal_use_count = 0;
	}
	return valid;
}

static int resolve_procedural_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id statement_id,
	pigen_predicate_id guard);

static int resolve_assignment(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id assignment_id, const pigen_syntax_node *assignment,
	pigen_predicate_id guard)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_expr_id destination_expression = pigen_resolve_expression(
		resolver->syntax, model, module->scope,
		assignment->as.nonblocking_assignment.destination);
	pigen_lvalue_id destination = pigen_lvalue_resolve(model,
		destination_expression);
	pigen_expr_id value;
	pigen_transfer_id transfer;
	pigen_transfer_signal_use *signal_uses = NULL;
	size_t signal_use_count = 0;

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
			"transfer destination is not a writable variable or signal");
	if (!analyze_transfer(resolver, destination, value, guard, domain,
		assignment->location.source_span, &signal_uses,
		&signal_use_count)) return 0;
	transfer = pigen_transfer_add(model, assignment_id, module_id, process,
		destination, value, guard, domain, signal_uses,
		signal_use_count, assignment->location.source_span);
	free(signal_uses);
	if (transfer.index == PIGEN_INVALID_ID)
		return fail_location(resolver, assignment->location,
			"invalid transfer semantic object");
	return 1;
}

static int resolve_if_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, const pigen_syntax_node *statement,
	pigen_predicate_id guard)
{
	const pigen_semantic_module *module = pigen_module_get(resolver->model,
		module_id);
	pigen_expr_id condition = pigen_resolve_expression(resolver->syntax,
		resolver->model, module->scope, statement->as.if_statement.condition);
	pigen_predicate_id then_guard;
	pigen_predicate_id else_guard;
	pigen_syntax_id then_id = statement->first_child;
	const pigen_syntax_node *then_statement = pigen_syntax_get(resolver->syntax,
		then_id);
	pigen_syntax_id else_id;

	if (condition.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"if condition requires a supported integral expression");
	if (!then_statement)
		return fail_location(resolver, statement->location,
			"if statement requires a then branch");
	else_id = then_statement->next_sibling;
	if ((statement->as.if_statement.has_else &&
		else_id.index == PIGEN_INVALID_ID) ||
		(!statement->as.if_statement.has_else &&
		else_id.index != PIGEN_INVALID_ID))
		return fail_location(resolver, statement->location,
			"invalid if branch structure");
	if (else_id.index != PIGEN_INVALID_ID &&
		pigen_syntax_get(resolver->syntax, else_id)->next_sibling.index !=
			PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid if branch structure");
	then_guard = pigen_predicate_and_condition(resolver->model, guard,
		condition, 1);
	else_guard = pigen_predicate_and_condition(resolver->model, guard,
		condition, 0);
	if (then_guard.index == PIGEN_INVALID_ID ||
		else_guard.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid if condition predicate");
	if (!resolve_procedural_statement(resolver, module_id, process, domain,
		then_id, then_guard)) return 0;
	return else_id.index == PIGEN_INVALID_ID ||
		resolve_procedural_statement(resolver, module_id, process, domain,
			else_id, else_guard);
}

static int resolve_procedural_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id statement_id,
	pigen_predicate_id guard)
{
	const pigen_syntax_node *statement = pigen_syntax_get(resolver->syntax,
		statement_id);
	pigen_syntax_id child;

	if (!statement) return 0;
	if (statement->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
		return resolve_assignment(resolver, module_id, process, domain,
			statement_id, statement, guard);
	if (statement->kind == PIGEN_SYNTAX_IF_STATEMENT)
		return resolve_if_statement(resolver, module_id, process, domain,
			statement, guard);
	if (statement->kind != PIGEN_SYNTAX_PROCEDURAL_BLOCK)
		return fail_location(resolver, statement->location,
			"invalid procedural statement");
	for (child = statement->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *known = pigen_syntax_get(resolver->syntax, child);
		if (!resolve_procedural_statement(resolver, module_id, process, domain,
			child, guard)) return 0;
		child = known->next_sibling;
	}
	return 1;
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
	pigen_syntax_id body_id = syntax_node->first_child;
	const pigen_syntax_node *body = pigen_syntax_get(resolver->syntax, body_id);

	if (!clock_expression || clock_expression->kind != PIGEN_EXPR_SYMBOL ||
		pigen_symbol_signal(model, clock_expression->as.symbol).index ==
			PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"clock edge requires one static signal name");
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
	if (!body || body->next_sibling.index != PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid procedural body");
	return resolve_procedural_statement(resolver, module_id, process, domain,
		body_id, pigen_predicate_true(model));
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
		if (node->kind == PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATION &&
			!add_static_signal_declaration(resolver, module_id, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			!add_signal_declaration(resolver, module_id, node)) return 0;
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

static int validate_signal_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	size_t later_index;

	for (later_index = 0; later_index < model->transfer_count; later_index++)
	{
		const pigen_semantic_transfer *later = pigen_transfer_get(model,
			(pigen_transfer_id){(uint32_t)later_index});
		const pigen_transfer_signal_use *later_uses =
			pigen_transfer_signal_uses(model,
				(pigen_transfer_id){(uint32_t)later_index});
		size_t earlier_index;
		size_t i;

		for (earlier_index = 0; earlier_index < later_index; earlier_index++)
		{
			const pigen_semantic_transfer *earlier = pigen_transfer_get(model,
				(pigen_transfer_id){(uint32_t)earlier_index});
			const pigen_transfer_signal_use *earlier_uses =
				pigen_transfer_signal_uses(model,
					(pigen_transfer_id){(uint32_t)earlier_index});
			size_t j;

			if (pigen_predicates_mutually_exclusive(model, earlier->guard,
				later->guard)) continue;
			for (i = 0; i < later->signal_use_count; i++)
				for (j = 0; j < earlier->signal_use_count; j++)
				{
					const pigen_semantic_signal *signal;
					const pigen_transfer_type_laws *laws;
					unsigned overlap;
					if (later_uses[i].signal.index !=
						earlier_uses[j].signal.index) continue;
					signal = pigen_signal_get(model, later_uses[i].signal);
					laws = signal ? pigen_transfer_type_get(
						signal->transfer_type) : NULL;
					if (!laws || !laws->requires_ownership) continue;
					overlap = later_uses[i].roles & earlier_uses[j].roles;
					if (overlap & PIGEN_TRANSFER_CONSUMER)
						return fail(resolver, later->span,
							"signal has nonexclusive consumers");
					if (overlap & PIGEN_TRANSFER_PRODUCER)
						return fail(resolver, later->span,
							"signal has nonexclusive producers");
				}
		}
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
	return validate_signal_ownership(&resolver);
}
