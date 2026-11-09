/* Resolution from structured syntax into stable semantic identities. */
#include <stdlib.h>
#include <string.h>

#include "pigen/expression_resolve.h"
#include "pigen/expression_use.h"
#include "pigen/predicate.h"
#include "pigen/resolve.h"
#include "pigen/type_resolve.h"
#include "pigen/pipeline_identity.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	const pigen_resolve_policy *policy;
	pigen_semantic_error *error;
	/* Expression scope for the current statement. Normally the module scope;
	 * a pipeline field/stage scope while resolving a pipeline body. */
	pigen_scope_id expression_scope;
	/* FSM context while resolving state bodies and gotos. */
	pigen_expr_id fsm_state_expression;
	pigen_data_type_id fsm_state_type;
	const pigen_source_span *fsm_state_names;
	size_t fsm_state_count;
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
		scope, syntax_id, PIGEN_LITERAL_DOMAIN_SYSTEMVERILOG, resolver->policy,
		resolver->error);
}

static pigen_shape_id resolve_shape(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_node *declarator)
{
	const pigen_syntax_signal_declarator *syntax_declarator;
	const pigen_syntax_shape_dimension *syntax_dimensions;
	pigen_shape_dimension *dimensions;
	pigen_shape_id result;
	size_t i;

	if (declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
		return INVALID_ID(pigen_shape_id);
	syntax_declarator = &declarator->as.signal_declarator;
	if (!syntax_declarator->dimension_count)
		return pigen_semantic_scalar_shape(resolver->model);
	syntax_dimensions = pigen_syntax_declarator_shape_dimensions(
		resolver->syntax, declarator);
	if (!syntax_dimensions) return INVALID_ID(pigen_shape_id);
	dimensions = pigen_resize(NULL,
		syntax_declarator->dimension_count * sizeof(*dimensions));
	for (i = 0; i < syntax_declarator->dimension_count; i++)
	{
		pigen_expr_id left;
		pigen_expr_id right = INVALID_ID(pigen_expr_id);

		dimensions[i].form = syntax_dimensions[i].form ==
			PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT ?
			PIGEN_SHAPE_DIMENSION_COUNT : PIGEN_SHAPE_DIMENSION_RANGE;
		left = resolve_constant(resolver, scope,
			syntax_dimensions[i].form == PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT ?
				syntax_dimensions[i].as.count :
				syntax_dimensions[i].as.range.left);
		if (syntax_dimensions[i].form == PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE)
			right = resolve_constant(resolver, scope,
				syntax_dimensions[i].as.range.right);
		if (left.index == PIGEN_INVALID_ID ||
			(syntax_dimensions[i].form == PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE &&
			right.index == PIGEN_INVALID_ID))
		{
			free(dimensions);
			fail_location(resolver, syntax_dimensions[i].location,
				"signal dimension requires constant expressions");
			return INVALID_ID(pigen_shape_id);
		}
		if (dimensions[i].form == PIGEN_SHAPE_DIMENSION_COUNT)
		{
			dimensions[i].as.count = pigen_const_expr_normalize_count(
				resolver->model, pigen_expr_constant(resolver->model, left));
			if (dimensions[i].as.count.index == PIGEN_INVALID_ID)
			{
				free(dimensions);
				fail_location(resolver, syntax_dimensions[i].location,
					"signal dimension must be a positive count");
				return INVALID_ID(pigen_shape_id);
			}
		}
		else
		{
			dimensions[i].as.range.left =
				pigen_expr_constant(resolver->model, left);
			dimensions[i].as.range.right =
				pigen_expr_constant(resolver->model, right);
		}
	}
	result = pigen_shape_intern(resolver->model, dimensions,
		syntax_declarator->dimension_count);
	free(dimensions);
	if (result.index == PIGEN_INVALID_ID)
		fail_location(resolver, declarator->location, "invalid signal shape");
	return result;
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
	{
		pigen_data_type_id parameter_type = expression->data_type;

		if (syntax_node->as.parameter.has_type)
		{
			parameter_type = pigen_resolve_type(resolver->syntax, model,
				module->scope, syntax_node->as.parameter.data_type,
				resolver->error);
			if (parameter_type.index == PIGEN_INVALID_ID)
				return fail_location(resolver, syntax_node->location,
					"invalid parameter type");
		}
		declared = pigen_symbol_declare(model, module->scope,
			PIGEN_SYMBOL_PARAMETER, parameter_type,
			token_spelling(resolver, syntax_node->as.parameter.name),
			syntax_node->location.source_span, &symbol, NULL);
	}
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

static int resolve_transfer_argument(resolver *resolver, pigen_scope_id scope,
	const pigen_transfer_type_descriptor *descriptor,
	const pigen_syntax_transfer_type_occurrence *occurrence,
	pigen_expr_id *argument)
{
	const pigen_syntax_expr *syntax_argument;
	pigen_const_expr_id constant;

	*argument = INVALID_ID(pigen_expr_id);
	switch (descriptor->parameter)
	{
		case PIGEN_TRANSFER_PARAMETER_NONE:
			if (occurrence->argument.index != PIGEN_INVALID_ID)
				return fail_location(resolver, occurrence->location,
					"transfer type does not accept an argument");
			return 1;
		case PIGEN_TRANSFER_PARAMETER_DEPTH:
			if (occurrence->argument.index == PIGEN_INVALID_ID)
				return fail_location(resolver, occurrence->location,
					"transfer type requires a depth argument");
			syntax_argument = pigen_syntax_expr_get(&resolver->syntax->expressions,
				occurrence->argument);
			if (!syntax_argument)
				return fail_location(resolver, occurrence->location,
					"transfer type requires a depth argument");
			*argument = pigen_resolve_constant_expression(resolver->syntax,
				resolver->model, scope, occurrence->argument,
				PIGEN_LITERAL_DOMAIN_PIGEN, resolver->policy, resolver->error);
			if (argument->index == PIGEN_INVALID_ID)
				return fail_location(resolver, syntax_argument->location,
					"transfer depth requires a constant expression");
			constant = pigen_expr_constant(resolver->model, *argument);
			if (pigen_const_expr_normalize_count(resolver->model, constant).index ==
				PIGEN_INVALID_ID)
				return fail_location(resolver, syntax_argument->location,
					"transfer depth must be a positive count");
			return 1;
	}
	return fail_location(resolver, occurrence->location,
		"unsupported transfer parameter form");
}

static int resolve_written_transfer(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_node *declaration, pigen_transfer_type *transfer_type,
	pigen_expr_id *transfer_argument)
{
	const pigen_syntax_transfer_type_occurrence *occurrence =
		&declaration->as.signal_declaration.transfer_type;
	const pigen_transfer_type_descriptor *descriptor =
		pigen_transfer_type_descriptor_get(occurrence->transfer_type);

	if (!descriptor || !descriptor->is_concrete)
		return fail_location(resolver, occurrence->location,
			"signal transfer type must be concrete");
	if (declaration->as.signal_declaration.direction == PIGEN_DIRECTION_INOUT &&
		!descriptor->is_static)
		return fail_location(resolver, occurrence->location,
			"dynamic transfer type cannot be inout");
	if (!resolve_transfer_argument(resolver, scope, descriptor, occurrence,
		transfer_argument)) return 0;
	*transfer_type = occurrence->transfer_type;
	return 1;
}

static int resolve_omitted_transfer(resolver *resolver,
	const pigen_syntax_node *declaration, const pigen_syntax_type *syntax_type,
	pigen_data_type_id data_type, pigen_transfer_type *transfer_type)
{
	pigen_syntax_direction direction =
		declaration->as.signal_declaration.direction;
	pigen_unqualified_transfer_policy policy =
		pigen_data_type_unqualified_transfer_policy(resolver->model, data_type,
			direction == PIGEN_DIRECTION_INPUT);

	switch (policy)
	{
		case PIGEN_UNQUALIFIED_TRANSFER_ABSTRACT:
			*transfer_type = PIGEN_TRANSFER_TYPE_ABSTRACT;
			return 1;
		case PIGEN_UNQUALIFIED_TRANSFER_FORBIDDEN:
			return fail_location(resolver, syntax_type->location,
				direction == PIGEN_DIRECTION_OUTPUT ?
					"unqualified output transfer policy is unresolved" :
					"data type forbids an omitted transfer realization");
		case PIGEN_UNQUALIFIED_TRANSFER_STATIC:
			if (direction == PIGEN_DIRECTION_INPUT ||
				direction == PIGEN_DIRECTION_INOUT)
				*transfer_type = PIGEN_TRANSFER_TYPE_WIRE;
			else if (direction == PIGEN_DIRECTION_INTERNAL)
				*transfer_type = PIGEN_TRANSFER_TYPE_LOGIC;
			else if (direction == PIGEN_DIRECTION_OUTPUT)
				*transfer_type = syntax_type->base.index == PIGEN_INVALID_ID ?
					PIGEN_TRANSFER_TYPE_WIRE : PIGEN_TRANSFER_TYPE_LOGIC;
			else return fail_location(resolver, declaration->location,
				"invalid signal direction");
			return 1;
	}
	return fail_location(resolver, syntax_type->location,
		"invalid unqualified transfer policy");
}

static int add_signal_declaration_in_scope(resolver *resolver,
	pigen_module_id module_id, pigen_scope_id scope,
	const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_syntax_type *syntax_type = pigen_syntax_type_get(
		&resolver->syntax->types, syntax_node->as.signal_declaration.data_type);
	pigen_data_type_id data_type;
	pigen_transfer_type transfer_type;
	pigen_expr_id transfer_argument = INVALID_ID(pigen_expr_id);
	pigen_syntax_id declarator_id;

	if (!syntax_type)
		return fail_location(resolver, syntax_node->location,
			"invalid signal data type");
	data_type = pigen_resolve_type(resolver->syntax, model, scope,
		syntax_node->as.signal_declaration.data_type, resolver->error);
	if (data_type.index == PIGEN_INVALID_ID) return 0;
	if (syntax_node->as.signal_declaration.has_transfer_type)
	{
		if (!resolve_written_transfer(resolver, scope, syntax_node,
			&transfer_type, &transfer_argument)) return 0;
	}
	else if (!resolve_omitted_transfer(resolver, syntax_node, syntax_type,
		data_type, &transfer_type)) return 0;

	for (declarator_id = syntax_node->first_child;
		declarator_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax,
			declarator_id);
		pigen_symbol_id symbol;
		pigen_signal_id signal;
		pigen_shape_id shape;
		pigen_declare_result declared;

		if (!declarator || declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declarator");
		shape = resolve_shape(resolver, scope, declarator);
		if (shape.index == PIGEN_INVALID_ID) return 0;
		declared = pigen_symbol_declare(model, scope,
			PIGEN_SYMBOL_SIGNAL, data_type,
			token_spelling(resolver, declarator->as.signal_declarator.name),
			syntax_node->location.source_span, &symbol, NULL);
		if (declared == PIGEN_DECLARE_DUPLICATE)
			return fail_token(resolver, declarator->as.signal_declarator.name,
				"duplicate module declaration");
		if (declared != PIGEN_DECLARE_OK)
			return fail_location(resolver, syntax_node->location,
				"invalid signal declaration");
		signal = pigen_signal_add(model, declarator_id, module_id, symbol,
			data_type, shape, transfer_argument, transfer_type,
			semantic_direction(syntax_node->as.signal_declaration.direction),
			syntax_node->location.source_span);
		if (signal.index == PIGEN_INVALID_ID)
			return fail_location(resolver, declarator->location,
				"invalid signal semantic object");
		declarator_id = declarator->next_sibling;
	}
	return 1;
}

static int add_signal_declaration(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *syntax_node)
{
	const pigen_semantic_module *module = pigen_module_get(resolver->model,
		module_id);

	return module ? add_signal_declaration_in_scope(resolver, module_id,
		module->scope, syntax_node) : 0;
}

static int add_typedef(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_node *syntax_node)
{
	pigen_data_type_id underlying = pigen_resolve_type(resolver->syntax, resolver->model, scope,
		syntax_node->as.type_definition.type, resolver->error);
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
		const pigen_transfer_type_descriptor *descriptor = signal ?
			pigen_transfer_type_descriptor_get(signal->transfer_type) : NULL;
		return descriptor && descriptor->accepts_write &&
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
	const pigen_transfer_type_descriptor *descriptor = signal ?
		pigen_transfer_type_descriptor_get(signal->transfer_type) : NULL;
	unsigned roles = 0;

	if (!descriptor) return 0;
	if (contexts & PIGEN_EXPRESSION_USE_LVALUE)
	{
		roles |= PIGEN_TRANSFER_SIGNAL_WRITE;
		if (descriptor->produces_on_write) roles |= PIGEN_TRANSFER_PRODUCER;
	}
	if (contexts & (PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_INDEX))
	{
		roles |= PIGEN_TRANSFER_SIGNAL_READ;
		if (descriptor->consumes_on_read) roles |= PIGEN_TRANSFER_CONSUMER;
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

static int resolve_assignment(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id assignment_id, const pigen_syntax_node *assignment,
	pigen_predicate_id guard);

/* Resolve one pipeline statement (already parsed as field declarations, stage
 * assignments and a yield) into ordinary semantic signals and transfers. The
 * pipeline-level fields become internal buf signals in their own scope; each
 * stage's local declarations become internal static signals in a nested scope;
 * stage assignments become transfers in the enclosing process; the yield
 * writes the established pipeline output signal. The shared transfer and
 * storage lowering then produces the hardware. */
static int resolve_pipeline(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id pipeline_id, const pigen_syntax_node *pipeline,
	pigen_predicate_id guard)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_pipeline_identifier ident;
	pigen_scope_id pipeline_scope;
	pigen_scope_id saved_scope = resolver->expression_scope;
	pigen_syntax_id child;
	int ok = 1;

	if (!module || !resolver->syntax->expanded)
		return fail_location(resolver, pipeline->location,
			"pipeline requires expanded source");
	if (pigen_pipeline_identifier_establish(model, pipeline_id, module_id,
		pipeline, resolver->syntax->expanded, &ident) != 0)
		return fail_location(resolver, pipeline->location,
			"cannot establish the pipeline output identity");
	pipeline_scope = pigen_scope_add(model, module->scope,
		pipeline->location.source_span);
	if (pipeline_scope.index == PIGEN_INVALID_ID)
		return fail_location(resolver, pipeline->location,
			"cannot create the pipeline scope");

	/* Declare the pipeline fields in the pipeline scope. */
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax,
			child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			!add_signal_declaration_in_scope(resolver, module_id,
				pipeline_scope, node))
			ok = 0;
		if (!node) break;
		child = node->next_sibling;
	}
	if (!ok) return 0;

	/* Stage-local declarations live in a nested scope; stage assignments are
	 * transfers resolved in that scope. */
	for (child = pipeline->first_child; ok && child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *stage = pigen_syntax_get(resolver->syntax,
			child);
		pigen_scope_id stage_scope;
		pigen_syntax_id item;

		if (!stage) break;
		if (stage->kind != PIGEN_SYNTAX_STAGE)
		{
			child = stage->next_sibling;
			continue;
		}
		stage_scope = pigen_scope_add(model, pipeline_scope,
			stage->location.source_span);
		if (stage_scope.index == PIGEN_INVALID_ID)
			return fail_location(resolver, stage->location,
				"cannot create the pipeline stage scope");
		for (item = stage->first_child; item.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(
				resolver->syntax, item);

			if (!node) break;
			if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
				!add_signal_declaration_in_scope(resolver, module_id,
					stage_scope, node))
			{
				ok = 0;
				break;
			}
			item = node->next_sibling;
		}
		for (item = stage->first_child; ok && item.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(
				resolver->syntax, item);

			if (!node) break;
			if (node->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
			{
				resolver->expression_scope = stage_scope;
				if (!resolve_assignment(resolver, module_id, process,
					domain, item, node, guard))
				{
					ok = 0;
					break;
				}
			}
			item = node->next_sibling;
		}
		child = stage->next_sibling;
	}
	resolver->expression_scope = saved_scope;
	if (!ok) return 0;

	/* The final yield writes the established pipeline output signal. A
	 * single-field yield adopts the field's data type so the pipeline output
	 * has the same type as the field (a multi-member yield keeps the
	 * concatenation aggregate). */
	{
		pigen_expr_id destination_expression;
		pigen_lvalue_id destination;
		const pigen_semantic_expr *destination_value;
		pigen_expr_id raw_value;
		const pigen_semantic_expr *raw_expression;
		pigen_expr_id value;
		const pigen_semantic_expr *source_value;
		pigen_transfer_signal_use *signal_uses = NULL;
		size_t signal_use_count = 0;
		pigen_transfer_id transfer;

		resolver->expression_scope = pipeline_scope;
		raw_value = pigen_resolve_expression(resolver->syntax, model,
			pipeline_scope, pipeline->as.pipeline.yield, resolver->policy,
			resolver->error);
		resolver->expression_scope = saved_scope;
		raw_expression = pigen_expr_get(model, raw_value);
		if (raw_expression && raw_expression->data_type.index !=
			PIGEN_INVALID_ID)
		{
			const pigen_symbol *out_symbol = pigen_symbol_get(model,
				ident.symbol);
			pigen_semantic_signal *out_signal =
				&model->signals[ident.signal.index];

			out_signal->direction = PIGEN_SEMANTIC_INTERNAL;
			out_signal->data_type = raw_expression->data_type;
			if (out_symbol)
				model->symbols[ident.symbol.index].data_type =
					raw_expression->data_type;
			ident.data_type = raw_expression->data_type;
		}
		destination_expression = pigen_expr_add_symbol(model, ident.symbol,
			ident.data_type, ident.span);
		if (destination_expression.index == PIGEN_INVALID_ID)
			return fail_location(resolver, pipeline->location,
				"invalid pipeline output expression");
		destination = pigen_lvalue_resolve(model, destination_expression);
		destination_value = pigen_expr_get(model, destination_expression);
		if (destination.index == PIGEN_INVALID_ID || !destination_value)
			return fail_location(resolver, pipeline->location,
				"invalid pipeline output destination");
		resolver->expression_scope = pipeline_scope;
		value = pigen_resolve_assignment_value(resolver->syntax, model,
			pipeline_scope, pipeline->as.pipeline.yield,
			destination_value->data_type, resolver->policy,
			resolver->error);
		resolver->expression_scope = saved_scope;
		if (value.index == PIGEN_INVALID_ID)
			return fail_location(resolver, pipeline->location,
				"pipeline yield requires a compatible expression");
		source_value = pigen_expr_get(model, value);
		if (!source_value)
			return fail_location(resolver, pipeline->location,
				"invalid pipeline yield value");
		if (!analyze_transfer(resolver, destination, value, guard, domain,
			pipeline->location.source_span, &signal_uses,
			&signal_use_count))
			return 0;
		transfer = pigen_transfer_add(model, pipeline_id, module_id,
			process, destination, value, guard, domain, signal_uses,
			signal_use_count, pipeline->location.source_span);
		free(signal_uses);
		if (transfer.index == PIGEN_INVALID_ID)
			return fail_location(resolver, pipeline->location,
				"invalid pipeline yield transfer");
	}
	return 1;
}

static int resolve_assignment(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id assignment_id, const pigen_syntax_node *assignment,
	pigen_predicate_id guard);
static int resolve_procedural_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id statement_id,
	pigen_predicate_id guard);

