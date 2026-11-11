/* Resolution from structured syntax into stable semantic identities. */
#include <stdlib.h>
#include <stdio.h>
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
	pigen_module_id module;
	pigen_process_id process;
	pigen_clock_domain_id domain;
	pigen_source_span name;
	pigen_predicate_id guard;
	pigen_source_span span;
	int consumed;
} pending_pipeline_reset;

typedef struct {
	pigen_module_id module;
	pigen_process_id process;
	pigen_clock_domain_id domain;
	pigen_source_span name;
	pigen_source_span span;
	pigen_signal_id output;
} resolved_pipeline_reset;

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
	int in_pipeline_stage;
	pending_pipeline_reset *reset_bindings;
	size_t reset_binding_count;
	size_t reset_binding_capacity;
	resolved_pipeline_reset *pipelines;
	size_t pipeline_count;
	size_t pipeline_capacity;
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

static pigen_source_span declaration_name_span(resolver *resolver,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	pigen_source_span spelling = token_spelling(resolver, token);
	size_t expanded_length = 0, origin_length = 0;
	const char *expanded_text;
	const char *origin_text;

	if (!known) return spelling;
	expanded_text = pigen_expanded_token_text(resolver->syntax->expanded,
		known, &expanded_length);
	origin_text = pigen_source_span_text(resolver->model->sources, spelling,
		&origin_length);
	if (!expanded_text || !origin_text ||
		(expanded_length == origin_length &&
		 !memcmp(expanded_text, origin_text, expanded_length)))
		return spelling;
	{
		pigen_source_id source = pigen_source_add(
			(pigen_source_manager *)resolver->model->sources, "<pigen-macro>",
			expanded_text, expanded_length);

		if (source.index != PIGEN_INVALID_ID)
			return (pigen_source_span){source, 0, expanded_length};
	}
	return spelling;
}

static pigen_source_span token_expansion(const resolver *resolver,
	pigen_token_id token)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		resolver->syntax->expanded, token);
	return known ? pigen_origin_expansion_span(resolver->syntax->expanded,
		known->origin) : (pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static int spelling_text_equal(resolver *resolver, pigen_source_span left,
	pigen_source_span right)
{
	size_t left_length = 0, right_length = 0;
	const char *left_text = pigen_source_span_text(resolver->model->sources,
		left, &left_length);
	const char *right_text = pigen_source_span_text(resolver->model->sources,
		right, &right_length);

	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
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
			{
				const pigen_syntax_type *type = pigen_syntax_type_get(
					&resolver->syntax->types,
					syntax_node->as.parameter.data_type);
				pigen_source_span base = type && type->base.index !=
					PIGEN_INVALID_ID ? token_spelling(resolver, type->base) :
					(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
				size_t length = 0;
				const char *text = pigen_source_span_text(model->sources,
					base, &length);

				/* `integer`/`int` are ordinary 32-bit signed integer
				 * parameter types; reuse the value's integer type. */
				if (text && ((length == 3 && !memcmp(text, "int", 3)) ||
					(length == 7 && !memcmp(text, "integer", 7))))
					parameter_type = expression->data_type;
				else
					return fail_location(resolver, syntax_node->location,
						"invalid parameter type");
			}
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
			declaration_name_span(resolver,
				declarator->as.signal_declarator.name),
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
	{
		const pigen_syntax_type *type = pigen_syntax_type_get(
			&resolver->syntax->types, syntax_node->as.type_definition.type);

		if (type && type->is_packed_struct)
		{
			const pigen_syntax_struct_member *members =
				pigen_syntax_type_members(&resolver->syntax->types,
					type->first_member, type->member_count);
			pigen_data_type_id *types;
			uint64_t *widths;
			uint64_t total = 0;
			size_t i;

			if (type->member_count && !members) return 0;
			types = pigen_resize(NULL,
				type->member_count * sizeof(*types));
			widths = pigen_resize(NULL,
				type->member_count * sizeof(*widths));
			for (i = 0; i < type->member_count; i++)
			{
				pigen_const_expr_id width;

				types[i] = pigen_resolve_type(resolver->syntax,
					resolver->model, scope, members[i].type,
					resolver->error);
				if (types[i].index == PIGEN_INVALID_ID)
					goto struct_members_done;
				width = pigen_data_type_packed_width(resolver->model,
					types[i]);
				if (!pigen_const_expr_evaluate_u64(resolver->model,
					width, &widths[i]) || widths[i] == 0)
					goto struct_members_done;
				total += widths[i];
			}
			{
				uint64_t offset = total;

				for (i = 0; i < type->member_count; i++)
				{
					offset -= widths[i];
					if (!pigen_struct_member_add(resolver->model,
						symbol, token_spelling(resolver,
							members[i].name), types[i],
						(size_t)offset))
						goto struct_members_done;
				}
			}
			free(types);
			free(widths);
			return 1;
struct_members_done:
			free(types);
			free(widths);
			return 0;
		}
	}
	return 1;
}

static int lvalue_is_assignable(const pigen_semantic_model *model,
	pigen_lvalue_id lvalue_id)
{
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model, lvalue_id);
	size_t i;

	if (!lvalue) return 0;
	if (lvalue->kind == PIGEN_LVALUE_PLACEHOLDER) return 1;
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
	for (i = 0; valid && domain.index != PIGEN_INVALID_ID && i < *signal_use_count; i++)
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
static int apply_pipeline_reset(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, const pending_pipeline_reset *reset,
	pigen_signal_id output, pigen_source_span pipeline_span)
{
	pigen_semantic_model *model = resolver->model;
	size_t signal_index;

	if (pigen_action_add(model, INVALID_ID(pigen_syntax_id), module_id,
		process, domain, output, PIGEN_ACTION_FLUSH, reset->guard,
		reset->span).index == PIGEN_INVALID_ID)
		return 0;
	for (signal_index = 0; signal_index < model->signal_count; signal_index++)
	{
		const pigen_semantic_signal *field = &model->signals[signal_index];

		if (field->module.index == module_id.index &&
			field->span.start >= pipeline_span.start &&
			field->span.end <= pipeline_span.end &&
			pigen_action_add(model, INVALID_ID(pigen_syntax_id), module_id,
				process, domain, (pigen_signal_id){(uint32_t)signal_index},
				PIGEN_ACTION_FLUSH, reset->guard,
				reset->span).index == PIGEN_INVALID_ID)
			return 0;
	}
	return 1;
}

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

	/* Apply any pipeline_reset bindings already seen for this pipeline, and
	 * remember the resolved pipeline so later reset statements can bind too. */
	{
		pigen_source_span pipeline_name = token_spelling(resolver,
			pipeline->as.pipeline.name);
		size_t r;

		for (r = 0; r < resolver->reset_binding_count; r++)
		{
			pending_pipeline_reset *reset = &resolver->reset_bindings[r];

			if (reset->consumed ||
				reset->module.index != module_id.index ||
				reset->process.index != process.index ||
				!spelling_text_equal(resolver, reset->name, pipeline_name))
				continue;
			if (!apply_pipeline_reset(resolver, module_id, process, domain,
				reset, ident.signal, pipeline->location.source_span))
				return fail_location(resolver, pipeline->location,
					"invalid pipeline reset binding");
			reset->consumed = 1;
		}
		if (resolver->pipeline_count == resolver->pipeline_capacity)
		{
			resolver->pipeline_capacity = resolver->pipeline_capacity ?
				resolver->pipeline_capacity * 2 : 8;
			resolver->pipelines = pigen_resize(resolver->pipelines,
				resolver->pipeline_capacity * sizeof(*resolver->pipelines));
		}
		resolver->pipelines[resolver->pipeline_count++] =
			(resolved_pipeline_reset){module_id, process, domain,
				pipeline_name, pipeline->location.source_span, ident.signal};
	}

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
				resolver->in_pipeline_stage = 1;
				if (!resolve_assignment(resolver, module_id, process,
					domain, item, node, guard))
				{
					resolver->in_pipeline_stage = 0;
					ok = 0;
					break;
				}
				resolver->in_pipeline_stage = 0;
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

static int syntax_is_placeholder(resolver *resolver,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&resolver->syntax->expressions, expression);
	pigen_source_span spelling;
	size_t length = 0;
	const char *text;

	if (!known || known->kind != PIGEN_SYNTAX_EXPR_NAME) return 0;
	spelling = token_spelling(resolver, known->as.atom.token);
	text = pigen_source_span_text(resolver->model->sources, spelling, &length);
	return text && length == 1 && text[0] == '_';
}

static int resolve_placeholder_assignment(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id assignment_id,
	const pigen_syntax_node *assignment,
	pigen_predicate_id guard, pigen_expr_id value)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_expr *value_expr = pigen_expr_get(model, value);
	const pigen_syntax_expr *destination = pigen_syntax_expr_get(
		&resolver->syntax->expressions,
		assignment->as.nonblocking_assignment.destination);
	const pigen_syntax_expr_id *dest_children;
	const pigen_expr_id *value_children;
	pigen_lvalue_id *children;
	pigen_lvalue_id destination_lvalue;
	pigen_transfer_signal_use *signal_uses = NULL;
	size_t signal_use_count = 0;
	size_t i;
	int ok = 1;

	if (!destination || destination->kind != PIGEN_SYNTAX_EXPR_CONCATENATION ||
		!value_expr || value_expr->kind != PIGEN_EXPR_CONCATENATION ||
		destination->as.sequence.child_count !=
			value_expr->as.sequence.child_count)
		return fail_location(resolver, assignment->location,
			"placeholder destination requires a matching concatenation");
	dest_children = pigen_syntax_expr_children(&resolver->syntax->expressions,
		destination->as.sequence.first_child,
		destination->as.sequence.child_count);
	value_children = pigen_expr_children(model,
		value_expr->as.sequence.first_child,
		value_expr->as.sequence.child_count);
	if (!dest_children || !value_children) return 0;
	children = pigen_resize(NULL,
		destination->as.sequence.child_count * sizeof(*children));
	for (i = 0; i < destination->as.sequence.child_count && ok; i++)
	{
		if (syntax_is_placeholder(resolver, dest_children[i]))
		{
			const pigen_semantic_expr *rhs = pigen_expr_get(model,
				value_children[i]);

			if (!rhs) ok = 0;
			else children[i] = pigen_lvalue_add_placeholder(model,
				rhs->data_type, assignment->location.source_span);
			if (ok && children[i].index == PIGEN_INVALID_ID) ok = 0;
		}
		else
		{
			pigen_expr_id child_expression = pigen_resolve_expression(
				resolver->syntax, model, resolver->expression_scope,
				dest_children[i], resolver->policy, resolver->error);

			if (child_expression.index == PIGEN_INVALID_ID)
			{
				ok = 0;
				break;
			}
			children[i] = pigen_lvalue_resolve(model, child_expression);
			if (children[i].index == PIGEN_INVALID_ID ||
				!lvalue_is_assignable(model, children[i]))
				ok = 0;
		}
	}
	destination_lvalue = ok ? pigen_lvalue_add_concatenation(model, children,
		destination->as.sequence.child_count, value_expr->data_type,
		assignment->location.source_span) :
		INVALID_ID(pigen_lvalue_id);
	free(children);
	if (!ok || destination_lvalue.index == PIGEN_INVALID_ID)
		return fail_location(resolver, assignment->location,
			"invalid placeholder destination");
	if (!analyze_transfer(resolver, destination_lvalue, value, guard, domain,
		assignment->location.source_span, &signal_uses,
		&signal_use_count))
		return 0;
	{
		pigen_transfer_id transfer = pigen_transfer_add(model, assignment_id,
			module_id, process, destination_lvalue, value, guard, domain,
			signal_uses, signal_use_count,
			assignment->location.source_span);
		free(signal_uses);
		if (transfer.index == PIGEN_INVALID_ID)
			return fail_location(resolver, assignment->location,
				"invalid transfer semantic object");
	}
	return 1;
}