/* Write the FSM state register from VALUE under GUARD (a static internal logic
 * destination transfer resolved in the module scope). */
static int add_state_transfer(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id syntax_id, pigen_expr_id value, pigen_predicate_id guard,
	pigen_source_span span)
{
	pigen_semantic_model *model = resolver->model;
	pigen_lvalue_id destination = pigen_lvalue_resolve(model,
		resolver->fsm_state_expression);
	pigen_transfer_signal_use *signal_uses = NULL;
	size_t signal_use_count = 0;
	pigen_transfer_id transfer;

	if (destination.index == PIGEN_INVALID_ID)
		return fail(resolver, span, "invalid fsm state destination");
	if (!analyze_transfer(resolver, destination, value, guard, domain, span,
		&signal_uses, &signal_use_count))
		return 0;
	transfer = pigen_transfer_add(model, syntax_id, module_id, process,
		destination, value, guard, domain, signal_uses, signal_use_count,
		span);
	free(signal_uses);
	if (transfer.index == PIGEN_INVALID_ID)
		return fail(resolver, span, "invalid fsm state transfer");
	return 1;
}

static int resolve_goto(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id syntax_id, const pigen_syntax_node *goto_node,
	pigen_predicate_id guard)
{
	pigen_semantic_model *model = resolver->model;
	pigen_source_span target = token_spelling(resolver,
		goto_node->as.goto_statement.target);
	pigen_expr_id value;
	size_t i;

	if (resolver->fsm_state_expression.index == PIGEN_INVALID_ID ||
		!resolver->fsm_state_names)
		return fail(resolver, goto_node->location.source_span,
			"goto outside an fsm");
	for (i = 0; i < resolver->fsm_state_count; i++)
	{
		const char *a;
		const char *b;
		size_t al = 0, bl = 0;

		a = pigen_source_span_text(model->sources,
			resolver->fsm_state_names[i], &al);
		b = pigen_source_span_text(model->sources, target, &bl);
		if (a && b && al == bl && !memcmp(a, b, al))
		{
			value = pigen_expr_add_integer(model, (uint64_t)i,
				resolver->fsm_state_type, goto_node->location.source_span);
			if (value.index == PIGEN_INVALID_ID)
				return fail(resolver, goto_node->location.source_span,
					"invalid fsm goto value");
			return add_state_transfer(resolver, module_id, process, domain,
				syntax_id, value, guard, goto_node->location.source_span);
		}
	}
	return fail(resolver, goto_node->location.source_span,
		"goto references an undeclared state");
}