static int resolve_assignment(resolver *resolver, pigen_module_id module_id,
	pigen_process_id process, pigen_clock_domain_id domain,
	pigen_syntax_id assignment_id, const pigen_syntax_node *assignment,
	pigen_predicate_id guard)
{
	pigen_semantic_model *model = resolver->model;
	{
		const pigen_syntax_expr *destination_syntax = pigen_syntax_expr_get(
			&resolver->syntax->expressions,
			assignment->as.nonblocking_assignment.destination);

		if (destination_syntax &&
			destination_syntax->kind == PIGEN_SYNTAX_EXPR_CONCATENATION)
		{
			const pigen_syntax_expr_id *syntax_children =
				pigen_syntax_expr_children(&resolver->syntax->expressions,
					destination_syntax->as.sequence.first_child,
					destination_syntax->as.sequence.child_count);
			size_t placeholder_index;

			for (placeholder_index = 0; syntax_children &&
				placeholder_index < destination_syntax->as.sequence.child_count;
				placeholder_index++)
				if (syntax_is_placeholder(resolver,
					syntax_children[placeholder_index]))
					break;
			if (syntax_children && placeholder_index <
				destination_syntax->as.sequence.child_count)
			{
				pigen_expr_id placeholder_value = pigen_resolve_expression(
					resolver->syntax, model, resolver->expression_scope,
					assignment->as.nonblocking_assignment.value,
					resolver->policy, resolver->error);

				return resolve_placeholder_assignment(resolver, module_id,
					process, domain, assignment_id, assignment, guard,
					placeholder_value);
			}
		}
	}
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
	{
		const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model,
			destination);
		const pigen_semantic_signal *destination_signal = NULL;
		const pigen_transfer_type_descriptor *descriptor = NULL;

		if (lvalue && lvalue->kind == PIGEN_LVALUE_PROJECTION)
			destination_signal = pigen_signal_get(model,
				lvalue->as.projection.signal);
		if (destination_signal)
			descriptor = pigen_transfer_type_descriptor_get(
				destination_signal->transfer_type);
		if (resolver->in_pipeline_stage && destination_signal &&
			descriptor && descriptor->is_static &&
			(destination_signal->direction == PIGEN_SEMANTIC_INPUT ||
			 destination_signal->transfer_type == PIGEN_TRANSFER_TYPE_WIRE))
			return fail_location(resolver, assignment->location,
				"wire is not a transfer destination");
		if (destination_signal && descriptor && descriptor->is_static &&
			destination_signal->transfer_type == PIGEN_TRANSFER_TYPE_WIRE)
			return fail_location(resolver, assignment->location,
				"wire cannot be a procedural signal destination");
		return fail_location(resolver, assignment->location,
			"transfer destination is not a writable variable or signal");
	}
	destination_value = pigen_expr_get(model, destination_expression);
	if (!destination_value)
		return fail_location(resolver, assignment->location,
			"transfer destination requires a supported expression");
	{
		const pigen_semantic_expr *dest_shape = destination_value;
		const pigen_semantic_lvalue *dest_lvalue =
			pigen_lvalue_get(model, destination);

		while (dest_shape && dest_shape->kind == PIGEN_EXPR_GROUP)
			dest_shape = pigen_expr_get(model,
				dest_shape->as.group.operand);
		if (dest_lvalue && dest_lvalue->kind == PIGEN_LVALUE_PROJECTION &&
			dest_shape && (dest_shape->kind == PIGEN_EXPR_SELECT ||
				dest_shape->kind == PIGEN_EXPR_INDEX))
		{
			const pigen_semantic_signal *dest_signal = pigen_signal_get(
				model, dest_lvalue->as.projection.signal);
			const pigen_transfer_type_descriptor *descriptor = dest_signal ?
				pigen_transfer_type_descriptor_get(
					dest_signal->transfer_type) : NULL;

			if (descriptor && !descriptor->is_static)
				return fail_location(resolver, assignment->location,
					"buffered signal destination must be written as a "
					"complete value");
		}
	}

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
	{
		const pigen_semantic_expr *slice_value = source_value;

		while (slice_value &&
			slice_value->kind == PIGEN_EXPR_CONVERSION)
			slice_value = pigen_expr_get(model,
				slice_value->as.conversion.operand);
		if (destination.index != PIGEN_INVALID_ID && slice_value &&
			(slice_value->kind == PIGEN_EXPR_SELECT ||
			 slice_value->kind == PIGEN_EXPR_INDEX))
	{
		const pigen_semantic_lvalue *dest = pigen_lvalue_get(model,
			destination);
		const pigen_semantic_signal *dest_signal = dest &&
			dest->kind == PIGEN_LVALUE_PROJECTION ? pigen_signal_get(model,
				dest->as.projection.signal) : NULL;
		const pigen_transfer_type_descriptor *descriptor = dest_signal ?
			pigen_transfer_type_descriptor_get(dest_signal->transfer_type) :
			NULL;

		if (dest_signal && descriptor && !descriptor->is_static)
		{
			pigen_const_expr_id dw = pigen_data_type_packed_width(model,
				destination_value->data_type);
			pigen_const_expr_id vw = pigen_data_type_packed_width(model,
				slice_value->data_type);
			uint64_t d = 0, v = 0;

			if (pigen_const_expr_evaluate_u64(model, dw, &d) &&
				pigen_const_expr_evaluate_u64(model, vw, &v) && d != v)
				return fail_location(resolver, assignment->location,
					"Pigen transfer aggregate width mismatch");
		}
		}
	}
	destination_value = pigen_expr_get(model, destination_expression);
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
	if (statement->as.action.action == 4)
	{
		size_t p;

		for (p = 0; p < resolver->pipeline_count; p++)
		{
			resolved_pipeline_reset *pipeline = &resolver->pipelines[p];
			pending_pipeline_reset immediate = {module_id, process, domain,
				target, guard, statement->location.source_span, 0};

			if (pipeline->module.index != module_id.index ||
				pipeline->process.index != process.index ||
				!spelling_text_equal(resolver, pipeline->name, target))
				continue;
			if (!apply_pipeline_reset(resolver, module_id, process, domain,
				&immediate, pipeline->output, pipeline->span))
				return fail_location(resolver, statement->location,
					"invalid pipeline reset binding");
			return 1;
		}
		if (resolver->reset_binding_count ==
			resolver->reset_binding_capacity)
		{
			resolver->reset_binding_capacity =
				resolver->reset_binding_capacity ?
				resolver->reset_binding_capacity * 2 : 8;
			resolver->reset_bindings = pigen_resize(
				resolver->reset_bindings,
				resolver->reset_binding_capacity *
				sizeof(*resolver->reset_bindings));
		}
		resolver->reset_bindings[resolver->reset_binding_count++] =
			(pending_pipeline_reset){module_id, process, domain, target,
				guard, statement->location.source_span, 0};
		return 1;
	}
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
	{
		const pigen_semantic_signal *declared =
			pigen_signal_get(model, signal);

		if (declared && (declared->direction == PIGEN_SEMANTIC_INPUT ||
			declared->direction == PIGEN_SEMANTIC_INOUT))
			return fail_location(resolver, statement->location,
				"cannot modify validity of a module input");
		if (declared && pigen_transfer_type_descriptor_get(
			declared->transfer_type) &&
			pigen_transfer_type_descriptor_get(declared->transfer_type)
				->is_static)
		{
			if (kind == PIGEN_ACTION_VALIDATE)
				pigen_warn("validate on an always-valid wire, reg, or "
					"logic signal has no effect");
			else if (kind == PIGEN_ACTION_INVALIDATE)
				return fail_location(resolver, statement->location,
					"cannot make an always-valid wire, reg, or logic "
					"signal invalid");
		}
	}
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