/* Resolve one fsm statement: create the state register and its process, reset
 * it to the header initial state, and lower each state's body under a
 * state-active guard. Goto writes are handled by resolve_procedural_statement
 * through resolver->fsm_state_expression. */
static int resolve_fsm(resolver *resolver, pigen_module_id module_id,
	pigen_syntax_id syntax_id, const pigen_syntax_node *fsm_node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_expr_id clock;
	pigen_expr_id reset;
	pigen_expr_id initial;
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_data_type_id state_type;
	pigen_symbol_id state_symbol;
	pigen_signal_id state_signal;
	pigen_expr_id state_expression;
	pigen_predicate_id guard_no_reset;
	pigen_predicate_id guard_reset;
	pigen_syntax_id child;
	pigen_source_span *names = NULL;
	size_t state_count = 0;
	size_t initial_index = SIZE_MAX;
	size_t i;
	int ok = 1;

	if (!module)
		return fail(resolver, fsm_node->location.source_span,
			"fsm requires a module");
	clock = pigen_resolve_expression(resolver->syntax, model, module->scope,
		fsm_node->as.fsm.clock, resolver->policy, resolver->error);
	{
		const pigen_semantic_expr *clock_expr = pigen_expr_get(model, clock);

		if (!clock_expr || clock_expr->kind != PIGEN_EXPR_SYMBOL ||
			pigen_symbol_signal(model, clock_expr->as.symbol).index ==
				PIGEN_INVALID_ID)
			return fail(resolver, fsm_node->location.source_span,
				"fsm clock requires one static signal name");
		domain = pigen_clock_domain_intern(model, clock_expr->as.symbol,
			PIGEN_SEMANTIC_POSEDGE);
		if (domain.index == PIGEN_INVALID_ID)
			return fail(resolver, fsm_node->location.source_span,
				"invalid fsm clock domain");
	}
	reset = pigen_resolve_expression(resolver->syntax, model, module->scope,
		fsm_node->as.fsm.reset, resolver->policy, resolver->error);
	if (reset.index == PIGEN_INVALID_ID)
		return fail(resolver, fsm_node->location.source_span,
			"fsm reset requires a supported expression");

	/* Collect state names in source order. */
	for (child = fsm_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax,
			child);

		if (!node) break;
		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
			state_count++;
		child = node->next_sibling;
	}
	if (!state_count)
		return fail(resolver, fsm_node->location.source_span,
			"fsm requires at least one state");
	names = pigen_resize(NULL, state_count * sizeof(*names));
	{
		size_t at = 0;

		for (child = fsm_node->first_child; child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(
				resolver->syntax, child);

			if (!node) break;
			if (node->kind == PIGEN_SYNTAX_FSM_STATE)
				names[at++] = token_spelling(resolver,
					node->as.fsm_state.name);
			child = node->next_sibling;
		}
	}
	{
		pigen_source_span initial_span = token_spelling(resolver,
			fsm_node->as.fsm.initial);

		for (i = 0; i < state_count; i++)
		{
			size_t al = 0, bl = 0;
			const char *a = pigen_source_span_text(model->sources,
				names[i], &al);
			const char *b = pigen_source_span_text(model->sources,
				initial_span, &bl);

			if (a && b && al == bl && !memcmp(a, b, al))
			{
				initial_index = i;
				break;
			}
		}
	}
	if (initial_index == SIZE_MAX)
	{
		free(names);
		return fail(resolver, fsm_node->location.source_span,
			"fsm initial state is not declared");
	}

	{
		size_t width = 1;

		while (((size_t)1 << width) < state_count) width++;
		state_type = pigen_data_type_sized_logic(model, width,
			PIGEN_SIGN_UNSIGNED);
	}
	if (state_type.index == PIGEN_INVALID_ID)
	{
		free(names);
		return fail(resolver, fsm_node->location.source_span,
			"invalid fsm state type");
	}
	{
		pigen_source_span name = token_spelling(resolver,
			fsm_node->as.fsm.name);
		pigen_declare_result declared = pigen_symbol_declare(model,
			module->scope, PIGEN_SYMBOL_SIGNAL, state_type, name,
			fsm_node->location.source_span, &state_symbol, NULL);

		if (declared != PIGEN_DECLARE_OK)
		{
			free(names);
			return fail(resolver, fsm_node->location.source_span,
				"cannot declare the fsm state register");
		}
		state_signal = pigen_signal_add(model, syntax_id, module_id,
			state_symbol, state_type, pigen_semantic_scalar_shape(model),
			INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, fsm_node->location.source_span);
		if (state_signal.index == PIGEN_INVALID_ID)
		{
			free(names);
			return fail(resolver, fsm_node->location.source_span,
				"invalid fsm state register");
		}
		state_expression = pigen_expr_add_symbol(model, state_symbol,
			state_type, name);
		if (state_expression.index == PIGEN_INVALID_ID)
		{
			free(names);
			return fail(resolver, fsm_node->location.source_span,
				"invalid fsm state expression");
		}
	}
	resolver->fsm_state_expression = state_expression;
	resolver->fsm_state_type = state_type;
	resolver->fsm_state_names = names;
	resolver->fsm_state_count = state_count;
	process = pigen_process_add(model, syntax_id, module_id, domain, clock,
		fsm_node->location.source_span);
	if (process.index == PIGEN_INVALID_ID)
	{
		free(names);
		return fail(resolver, fsm_node->location.source_span,
			"invalid fsm process");
	}
	guard_reset = pigen_predicate_and_condition(model,
		pigen_predicate_true(model), reset, 1);
	guard_no_reset = pigen_predicate_and_condition(model,
		pigen_predicate_true(model), reset, 0);
	if (guard_reset.index == PIGEN_INVALID_ID ||
		guard_no_reset.index == PIGEN_INVALID_ID)
	{
		free(names);
		return fail(resolver, fsm_node->location.source_span,
			"invalid fsm reset predicate");
	}
	initial = pigen_expr_add_integer(model, (uint64_t)initial_index,
		state_type, fsm_node->location.source_span);
	if (initial.index == PIGEN_INVALID_ID ||
		!add_state_transfer(resolver, module_id, process, domain, syntax_id,
			initial, guard_reset, fsm_node->location.source_span))
	{
		free(names);
		return 0;
	}

	/* Resolve every state body under its state-active and no-reset guard. */
	{
		size_t index = 0;
		pigen_scope_id saved_scope = resolver->expression_scope;

		resolver->expression_scope = module->scope;
		for (child = fsm_node->first_child; ok &&
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *state = pigen_syntax_get(
				resolver->syntax, child);
			pigen_expr_id state_const;
			pigen_binary_resolution equality_resolution;
			pigen_expr_id condition;
			pigen_predicate_id state_guard;

			if (!state) break;
			if (state->kind != PIGEN_SYNTAX_FSM_STATE)
			{
				child = state->next_sibling;
				continue;
			}
			state_const = pigen_expr_add_integer(model, (uint64_t)index,
				state_type, state->location.source_span);
			if (state_const.index == PIGEN_INVALID_ID ||
				!pigen_data_type_resolve_binary_operation(model,
					PIGEN_BINARY_EQUAL, state_type, state_type,
					&equality_resolution))
			{
				ok = 0;
				break;
			}
			condition = pigen_expr_add_binary(model,
				equality_resolution.operation, state_expression,
				state_const, state->location.source_span);
			if (condition.index == PIGEN_INVALID_ID)
			{
				ok = 0;
				break;
			}
			state_guard = pigen_predicate_and_condition(model,
				guard_no_reset, condition, 1);
			if (state_guard.index == PIGEN_INVALID_ID)
			{
				ok = 0;
				break;
			}
			{
				pigen_syntax_id item;

				for (item = state->first_child;
					ok && item.index != PIGEN_INVALID_ID; )
				{
					const pigen_syntax_node *statement =
						pigen_syntax_get(resolver->syntax, item);

					if (!statement) break;
					if (!resolve_procedural_statement(resolver,
						module_id, process, domain, item,
						state_guard))
						ok = 0;
					else
						item = statement->next_sibling;
				}
			}
			index++;
			child = state->next_sibling;
		}
		resolver->expression_scope = saved_scope;
	}
	resolver->fsm_state_expression =
		(pigen_expr_id){PIGEN_INVALID_ID};
	resolver->fsm_state_names = NULL;
	resolver->fsm_state_count = 0;
	free(names);
	return ok;
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
	pigen_expr_id destination_expression = pigen_resolve_expression(
		resolver->syntax, model, resolver->expression_scope,
		assignment->as.nonblocking_assignment.destination, resolver->policy,
		resolver->error);
	pigen_lvalue_id destination = pigen_lvalue_resolve(model,
		destination_expression);
	pigen_expr_id value;
	const pigen_semantic_expr *destination_value;
	const pigen_semantic_expr *source_value;
	pigen_transfer_id transfer;
	pigen_transfer_signal_use *signal_uses = NULL;
	size_t signal_use_count = 0;

	if (destination.index == PIGEN_INVALID_ID)
		return fail_location(resolver, assignment->location,
			"transfer destination requires a supported lvalue");
	if (!lvalue_is_assignable(model, destination))
		return fail_location(resolver, assignment->location,
			"transfer destination is not a writable variable or signal");
	destination_value = pigen_expr_get(model, destination_expression);
	if (!destination_value)
		return fail_location(resolver, assignment->location,
			"transfer destination requires a supported expression");
	{
		const pigen_semantic_lvalue *dest_lvalue =
			pigen_lvalue_get(model, destination);

		if (dest_lvalue && dest_lvalue->kind == PIGEN_LVALUE_CONCATENATION)
			value = pigen_resolve_expression(resolver->syntax, model,
				resolver->expression_scope,
				assignment->as.nonblocking_assignment.value,
				resolver->policy, resolver->error);
		else
			value = pigen_resolve_assignment_value(resolver->syntax, model,
				resolver->expression_scope,
				assignment->as.nonblocking_assignment.value,
				destination_value->data_type, resolver->policy,
				resolver->error);
	}
	if (value.index == PIGEN_INVALID_ID)
		return fail_location(resolver, assignment->location,
			"transfer value requires a compatible expression");
	source_value = pigen_expr_get(model, value);
	if (!destination_value || !source_value ||
		destination_value->shape.index != source_value->shape.index)
		return fail_location(resolver, assignment->location,
			"transfer shape mismatch");
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
	pigen_expr_id condition = INVALID_ID(pigen_expr_id);
	pigen_predicate_id then_guard;
	int transfer_condition = 0;

	/* `if (destination <= source)` is the transfer-and-branch form: perform
	 * the transfer and branch on its acceptance. */
	{
		const pigen_syntax_expr *syntax_condition = pigen_syntax_expr_get(
			&resolver->syntax->expressions,
			statement->as.if_statement.condition);

		if (syntax_condition &&
			syntax_condition->kind == PIGEN_SYNTAX_EXPR_BINARY &&
			syntax_condition->as.binary.operator ==
				PIGEN_SYNTAX_OP_LESS_EQUAL)
		{
			pigen_semantic_model *model = resolver->model;
			pigen_expr_id destination_expression = pigen_resolve_expression(
				resolver->syntax, model, resolver->expression_scope,
				syntax_condition->as.binary.left, resolver->policy,
				resolver->error);
			pigen_lvalue_id destination =
				pigen_lvalue_resolve(model, destination_expression);
			const pigen_semantic_expr *destination_value =
				pigen_expr_get(model, destination_expression);
			pigen_expr_id source_expression = pigen_resolve_expression(
				resolver->syntax, model, resolver->expression_scope,
				syntax_condition->as.binary.right, resolver->policy,
				resolver->error);
			const pigen_semantic_expr *source_value =
				pigen_expr_get(model, source_expression);

			if (destination.index != PIGEN_INVALID_ID &&
				destination_value && source_value &&
				source_value->kind == PIGEN_EXPR_SYMBOL)
			{
				pigen_signal_id source_signal = pigen_symbol_signal(model,
					source_value->as.symbol);
				pigen_expr_id value = pigen_resolve_assignment_value(
					resolver->syntax, model, resolver->expression_scope,
					syntax_condition->as.binary.right,
					destination_value->data_type, resolver->policy,
					resolver->error);
				pigen_transfer_signal_use *uses = NULL;
				size_t use_count = 0;
				pigen_transfer_id transfer;

				if (value.index != PIGEN_INVALID_ID &&
					source_signal.index != PIGEN_INVALID_ID &&
					analyze_transfer(resolver, destination, value, guard,
						domain, statement->location.source_span, &uses,
						&use_count))
				{
					transfer = pigen_transfer_add(model,
						statement->first_child.index ==
							PIGEN_INVALID_ID ? (pigen_syntax_id){0} :
							(pigen_syntax_id){0},
						module_id, process, destination, value, guard,
						domain, uses, use_count,
						statement->location.source_span);
					free(uses);
					if (transfer.index != PIGEN_INVALID_ID)
					{
						condition = pigen_expr_add_accepts(model,
							destination, value, source_signal,
							statement->location.source_span);
						if (condition.index != PIGEN_INVALID_ID)
							transfer_condition = 1;
					}
				}
				else
					free(uses);
			}
		}
	}
	if (!transfer_condition)
		condition = pigen_resolve_expression(resolver->syntax,
			resolver->model, resolver->expression_scope,
			statement->as.if_statement.condition,
			resolver->policy, resolver->error);
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

static int resolve_action(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id syntax_id, const pigen_syntax_node *statement,
	pigen_predicate_id guard)
{
	pigen_semantic_model *model = resolver->model;
	pigen_source_span target = token_spelling(resolver,
		statement->as.action.target);
	pigen_symbol_id symbol;
	const pigen_symbol *known;
	pigen_signal_id signal;
	pigen_action_kind kind;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);

	(void)syntax_id;
	if (!module) return 0;
	symbol = pigen_symbol_lookup(model, module->scope, target);
	known = pigen_symbol_get(model, symbol);
	if (!known || known->kind != PIGEN_SYMBOL_SIGNAL)
		return fail_location(resolver, statement->location,
			"validity action requires a declared signal");
	signal = pigen_symbol_signal(model, symbol);
	if (signal.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"validity action requires a signal");
	kind = statement->as.action.action == 1 ? PIGEN_ACTION_VALIDATE :
		statement->as.action.action == 2 ? PIGEN_ACTION_INVALIDATE :
			PIGEN_ACTION_FLUSH;
	if (pigen_action_add(model, syntax_id, module_id, process, domain, signal,
		kind, guard, statement->location.source_span).index ==
		PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid validity action");
	return 1;
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
	if (statement->kind == PIGEN_SYNTAX_PIPELINE)
		return resolve_pipeline(resolver, module_id, process, domain,
			statement_id, statement, guard);
	if (statement->kind == PIGEN_SYNTAX_GOTO)
		return resolve_goto(resolver, module_id, process, domain,
			statement_id, statement, guard);
	if (statement->kind == PIGEN_SYNTAX_ACTION)
		return resolve_action(resolver, module_id, process, domain,
			statement_id, statement, guard);
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
		module->scope, syntax_node->as.clocked_process.clock, resolver->policy,
		resolver->error);
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
	{
		pigen_scope_id saved_scope = resolver->expression_scope;
		resolver->expression_scope = module->scope;
		process = pigen_process_add(model, syntax_id, module_id, domain, clock,
			syntax_node->location.source_span);
		resolver->expression_scope = saved_scope;
	}
	if (process.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid clocked process");
	if (!body || body->next_sibling.index != PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid procedural body");
	return resolve_procedural_statement(resolver, module_id, process, domain,
		body_id, pigen_predicate_true(model));
}

static int resolve_instance(resolver *resolver, pigen_module_id module_id,
	pigen_syntax_id syntax_id, const pigen_syntax_node *node);

static int child_port_at(pigen_semantic_model *model,
	pigen_module_id child_module, size_t index, pigen_symbol_id *symbol,
	pigen_signal_id *signal)
{
	size_t at = 0;
	for (size_t i = 0; i < model->signal_count; i++)
	{
		const pigen_semantic_signal *known = &model->signals[i];
		if (known->module.index != child_module.index ||
			known->direction == PIGEN_SEMANTIC_INTERNAL) continue;
		if (at++ == index)
		{
			*symbol = known->symbol;
			*signal = (pigen_signal_id){(uint32_t)i};
			return 1;
		}
	}
	return 0;
}

static int resolve_instance(resolver *resolver, pigen_module_id module_id,
	pigen_syntax_id syntax_id, const pigen_syntax_node *node)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_source_span module_name = token_spelling(resolver,
		node->as.instance.module);
	pigen_source_span instance_name = token_spelling(resolver,
		node->as.instance.name);
	pigen_symbol_id module_symbol = pigen_symbol_lookup(model,
		model->compilation_scope, module_name);
	const pigen_symbol *module_known = pigen_symbol_get(model, module_symbol);
	pigen_module_id child_module;
	const pigen_semantic_module *child;
	pigen_symbol_id instance_symbol;
	pigen_declare_result declared;
	pigen_instance_id instance;
	pigen_syntax_id child_id;
	size_t positional = 0;

	if (!module) return fail_location(resolver, node->location,
		"instance requires a module");
	if (!module_known || module_known->kind != PIGEN_SYMBOL_MODULE)
		return fail_location(resolver, node->location,
			"instance module is not declared");
	child_module = module_known->object.module;
	child = pigen_module_get(model, child_module);
	if (!child) return fail_location(resolver, node->location,
		"invalid instance module");
	declared = pigen_symbol_declare(model, module->scope,
		PIGEN_SYMBOL_INSTANCE, INVALID_ID(pigen_data_type_id),
		instance_name, node->location.source_span, &instance_symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail_location(resolver, node->location, "duplicate instance name");
	if (declared != PIGEN_DECLARE_OK)
		return fail_location(resolver, node->location, "invalid instance declaration");
	instance = pigen_instance_add(model, syntax_id, module_id,
		instance_symbol, child_module, node->location.source_span);
	if (instance.index == PIGEN_INVALID_ID)
		return fail_location(resolver, node->location,
			"invalid instance semantic object");
	for (child_id = node->first_child; child_id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *connection = pigen_syntax_get(resolver->syntax, child_id);
		pigen_symbol_id port_symbol;
		pigen_signal_id port_signal;
		pigen_source_span port_name = {INVALID_ID(pigen_source_id), 0, 0};
		const pigen_symbol *port_known;
		const pigen_semantic_signal *port;
		pigen_expr_id value = INVALID_ID(pigen_expr_id);
		pigen_semantic_direction direction;
		pigen_data_type_id connection_type = INVALID_ID(pigen_data_type_id);

		if (!connection) break;
		if (connection->kind != PIGEN_SYNTAX_CONNECTION)
		{ child_id = connection->next_sibling; continue; }
		if (connection->as.connection.port.index != PIGEN_INVALID_ID)
		{
			port_name = token_spelling(resolver,
				connection->as.connection.port);
			port_symbol = pigen_symbol_lookup(model, child->scope, port_name);
		}
		else if (!child_port_at(model, child_module, positional, &port_symbol,
			&port_signal))
			return fail_location(resolver, connection->location,
				"too many positional instance connections");
		else
		{
			const pigen_symbol *positional_symbol = pigen_symbol_get(model,
				port_symbol);
			if (positional_symbol)
				port_name = positional_symbol->name;
		}
		positional++;
		if (port_symbol.index == PIGEN_INVALID_ID)
		{
			/* Generated boundary controls: <payload>_valid / <payload>_ready. */
			size_t length = 0;
			const char *text = pigen_source_span_text(model->sources,
				port_name, &length);
			const char *suffix = NULL;
			pigen_semantic_direction base_direction;
			size_t suffix_length = 0;

			if (text && length > 6 && !memcmp(text + length - 6,
				"_valid", 6))
				suffix = "_valid", suffix_length = 6;
			else if (text && length > 6 && !memcmp(text + length - 6,
				"_ready", 6))
				suffix = "_ready", suffix_length = 6;
			if (suffix)
			{
				char *base = pigen_resize(NULL,
					length - suffix_length + 1);
				pigen_source_span base_span = port_name;
				memcpy(base, text, length - suffix_length);
				base[length - suffix_length] = 0;
				base_span.end = base_span.start + length - suffix_length;
				port_symbol = pigen_symbol_lookup(model, child->scope,
					base_span);
				free(base);
				if (port_symbol.index != PIGEN_INVALID_ID)
				{
					const pigen_symbol *base_symbol =
						pigen_symbol_get(model, port_symbol);
					base_direction = pigen_signal_get(model,
						base_symbol->object.signal)->direction;
					direction = suffix[1] == 'v' ? base_direction :
						(base_direction == PIGEN_SEMANTIC_INPUT ?
							PIGEN_SEMANTIC_OUTPUT :
							base_direction == PIGEN_SEMANTIC_OUTPUT ?
								PIGEN_SEMANTIC_INPUT :
								PIGEN_SEMANTIC_INTERNAL);
					port_signal = base_symbol->object.signal;
					connection_type = pigen_data_type_sized_logic(model, 1,
						PIGEN_SIGN_UNSIGNED);
					goto port_ready;
				}
			}
		}
		port_known = pigen_symbol_get(model, port_symbol);
		if (!port_known || port_known->kind != PIGEN_SYMBOL_SIGNAL)
			return fail_location(resolver, connection->location,
				"unknown child module port");
		port_signal = pigen_symbol_signal(model, port_symbol);
		port = pigen_signal_get(model, port_signal);
		if (!port) return fail_location(resolver, connection->location,
			"invalid child module port");
		direction = port->direction;
		connection_type = port->data_type;
	port_ready:
		if (connection->as.connection.value.index != PIGEN_INVALID_ID)
		{
			if (direction == PIGEN_SEMANTIC_OUTPUT)
			{
				pigen_expr_id expression = pigen_resolve_expression(
					resolver->syntax, model, module->scope,
					connection->as.connection.value, resolver->policy,
					resolver->error);
				pigen_lvalue_id lvalue = pigen_lvalue_resolve(model, expression);
				const pigen_semantic_lvalue *known =
					pigen_lvalue_get(model, lvalue);
				if (!known || known->kind != PIGEN_LVALUE_PROJECTION)
					return fail_location(resolver, connection->location,
						"instance output requires a writable parent signal");
				value = expression;
			}
			else
			{
				value = pigen_resolve_assignment_value(resolver->syntax,
					model, module->scope,
					connection->as.connection.value, connection_type,
					resolver->policy, resolver->error);
				if (value.index == PIGEN_INVALID_ID)
				{
					pigen_expr_id raw = pigen_resolve_expression(
						resolver->syntax, model, module->scope,
						connection->as.connection.value, resolver->policy,
						resolver->error);
					const pigen_semantic_expr *known =
						pigen_expr_get(model, raw);

					if (known && known->data_type.index !=
						PIGEN_INVALID_ID)
					{
						pigen_const_expr_id source_width =
							pigen_data_type_packed_width(model,
								known->data_type);
						pigen_const_expr_id target_width =
							pigen_data_type_packed_width(model,
								connection_type);
						uint64_t sw = 0, tw = 0;

						if (pigen_const_expr_evaluate_u64(model,
							source_width, &sw) &&
							pigen_const_expr_evaluate_u64(model,
								target_width, &tw) && sw == tw)
							value = raw;
					}
				}
			}
			if (value.index == PIGEN_INVALID_ID)
				return fail_location(resolver, connection->location,
					"instance connection requires a compatible expression");
		}
		if (!pigen_instance_add_connection(model, instance, port_symbol,
			port_signal, port_name, value, direction,
			connection->location.source_span))
			return fail_location(resolver, connection->location,
				"invalid instance connection");
		child_id = connection->next_sibling;
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
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_data_type_id),
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
	resolver->expression_scope = scope;
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
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			!add_signal_declaration(resolver, module_id, node)) return 0;
		child = node->next_sibling;
	}
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS &&
			!add_clocked_process(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_FSM &&
			!resolve_fsm(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_INSTANCE &&
			!resolve_instance(resolver, module_id, child, node)) return 0;
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
					const pigen_transfer_type_descriptor *descriptor;
					unsigned overlap;
					if (later_uses[i].signal.index !=
						earlier_uses[j].signal.index) continue;
					signal = pigen_signal_get(model, later_uses[i].signal);
					descriptor = signal ? pigen_transfer_type_descriptor_get(
						signal->transfer_type) : NULL;
					if (!descriptor || !descriptor->requires_ownership) continue;
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
	pigen_semantic_model *model, const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	const pigen_source_manager *sources = syntax && syntax->expanded ?
		syntax->expanded->sources : NULL;
	resolver resolver = {syntax, model, policy, error,
		(pigen_scope_id){PIGEN_INVALID_ID},
		INVALID_ID(pigen_expr_id), INVALID_ID(pigen_data_type_id),
		NULL, 0};
	const pigen_syntax_node *root;
	pigen_syntax_id child;
	pigen_source_span root_span;

	pigen_semantic_init(model, sources);
	if (error) *error = (pigen_semantic_error){INVALID_ID(pigen_origin_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0}, NULL};
	if (!policy || !policy->maximum_generated_bits) return 0;
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