static int child_port_at(pigen_semantic_model *model,
	pigen_module_id child_module, size_t index, pigen_symbol_id *symbol,
	pigen_signal_id *signal)
{
	size_t at = 0;
	for (size_t i = 0; i < model->signal_count; i++)
	{
		const pigen_semantic_signal *known = &model->signals[i];
		if (known->module.index != child_module.index ||
			known->direction == PIGEN_SEMANTIC_INTERNAL ||
			pigen_signal_is_instance_endpoint(model, (pigen_signal_id){(uint32_t)i})) continue;
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
	pigen_syntax_id syntax_id, const pigen_syntax_node *node, int phase)
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
	size_t positional = 0, connection_index = 0;

	if (!module) return fail_location(resolver, node->location,
		"instance requires a module");
	if (!module_known || module_known->kind != PIGEN_SYMBOL_MODULE)
		return fail_location(resolver, node->location,
			"instance module is not declared");
	child_module = module_known->object.module;
	child = pigen_module_get(model, child_module);
	if (!child) return fail_location(resolver, node->location,
		"invalid instance module");
	if (phase == 0)
	{
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
	}
	else
	{
		instance_symbol = pigen_symbol_lookup(model, module->scope, instance_name);
		const pigen_symbol *known = pigen_symbol_get(model, instance_symbol);
		if (!known || known->kind != PIGEN_SYMBOL_INSTANCE) return 0;
		instance = known->object.instance;
	}
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
		int generated_control = 0;

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
					generated_control = 1;
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
		if (phase == 0) goto add_connection;
		if ((phase == 1) != (direction == PIGEN_SEMANTIC_OUTPUT || generated_control))
		{
			connection_index++;
			child_id = connection->next_sibling;
			continue;
		}
		if (generated_control)
		{
			/* Explicit parent controls have semantic identities needed by fabric
			 * lowering. Implicit boundary siblings are emitted later. */
			if (connection->as.connection.value.index != PIGEN_INVALID_ID)
				value = pigen_resolve_expression(resolver->syntax, model,
					module->scope, connection->as.connection.value,
					resolver->policy, NULL);
			goto add_connection;
		}
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
	add_connection:
		if (phase == 0)
		{
			if (!pigen_instance_add_connection(model, instance, port_symbol,
				port_signal, port_name, value, direction, connection->location.source_span))
				return fail_location(resolver, connection->location, "invalid instance connection");
		}
		else
		{
			const pigen_semantic_instance *record = pigen_instance_get(model, instance);
			model->instance_connections[record->first_connection + connection_index].value = value;
		}
		connection_index++;
		child_id = connection->next_sibling;
	}
	if (phase == 1)
	{
		const pigen_semantic_instance *record = pigen_instance_get(model, instance);
		for (size_t c = 0; c < record->connection_count; c++)
		{
			pigen_semantic_instance_connection *connection = &model->instance_connections[record->first_connection+c];
			pigen_semantic_signal port = model->signals[connection->port_signal.index];
			const pigen_semantic_expr *expression = pigen_expr_get(model, connection->value);
			const pigen_semantic_signal *parent;
			const pigen_symbol *port_symbol_record = pigen_symbol_get(model, port.symbol);
			size_t port_length = 0, name_length = 0;
			const char *port_name = pigen_source_span_text(model->sources, port_symbol_record->name, &port_length);
			const char *connection_name = pigen_source_span_text(model->sources, connection->port_name, &name_length);
			if (port.direction != PIGEN_SEMANTIC_OUTPUT ||
				pigen_transfer_type_descriptor_get(port.transfer_type)->is_static ||
				!port_name || !connection_name || port_length != name_length ||
				memcmp(port_name, connection_name, port_length)) continue;
			while (expression && expression->kind == PIGEN_EXPR_CONVERSION)
				expression = pigen_expr_get(model, expression->as.conversion.operand);
			if (!expression || expression->kind != PIGEN_EXPR_SYMBOL) continue;
			parent = pigen_signal_get(model, pigen_symbol_signal(model, expression->as.symbol));
			if (!parent || !pigen_transfer_type_descriptor_get(parent->transfer_type)->is_static) continue;
			char name[96];
			snprintf(name, sizeof name, "__pigen_endpoint_view_%u_%zu", instance.index, c);
			pigen_source_id source = pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-endpoint-view>", name, strlen(name));
			pigen_source_span span = {source, 0, strlen(name)};
			pigen_symbol_id symbol;
			if (pigen_symbol_declare(model, module->scope, PIGEN_SYMBOL_SIGNAL,
				port.data_type, span, span, &symbol, NULL) != PIGEN_DECLARE_OK) return 0;
			pigen_signal_id view = pigen_signal_add(model, syntax_id, module_id, symbol,
				port.data_type, port.shape, INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, span);
			if (view.index == PIGEN_INVALID_ID) return 0;
			connection->endpoint_view = view;
		}
	}
	if (phase != 0) return 1;
	/* Omitted child ports remain addressable from the enclosing module. */
	{
		size_t signal_count = model->signal_count;
		for (size_t p = 0; p < signal_count; p++)
		{
			pigen_semantic_signal port = model->signals[p];
			const pigen_symbol *port_symbol_record;
			pigen_source_span port_name, name_span;
			pigen_symbol_id endpoint_symbol;
			pigen_signal_id endpoint;
			pigen_expr_id value;
			pigen_source_id name_source;
			pigen_transfer_type transfer;
			const pigen_transfer_type_descriptor *descriptor;
			char name[96];
			int connected = 0;
			const pigen_semantic_instance *record = pigen_instance_get(model, instance);
			if (port.module.index != child_module.index || port.direction == PIGEN_SEMANTIC_INTERNAL ||
				pigen_signal_is_instance_endpoint(model, (pigen_signal_id){(uint32_t)p}))
				continue;
			for (size_t c = 0; c < record->connection_count; c++)
				if (model->instance_connections[record->first_connection + c].port_signal.index == p)
					connected = 1;
			if (connected) continue;
			port_symbol_record = pigen_symbol_get(model, port.symbol);
			port_name = port_symbol_record->name;
			snprintf(name, sizeof name, "__pigen_endpoint_%u_%zu", instance.index, p);
			name_source = pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-endpoint>", name, strlen(name));
			name_span = (pigen_source_span){name_source, 0, strlen(name)};
			if (pigen_symbol_declare(model, module->scope, PIGEN_SYMBOL_SIGNAL,
				port.data_type, name_span, name_span,
				&endpoint_symbol, NULL) != PIGEN_DECLARE_OK) return 0;
			descriptor = pigen_transfer_type_descriptor_get(port.transfer_type);
			transfer = descriptor->is_static ? PIGEN_TRANSFER_TYPE_LOGIC : PIGEN_TRANSFER_TYPE_BUF;
			endpoint = pigen_signal_add(model, syntax_id, module_id, endpoint_symbol,
				port.data_type, port.shape, INVALID_ID(pigen_expr_id), transfer,
				port.direction == PIGEN_SEMANTIC_OUTPUT ? PIGEN_SEMANTIC_INPUT : PIGEN_SEMANTIC_OUTPUT,
				name_span);
			if (endpoint.index == PIGEN_INVALID_ID) return 0;
			value = pigen_expr_add_symbol(model, endpoint_symbol, port.data_type, node->location.source_span);
			if (!pigen_instance_add_connection(model, instance, port.symbol,
				(pigen_signal_id){(uint32_t)p}, port_name, value, port.direction,
				node->location.source_span)) return 0;
		}
	}

	return 1;
}

static int add_module(resolver *resolver, const pigen_syntax_node *syntax_node,
	pigen_syntax_id syntax_id, int declarations_only)
{
	pigen_semantic_model *model = resolver->model;
	pigen_symbol_id symbol;
	pigen_declare_result declared;
	pigen_scope_id scope;
	pigen_module_id module_id;
	pigen_syntax_id child;

	if (!declarations_only)
	{
		symbol = pigen_symbol_lookup(model, model->compilation_scope,
			token_spelling(resolver, syntax_node->as.module.name));
		const pigen_symbol *known = pigen_symbol_get(model, symbol);
		if (!known || known->kind != PIGEN_SYMBOL_MODULE) return 0;
		module_id = known->object.module;
		scope = pigen_module_get(model, module_id)->scope;
		goto resolve_body;
	}

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
	return 1;
resolve_body:
	resolver->expression_scope = scope;
	/* Establish every sibling's identity and omitted endpoints first, then bind
	 * explicit output destinations before input expressions reference them. */
	for (int phase = 0; phase < 3; phase++)
		for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
			if (node->kind == PIGEN_SYNTAX_INSTANCE &&
				!resolve_instance(resolver, module_id, child, node, phase)) return 0;
			child = node->next_sibling;
		}
	/* Computed elastic inputs are storage-free atomic transfers into a
	 * child boundary. Reuse ordinary expression and transfer semantics. */
	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		if (instance->module.index != module_id.index) continue;
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			pigen_semantic_instance_connection *connection = &model->instance_connections[instance->first_connection+c];
			pigen_semantic_signal port = model->signals[connection->port_signal.index];
			const pigen_semantic_expr *value = pigen_expr_get(model, connection->value);
			while (value && value->kind == PIGEN_EXPR_CONVERSION)
				value = pigen_expr_get(model, value->as.conversion.operand);
			if (connection->direction != PIGEN_SEMANTIC_INPUT || port.direction != PIGEN_SEMANTIC_INPUT ||
				pigen_transfer_type_descriptor_get(port.transfer_type)->is_static || !value || value->kind == PIGEN_EXPR_SYMBOL) continue;
			/* Control sibling connections share port_signal: only adapt payload. */
			const pigen_symbol *port_symbol = pigen_symbol_get(model, port.symbol);
			size_t n = 0, m = 0;
			const char *name = pigen_source_span_text(model->sources, connection->port_name, &n);
			const char *base = pigen_source_span_text(model->sources, port_symbol->name, &m);
			if (!name || !base || n != m || memcmp(name, base, n)) continue;
			char name_buffer[96];
			snprintf(name_buffer, sizeof name_buffer, "__pigen_connection_%zu_%zu", i, c);
			pigen_source_id source = pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-connection>", name_buffer, strlen(name_buffer));
			pigen_source_span span = {source, 0, strlen(name_buffer)};
			pigen_symbol_id symbol;
			if (pigen_symbol_declare(model, scope, PIGEN_SYMBOL_SIGNAL, port.data_type,
				span, span, &symbol, NULL) != PIGEN_DECLARE_OK) return 0;
			pigen_signal_id signal = pigen_signal_add(model, instance->syntax, module_id, symbol,
				port.data_type, port.shape, INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_OUTPUT, span);
			if (signal.index == PIGEN_INVALID_ID) return 0;
			pigen_expr_id endpoint = pigen_expr_add_symbol(model, symbol, port.data_type, connection->span);
			pigen_lvalue_id destination = pigen_lvalue_resolve(model, endpoint);
			pigen_process_id process = pigen_structural_process_add(model, instance->syntax, module_id, instance->span);
			pigen_predicate_id guard = pigen_predicate_true(model);
			pigen_transfer_signal_use *uses = NULL;
			size_t use_count = 0;
			if (!analyze_transfer(resolver, destination, connection->value, guard,
				INVALID_ID(pigen_clock_domain_id), connection->span, &uses, &use_count)) return 0;
			pigen_transfer_id transfer = pigen_transfer_add(model, instance->syntax, module_id, process,
				destination, connection->value, guard, INVALID_ID(pigen_clock_domain_id), uses, use_count, connection->span);
			free(uses);
			if (transfer.index == PIGEN_INVALID_ID) return fail(resolver, connection->span, "invalid structural input transfer");
			connection->value = endpoint;
		}
	}
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS &&
			!add_clocked_process(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_FSM &&
			!resolve_fsm(resolver, module_id, child, node)) return 0;
		child = node->next_sibling;
	}
	return 1;
}

static int resolve_expr_references_symbol(const pigen_semantic_model *model,
	pigen_expr_id expression, pigen_symbol_id symbol)
{
	const pigen_semantic_expr *expr;

	if (expression.index == PIGEN_INVALID_ID) return 0;
	expr = pigen_expr_get(model, expression);
	if (!expr) return 0;
	switch (expr->kind)
	{
	case PIGEN_EXPR_SYMBOL:
		return expr->as.symbol.index == symbol.index;
	case PIGEN_EXPR_GROUP:
		return resolve_expr_references_symbol(model, expr->as.group.operand,
			symbol);
	case PIGEN_EXPR_UNARY:
		return resolve_expr_references_symbol(model, expr->as.unary.operand,
			symbol);
	case PIGEN_EXPR_BINARY:
		return resolve_expr_references_symbol(model, expr->as.binary.left,
			symbol) ||
			resolve_expr_references_symbol(model, expr->as.binary.right,
				symbol);
	case PIGEN_EXPR_CONDITIONAL:
		return resolve_expr_references_symbol(model,
			expr->as.conditional.condition, symbol) ||
			resolve_expr_references_symbol(model,
				expr->as.conditional.when_true, symbol) ||
			resolve_expr_references_symbol(model,
				expr->as.conditional.when_false, symbol);
	case PIGEN_EXPR_CONVERSION:
		return resolve_expr_references_symbol(model,
			expr->as.conversion.operand, symbol);
	case PIGEN_EXPR_INDEX:
		return resolve_expr_references_symbol(model, expr->as.index.base,
			symbol) ||
			resolve_expr_references_symbol(model, expr->as.index.index,
				symbol);
	case PIGEN_EXPR_SELECT:
		return resolve_expr_references_symbol(model, expr->as.select.base,
			symbol) ||
			resolve_expr_references_symbol(model, expr->as.select.left,
				symbol) ||
			resolve_expr_references_symbol(model, expr->as.select.right,
				symbol);
	case PIGEN_EXPR_CONCATENATION:
	{
		const pigen_expr_id *children = pigen_expr_children(model,
			expr->as.sequence.first_child, expr->as.sequence.child_count);

		for (size_t i = 0; children && i < expr->as.sequence.child_count; i++)
			if (resolve_expr_references_symbol(model, children[i], symbol))
				return 1;
		return 0;
	}
	case PIGEN_EXPR_PEEK:
		return resolve_expr_references_symbol(model, expr->as.peek.source,
			symbol);
	default:
		return 0;
	}
}

typedef struct {
	pigen_module_id module;
	pigen_signal_id signal;
	pigen_source_span span;
} instance_destination_owner;

static int validate_instance_destination_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	instance_destination_owner *owners = NULL;
	size_t owner_count = 0;

	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		const pigen_semantic_instance_connection *connections =
			pigen_instance_connections(model, (pigen_instance_id){(uint32_t)i});

		for (size_t c = 0; connections && c < instance->connection_count; c++)
		{
			const pigen_semantic_signal *child_port;
			pigen_transfer_type child_transfer;

			if (connections[c].direction != PIGEN_SEMANTIC_OUTPUT) continue;
			child_port = pigen_signal_get(model, connections[c].port_signal);
			if (!child_port) continue;
			child_transfer = child_port->transfer_type;
			if (child_transfer != PIGEN_TRANSFER_TYPE_BUF &&
				child_transfer != PIGEN_TRANSFER_TYPE_PORT &&
				child_transfer != PIGEN_TRANSFER_TYPE_FIFO &&
				child_transfer != PIGEN_TRANSFER_TYPE_SKID) continue;
			for (size_t s = 0; s < model->signal_count; s++)
			{
				const pigen_semantic_signal *parent = &model->signals[s];
				size_t o;

				if (parent->module.index != instance->module.index ||
					!resolve_expr_references_symbol(model,
						connections[c].value, parent->symbol))
					continue;
				for (o = 0; o < owner_count; o++)
					if (owners[o].module.index == parent->module.index &&
						owners[o].signal.index == s)
					{
						free(owners);
						return fail(resolver, connections[c].span,
							"buffered instance destination has multiple producers");
					}
				owners = pigen_resize(owners,
					(owner_count + 1) * sizeof(*owners));
				owners[owner_count++] = (instance_destination_owner){
					parent->module, (pigen_signal_id){(uint32_t)s},
					connections[c].span};
			}
		}
	}
	for (size_t t = 0; t < model->transfer_count; t++)
	{
		const pigen_semantic_transfer *transfer = &model->transfers[t];
		const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(
			model, (pigen_transfer_id){(uint32_t)t});

		for (size_t u = 0; uses && u < transfer->signal_use_count; u++)
		{
			if (!(uses[u].roles & PIGEN_TRANSFER_SIGNAL_WRITE)) continue;
			for (size_t o = 0; o < owner_count; o++)
				if (owners[o].module.index == transfer->module.index &&
					owners[o].signal.index == uses[u].signal.index)
				{
					free(owners);
					return fail(resolver, transfer->span,
						"buffered destination has both instance and local producers");
				}
		}
	}
	free(owners);
	return 1;
}

/* Explicit parent payload wires and instance.port share a consumer identity. */
static int connection_consumes_endpoint(const pigen_semantic_model *model,
	pigen_expr_id value, pigen_signal_id endpoint)
{
	const pigen_semantic_signal *signal = pigen_signal_get(model, endpoint);
	if (resolve_expr_references_symbol(model, value, signal->symbol)) return 1;
	for (size_t c = 0; c < model->instance_connection_count; c++)
	{
		const pigen_semantic_instance_connection *connection = &model->instance_connections[c];
		if (connection->endpoint_view.index != endpoint.index) continue;
		const pigen_semantic_expr *payload = pigen_expr_get(model, connection->value);
		while (payload && payload->kind == PIGEN_EXPR_CONVERSION)
			payload = pigen_expr_get(model, payload->as.conversion.operand);
		if (payload && payload->kind == PIGEN_EXPR_SYMBOL &&
			resolve_expr_references_symbol(model, value, payload->as.symbol)) return 1;
	}
	return 0;
}

static int endpoint_has_readonly_ready(pigen_semantic_model *model,
	pigen_signal_id endpoint);

static int validate_instance_consumer_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	unsigned char *owned = calloc(model->signal_count ? model->signal_count : 1, 1);
	if (!owned) return fail(resolver, model->scopes[model->compilation_scope.index].span,
		"cannot allocate child consumer ownership map");
	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *connection =
				&model->instance_connections[instance->first_connection + c];
			const pigen_semantic_signal *port = pigen_signal_get(model, connection->port_signal);
			const pigen_symbol *port_symbol = pigen_symbol_get(model, connection->port_symbol);
			size_t name_length = 0, port_length = 0;
			const char *name = pigen_source_span_text(model->sources, connection->port_name, &name_length);
			const char *port_name = port_symbol ? pigen_source_span_text(model->sources, port_symbol->name, &port_length) : NULL;
			if (!port || !name || !port_name || name_length != port_length ||
				memcmp(name, port_name, name_length) || connection->direction != PIGEN_SEMANTIC_INPUT ||
				pigen_transfer_type_descriptor_get(port->transfer_type)->is_static) continue;
			for (size_t signal = 0; signal < model->signal_count; signal++)
			{
				const pigen_semantic_signal *parent = &model->signals[signal];
				if (parent->module.index != instance->module.index ||
					pigen_transfer_type_descriptor_get(parent->transfer_type)->is_static ||
					!connection_consumes_endpoint(model, connection->value,
						(pigen_signal_id){(uint32_t)signal})) continue;
				if (endpoint_has_readonly_ready(model, (pigen_signal_id){(uint32_t)signal}))
				{
					free(owned);
					return fail(resolver, connection->span, "automatic consumer conflicts with read-only child ready binding");
				}
				if (owned[signal])
				{
					free(owned);
					return fail(resolver, connection->span, "elastic endpoint has multiple child consumers");
				}
				owned[signal] = 1;
			}
		}
	}
	for (size_t t = 0; t < model->transfer_count; t++)
	{
		const pigen_semantic_transfer *transfer = &model->transfers[t];
		const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(model,
			(pigen_transfer_id){(uint32_t)t});
		for (size_t u = 0; uses && u < transfer->signal_use_count; u++)
			if ((uses[u].roles & PIGEN_TRANSFER_CONSUMER) && endpoint_has_readonly_ready(model, uses[u].signal))
			{
				free(owned);
				return fail(resolver, transfer->span, "automatic consumer conflicts with read-only child ready binding");
			}
		for (size_t u = 0; uses && u < transfer->signal_use_count; u++)
			if ((uses[u].roles & PIGEN_TRANSFER_CONSUMER) && owned[uses[u].signal.index])
			{
				free(owned);
				return fail(resolver, transfer->span, "elastic endpoint has both child and local consumers");
			}
	}
	free(owned);
	return 1;
}

static pigen_signal_id connection_signal(const pigen_semantic_model *model,
	const pigen_semantic_instance_connection *connection)
{
	if (connection->endpoint_view.index != PIGEN_INVALID_ID) return connection->endpoint_view;
	const pigen_semantic_expr *value = pigen_expr_get(model, connection->value);
	while (value && value->kind == PIGEN_EXPR_CONVERSION)
		value = pigen_expr_get(model, value->as.conversion.operand);
	return value && value->kind == PIGEN_EXPR_SYMBOL ? pigen_symbol_signal(model, value->as.symbol) : INVALID_ID(pigen_signal_id);
}

static pigen_signal_id canonical_endpoint(const pigen_semantic_model *model, pigen_signal_id signal)
{
	for (size_t c = 0; c < model->instance_connection_count; c++)
	{
		const pigen_semantic_instance_connection *connection = &model->instance_connections[c];
		if (connection->endpoint_view.index == PIGEN_INVALID_ID) continue;
		const pigen_semantic_expr *value = pigen_expr_get(model, connection->value);
		while (value && value->kind == PIGEN_EXPR_CONVERSION)
			value = pigen_expr_get(model, value->as.conversion.operand);
		if (value && value->kind == PIGEN_EXPR_SYMBOL &&
			pigen_symbol_signal(model, value->as.symbol).index == signal.index) return connection->endpoint_view;
	}
	return signal;
}

static int endpoint_has_readonly_ready(pigen_semantic_model *model,
	pigen_signal_id endpoint)
{
	endpoint = canonical_endpoint(model, endpoint);
	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *payload = &model->instance_connections[instance->first_connection+c];
			if (payload->direction != PIGEN_SEMANTIC_OUTPUT ||
				canonical_endpoint(model, connection_signal(model, payload)).index != endpoint.index) continue;
			const pigen_symbol *port = pigen_symbol_get(model, payload->port_symbol);
			size_t n = 0;
			const char *base = pigen_source_span_text(model->sources, port->name, &n);
			for (size_t d = 0; base && d < instance->connection_count; d++)
			{
				const pigen_semantic_instance_connection *control = &model->instance_connections[instance->first_connection+d];
				size_t m = 0;
				const char *name = pigen_source_span_text(model->sources, control->port_name, &m);
				if (name && m == n+6 && !memcmp(name, base, n) && !memcmp(name+n, "_ready", 6) &&
					pigen_lvalue_resolve(model, control->value).index == PIGEN_INVALID_ID) return 1;
			}
		}
	}
	return 0;
}

static const pigen_semantic_instance_connection *resolved_fabric_port(resolver *resolver,
	pigen_module_id module, pigen_token_id instance_name, pigen_token_id port_name)
{
	pigen_semantic_model *model = resolver->model;
	pigen_symbol_id id = pigen_symbol_lookup(model, model->modules[module.index].scope,
		token_spelling(resolver, instance_name));
	const pigen_symbol *symbol = pigen_symbol_get(model, id);
	if (!symbol || symbol->kind != PIGEN_SYMBOL_INSTANCE) return NULL;
	const pigen_semantic_instance *instance = pigen_instance_get(model, symbol->object.instance);
	for (size_t c = 0; c < instance->connection_count; c++)
	{
		const pigen_semantic_instance_connection *connection = &model->instance_connections[instance->first_connection+c];
		if (spelling_text_equal(resolver, connection->port_name, token_spelling(resolver, port_name))) return connection;
	}
	return NULL;
}

/* Compare canonical endpoint identities across all earlier fabric blocks. */
static int validate_prior_fabric_sources(resolver *resolver,
	pigen_module_id module_id, const pigen_syntax_node *edge,
	pigen_signal_id source)
{
	const pigen_syntax_node *module = pigen_syntax_get(resolver->syntax,
		resolver->model->modules[module_id.index].syntax);
	for (pigen_syntax_id item = module->first_child;
		item.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *fabric = pigen_syntax_get(resolver->syntax, item);
		item = fabric->next_sibling;
		if (fabric->kind != PIGEN_SYNTAX_FABRIC) continue;
		for (pigen_syntax_id e = fabric->first_child;
			e.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *prior = pigen_syntax_get(resolver->syntax, e);
			e = prior->next_sibling;
			if (prior == edge) return 1;
			if (prior->kind != PIGEN_SYNTAX_FABRIC_CONNECTION) continue;
			const pigen_semantic_instance_connection *connection = resolved_fabric_port(
				resolver, module_id, prior->as.fabric_connection.left_instance,
				prior->as.fabric_connection.left_port);
			if (connection && canonical_endpoint(resolver->model,
				connection_signal(resolver->model, connection)).index == source.index)
				return fail(resolver, edge->location.source_span,
					"fabric source has another fabric consumer");
		}
	}
	return 1;
}

/* Fabric arbitration owns its routed group. That ownership cannot also be
 * claimed by a named child connection or a parent transfer. */
static int validate_fabric_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	for (size_t m = 0; m < model->module_count; m++)
	{
		const pigen_syntax_node *module = pigen_syntax_get(resolver->syntax, model->modules[m].syntax);
		for (pigen_syntax_id item = module->first_child; item.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *fabric = pigen_syntax_get(resolver->syntax, item);
			item = fabric->next_sibling;
			if (fabric->kind != PIGEN_SYNTAX_FABRIC) continue;
			for (pigen_syntax_id e = fabric->first_child; e.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *edge = pigen_syntax_get(resolver->syntax, e);
				e = edge->next_sibling;
				if (edge->kind != PIGEN_SYNTAX_FABRIC_CONNECTION) continue;
				const pigen_semantic_instance_connection *source = resolved_fabric_port(resolver,
					(pigen_module_id){(uint32_t)m}, edge->as.fabric_connection.left_instance, edge->as.fabric_connection.left_port);
				const pigen_semantic_instance_connection *destination = resolved_fabric_port(resolver,
					(pigen_module_id){(uint32_t)m}, edge->as.fabric_connection.right_instance, edge->as.fabric_connection.right_port);
				if (!source || !destination) return fail(resolver, edge->location.source_span, "fabric endpoint requires a resolved child port");
				if (source->direction != PIGEN_SEMANTIC_OUTPUT || destination->direction != PIGEN_SEMANTIC_INPUT)
					return fail(resolver, edge->location.source_span, "fabric endpoint direction must be output to input");
				pigen_signal_id src = canonical_endpoint(model, connection_signal(model, source));
				pigen_signal_id dst = canonical_endpoint(model, connection_signal(model, destination));
				if (src.index == PIGEN_INVALID_ID || dst.index == PIGEN_INVALID_ID)
					return fail(resolver, edge->location.source_span, "fabric endpoint requires a signal binding");
				int consuming = !pigen_transfer_type_descriptor_get(model->signals[source->port_signal.index].transfer_type)->is_static;
				if (consuming && endpoint_has_readonly_ready(model, src))
					return fail(resolver, edge->location.source_span, "automatic consumer conflicts with read-only child ready binding");
				if (consuming && !validate_prior_fabric_sources(resolver,
					(pigen_module_id){(uint32_t)m}, edge, src)) return 0;
				if (model->signals[dst.index].direction == PIGEN_SEMANTIC_INPUT)
					return fail(resolver, edge->location.source_span, "fabric destination already has an upstream producer");
				for (size_t i = 0; i < model->instance_count; i++)
				{
					const pigen_semantic_instance *instance = &model->instances[i];
					if (instance->module.index != m) continue;
					for (size_t c = 0; c < instance->connection_count; c++)
					{
						const pigen_semantic_instance_connection *other = &model->instance_connections[instance->first_connection+c];
						const pigen_semantic_signal *port = &model->signals[other->port_signal.index];
						const pigen_symbol *name = pigen_symbol_get(model, port->symbol);
						if (!spelling_text_equal(resolver, other->port_name, name->name)) continue;
						pigen_signal_id signal = canonical_endpoint(model, connection_signal(model, other));
						if (consuming && signal.index == src.index && other->direction == PIGEN_SEMANTIC_INPUT &&
							!pigen_transfer_type_descriptor_get(port->transfer_type)->is_static)
							return fail(resolver, edge->location.source_span, "fabric source has another child consumer");
						if (other != destination && signal.index == dst.index &&
							other->direction == PIGEN_SEMANTIC_INPUT &&
							!pigen_transfer_type_descriptor_get(port->transfer_type)->is_static)
							return fail(resolver, edge->location.source_span,
								"fabric destination aliases another child consumer");
						if (signal.index == dst.index && other->direction == PIGEN_SEMANTIC_OUTPUT)
							return fail(resolver, edge->location.source_span, "fabric destination has another child producer");
					}
				}
				for (size_t t = 0; t < model->transfer_count; t++)
				{
					const pigen_semantic_transfer *transfer = &model->transfers[t];
					if (transfer->module.index != m) continue;
					const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(model, (pigen_transfer_id){(uint32_t)t});
					for (size_t u = 0; u < transfer->signal_use_count; u++)
					{
						pigen_signal_id signal = canonical_endpoint(model, uses[u].signal);
						if (consuming && signal.index == src.index && (uses[u].roles & PIGEN_TRANSFER_CONSUMER))
							return fail(resolver, edge->location.source_span, "fabric source has another local consumer");
						if (signal.index == dst.index && (uses[u].roles & PIGEN_TRANSFER_SIGNAL_WRITE))
							return fail(resolver, edge->location.source_span, "fabric destination has another local producer");
					}
				}
			}
		}
	}
	return 1;
}

static int validate_signal_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	size_t later_index;

	if (!validate_instance_destination_ownership(resolver) ||
		!validate_instance_consumer_ownership(resolver) ||
		!validate_fabric_ownership(resolver)) return 0;

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

/* Validate the resolved hierarchy with an explicit DFS stack, so deep but
 * acyclic designs do not consume the host call stack. */
static int validate_module_hierarchy(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	size_t modules = model->module_count;
	if (!modules || !model->instance_count) return 1;
	size_t *first = pigen_resize(NULL, modules * sizeof(*first));
	size_t *next = pigen_resize(NULL, model->instance_count * sizeof(*next));
	size_t *stack = pigen_resize(NULL, modules * sizeof(*stack));
	size_t *cursor = pigen_resize(NULL, modules * sizeof(*cursor));
	unsigned char *color = pigen_resize(NULL, modules);
	memset(color, 0, modules);
	for (size_t m = 0; m < modules; m++) first[m] = SIZE_MAX;
	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		next[i] = first[instance->module.index];
		first[instance->module.index] = i;
	}
	int valid = 1;
	for (size_t root = 0; valid && root < modules; root++)
	{
		if (color[root]) continue;
		size_t depth = 1;
		stack[0] = root;
		cursor[0] = first[root];
		color[root] = 1;
		while (valid && depth)
		{
			size_t edge = cursor[depth-1];
			if (edge == SIZE_MAX)
			{
				color[stack[--depth]] = 2;
				continue;
			}
			cursor[depth-1] = next[edge];
			const pigen_semantic_instance *instance = &model->instances[edge];
			size_t child = instance->child_module.index;
			if (color[child] == 1)
			{
				valid = fail(resolver, instance->span, "recursive module instantiation cannot be elaborated");
				break;
			}
			if (color[child] == 2) continue;
			color[child] = 1;
			stack[depth] = child;
			cursor[depth++] = first[child];
		}
	}
	free(color);
	free(cursor);
	free(stack);
	free(next);
	free(first);
	return valid;
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
		NULL, 0, 0, NULL, 0, 0, NULL, 0, 0};
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
			!add_module(&resolver, node, child, 1)) return 0;
		child = node->next_sibling;
	}
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		if (node->kind == PIGEN_SYNTAX_MODULE &&
			!add_module(&resolver, node, child, 0)) return 0;
		child = node->next_sibling;
	}
	return validate_module_hierarchy(&resolver) && validate_signal_ownership(&resolver);
}
