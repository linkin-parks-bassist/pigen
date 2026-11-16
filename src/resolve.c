/* Resolution from structured syntax into stable semantic identities. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_resolve.h"
#include "pigen/integer.h"
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
	pigen_scope_id scope;
} pending_pipeline_reset;

typedef struct {
	pigen_module_id module;
	pigen_process_id process;
	pigen_clock_domain_id domain;
	pigen_source_span name;
	pigen_source_span span;
	pigen_signal_id output;
	pigen_scope_id scope;
} resolved_pipeline_reset;

typedef struct {
	pigen_syntax_id parameter;
	pigen_expr_id value;
	pigen_syntax_expr_id expression;
	pigen_scope_id scope;
} scoped_parameter_override;

typedef struct {
	pigen_syntax_id syntax;
	pigen_process_id process;
	pigen_scope_id scope;
	size_t first_symbol,after_symbol;
} foreach_declaration;

typedef struct clocked_loop_frame {
 struct clocked_loop_frame *parent;
 pigen_expr_id broken,continued;
} clocked_loop_frame;

typedef struct {
	const pigen_syntax_tree *syntax;
	pigen_semantic_model *model;
	const pigen_resolve_policy *policy;
	pigen_semantic_error *error;
	/* Expression scope for the current statement. Normally the module scope;
	 * a pipeline field/stage scope while resolving a pipeline body. */
	pigen_scope_id expression_scope;
	size_t combinational_loop_depth;
	clocked_loop_frame *clocked_loop;
	pigen_signal_id foreach_readonly[128];
	size_t foreach_readonly_count;
	pigen_symbol_id foreach_parameters[128];
	size_t foreach_parameter_count;
	size_t native_foreach_depth,foreach_specializations;
	foreach_declaration *foreach_declarations;
	size_t foreach_declaration_count;
	int specializing;
	pigen_source_span specialization_name;
	const scoped_parameter_override *parameter_overrides;
	size_t parameter_override_count;
	/* FSM context while resolving state bodies and gotos. */
	pigen_expr_id fsm_state_expression;
	pigen_data_type_id fsm_state_type;
	const pigen_source_span *fsm_state_names;
	size_t fsm_state_count;
	/* Nonzero while resolving an FSM body. fsm_terminated is the condition
	 * under which some goto has executed in this invocation (invalid until a
	 * goto is resolved): every statement is suppressed once an earlier goto
	 * fires, so the whole-FSM early exit composes in source order
	 * (how/to/write/fsm/states.md). */
	int in_fsm;
	pigen_expr_id fsm_terminated;
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
	const pigen_token_origin *origin;

	if (!known) return spelling;
	origin = pigen_origin_get(resolver->syntax->expanded, known->origin);
	expanded_text = pigen_expanded_token_text(resolver->syntax->expanded,
		known, &expanded_length);
	origin_text = pigen_source_span_text(resolver->model->sources, spelling,
		&origin_length);
	if (!expanded_text || !origin_text ||
		(origin && origin->kind == PIGEN_ORIGIN_SOURCE && expanded_length == origin_length &&
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
	pigen_scope_id scope = resolver->expression_scope;
	pigen_expr_id value;
	const pigen_semantic_expr *expression;
	pigen_symbol_id symbol;
	pigen_parameter_id parameter;
	pigen_declare_result declared;
	const scoped_parameter_override *override=NULL;

	pigen_data_type_id parameter_type = INVALID_ID(pigen_data_type_id);
	if (syntax_node->as.parameter.has_type)
	{
		parameter_type = pigen_resolve_type(resolver->syntax, model, scope,
			syntax_node->as.parameter.data_type, resolver->error);
		if (parameter_type.index == PIGEN_INVALID_ID)
		{
			const pigen_syntax_type *type = pigen_syntax_type_get(
				&resolver->syntax->types, syntax_node->as.parameter.data_type);
			pigen_source_span base = type && type->base.index != PIGEN_INVALID_ID ?
				token_spelling(resolver, type->base) :
				(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
			size_t length = 0;
			const char *text = pigen_source_span_text(model->sources, base, &length);
			/* Bare SV int has no Pigen width argument in a parameter type. */
			if (text && length == 3 && !memcmp(text, "int", 3) && !type->has_package && !type->argument_count)
			{
				parameter_type = pigen_data_type_sized_integral(model, 32,
					type->signedness == PIGEN_SYNTAX_SIGN_UNSIGNED ? PIGEN_SIGN_UNSIGNED : PIGEN_SIGN_SIGNED,
					PIGEN_DATA_TYPE_STATE_TWO);
				if(resolver->error) resolver->error->message=NULL;
			}
			else return fail_location(resolver, syntax_node->location, "invalid parameter type");
		}
	}
	value = INVALID_ID(pigen_expr_id);
	for (size_t i=0;i<resolver->parameter_override_count;i++)
		if (resolver->parameter_overrides[i].parameter.index==syntax_id.index)
		{ override=&resolver->parameter_overrides[i];value=override->value; }
	if (syntax_node->as.parameter.has_type)
		value = pigen_resolve_assignment_value(resolver->syntax, model,
			override ? override->scope : scope,
			override ? override->expression : syntax_node->as.parameter.value,
			parameter_type, resolver->policy, resolver->error);
	else if (value.index == PIGEN_INVALID_ID)
		value = resolve_constant(resolver, scope, syntax_node->as.parameter.value);
	expression = pigen_expr_get(model, value);
	if (!expression || pigen_expr_constant(model,value).index == PIGEN_INVALID_ID)
	{
		if(resolver->error && resolver->error->message) return 0;
		return fail_location(resolver, syntax_node->location,
			"parameter value requires a compatible constant expression");
	}
	if (!syntax_node->as.parameter.has_type) parameter_type = expression->data_type;
	declared = pigen_symbol_declare(model, scope,
		PIGEN_SYMBOL_PARAMETER, parameter_type,
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

static int add_genvar(resolver *resolver, const pigen_syntax_node *node)
{
	pigen_source_span name=declaration_name_span(resolver,node->as.genvar.name);
	pigen_declare_result result=pigen_symbol_declare(resolver->model,resolver->expression_scope,
		PIGEN_SYMBOL_GENVAR,pigen_data_type_sized_logic(resolver->model,32,PIGEN_SIGN_SIGNED),
		name,name,NULL,NULL);
	return result==PIGEN_DECLARE_OK || fail_location(resolver,node->location,
		result==PIGEN_DECLARE_DUPLICATE ? "duplicate genvar declaration" : "invalid genvar declaration");
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

static pigen_source_span protocol_generated_span(pigen_semantic_model *model,
	const char *text)
{
	pigen_source_id source = pigen_source_add((pigen_source_manager *)model->sources,
		"<pigen-axi>", text, strlen(text));
	return (pigen_source_span){source, 0, strlen(text)};
}

static pigen_data_type_id protocol_bits_type(pigen_semantic_model *model,
	pigen_data_type_id data_type, size_t bits)
{
	if (pigen_data_type_state_domain(model, data_type) == PIGEN_DATA_TYPE_STATE_FOUR)
		return pigen_data_type_sized_logic(model, bits, PIGEN_SIGN_UNSIGNED);
	return pigen_data_type_unsigned_integer(model,
		pigen_const_expr_intern_integer(model, bits, pigen_data_type_unsized_integer(model)));
}

static int resolve_protocol_contract(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_protocol_occurrence *written, pigen_data_type_id data_type,
	pigen_protocol *protocol)
{
	uint64_t bits;
	pigen_semantic_model *model = resolver->model;
	if (!pigen_const_expr_evaluate_u64(model,
		pigen_data_type_packed_width(model, data_type), &bits) || bits > SIZE_MAX)
		return fail_location(resolver, written->location, "AXI data width must be concrete");
	*protocol = (pigen_protocol){.family = written->family, .data_bits = (size_t)bits,
		.address_bits = written->family == PIGEN_PROTOCOL_AXI4_STREAM ? 0 : 32};
	for (pigen_syntax_protocol_option option = 0; option < PIGEN_PROTOCOL_OPTION_COUNT; option++)
	{
		if (!(written->present & (1u << option))) continue;
		if (option >= PIGEN_PROTOCOL_OPTION_CLOCK) continue;
		pigen_expr_id expression = pigen_resolve_constant_expression(resolver->syntax,
			model, scope, written->options[option], PIGEN_LITERAL_DOMAIN_PIGEN,
			resolver->policy, resolver->error);
		if (expression.index == PIGEN_INVALID_ID) return 0;
		if (!pigen_const_expr_evaluate_u64(model, pigen_expr_constant(model, expression), &bits) ||
			bits > SIZE_MAX)
			return fail_location(resolver, written->location,
				"AXI option requires a nonnegative concrete integer");
		int stream = written->family == PIGEN_PROTOCOL_AXI4_STREAM;
		int full = written->family == PIGEN_PROTOCOL_AXI4;
		if ((option == PIGEN_PROTOCOL_OPTION_ADDR && stream) ||
			(option == PIGEN_PROTOCOL_OPTION_ID && !stream && !full) ||
			((option == PIGEN_PROTOCOL_OPTION_DEST || option == PIGEN_PROTOCOL_OPTION_USER ||
				(option >= PIGEN_PROTOCOL_OPTION_KEEP && option <= PIGEN_PROTOCOL_OPTION_LAST)) && !stream) ||
			(option >= PIGEN_PROTOCOL_OPTION_AWUSER && option <= PIGEN_PROTOCOL_OPTION_RUSER && !full))
			return fail_location(resolver, written->location,
				"AXI option is not applicable to this protocol family");
		switch (option)
		{
			case PIGEN_PROTOCOL_OPTION_ADDR: protocol->address_bits = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_ID: protocol->id_bits = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_DEST: protocol->destination_bits = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_USER: protocol->user_bits[PIGEN_CHANNEL_BEAT] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_AWUSER: protocol->user_bits[PIGEN_CHANNEL_AW] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_WUSER: protocol->user_bits[PIGEN_CHANNEL_W] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_BUSER: protocol->user_bits[PIGEN_CHANNEL_B] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_ARUSER: protocol->user_bits[PIGEN_CHANNEL_AR] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_RUSER: protocol->user_bits[PIGEN_CHANNEL_R] = (size_t)bits; break;
			case PIGEN_PROTOCOL_OPTION_KEEP:
			case PIGEN_PROTOCOL_OPTION_STRB:
			case PIGEN_PROTOCOL_OPTION_LAST:
				if (bits > 1) return fail_location(resolver, written->location,
					"AXI stream field option must be zero or one");
				if (bits) protocol->stream_fields |= option == PIGEN_PROTOCOL_OPTION_KEEP ?
					PIGEN_STREAM_KEEP : option == PIGEN_PROTOCOL_OPTION_STRB ? PIGEN_STREAM_STRB : PIGEN_STREAM_LAST;
				break;
			case PIGEN_PROTOCOL_OPTION_CLOCK:
			case PIGEN_PROTOCOL_OPTION_RESET:
			case PIGEN_PROTOCOL_OPTION_RESETN:
			case PIGEN_PROTOCOL_OPTION_COUNT: return 0;
		}
	}
	if (!pigen_protocol_is_valid(protocol))
		return fail_location(resolver, written->location, "invalid AXI protocol configuration");
	for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
	{
		pigen_protocol_channel_layout layout;
		if (!pigen_protocol_layout(protocol, channel, &layout)) continue;
		for (size_t f = 0; f < layout.field_count; f++)
			if (layout.fields[f].bits > resolver->policy->maximum_generated_bits)
				return fail_location(resolver, written->location,
					"AXI field exceeds the generated width limit");
	}
	return 1;
}

static pigen_symbol_id native_type_symbol(resolver *resolver,pigen_scope_id scope,pigen_syntax_type_id type)
{
 const pigen_syntax_type *written=pigen_syntax_type_get(&resolver->syntax->types,type);
 if(!written || written->base.index==PIGEN_INVALID_ID || written->is_packed_struct) return INVALID_ID(pigen_symbol_id);
 pigen_symbol_id symbol=pigen_resolve_type_symbol(resolver->syntax,resolver->model,scope,type);
 const pigen_symbol *known=pigen_symbol_get(resolver->model,symbol);
 return known && known->kind==PIGEN_SYMBOL_PROTOCOL_TYPE ? symbol : INVALID_ID(pigen_symbol_id);
}

static int native_type_is_plain(resolver *resolver,pigen_syntax_type_id type)
{
 const pigen_syntax_type *written=pigen_syntax_type_get(&resolver->syntax->types,type);
 return written && !written->argument_count && written->signedness==PIGEN_SYNTAX_SIGN_IMPLICIT;
}

static int add_protocol_declaration(resolver *resolver, pigen_module_id module,
	pigen_scope_id scope, const pigen_syntax_node *declaration, pigen_data_type_id data_type)
{
	pigen_semantic_model *model = resolver->model;
	pigen_protocol protocol;
	pigen_transfer_type storage = PIGEN_TRANSFER_TYPE_BUF;
	pigen_expr_id argument = INVALID_ID(pigen_expr_id);
	pigen_syntax_direction direction = declaration->as.signal_declaration.direction;
	if (direction == PIGEN_DIRECTION_INTERNAL)
		return fail_location(resolver, declaration->location,
			"AXI declarations are only allowed on module or fabric input/output ports; use child.port to access a child AXI boundary");
	if (scope.index != pigen_module_get(model, module)->scope.index)
		return fail_location(resolver, declaration->location, "AXI ports require module or fabric scope");
	if (direction == PIGEN_DIRECTION_INOUT)
		return fail_location(resolver, declaration->location,
			"AXI endpoint role uses input for target or output for initiator");
	pigen_symbol_id alias=native_type_symbol(resolver,scope,declaration->as.signal_declaration.data_type);
	pigen_protocol_type_id aliased=pigen_symbol_protocol_type(model,alias);
	if(alias.index!=PIGEN_INVALID_ID)
	{
		const pigen_protocol *contract=pigen_protocol_type_get(model,aliased);
		unsigned bindings=(1u<<PIGEN_PROTOCOL_OPTION_CLOCK)|(1u<<PIGEN_PROTOCOL_OPTION_RESET)|(1u<<PIGEN_PROTOCOL_OPTION_RESETN);
		if(!native_type_is_plain(resolver,declaration->as.signal_declaration.data_type) || !contract)
			return fail_location(resolver,declaration->location,"invalid native AXI type alias");
		if(declaration->as.signal_declaration.protocol.present & ~bindings)
			return fail_location(resolver,declaration->location,"native AXI alias use can bind clock/reset but cannot change its field contract");
		protocol=*contract;
	}
	else if (!resolve_protocol_contract(resolver, scope,
		&declaration->as.signal_declaration.protocol, data_type, &protocol)) return 0;
	if (declaration->as.signal_declaration.has_transfer_type &&
		!resolve_written_transfer(resolver, scope, declaration, &storage, &argument)) return 0;
	const pigen_transfer_type_descriptor *descriptor = pigen_transfer_type_descriptor_get(storage);
	if (!descriptor || descriptor->is_static || storage == PIGEN_TRANSFER_TYPE_PORT)
		return fail_location(resolver, declaration->location, "AXI channels require backpressure-preserving storage");
	pigen_protocol_type_id type = pigen_protocol_type_intern(model, &protocol);
	for (pigen_syntax_id id = declaration->first_child; id.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax, id);
		if (!declarator) return 0;
		pigen_shape_id shape = resolve_shape(resolver, scope, declarator);
		if (shape.index == PIGEN_INVALID_ID) return 0;
		int is_array = declarator->as.signal_declarator.dimension_count != 0;
		size_t lane_count = 1;
		if (is_array && (!pigen_protocol_array_shape_size(model, shape, &lane_count) ||
			lane_count > resolver->policy->maximum_generated_bits ||
			lane_count > SIZE_MAX / sizeof(pigen_protocol_endpoint_id) || lane_count >= PIGEN_INVALID_ID))
			return fail_location(resolver, declarator->location, "AXI endpoint array requires bounded constant dimensions");
		pigen_protocol_endpoint_id *lanes = pigen_resize(NULL, lane_count * sizeof(*lanes));
		pigen_symbol_id array_symbol = INVALID_ID(pigen_symbol_id);
		pigen_source_span name = declaration_name_span(resolver, declarator->as.signal_declarator.name);
		if (is_array && pigen_symbol_declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ARRAY,
			INVALID_ID(pigen_data_type_id), name, declaration->location.source_span, &array_symbol, NULL) != PIGEN_DECLARE_OK)
		{ free(lanes); return fail_location(resolver, declarator->location, "duplicate AXI endpoint declaration"); }
		for (size_t lane = 0; lane < lane_count; lane++)
		{
			pigen_source_span lane_name = name;
			if (is_array)
			{
				char generated[96];
				snprintf(generated, sizeof generated, "__pigen_axi_lane_%u_%zu", array_symbol.index, lane);
				lane_name = protocol_generated_span(model, generated);
			}
			pigen_semantic_protocol_endpoint endpoint = {
				.syntax = id, .module = module, .type = type,
				.role = direction == PIGEN_DIRECTION_INPUT ? PIGEN_PROTOCOL_TARGET : PIGEN_PROTOCOL_INITIATOR,
				.is_port = 1, .span = is_array ? lane_name : declaration->location.source_span
			};
			if (pigen_symbol_declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
				INVALID_ID(pigen_data_type_id), lane_name, endpoint.span, &endpoint.symbol, NULL) != PIGEN_DECLARE_OK)
				{ free(lanes); return fail_location(resolver, declarator->location, "duplicate AXI endpoint declaration"); }
			for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
			{
				pigen_protocol_channel_layout layout;
				endpoint.channels[channel] = INVALID_ID(pigen_signal_id);
				if (!pigen_protocol_layout(&protocol, channel, &layout)) continue;
				pigen_data_type_id payload = protocol_bits_type(model, data_type,
					layout.payload_bits ? layout.payload_bits : 1);
				char generated[96];
				snprintf(generated, sizeof generated, "__pigen_axi_record_%u_%u", endpoint.symbol.index, channel);
				pigen_source_span alias_name = protocol_generated_span(model, generated);
				pigen_symbol_id alias;
				if (pigen_symbol_declare(model, scope, PIGEN_SYMBOL_TYPEDEF, payload,
					alias_name, alias_name, &alias, NULL) != PIGEN_DECLARE_OK) { free(lanes); return 0; }
				pigen_data_type_id record = pigen_data_type_alias(model, alias, payload,
					PIGEN_SIGN_UNSIGNED, NULL, 0);
				for (size_t f = 0; f < layout.field_count; f++)
				{
					const pigen_protocol_field_layout *field = &layout.fields[f];
					pigen_data_type_id field_type = field->field == PIGEN_FIELD_DATA ? data_type :
						protocol_bits_type(model, data_type, field->bits);
					if (!pigen_struct_member_add(model, alias,
						protocol_generated_span(model, pigen_protocol_field_name(field->field)),
						field_type, field->offset)) { free(lanes); return 0; }
				}
				snprintf(generated, sizeof generated, "__pigen_axi_channel_%u_%u", endpoint.symbol.index, channel);
				pigen_source_span channel_name = protocol_generated_span(model, generated);
				pigen_symbol_id symbol;
				if (pigen_symbol_declare(model, scope, PIGEN_SYMBOL_SIGNAL, record,
					channel_name, channel_name, &symbol, NULL) != PIGEN_DECLARE_OK) { free(lanes); return 0; }
				pigen_protocol_role source;
				if (!pigen_protocol_channel_source(protocol.family, channel, &source)) { free(lanes); return 0; }
				pigen_semantic_direction channel_direction =
					source == endpoint.role ? PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT;
				endpoint.channels[channel] = pigen_signal_add(model, id, module, symbol, record,
					pigen_semantic_scalar_shape(model), argument, storage, channel_direction, channel_name);
				if (endpoint.channels[channel].index == PIGEN_INVALID_ID) { free(lanes); return 0; }
			}
			lanes[lane] = pigen_protocol_endpoint_add(model, &endpoint);
			if (lanes[lane].index == PIGEN_INVALID_ID)
			{ free(lanes); return fail_location(resolver, declarator->location, "invalid AXI endpoint channels"); }
		}
		if (is_array)
		{
			pigen_semantic_protocol_array array = {
				.syntax = id, .module = module, .symbol = array_symbol, .type = type,
				.role = direction == PIGEN_DIRECTION_INPUT ? PIGEN_PROTOCOL_TARGET : PIGEN_PROTOCOL_INITIATOR,
				.is_port = 1, .shape = shape,
				.span = declaration->location.source_span
			};
			if (pigen_protocol_array_add(model, &array, lanes, lane_count).index == PIGEN_INVALID_ID)
			{ free(lanes); return fail_location(resolver, declarator->location, "invalid AXI endpoint array"); }
		}
		free(lanes);
		id = declarator->next_sibling;
	}
	return 1;
}

static pigen_symbol_id protocol_binding_symbol(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_protocol_occurrence *written, pigen_syntax_protocol_option option,
	const char *default_name)
{
	if (!(written->present & (1u << option)))
		return pigen_symbol_lookup_text(resolver->model, scope, default_name, strlen(default_name));
	pigen_expr_id expression = pigen_resolve_expression(resolver->syntax, resolver->model,
		scope, written->options[option], resolver->policy, resolver->error);
	const pigen_semantic_expr *value = pigen_expr_get(resolver->model, expression);
	while (value && value->kind == PIGEN_EXPR_GROUP)
		value = pigen_expr_get(resolver->model, value->as.group.operand);
	return value && value->kind == PIGEN_EXPR_SYMBOL ? value->as.symbol : INVALID_ID(pigen_symbol_id);
}

/* Binding follows declaration collection so clocks and resets may be declared
 * after the endpoint. Protocol configuration itself remains clock-independent. */
static int bind_module_protocols(resolver *resolver, pigen_module_id module)
{
	pigen_semantic_model *model = resolver->model;
	for (size_t e = 0; e < model->protocol_endpoint_count; e++)
	{
		const pigen_semantic_protocol_endpoint *endpoint = &model->protocol_endpoints[e];
		if (endpoint->module.index != module.index) continue;
		pigen_scope_id scope = model->symbols[endpoint->symbol.index].scope;
		const pigen_syntax_node *declarator = pigen_syntax_get(resolver->syntax, endpoint->syntax);
		const pigen_syntax_node *declaration = declarator ? pigen_syntax_get(resolver->syntax, declarator->parent) : NULL;
		if (!declaration || declaration->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION) return 0;
		const pigen_syntax_protocol_occurrence *written = &declaration->as.signal_declaration.protocol;
		if ((written->present & (1u << PIGEN_PROTOCOL_OPTION_RESET)) &&
			(written->present & (1u << PIGEN_PROTOCOL_OPTION_RESETN)))
			return fail_location(resolver, written->location, "AXI endpoint cannot bind both reset and resetn");
		int active_low = (written->present & (1u << PIGEN_PROTOCOL_OPTION_RESETN)) != 0;
		pigen_symbol_id clock = protocol_binding_symbol(resolver, scope, written,
			PIGEN_PROTOCOL_OPTION_CLOCK, "clk");
		pigen_symbol_id reset = protocol_binding_symbol(resolver, scope, written,
			active_low ? PIGEN_PROTOCOL_OPTION_RESETN : PIGEN_PROTOCOL_OPTION_RESET, "reset");
		pigen_clock_domain_id domain = pigen_clock_domain_intern(model, clock, PIGEN_SEMANTIC_POSEDGE);
		if (!pigen_protocol_endpoint_bind(model, (pigen_protocol_endpoint_id){(uint32_t)e}, domain, reset, active_low))
			return fail_location(resolver, written->location,
				"AXI clock/reset bindings require scalar static module inputs; use clock= and reset= or resetn=");
	}
	return 1;
}

static int resolve_expr_references_symbol(const pigen_semantic_model *model,
 pigen_expr_id expression,pigen_symbol_id symbol);

static int foreach_iterator_in_extent(resolver *resolver,pigen_syntax_location location);

static int signal_is_iteration_variable(resolver *resolver,const pigen_semantic_signal *signal)
{
 const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,signal->syntax);
 if(!node) return 0;
 if(node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH) return 1;
 if(node->kind!=PIGEN_SYNTAX_SIGNAL_DECLARATOR) return 0;
 const pigen_syntax_node *declaration=pigen_syntax_get(resolver->syntax,node->parent);
 const pigen_syntax_node *header=declaration ? pigen_syntax_get(resolver->syntax,declaration->parent) : NULL;
 const pigen_syntax_node *loop=header ? pigen_syntax_get(resolver->syntax,header->parent) : NULL;
 return loop && loop->kind==PIGEN_SYNTAX_PROCEDURAL_FOR && loop->first_child.index==declaration->parent.index;
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
	pigen_symbol_id native=native_type_symbol(resolver,scope,syntax_node->as.signal_declaration.data_type);
	const pigen_symbol *alias=pigen_symbol_get(model,native);
	data_type = alias ? alias->data_type : pigen_resolve_type(resolver->syntax, model, scope,
		syntax_node->as.signal_declaration.data_type, resolver->error);
	if (data_type.index == PIGEN_INVALID_ID) return 0;
	if(alias && syntax_node->as.signal_declaration.protocol.family==PIGEN_PROTOCOL_NONE)
		return fail_location(resolver,syntax_node->location,"native AXI alias requires a typed endpoint declaration");
	if (syntax_node->as.signal_declaration.protocol.family != PIGEN_PROTOCOL_NONE)
		return add_protocol_declaration(resolver, module_id, scope, syntax_node, data_type);
	if (pigen_data_type_is_void(model, data_type))
		return fail_location(resolver, syntax_node->location, "void DATA requires a native AXI-Stream endpoint");
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
  int automatic=syntax_node->as.signal_declaration.is_automatic;
  model->signals[signal.index].is_automatic=automatic;
  if(declarator->as.signal_declarator.has_initializer)
  {
   if(!pigen_scope_get(model,scope)->is_procedural || !pigen_transfer_type_descriptor_get(transfer_type)->is_static || transfer_type==PIGEN_TRANSFER_TYPE_WIRE)
    return fail_location(resolver,declarator->location,"initializer requires a static procedural variable");
   const pigen_syntax_expr *written=pigen_syntax_expr_get(&resolver->syntax->expressions,declarator->as.signal_declarator.initializer);
   if(!automatic && resolver->native_foreach_depth && written && foreach_iterator_in_extent(resolver,written->location))
    return fail_location(resolver,declarator->location,"static initializer cannot capture a native foreach iteration variable");
   pigen_expr_id value=pigen_resolve_shaped_assignment_value(resolver->syntax,model,scope,
    declarator->as.signal_declarator.initializer,data_type,shape,resolver->policy,resolver->error);
   const pigen_semantic_expr *expression=pigen_expr_get(model,value);
   if(!expression) return 0;
   if(expression->shape.index!=shape.index)
    return fail_location(resolver,declarator->location,"static initializer requires a compatible shape");
   for(size_t i=0;i<model->signal_count;i++)
    if(!automatic && (signal_is_iteration_variable(resolver,&model->signals[i]) || model->signals[i].is_automatic) &&
       resolve_expr_references_symbol(model,value,model->signals[i].symbol))
     return fail_location(resolver,declarator->location,"static initializer cannot capture an automatic or procedural iteration variable");
   pigen_expression_use_analysis uses={0};
   int valid=pigen_analyze_expression_uses(model,value,pigen_predicate_true(model),PIGEN_EXPRESSION_USE_READ,&uses);
   for(size_t i=0;valid && i<uses.signal_count;i++)
   {
    const pigen_semantic_signal *read=pigen_signal_get(model,uses.signals[i].signal);
    if(!read || !pigen_transfer_type_descriptor_get(read->transfer_type)->is_static) valid=0;
   }
   pigen_free_expression_use_analysis(&uses);
   if(!valid) return fail_location(resolver,declarator->location,"static initializer buffered observations require peek");
   if(automatic) model->signals[signal.index].entry_value=value;
   else model->signals[signal.index].initial_value=value;
  }
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

static int add_import(resolver *resolver,pigen_scope_id scope,const pigen_syntax_node *node)
{
 if(!node->as.package_import.is_structured) return 1;
 pigen_symbol_id package=pigen_symbol_lookup(resolver->model,scope,token_spelling(resolver,node->as.package_import.package));
 pigen_declare_result result=pigen_import_package(resolver->model,scope,package,
  token_spelling(resolver,node->as.package_import.member),node->as.package_import.wildcard,node->location.extent.first.index,node->location.source_span);
 return result==PIGEN_DECLARE_OK || fail_location(resolver,node->location,
  result==PIGEN_DECLARE_DUPLICATE ? "package import conflicts with an existing declaration or import" : "package import requires a declared package and exported member");
}

/* Procedural declarations belong to their nearest lexical block/loop. During
 * declaration collection those scopes do not exist yet; consult syntax barriers
 * rather than publishing placeholder symbols into the module namespace. */
static int import_use_has_local(resolver *resolver,pigen_scope_id scope,const pigen_syntax_node *process,
 pigen_token_id name,size_t position)
{
 for(size_t i=0;i<resolver->syntax->node_count;i++)
 {
  const pigen_syntax_node *local=&resolver->syntax->nodes[i];
  if(local->location.extent.first.index<process->location.extent.first.index ||
   local->location.extent.after.index>process->location.extent.after.index) continue;
  pigen_token_id declared=INVALID_ID(pigen_token_id);
  if(local->kind==PIGEN_SYNTAX_SIGNAL_DECLARATOR) declared=local->as.signal_declarator.name;
  else if(local->kind==PIGEN_SYNTAX_PARAMETER) declared=local->as.parameter.name;
  else if(local->kind==PIGEN_SYNTAX_TYPEDEF) declared=local->as.type_definition.name;
  else if(local->kind==PIGEN_SYNTAX_FOREACH_INDEX) declared=local->as.foreach_index.name;
  else if(local->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK && local->as.procedural_block.has_name)
   declared=local->as.procedural_block.name;
  else if(local->kind==PIGEN_SYNTAX_IMPORT && local->as.package_import.is_structured)
  {
   if(!local->as.package_import.wildcard) declared=local->as.package_import.member;
   else
   {
    if(pigen_symbol_lookup_package_member(resolver->model,scope,
     token_spelling(resolver,local->as.package_import.package),token_spelling(resolver,name)).index!=PIGEN_INVALID_ID)
     declared=name;
   }
  }
  if(declared.index==PIGEN_INVALID_ID || local->location.extent.first.index>position || declared.index>position ||
   !spelling_text_equal(resolver,token_spelling(resolver,declared),token_spelling(resolver,name))) continue;
  const pigen_syntax_node *owner=local->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK ?
   pigen_syntax_get(resolver->syntax,local->parent) : local;
  while(owner && owner!=process && owner->kind!=PIGEN_SYNTAX_PROCEDURAL_BLOCK &&
   owner->kind!=PIGEN_SYNTAX_PROCEDURAL_FOR && owner->kind!=PIGEN_SYNTAX_PROCEDURAL_FOREACH)
   owner=pigen_syntax_get(resolver->syntax,owner->parent);
  /* For-header declarations are children of a synthetic initializer block,
   * but their lexical lifetime covers the entire loop. */
  if(owner && owner->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK)
  {
   const pigen_syntax_node *parent=pigen_syntax_get(resolver->syntax,owner->parent);
   if(parent && parent->kind==PIGEN_SYNTAX_PROCEDURAL_FOR &&
    pigen_syntax_get(resolver->syntax,parent->first_child)==owner) owner=parent;
  }
  if(owner && position>=owner->location.extent.first.index && position<owner->location.extent.after.index) return 1;
 }
 return 0;
}

/* Bind imported names in module-scope expressions at their authored point,
 * before the declaration pass reaches a later import/parameter. This pass
 * publishes no values, reads or hardware; syntax barriers protect procedural locals. */
static void bind_import_uses(resolver *resolver,pigen_scope_id scope,const pigen_syntax_node *node)
{
 if(!resolver->model->import_count) return;
 size_t first=node->location.extent.first.index,after=node->location.extent.after.index;
 for(size_t i=0;i<resolver->syntax->expressions.node_count;i++)
 {
  const pigen_syntax_expr *expression=&resolver->syntax->expressions.nodes[i];
  if(expression->kind!=PIGEN_SYNTAX_EXPR_NAME || expression->location.extent.first.index<first || expression->location.extent.after.index>after) continue;
  if(import_use_has_local(resolver,scope,node,expression->as.atom.token,expression->location.extent.first.index)) continue;
  const pigen_expanded_token *token=pigen_expanded_token_get(resolver->syntax->expanded,expression->as.atom.token);
  size_t n=0;const char *name=pigen_expanded_token_text(resolver->syntax->expanded,token,&n);
  if(name)
  {
   static const char *const builtins[]={"peek","valid","ready","accepts","$bits","$left","$right","$low","$high",
    "$increment","$size","$dimensions","$unpacked_dimensions","$signed","$unsigned","$clog2"};
   int builtin=0;
   for(size_t b=0;b<sizeof(builtins)/sizeof(*builtins);b++)
    if(strlen(builtins[b])==n && !memcmp(name,builtins[b],n)) {builtin=1;break;}
   if(builtin)
   {
    int callee=0;
    for(size_t call=0;call<resolver->syntax->expressions.node_count;call++)
     if(resolver->syntax->expressions.nodes[call].kind==PIGEN_SYNTAX_EXPR_CALL &&
      resolver->syntax->expressions.nodes[call].as.call.callee.index==i) {callee=1;break;}
    if(callee)
    {
     const pigen_symbol *target=pigen_symbol_get(resolver->model,pigen_symbol_lookup_text_at(resolver->model,scope,
      name,n,expression->location.extent.first.index));
     if(!target || target->kind!=PIGEN_SYMBOL_RETAINED_SUBROUTINE) continue;
    }
   }
   (void)pigen_symbol_resolve_text_at(resolver->model,scope,name,n,expression->location.extent.first.index);
  }
 }
 for(size_t i=0;i<resolver->syntax->types.node_count;i++)
 {
  const pigen_syntax_type *type=&resolver->syntax->types.nodes[i];
  if(type->location.extent.first.index>=first && type->location.extent.after.index<=after &&
   (type->has_package || !import_use_has_local(resolver,scope,node,type->base,type->location.extent.first.index)))
   (void)pigen_resolve_type_symbol(resolver->syntax,resolver->model,scope,(pigen_syntax_type_id){(uint32_t)i});
 }
}

static int add_typedef(resolver *resolver, pigen_scope_id scope,
	const pigen_syntax_node *syntax_node)
{
	pigen_symbol_id original=native_type_symbol(resolver,scope,syntax_node->as.type_definition.type);
	const pigen_symbol *native=pigen_symbol_get(resolver->model,original);
	pigen_data_type_id underlying = native ? native->data_type : pigen_resolve_type(resolver->syntax, resolver->model, scope,
		syntax_node->as.type_definition.type, resolver->error);
	if (!native && syntax_node->as.type_definition.protocol.family == PIGEN_PROTOCOL_NONE &&
		pigen_data_type_is_void(resolver->model, underlying))
		return fail_location(resolver, syntax_node->location, "void DATA aliases require a native AXI-Stream protocol; typedef void axi4_stream instead");
	if(native || syntax_node->as.type_definition.protocol.family!=PIGEN_PROTOCOL_NONE)
	{
		pigen_protocol_type_id type=pigen_symbol_protocol_type(resolver->model,original);
		unsigned bindings=(1u<<PIGEN_PROTOCOL_OPTION_CLOCK)|(1u<<PIGEN_PROTOCOL_OPTION_RESET)|(1u<<PIGEN_PROTOCOL_OPTION_RESETN);
		const pigen_syntax_protocol_occurrence *written=&syntax_node->as.type_definition.protocol;
		if(underlying.index==PIGEN_INVALID_ID) return 0;
		if(written->present & bindings)
			return fail_location(resolver,syntax_node->location,"native AXI type aliases bind field contracts; clock/reset belong to endpoint declarations");
		if(native)
		{
			if(!native_type_is_plain(resolver,syntax_node->as.type_definition.type) || written->present)
				return fail_location(resolver,syntax_node->location,"native AXI alias cannot change the aliased field contract");
		}
		else
		{
			pigen_protocol contract;
			if(!resolve_protocol_contract(resolver,scope,written,underlying,&contract)) return 0;
			type=pigen_protocol_type_intern(resolver->model,&contract);
		}
		pigen_symbol_id symbol;
		pigen_declare_result declared=pigen_symbol_declare(resolver->model,scope,PIGEN_SYMBOL_PROTOCOL_TYPE,
			underlying,declaration_name_span(resolver,syntax_node->as.type_definition.name),syntax_node->location.source_span,&symbol,NULL);
		if(declared!=PIGEN_DECLARE_OK)
			return fail_location(resolver,syntax_node->location,declared==PIGEN_DECLARE_DUPLICATE ? "duplicate typedef name" : "invalid native AXI typedef");
		if(!pigen_symbol_bind_protocol_type(resolver->model,symbol,type))
			return fail_location(resolver,syntax_node->location,"invalid native AXI type contract binding");
		return 1;
	}
	pigen_symbol_id symbol;
	pigen_declare_result declared;

	if (underlying.index == PIGEN_INVALID_ID) return 0;
	declared = pigen_symbol_declare(resolver->model, scope, PIGEN_SYMBOL_TYPEDEF,
		underlying, declaration_name_span(resolver,
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

static int has_record_diagnostic(resolver *resolver, pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *written=pigen_syntax_expr_get(&resolver->syntax->expressions, expression);
	while (written && written->kind==PIGEN_SYNTAX_EXPR_GROUP)
		written=pigen_syntax_expr_get(&resolver->syntax->expressions, written->as.group.operand);
	if (!written || !resolver->error || !resolver->error->message) return 0;
	if (written->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL)
		return has_record_diagnostic(resolver, written->as.conditional.when_true) ||
			has_record_diagnostic(resolver, written->as.conditional.when_false);
	if (written->kind == PIGEN_SYNTAX_EXPR_MEMBER)
		return has_record_diagnostic(resolver, written->as.member.base);
	if (written->kind == PIGEN_SYNTAX_EXPR_INDEX)
		return has_record_diagnostic(resolver, written->as.index.base);
	if (written->kind == PIGEN_SYNTAX_EXPR_SELECT)
		return has_record_diagnostic(resolver, written->as.select.base);

	if (written->kind == PIGEN_SYNTAX_EXPR_CAST)
		return has_record_diagnostic(resolver, written->as.cast.value);
	if (written->kind == PIGEN_SYNTAX_EXPR_UNARY)
		return has_record_diagnostic(resolver, written->as.unary.operand);
	if (written->kind == PIGEN_SYNTAX_EXPR_BINARY)
		return has_record_diagnostic(resolver, written->as.binary.left) ||
			has_record_diagnostic(resolver, written->as.binary.right);
	if (written->kind == PIGEN_SYNTAX_EXPR_CONCATENATION || written->kind == PIGEN_SYNTAX_EXPR_REPLICATION)
	{
		size_t first = written->kind == PIGEN_SYNTAX_EXPR_CONCATENATION ? written->as.sequence.first_child : written->as.replication.first_child;
		size_t count = written->kind == PIGEN_SYNTAX_EXPR_CONCATENATION ? written->as.sequence.child_count : written->as.replication.child_count;
		const pigen_syntax_expr_id *children = pigen_syntax_expr_children(&resolver->syntax->expressions, first, count);
		for (size_t i=0; children && i<count; i++)
			if (has_record_diagnostic(resolver, children[i])) return 1;
	}

	return written->kind == PIGEN_SYNTAX_EXPR_RECORD;
}

static int validate_protocol_copy(resolver *resolver, pigen_lvalue_id destination,
	pigen_expr_id value, pigen_syntax_expr_id written_value, pigen_syntax_location location)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model, destination);
	if (!lvalue || lvalue->kind != PIGEN_LVALUE_PROJECTION) return 1;
	pigen_protocol_channel destination_channel;
	pigen_protocol_endpoint_id destination_id = pigen_protocol_endpoint_for_signal(model,
		lvalue->as.projection.signal, &destination_channel);
	const pigen_semantic_protocol_endpoint *dest = pigen_protocol_endpoint_get(model, destination_id);
	if (!dest) return 1;
	const pigen_syntax_expr *written = pigen_syntax_expr_get(&resolver->syntax->expressions, written_value);
	while (written && written->kind == PIGEN_SYNTAX_EXPR_GROUP)
		written = pigen_syntax_expr_get(&resolver->syntax->expressions, written->as.group.operand);
	if (written && written->kind == PIGEN_SYNTAX_EXPR_CAST) return 1;
	const pigen_semantic_expr *source = pigen_expr_get(model, value);
	while (source && (source->kind == PIGEN_EXPR_CONVERSION || source->kind == PIGEN_EXPR_GROUP || source->kind == PIGEN_EXPR_PEEK))
		source = pigen_expr_get(model, source->kind == PIGEN_EXPR_CONVERSION ? source->as.conversion.operand :
			source->kind == PIGEN_EXPR_GROUP ? source->as.group.operand : source->as.peek.source);
	if (!source) return 0;
	if (source->kind == PIGEN_EXPR_CONDITIONAL)
	{
		pigen_expr_id when_true = source->as.conditional.when_true;
		pigen_expr_id when_false = source->as.conditional.when_false;
		pigen_syntax_expr_id true_syntax = written && written->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL ?
			written->as.conditional.when_true : (pigen_syntax_expr_id){PIGEN_INVALID_ID};
		pigen_syntax_expr_id false_syntax = written && written->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL ?
			written->as.conditional.when_false : (pigen_syntax_expr_id){PIGEN_INVALID_ID};
		return validate_protocol_copy(resolver, destination, when_true, true_syntax, location) &&
			validate_protocol_copy(resolver, destination, when_false, false_syntax, location);
	}

	if (source->kind == PIGEN_EXPR_SYMBOL)
	{
		pigen_protocol_channel source_channel;
		pigen_protocol_endpoint_id source_id = pigen_protocol_endpoint_for_signal(model,
			pigen_symbol_signal(model, source->as.symbol), &source_channel);
		const pigen_semantic_protocol_endpoint *src = pigen_protocol_endpoint_get(model, source_id);
		if (src)
		{
			if (src->binding.is_bound && dest->binding.is_bound &&
				(src->binding.domain.index != dest->binding.domain.index ||
				 src->binding.reset_symbol.index != dest->binding.reset_symbol.index ||
				 src->binding.reset_active_low != dest->binding.reset_active_low))
				return fail_location(resolver, location, "AXI channel copy requires compatible clock domains and reset bindings");
			if (source_channel == destination_channel && pigen_protocol_equal(
				pigen_protocol_type_get(model, src->type), pigen_protocol_type_get(model, dest->type))) return 1;
			return fail_location(resolver, location, "AXI channel copy requires compatible protocol contracts");
		}
		const pigen_semantic_signal *signal = pigen_signal_get(model, lvalue->as.projection.signal);
		if (signal && source->data_type.index == signal->data_type.index) return 1;
		return fail_location(resolver, location, "AXI channel requires a typed channel or explicit record construction");
	}
	if (source->kind == PIGEN_EXPR_SELECT || source->kind == PIGEN_EXPR_INDEX)
		return fail_location(resolver, location, "AXI channel requires a complete record; construct all fields explicitly");
	return 1;
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
	size_t guard_atom_count = predicate ? predicate->atom_count : 0;
	size_t i;
	size_t capacity = 0;
	int valid = predicate && pigen_analyze_lvalue_uses(resolver->model,
		destination, guard,
		&destination_uses) &&
		pigen_analyze_expression_uses(resolver->model, value, guard,
			PIGEN_EXPRESSION_USE_READ, &value_uses);
	/* Conditional data expressions may relocate both predicate arenas. */
	for (i = 0; valid && i < guard_atom_count; i++)
	{
		const pigen_predicate_atom *atoms = pigen_predicate_atoms(resolver->model, guard);
		if (!atoms) { valid = 0; break; }
		pigen_expr_id condition = atoms[i].condition;
		pigen_predicate_id unconditional = pigen_predicate_true(resolver->model);
		valid = pigen_analyze_expression_uses(resolver->model, condition,
			unconditional, PIGEN_EXPRESSION_USE_READ, &guard_uses);
	}

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
	pigen_signal_id output, pigen_scope_id pipeline_scope)
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

		pigen_scope_id scope=model->symbols[field->symbol.index].scope;
		while(pigen_scope_get(model,scope) && scope.index!=pipeline_scope.index)
			scope=model->scopes[scope.index].parent;
		if (field->module.index == module_id.index && scope.index==pipeline_scope.index &&
			!pigen_transfer_type_descriptor_get(field->transfer_type)->is_static &&
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
	if (pigen_pipeline_identifier_establish_in_scope(model, pipeline_id, module_id, saved_scope,
		pipeline, resolver->syntax->expanded, &ident) != 0)
		return fail_location(resolver, pipeline->location,
			"cannot establish the pipeline output identity");
	pipeline_scope = pigen_scope_add(model, saved_scope,
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
				!spelling_text_equal(resolver, reset->name, pipeline_name) ||
				pigen_symbol_lookup(model,reset->scope,reset->name).index!=ident.symbol.index)
				continue;
			if (!apply_pipeline_reset(resolver, module_id, process, domain,
				reset, ident.signal, pipeline_scope))
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
				pipeline_name, pipeline->location.source_span, ident.signal, pipeline_scope};
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
	pigen_predicate_id guard, pigen_procedural_id *checks);
static int syntax_has_qualified_control(const pigen_syntax_tree *tree,
	pigen_syntax_id id,size_t depth);
static pigen_expr_id case_binary(pigen_semantic_model *model,pigen_binary_operator op,
 pigen_expr_id left,pigen_expr_id right,pigen_source_span span);
static int resolve_clocked_foreach(resolver *resolver,pigen_module_id module,
 pigen_process_id process,pigen_clock_domain_id domain,pigen_syntax_id syntax,
 const pigen_syntax_node *node,pigen_predicate_id guard,pigen_procedural_id *checks);
static int resolve_clocked_case(resolver *resolver,pigen_module_id module,
	pigen_process_id process,pigen_clock_domain_id domain,
	const pigen_syntax_node *node,pigen_predicate_id guard,
	pigen_procedural_id *checks);

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
	pigen_predicate_id guard);

/* The condition expression of PREDICATE: the conjunction of its atoms, where a
 * false atom is the negated condition. The true predicate is the constant 1.
 * The expression is used as one guard atom, so an FSM can suppress everything
 * after an executed goto (how/to/write/fsm/states.md). */
static pigen_expr_id predicate_expression(resolver *resolver,
	pigen_predicate_id predicate, pigen_source_span span)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_predicate *known = pigen_predicate_get(model, predicate);
	const pigen_predicate_atom *atoms;
	pigen_expr_id result = INVALID_ID(pigen_expr_id);
	size_t i;

	if (!known) return INVALID_ID(pigen_expr_id);
	if (known->impossible)
		return pigen_expr_add_integer(model, 0,
			pigen_data_type_boolean(model), span);
	atoms = known->atom_count ? pigen_predicate_atoms(model, predicate) : NULL;
	if (known->atom_count && !atoms) return INVALID_ID(pigen_expr_id);
	for (i = 0; i < known->atom_count; i++)
	{
		pigen_expr_id term = atoms[i].condition;

		if (!atoms[i].expected)
		{
			const pigen_semantic_expr *condition = pigen_expr_get(model,
				term);
			pigen_unary_resolution resolution;

			if (!condition ||
				!pigen_data_type_resolve_unary_operation(model,
					PIGEN_UNARY_LOGICAL_NOT, condition->data_type,
					&resolution))
				return INVALID_ID(pigen_expr_id);
			term = pigen_expr_add_unary(model, resolution.operation, term,
				span);
			if (term.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_expr_id);
		}
		if (result.index == PIGEN_INVALID_ID)
		{
			result = term;
			continue;
		}
		{
			const pigen_semantic_expr *left = pigen_expr_get(model,
				result);
			const pigen_semantic_expr *right = pigen_expr_get(model, term);
			pigen_binary_resolution resolution;

			if (!left || !right ||
				!pigen_data_type_resolve_binary_operation(model,
					PIGEN_BINARY_LOGICAL_AND, left->data_type,
					right->data_type, &resolution))
				return INVALID_ID(pigen_expr_id);
			result = pigen_expr_add_binary(model, resolution.operation,
				result, term, span);
			if (result.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_expr_id);
		}
	}
	if (result.index == PIGEN_INVALID_ID)
		result = pigen_expr_add_integer(model, 1,
			pigen_data_type_boolean(model), span);
	return result;
}

/* Loop exits affect leaves in their lexical loop, including nested loops.
 * A continue flag lasts one specialization; a break lasts the whole loop. */
static pigen_predicate_id clocked_loop_guard(resolver *resolver,
 pigen_predicate_id guard,pigen_source_span span)
{
 (void)span;
 for(clocked_loop_frame *loop=resolver->clocked_loop;loop;loop=loop->parent)
 {
  if(loop->broken.index!=PIGEN_INVALID_ID)
   guard=pigen_predicate_and_condition(resolver->model,guard,loop->broken,0);
  if(loop->continued.index!=PIGEN_INVALID_ID)
   guard=pigen_predicate_and_condition(resolver->model,guard,loop->continued,0);
  if(guard.index==PIGEN_INVALID_ID) return guard;
 }
 return guard;
}

/* Gate effectful leaves by lexical loop exits and the first FSM goto.
 * Control-entry checks capture their own liveness snapshot before resolving
 * a body; exits in that body must not cancel the already entered check. */
static pigen_predicate_id statement_guard(resolver *resolver,
	pigen_predicate_id guard, pigen_source_span span)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_expr *known;
	pigen_unary_resolution resolution;
	pigen_expr_id live;

	guard=clocked_loop_guard(resolver,guard,span);
	if(guard.index==PIGEN_INVALID_ID) return guard;
	if (!resolver->in_fsm ||
		resolver->fsm_terminated.index == PIGEN_INVALID_ID)
		return guard;
	known = pigen_expr_get(model, resolver->fsm_terminated);
	if (!known || !pigen_data_type_resolve_unary_operation(model,
		PIGEN_UNARY_LOGICAL_NOT, known->data_type, &resolution))
		return INVALID_ID(pigen_predicate_id);
	live = pigen_expr_add_unary(model, resolution.operation,
		resolver->fsm_terminated, span);
	if (live.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_predicate_id);
	return pigen_predicate_and_condition(model, guard, live, 1);
}

static int resolve_clocked_loop_exit(resolver *resolver,const pigen_syntax_node *node,
 pigen_predicate_id guard)
{
 if(!resolver->clocked_loop)
  return fail_location(resolver,node->location,"clocked break/continue requires an enclosing foreach loop");
 pigen_semantic_model *model=resolver->model;pigen_source_span span=node->location.source_span;
 /* Exit flags are monotone within their lifetime. Re-recording an exit
  * after break, an enclosing exit or goto cannot affect a live leaf. Do
  * not fold those flags into their own triggers: that duplicates the
  * accumulated expression at every iteration. A break must still honor
  * this iteration's earlier continue, which skips a later break. */
 if(node->kind==PIGEN_SYNTAX_PROCEDURAL_BREAK && resolver->clocked_loop->continued.index!=PIGEN_INVALID_ID)
  guard=pigen_predicate_and_condition(model,guard,resolver->clocked_loop->continued,0);
 pigen_expr_id executed=predicate_expression(resolver,guard,span);
 if(executed.index==PIGEN_INVALID_ID) return fail_location(resolver,node->location,"invalid clocked loop exit condition");
 pigen_expr_id *flag=node->kind==PIGEN_SYNTAX_PROCEDURAL_BREAK ?
  &resolver->clocked_loop->broken : &resolver->clocked_loop->continued;
 if(flag->index!=PIGEN_INVALID_ID) executed=case_binary(model,PIGEN_BINARY_LOGICAL_OR,*flag,executed,span);
 if(executed.index==PIGEN_INVALID_ID) return fail_location(resolver,node->location,"invalid clocked loop exit condition");
 *flag=executed;return 1;
}

/* Record the goto condition GUARD with loop liveness, before FSM termination
 * is applied: if any of the gotos resolved so far executes, this invocation
 * ends for the current edge. fsm_terminated is the disjunction of those
 * triggers, so the first executed goto wins by suppressing everything later
 * in source order (how/to/write/fsm/states.md). */
static int consume_goto(resolver *resolver, pigen_predicate_id guard,
	pigen_source_span span)
{
	pigen_semantic_model *model = resolver->model;
	pigen_expr_id executed;

	if (!resolver->in_fsm) return 1;
	executed = predicate_expression(resolver, guard, span);
	if (executed.index == PIGEN_INVALID_ID)
		return fail(resolver, span, "invalid fsm goto condition");
	if (resolver->fsm_terminated.index == PIGEN_INVALID_ID)
	{
		resolver->fsm_terminated = executed;
		return 1;
	}
	{
		const pigen_semantic_expr *left = pigen_expr_get(model,
			resolver->fsm_terminated);
		const pigen_semantic_expr *right = pigen_expr_get(model, executed);
		pigen_binary_resolution resolution;
		pigen_expr_id merged;

		if (!left || !right ||
			!pigen_data_type_resolve_binary_operation(model,
				PIGEN_BINARY_LOGICAL_OR, left->data_type,
				right->data_type, &resolution))
			return fail(resolver, span, "invalid fsm goto condition");
		merged = pigen_expr_add_binary(model, resolution.operation,
			resolver->fsm_terminated, executed, span);
		if (merged.index == PIGEN_INVALID_ID)
			return fail(resolver, span, "invalid fsm goto condition");
		resolver->fsm_terminated = merged;
	}
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
			int ok;
			pigen_predicate_id effective;

			value = pigen_expr_add_integer(model, (uint64_t)i,
				resolver->fsm_state_type, goto_node->location.source_span);
			if (value.index == PIGEN_INVALID_ID)
				return fail(resolver, goto_node->location.source_span,
					"invalid fsm goto value");
			effective = statement_guard(resolver, guard,
				goto_node->location.source_span);
			if (effective.index == PIGEN_INVALID_ID)
				return fail(resolver, goto_node->location.source_span,
					"invalid fsm goto condition");
			ok = add_state_transfer(resolver, module_id, process, domain,
				syntax_id, value, effective,
				goto_node->location.source_span);
			if (!ok) return 0;
			/* The first executed goto ends this FSM invocation for the
			 * current edge; later statements are suppressed by the
			 * terminated condition (how/to/write/fsm/states.md). The
			 * recorded trigger includes enclosing loop liveness; repeated
			 * disjunction is harmless. */
			return consume_goto(resolver, clocked_loop_guard(resolver,guard,goto_node->location.source_span),
				goto_node->location.source_span);
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
	resolver->foreach_specializations=0;
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_expr_id clock;
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_data_type_id state_type;
	pigen_symbol_id state_symbol;
	pigen_signal_id state_signal;
	pigen_expr_id state_expression;
	pigen_syntax_id child;
	pigen_source_span *names = NULL;
	pigen_source_span register_name;
	size_t state_count = 0;
	size_t index = 0;
	pigen_scope_id saved_scope;
	pigen_scope_id scope = resolver->expression_scope;
	int ok = 1;
	int qualified=syntax_has_qualified_control(resolver->syntax,syntax_id,0);
	pigen_procedural_id *check_children=NULL;
	size_t check_count=0;

	if (!module)
		return fail(resolver, fsm_node->location.source_span,
			"fsm requires a module");
	clock = pigen_resolve_expression(resolver->syntax, model, scope,
		fsm_node->as.fsm.clock, resolver->policy, resolver->error);
	{
		const pigen_semantic_expr *clock_expr = pigen_expr_get(model, clock);

		if (!clock_expr || clock_expr->kind != PIGEN_EXPR_SYMBOL ||
			pigen_symbol_signal(model, clock_expr->as.symbol).index ==
				PIGEN_INVALID_ID)
			return fail(resolver, fsm_node->location.source_span,
				"fsm clock requires one static signal name");
		const pigen_semantic_signal *clock_signal=pigen_signal_get(model,pigen_symbol_signal(model,clock_expr->as.symbol));
		if(!clock_signal || !pigen_transfer_type_descriptor_get(clock_signal->transfer_type)->is_static)
			return fail(resolver,fsm_node->location.source_span,"fsm clock requires one static signal name");
		domain = pigen_clock_domain_intern(model, clock_expr->as.symbol,
			PIGEN_SEMANTIC_POSEDGE);
		if (domain.index == PIGEN_INVALID_ID)
			return fail(resolver, fsm_node->location.source_span,
				"invalid fsm clock domain");
	}

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
	/* The state register is the named FSM's lexical-scope identifier, or a
	 * synthetic internal signal for an anonymous FSM (`fsm @(posedge clk)`). */
	if (fsm_node->as.fsm.has_name)
		register_name = token_spelling(resolver, fsm_node->as.fsm.name);
	else
	{
		char name[64];
		pigen_source_id source;

		snprintf(name, sizeof name, "__pigen_fsm_%u",
			(unsigned)syntax_id.index);
		source = pigen_source_add((pigen_source_manager *)model->sources,
			"<pigen-fsm>", name, strlen(name));
		register_name = (pigen_source_span){source, 0, strlen(name)};
	}
	if (register_name.start == register_name.end)
	{
		free(names);
		return fail(resolver, fsm_node->location.source_span,
			"invalid fsm state register name");
	}
	{
		pigen_declare_result declared = pigen_symbol_declare(model,
			scope, PIGEN_SYMBOL_SIGNAL, state_type, register_name,
			register_name, &state_symbol, NULL);

		if (declared != PIGEN_DECLARE_OK)
		{
			free(names);
			return fail(resolver, fsm_node->location.source_span,
				"cannot declare the fsm state register");
		}
		state_signal = pigen_signal_add(model, syntax_id, module_id,
			state_symbol, state_type, pigen_semantic_scalar_shape(model),
			INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, register_name);
		if (state_signal.index == PIGEN_INVALID_ID)
		{
			free(names);
			return fail(resolver, fsm_node->location.source_span,
				"invalid fsm state register");
		}
		state_expression = pigen_expr_add_symbol(model, state_symbol,
			state_type, register_name);
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

	/* Resolve the body in source order: level-zero ordinary statements run
	 * unconditionally; `state identifier : statement` runs only while that
	 * state is active. Reset is ordinary code (`if (reset) goto initialise;`)
	 * whose goto terminates this invocation via the first-goto rule, so no
	 * special reset priority is applied here (how/to/write/fsm/states.md). */
	saved_scope = resolver->expression_scope;
	resolver->expression_scope = scope;
	resolver->in_fsm = 1;
	resolver->fsm_terminated = INVALID_ID(pigen_expr_id);
	for (child = fsm_node->first_child; ok &&
		child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax,
			child);

		if (!node) break;
		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
		{
			pigen_expr_id state_const;
			pigen_binary_resolution equality_resolution;
			pigen_expr_id condition;
			pigen_predicate_id state_guard;
			pigen_procedural_id checks=INVALID_ID(pigen_procedural_id);

			state_const = pigen_expr_add_integer(model, (uint64_t)index,
				state_type, node->location.source_span);
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
				state_const, node->location.source_span);
			if (condition.index == PIGEN_INVALID_ID)
			{
				ok = 0;
				break;
			}
			state_guard = pigen_predicate_and_condition(model,
				pigen_predicate_true(model), condition, 1);
			if (state_guard.index == PIGEN_INVALID_ID)
			{
				ok = 0;
				break;
			}
			if (node->first_child.index != PIGEN_INVALID_ID &&
				!resolve_procedural_statement(resolver, module_id,
					process, domain, node->first_child, state_guard, qualified ? &checks : NULL))
				ok = 0;
			if (ok && qualified && checks.index!=PIGEN_INVALID_ID)
			{
				pigen_procedural_id state_check=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_IF,
					condition,INVALID_ID(pigen_transfer_id),&checks,1,node->location.source_span);
				if (state_check.index==PIGEN_INVALID_ID) {ok=0;break;}
				check_children=pigen_resize(check_children,(check_count+1)*sizeof(*check_children));
				check_children[check_count++]=state_check;
			}
			index++;
		}
		else
		{
			pigen_procedural_id checks;
			if (!resolve_procedural_statement(resolver,module_id,process,domain,
				child,pigen_predicate_true(model),qualified ? &checks : NULL)) ok=0;
			else if (qualified)
			{
				check_children=pigen_resize(check_children,(check_count+1)*sizeof(*check_children));
				check_children[check_count++]=checks;
			}
		}
		child = node->next_sibling;
	}
	resolver->expression_scope = saved_scope;
	resolver->in_fsm = 0;
	resolver->fsm_terminated = INVALID_ID(pigen_expr_id);
	resolver->fsm_state_expression =
		(pigen_expr_id){PIGEN_INVALID_ID};
	resolver->fsm_state_names = NULL;
	resolver->fsm_state_count = 0;
	if (ok && qualified)
	{
		pigen_procedural_id checks=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,
			INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),check_children,check_count,fsm_node->location.source_span);
		if (checks.index==PIGEN_INVALID_ID) ok=0;
		else model->processes[process.index].body=checks;
	}
	free(check_children);
	free(names);
	return ok;
}

static int resolve_procedural_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id statement_id,
	pigen_predicate_id guard, pigen_procedural_id *checks);

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
	pigen_expr_id *value_ids;
	pigen_data_type_id *child_types;
	pigen_data_type_id source_type, destination_type;
	pigen_conversion conversion;
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
	source_type = value_expr->data_type;
	value_ids = pigen_resize(NULL, destination->as.sequence.child_count * sizeof(*value_ids));
	memcpy(value_ids, value_children, destination->as.sequence.child_count * sizeof(*value_ids));
	child_types = pigen_resize(NULL, destination->as.sequence.child_count * sizeof(*child_types));
	children = pigen_resize(NULL,
		destination->as.sequence.child_count * sizeof(*children));
	for (i = 0; i < destination->as.sequence.child_count && ok; i++)
	{
		if (syntax_is_placeholder(resolver, dest_children[i]))
		{
			const pigen_semantic_expr *rhs = pigen_expr_get(model,
				value_ids[i]);

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
	for (i = 0; ok && i < destination->as.sequence.child_count; i++)
		child_types[i] = pigen_lvalue_get(model, children[i])->data_type;
	free(value_ids);
	destination_type = ok ? pigen_data_type_concatenation(model, child_types,
		destination->as.sequence.child_count) : INVALID_ID(pigen_data_type_id);
	free(child_types);
	if (ok && !pigen_data_type_resolve_assignment_conversion(model,
		source_type, destination_type, &conversion)) ok = 0;
	if (ok && conversion.kind != PIGEN_CONVERSION_IDENTITY)
	{
		value = pigen_expr_add_conversion(model, conversion, value,
			assignment->location.source_span);
		if (value.index == PIGEN_INVALID_ID) ok = 0;
	}

	destination_lvalue = ok ? pigen_lvalue_add_concatenation(model, children,
		destination->as.sequence.child_count, destination_type,
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

	/* A concatenation is one packed destination. Resize the complete RHS
	 * before splitting it, just as for an ordinary vector assignment. The
	 * separate placeholder path has already established its inferred widths. */
	int array_assignment=pigen_shape_get(model,destination_value->shape)->dimension_count!=0;
	value = array_assignment ? pigen_resolve_shaped_assignment_value(resolver->syntax, model,
		resolver->expression_scope, assignment->as.nonblocking_assignment.value,
		destination_value->data_type,destination_value->shape,resolver->policy,resolver->error) :
		pigen_resolve_assignment_value(resolver->syntax,model,resolver->expression_scope,
			assignment->as.nonblocking_assignment.value,destination_value->data_type,resolver->policy,resolver->error);

	if (value.index == PIGEN_INVALID_ID)
	{
		if (has_record_diagnostic(resolver, assignment->as.nonblocking_assignment.value) ||
			(array_assignment && resolver->error && resolver->error->message)) return 0;
		return fail_location(resolver, assignment->location,
			"transfer value requires a compatible expression");
	}
	if (!validate_protocol_copy(resolver, destination, value,
		assignment->as.nonblocking_assignment.value, assignment->location)) return 0;
	destination_value = pigen_expr_get(model, destination_expression);
	source_value = pigen_expr_get(model, value);
	{
		const pigen_semantic_expr *slice_value = source_value;

		const pigen_syntax_expr *written = pigen_syntax_expr_get(&resolver->syntax->expressions,
			assignment->as.nonblocking_assignment.value);
		while (written && written->kind == PIGEN_SYNTAX_EXPR_GROUP)
			written = pigen_syntax_expr_get(&resolver->syntax->expressions, written->as.group.operand);
		int explicit_width = written && written->kind == PIGEN_SYNTAX_EXPR_CAST;

		while (slice_value &&
			slice_value->kind == PIGEN_EXPR_CONVERSION)
			slice_value = pigen_expr_get(model,
				slice_value->as.conversion.operand);
		if (!explicit_width && destination.index != PIGEN_INVALID_ID && slice_value &&
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
	pigen_predicate_id guard, pigen_procedural_id *checks);

/* The fire condition of a transfer, built from its buffered signal uses: every
 * consumed source must be valid and every produced destination ready. Returns
 * an invalid expression when nothing buffered constrains the fire (a static
 * transfer always fires). */
static pigen_expr_id transfer_fire_expression(resolver *resolver,
	const pigen_transfer_signal_use *uses, size_t use_count,
	pigen_source_span span)
{
	pigen_semantic_model *model = resolver->model;
	pigen_expr_id result = INVALID_ID(pigen_expr_id);
	size_t i;

	for (i = 0; i < use_count; i++)
	{
		pigen_expr_id term = INVALID_ID(pigen_expr_id);

		if (uses[i].roles & PIGEN_TRANSFER_CONSUMER)
			term = pigen_expr_add_control(model, PIGEN_EXPR_VALID,
				uses[i].signal, span);
		if (uses[i].roles & PIGEN_TRANSFER_PRODUCER)
		{
			pigen_expr_id ready = pigen_expr_add_control(model,
				PIGEN_EXPR_READY, uses[i].signal, span);

			if (ready.index == PIGEN_INVALID_ID)
				return INVALID_ID(pigen_expr_id);
			if (term.index == PIGEN_INVALID_ID)
				term = ready;
			else
			{
				const pigen_semantic_expr *left = pigen_expr_get(model,
					term);
				const pigen_semantic_expr *right = pigen_expr_get(model,
					ready);
				pigen_binary_resolution resolution;

				if (!left || !right ||
					!pigen_data_type_resolve_binary_operation(model,
						PIGEN_BINARY_LOGICAL_AND, left->data_type,
						right->data_type, &resolution))
					return INVALID_ID(pigen_expr_id);
				term = pigen_expr_add_binary(model,
					resolution.operation, term, ready, span);
			}
		}
		if (term.index == PIGEN_INVALID_ID)
			continue;
		if (result.index == PIGEN_INVALID_ID)
		{
			result = term;
			continue;
		}
		{
			const pigen_semantic_expr *left = pigen_expr_get(model, result);
			const pigen_semantic_expr *right = pigen_expr_get(model, term);
			pigen_binary_resolution resolution;

			if (!left || !right ||
				!pigen_data_type_resolve_binary_operation(model,
					PIGEN_BINARY_LOGICAL_AND, left->data_type,
					right->data_type, &resolution))
				return INVALID_ID(pigen_expr_id);
			result = pigen_expr_add_binary(model, resolution.operation,
				result, term, span);
		}
		if (result.index == PIGEN_INVALID_ID)
			return INVALID_ID(pigen_expr_id);
	}
	return result;
}

/* `when (destination <= source) statement`: perform the transfer and run the
 * body only on the cycle that transfer fires (how/to/write/fsm/states.md). The
 * transfer and the body therefore share one fire condition; the body's
 * effective guard is the enclosing guard plus the transfer's acceptance. */
static int resolve_when_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_syntax_id syntax_id,
	const pigen_syntax_node *statement, pigen_predicate_id guard,
	pigen_procedural_id *checks)
{
	pigen_semantic_model *model = resolver->model;

	const pigen_syntax_node *body;
	pigen_expr_id destination_expression;
	pigen_expr_id value;
	pigen_lvalue_id destination;
	const pigen_semantic_lvalue *destination_lvalue;
	const pigen_semantic_expr *destination_value;
	const pigen_semantic_expr *source_value;
	pigen_transfer_signal_use *signal_uses = NULL;
	size_t signal_use_count = 0;
	pigen_transfer_id transfer;
	pigen_expr_id fire;
	pigen_predicate_id effective = guard;
	pigen_predicate_id body_guard = guard;

	if (!pigen_syntax_expr_get(&resolver->syntax->expressions,
		statement->as.when_statement.destination) ||
		!pigen_syntax_expr_get(&resolver->syntax->expressions,
			statement->as.when_statement.value))
		return fail_location(resolver, statement->location,
			"when requires a transfer event, e.g. "
			"`when (destination <= source)`");
	destination_expression = pigen_resolve_expression(resolver->syntax, model,
		resolver->expression_scope, statement->as.when_statement.destination,
		resolver->policy, resolver->error);
	destination = pigen_lvalue_resolve(model, destination_expression);
	destination_value = pigen_expr_get(model, destination_expression);
	destination_lvalue = pigen_lvalue_get(model, destination);
	if (destination.index == PIGEN_INVALID_ID || !destination_value ||
		!destination_lvalue || !lvalue_is_assignable(model, destination))
		return fail_location(resolver, statement->location,
			"when requires a transfer destination lvalue");
	int array_assignment=pigen_shape_get(model,destination_value->shape)->dimension_count!=0;
	value = array_assignment ? pigen_resolve_shaped_assignment_value(resolver->syntax,model,
		resolver->expression_scope,statement->as.when_statement.value,
		destination_value->data_type,destination_value->shape,resolver->policy,resolver->error) :
		pigen_resolve_assignment_value(resolver->syntax,model,resolver->expression_scope,
			statement->as.when_statement.value,destination_value->data_type,resolver->policy,resolver->error);

	source_value = pigen_expr_get(model, value);
	if (value.index == PIGEN_INVALID_ID || !source_value)
	{
		if (has_record_diagnostic(resolver, statement->as.when_statement.value) ||
			(array_assignment && resolver->error && resolver->error->message)) return 0;
		return fail_location(resolver, statement->location,
			"when transfer value requires a compatible expression");
	}
	if (!validate_protocol_copy(resolver, destination, value,
		statement->as.when_statement.value, statement->location)) return 0;
	/* Resolving the value can grow both arenas. Destination identities are
	 * stable; pointers obtained before that resolution are not. */
	destination_value = pigen_expr_get(model, destination_expression);
	destination_lvalue = pigen_lvalue_get(model, destination);
	if (destination_lvalue->kind != PIGEN_LVALUE_CONCATENATION &&
		destination_value->shape.index != source_value->shape.index)
		return fail_location(resolver, statement->location,
			"when transfer shape mismatch");
	/* The transfer is suppressed once an earlier goto has executed. */
	effective = statement_guard(resolver, guard,
		statement->location.source_span);
	if (effective.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid when event condition");
	if (!analyze_transfer(resolver, destination, value, effective, domain,
		statement->location.source_span, &signal_uses, &signal_use_count))
		return 0;
	transfer = pigen_transfer_add(model, syntax_id, module_id, process,
		destination, value, effective, domain, signal_uses,
		signal_use_count, statement->location.source_span);
	fire = transfer_fire_expression(resolver, signal_uses, signal_use_count,
		statement->location.source_span);
	free(signal_uses);
	if (transfer.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid when transfer event");
	/* The body runs only when the same transfer fires. Its own leaf
	 * statements apply the termination condition themselves, so the body
	 * guard is the plain enclosing guard plus the fire condition. */
	if (fire.index != PIGEN_INVALID_ID)
	{
		/* An unknown fire does not enter the body. Preserve that known
		 * entry decision when a body exit gates subsequent statements. */
		pigen_expr_id one=pigen_expr_add_integer(model,1,pigen_data_type_boolean(model),statement->location.source_span);
		pigen_expr_id entered=case_binary(model,PIGEN_BINARY_CASE_EQUAL,fire,one,statement->location.source_span);
		body_guard = pigen_predicate_and_condition(model, guard, entered, 1);
		if (body_guard.index == PIGEN_INVALID_ID)
			return fail_location(resolver, statement->location,
				"invalid when event condition");
	}
	body = pigen_syntax_get(resolver->syntax, statement->first_child);
	if (!body)
		return fail_location(resolver, statement->location,
			"when requires a body statement");
	if (!resolve_procedural_statement(resolver, module_id, process, domain,
		statement->first_child, body_guard, checks))
		return 0;
	if (checks && fire.index!=PIGEN_INVALID_ID)
	{
		pigen_procedural_id body_checks=*checks;
		*checks=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_IF,fire,
			INVALID_ID(pigen_transfer_id),&body_checks,1,statement->location.source_span);
		if (checks->index==PIGEN_INVALID_ID) return 0;
	}
	return 1;
}

static int resolve_if_statement(resolver *resolver,
	pigen_module_id module_id, pigen_process_id process,
	pigen_clock_domain_id domain, const pigen_syntax_node *statement,
	pigen_predicate_id guard, pigen_procedural_id *checks)
{
	/* `if` tests a computed condition, including ordinary relational `<=`; the
	 * transfer-event form is `when` (how/to/write/fsm/states.md). Control flow
	 * has no observable effect of its own, so the FSM termination condition is
	 * applied by the leaf statements inside each branch. */
	pigen_expr_id condition = pigen_resolve_control_expression(resolver->syntax,
		resolver->model, resolver->expression_scope,
		statement->as.if_statement.condition, resolver->policy,
		resolver->error);
	pigen_predicate_id else_guard;
	pigen_predicate_id then_guard;
	pigen_syntax_id then_id = statement->first_child;
	const pigen_syntax_node *then_statement = pigen_syntax_get(resolver->syntax,
		then_id);
	pigen_syntax_id else_id;
	pigen_procedural_id branches[2];
	pigen_expr_id check_condition=condition;
	pigen_expr_id entry_live=INVALID_ID(pigen_expr_id);
	if (!checks && statement->as.if_statement.qualifier!=PIGEN_CASE_UNQUALIFIED)
		return fail_location(resolver,statement->location,
			"qualified if requires an ordered combinational or supported clocked transfer context");
	if (checks && statement->as.if_statement.qualifier!=PIGEN_CASE_UNQUALIFIED &&
		(resolver->clocked_loop || (resolver->in_fsm && resolver->fsm_terminated.index!=PIGEN_INVALID_ID)))
	{
		/* Capture termination before resolving this chain's own bodies: a
		 * goto inside a selected arm must not cancel the chain-entry check. */
		pigen_predicate_id live=statement_guard(resolver,pigen_predicate_true(resolver->model),statement->location.source_span);
		entry_live=predicate_expression(resolver,live,statement->location.source_span);
		if (entry_live.index==PIGEN_INVALID_ID) return 0;
	}

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
	{
		/* Checks see the original four-state truth. Transfer predicates use
		 * a known branch decision, since an SV if enters else for X/Z. */
		pigen_semantic_model *model=resolver->model;
		pigen_data_type_id type=pigen_expr_get(model,condition)->data_type;
		pigen_unary_resolution truth;
		int exact=pigen_data_type_numerical_interpretation(model,type)==PIGEN_NUMERICAL_EXACT_INTEGER;
		if (!pigen_data_type_resolve_unary_operation(model,exact ? PIGEN_UNARY_LOGICAL_NOT : PIGEN_UNARY_REDUCTION_OR,type,&truth)) return 0;
		check_condition=pigen_expr_add_unary(model,truth.operation,condition,statement->location.source_span);
		if (exact)
		{
			if (!pigen_data_type_resolve_unary_operation(model,PIGEN_UNARY_LOGICAL_NOT,truth.operation.result_data_type,&truth)) return 0;
			check_condition=pigen_expr_add_unary(model,truth.operation,check_condition,statement->location.source_span);
		}
		pigen_expr_id one=pigen_expr_add_integer(model,1,pigen_data_type_boolean(model),statement->location.source_span);
		pigen_binary_resolution equal;
		if (check_condition.index==PIGEN_INVALID_ID || one.index==PIGEN_INVALID_ID ||
			!pigen_data_type_resolve_binary_operation(model,PIGEN_BINARY_CASE_EQUAL,
				pigen_expr_get(model,check_condition)->data_type,pigen_expr_get(model,one)->data_type,&equal)) return 0;
		condition=pigen_expr_add_binary(model,equal.operation,check_condition,one,statement->location.source_span);
		if (condition.index==PIGEN_INVALID_ID) return 0;
	}
	then_guard = pigen_predicate_and_condition(resolver->model, guard,
		condition, 1);
	else_guard = pigen_predicate_and_condition(resolver->model, guard,
		condition, 0);
	if (then_guard.index == PIGEN_INVALID_ID ||
		else_guard.index == PIGEN_INVALID_ID)
		return fail_location(resolver, statement->location,
			"invalid if condition predicate");
	if (!resolve_procedural_statement(resolver, module_id, process, domain,
		then_id, then_guard, checks ? &branches[0] : NULL)) return 0;
	if (else_id.index!=PIGEN_INVALID_ID &&
		!resolve_procedural_statement(resolver,module_id,process,domain,
			else_id,else_guard,checks ? &branches[1] : NULL)) return 0;
	if (checks)
	{
		*checks=pigen_procedural_add(resolver->model,process,PIGEN_PROCEDURAL_IF,
			check_condition,INVALID_ID(pigen_transfer_id),branches,
			else_id.index!=PIGEN_INVALID_ID ? 2 : 1,statement->location.source_span);
		if (checks->index==PIGEN_INVALID_ID) return 0;
		resolver->model->procedural[checks->index].case_qualifier=statement->as.if_statement.qualifier;
		resolver->model->procedural[checks->index].else_if=statement->as.if_statement.else_if;
		if (entry_live.index!=PIGEN_INVALID_ID)
		{
			pigen_procedural_id chain=*checks;
			*checks=pigen_procedural_add(resolver->model,process,PIGEN_PROCEDURAL_IF,
				entry_live,INVALID_ID(pigen_transfer_id),&chain,1,statement->location.source_span);
			if (checks->index==PIGEN_INVALID_ID) return 0;
		}
	}
	return 1;
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
		/* Repeated foreach instances share source spelling and process, but
		 * their packet symbols have distinct lexical identities. */
		symbol=pigen_symbol_lookup(model,resolver->expression_scope,target);

		for (p = 0; p < resolver->pipeline_count; p++)
		{
			resolved_pipeline_reset *pipeline = &resolver->pipelines[p];
			pending_pipeline_reset immediate = {module_id, process, domain,
				target, guard, statement->location.source_span, 0,resolver->expression_scope};

			if (pipeline->module.index != module_id.index ||
				pipeline->process.index != process.index ||
				!spelling_text_equal(resolver, pipeline->name, target) ||
				pigen_signal_get(model,pipeline->output)->symbol.index!=symbol.index)
				continue;
			if (!apply_pipeline_reset(resolver, module_id, process, domain,
				&immediate, pipeline->output, pipeline->scope))
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
				guard, statement->location.source_span, 0,resolver->expression_scope};
		return 1;
	}
	symbol = pigen_symbol_lookup(model, resolver->expression_scope, target);
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
	pigen_predicate_id guard, pigen_procedural_id *checks)
{
	const pigen_syntax_node *statement = pigen_syntax_get(resolver->syntax,
		statement_id);
	pigen_syntax_id child;

	if (!statement) return 0;
	if (checks)
	{
		*checks=pigen_procedural_add(resolver->model,process,PIGEN_PROCEDURAL_BLOCK,
			INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),NULL,0,statement->location.source_span);
		if (checks->index==PIGEN_INVALID_ID) return 0;
	}
	if(statement->kind==PIGEN_SYNTAX_PROCEDURAL_BREAK || statement->kind==PIGEN_SYNTAX_PROCEDURAL_CONTINUE)
		return resolve_clocked_loop_exit(resolver,statement,guard);
	if (statement->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
		return resolve_assignment(resolver, module_id, process, domain,
			statement_id, statement,
			statement_guard(resolver, guard, statement->location.source_span));
	if (statement->kind == PIGEN_SYNTAX_IF_STATEMENT)
		return resolve_if_statement(resolver, module_id, process, domain,
			statement, guard, checks);
	if (statement->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH)
		return resolve_clocked_foreach(resolver,module_id,process,domain,statement_id,statement,guard,checks);
	if (statement->kind==PIGEN_SYNTAX_PROCEDURAL_CASE)
		return resolve_clocked_case(resolver,module_id,process,domain,statement,guard,checks);
	if (statement->kind == PIGEN_SYNTAX_WHEN_STATEMENT)
		return resolve_when_statement(resolver, module_id, process, domain,
			statement_id, statement, guard, checks);
	if (statement->kind == PIGEN_SYNTAX_PIPELINE)
		return resolve_pipeline(resolver, module_id, process, domain,
			statement_id, statement,
			statement_guard(resolver, guard, statement->location.source_span));
	if (statement->kind == PIGEN_SYNTAX_GOTO)
		return resolve_goto(resolver, module_id, process, domain,
			statement_id, statement, guard);
	if (statement->kind == PIGEN_SYNTAX_ACTION)
		return resolve_action(resolver, module_id, process, domain,
			statement_id, statement,
			statement_guard(resolver, guard, statement->location.source_span));
    if(statement->kind==PIGEN_SYNTAX_IMPORT)
        return statement->as.package_import.is_structured ? add_import(resolver,resolver->expression_scope,statement) :
            fail_location(resolver,statement->location,"procedural imports require a structured package");
    if(statement->kind!=PIGEN_SYNTAX_PROCEDURAL_BLOCK)
        return fail_location(resolver,statement->location,"invalid procedural statement");
    pigen_scope_id saved=resolver->expression_scope;
    int scoped=0;
    for(child=statement->first_child;child.index!=PIGEN_INVALID_ID;child=pigen_syntax_get(resolver->syntax,child)->next_sibling)
        if(pigen_syntax_get(resolver->syntax,child)->kind==PIGEN_SYNTAX_IMPORT) {scoped=1;break;}
    if(scoped)
    {
        resolver->expression_scope=pigen_scope_add(resolver->model,saved,statement->location.source_span);
        if(resolver->expression_scope.index==PIGEN_INVALID_ID) {resolver->expression_scope=saved;return 0;}
        resolver->model->scopes[resolver->expression_scope.index].is_procedural=1;
    }
    int executable=0,ok=1;
    pigen_procedural_id *check_children=NULL;size_t check_count=0;
    for(child=statement->first_child;child.index!=PIGEN_INVALID_ID;)
    {
        const pigen_syntax_node *known=pigen_syntax_get(resolver->syntax,child);
        if(known->kind==PIGEN_SYNTAX_IMPORT && executable)
        {ok=fail_location(resolver,known->location,"procedural declarations must precede statements in their block");break;}
        if(known->kind!=PIGEN_SYNTAX_IMPORT && known->kind!=PIGEN_SYNTAX_OPAQUE) executable=1;
        pigen_procedural_id child_checks;
        if(!resolve_procedural_statement(resolver,module_id,process,domain,child,guard,checks ? &child_checks : NULL)) {ok=0;break;}
        if(checks) {check_children=pigen_resize(check_children,(check_count+1)*sizeof(*check_children));check_children[check_count++]=child_checks;}
        child=known->next_sibling;
    }
    resolver->expression_scope=saved;
    if(ok && checks)
    {
        *checks=pigen_procedural_add(resolver->model,process,PIGEN_PROCEDURAL_BLOCK,
            INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),check_children,check_count,statement->location.source_span);
        ok=checks->index!=PIGEN_INVALID_ID;
    }
    free(check_children);
    return ok;
}

static int syntax_has_qualified_control(const pigen_syntax_tree *tree,pigen_syntax_id id,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(tree,id);
	if (!node || depth>128) return 0;
	if (node->kind==PIGEN_SYNTAX_IF_STATEMENT && node->as.if_statement.qualifier!=PIGEN_CASE_UNQUALIFIED) return 1;
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_CASE && node->as.procedural_case.qualifier!=PIGEN_CASE_UNQUALIFIED) return 1;
	for (pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;
		child=pigen_syntax_get(tree,child)->next_sibling)
		if (syntax_has_qualified_control(tree,child,depth+1)) return 1;
	return 0;
}

static int add_clocked_process(resolver *resolver,
	pigen_module_id module_id, pigen_syntax_id syntax_id,
	const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model = resolver->model;
	resolver->foreach_specializations=0;
	pigen_scope_id scope = resolver->expression_scope;
	pigen_expr_id clock = pigen_resolve_expression(resolver->syntax, model,
		scope, syntax_node->as.clocked_process.clock, resolver->policy,
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
		resolver->expression_scope = scope;
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
	int qualified=syntax_has_qualified_control(resolver->syntax,body_id,0);
	pigen_procedural_id checks=INVALID_ID(pigen_procedural_id);
	if (!resolve_procedural_statement(resolver,module_id,process,domain,
		body_id,pigen_predicate_true(model),qualified ? &checks : NULL)) return 0;
	if (qualified) model->processes[process.index].body=checks;
	return 1;
}

static int child_port_at(pigen_semantic_model *model,
	pigen_module_id child_module, size_t index, pigen_symbol_id *symbol,
	pigen_signal_id *signal)
{
	size_t at = 0;
	const pigen_semantic_module *module = pigen_module_get(model, child_module);
	for (size_t i = 0; module && i < model->symbol_count; i++)
	{
		const pigen_symbol *candidate = &model->symbols[i];
		if (candidate->scope.index != module->scope.index) continue;
		pigen_signal_id scalar = pigen_symbol_signal(model, (pigen_symbol_id){(uint32_t)i});
		if (candidate->kind == PIGEN_SYMBOL_PROTOCOL_ENDPOINT)
		{
			const pigen_semantic_protocol_endpoint *endpoint = pigen_protocol_endpoint_get(model,
				pigen_symbol_protocol_endpoint(model, (pigen_symbol_id){(uint32_t)i}));
			if (!endpoint || !endpoint->is_port || pigen_protocol_array_for_endpoint(model,
				pigen_symbol_protocol_endpoint(model, (pigen_symbol_id){(uint32_t)i}), NULL).index != PIGEN_INVALID_ID) continue;
		}
		else if (candidate->kind == PIGEN_SYMBOL_PROTOCOL_ARRAY)
		{
			const pigen_semantic_protocol_array *array = pigen_protocol_array_get(model,
				pigen_symbol_protocol_array(model, (pigen_symbol_id){(uint32_t)i}));
			if (!array || !array->is_port) continue;
		}
		else
		{
			const pigen_semantic_signal *known = pigen_signal_get(model, scalar);
			if (!known || known->direction == PIGEN_SEMANTIC_INTERNAL ||
				pigen_signal_is_instance_endpoint(model, scalar) ||
				pigen_protocol_endpoint_for_signal(model, scalar, NULL).index != PIGEN_INVALID_ID) continue;
		}
		if (at++ == index)
		{
			*symbol = (pigen_symbol_id){(uint32_t)i};
			*signal = scalar;
			return 1;
		}
	}
	return 0;
}

static int ownership_same_projection(pigen_semantic_model *model,
	pigen_expr_id left, pigen_expr_id right);

static int resolve_expr_references_symbol(const pigen_semantic_model *model,
	pigen_expr_id expression, pigen_symbol_id symbol);

static pigen_expr_id partial_endpoint_alias(pigen_semantic_model *model,
	pigen_expr_id value, const pigen_semantic_instance_connection *upstream,
	pigen_data_type_id target_type, pigen_source_span span);

static pigen_expr_id dynamic_endpoint_alias(pigen_semantic_model *model,
	pigen_expr_id value, const pigen_semantic_instance_connection *upstream,
	pigen_data_type_id target_type, pigen_source_span span);

static pigen_expr_id resolve_input_aliases(pigen_semantic_model *model,
	pigen_expr_id value);

static pigen_module_id specialize_generated_child(resolver *resolver,
	pigen_module_id parent, pigen_module_id target, const pigen_syntax_node *instance);

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
		if (node->as.instance.has_parameters)
		{
			child_module=specialize_generated_child(resolver,module_id,child_module,node);
			if(child_module.index==PIGEN_INVALID_ID) return 0;
			module=pigen_module_get(model,module_id);
		}
		declared = pigen_symbol_declare(model, resolver->expression_scope,
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
		instance_symbol = pigen_symbol_lookup(model, resolver->expression_scope, instance_name);
		const pigen_symbol *known = pigen_symbol_get(model, instance_symbol);
		if (!known || known->kind != PIGEN_SYMBOL_INSTANCE) return 0;
		instance = known->object.instance;
		child_module=model->instances[instance.index].child_module;
	}
	child=pigen_module_get(model,child_module);
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
					if (base_symbol->kind == PIGEN_SYMBOL_PROTOCOL_ENDPOINT)
						return fail_location(resolver, connection->location,
							"AXI aggregate has no single valid/ready control; select a channel");
					if (base_symbol->kind != PIGEN_SYMBOL_SIGNAL)
						return fail_location(resolver, connection->location, "unknown child module port");
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
		if (port_known && (port_known->kind == PIGEN_SYMBOL_PROTOCOL_ENDPOINT ||
			port_known->kind == PIGEN_SYMBOL_PROTOCOL_ARRAY))
		{
			/* The aggregate is expanded after every sibling identity and timing
			 * binding exists. Its channels retain the omitted-port proxies. */
			child_id = connection->next_sibling;
			continue;
		}
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
		pigen_syntax_expr_id binding = connection->as.connection.value;
		const pigen_syntax_expr *group = pigen_syntax_expr_get(&resolver->syntax->expressions, binding);
		while (group && group->kind == PIGEN_SYNTAX_EXPR_GROUP)
		{
			binding = group->as.group.operand;
			group = pigen_syntax_expr_get(&resolver->syntax->expressions, binding);
		}
		if (phase == 3)
		{
			/* Bind direct parent-wire input aliases before resolving sibling
			 * output lvalues. Endpoint expressions wait for normal input resolution. */
			const pigen_syntax_expr *expr = pigen_syntax_expr_get(&resolver->syntax->expressions,
				binding);
			while (expr && (expr->kind == PIGEN_SYNTAX_EXPR_GROUP ||
				expr->kind == PIGEN_SYNTAX_EXPR_INDEX || expr->kind == PIGEN_SYNTAX_EXPR_SELECT))
				expr = pigen_syntax_expr_get(&resolver->syntax->expressions,
					expr->kind == PIGEN_SYNTAX_EXPR_GROUP ? expr->as.group.operand :
					expr->kind == PIGEN_SYNTAX_EXPR_INDEX ? expr->as.index.base : expr->as.select.base);
			const pigen_symbol *parent = expr && expr->kind == PIGEN_SYNTAX_EXPR_NAME ?
				pigen_symbol_get(model, pigen_symbol_lookup(model, resolver->expression_scope,
					token_spelling(resolver, expr->as.atom.token))) : NULL;
			if (generated_control || direction != PIGEN_SEMANTIC_INPUT || !parent ||
				parent->kind != PIGEN_SYMBOL_SIGNAL ||
				!pigen_transfer_type_descriptor_get(model->signals[parent->object.signal.index].transfer_type)->is_static)
			{
				connection_index++;
				child_id = connection->next_sibling;
				continue;
			}
		}
		else if ((phase == 1) != (direction == PIGEN_SEMANTIC_OUTPUT || generated_control))
		{
			connection_index++;
			child_id = connection->next_sibling;
			continue;
		}
		if (generated_control)
		{
			/* Explicit parent controls have semantic identities needed by fabric
			 * lowering. Implicit boundary siblings are emitted later. */
			if (binding.index != PIGEN_INVALID_ID)
				value = pigen_resolve_expression(resolver->syntax, model,
					resolver->expression_scope, binding,
					resolver->policy, NULL);
			goto add_connection;
		}
		if (binding.index != PIGEN_INVALID_ID)
		{
			if (direction == PIGEN_SEMANTIC_OUTPUT)
			{
				pigen_expr_id expression = pigen_resolve_expression(
					resolver->syntax, model, resolver->expression_scope,
					binding, resolver->policy,
					resolver->error);
				pigen_lvalue_id lvalue = pigen_lvalue_resolve(model, expression);
				const pigen_semantic_lvalue *known =
					pigen_lvalue_get(model, lvalue);
				if (!known || !pigen_lvalue_is_writable(model, lvalue))
					return fail_location(resolver, connection->location,
						"instance output requires a writable parent signal");
				value = expression;
			}
			else
			{
				value = pigen_resolve_assignment_value(resolver->syntax,
					model, resolver->expression_scope,
					binding, connection_type,
					resolver->policy, resolver->error);
				if (value.index == PIGEN_INVALID_ID)
				{
					pigen_expr_id raw = pigen_resolve_expression(
						resolver->syntax, model, resolver->expression_scope,
						binding, resolver->policy,
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
	if (phase == 1 || phase == 2 || phase == 3)
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
			if (port.direction != (phase == 1 ? PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT) ||
				pigen_transfer_type_descriptor_get(port.transfer_type)->is_static ||
				!port_name || !connection_name || port_length != name_length ||
				memcmp(port_name, connection_name, port_length)) continue;
			while (expression && expression->kind == PIGEN_EXPR_CONVERSION)
				expression = pigen_expr_get(model, expression->as.conversion.operand);
			pigen_signal_id parent_id = INVALID_ID(pigen_signal_id);
			if (expression && expression->kind == PIGEN_EXPR_SYMBOL)
				parent_id = pigen_symbol_signal(model, expression->as.symbol);
			else if (expression)
			{
				const pigen_semantic_lvalue *projection = pigen_lvalue_get(model,
					pigen_lvalue_resolve(model, (pigen_expr_id){(uint32_t)(expression-model->expressions)}));
				if (projection && projection->kind == PIGEN_LVALUE_PROJECTION)
					parent_id = projection->as.projection.signal;
			}
			parent = pigen_signal_get(model, parent_id);
			if (!parent || (phase != 1 && parent->direction == PIGEN_SEMANTIC_INPUT)) continue;
			if (!pigen_transfer_type_descriptor_get(parent->transfer_type)->is_static)
			{
				if (phase != 1) continue;
				if (!expression || expression->kind != PIGEN_EXPR_SYMBOL) continue;
				int explicit_control = 0;
				if (!pigen_signal_is_instance_endpoint(model, parent_id)) continue;
				for (size_t d = 0; d < record->connection_count; d++)
				{
					const pigen_semantic_instance_connection *control = &model->instance_connections[record->first_connection+d];
					if (control->port_signal.index == connection->port_signal.index &&
						!spelling_text_equal(resolver, control->port_name, connection->port_name)) explicit_control = 1;
				}
				if (!explicit_control) continue;
			}
			if (phase == 2 && expression)
			{
				/* A wire already driven by an elastic output retains that
				 * upstream endpoint identity, including write protection. */
				/* Earlier sibling outputs may already target the preliminary
				 * input view. Retain it so physical producer overlap is checked. */
				const pigen_semantic_signal *existing = pigen_signal_get(model, connection->endpoint_view);
				int driven = 0;
				for (size_t d = 0; existing && d < model->instance_connection_count; d++)
				{
					const pigen_semantic_instance_connection *candidate = &model->instance_connections[d];
					if (candidate->direction == PIGEN_SEMANTIC_OUTPUT &&
						resolve_expr_references_symbol(model, candidate->value, existing->symbol))
						driven = 1;
				}
				if (driven) continue;
				int reused = 0;
				for (size_t d = 0; d < model->instance_connection_count; d++)
				{
					const pigen_semantic_instance_connection *upstream = &model->instance_connections[d];
					if (upstream->direction != PIGEN_SEMANTIC_OUTPUT ||
						upstream->endpoint_view.index == PIGEN_INVALID_ID) continue;
					const pigen_semantic_signal *source = pigen_signal_get(model, upstream->endpoint_view);
					if (source && source->data_type.index == port.data_type.index &&
						source->shape.index == port.shape.index &&
						ownership_same_projection(model, connection->value, upstream->value))
					{
						connection->endpoint_view = upstream->endpoint_view;
						reused = 1;
					}
				}
				if (reused) continue;
				for (size_t d = 0; d < model->instance_connection_count; d++)
				{
					const pigen_semantic_instance_connection *upstream = &model->instance_connections[d];
					pigen_expr_id mapped = partial_endpoint_alias(model, connection->value,
						upstream, port.data_type, connection->span);
					if (mapped.index == PIGEN_INVALID_ID)
						mapped = dynamic_endpoint_alias(model, connection->value, upstream,
							port.data_type, connection->span);
					if (mapped.index == PIGEN_INVALID_ID) continue;
					connection->value = mapped;
					connection->endpoint_view = INVALID_ID(pigen_signal_id);
					reused = 1;
					break;
				}
				if (reused) continue;
			}
			if (connection->endpoint_view.index != PIGEN_INVALID_ID) continue;
			char name[96];
			snprintf(name, sizeof name, "__pigen_endpoint_view_%u_%zu", instance.index, c);
			pigen_source_id source = pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-endpoint-view>", name, strlen(name));
			pigen_source_span span = {source, 0, strlen(name)};
			pigen_symbol_id symbol;
			if (pigen_symbol_declare(model, resolver->expression_scope, PIGEN_SYMBOL_SIGNAL,
				port.data_type, span, span, &symbol, NULL) != PIGEN_DECLARE_OK) return 0;
			pigen_signal_id view = pigen_signal_add(model, syntax_id, module_id, symbol,
				port.data_type, port.shape, INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_BUF,
				(phase == 1 ? PIGEN_SEMANTIC_INPUT : PIGEN_SEMANTIC_OUTPUT), span);
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
			port_symbol_record = pigen_symbol_get(model, port.symbol);
			for (size_t c = 0; c < record->connection_count; c++)
			{
				const pigen_semantic_instance_connection *connection = &model->instance_connections[record->first_connection+c];
				if (connection->port_signal.index == p &&
					spelling_text_equal(resolver, connection->port_name, port_symbol_record->name)) connected = 1;
			}
			if (connected) continue;
			port_symbol_record = pigen_symbol_get(model, port.symbol);
			port_name = port_symbol_record->name;
			snprintf(name, sizeof name, "__pigen_endpoint_%u_%zu", instance.index, p);
			name_source = pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-endpoint>", name, strlen(name));
			name_span = (pigen_source_span){name_source, 0, strlen(name)};
			if (pigen_symbol_declare(model, resolver->expression_scope, PIGEN_SYMBOL_SIGNAL,
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

	/* Each omitted native bus owns one aggregate view over the existing
	 * storage-free channel endpoints. Establish all views in identity phase
	 * before any sibling expression is resolved. */
	size_t protocol_count = model->protocol_endpoint_count;
	for (size_t e = 0; e < protocol_count; e++)
	{
		pigen_semantic_protocol_endpoint child_endpoint = model->protocol_endpoints[e];
		if (child_endpoint.module.index != child_module.index || !child_endpoint.is_port) continue;
		pigen_semantic_protocol_endpoint view = {
			.syntax = syntax_id, .module = module_id, .type = child_endpoint.type,
			.role = child_endpoint.role == PIGEN_PROTOCOL_TARGET ? PIGEN_PROTOCOL_INITIATOR : PIGEN_PROTOCOL_TARGET,
			.is_instance_view = 1, .instance = instance,
			.child_endpoint = {(uint32_t)e}
		};
		for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
		{
			view.channels[channel] = INVALID_ID(pigen_signal_id);
			if (child_endpoint.channels[channel].index == PIGEN_INVALID_ID) continue;
			const pigen_semantic_instance *record = pigen_instance_get(model, instance);
			for (size_t c = 0; c < record->connection_count; c++)
			{
				const pigen_semantic_instance_connection *connection = &model->instance_connections[record->first_connection+c];
				if (connection->port_signal.index != child_endpoint.channels[channel].index) continue;
				const pigen_semantic_expr *value = pigen_expr_get(model, connection->value);
				if (value && value->kind == PIGEN_EXPR_SYMBOL)
					view.channels[channel] = pigen_symbol_signal(model, value->as.symbol);
			}
		}
		char name[96];
		snprintf(name, sizeof name, "__pigen_axi_endpoint_%u_%zu", instance.index, e);
		pigen_source_id source = pigen_source_add((pigen_source_manager *)model->sources,
			"<pigen-axi-endpoint>", name, strlen(name));
		pigen_source_span name_span = {source, 0, strlen(name)};
		view.span = name_span;
		if (pigen_symbol_declare(model, resolver->expression_scope, PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
			INVALID_ID(pigen_data_type_id), name_span, view.span, &view.symbol, NULL) != PIGEN_DECLARE_OK ||
			pigen_protocol_endpoint_add(model, &view).index == PIGEN_INVALID_ID)
			return fail_location(resolver, node->location, "invalid child AXI endpoint view");
	}
	return 1;
}

/* Reuse the resolved lvalue expression as the read side of an update. Typed
 * indices in these processes are pure; side-effecting calls/updates are not
 * admitted by expression resolution. No source text or new name lookup is used. */
static pigen_expr_id resolve_static_update(resolver *resolver,
	const pigen_syntax_node *node,pigen_expr_id left,pigen_data_type_id target)
{
	pigen_semantic_model *model=resolver->model;
	pigen_source_span span=node->location.source_span;
	pigen_binary_operator operator=node->as.nonblocking_assignment.increment<0 ? PIGEN_BINARY_SUBTRACT : PIGEN_BINARY_ADD;
	if (node->as.nonblocking_assignment.compound &&
		!pigen_binary_operator_from_syntax(node->as.nonblocking_assignment.compound_operator,&operator))
		return INVALID_ID(pigen_expr_id);
	pigen_expr_id right=node->as.nonblocking_assignment.increment ?
		pigen_expr_add_exact_integer(model,pigen_integer_intern_u64(model,1),span) :
		pigen_resolve_expression(resolver->syntax,model,resolver->expression_scope,
			node->as.nonblocking_assignment.value,resolver->policy,resolver->error);
	const pigen_semantic_expr *lhs=pigen_expr_get(model,left),*rhs=pigen_expr_get(model,right);
	const pigen_semantic_shape *shape=lhs ? pigen_shape_get(model,lhs->shape) : NULL;
	pigen_binary_resolution operation;
	if (!lhs || !rhs || !shape || shape->dimension_count || lhs->shape.index!=rhs->shape.index ||
		!pigen_data_type_resolve_binary_operation(model,operator,lhs->data_type,rhs->data_type,&operation))
		return INVALID_ID(pigen_expr_id);
	if (resolver->policy && (operator==PIGEN_BINARY_SHIFT_LEFT || operator==PIGEN_BINARY_ARITH_SHIFT_LEFT))
	{
		pigen_const_expr_id width=pigen_data_type_packed_width(model,operation.operation.result_data_type);
		uint64_t bits;
		if (pigen_const_expr_evaluate_u64(model,width,&bits))
		{
			if (bits>resolver->policy->maximum_generated_bits) return INVALID_ID(pigen_expr_id);
		}
		else if (!pigen_const_expr_is_symbolic(model,width) ||
			!pigen_width_constraint_add(model,width,resolver->policy->maximum_generated_bits,span))
			return INVALID_ID(pigen_expr_id);
	}
	if (operation.left_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
		left=pigen_expr_add_conversion(model,operation.left_conversion,left,span);
	if (operation.right_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
		right=pigen_expr_add_conversion(model,operation.right_conversion,right,span);
	pigen_expr_id value=pigen_expr_add_binary(model,operation.operation,left,right,span);
	pigen_conversion conversion;
	if (value.index==PIGEN_INVALID_ID ||
		/* An update stores back into the original variable type, including
		 * unsigned decrement/wrap after lossless numerical subtraction. */
		!pigen_data_type_resolve_explicit_conversion(model,operation.operation.result_data_type,target,&conversion))
		return INVALID_ID(pigen_expr_id);
	return conversion.kind==PIGEN_CONVERSION_IDENTITY ? value :
		pigen_expr_add_conversion(model,conversion,value,span);
}

static int add_static_assignment(resolver *resolver,
	pigen_module_id module_id, pigen_syntax_id syntax_id,
	const pigen_syntax_node *node, pigen_process_id process)
{
	pigen_semantic_model *model = resolver->model;
	pigen_scope_id scope = resolver->expression_scope;
	pigen_expr_id expression = pigen_resolve_expression(resolver->syntax, model,
		scope, node->as.nonblocking_assignment.destination, resolver->policy, resolver->error);
	pigen_lvalue_id destination = pigen_lvalue_resolve(model, expression);
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model, destination);
	if (!lvalue || (lvalue->kind != PIGEN_LVALUE_PROJECTION && lvalue->kind != PIGEN_LVALUE_CONCATENATION) ||
		!pigen_lvalue_is_writable(model, destination))
		return fail_location(resolver, node->location,
			"continuous intrinsic assignment requires a writable static projection");
	pigen_data_type_id type = lvalue->data_type;
	pigen_predicate_id guard = pigen_predicate_true(model);
	pigen_expression_use_analysis analysis = {0};
	if (!pigen_analyze_lvalue_uses(model, destination, guard, &analysis))
	{
		pigen_free_expression_use_analysis(&analysis);
		return fail_location(resolver, node->location, "invalid continuous destination");
	}
	for (size_t i = 0; i < analysis.signal_count; i++)
	{
		const pigen_semantic_signal *signal = pigen_signal_get(model, analysis.signals[i].signal);
		if(analysis.signals[i].contexts & PIGEN_EXPRESSION_USE_LVALUE)
			for(size_t j=0;j<resolver->foreach_readonly_count;j++)
				if(analysis.signals[i].signal.index==resolver->foreach_readonly[j].index)
				{
					pigen_free_expression_use_analysis(&analysis);
					return fail_location(resolver,node->location,"foreach loop variables are read-only");
				}

		if ((analysis.signals[i].contexts & PIGEN_EXPRESSION_USE_LVALUE) &&
			(!signal || !pigen_transfer_type_descriptor_get(signal->transfer_type)->is_static))
		{
			pigen_free_expression_use_analysis(&analysis);
			return fail_location(resolver, node->location,
				"continuous assignment cannot produce a buffered token; use a clocked transfer");
		}
	}
	pigen_expr_id value = node->kind==PIGEN_SYNTAX_BLOCKING_ASSIGNMENT &&
		(node->as.nonblocking_assignment.compound || node->as.nonblocking_assignment.increment) ?
		resolve_static_update(resolver,node,expression,type) :
		pigen_resolve_shaped_assignment_value(resolver->syntax, model,
			scope, node->as.nonblocking_assignment.value, type,pigen_expr_get(model,expression)->shape, resolver->policy, resolver->error);
	if (value.index == PIGEN_INVALID_ID)
	{
		pigen_free_expression_use_analysis(&analysis);
		if(resolver->error && resolver->error->message) return 0;
		return fail_location(resolver, node->location,
			"continuous intrinsic assignment requires a compatible expression");
	}
	if (!pigen_analyze_expression_uses(model, value, guard,
		PIGEN_EXPRESSION_USE_READ, &analysis))
	{
		pigen_free_expression_use_analysis(&analysis);
		return fail_location(resolver, node->location, "invalid continuous expression");
	}
	pigen_transfer_signal_use *uses = NULL;
	size_t count = 0, capacity = 0;
	for (size_t i = 0; i < analysis.signal_count; i++)
	{
		pigen_signal_id source = analysis.signals[i].signal;
		const pigen_semantic_signal *known = pigen_signal_get(model, source);
		if (!known || !pigen_transfer_type_descriptor_get(known->transfer_type)->is_static)
		{
			const char *message = analysis.signals[i].contexts & PIGEN_EXPRESSION_USE_LVALUE ?
				"continuous assignment cannot produce a buffered token; use a clocked transfer" :
				"continuous buffered payload observation requires peek";
			free(uses);
			pigen_free_expression_use_analysis(&analysis);
			return fail_location(resolver, node->location, message);
		}
		unsigned contexts = analysis.signals[i].contexts;
		if (contexts & PIGEN_EXPRESSION_USE_LVALUE)
			add_transfer_signal_use(&uses, &count, &capacity, source, PIGEN_TRANSFER_SIGNAL_WRITE);
		if (contexts & (PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_INDEX | PIGEN_EXPRESSION_USE_TYPE))
			add_transfer_signal_use(&uses, &count, &capacity, source, PIGEN_TRANSFER_SIGNAL_READ);
	}
	pigen_free_expression_use_analysis(&analysis);
	if (process.index == PIGEN_INVALID_ID)
	{
		process = pigen_structural_process_add(model, syntax_id, module_id, node->location.source_span);
		if (process.index == PIGEN_INVALID_ID) { free(uses); return 0; }
		model->processes[process.index].is_continuous = 1;
	}
	pigen_transfer_id transfer = pigen_transfer_add(model, syntax_id, module_id, process,
		destination, value, guard, INVALID_ID(pigen_clock_domain_id), uses, count,
		node->location.source_span);
	free(uses);
	if (transfer.index == PIGEN_INVALID_ID)
		return fail_location(resolver, node->location, "invalid continuous assignment");
	return 1;
}


static int add_continuous_assignment(resolver *resolver, pigen_module_id module,
	pigen_syntax_id syntax, const pigen_syntax_node *node)
{
	return add_static_assignment(resolver,module,syntax,node,INVALID_ID(pigen_process_id));
}

static pigen_procedural_id resolve_combinational_statement(resolver *resolver,
	pigen_module_id module, pigen_process_id process, pigen_syntax_id syntax, size_t depth);

static pigen_expr_id resolve_procedural_case_value(resolver *resolver,
	pigen_syntax_expr_id syntax,pigen_syntax_location location,int clocked)
{
	pigen_semantic_model *model=resolver->model;
	pigen_expr_id value=pigen_resolve_case_expression(resolver->syntax,model,resolver->expression_scope,syntax,resolver->policy,resolver->error);
	const pigen_semantic_expr *expression=pigen_expr_get(model,value);
	const pigen_semantic_shape *shape=expression ? pigen_shape_get(model,expression->shape) : NULL;
	if (!expression || !shape || shape->dimension_count ||
		(!pigen_data_type_is_integral(model,expression->data_type) &&
		pigen_data_type_numerical_interpretation(model,expression->data_type)!=PIGEN_NUMERICAL_EXACT_INTEGER))
	{
		fail_location(resolver,location,"procedural case requires integral scalar selector and labels");return INVALID_ID(pigen_expr_id);
	}
	pigen_expression_use_analysis uses={0};
	int ok=pigen_analyze_expression_uses(model,value,pigen_predicate_true(model),PIGEN_EXPRESSION_USE_READ,&uses);
	for(size_t i=0;ok && !clocked && i<uses.signal_count;i++)
	{
		const pigen_semantic_signal *signal=pigen_signal_get(model,uses.signals[i].signal);
		if (!signal || !pigen_transfer_type_descriptor_get(signal->transfer_type)->is_static) ok=0;
	}
	pigen_free_expression_use_analysis(&uses);
 if (!ok) {fail_location(resolver,location,clocked ? "cannot analyze clocked case dependencies" :
  "procedural case buffered observations require peek");return INVALID_ID(pigen_expr_id);}
	return value;
}

static int procedural_case_width(resolver *resolver,pigen_expr_id value,
	uint64_t *maximum,int *all_signed,pigen_syntax_location location)
{
	const pigen_semantic_expr *expression=pigen_expr_get(resolver->model,value);uint64_t width;
	if (!expression || !pigen_const_expr_evaluate_u64(resolver->model,
		pigen_data_type_packed_width(resolver->model,expression->data_type),&width) || !width ||
		width>resolver->policy->maximum_generated_bits || width>SIZE_MAX)
		return fail_location(resolver,location,"procedural case comparison exceeds generated-size limit");
	if (width>*maximum) *maximum=width;
	*all_signed&=pigen_data_type_signedness(resolver->model,expression->data_type)==PIGEN_SIGN_SIGNED;
	return 1;
}

/* Both ordered combinational and clocked cases share one operand context.
 * Labels remain grouped so overlap checks count items, not comma expressions. */
typedef struct {
 pigen_syntax_id body;
 pigen_source_span span;
 size_t first_label,label_count;
} resolved_case_item;
typedef struct {
 pigen_expr_id *values; /* Selector first, then all non-default labels. */
 size_t value_count;
 resolved_case_item *items;
 size_t item_count;
 uint64_t width;
} resolved_case;

static void free_resolved_case(resolved_case *selection)
{
 free(selection->values);free(selection->items);*selection=(resolved_case){0};
}

static int resolve_case_operands(resolver *resolver,const pigen_syntax_node *node,resolved_case *out,int clocked)
{
 pigen_semantic_model *model=resolver->model;
 pigen_expr_id selector=resolve_procedural_case_value(resolver,node->as.procedural_case.selector,node->location,clocked);
 *out=(resolved_case){0};
 if(selector.index==PIGEN_INVALID_ID) return 0;
 out->values=pigen_resize(NULL,sizeof(*out->values));out->values[out->value_count++]=selector;
 for(pigen_syntax_id at=node->first_child;at.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,at);
  resolved_case_item group={item->first_child,item->location.source_span,out->value_count,0};
  while(group.body.index!=PIGEN_INVALID_ID && pigen_syntax_get(resolver->syntax,group.body)->kind==PIGEN_SYNTAX_PROCEDURAL_CASE_LABEL)
  {
   const pigen_syntax_node *label=pigen_syntax_get(resolver->syntax,group.body);
   pigen_expr_id value=resolve_procedural_case_value(resolver,label->as.generate_case_label.value,label->location,clocked);
   if(value.index==PIGEN_INVALID_ID) goto bad;
   out->values=pigen_resize(out->values,(out->value_count+1)*sizeof(*out->values));
   out->values[out->value_count++]=value;group.label_count++;group.body=label->next_sibling;
  }
  if(group.body.index==PIGEN_INVALID_ID || pigen_syntax_get(resolver->syntax,group.body)->next_sibling.index!=PIGEN_INVALID_ID ||
   (!group.label_count && !item->as.generate_case_item.is_default))
  {fail_location(resolver,item->location,"invalid procedural case item body");goto bad;}
  out->items=pigen_resize(out->items,(out->item_count+1)*sizeof(*out->items));out->items[out->item_count++]=group;
  at=item->next_sibling;
 }
 int all_signed=1;
 for(size_t i=0;i<out->value_count;i++)
  if(!procedural_case_width(resolver,out->values[i],&out->width,&all_signed,node->location)) goto bad;
 for(size_t i=0;i<out->value_count;i++)
 {
  out->values[i]=pigen_resolve_case_context(model,out->values[i],(size_t)out->width,
   all_signed ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED);
  if(out->values[i].index==PIGEN_INVALID_ID) {fail_location(resolver,node->location,"invalid procedural case comparison context");goto bad;}
 }
 return 1;
bad:
 free_resolved_case(out);return 0;
}

static pigen_procedural_id case_tree(resolver *resolver,pigen_process_id process,
 const pigen_syntax_node *node,const resolved_case *selection,const pigen_procedural_id *bodies)
{
 pigen_semantic_model *model=resolver->model;
 pigen_procedural_id *items=NULL,result=INVALID_ID(pigen_procedural_id);size_t count=0;
 for(size_t i=0;i<selection->item_count;i++)
 {
  resolved_case_item group=selection->items[i];
  for(size_t j=0;j<(group.label_count ? group.label_count : 1);j++)
  {
   pigen_procedural_id item=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_CASE_ITEM,
    group.label_count ? selection->values[group.first_label+j] : INVALID_ID(pigen_expr_id),
    INVALID_ID(pigen_transfer_id),&bodies[i],1,group.span);
   if(item.index==PIGEN_INVALID_ID) goto done;
   model->procedural[item.index].case_continuation=j!=0;
   items=pigen_resize(items,(count+1)*sizeof(*items));items[count++]=item;
  }
 }
 pigen_procedural_kind kind=node->as.procedural_case.wildcard==1 ? PIGEN_PROCEDURAL_CASEZ :
  node->as.procedural_case.wildcard==2 ? PIGEN_PROCEDURAL_CASEX : PIGEN_PROCEDURAL_CASE;
 result=pigen_procedural_add(model,process,kind,selection->values[0],INVALID_ID(pigen_transfer_id),items,count,node->location.source_span);
 if(result.index!=PIGEN_INVALID_ID) model->procedural[result.index].case_qualifier=node->as.procedural_case.qualifier;
done:
 free(items);return result;
}

static pigen_procedural_id resolve_combinational_case(resolver *resolver,
 pigen_module_id module,pigen_process_id process,const pigen_syntax_node *node,size_t depth)
{
 resolved_case selection;
 pigen_procedural_id result=INVALID_ID(pigen_procedural_id);
 if(!resolve_case_operands(resolver,node,&selection,0)) return result;
 pigen_procedural_id *bodies=pigen_resize(NULL,selection.item_count*sizeof(*bodies));
 for(size_t i=0;i<selection.item_count;i++)
 {
  bodies[i]=resolve_combinational_statement(resolver,module,process,selection.items[i].body,depth+2);
  if(bodies[i].index==PIGEN_INVALID_ID) goto done;
 }
 result=case_tree(resolver,process,node,&selection,bodies);
done:
 free(bodies);free_resolved_case(&selection);return result;
}

static pigen_expr_id case_binary(pigen_semantic_model *model,pigen_binary_operator op,
 pigen_expr_id left,pigen_expr_id right,pigen_source_span span)
{
 const pigen_semantic_expr *l=pigen_expr_get(model,left),*r=pigen_expr_get(model,right);
 pigen_binary_resolution resolution;
 if(!l || !r || !pigen_data_type_resolve_binary_operation(model,op,l->data_type,r->data_type,&resolution)) return INVALID_ID(pigen_expr_id);
 if(resolution.left_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
  left=pigen_expr_add_conversion(model,resolution.left_conversion,left,span);
 if(resolution.right_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
  right=pigen_expr_add_conversion(model,resolution.right_conversion,right,span);
 return pigen_expr_add_binary(model,resolution.operation,left,right,span);
}

/* Native wildcard cases ignore X/Z on either side as prescribed by their
 * mode. Case equality makes every bit test known; balanced conjunction keeps
 * large packed selectors within the ordinary expression depth budget. */
static pigen_expr_id case_match(resolver *resolver,pigen_expr_id selector,pigen_expr_id label,
 int wildcard,uint64_t width,pigen_source_span span)
{
 pigen_semantic_model *model=resolver->model;
 pigen_data_type_id st=pigen_expr_get(model,selector)->data_type,lt=pigen_expr_get(model,label)->data_type;
 if(!wildcard || (pigen_data_type_state_domain(model,st)==PIGEN_DATA_TYPE_STATE_TWO &&
  pigen_data_type_state_domain(model,lt)==PIGEN_DATA_TYPE_STATE_TWO))
  return case_binary(model,PIGEN_BINARY_CASE_EQUAL,selector,label,span);
 pigen_expr_id *terms=pigen_resize(NULL,(size_t)width*sizeof(*terms)),result=INVALID_ID(pigen_expr_id);
 pigen_data_type_id index_type=pigen_data_type_sized_logic(model,32,PIGEN_SIGN_UNSIGNED);
 pigen_bit_state z=PIGEN_BIT_Z,x=PIGEN_BIT_X;
 pigen_expr_id zv=pigen_expr_add_bits(model,&z,1,pigen_data_type_boolean(model),span);
 pigen_expr_id xv=pigen_expr_add_bits(model,&x,1,pigen_data_type_boolean(model),span);
 for(size_t i=0;i<(size_t)width;i++)
 {
  pigen_expr_id index=pigen_expr_add_integer(model,i,index_type,span);
  pigen_expr_id a=width==1 ? selector : pigen_expr_add_index(model,selector,index,span);
  pigen_expr_id b=width==1 ? label : pigen_expr_add_index(model,label,index,span);
  if(a.index==PIGEN_INVALID_ID || b.index==PIGEN_INVALID_ID) goto done;
  pigen_expr_id term=case_binary(model,PIGEN_BINARY_CASE_EQUAL,a,b,span);
  term=case_binary(model,PIGEN_BINARY_LOGICAL_OR,term,case_binary(model,PIGEN_BINARY_CASE_EQUAL,a,zv,span),span);
  term=case_binary(model,PIGEN_BINARY_LOGICAL_OR,term,case_binary(model,PIGEN_BINARY_CASE_EQUAL,b,zv,span),span);
  if(wildcard==2)
  {
   term=case_binary(model,PIGEN_BINARY_LOGICAL_OR,term,case_binary(model,PIGEN_BINARY_CASE_EQUAL,a,xv,span),span);
   term=case_binary(model,PIGEN_BINARY_LOGICAL_OR,term,case_binary(model,PIGEN_BINARY_CASE_EQUAL,b,xv,span),span);
  }
  if(term.index==PIGEN_INVALID_ID) goto done;
  terms[i]=term;
 }
 for(size_t count=(size_t)width;count>1;)
 {
  size_t next=0;
  for(size_t i=0;i<count;i+=2)
  {
   pigen_expr_id term=i+1<count ? case_binary(model,PIGEN_BINARY_LOGICAL_AND,terms[i],terms[i+1],span) : terms[i];
   if(term.index==PIGEN_INVALID_ID) goto done;
   terms[next++]=term;
  }
  count=next;
 }
 result=terms[0];
done:
 free(terms);return result;
}

static int resolve_clocked_case(resolver *resolver,pigen_module_id module,
 pigen_process_id process,pigen_clock_domain_id domain,const pigen_syntax_node *node,
 pigen_predicate_id guard,pigen_procedural_id *checks)
{
 pigen_semantic_model *model=resolver->model;
 resolved_case selection;
 if(!resolve_case_operands(resolver,node,&selection,1)) return 0;
 pigen_procedural_id *bodies=checks ? pigen_resize(NULL,selection.item_count*sizeof(*bodies)) : NULL;
 pigen_expr_id *matches=pigen_resize(NULL,selection.item_count*sizeof(*matches));
 pigen_expr_id zero=pigen_expr_add_integer(model,0,pigen_data_type_boolean(model),node->location.source_span);
 pigen_expr_id entry_live=INVALID_ID(pigen_expr_id);int ok=0;
 /* Even a default-only case evaluates its selector. Preserve that read as
  * a consuming dependency while keeping the guard's value unconditionally
  * true; repeated selector/payload reads still deduplicate by base token. */
 pigen_expr_id one=pigen_expr_add_integer(model,1,pigen_data_type_boolean(model),node->location.source_span);
 pigen_expr_id evaluated=pigen_expr_add_evaluated_value(model,one,selection.values[0],1,node->location.source_span);
 guard=pigen_predicate_and_condition(model,guard,evaluated,1);
 if(guard.index==PIGEN_INVALID_ID) goto done;
 if(!checks && node->as.procedural_case.qualifier!=PIGEN_CASE_UNQUALIFIED)
 {fail_location(resolver,node->location,"qualified case requires an ordered clocked check context");goto done;}
 if(checks && node->as.procedural_case.qualifier!=PIGEN_CASE_UNQUALIFIED &&
  (resolver->clocked_loop || (resolver->in_fsm && resolver->fsm_terminated.index!=PIGEN_INVALID_ID)))
 {
  entry_live=predicate_expression(resolver,statement_guard(resolver,pigen_predicate_true(model),node->location.source_span),node->location.source_span);
  if(entry_live.index==PIGEN_INVALID_ID) goto done;
 }
 for(size_t i=0;i<selection.item_count;i++)
 {
  resolved_case_item item=selection.items[i];matches[i]=zero;
  for(size_t j=0;j<item.label_count;j++)
  {
   pigen_expr_id match=case_match(resolver,selection.values[0],selection.values[item.first_label+j],
    node->as.procedural_case.wildcard,selection.width,item.span);
   matches[i]=case_binary(model,PIGEN_BINARY_LOGICAL_OR,matches[i],match,item.span);
   if(matches[i].index==PIGEN_INVALID_ID) goto done;
  }
 }
 pigen_predicate_id fallback=guard;
 for(size_t i=0;i<selection.item_count;i++)
  if(selection.items[i].label_count)
  {
   fallback=pigen_predicate_and_condition(model,fallback,matches[i],0);
   if(fallback.index==PIGEN_INVALID_ID) goto done;
  }
 pigen_predicate_id remaining=guard;
 for(size_t i=0;i<selection.item_count;i++)
 {
  resolved_case_item item=selection.items[i];
  pigen_predicate_id branch=item.label_count ? pigen_predicate_and_condition(model,remaining,matches[i],1) :
   fallback;
  if(branch.index==PIGEN_INVALID_ID || !resolve_procedural_statement(resolver,module,process,domain,item.body,branch,checks ? &bodies[i] : NULL)) goto done;
  if(item.label_count) remaining=pigen_predicate_and_condition(model,remaining,matches[i],0);
  if(remaining.index==PIGEN_INVALID_ID) goto done;
 }
 if(checks)
 {
  *checks=case_tree(resolver,process,node,&selection,bodies);
  if(checks->index==PIGEN_INVALID_ID) goto done;
  if(entry_live.index!=PIGEN_INVALID_ID)
  {
   pigen_procedural_id body=*checks;
   *checks=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_IF,entry_live,
    INVALID_ID(pigen_transfer_id),&body,1,node->location.source_span);
   if(checks->index==PIGEN_INVALID_ID) goto done;
  }
 }
 ok=1;
done:
 free(matches);free(bodies);free_resolved_case(&selection);
 if(!ok && (!resolver->error || !resolver->error->message))
  return fail_location(resolver,node->location,"cannot construct typed clocked case selection");
 return ok;
}

static pigen_expr_id resolve_combinational_control(resolver *resolver,
	pigen_syntax_expr_id expression,pigen_syntax_location location,int repeat)
{
	pigen_semantic_model *model=resolver->model;
	pigen_expr_id condition;
	condition=pigen_resolve_control_expression(resolver->syntax,model,resolver->expression_scope,
		expression,
		resolver->policy,resolver->error);
	const pigen_semantic_expr *value=pigen_expr_get(model,condition);
	const pigen_semantic_shape *shape=value ? pigen_shape_get(model,value->shape) : NULL;
	if (!value || !shape || shape->dimension_count ||
		(!pigen_data_type_is_integral(model,value->data_type) &&
		pigen_data_type_numerical_interpretation(model,value->data_type)!=PIGEN_NUMERICAL_EXACT_INTEGER))
	{
		fail_location(resolver,location,"always_comb condition requires an integral scalar expression");
		return INVALID_ID(pigen_expr_id);
	}
	pigen_expression_use_analysis uses={0};
	int ok=pigen_analyze_expression_uses(model,condition,pigen_predicate_true(model),PIGEN_EXPRESSION_USE_READ,&uses);
	for (size_t i=0;ok && i<uses.signal_count;i++)
	{
		const pigen_semantic_signal *signal=pigen_signal_get(model,uses.signals[i].signal);
		if (!signal || !pigen_transfer_type_descriptor_get(signal->transfer_type)->is_static) ok=0;
	}
	pigen_free_expression_use_analysis(&uses);
	if (!ok) { fail_location(resolver,location,"always_comb buffered conditions require peek");return INVALID_ID(pigen_expr_id); }
	/* Normalize SV vector truth explicitly, preserving branch entry for
	 * zero, nonzero and unknown values without a width-truncating cast. */
	if (repeat)
	{
		pigen_data_type_id type=pigen_expr_get(model,condition)->data_type;
		if (pigen_data_type_is_integral(model,type))
		{
			uint64_t width=0;
			if (!pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,type),&width))
				return INVALID_ID(pigen_expr_id);
			if (width<32)
			{
				pigen_signedness sign=pigen_data_type_signedness(model,type)==PIGEN_SIGN_SIGNED ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED;
				pigen_data_type_id target=pigen_data_type_sized_integral(model,32,sign,pigen_data_type_state_domain(model,type));
				pigen_conversion conversion;
				if (!pigen_data_type_resolve_explicit_conversion(model,type,target,&conversion)) return INVALID_ID(pigen_expr_id);
				condition=pigen_expr_add_conversion(model,conversion,condition,location.source_span);
				if (condition.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_expr_id);
			}
		}
	}
	if (!repeat)
	{
		pigen_data_type_id type=pigen_expr_get(model,condition)->data_type;
		pigen_unary_resolution truth;
		int exact=pigen_data_type_numerical_interpretation(model,type)==PIGEN_NUMERICAL_EXACT_INTEGER;
		if (!pigen_data_type_resolve_unary_operation(model,exact ? PIGEN_UNARY_LOGICAL_NOT : PIGEN_UNARY_REDUCTION_OR,type,&truth))
			return INVALID_ID(pigen_expr_id);
		condition=pigen_expr_add_unary(model,truth.operation,condition,location.source_span);
		if (exact)
		{
			if (!pigen_data_type_resolve_unary_operation(model,PIGEN_UNARY_LOGICAL_NOT,
				truth.operation.result_data_type,&truth)) return INVALID_ID(pigen_expr_id);
			condition=pigen_expr_add_unary(model,truth.operation,condition,location.source_span);
		}
		if (condition.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_expr_id);
	}
	return condition;
}

/* Fixed foreach bounds become a flat ordinal FOR. Reconstructing all named
 * indices at iteration entry preserves one multidimensional break/continue
 * target and resolves the authored body exactly once, including static locals. */
static pigen_expr_id foreach_integer(pigen_semantic_model *model,uint64_t value,
	size_t width,int sign,pigen_source_span span)
{
	pigen_data_type_id type=pigen_data_type_sized_integral(model,width,
		sign ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,sign ? PIGEN_DATA_TYPE_STATE_FOUR : PIGEN_DATA_TYPE_STATE_TWO);
	if(sign && width<=64)
	{
		pigen_bit_state states[64];
		for(size_t i=0;i<width;i++) states[i]=(value>>i)&1 ? PIGEN_BIT_ONE : PIGEN_BIT_ZERO;
		return pigen_expr_add_bits(model,states,width,type,span);
	}
	return pigen_expr_add_integer(model,value,type,span);
}

static pigen_expr_id foreach_binary(pigen_semantic_model *model,pigen_binary_operator op,
	pigen_expr_id left,pigen_expr_id right,pigen_source_span span)
{
	const pigen_semantic_expr *a=pigen_expr_get(model,left),*b=pigen_expr_get(model,right);
	pigen_binary_resolution result;
	if(!a || !b || !pigen_data_type_resolve_binary_operation(model,op,a->data_type,b->data_type,&result)) return INVALID_ID(pigen_expr_id);
	if(result.left_conversion.kind!=PIGEN_CONVERSION_IDENTITY) left=pigen_expr_add_conversion(model,result.left_conversion,left,span);
	if(result.right_conversion.kind!=PIGEN_CONVERSION_IDENTITY) right=pigen_expr_add_conversion(model,result.right_conversion,right,span);
	return pigen_expr_add_binary(model,result.operation,left,right,span);
}

static pigen_expr_id foreach_variable(resolver *resolver,pigen_module_id module,
	pigen_syntax_id syntax,pigen_source_span name,pigen_source_span span,size_t width,int sign,pigen_signal_id *signal)
{
	pigen_semantic_model *model=resolver->model;
	pigen_data_type_id type=pigen_data_type_sized_integral(model,width,sign ? PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED,sign ? PIGEN_DATA_TYPE_STATE_FOUR : PIGEN_DATA_TYPE_STATE_TWO);
	pigen_symbol_id symbol;
	pigen_source_span declaration=name.source.index==span.source.index ? span : name;
	if(pigen_symbol_declare(model,resolver->expression_scope,PIGEN_SYMBOL_SIGNAL,type,name,declaration,&symbol,NULL)!=PIGEN_DECLARE_OK) return INVALID_ID(pigen_expr_id);
	*signal=pigen_signal_add(model,syntax,module,symbol,type,pigen_semantic_scalar_shape(model),INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC,PIGEN_SEMANTIC_INTERNAL,declaration);
	return signal->index==PIGEN_INVALID_ID ? INVALID_ID(pigen_expr_id) : pigen_expr_add_symbol(model,symbol,type,span);
}

static pigen_procedural_id foreach_assignment(resolver *resolver,pigen_module_id module,
	pigen_process_id process,pigen_syntax_id syntax,pigen_expr_id destination,pigen_expr_id value,pigen_source_span span)
{
	pigen_semantic_model *model=resolver->model;
	pigen_expression_use_analysis analysis={0};
	pigen_lvalue_id lvalue=pigen_lvalue_resolve(model,destination);
	pigen_predicate_id guard=pigen_predicate_true(model);
	const pigen_semantic_lvalue *target=pigen_lvalue_get(model,lvalue);
	const pigen_semantic_expr *source=pigen_expr_get(model,value);pigen_conversion conversion;
	if(!target || !source || !pigen_data_type_resolve_explicit_conversion(model,source->data_type,target->data_type,&conversion)) return INVALID_ID(pigen_procedural_id);
	if(conversion.kind!=PIGEN_CONVERSION_IDENTITY) value=pigen_expr_add_conversion(model,conversion,value,span);
	if(!pigen_analyze_lvalue_uses(model,lvalue,guard,&analysis) ||
		!pigen_analyze_expression_uses(model,value,guard,PIGEN_EXPRESSION_USE_READ,&analysis))
	{pigen_free_expression_use_analysis(&analysis);return INVALID_ID(pigen_procedural_id);}
	pigen_transfer_signal_use *uses=NULL;size_t count=0,capacity=0;
	for(size_t i=0;i<analysis.signal_count;i++)
	{
		unsigned contexts=analysis.signals[i].contexts;
		if(contexts & PIGEN_EXPRESSION_USE_LVALUE) add_transfer_signal_use(&uses,&count,&capacity,analysis.signals[i].signal,PIGEN_TRANSFER_SIGNAL_WRITE);
		if(contexts & (PIGEN_EXPRESSION_USE_READ|PIGEN_EXPRESSION_USE_INDEX|PIGEN_EXPRESSION_USE_TYPE)) add_transfer_signal_use(&uses,&count,&capacity,analysis.signals[i].signal,PIGEN_TRANSFER_SIGNAL_READ);
	}
	pigen_free_expression_use_analysis(&analysis);
	pigen_transfer_id assignment=pigen_transfer_add(model,syntax,module,process,lvalue,value,guard,INVALID_ID(pigen_clock_domain_id),uses,count,span);free(uses);
	return assignment.index==PIGEN_INVALID_ID ? INVALID_ID(pigen_procedural_id) :
		pigen_procedural_add(model,process,PIGEN_PROCEDURAL_ASSIGN,INVALID_ID(pigen_expr_id),assignment,NULL,0,span);
}

/* Static body declarations retain one object while endpoint lanes specialize.
 * Declaration types and bounds cannot depend on iterator values. */
static int foreach_iterator_in_extent(resolver *resolver,pigen_syntax_location location)
{
	for(size_t i=0;i<resolver->syntax->expressions.node_count;i++)
	{
		const pigen_syntax_expr *node=&resolver->syntax->expressions.nodes[i];
		if(node->kind!=PIGEN_SYNTAX_EXPR_NAME ||
			node->location.extent.first.index<location.extent.first.index ||
			node->location.extent.after.index>location.extent.after.index) continue;
		const pigen_expanded_token *token=pigen_expanded_token_get(resolver->syntax->expanded,node->as.atom.token);size_t length=0;
		const char *name=pigen_expanded_token_text(resolver->syntax->expanded,token,&length);
		pigen_symbol_id symbol=name ? pigen_symbol_lookup_text(resolver->model,resolver->expression_scope,name,length) : INVALID_ID(pigen_symbol_id);
		for(size_t j=0;j<resolver->foreach_parameter_count;j++) if(symbol.index==resolver->foreach_parameters[j].index) return 1;
	}
	return 0;
}

static int foreach_declaration_uses_iterator(resolver *resolver,const pigen_syntax_node *node)
{
	pigen_syntax_type_id type=node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION ? node->as.signal_declaration.data_type :
		node->kind==PIGEN_SYNTAX_TYPEDEF ? node->as.type_definition.type :
		node->as.parameter.has_type ? node->as.parameter.data_type : INVALID_ID(pigen_syntax_type_id);
	const pigen_syntax_type *written=pigen_syntax_type_get(&resolver->syntax->types,type);
	if(written && foreach_iterator_in_extent(resolver,written->location)) return 1;
	if(node->kind==PIGEN_SYNTAX_TYPEDEF)
		for(size_t i=0;i<PIGEN_PROTOCOL_OPTION_COUNT;i++)
			if(node->as.type_definition.protocol.present & (1u<<i))
			{
				const pigen_syntax_expr *value=pigen_syntax_expr_get(&resolver->syntax->expressions,node->as.type_definition.protocol.options[i]);
				if(value && foreach_iterator_in_extent(resolver,value->location)) return 1;
			}
	if(node->kind==PIGEN_SYNTAX_PARAMETER)
	{
		const pigen_syntax_expr *value=pigen_syntax_expr_get(&resolver->syntax->expressions,node->as.parameter.value);
		return value && foreach_iterator_in_extent(resolver,value->location);
	}
	if(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION)
		for(pigen_syntax_id id=node->first_child;id.index!=PIGEN_INVALID_ID;)
		{
			const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,id);
			if(item->kind==PIGEN_SYNTAX_SIGNAL_DECLARATOR)
			{
				const pigen_syntax_shape_dimension *dimensions=pigen_syntax_declarator_shape_dimensions(resolver->syntax,item);
				for(size_t i=0;dimensions && i<item->as.signal_declarator.dimension_count;i++)
					if(foreach_iterator_in_extent(resolver,dimensions[i].location)) return 1;
			}
			id=item->next_sibling;
		}
	return 0;
}

static int resolve_foreach_declaration(resolver *resolver,pigen_module_id module,pigen_process_id process,
	pigen_syntax_id syntax,const pigen_syntax_node *node)
{
	pigen_semantic_model *model=resolver->model;
	if(resolver->native_foreach_depth && foreach_declaration_uses_iterator(resolver,node))
		return fail_location(resolver,node->location,"foreach iteration variables are not declaration constants");
	if(resolver->native_foreach_depth && !(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION && node->as.signal_declaration.is_automatic))
		for(size_t i=0;i<resolver->foreach_declaration_count;i++)
		{
			foreach_declaration entry=resolver->foreach_declarations[i];
			if(entry.syntax.index!=syntax.index || entry.process.index!=process.index) continue;
			for(size_t j=entry.first_symbol;j<entry.after_symbol;j++)
			{
				pigen_symbol original=model->symbols[j];pigen_symbol_id alias;
				if(original.scope.index!=entry.scope.index) continue;
				if(pigen_symbol_bind_procedural_alias(model,resolver->expression_scope,(pigen_symbol_id){(uint32_t)j},&alias)!=PIGEN_DECLARE_OK)
					return fail_location(resolver,node->location,"duplicate foreach body declaration");
			}
			return 1;
		}
	size_t first=model->symbol_count;
	int ok=node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION ? add_signal_declaration_in_scope(resolver,module,resolver->expression_scope,node) :
		node->kind==PIGEN_SYNTAX_TYPEDEF ? add_typedef(resolver,resolver->expression_scope,node) : add_parameter(resolver,module,syntax,node);
	if(ok && resolver->native_foreach_depth && !(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION && node->as.signal_declaration.is_automatic))
	{
		resolver->foreach_declarations=pigen_resize(resolver->foreach_declarations,(resolver->foreach_declaration_count+1)*sizeof(*resolver->foreach_declarations));
		resolver->foreach_declarations[resolver->foreach_declaration_count++]=(foreach_declaration){syntax,process,resolver->expression_scope,first,model->symbol_count};
	}
	return ok;
}

static pigen_procedural_id resolve_native_foreach_body(resolver *resolver,pigen_module_id module,pigen_process_id process,
	pigen_syntax_id syntax,const pigen_syntax_node *node,pigen_syntax_id body,pigen_expr_id counter,
	const pigen_source_span *names,const int64_t *left,const int64_t *right,const size_t *lengths,const size_t *strides,
	size_t indices,size_t total,size_t depth)
{
	pigen_semantic_model *model=resolver->model;pigen_source_span span=node->location.source_span;
	if(total>=SIZE_MAX/sizeof(pigen_procedural_id) || total>resolver->policy->maximum_generated_bits-resolver->foreach_specializations)
	{fail_location(resolver,node->location,"native foreach exceeds the process specialization limit");return INVALID_ID(pigen_procedural_id);}
	resolver->foreach_specializations+=total;
	pigen_procedural_id *items=pigen_resize(NULL,(total+1)*sizeof(*items));
	pigen_scope_id saved=resolver->expression_scope;size_t saved_parameters=resolver->foreach_parameter_count;
	int ok=1;resolver->native_foreach_depth++;
	for(size_t ordinal=0;ordinal<total;ordinal++)
	{
		resolver->expression_scope=pigen_scope_add(model,saved,span);
		if(resolver->expression_scope.index==PIGEN_INVALID_ID) {ok=0;break;}
		model->scopes[resolver->expression_scope.index].is_procedural=1;
		resolver->foreach_parameter_count=saved_parameters;
		for(size_t i=0;i<indices;i++) if(names[i].source.index!=PIGEN_INVALID_ID)
		{
			size_t offset=(ordinal/strides[i])%lengths[i];
			int64_t coordinate=left[i]>right[i] ? left[i]-(int64_t)offset : left[i]+(int64_t)offset;
			pigen_expr_id value=foreach_integer(model,(uint32_t)coordinate,32,1,span);
			pigen_symbol_id symbol;
			if(pigen_symbol_declare(model,resolver->expression_scope,PIGEN_SYMBOL_PARAMETER,pigen_expr_get(model,value)->data_type,
				names[i],span,&symbol,NULL)!=PIGEN_DECLARE_OK || pigen_parameter_add(model,syntax,module,symbol,value,1,span).index==PIGEN_INVALID_ID)
			{ok=0;break;}
			resolver->foreach_parameters[resolver->foreach_parameter_count++]=symbol;
		}
		if(!ok) break;
		pigen_procedural_id statement=resolve_combinational_statement(resolver,module,process,body,depth+1);
		if(statement.index==PIGEN_INVALID_ID) {ok=0;break;}
		items[ordinal]=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_CASE_ITEM,foreach_integer(model,ordinal,64,0,span),INVALID_ID(pigen_transfer_id),&statement,1,span);
		if(items[ordinal].index==PIGEN_INVALID_ID) {ok=0;break;}
	}
	resolver->expression_scope=saved;resolver->foreach_parameter_count=saved_parameters;resolver->native_foreach_depth--;
	pigen_procedural_id result=INVALID_ID(pigen_procedural_id);
	if(ok)
	{
		pigen_procedural_id empty=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),NULL,0,span);
		items[total]=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_CASE_ITEM,INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),&empty,1,span);
		result=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_CASE,counter,INVALID_ID(pigen_transfer_id),items,total+1,span);
	}
	free(items);return result;
}

typedef struct {
 int native;
 pigen_syntax_id body;
 pigen_source_span names[128];
 int64_t left[128],right[128];
 size_t lengths[128],strides[128],indices,total;
} resolved_foreach;

/* Inspect bounds without reading an endpoint payload or consuming a token. */
static int resolve_foreach_bounds(resolver *resolver,const pigen_syntax_node *node,
 resolved_foreach *result)
{
	pigen_semantic_model *model=resolver->model;
	pigen_protocol_array_view view={0};
	int native=pigen_resolve_protocol_array_view(resolver->syntax,model,resolver->expression_scope,node->as.procedural_loop.control,resolver->policy,&view);
	pigen_analyzed_expr_arena arena={0};pigen_analyzed_expr_id target;
	pigen_analyzed_expr value={.shape={PIGEN_INVALID_ID},.data_type={PIGEN_INVALID_ID}};
	int ok=native==1 && view.dimension_count;
	if(!native)
	{
		ok=pigen_analyze_expression(resolver->syntax,model,resolver->expression_scope,node->as.procedural_loop.control,
			0,PIGEN_LITERAL_DOMAIN_CASE,resolver->policy,&arena,&target,resolver->error);
		if(ok) value=arena.nodes[target.index];
	}
	if(!ok) {pigen_free_protocol_array_view(&view);pigen_free_analyzed_expr_arena(&arena);fail_location(resolver,node->location,"foreach requires a fixed-size array or native collection");return 0;}
	const pigen_syntax_expr *written=pigen_syntax_expr_get(&resolver->syntax->expressions,node->as.procedural_loop.control);
	if(!written || (written->kind!=PIGEN_SYNTAX_EXPR_NAME && written->kind!=PIGEN_SYNTAX_EXPR_MEMBER && written->kind!=PIGEN_SYNTAX_EXPR_INDEX))
	{pigen_free_protocol_array_view(&view);pigen_free_analyzed_expr_arena(&arena);fail_location(resolver,node->location,"foreach target must designate an array variable");return 0;}
	pigen_source_span array_name={.source={PIGEN_INVALID_ID}};
	if(written && written->kind==PIGEN_SYNTAX_EXPR_NAME) array_name=token_spelling(resolver,written->as.atom.token);
	else if(written && written->kind==PIGEN_SYNTAX_EXPR_MEMBER) array_name=token_spelling(resolver,written->as.member.member);

	const pigen_semantic_shape *shape=pigen_shape_get(model,value.shape);
	size_t unpacked=native ? view.dimension_count : shape ? shape->dimension_count : 0,packed=native ? 0 : pigen_data_type_dimension_count(model,value.data_type);
	size_t dimensions=unpacked+packed;
	int64_t left[128]={0},right[128]={0};size_t lengths[128],strides[128];
	pigen_source_span names[128];pigen_syntax_id body=node->first_child;size_t indices=0,named=0,total=1;
	const pigen_shape_dimension *unpacked_dimensions=pigen_shape_dimensions(model,value.shape);
	const pigen_packed_dimension *packed_dimensions=pigen_data_type_dimensions(model,value.data_type);
	for(;body.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *index=pigen_syntax_get(resolver->syntax,body);
		if(index->kind!=PIGEN_SYNTAX_FOREACH_INDEX) break;
		if(indices>=dimensions || indices==128) {ok=0;break;}
		names[indices]=index->as.foreach_index.name.index==PIGEN_INVALID_ID ? (pigen_source_span){.source={PIGEN_INVALID_ID}} : token_spelling(resolver,index->as.foreach_index.name);
		if(names[indices].source.index!=PIGEN_INVALID_ID)
		{
			if(array_name.source.index!=PIGEN_INVALID_ID && spelling_text_equal(resolver,names[indices],array_name)) {ok=0;break;}
			named++;
			if(native) {left[indices]=view.dimensions[indices].left;right[indices]=view.dimensions[indices].right;}
			else if(indices<unpacked)
			{
				const pigen_shape_dimension *d=&unpacked_dimensions[indices];uint64_t count;
				if(d->form==PIGEN_SHAPE_DIMENSION_COUNT)
				{ok=pigen_const_expr_evaluate_u64(model,d->as.count,&count) && count && count<=(uint64_t)INT32_MAX+1;left[indices]=0;right[indices]=ok ? (int64_t)count-1 : 0;}
				else ok=pigen_const_expr_evaluate_i64(model,d->as.range.left,&left[indices]) && pigen_const_expr_evaluate_i64(model,d->as.range.right,&right[indices]);
			}
			else
			{const pigen_packed_dimension *d=&packed_dimensions[indices-unpacked];ok=pigen_const_expr_evaluate_i64(model,d->left,&left[indices]) && pigen_const_expr_evaluate_i64(model,d->right,&right[indices]);}
			if(!ok || left[indices]<INT32_MIN || left[indices]>INT32_MAX || right[indices]<INT32_MIN || right[indices]>INT32_MAX) {ok=0;break;}
			uint64_t count=(uint64_t)(left[indices]>right[indices] ? left[indices]-right[indices] : right[indices]-left[indices])+1;
			if(count>resolver->policy->maximum_generated_bits/total) {ok=0;break;}
			lengths[indices]=(size_t)count;total*=lengths[indices];
		}
		else lengths[indices]=1;
		indices++;body=index->next_sibling;
	}
	pigen_free_protocol_array_view(&view);pigen_free_analyzed_expr_arena(&arena);
	if(!ok || !named || body.index==PIGEN_INVALID_ID || resolver->foreach_readonly_count+named>128 || resolver->foreach_parameter_count+named>128)
	{fail_location(resolver,node->location,"foreach requires named indices within fixed 32-bit bounds and the generation limit");return 0;}
	size_t stride=1;for(size_t i=indices;i--;) {strides[i]=stride;stride*=lengths[i];}
 result->native=native;result->body=body;result->indices=indices;result->total=total;
 memcpy(result->names,names,indices*sizeof(*names));
 memcpy(result->left,left,indices*sizeof(*left));memcpy(result->right,right,indices*sizeof(*right));
 memcpy(result->lengths,lengths,indices*sizeof(*lengths));memcpy(result->strides,strides,indices*sizeof(*strides));
 return 1;
}

/* Constant coordinate bindings keep native endpoints structural. Each body
 * remains an ordinary ordered group of transfers, with its own fire events. */
static int resolve_clocked_foreach(resolver *resolver,pigen_module_id module,
 pigen_process_id process,pigen_clock_domain_id domain,pigen_syntax_id syntax,
 const pigen_syntax_node *node,pigen_predicate_id guard,pigen_procedural_id *checks)
{
 resolved_foreach selection={0};
 if(!resolve_foreach_bounds(resolver,node,&selection)) return 0;
 size_t total=selection.total;
 if(total>resolver->policy->maximum_generated_bits-resolver->foreach_specializations ||
  total>SIZE_MAX/sizeof(pigen_procedural_id))
  return fail_location(resolver,node->location,"clocked foreach exceeds the process specialization limit");
 resolver->foreach_specializations+=total;
 pigen_semantic_model *model=resolver->model;pigen_source_span span=node->location.source_span;
 pigen_scope_id saved=resolver->expression_scope;size_t parameters=resolver->foreach_parameter_count;
 pigen_procedural_id *bodies=checks ? pigen_resize(NULL,total*sizeof(*bodies)) : NULL;
 clocked_loop_frame loop={resolver->clocked_loop,{PIGEN_INVALID_ID},{PIGEN_INVALID_ID}};
 resolver->clocked_loop=&loop;
 int ok=1;
 for(size_t ordinal=0;ordinal<total;ordinal++)
 {
  loop.continued=(pigen_expr_id){PIGEN_INVALID_ID};
  resolver->expression_scope=pigen_scope_add(model,saved,span);
  if(resolver->expression_scope.index==PIGEN_INVALID_ID) {ok=0;break;}
  model->scopes[resolver->expression_scope.index].is_procedural=1;
  resolver->foreach_parameter_count=parameters;
  for(size_t i=0;i<selection.indices;i++) if(selection.names[i].source.index!=PIGEN_INVALID_ID)
  {
   size_t offset=(ordinal/selection.strides[i])%selection.lengths[i];
   int64_t coordinate=selection.left[i]>selection.right[i] ? selection.left[i]-(int64_t)offset : selection.left[i]+(int64_t)offset;
   pigen_expr_id value=foreach_integer(model,(uint32_t)coordinate,32,1,span);
   pigen_symbol_id symbol;
   if(value.index==PIGEN_INVALID_ID || pigen_symbol_declare(model,resolver->expression_scope,
    PIGEN_SYMBOL_PARAMETER,pigen_expr_get(model,value)->data_type,selection.names[i],span,&symbol,NULL)!=PIGEN_DECLARE_OK ||
    pigen_parameter_add(model,syntax,module,symbol,value,1,span).index==PIGEN_INVALID_ID)
   {ok=0;break;}
   resolver->foreach_parameters[resolver->foreach_parameter_count++]=symbol;
  }
  if(!ok || !resolve_procedural_statement(resolver,module,process,domain,selection.body,guard,checks ? &bodies[ordinal] : NULL))
  {ok=0;break;}
 }
 resolver->clocked_loop=loop.parent;
 resolver->expression_scope=saved;resolver->foreach_parameter_count=parameters;
 if(ok && checks)
 {
  *checks=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),
   INVALID_ID(pigen_transfer_id),bodies,total,span);
  ok=checks->index!=PIGEN_INVALID_ID;
 }
 free(bodies);
 if(!ok && (!resolver->error || !resolver->error->message))
  return fail_location(resolver,node->location,"cannot resolve clocked foreach body");
 return ok;
}

static pigen_procedural_id resolve_combinational_foreach(resolver *resolver,pigen_module_id module,
 pigen_process_id process,pigen_syntax_id syntax,const pigen_syntax_node *node,size_t depth)
{
 pigen_semantic_model *model=resolver->model;pigen_source_span span=node->location.source_span;
 resolved_foreach selection={0};
 if(!resolve_foreach_bounds(resolver,node,&selection)) return INVALID_ID(pigen_procedural_id);
 int native=selection.native,ok=1;
 pigen_syntax_id body=selection.body;
 size_t indices=selection.indices,total=selection.total;
 const pigen_source_span *names=selection.names;
 const int64_t *left=selection.left,*right=selection.right;
 const size_t *lengths=selection.lengths,*strides=selection.strides;
	char counter_name[96];snprintf(counter_name,sizeof counter_name,"__pigen_foreach_%u_%u",syntax.index,resolver->expression_scope.index);
	pigen_source_id source=pigen_source_add((pigen_source_manager *)model->sources,"<pigen-foreach>",counter_name,strlen(counter_name));
	pigen_signal_id counter_signal;
	pigen_expr_id counter=foreach_variable(resolver,module,syntax,(pigen_source_span){source,0,strlen(counter_name)},span,64,0,&counter_signal);
	if(counter.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_procedural_id);
	pigen_procedural_id initial=foreach_assignment(resolver,module,process,syntax,counter,foreach_integer(model,0,64,0,span),span);
	pigen_procedural_id step=foreach_assignment(resolver,module,process,syntax,counter,foreach_binary(model,PIGEN_BINARY_ADD,counter,foreach_integer(model,1,64,0,span),span),span);
	pigen_procedural_id phases[3];
	phases[0]=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),&initial,1,span);
	phases[1]=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),&step,1,span);
	if(native)
		phases[2]=resolve_native_foreach_body(resolver,module,process,syntax,node,body,counter,names,left,right,lengths,strides,indices,total,depth);
	else
	{
		pigen_procedural_id children[129];size_t count=0,saved=resolver->foreach_readonly_count;
		for(size_t i=0;i<indices;i++) if(names[i].source.index!=PIGEN_INVALID_ID)
		{
			pigen_signal_id signal;
			pigen_expr_id index=foreach_variable(resolver,module,syntax,names[i],span,32,1,&signal);
			if(index.index==PIGEN_INVALID_ID) {ok=0;break;}
			resolver->foreach_readonly[resolver->foreach_readonly_count++]=signal;
			pigen_expr_id position=foreach_binary(model,PIGEN_BINARY_DIVIDE,counter,foreach_integer(model,strides[i],64,0,span),span);
			position=foreach_binary(model,PIGEN_BINARY_MODULO,position,foreach_integer(model,lengths[i],64,0,span),span);
			pigen_conversion conversion;
			if(position.index==PIGEN_INVALID_ID || !pigen_data_type_resolve_explicit_conversion(model,pigen_expr_get(model,position)->data_type,pigen_expr_get(model,index)->data_type,&conversion)) {ok=0;break;}
			position=pigen_expr_add_conversion(model,conversion,position,span);
			pigen_expr_id coordinate=foreach_binary(model,left[i]>right[i] ? PIGEN_BINARY_SUBTRACT : PIGEN_BINARY_ADD,
				foreach_integer(model,(uint32_t)left[i],32,1,span),position,span);
			children[count++]=foreach_assignment(resolver,module,process,syntax,index,coordinate,span);
		}
		if(ok) children[count++]=resolve_combinational_statement(resolver,module,process,body,depth+1);
		resolver->foreach_readonly_count=saved;
		if(!ok) return INVALID_ID(pigen_procedural_id);
		phases[2]=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),children,count,span);
	}
	pigen_expr_id control=foreach_binary(model,PIGEN_BINARY_LESS,counter,foreach_integer(model,total,64,0,span),span);
	return pigen_procedural_add(model,process,PIGEN_PROCEDURAL_FOR,control,INVALID_ID(pigen_transfer_id),phases,3,span);
}

static int reserve_procedural_scope_names(resolver *resolver,pigen_syntax_id syntax,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,syntax);
	if (!node || depth>128) return 0;
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK)
	{
		if (!node->as.procedural_block.has_name) return 1;
		pigen_symbol_id symbol;
		if (pigen_symbol_declare(resolver->model,resolver->expression_scope,PIGEN_SYMBOL_PROCEDURAL_SCOPE,
			INVALID_ID(pigen_data_type_id),token_spelling(resolver,node->as.procedural_block.name),
			node->location.source_span,&symbol,NULL)!=PIGEN_DECLARE_OK)
			return fail_location(resolver,node->location,"duplicate procedural block name");
		return 1;
	}
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOR || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH) return 1;
	for (pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;
		child=pigen_syntax_get(resolver->syntax,child)->next_sibling)
		if (!reserve_procedural_scope_names(resolver,child,depth+1)) return 0;
	return 1;
}

static pigen_expr_id coordinate_literal(pigen_semantic_model *model,int64_t value,pigen_source_span span);

static int automatic_default_assignments(resolver *resolver,pigen_module_id module,pigen_process_id process,
 pigen_syntax_id syntax,pigen_expr_id target,pigen_expr_id value,const int64_t *left,const int64_t *right,
 size_t rank,pigen_procedural_id **children,size_t *count,pigen_source_span span)
{
 if(!rank)
 {
  pigen_procedural_id assignment=foreach_assignment(resolver,module,process,syntax,target,value,span);
  if(assignment.index==PIGEN_INVALID_ID) return 0;
  *children=pigen_resize(*children,(*count+1)*sizeof(**children));(*children)[(*count)++]=assignment;return 1;
 }
 for(int64_t i=*left;;)
 {
  pigen_expr_id index=coordinate_literal(resolver->model,i,span);
  pigen_expr_id selected=pigen_expr_add_index(resolver->model,target,index,span);
  if(selected.index==PIGEN_INVALID_ID || !automatic_default_assignments(resolver,module,process,syntax,
   selected,value,left+1,right+1,rank-1,children,count,span)) return 0;
  if(i==*right) break;
  i+=*left<*right ? 1 : -1;
 }
 return 1;
}

static pigen_procedural_id automatic_declaration_entries(resolver *resolver,pigen_module_id module,
 pigen_process_id process,pigen_syntax_id syntax,const pigen_syntax_node *node,size_t first_signal,int explicit_values)
{
 pigen_semantic_model *model=resolver->model;
 pigen_procedural_id *children=NULL;size_t count=0;
 for(size_t i=first_signal;i<model->signal_count;i++)
 {
  pigen_semantic_signal signal=model->signals[i];uint64_t width;
  if(!signal.is_automatic) continue;
  const pigen_semantic_shape *shape=pigen_shape_get(model,signal.shape);
  size_t rank=shape ? shape->dimension_count : 0,total=1;
  int64_t left[128],right[128];
  if(rank>128 || !pigen_const_expr_evaluate_u64(model,pigen_data_type_packed_width(model,signal.data_type),&width) ||
   !width || width>resolver->policy->maximum_generated_bits || width>SIZE_MAX/sizeof(pigen_bit_state)) goto invalid;
  const pigen_shape_dimension *dimensions=pigen_shape_dimensions(model,signal.shape);
  for(size_t d=0;d<rank;d++)
  {
   uint64_t length;
   if(dimensions[d].form==PIGEN_SHAPE_DIMENSION_COUNT)
   {
    if(!pigen_const_expr_evaluate_u64(model,dimensions[d].as.count,&length) || !length || length>INT64_MAX) goto invalid;
    left[d]=0;right[d]=(int64_t)length-1;
   }
   else
   {
    if(!pigen_const_expr_evaluate_i64(model,dimensions[d].as.range.left,&left[d]) ||
     !pigen_const_expr_evaluate_i64(model,dimensions[d].as.range.right,&right[d])) goto invalid;
    uint64_t distance=left[d]<right[d] ? (uint64_t)right[d]-(uint64_t)left[d] : (uint64_t)left[d]-(uint64_t)right[d];
    if(distance==UINT64_MAX) goto invalid;
    length=distance+1;
   }
   if(length>resolver->policy->maximum_generated_bits/total) goto invalid;
   total*=(size_t)length;
  }
  if(total>resolver->policy->maximum_generated_bits/width) goto invalid;
  pigen_bit_state *bits=pigen_resize(NULL,(size_t)width*sizeof(*bits));
  pigen_state_domain state=pigen_data_type_state_domain(model,signal.data_type);
  int ok=state!=PIGEN_DATA_TYPE_STATE_INVALID;
  for(size_t b=0;b<(size_t)width;b++) bits[b]=state==PIGEN_DATA_TYPE_STATE_TWO ? PIGEN_BIT_ZERO : PIGEN_BIT_X;
  pigen_expr_id value=ok ? pigen_expr_add_bits(model,bits,(size_t)width,signal.data_type,signal.span) : INVALID_ID(pigen_expr_id);
  free(bits);
  pigen_expr_id target=pigen_expr_add_symbol(model,signal.symbol,signal.data_type,signal.span);
  if(value.index==PIGEN_INVALID_ID || target.index==PIGEN_INVALID_ID ||
   !automatic_default_assignments(resolver,module,process,syntax,target,value,left,right,rank,&children,&count,signal.span)) goto invalid;
  if(explicit_values && signal.entry_value.index!=PIGEN_INVALID_ID)
  {
   pigen_procedural_id assignment=foreach_assignment(resolver,module,process,syntax,target,signal.entry_value,signal.span);
   if(assignment.index==PIGEN_INVALID_ID) goto invalid;
   children=pigen_resize(children,(count+1)*sizeof(*children));children[count++]=assignment;
  }
 }
 pigen_procedural_id result=pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,
  INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),children,count,node->location.source_span);
 free(children);return result;
invalid:
 free(children);fail_location(resolver,node->location,"automatic initialization requires bounded fixed-size variable storage");
 return INVALID_ID(pigen_procedural_id);
}

static pigen_procedural_id resolve_combinational_statement_in_scope(resolver *resolver,
	pigen_module_id module, pigen_process_id process, pigen_syntax_id syntax, size_t depth)
{
	pigen_semantic_model *model=resolver->model;
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,syntax);
	if (!node || depth>128) return INVALID_ID(pigen_procedural_id);
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BREAK || node->kind==PIGEN_SYNTAX_PROCEDURAL_CONTINUE)
	{
		if (!resolver->combinational_loop_depth)
		{
			fail_location(resolver,node->location,"break/continue requires an enclosing procedural loop");
			return INVALID_ID(pigen_procedural_id);
		}
		return pigen_procedural_add(model,process,node->kind==PIGEN_SYNTAX_PROCEDURAL_BREAK ? PIGEN_PROCEDURAL_BREAK : PIGEN_PROCEDURAL_CONTINUE,
			INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),NULL,0,node->location.source_span);
	}
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH)
		return resolve_combinational_foreach(resolver,module,process,syntax,node,depth);
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOR)
	{
		pigen_syntax_id init=node->first_child;
		const pigen_syntax_node *initial=pigen_syntax_get(resolver->syntax,init);
		const pigen_syntax_node *step=initial ? pigen_syntax_get(resolver->syntax,initial->next_sibling) : NULL;
		if (!initial || !step || step->next_sibling.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_procedural_id);
		pigen_procedural_id children[3];
		/* Initializer declarations live in the for scope, not a nested block. */
		children[0]=resolve_combinational_statement_in_scope(resolver,module,process,init,depth+1);
		if (children[0].index==PIGEN_INVALID_ID) return children[0];
		pigen_expr_id control=node->as.procedural_loop.control.index==PIGEN_INVALID_ID ?
			pigen_expr_add_integer(model,1,pigen_data_type_sized_integral(model,1,PIGEN_SIGN_UNSIGNED,PIGEN_DATA_TYPE_STATE_TWO),node->location.source_span) :
			resolve_combinational_control(resolver,node->as.procedural_loop.control,node->location,0);
		children[1]=resolve_combinational_statement_in_scope(resolver,module,process,initial->next_sibling,depth+1);
		children[2]=resolve_combinational_statement(resolver,module,process,step->next_sibling,depth+1);
		if (control.index==PIGEN_INVALID_ID || children[1].index==PIGEN_INVALID_ID || children[2].index==PIGEN_INVALID_ID)
			return INVALID_ID(pigen_procedural_id);
		return pigen_procedural_add(model,process,PIGEN_PROCEDURAL_FOR,control,INVALID_ID(pigen_transfer_id),children,3,node->location.source_span);
	}
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_CASE)
		return resolve_combinational_case(resolver,module,process,node,depth);
	pigen_procedural_kind kind=PIGEN_PROCEDURAL_BLOCK;
	pigen_expr_id condition=INVALID_ID(pigen_expr_id);
	pigen_transfer_id assignment=INVALID_ID(pigen_transfer_id);
    if(node->kind==PIGEN_SYNTAX_IMPORT)
    {
        if(!node->as.package_import.is_structured)
        {
            fail_location(resolver,node->location,"procedural imports require a structured package");
            return INVALID_ID(pigen_procedural_id);
        }
        return add_import(resolver,resolver->expression_scope,node) ?
            pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,INVALID_ID(pigen_expr_id),
                INVALID_ID(pigen_transfer_id),NULL,0,node->location.source_span) : INVALID_ID(pigen_procedural_id);
    }
	if (node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION || node->kind==PIGEN_SYNTAX_TYPEDEF ||
		node->kind==PIGEN_SYNTAX_PARAMETER || node->kind==PIGEN_SYNTAX_OPAQUE)
	{
		size_t first_signal=model->signal_count;
		int ok=node->kind==PIGEN_SYNTAX_OPAQUE ? 1 : resolve_foreach_declaration(resolver,module,process,syntax,node);
		if(ok && node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION && node->as.signal_declaration.is_automatic)
			return automatic_declaration_entries(resolver,module,process,syntax,node,first_signal,1);
		return ok ? pigen_procedural_add(model,process,PIGEN_PROCEDURAL_BLOCK,condition,assignment,NULL,0,node->location.source_span) : INVALID_ID(pigen_procedural_id);
	}
	if (node->kind==PIGEN_SYNTAX_BLOCKING_ASSIGNMENT)
	{
		kind=PIGEN_PROCEDURAL_ASSIGN;
		if (!add_static_assignment(resolver,module,syntax,node,process)) return INVALID_ID(pigen_procedural_id);
		assignment=(pigen_transfer_id){(uint32_t)(model->transfer_count-1)};
	}
	else if (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREVER) kind=PIGEN_PROCEDURAL_FOREVER;
	else if (node->kind==PIGEN_SYNTAX_IF_STATEMENT || node->kind==PIGEN_SYNTAX_PROCEDURAL_WHILE ||
		node->kind==PIGEN_SYNTAX_PROCEDURAL_REPEAT || node->kind==PIGEN_SYNTAX_PROCEDURAL_DO_WHILE)
	{
		kind=node->kind==PIGEN_SYNTAX_IF_STATEMENT ? PIGEN_PROCEDURAL_IF :
			node->kind==PIGEN_SYNTAX_PROCEDURAL_WHILE ? PIGEN_PROCEDURAL_WHILE :
			node->kind==PIGEN_SYNTAX_PROCEDURAL_DO_WHILE ? PIGEN_PROCEDURAL_DO_WHILE : PIGEN_PROCEDURAL_REPEAT;
		condition=resolve_combinational_control(resolver,
			node->kind==PIGEN_SYNTAX_IF_STATEMENT ? node->as.if_statement.condition : node->as.procedural_loop.control,
			node->location,kind==PIGEN_PROCEDURAL_REPEAT);
		if (condition.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_procedural_id);
	}
	else if (node->kind!=PIGEN_SYNTAX_PROCEDURAL_BLOCK) return INVALID_ID(pigen_procedural_id);
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK)
		for (pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;
			child=pigen_syntax_get(resolver->syntax,child)->next_sibling)
			if (!reserve_procedural_scope_names(resolver,child,depth+1)) return INVALID_ID(pigen_procedural_id);
	pigen_procedural_id *children=NULL;size_t count=0;int executable=0;
	for (pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
		if (item->kind==PIGEN_SYNTAX_OPAQUE) { child=item->next_sibling;continue; }
		int declaration=item->kind==PIGEN_SYNTAX_IMPORT || item->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION || item->kind==PIGEN_SYNTAX_TYPEDEF || item->kind==PIGEN_SYNTAX_PARAMETER;
		if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK && declaration && executable)
		{
			free(children);fail_location(resolver,item->location,"procedural declarations must precede statements in their block");
			return INVALID_ID(pigen_procedural_id);
		}
		if (!declaration) executable=1;
		pigen_procedural_id resolved=resolve_combinational_statement(resolver,module,process,child,depth+1);
		if (resolved.index==PIGEN_INVALID_ID) { free(children);return resolved; }
		if (!declaration || (item->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION && item->as.signal_declaration.is_automatic))
		{
			children=pigen_resize(children,(count+1)*sizeof(*children));children[count++]=resolved;
		}
		child=pigen_syntax_get(resolver->syntax,child)->next_sibling;
	}
	pigen_procedural_id result=pigen_procedural_add(model,process,kind,condition,assignment,children,count,node->location.source_span);
	if (result.index!=PIGEN_INVALID_ID && kind==PIGEN_PROCEDURAL_IF)
	{
		model->procedural[result.index].case_qualifier=node->as.if_statement.qualifier;
		model->procedural[result.index].else_if=node->as.if_statement.else_if;
	}
	free(children);return result;
}


static pigen_procedural_id resolve_combinational_statement(resolver *resolver,
	pigen_module_id module,pigen_process_id process,pigen_syntax_id syntax,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,syntax);
	pigen_scope_id saved=resolver->expression_scope;
	if (node && (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOR || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH))
	{
		pigen_scope_id scope=pigen_scope_add(resolver->model,saved,node->location.source_span);
		if (scope.index==PIGEN_INVALID_ID) return INVALID_ID(pigen_procedural_id);
		resolver->model->scopes[scope.index].is_procedural=1;resolver->expression_scope=scope;
		if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK && node->as.procedural_block.has_name)
		{
			pigen_source_span name=token_spelling(resolver,node->as.procedural_block.name);
			pigen_symbol_id symbol=pigen_symbol_lookup_local(resolver->model,saved,name);
			const pigen_symbol *reserved=pigen_symbol_get(resolver->model,symbol);
			pigen_declare_result declared=reserved ?
				(reserved->kind==PIGEN_SYMBOL_PROCEDURAL_SCOPE && reserved->object.scope.index==PIGEN_INVALID_ID ?
				PIGEN_DECLARE_OK : PIGEN_DECLARE_DUPLICATE) :
				pigen_symbol_declare(resolver->model,saved,PIGEN_SYMBOL_PROCEDURAL_SCOPE,
				INVALID_ID(pigen_data_type_id),name,node->location.source_span,&symbol,NULL);
			if (declared!=PIGEN_DECLARE_OK || !pigen_symbol_bind_procedural_scope(resolver->model,symbol,scope))
			{
				resolver->expression_scope=saved;
				fail_location(resolver,node->location,declared==PIGEN_DECLARE_DUPLICATE ?
					"duplicate procedural block name" : "invalid procedural block scope");
				return INVALID_ID(pigen_procedural_id);
			}
		}
	}
	size_t loop_depth=resolver->combinational_loop_depth;
	if (node && (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOR || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH || node->kind==PIGEN_SYNTAX_PROCEDURAL_WHILE ||
		node->kind==PIGEN_SYNTAX_PROCEDURAL_REPEAT || node->kind==PIGEN_SYNTAX_PROCEDURAL_DO_WHILE || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREVER))
		resolver->combinational_loop_depth++;
	pigen_procedural_id result=resolve_combinational_statement_in_scope(resolver,module,process,syntax,depth);
	resolver->combinational_loop_depth=loop_depth;
	resolver->expression_scope=saved;return result;
}

static int add_combinational_process(resolver *resolver,pigen_module_id module,
	pigen_syntax_id syntax,const pigen_syntax_node *node)
{
	pigen_process_id process=pigen_structural_process_add(resolver->model,syntax,module,node->location.source_span);
	if (process.index==PIGEN_INVALID_ID) return 0;
	resolver->model->processes[process.index].is_combinational=1;
	resolver->foreach_specializations=0;
	size_t first_signal=resolver->model->signal_count;
	pigen_procedural_id body=resolve_combinational_statement(resolver,module,process,node->first_child,0);
	if (body.index==PIGEN_INVALID_ID)
		return resolver->error && resolver->error->message ? 0 :
			fail_location(resolver,node->location,"invalid always_comb statement");
	/* Private automatic objects have no observable value outside their block.
	 * Defaults at process entry keep hoisted terminal storage fully driven;
	 * the lexical entry assignments still implement every re-entry. */
	int automatic=0;
	for(size_t i=first_signal;i<resolver->model->signal_count;i++) automatic|=resolver->model->signals[i].is_automatic;
	if(automatic)
	{
	pigen_procedural_id initial=automatic_declaration_entries(resolver,module,process,syntax,node,first_signal,0);
	if(initial.index==PIGEN_INVALID_ID) return 0;
	pigen_procedural_id children[2]={initial,body};
	body=pigen_procedural_add(resolver->model,process,PIGEN_PROCEDURAL_BLOCK,
		INVALID_ID(pigen_expr_id),INVALID_ID(pigen_transfer_id),children,2,node->location.source_span);
	if(body.index==PIGEN_INVALID_ID) return 0;
	}
	resolver->model->processes[process.index].body=body;return 1;
}

static int connect_protocol_aggregate(resolver *resolver, pigen_protocol_endpoint_id view_id,
	pigen_protocol_endpoint_id other_id, pigen_syntax_id syntax, pigen_syntax_location location)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_protocol_endpoint *view = pigen_protocol_endpoint_get(model, view_id);
	const pigen_semantic_protocol_endpoint *other = pigen_protocol_endpoint_get(model, other_id);
	if (!view || !other || view->module.index != other->module.index ||
		view->type.index != other->type.index ||
		view->role == other->role)
		return fail_location(resolver, location, "AXI child connection requires a complementary native endpoint with the complete compatible protocol contract");
	if (!view->binding.is_bound || !other->binding.is_bound ||
		view->binding.domain.index != other->binding.domain.index ||
		view->binding.reset_symbol.index != other->binding.reset_symbol.index ||
		view->binding.reset_active_low != other->binding.reset_active_low)
		return fail_location(resolver, location, "AXI child connection requires compatible clock domains and reset bindings");
	pigen_process_id process = pigen_structural_process_add(model, syntax, view->module, location.source_span);
	for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
	{
		const pigen_semantic_signal *a = pigen_signal_get(model, view->channels[channel]);
		const pigen_semantic_signal *b = pigen_signal_get(model, other->channels[channel]);
		if (!a) continue;
		const pigen_semantic_signal *destination = a->direction == PIGEN_SEMANTIC_OUTPUT ? a : b;
		const pigen_semantic_signal *source = destination == a ? b : a;
		pigen_expr_id target = pigen_expr_add_symbol(model, destination->symbol, destination->data_type, location.source_span);
		pigen_lvalue_id lvalue = pigen_lvalue_resolve(model, target);
		pigen_expr_id value = pigen_expr_add_symbol(model, source->symbol, source->data_type, location.source_span);
		pigen_conversion conversion;
		if (!pigen_data_type_resolve_assignment_conversion(model, source->data_type, destination->data_type, &conversion))
			return fail_location(resolver, location, "AXI child channel payload types are incompatible");
		value = pigen_expr_add_conversion(model, conversion, value, location.source_span);
		pigen_predicate_id guard = pigen_predicate_true(model);
		pigen_transfer_signal_use *uses = NULL;
		size_t use_count = 0;
		if (!analyze_transfer(resolver, lvalue, value, guard, INVALID_ID(pigen_clock_domain_id), location.source_span, &uses, &use_count)) return 0;
		pigen_transfer_id transfer = pigen_transfer_add(model, syntax, view->module, process,
			lvalue, value, guard, INVALID_ID(pigen_clock_domain_id), uses, use_count, location.source_span);
		free(uses);
		if (transfer.index == PIGEN_INVALID_ID) return fail_location(resolver, location, "invalid AXI child channel transfer");
	}
	return 1;
}

typedef struct {
	uint32_t *keys;
	unsigned char *used;
	size_t count, capacity;
} generate_values;

static int resolve_expr_references_symbol(const pigen_semantic_model *model,
	pigen_expr_id expression,pigen_symbol_id symbol);

static int generate_visit(generate_values *values, uint32_t key)
{
	if(values->count*2>=values->capacity)
	{
		size_t old=values->capacity, capacity=old ? old*2 : 16;
		uint32_t *keys=pigen_resize(NULL,capacity*sizeof(*keys));
		unsigned char *used=pigen_resize(NULL,capacity); memset(used,0,capacity);
		for(size_t i=0;i<old;i++) if(values->used[i])
		{
			size_t slot=(values->keys[i]*UINT32_C(2654435761))&(capacity-1);
			while(used[slot]) slot=(slot+1)&(capacity-1);
			keys[slot]=values->keys[i]; used[slot]=1;
		}
		free(values->keys); free(values->used);
		values->keys=keys; values->used=used; values->capacity=capacity;
	}
	size_t slot=(key*UINT32_C(2654435761))&(values->capacity-1);
	while(values->used[slot])
	{
		if(values->keys[slot]==key) return 0;
		slot=(slot+1)&(values->capacity-1);
	}
	values->used[slot]=1; values->keys[slot]=key; values->count++; return 1;
}

static pigen_expr_id generate_integer32(resolver *resolver, pigen_expr_id value,
	pigen_source_span span)
{
	const pigen_semantic_expr *expression=pigen_expr_get(resolver->model,value);
	pigen_data_type_id type=pigen_data_type_sized_logic(resolver->model,32,PIGEN_SIGN_SIGNED);
	pigen_conversion conversion;
	if(!expression || !pigen_data_type_resolve_assignment_conversion(resolver->model,
		expression->data_type,type,&conversion)) return INVALID_ID(pigen_expr_id);
	if(conversion.kind==PIGEN_CONVERSION_IDENTITY) return value;
	return pigen_expr_add_conversion(resolver->model,conversion,value,span);
}

static int generate_constant(resolver *resolver,pigen_scope_id scope,
	pigen_syntax_expr_id syntax,int64_t *value)
{
	pigen_expr_id expression=resolve_constant(resolver,scope,syntax);
	return pigen_const_expr_evaluate_i64(resolver->model,pigen_expr_constant(resolver->model,expression),value);
}

static int collect_generate(resolver *resolver,pigen_module_id module,
	pigen_scope_id scope,pigen_generate_scope_id owner,pigen_syntax_id syntax,size_t depth);

static int collect_generate_children(resolver *resolver,pigen_module_id module,
	pigen_scope_id scope,pigen_generate_scope_id owner,const pigen_syntax_node *node,size_t depth)
{
	for(pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
		if(!item || !collect_generate(resolver,module,scope,owner,child,depth+1)) return 0;
		child=item->next_sibling;
	}
	return 1;
}

/* Source namespaces are checked before branch selection. Alternative blocks
 * of one directly nested conditional share a declaration family; different
 * constructs and ordinary declarations never do. Each block owns a new table. */
typedef struct {
	pigen_token_id name;
	pigen_syntax_id family;
	int block;
} generate_name;

typedef struct { generate_name *names; size_t count; } generate_namespace;

static int validate_generate_namespace(resolver *resolver,const pigen_syntax_node *owner,size_t depth);

static int register_generate_name(resolver *resolver,generate_namespace *space,
	pigen_token_id token,pigen_syntax_id family,int block)
{
	if(token.index==PIGEN_INVALID_ID) return 1;
	for(size_t i=0;i<space->count;i++)
	{
		generate_name other=space->names[i];
		if((block || other.block) && spelling_text_equal(resolver,token_spelling(resolver,token),
			token_spelling(resolver,other.name)) &&
			!(block && other.block && family.index==other.family.index))
			return fail_token(resolver,token,"generate block name conflicts with another declaration in the same source scope");
	}
	space->names=pigen_resize(space->names,(space->count+1)*sizeof(*space->names));
	space->names[space->count++]=(generate_name){token,family,block};return 1;
}

static int validate_procedural_namespace_names(resolver *resolver,generate_namespace *space,
	pigen_syntax_id id,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,id);
	if (!node || depth>128) return node ? fail_location(resolver,node->location,"procedural namespace nesting exceeds limit") : 0;
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_BLOCK)
		return !node->as.procedural_block.has_name || register_generate_name(resolver,space,
			node->as.procedural_block.name,INVALID_ID(pigen_syntax_id),0);
	if (node->kind==PIGEN_SYNTAX_PROCEDURAL_FOR || node->kind==PIGEN_SYNTAX_PROCEDURAL_FOREACH) return 1;
	for (pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;
		child=pigen_syntax_get(resolver->syntax,child)->next_sibling)
		if (!validate_procedural_namespace_names(resolver,space,child,depth+1)) return 0;
	return 1;
}

static int validate_generate_names(resolver *resolver,generate_namespace *space,
	pigen_syntax_id id,pigen_syntax_id family,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,id);
	if(!node || depth>128) return node ? fail_location(resolver,node->location,"generate namespace nesting exceeds limit") : 0;
	if (node->kind==PIGEN_SYNTAX_COMBINATIONAL_PROCESS)
		return validate_procedural_namespace_names(resolver,space,node->first_child,0);
	pigen_token_id name=INVALID_ID(pigen_token_id);
	if(node->kind==PIGEN_SYNTAX_GENERATE_BLOCK)
	{
		if(family.index==PIGEN_INVALID_ID) family=id;
		return register_generate_name(resolver,space,node->as.generate_block.name,family,1) &&
			validate_generate_namespace(resolver,node,depth+1);
	}
	if(node->kind==PIGEN_SYNTAX_GENERATE_FOR)
	{
		const pigen_syntax_node *body=pigen_syntax_get(resolver->syntax,node->first_child);
		if(!body) return 0;
		if(body->kind==PIGEN_SYNTAX_GENERATE_BLOCK)
		{
			if(!register_generate_name(resolver,space,body->as.generate_block.name,id,1)) return 0;
		}
		/* The loop iteration is a scope even when its single body is another
		 * generate construct. Its implicit iterator parameter is a declaration. */
		generate_namespace iteration={0};
		int ok=register_generate_name(resolver,&iteration,node->as.generate_for.iterator,INVALID_ID(pigen_syntax_id),0);
		if(body->kind==PIGEN_SYNTAX_GENERATE_BLOCK)
		{
			for(pigen_syntax_id child=body->first_child;ok && child.index!=PIGEN_INVALID_ID;)
			{
				const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
				ok=validate_generate_names(resolver,&iteration,child,INVALID_ID(pigen_syntax_id),depth+1);
				child=item->next_sibling;
			}
		}
		else if(ok) ok=validate_generate_names(resolver,&iteration,node->first_child,INVALID_ID(pigen_syntax_id),depth+1);
		free(iteration.names);return ok;
	}
	if(node->kind==PIGEN_SYNTAX_GENERATE_IF || node->kind==PIGEN_SYNTAX_GENERATE_CASE)
	{if(family.index==PIGEN_INVALID_ID) family=id;}
	else if(node->kind==PIGEN_SYNTAX_PARAMETER) name=node->as.parameter.name;
	else if(node->kind==PIGEN_SYNTAX_GENVAR) name=node->as.genvar.name;
	else if(node->kind==PIGEN_SYNTAX_TYPEDEF) name=node->as.type_definition.name;
	else if(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATOR) name=node->as.signal_declarator.name;
	else if(node->kind==PIGEN_SYNTAX_INSTANCE) name=node->as.instance.name;
	else if(node->kind!=PIGEN_SYNTAX_GENERATE_REGION && node->kind!=PIGEN_SYNTAX_GENERATE_CASE_ITEM &&
		node->kind!=PIGEN_SYNTAX_SIGNAL_DECLARATION) return 1;
	if(name.index!=PIGEN_INVALID_ID) return register_generate_name(resolver,space,name,INVALID_ID(pigen_syntax_id),0);
	for(pigen_syntax_id child=node->first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
		if((node->kind==PIGEN_SYNTAX_GENERATE_IF || node->kind==PIGEN_SYNTAX_GENERATE_CASE_ITEM) &&
			item->kind==PIGEN_SYNTAX_GENERATE_FOR)
		{
			generate_namespace nested={0};
			int ok=validate_generate_names(resolver,&nested,child,INVALID_ID(pigen_syntax_id),depth+1);
			free(nested.names);if(!ok) return 0;
		}
		else if(!validate_generate_names(resolver,space,child,family,depth+1)) return 0;
		child=item->next_sibling;
	}
	return 1;
}

static int validate_generate_namespace(resolver *resolver,const pigen_syntax_node *owner,size_t depth)
{
	generate_namespace space={0};int ok=1;
	for(pigen_syntax_id child=owner->first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
		if(!validate_generate_names(resolver,&space,child,INVALID_ID(pigen_syntax_id),depth)) {ok=0;break;}
		child=item->next_sibling;
	}
	free(space.names);return ok;
}

static pigen_generate_scope_id generate_scope(resolver *resolver,pigen_module_id module,
	pigen_scope_id scope,pigen_syntax_id syntax,pigen_syntax_id control,
	pigen_symbol_id iterator,int64_t index)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,syntax);
	pigen_source_span name={INVALID_ID(pigen_source_id),0,0};
	if(node->kind==PIGEN_SYNTAX_GENERATE_BLOCK && node->as.generate_block.name.index!=PIGEN_INVALID_ID)
		name=token_spelling(resolver,node->as.generate_block.name);
	pigen_semantic_generate_scope record={.syntax=syntax,.control=control,.module=module,
		.scope=scope,.name=name,.iterator=iterator,.is_iteration=iterator.index!=PIGEN_INVALID_ID,
		.index=index,.span=node->location.source_span};
	const pigen_semantic_module *owner=pigen_module_get(resolver->model,module);
	for(size_t i=0;i<resolver->syntax->generate_name_count;i++)
	{
		const pigen_syntax_generate_name *external=&resolver->syntax->generate_names[i];
		if(external->module.index==owner->syntax.index && external->block.index==node->location.extent.first.index)
		{
			record.external_name=external->external_name;record.external_ordinal=external->ordinal;break;
		}
	}
	if(!record.external_name) return INVALID_ID(pigen_generate_scope_id);
	return pigen_generate_scope_add(resolver->model,&record);
}

static int collect_generate_selected(resolver *resolver,pigen_module_id module,pigen_scope_id scope,
	pigen_generate_scope_id owner,pigen_syntax_id control,pigen_syntax_id selected,size_t depth)
{
	const pigen_syntax_node *body=pigen_syntax_get(resolver->syntax,selected);
	if(!body) return selected.index==PIGEN_INVALID_ID;
	/* Scope suppression applies only to directly nested conditionals. A loop
	 * used as a conditional's unbraced body still has an outer anonymous block. */
	if(body->kind==PIGEN_SYNTAX_GENERATE_FOR)
	{
		pigen_scope_id nested=pigen_scope_add(resolver->model,scope,body->location.source_span);
		pigen_generate_scope_id block=generate_scope(resolver,module,nested,selected,control,INVALID_ID(pigen_symbol_id),0);
		if(block.index==PIGEN_INVALID_ID) return fail_location(resolver,body->location,"invalid anonymous conditional generate scope");
		return collect_generate(resolver,module,nested,block,selected,depth+1);
	}
	return collect_generate(resolver,module,scope,owner,selected,depth+1);
}

static int collect_generate_loop(resolver *resolver,pigen_module_id module,
	pigen_scope_id parent,pigen_syntax_id syntax,const pigen_syntax_node *node,size_t depth)
{
	if(!node->as.generate_for.declares_iterator)
	{
		const pigen_symbol *declared=pigen_symbol_get(resolver->model,pigen_symbol_lookup(resolver->model,parent,
			token_spelling(resolver,node->as.generate_for.iterator)));
		if(!declared || declared->kind!=PIGEN_SYMBOL_GENVAR)
			return fail_location(resolver,node->location,"generate loop iterator requires a visible genvar declaration");
	}
	pigen_semantic_model *model=resolver->model;
	const pigen_syntax_node *body=pigen_syntax_get(resolver->syntax,node->first_child);
	pigen_expr_id initial=resolve_constant(resolver,parent,node->as.generate_for.initial);
	pigen_symbol_id shadow=pigen_symbol_lookup(model,parent,token_spelling(resolver,node->as.generate_for.iterator));
	if(shadow.index!=PIGEN_INVALID_ID && resolve_expr_references_symbol(model,initial,shadow))
		return fail_location(resolver,node->location,"generate initializer cannot reference its loop index");
	pigen_expr_id value=generate_integer32(resolver,initial,node->location.source_span);
	generate_values visited={0}; int ok=0;
	if(!body) return fail_location(resolver,node->location,"generate loop requires a body");
	for(size_t iteration=0;;iteration++)
	{
		pigen_scope_id scope=pigen_scope_add(model,parent,body->location.source_span);
		const pigen_semantic_expr *initial=pigen_expr_get(model,value);
		int64_t index;
		if(!initial || !pigen_const_expr_evaluate_i64(model,pigen_expr_constant(model,value),&index))
		{ fail_location(resolver,node->location,"generate iterator requires a known 32-bit integer"); break; }
		/* Materialize each immutable binding so evaluation does not retain a
		 * recursive dependency on every preceding iteration. */
		pigen_integer_id integer=pigen_integer_intern_u64(model,index<0 ? (uint64_t)(-(index+1))+1 : (uint64_t)index);
		if(index<0) integer=pigen_integer_negate(model,integer);
		value=generate_integer32(resolver,pigen_expr_add_exact_integer(model,integer,node->location.source_span),node->location.source_span);
		pigen_data_type_id type=pigen_expr_get(model,value)->data_type;
		pigen_symbol_id iterator;
		if(pigen_symbol_declare(model,scope,PIGEN_SYMBOL_PARAMETER,type,
			token_spelling(resolver,node->as.generate_for.iterator),node->location.source_span,&iterator,NULL)!=PIGEN_DECLARE_OK ||
			pigen_parameter_add(model,syntax,module,iterator,value,1,node->location.source_span).index==PIGEN_INVALID_ID) break;
		int64_t condition;
		if(!generate_constant(resolver,scope,node->as.generate_for.condition,&condition))
		{ fail_location(resolver,node->location,"generate loop condition must be a known constant"); break; }
		if(!condition) {ok=1;break;}
		if(iteration>=resolver->policy->maximum_generated_bits || !generate_visit(&visited,(uint32_t)index))
		{ fail_location(resolver,node->location,"generate loop does not terminate within the elaboration limit"); break; }
		pigen_generate_scope_id owner=generate_scope(resolver,module,scope,node->first_child,syntax,iterator,index);
		if(owner.index==PIGEN_INVALID_ID) break;
		if(body->kind==PIGEN_SYNTAX_GENERATE_BLOCK)
		{ if(!collect_generate_children(resolver,module,scope,owner,body,depth)) break; }
		else if(!collect_generate(resolver,module,scope,owner,node->first_child,depth+1)) break;
		if(node->as.generate_for.increment || node->as.generate_for.compound_step)
		{
			pigen_expr_id left=pigen_expr_add_symbol(model,iterator,type,node->location.source_span);
			pigen_expr_id right=node->as.generate_for.increment ?
				pigen_expr_add_exact_integer(model,pigen_integer_intern_u64(model,1),node->location.source_span) :
				resolve_constant(resolver,scope,node->as.generate_for.next_value);
			pigen_binary_operator operator=node->as.generate_for.increment<0 ? PIGEN_BINARY_SUBTRACT : PIGEN_BINARY_ADD;
			pigen_binary_resolution operation;
			const pigen_semantic_expr *rhs=pigen_expr_get(model,right);
			if((node->as.generate_for.compound_step && !pigen_binary_operator_from_syntax(node->as.generate_for.compound_operator,&operator)) ||
				!rhs || !pigen_data_type_resolve_binary_operation(model,operator,type,rhs->data_type,&operation)) break;
			if(operation.left_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
				left=pigen_expr_add_conversion(model,operation.left_conversion,left,node->location.source_span);
			if(operation.right_conversion.kind!=PIGEN_CONVERSION_IDENTITY)
				right=pigen_expr_add_conversion(model,operation.right_conversion,right,node->location.source_span);
			value=pigen_expr_add_binary(model,operation.operation,left,right,node->location.source_span);
		}
		else value=resolve_constant(resolver,scope,node->as.generate_for.next_value);
		value=generate_integer32(resolver,value,node->location.source_span);
	}
	free(visited.keys); free(visited.used);
	if(!ok && (!resolver->error || !resolver->error->message))
		fail_location(resolver,node->location,"invalid generate loop scope or step");
	return ok;
}

static int generate_case_width(resolver *resolver,pigen_expr_id value,
	uint64_t *maximum,int *all_signed,pigen_syntax_location location)
{
	const pigen_semantic_expr *expression=pigen_expr_get(resolver->model,value);uint64_t width;
	if(!expression || pigen_expr_constant(resolver->model,value).index==PIGEN_INVALID_ID ||
		!pigen_const_expr_evaluate_u64(resolver->model,pigen_data_type_packed_width(resolver->model,expression->data_type),&width) || !width)
		return fail_location(resolver,location,"case generate requires integral constant selector and labels");
	if(width>resolver->policy->maximum_generated_bits)
		return fail_location(resolver,location,"case generate comparison exceeds generated-size limit");
	if(width>*maximum) *maximum=width;
	*all_signed&=pigen_data_type_signedness(resolver->model,expression->data_type)==PIGEN_SIGN_SIGNED;
	return 1;
}

static int collect_generate_case(resolver *resolver,pigen_module_id module,pigen_scope_id scope,
	pigen_generate_scope_id owner,pigen_syntax_id syntax,const pigen_syntax_node *node,size_t depth)
{
	typedef struct {pigen_const_expr_id value;pigen_syntax_id body;pigen_syntax_location location;} choice;
	choice *choices=NULL;size_t count=0;int ok=0,all_signed=1;uint64_t width=0;
	pigen_syntax_id selected=INVALID_ID(pigen_syntax_id),fallback=INVALID_ID(pigen_syntax_id);
	pigen_expr_id value=resolve_constant(resolver,scope,node->as.generate_case.selector);
	if(!generate_case_width(resolver,value,&width,&all_signed,node->location)) goto done;
	pigen_const_expr_id selector=pigen_expr_constant(resolver->model,value);
	for(pigen_syntax_id at=node->first_child;at.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,at);pigen_syntax_id body=item->first_child;
		while(body.index!=PIGEN_INVALID_ID && pigen_syntax_get(resolver->syntax,body)->kind==PIGEN_SYNTAX_GENERATE_CASE_LABEL)
			body=pigen_syntax_get(resolver->syntax,body)->next_sibling;
		if(body.index==PIGEN_INVALID_ID) {fail_location(resolver,item->location,"case generate item requires a body");goto done;}
		if(item->as.generate_case_item.is_default) fallback=body;
		else for(pigen_syntax_id label=item->first_child;label.index!=body.index;)
		{
			const pigen_syntax_node *written=pigen_syntax_get(resolver->syntax,label);
			pigen_expr_id expression=resolve_constant(resolver,scope,written->as.generate_case_label.value);
			if(!generate_case_width(resolver,expression,&width,&all_signed,written->location)) goto done;
			choices=pigen_resize(choices,(count+1)*sizeof(*choices));
			choices[count++]=(choice){pigen_expr_constant(resolver->model,expression),body,written->location};
			label=written->next_sibling;
		}
		at=item->next_sibling;
	}
	for(size_t i=0;i<count;i++)
	{
		int equal=0;
		if(!pigen_const_expr_case_equal(resolver->model,selector,choices[i].value,(size_t)width,all_signed,&equal))
		{fail_location(resolver,choices[i].location,"case generate constant comparison could not be evaluated");goto done;}
		if(equal && selected.index==PIGEN_INVALID_ID) selected=choices[i].body;
	}
	if(selected.index==PIGEN_INVALID_ID) selected=fallback;
	ok=collect_generate_selected(resolver,module,scope,owner,syntax,selected,depth);
 done:
	free(choices);return ok;
}

static int collect_generate(resolver *resolver,pigen_module_id module,
	pigen_scope_id scope,pigen_generate_scope_id owner,pigen_syntax_id syntax,size_t depth)
{
	const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,syntax);
	if(!node || depth>128) return node ? fail_location(resolver,node->location,"generate elaboration nesting exceeds limit") : 0;
	pigen_semantic_model *model=resolver->model;
	if(model->generate_item_count>=resolver->policy->maximum_generated_bits || model->generate_scope_count>=resolver->policy->maximum_generated_bits)
		return fail_location(resolver,node->location,"generate elaboration exceeds generated-size limit");
	if(node->kind==PIGEN_SYNTAX_GENERATE_REGION)
		return collect_generate_children(resolver,module,scope,owner,node,depth);
	if(node->kind==PIGEN_SYNTAX_GENERATE_BLOCK)
	{
		pigen_scope_id nested=pigen_scope_add(model,scope,node->location.source_span);
		pigen_generate_scope_id block=generate_scope(resolver,module,nested,syntax,syntax,INVALID_ID(pigen_symbol_id),0);
		if(block.index==PIGEN_INVALID_ID) return fail_location(resolver,node->location,"duplicate or invalid generated scope");
		return collect_generate_children(resolver,module,nested,block,node,depth);
	}
	if(node->kind==PIGEN_SYNTAX_GENERATE_IF)
	{
		int64_t condition;
		if(!generate_constant(resolver,scope,node->as.generate_if.condition,&condition))
			return fail_location(resolver,node->location,"generate condition must be a known constant");
		pigen_syntax_id selected=node->first_child;
		if(!condition) selected=node->as.generate_if.has_else ? pigen_syntax_get(resolver->syntax,selected)->next_sibling : INVALID_ID(pigen_syntax_id);
		return collect_generate_selected(resolver,module,scope,owner,syntax,selected,depth);
	}
	if(node->kind==PIGEN_SYNTAX_GENERATE_CASE)
		return collect_generate_case(resolver,module,scope,owner,syntax,node,depth);
	if(node->kind==PIGEN_SYNTAX_GENERATE_FOR)
		return collect_generate_loop(resolver,module,scope,syntax,node,depth);
	if(node->kind==PIGEN_SYNTAX_OPAQUE) return 1;
	pigen_scope_id saved=resolver->expression_scope; resolver->expression_scope=scope;
	int ok=1;
	if(node->kind==PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT || node->kind==PIGEN_SYNTAX_INSTANCE ||
  node->kind==PIGEN_SYNTAX_COMBINATIONAL_PROCESS || node->kind==PIGEN_SYNTAX_CLOCKED_PROCESS) bind_import_uses(resolver,scope,node);
	if(node->kind==PIGEN_SYNTAX_IMPORT) ok=add_import(resolver,scope,node);
	else if(node->kind==PIGEN_SYNTAX_GENVAR) ok=add_genvar(resolver,node);
	else if(node->kind==PIGEN_SYNTAX_PARAMETER) ok=add_parameter(resolver,module,syntax,node);
	else if(node->kind==PIGEN_SYNTAX_TYPEDEF) ok=add_typedef(resolver,scope,node);
	else if(node->kind==PIGEN_SYNTAX_SIGNAL_DECLARATION) ok=add_signal_declaration_in_scope(resolver,module,scope,node);
	else if(node->kind!=PIGEN_SYNTAX_COMBINATIONAL_PROCESS && node->kind!=PIGEN_SYNTAX_CLOCKED_PROCESS && node->kind!=PIGEN_SYNTAX_FSM && node->kind!=PIGEN_SYNTAX_FABRIC &&
		node->kind!=PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT && node->kind!=PIGEN_SYNTAX_INSTANCE)
		ok=fail_location(resolver,node->location,"generated module item requires scoped lowering integration");
	resolver->expression_scope=saved;
	pigen_semantic_generate_item item={syntax,module,scope,owner};
	return ok && pigen_generate_item_add(model,&item);
}

static int resolve_module_body(resolver *resolver, pigen_module_id module_id,
	const pigen_syntax_node *syntax_node);
static int bind_retained_module_uses(resolver *resolver,pigen_module_id module,
 pigen_scope_id scope,const pigen_syntax_node *node);

static int add_module(resolver *resolver, const pigen_syntax_node *syntax_node,
	pigen_syntax_id syntax_id, int declarations_only)
{
	pigen_semantic_model *model = resolver->model;
	pigen_symbol_id symbol;
	pigen_declare_result declared;
	pigen_scope_id scope;
	pigen_module_id module_id;
	pigen_syntax_id child;
	pigen_source_span declaration_span=resolver->specializing ?
		resolver->specialization_name : syntax_node->location.source_span;

	if (!declarations_only)
	{
		symbol = pigen_symbol_lookup(model, model->compilation_scope,
			token_spelling(resolver, syntax_node->as.module.name));
		const pigen_symbol *known = pigen_symbol_get(model, symbol);
		if (!known || known->kind != PIGEN_SYMBOL_MODULE) return 0;
		module_id = known->object.module;
		return resolve_module_body(resolver,module_id,syntax_node);
	}

	declared = pigen_symbol_declare(model, model->compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_data_type_id),
		resolver->specializing ? resolver->specialization_name :
			token_spelling(resolver, syntax_node->as.module.name),
		declaration_span, &symbol, NULL);
	if (declared == PIGEN_DECLARE_DUPLICATE)
		return fail_token(resolver, syntax_node->as.module.name,
			"duplicate module name");
	if (declared != PIGEN_DECLARE_OK)
		return fail_location(resolver, syntax_node->location,
			"invalid module declaration");
	scope = pigen_scope_add(model, model->compilation_scope,
		declaration_span);
	if (scope.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"cannot create module scope");
	resolver->expression_scope = scope;
	module_id = pigen_module_add(model, syntax_id, symbol, scope,
		declaration_span);
	if (module_id.index == PIGEN_INVALID_ID)
		return fail_location(resolver, syntax_node->location,
			"invalid module semantic object");
	model->modules[module_id.index].is_external = syntax_node->as.module.is_external;
	for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
		if(node->kind==PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT || node->kind==PIGEN_SYNTAX_INSTANCE ||
  node->kind==PIGEN_SYNTAX_COMBINATIONAL_PROCESS || node->kind==PIGEN_SYNTAX_CLOCKED_PROCESS) bind_import_uses(resolver,scope,node);
		if(node->kind==PIGEN_SYNTAX_IMPORT && !add_import(resolver,scope,node)) return 0;
		if(node->kind==PIGEN_SYNTAX_GENVAR && !add_genvar(resolver,node)) return 0;
		if (node->kind == PIGEN_SYNTAX_PARAMETER &&
			!add_parameter(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_TYPEDEF &&
			!add_typedef(resolver, scope, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			!add_signal_declaration(resolver, module_id, node)) return 0;
		child = node->next_sibling;
	}
	if(!validate_generate_namespace(resolver,syntax_node,0)) return 0;
	for (child=syntax_node->first_child;child.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,child);
		if(node->kind==PIGEN_SYNTAX_GENERATE_REGION && !collect_generate(resolver,module_id,scope,
			INVALID_ID(pigen_generate_scope_id),child,0)) return 0;
		child=node->next_sibling;
	}
	return bind_module_protocols(resolver, module_id);
}

static int resolve_module_body(resolver *resolver, pigen_module_id module_id,
	const pigen_syntax_node *syntax_node)
{
	pigen_semantic_model *model=resolver->model;
	pigen_scope_id scope=pigen_module_get(model,module_id)->scope;
	pigen_syntax_id child;
	resolver->expression_scope = scope;
	/* Establish sibling identities, then direct input-wire aliases, then
	 * output destinations and finally general input expressions. */
	for (int step = 0; step < 4; step++)
	{
		resolver->expression_scope=scope;
		for (child = syntax_node->first_child; child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(resolver->syntax, child);
			if (node->kind == PIGEN_SYNTAX_INSTANCE &&
				!resolve_instance(resolver, module_id, child, node,
					step == 0 ? 0 : step == 1 ? 3 : step == 2 ? 1 : 2)) return 0;
			child = node->next_sibling;
		}
		for(size_t i=0;i<model->generate_item_count;i++)
		{
			pigen_semantic_generate_item item=model->generate_items[i];
			if(item.module.index!=module_id.index) continue;
			const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,item.syntax);
			resolver->expression_scope=item.scope;
			if(node->kind==PIGEN_SYNTAX_INSTANCE && !resolve_instance(resolver,module_id,item.syntax,node,
				step==0 ? 0 : step==1 ? 3 : step==2 ? 1 : 2)) return 0;
		}
	}
	resolver->expression_scope=scope;
	/* Native child timing controls are interpreted through resolved scalar
	 * connections, never through the spelling of the child's control pins. */
	for (size_t e = 0; e < model->protocol_endpoint_count; e++)
	{
		pigen_semantic_protocol_endpoint view = model->protocol_endpoints[e];
		if (view.module.index != module_id.index || !view.is_instance_view) continue;
		const pigen_semantic_protocol_endpoint *port = pigen_protocol_endpoint_get(model, view.child_endpoint);
		const pigen_semantic_clock_domain *clock = port && port->binding.is_bound ?
			pigen_clock_domain_get(model, port->binding.domain) : NULL;
		if (!clock) return fail(resolver, view.span, "child AXI endpoint has no clock/reset binding");
		pigen_symbol_id controls[2] = {clock->clock_symbol, port->binding.reset_symbol};
		pigen_symbol_id bound[2] = {INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_symbol_id)};
		int active_low = port->binding.reset_active_low;
		const pigen_semantic_instance *instance = pigen_instance_get(model, view.instance);
		for (size_t k = 0; k < 2; k++)
			for (size_t c = 0; c < instance->connection_count; c++)
			{
				const pigen_semantic_instance_connection *connection = &model->instance_connections[instance->first_connection+c];
				if (connection->port_symbol.index != controls[k].index) continue;
				const pigen_semantic_expr *value = pigen_expr_get(model, resolve_input_aliases(model, connection->value));
				while (value && (value->kind == PIGEN_EXPR_CONVERSION || value->kind == PIGEN_EXPR_GROUP))
					value = pigen_expr_get(model, value->kind == PIGEN_EXPR_CONVERSION ? value->as.conversion.operand : value->as.group.operand);
				if (k == 1 && value && value->kind == PIGEN_EXPR_UNARY && value->as.unary.operation.operator == PIGEN_UNARY_LOGICAL_NOT)
				{
					active_low = !active_low;
					value = pigen_expr_get(model, value->as.unary.operand);
					while (value && (value->kind == PIGEN_EXPR_CONVERSION || value->kind == PIGEN_EXPR_GROUP))
						value = pigen_expr_get(model, value->kind == PIGEN_EXPR_CONVERSION ? value->as.conversion.operand : value->as.group.operand);
				}
				if (value && value->kind == PIGEN_EXPR_SYMBOL) bound[k] = value->as.symbol;
			}
		pigen_clock_domain_id domain = pigen_clock_domain_intern(model, bound[0], PIGEN_SEMANTIC_POSEDGE);
		if (!pigen_protocol_endpoint_bind(model, (pigen_protocol_endpoint_id){(uint32_t)e}, domain, bound[1], active_low))
			return fail(resolver, view.span, "child AXI clock/reset connections require scalar static parent inputs");
	}
	for(size_t instance_index=0;instance_index<model->instance_count;instance_index++)
	{
		const pigen_semantic_instance *instance=&model->instances[instance_index];
		if(instance->module.index!=module_id.index) continue;
		const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,instance->syntax);
		pigen_scope_id instance_scope=model->symbols[instance->symbol.index].scope;
		const pigen_semantic_module *child_module = pigen_module_get(model, instance->child_module);
		size_t positional = 0;
		for (pigen_syntax_id c = node->first_child; c.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *connection = pigen_syntax_get(resolver->syntax, c);
			if (connection->kind == PIGEN_SYNTAX_CONNECTION)
			{
				pigen_symbol_id symbol = INVALID_ID(pigen_symbol_id);
				pigen_signal_id unused;
				if (connection->as.connection.port.index != PIGEN_INVALID_ID)
					symbol = pigen_symbol_lookup(model, child_module->scope, token_spelling(resolver, connection->as.connection.port));
				else child_port_at(model, instance->child_module, positional, &symbol, &unused);
				positional++;
				pigen_protocol_array_id array_id = pigen_symbol_protocol_array(model, symbol);
				if (array_id.index != PIGEN_INVALID_ID)
				{
					pigen_protocol_array_view other={0};
					const pigen_semantic_protocol_array *array=pigen_protocol_array_get(model,array_id);
					int selected=pigen_resolve_protocol_array_binding_to_array(resolver->syntax,model,instance_scope,
						connection->as.connection.value,array_id,resolver->policy,&other);
					int compatible=selected==1 && array->dimension_count==other.dimension_count && array->element_count==other.element_count;
					for(size_t d=0;compatible && d<array->dimension_count;d++)
						if(model->protocol_array_dimensions[array->first_dimension+d].count!=other.dimensions[d].count) compatible=0;
					if(!compatible)
					{
						pigen_free_protocol_array_view(&other);
						return fail_location(resolver,connection->location,"AXI child array connection requires a constant collection selection, concatenation or assignment pattern with matching rank and dimension cardinalities");
					}
					pigen_instance_id owner={(uint32_t)(instance-model->instances)};
					for(size_t lane=0;lane<array->element_count;lane++)
					{
						pigen_protocol_endpoint_id target=pigen_instance_protocol_endpoint(model,owner,model->protocol_array_elements[array->first_element+lane]);
						if(!connect_protocol_aggregate(resolver,target,other.elements[lane],c,connection->location))
						{pigen_free_protocol_array_view(&other);return 0;}
					}
					pigen_free_protocol_array_view(&other);
				}
				pigen_protocol_endpoint_id port = pigen_symbol_protocol_endpoint(model, symbol);
				if (port.index != PIGEN_INVALID_ID && !connect_protocol_aggregate(resolver,
					pigen_instance_protocol_endpoint(model, (pigen_instance_id){(uint32_t)(instance-model->instances)}, port),
					pigen_resolve_protocol_reference(resolver->syntax, model, instance_scope, connection->as.connection.value, resolver->policy),
					c, connection->location)) return 0;
			}
			c = connection->next_sibling;
		}
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
			if (connection->endpoint_view.index != PIGEN_INVALID_ID ||
				connection->direction != PIGEN_SEMANTIC_INPUT || port.direction != PIGEN_SEMANTIC_INPUT ||
				pigen_transfer_type_descriptor_get(port.transfer_type)->is_static || !value || value->kind == PIGEN_EXPR_SYMBOL) continue;
			/* Control sibling connections share port_signal: only adapt payload. */
			const pigen_symbol *port_symbol = pigen_symbol_get(model, port.symbol);
			size_t n = 0, m = 0;
			const char *name = pigen_source_span_text(model->sources, connection->port_name, &n);
			const char *base = pigen_source_span_text(model->sources, port_symbol->name, &m);
			if (!name || !base || n != m || memcmp(name, base, n)) continue;
			connection->value = resolve_input_aliases(model, connection->value);
			if (connection->value.index == PIGEN_INVALID_ID)
				return fail(resolver, connection->span, "invalid endpoint alias expression");
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
		if(node->kind==PIGEN_SYNTAX_RETAINED && !bind_retained_module_uses(resolver,module_id,scope,node)) return 0;
		if (node->kind == PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT &&
			!add_continuous_assignment(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_COMBINATIONAL_PROCESS &&
			!add_combinational_process(resolver,module_id,child,node)) return 0;
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS &&
			!add_clocked_process(resolver, module_id, child, node)) return 0;
		if (node->kind == PIGEN_SYNTAX_FSM &&
			!resolve_fsm(resolver, module_id, child, node)) return 0;
		child = node->next_sibling;
	}
	for(size_t i=0;i<model->generate_item_count;i++)
	{
		pigen_semantic_generate_item item=model->generate_items[i];
		if(item.module.index!=module_id.index) continue;
		const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,item.syntax);
		resolver->expression_scope=item.scope;
		if(node->kind==PIGEN_SYNTAX_RETAINED && !bind_retained_module_uses(resolver,module_id,item.scope,node)) return 0;
		if(node->kind==PIGEN_SYNTAX_COMBINATIONAL_PROCESS && !add_combinational_process(resolver,module_id,item.syntax,node)) return 0;
		if(node->kind==PIGEN_SYNTAX_CLOCKED_PROCESS && !add_clocked_process(resolver,module_id,item.syntax,node)) return 0;
		if(node->kind==PIGEN_SYNTAX_FSM && !resolve_fsm(resolver,module_id,item.syntax,node)) return 0;
		if(node->kind==PIGEN_SYNTAX_CONTINUOUS_ASSIGNMENT && !add_continuous_assignment(resolver,module_id,item.syntax,node)) return 0;
	}
	resolver->expression_scope=scope;
	return 1;
}

static int parameter_names_equal(resolver *resolver, pigen_token_id left, pigen_token_id right)
{
	size_t a=0,b=0;
	const char *x=pigen_source_span_text(resolver->model->sources,token_spelling(resolver,left),&a);
	const char *y=pigen_source_span_text(resolver->model->sources,token_spelling(resolver,right),&b);
	return x && y && a==b && !memcmp(x,y,a);
}

static int specialized_parameters_equal(pigen_semantic_model *model,
	pigen_module_id left, pigen_module_id right)
{
	for(size_t i=0;i<model->parameter_count;i++)
	{
		const pigen_semantic_parameter *a=&model->parameters[i];
		if(a->module.index!=left.index || a->is_local ||
			model->symbols[a->symbol.index].scope.index!=model->modules[left.index].scope.index) continue;
		size_t j=0;
		for(;j<model->parameter_count;j++)
			if(model->parameters[j].module.index==right.index && model->parameters[j].syntax.index==a->syntax.index) break;
		if(j==model->parameter_count) return 0;
		pigen_const_expr_id av=pigen_expr_constant(model,a->value),bv=pigen_expr_constant(model,model->parameters[j].value);
		if(av.index==bv.index) continue;
		int64_t ai,bi; uint64_t au,bu;
		if(pigen_const_expr_evaluate_i64(model,av,&ai) && pigen_const_expr_evaluate_i64(model,bv,&bi) && ai==bi) continue;
		if(pigen_const_expr_evaluate_u64(model,av,&au) && pigen_const_expr_evaluate_u64(model,bv,&bu) && au==bu) continue;
		return 0;
	}
	return 1;
}

static pigen_module_id specialize_generated_child(resolver *resolver,
	pigen_module_id parent, pigen_module_id target, const pigen_syntax_node *instance)
{
	pigen_semantic_model *model=resolver->model;
	pigen_syntax_id template=model->modules[target.index].syntax;
	const pigen_syntax_node *definition=pigen_syntax_get(resolver->syntax,template);
	pigen_syntax_id *parameters=NULL; size_t count=0,position=0;
	scoped_parameter_override *overrides=NULL;size_t used=0;
	pigen_module_id result=INVALID_ID(pigen_module_id);
	int named=0,ordered=0;
	unsigned char *seen=NULL;
	for(pigen_syntax_id p=definition->first_child;p.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,p);
		if(node->kind==PIGEN_SYNTAX_PARAMETER && !node->as.parameter.is_local)
		{
			parameters=pigen_resize(parameters,(count+1)*sizeof(*parameters));
			parameters[count++]=p;
		}
		p=node->next_sibling;
	}
	seen=calloc(count ? count : 1,1);
	if(!seen) pigen_fail("memory allocation failed");
	for(pigen_syntax_id p=instance->first_child;p.index!=PIGEN_INVALID_ID;)
	{
		const pigen_syntax_node *binding=pigen_syntax_get(resolver->syntax,p);
		p=binding->next_sibling;
		if(binding->kind!=PIGEN_SYNTAX_PARAMETER_CONNECTION) continue;
		size_t selected=count;
		if(binding->as.connection.port.index!=PIGEN_INVALID_ID)
		{
			named=1;
			for(size_t i=0;i<count;i++)
				if(parameter_names_equal(resolver,binding->as.connection.port,
					pigen_syntax_get(resolver->syntax,parameters[i])->as.parameter.name)) {selected=i;break;}
		}
		else {ordered=1;selected=position++;}
		if(named && ordered) {fail_location(resolver,binding->location,"child parameters cannot mix named and positional overrides");goto done;}
		if(selected>=count) {fail_location(resolver,binding->location,"child parameter override does not name an overridable parameter");goto done;}
		if(seen[selected]) {fail_location(resolver,binding->location,"duplicate child parameter override");goto done;}
		seen[selected]=1;
		if(binding->as.connection.value.index==PIGEN_INVALID_ID) continue;
		pigen_expr_id value=resolve_constant(resolver,resolver->expression_scope,binding->as.connection.value);
		if(value.index==PIGEN_INVALID_ID) goto done;
		overrides=pigen_resize(overrides,(used+1)*sizeof(*overrides));
		overrides[used++]=(scoped_parameter_override){parameters[selected],value,binding->as.connection.value,resolver->expression_scope};
	}
	{
		size_t depth=0;
		for(pigen_module_id p=parent;p.index!=PIGEN_INVALID_ID;p=model->modules[p.index].specialization_parent)
			if(++depth>256) {fail_location(resolver,instance->location,"child specialization exceeded hierarchy depth limit");goto done;}
		char name[96];pigen_source_span spelling;
		for(size_t n=model->module_count;;n++)
		{
			int length=snprintf(name,sizeof name,"__pigen_scope_spec_%zu",n);
			pigen_source_id source=pigen_source_add((pigen_source_manager *)model->sources,
				"<pigen-specialization>",name,(size_t)length);
			spelling=(pigen_source_span){source,0,(size_t)length};
			if(pigen_symbol_lookup(model,model->compilation_scope,spelling).index==PIGEN_INVALID_ID) break;
		}
		pigen_scope_id saved_scope=resolver->expression_scope;
		const scoped_parameter_override *saved_overrides=resolver->parameter_overrides;
		size_t saved_count=resolver->parameter_override_count;
		int saved_specializing=resolver->specializing;
		pigen_source_span saved_name=resolver->specialization_name;
		resolver->specializing=1;resolver->specialization_name=spelling;
		resolver->parameter_overrides=overrides;resolver->parameter_override_count=used;
		result=(pigen_module_id){(uint32_t)model->module_count};
		int ok=add_module(resolver,definition,template,1);
		resolver->expression_scope=saved_scope;resolver->specializing=saved_specializing;
		resolver->specialization_name=saved_name;resolver->parameter_overrides=saved_overrides;
		resolver->parameter_override_count=saved_count;
		if(!ok) {result=INVALID_ID(pigen_module_id);goto done;}
		model->modules[result.index].specialization_of=target;
		model->modules[result.index].specialization_parent=parent;
		for(pigen_module_id p=parent;p.index!=PIGEN_INVALID_ID;p=model->modules[p.index].specialization_parent)
			if(model->modules[p.index].syntax.index==template.index && specialized_parameters_equal(model,result,p))
			{fail_location(resolver,instance->location,"recursive module instantiation cannot be elaborated");result=INVALID_ID(pigen_module_id);break;}
	}
done:
	free(parameters);free(overrides);free(seen);return result;
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
	case PIGEN_EXPR_EVALUATED_VALUE:
		return resolve_expr_references_symbol(model,expr->as.evaluation.value,symbol) ||
			resolve_expr_references_symbol(model,expr->as.evaluation.evaluated,symbol);
	case PIGEN_EXPR_ARRAY_PATTERN:
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

/* Only prove disjointness in a shared selection coordinate system. Unknown
 * indices remain potentially overlapping; buffered tokens never use this escape. */
static const pigen_semantic_expr *ownership_expression(
	const pigen_semantic_model *model, pigen_expr_id value)
{
	const pigen_semantic_expr *expr = pigen_expr_get(model, value);
	while (expr && (expr->kind == PIGEN_EXPR_GROUP ||
		expr->kind == PIGEN_EXPR_CONVERSION))
		expr = pigen_expr_get(model, expr->kind == PIGEN_EXPR_GROUP ?
			expr->as.group.operand : expr->as.conversion.operand);
	return expr;
}

static int ownership_selection(pigen_semantic_model *model,
	const pigen_semantic_expr *expr, pigen_expr_id *base,
	int64_t *low, int64_t *high)
{
	pigen_expr_id left, right;
	int64_t a, b;
	if (!expr) return 0;
	if (expr->kind == PIGEN_EXPR_INDEX)
	{
		*base = expr->as.index.base;
		if (!pigen_const_expr_evaluate_i64(model,
			pigen_expr_constant(model, expr->as.index.index), &a)) return 0;
		*low = *high = a;
		return 1;
	}
	if (expr->kind != PIGEN_EXPR_SELECT) return 0;
	*base = expr->as.select.base;
	left = expr->as.select.left;
	right = expr->as.select.right;
	if (!pigen_const_expr_evaluate_i64(model, pigen_expr_constant(model, left), &a) ||
		!pigen_const_expr_evaluate_i64(model, pigen_expr_constant(model, right), &b))
		return 0;
	if (expr->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE)
	{
		*low = a < b ? a : b;
		*high = a > b ? a : b;
	}
	else if (expr->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP)
	{
		if (b <= 0 || a > INT64_MAX - (b - 1)) return 0;
		*low = a; *high = a + (b - 1);
	}
	else
	{
		if (b <= 0 || a < INT64_MIN + (b - 1)) return 0;
		*low = a - (b - 1); *high = a;
	}
	return 1;
}

/* Indexed selections follow the base declaration's packed direction. A
 * normalized interval alone cannot establish that payload bit order matches. */
static int ownership_ordered_selection(pigen_semantic_model *model,
	const pigen_semantic_expr *expr, pigen_expr_id *base,
	int64_t *left, int64_t *right)
{
	int64_t low, high;
	if (!expr || expr->kind != PIGEN_EXPR_SELECT ||
		!ownership_selection(model, expr, base, &low, &high)) return 0;
	if (expr->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE)
		return pigen_const_expr_evaluate_i64(model,
			pigen_expr_constant(model, expr->as.select.left), left) &&
			pigen_const_expr_evaluate_i64(model,
				pigen_expr_constant(model, expr->as.select.right), right);
	const pigen_semantic_expr *source = pigen_expr_get(model, *base);
	if (!source) return 0;
	const pigen_packed_dimension *dimensions = pigen_data_type_dimensions(model, source->data_type);
	int descending = 1;
	if (pigen_data_type_dimension_count(model, source->data_type))
	{
		int64_t declared_left, declared_right;
		if (!dimensions || !pigen_const_expr_evaluate_i64(model, dimensions[0].left, &declared_left) ||
			!pigen_const_expr_evaluate_i64(model, dimensions[0].right, &declared_right)) return 0;
		descending = declared_left >= declared_right;
	}
	*left = descending ? high : low;
	*right = descending ? low : high;
	return 1;
}

static int ownership_same_projection(pigen_semantic_model *model,
	pigen_expr_id left, pigen_expr_id right)
{
	const pigen_semantic_expr *a = ownership_expression(model, left);
	const pigen_semantic_expr *b = ownership_expression(model, right);
	pigen_expr_id abase, bbase;
	int64_t alow, ahigh, blow, bhigh;
	if (!a || !b) return 0;
	if (a == b) return 1;
	if (a->kind == PIGEN_EXPR_SYMBOL && b->kind == PIGEN_EXPR_SYMBOL)
		return a->as.symbol.index == b->as.symbol.index;
	if (a->kind == PIGEN_EXPR_SELECT && b->kind == PIGEN_EXPR_SELECT)
	{
		int64_t al, ar, bl, br;
		return ownership_ordered_selection(model, a, &abase, &al, &ar) &&
			ownership_ordered_selection(model, b, &bbase, &bl, &br) &&
			al == bl && ar == br && ownership_same_projection(model, abase, bbase);
	}
	return a->kind == PIGEN_EXPR_INDEX && b->kind == PIGEN_EXPR_INDEX &&
		ownership_selection(model, a, &abase, &alow, &ahigh) &&
		ownership_selection(model, b, &bbase, &blow, &bhigh) &&
		alow == blow && ownership_same_projection(model, abase, bbase);
}

/* One packed dimension supplies a bit-coordinate system. Unpacked shapes
 * and wider packed elements require structural projection, not flat offsets. */
static int packed_coordinate_range(pigen_semantic_model *model,
	pigen_data_type_id type, pigen_shape_id shape, int64_t *left, int64_t *right)
{
	const pigen_semantic_shape *dimensions = pigen_shape_get(model, shape);
	if (!dimensions || dimensions->dimension_count || pigen_data_type_dimension_count(model, type) > 1) return 0;
	if (pigen_data_type_dimension_count(model, type))
	{
		const pigen_packed_dimension *packed = pigen_data_type_dimensions(model, type);
		return packed && pigen_const_expr_evaluate_i64(model, packed[0].left, left) &&
			pigen_const_expr_evaluate_i64(model, packed[0].right, right);
	}
	uint64_t width;
	if (!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, type), &width) ||
		!width || width > INT64_MAX) return 0;
	*left = (int64_t)width-1; *right = 0;
	return 1;
}

static pigen_expr_id coordinate_literal(pigen_semantic_model *model, int64_t value, pigen_source_span span)
{
	if (value >= 0) return pigen_expr_add_integer(model, (uint64_t)value,
		pigen_data_type_unsized_integer(model), span);
	pigen_integer_id magnitude = pigen_integer_intern_u64(model, (uint64_t)(-(value+1))+1);
	return pigen_expr_add_exact_integer(model, pigen_integer_negate(model, magnitude), span);
}

static pigen_expr_id partial_endpoint_alias(pigen_semantic_model *model,
	pigen_expr_id value, const pigen_semantic_instance_connection *upstream,
	pigen_data_type_id target_type, pigen_source_span span)
{
	if (upstream->direction != PIGEN_SEMANTIC_OUTPUT || upstream->endpoint_view.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	const pigen_semantic_expr *consumer = ownership_expression(model, value);
	const pigen_semantic_expr *producer = ownership_expression(model, upstream->value);
	if (!consumer || !producer || (consumer->kind != PIGEN_EXPR_SELECT && consumer->kind != PIGEN_EXPR_INDEX))
		return INVALID_ID(pigen_expr_id);
	pigen_expr_id base, source_base;
	int64_t left, right, source_left, source_right, ignored_left, ignored_right;
	int indexed = consumer->kind == PIGEN_EXPR_INDEX;
	if (indexed)
	{
		base = consumer->as.index.base;
		if (!pigen_const_expr_evaluate_i64(model, pigen_expr_constant(model, consumer->as.index.index), &left))
			return INVALID_ID(pigen_expr_id);
		right = left;
	}
	else if (!ownership_ordered_selection(model, consumer, &base, &left, &right)) return INVALID_ID(pigen_expr_id);
	if (producer->kind == PIGEN_EXPR_SELECT)
	{
		if (!ownership_ordered_selection(model, producer, &source_base, &source_left, &source_right))
			return INVALID_ID(pigen_expr_id);
	}
	else
	{
		source_base = (pigen_expr_id){(uint32_t)(producer-model->expressions)};
		if (!packed_coordinate_range(model, producer->data_type, producer->shape, &source_left, &source_right))
			return INVALID_ID(pigen_expr_id);
	}
	const pigen_semantic_expr *source_expression = pigen_expr_get(model, source_base);
	if (!ownership_same_projection(model, base, source_base) || !source_expression ||
		!packed_coordinate_range(model, source_expression->data_type, source_expression->shape, &ignored_left, &ignored_right))
		return INVALID_ID(pigen_expr_id);
	int64_t low = source_left < source_right ? source_left : source_right;
	int64_t high = source_left > source_right ? source_left : source_right;
	if (left < low || left > high || right < low || right > high ||
		(!indexed && left != right && (left > right) != (source_left > source_right))) return INVALID_ID(pigen_expr_id);
	const pigen_semantic_signal *endpoint = pigen_signal_get(model, upstream->endpoint_view);
	int64_t endpoint_left, endpoint_right;
	uint64_t width;
	if (!endpoint || !packed_coordinate_range(model, endpoint->data_type, endpoint->shape, &endpoint_left, &endpoint_right) ||
		!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, endpoint->data_type), &width) ||
		(uint64_t)high-(uint64_t)low != width-1 || width > INT64_MAX) return INVALID_ID(pigen_expr_id);
	uint64_t left_offset = source_left >= source_right ? (uint64_t)left-(uint64_t)source_right : (uint64_t)source_right-(uint64_t)left;
	uint64_t right_offset = source_left >= source_right ? (uint64_t)right-(uint64_t)source_right : (uint64_t)source_right-(uint64_t)right;
	int64_t mapped_left = endpoint_left >= endpoint_right ? endpoint_right+(int64_t)left_offset : endpoint_right-(int64_t)left_offset;
	int64_t mapped_right = endpoint_left >= endpoint_right ? endpoint_right+(int64_t)right_offset : endpoint_right-(int64_t)right_offset;
	pigen_expr_id source = pigen_expr_add_symbol(model, endpoint->symbol, endpoint->data_type, span);
	pigen_expr_id l = coordinate_literal(model, mapped_left, span);
	pigen_expr_id projected = indexed ? pigen_expr_add_index(model, source, l, span) :
		pigen_expr_add_select(model, source, l, coordinate_literal(model, mapped_right, span), PIGEN_SEMANTIC_SELECT_RANGE, span);
	const pigen_semantic_expr *result = pigen_expr_get(model, projected);
	pigen_conversion conversion;
	if (!result || !pigen_data_type_resolve_assignment_conversion(model, result->data_type, target_type, &conversion))
		return INVALID_ID(pigen_expr_id);
	return conversion.kind == PIGEN_CONVERSION_IDENTITY ? projected :
		pigen_expr_add_conversion(model, conversion, projected, span);
}

/* Runtime coordinates use a widened signed domain so subtraction neither
 * truncates a small index nor wraps an out-of-range value into the payload. */
static pigen_expr_id dynamic_endpoint_alias(pigen_semantic_model *model,
	pigen_expr_id value, const pigen_semantic_instance_connection *upstream,
	pigen_data_type_id target_type, pigen_source_span span)
{
	if (upstream->direction != PIGEN_SEMANTIC_OUTPUT ||
		upstream->endpoint_view.index == PIGEN_INVALID_ID) return INVALID_ID(pigen_expr_id);
	const pigen_semantic_expr *known = ownership_expression(model, value);
	const pigen_semantic_expr *producer = ownership_expression(model, upstream->value);
	if (!known || !producer || producer->kind != PIGEN_EXPR_SYMBOL ||
		(known->kind != PIGEN_EXPR_INDEX && known->kind != PIGEN_EXPR_SELECT)) return INVALID_ID(pigen_expr_id);
	pigen_semantic_expr expr = *known;
	if (expr.kind == PIGEN_EXPR_SELECT && expr.as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE)
		return INVALID_ID(pigen_expr_id);
	pigen_expr_id base = expr.kind == PIGEN_EXPR_INDEX ? expr.as.index.base : expr.as.select.base;
	pigen_expr_id index = expr.kind == PIGEN_EXPR_INDEX ? expr.as.index.index : expr.as.select.left;
	if (!ownership_same_projection(model, base, upstream->value)) return INVALID_ID(pigen_expr_id);
	const pigen_semantic_signal *endpoint = pigen_signal_get(model, upstream->endpoint_view);
	const pigen_semantic_expr *index_expr = pigen_expr_get(model, index);
	int64_t sl, sr, el, er;
	uint64_t sw, ew, iw;
	if (!endpoint || !index_expr ||
		!packed_coordinate_range(model, producer->data_type, producer->shape, &sl, &sr) ||
		!packed_coordinate_range(model, endpoint->data_type, endpoint->shape, &el, &er) ||
		!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, producer->data_type), &sw) ||
		!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, endpoint->data_type), &ew) ||
		!pigen_const_expr_evaluate_u64(model, pigen_data_type_packed_width(model, index_expr->data_type), &iw) ||
		sw != ew || !iw || iw > UINT64_MAX-3) return INVALID_ID(pigen_expr_id);
	pigen_symbol_id symbol = endpoint->symbol;
	pigen_data_type_id payload_type = endpoint->data_type;
	pigen_data_type_id index_type = index_expr->data_type;
	pigen_packed_dimension dimension = {pigen_const_expr_intern_integer(model, (iw > 64 ? iw : 64)+2, pigen_data_type_unsized_integer(model)),
		pigen_const_expr_intern_integer(model, 0, pigen_data_type_unsized_integer(model))};
	pigen_data_type_id wide = pigen_data_type_implicit(model, PIGEN_SIGN_SIGNED, &dimension, 1);
	pigen_conversion conversion;
	if (!pigen_data_type_resolve_assignment_conversion(model, index_type, wide, &conversion))
		return INVALID_ID(pigen_expr_id);
	index = resolve_input_aliases(model, index);
	pigen_expr_id coordinate = pigen_expr_add_conversion(model, conversion, index, span);
	pigen_expr_id source_right = pigen_expr_add_integer(model,
		sr < 0 ? (uint64_t)(-(sr+1))+1 : (uint64_t)sr, wide, span);
	pigen_expr_id endpoint_right = pigen_expr_add_integer(model,
		er < 0 ? (uint64_t)(-(er+1))+1 : (uint64_t)er, wide, span);
	pigen_unary_operation negate = {PIGEN_UNARY_NEGATE, wide, wide};
	if (sr < 0) source_right = pigen_expr_add_unary(model, negate, source_right, span);
	if (er < 0) endpoint_right = pigen_expr_add_unary(model, negate, endpoint_right, span);
	int reversed = (sl >= sr) != (el >= er);
	coordinate = pigen_expr_add_binary(model, (pigen_binary_operation){PIGEN_BINARY_SUBTRACT, wide, wide, wide},
		reversed ? source_right : coordinate, reversed ? coordinate : source_right, span);
	coordinate = pigen_expr_add_binary(model, (pigen_binary_operation){PIGEN_BINARY_ADD, wide, wide, wide}, endpoint_right, coordinate, span);
	pigen_expr_id source = pigen_expr_add_symbol(model, symbol, payload_type, span);
	pigen_select_kind kind = expr.kind == PIGEN_EXPR_SELECT ? expr.as.select.kind : PIGEN_SEMANTIC_SELECT_RANGE;
	if (expr.kind == PIGEN_EXPR_SELECT && reversed)
		kind = kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP ? PIGEN_SEMANTIC_SELECT_INDEXED_DOWN : PIGEN_SEMANTIC_SELECT_INDEXED_UP;
	pigen_expr_id projected = expr.kind == PIGEN_EXPR_INDEX ?
		pigen_expr_add_index(model, source, coordinate, span) :
		pigen_expr_add_select(model, source, coordinate, expr.as.select.right, kind, span);
	const pigen_semantic_expr *result = pigen_expr_get(model, projected);
	if (!result || !pigen_data_type_resolve_assignment_conversion(model, result->data_type, target_type, &conversion))
		return INVALID_ID(pigen_expr_id);
	return conversion.kind == PIGEN_CONVERSION_IDENTITY ? projected :
		pigen_expr_add_conversion(model, conversion, projected, span);
}

/* Rebuild only changed expression paths; never mutate shared expression nodes.
 * A selection must map as a unit, since replacing its base can rebase coordinates. */
static pigen_expr_id resolve_input_aliases(pigen_semantic_model *model,
	pigen_expr_id value)
{
	const pigen_semantic_expr *original = pigen_expr_get(model, value);
	if (!original) return value;
	pigen_semantic_expr expr = *original;
	if (expr.kind == PIGEN_EXPR_SYMBOL || expr.kind == PIGEN_EXPR_SELECT ||
		expr.kind == PIGEN_EXPR_INDEX)
	{
		for (size_t i = 0; i < model->instance_connection_count; i++)
		{
			const pigen_semantic_instance_connection *upstream = &model->instance_connections[i];
			if (upstream->direction != PIGEN_SEMANTIC_OUTPUT ||
				upstream->endpoint_view.index == PIGEN_INVALID_ID) continue;
			const pigen_semantic_signal *source = pigen_signal_get(model, upstream->endpoint_view);
			if (source && source->shape.index == expr.shape.index &&
				ownership_same_projection(model, value, upstream->value))
			{
				uint64_t source_width, value_width;
				pigen_conversion conversion;
				if (pigen_const_expr_evaluate_u64(model,
					pigen_data_type_packed_width(model, source->data_type), &source_width) &&
					pigen_const_expr_evaluate_u64(model,
						pigen_data_type_packed_width(model, expr.data_type), &value_width) &&
					source_width == value_width &&
					pigen_data_type_resolve_assignment_conversion(model,
						source->data_type, expr.data_type, &conversion))
				{
					pigen_expr_id mapped = pigen_expr_add_symbol(model, source->symbol, source->data_type, expr.span);
					return conversion.kind == PIGEN_CONVERSION_IDENTITY ? mapped :
						pigen_expr_add_conversion(model, conversion, mapped, expr.span);
				}
			}
			pigen_expr_id mapped = partial_endpoint_alias(model, value, upstream, expr.data_type, expr.span);
			if (mapped.index == PIGEN_INVALID_ID)
				mapped = dynamic_endpoint_alias(model, value, upstream, expr.data_type, expr.span);
			if (mapped.index != PIGEN_INVALID_ID) return mapped;
		}
		return value;
	}
	pigen_expr_id a, b, c;
	switch (expr.kind)
	{
	case PIGEN_EXPR_GROUP:
		a = resolve_input_aliases(model, expr.as.group.operand);
		return a.index == expr.as.group.operand.index ? value : pigen_expr_add_group(model, a, expr.span);
	case PIGEN_EXPR_CONVERSION:
		a = resolve_input_aliases(model, expr.as.conversion.operand);
		return a.index == expr.as.conversion.operand.index ? value :
			pigen_expr_add_conversion(model, expr.as.conversion.conversion, a, expr.span);
	case PIGEN_EXPR_UNARY:
		a = resolve_input_aliases(model, expr.as.unary.operand);
		return a.index == expr.as.unary.operand.index ? value :
			pigen_expr_add_unary(model, expr.as.unary.operation, a, expr.span);
	case PIGEN_EXPR_BINARY:
		a = resolve_input_aliases(model, expr.as.binary.left);
		b = resolve_input_aliases(model, expr.as.binary.right);
		return a.index == expr.as.binary.left.index && b.index == expr.as.binary.right.index ? value :
			pigen_expr_add_binary(model, expr.as.binary.operation, a, b, expr.span);
	case PIGEN_EXPR_CONDITIONAL:
		a = resolve_input_aliases(model, expr.as.conditional.condition);
		b = resolve_input_aliases(model, expr.as.conditional.when_true);
		c = resolve_input_aliases(model, expr.as.conditional.when_false);
		return a.index == expr.as.conditional.condition.index &&
			b.index == expr.as.conditional.when_true.index && c.index == expr.as.conditional.when_false.index ? value :
			pigen_expr_add_conditional(model, expr.as.conditional.operation, a, b, c, expr.span);
	case PIGEN_EXPR_PEEK:
		a = resolve_input_aliases(model, expr.as.peek.source);
		return a.index == expr.as.peek.source.index ? value : pigen_expr_add_peek(model, a, expr.span);
	case PIGEN_EXPR_EVALUATED_VALUE:
		a=resolve_input_aliases(model,expr.as.evaluation.value);
		b=resolve_input_aliases(model,expr.as.evaluation.evaluated);
		return a.index==expr.as.evaluation.value.index && b.index==expr.as.evaluation.evaluated.index ? value :
			pigen_expr_add_evaluated_value(model,a,b,expr.as.evaluation.before,expr.span);
	case PIGEN_EXPR_ARRAY_PATTERN:
	case PIGEN_EXPR_CONCATENATION:
	{
		size_t count = expr.as.sequence.child_count;
		pigen_expr_id *children = malloc(count * sizeof *children);
		if (!children && count) return INVALID_ID(pigen_expr_id);
		const pigen_expr_id *old = pigen_expr_children(model, expr.as.sequence.first_child, count);
		if (count) memcpy(children, old, count * sizeof *children);
		int changed = 0;
		for (size_t i = 0; i < count; i++)
		{
			a = resolve_input_aliases(model, children[i]);
			changed |= a.index != children[i].index;
			children[i] = a;
		}
		a = changed ? (expr.kind==PIGEN_EXPR_ARRAY_PATTERN ?
			pigen_expr_add_array_pattern(model,expr.data_type,expr.shape,children,count,expr.span) :
			pigen_expr_add_concatenation(model, children, count, expr.span)) : value;
		free(children);
		return a;
	}
	default:
		return value;
	}
}

static int ownership_disjoint(pigen_semantic_model *model,
	pigen_expr_id left, pigen_expr_id right)
{
	const pigen_semantic_expr *a = ownership_expression(model, left);
	const pigen_semantic_expr *b = ownership_expression(model, right);
	pigen_expr_id abase, bbase;
	int64_t alow, ahigh, blow, bhigh;
	if (!ownership_selection(model, a, &abase, &alow, &ahigh) ||
		!ownership_selection(model, b, &bbase, &blow, &bhigh)) return 0;
	if (ownership_same_projection(model, abase, bbase))
		return ahigh < blow || bhigh < alow;
	return ownership_disjoint(model, abase, bbase);
}
static int continuous_destinations_disjoint(pigen_semantic_model *model,
	pigen_lvalue_id left, pigen_lvalue_id right, pigen_signal_id signal)
{
	const pigen_semantic_lvalue *a = pigen_lvalue_get(model, left);
	const pigen_semantic_lvalue *b = pigen_lvalue_get(model, right);
	if (!a || !b) return 0;
	if (a->kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children = pigen_lvalue_children(model,
			a->as.sequence.first_child, a->as.sequence.child_count);
		if (!children) return 0;
		for (size_t i = 0; i < a->as.sequence.child_count; i++)
			if (!continuous_destinations_disjoint(model, children[i], right, signal)) return 0;
		return 1;
	}
	if (b->kind == PIGEN_LVALUE_CONCATENATION)
		return continuous_destinations_disjoint(model, right, left, signal);
	if (a->kind != PIGEN_LVALUE_PROJECTION || b->kind != PIGEN_LVALUE_PROJECTION) return 0;
	if (a->as.projection.signal.index != signal.index || b->as.projection.signal.index != signal.index) return 1;
	return ownership_disjoint(model, a->expression, b->expression);
}

typedef struct {
	pigen_module_id module;
	pigen_signal_id signal;
	pigen_source_span span;
	pigen_expr_id destination;
} instance_destination_owner;

static int endpoint_has_readonly_control(pigen_semantic_model *model,
	pigen_signal_id endpoint, pigen_semantic_direction direction);

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
				if (!pigen_transfer_type_descriptor_get(parent->transfer_type)->is_static &&
					endpoint_has_readonly_control(model, (pigen_signal_id){(uint32_t)s}, PIGEN_SEMANTIC_INPUT))
				{
					free(owners);
					return fail(resolver, connections[c].span, "automatic producer conflicts with read-only child valid binding");
				}
				for (o = 0; o < owner_count; o++)
					if (owners[o].module.index == parent->module.index &&
						owners[o].signal.index == s &&
						!(pigen_transfer_type_descriptor_get(parent->transfer_type)->is_static &&
							ownership_disjoint(model, owners[o].destination, connections[c].value)))
					{
						free(owners);
						return fail(resolver, connections[c].span,
							"buffered instance destination has multiple producers");
					}
				owners = pigen_resize(owners,
					(owner_count + 1) * sizeof(*owners));
				owners[owner_count++] = (instance_destination_owner){
					parent->module, (pigen_signal_id){(uint32_t)s},
					connections[c].span, connections[c].value};
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
static int connection_consumes_endpoint(pigen_semantic_model *model,
	pigen_expr_id value, pigen_signal_id endpoint)
{
	const pigen_semantic_signal *signal = pigen_signal_get(model, endpoint);
	if (resolve_expr_references_symbol(model, value, signal->symbol)) return 1;
	for (size_t c = 0; c < model->instance_connection_count; c++)
	{
		const pigen_semantic_instance_connection *connection = &model->instance_connections[c];
		if (connection->endpoint_view.index != endpoint.index) continue;
		if (ownership_same_projection(model, value, connection->value)) return 1;
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

static int endpoint_has_readonly_control(pigen_semantic_model *model,
	pigen_signal_id endpoint, pigen_semantic_direction direction)
{
	endpoint = canonical_endpoint(model, endpoint);
	for (size_t i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *payload = &model->instance_connections[instance->first_connection+c];
			if (payload->direction != direction ||
				canonical_endpoint(model, connection_signal(model, payload)).index != endpoint.index) continue;
			const pigen_symbol *port = pigen_symbol_get(model, payload->port_symbol);
			size_t n = 0;
			const char *base = pigen_source_span_text(model->sources, port->name, &n);
			for (size_t d = 0; base && d < instance->connection_count; d++)
			{
				const pigen_semantic_instance_connection *control = &model->instance_connections[instance->first_connection+d];
				size_t m = 0;
				const char *name = pigen_source_span_text(model->sources, control->port_name, &m);
				if (name && m == n+6 && !memcmp(name, base, n) && !memcmp(name+n, direction == PIGEN_SEMANTIC_OUTPUT ? "_ready" : "_valid", 6) &&
					!pigen_lvalue_is_writable(model, pigen_lvalue_resolve(model, control->value))) return 1;
			}
		}
	}
	return 0;
}

static int endpoint_has_readonly_ready(pigen_semantic_model *model,
	pigen_signal_id endpoint)
{
	return endpoint_has_readonly_control(model, endpoint, PIGEN_SEMANTIC_OUTPUT);
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

static int add_fabric_protocol_edge(resolver *resolver,pigen_module_id module,pigen_scope_id scope,
	pigen_syntax_id syntax, const pigen_syntax_node *edge,
	pigen_protocol_endpoint_id source, pigen_protocol_endpoint_id destination)
{
	pigen_semantic_model *model = resolver->model;
	const pigen_semantic_protocol_endpoint *src = pigen_protocol_endpoint_get(model, source);
	const pigen_semantic_protocol_endpoint *dst = pigen_protocol_endpoint_get(model, destination);
	if (!src || !dst || !pigen_protocol_can_route(pigen_protocol_type_get(model, src->type),
		pigen_protocol_type_get(model, dst->type), 1) ||
		source.index==destination.index ||
		src->role != PIGEN_PROTOCOL_TARGET || dst->role != PIGEN_PROTOCOL_INITIATOR)
		return fail_location(resolver, edge->location, "AXI fabric requires compatible native initiator-to-target endpoints");
	if (!src->binding.is_bound || !dst->binding.is_bound ||
		src->binding.domain.index != dst->binding.domain.index ||
		src->binding.reset_symbol.index != dst->binding.reset_symbol.index ||
		src->binding.reset_active_low != dst->binding.reset_active_low)
		return fail_location(resolver, edge->location, "AXI fabric requires compatible clock domains and reset bindings");
	const pigen_protocol *routing_protocol=pigen_protocol_type_get(model,src->type);
	uint64_t address_base=0,address_limit=0;
	int has_window=edge->as.fabric_connection.has_window;
	if(has_window)
	{
		if(edge->as.fabric_connection.direct || routing_protocol->family==PIGEN_PROTOCOL_AXI4_STREAM)
			return fail_location(resolver,edge->location,"address windows require routed native memory-mapped AXI links");
		pigen_expr_id base=resolve_constant(resolver,scope,edge->as.fabric_connection.window_base);
		pigen_expr_id limit=resolve_constant(resolver,scope,edge->as.fabric_connection.window_limit);
		if(base.index==PIGEN_INVALID_ID || limit.index==PIGEN_INVALID_ID ||
			!pigen_const_expr_evaluate_u64(model,pigen_expr_constant(model,base),&address_base) ||
			!pigen_const_expr_evaluate_u64(model,pigen_expr_constant(model,limit),&address_limit) || address_base>address_limit ||
			(routing_protocol->address_bits<64 && (address_limit>>routing_protocol->address_bits)))
			return fail_location(resolver,edge->location,"AXI address windows require known nonnegative 64-bit bounds within the address width, base <= limit");
	}
	size_t source_count = 1, destination_count = 1;
	for (size_t p = 0; p < model->protocol_fabric_edge_count; p++)
	{
		const pigen_semantic_protocol_fabric_edge *prior = &model->protocol_fabric_edges[p];
		if (prior->source.index == source.index)
		{
			if(prior->destination.index==destination.index || prior->direct || edge->as.fabric_connection.direct)
				return fail_location(resolver,edge->location,"AXI fabric source has another fabric consumer; routed fan-out requires distinct destinations");
			if(routing_protocol->family==PIGEN_PROTOCOL_AXI4_STREAM)
			{
				if(!routing_protocol->destination_bits) return fail_location(resolver,edge->location,"AXI fabric source has another fabric consumer; Stream fan-out requires TDEST");
			}
			else
			{
				if(!has_window || !prior->has_window) return fail_location(resolver,edge->location,"AXI fabric source has another fabric consumer; memory-mapped fan-out requires address windows on every link");
				if(address_base<=prior->address_limit && prior->address_base<=address_limit)
					return fail_location(resolver,edge->location,"AXI address windows from one source must not overlap");
			}
			destination_count++;
		}
		if (prior->destination.index == destination.index && (prior->direct || edge->as.fabric_connection.direct))
			return fail_location(resolver, edge->location, "direct AXI fabric destination cannot appear in another connection");
		if (prior->destination.index == destination.index)
		{
			const pigen_semantic_protocol_endpoint *other = pigen_protocol_endpoint_get(model, prior->source);
			if (other->type.index != src->type.index)
				return fail_location(resolver, edge->location, "AXI fabric initiators require matching protocol contracts");
			source_count++;
		}
	}
	if(destination_count>32) return fail_location(resolver,edge->location,"AXI fabric fan-out supports at most 32 destinations");
	if(routing_protocol->family==PIGEN_PROTOCOL_AXI4_STREAM && destination_count>1 && routing_protocol->destination_bits<5 &&
		destination_count>((size_t)1<<routing_protocol->destination_bits))
		return fail_location(resolver,edge->location,"Stream fabric fan-out requires sufficient TDEST bits and at most 32 destinations");
	if (!pigen_protocol_can_route(pigen_protocol_type_get(model, src->type),
		pigen_protocol_type_get(model, dst->type), source_count))
		return fail_location(resolver, edge->location, "AXI fabric target ID width must fit the initiator ID plus ceil(log2(source count)) source-tag bits");
	for (size_t t = 0; t < model->transfer_count; t++)
	{
		const pigen_semantic_transfer *transfer = &model->transfers[t];
		if (transfer->module.index != module.index) continue;
		const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(model, (pigen_transfer_id){(uint32_t)t});
		for (size_t u = 0; u < transfer->signal_use_count; u++)
			for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
			{
				unsigned roles = PIGEN_TRANSFER_CONSUMER | PIGEN_TRANSFER_SIGNAL_WRITE;
				if ((uses[u].signal.index == src->channels[channel].index && (uses[u].roles & roles)) ||
					(uses[u].signal.index == dst->channels[channel].index && (uses[u].roles & roles)))
					return fail_location(resolver, edge->location, "AXI fabric endpoint has another transfer owner");
			}
	}
	if (model->protocol_fabric_edge_count == model->protocol_fabric_edge_capacity)
	{
		model->protocol_fabric_edge_capacity = model->protocol_fabric_edge_capacity ? model->protocol_fabric_edge_capacity * 2 : 8;
		model->protocol_fabric_edges = pigen_resize(model->protocol_fabric_edges,
			model->protocol_fabric_edge_capacity * sizeof(*model->protocol_fabric_edges));
	}
	model->protocol_fabric_edges[model->protocol_fabric_edge_count++] = (pigen_semantic_protocol_fabric_edge){
		.syntax=syntax,.module=module,.source=source,.destination=destination,
		.direct=edge->as.fabric_connection.direct,.span=edge->location.source_span,
		.has_window=has_window,.address_base=address_base,.address_limit=address_limit};
	return 1;
}

/* Whole collections zip by equal evaluated coordinates. Publish the same
 * scalar edges used by indexed links, preserving every lane's ownership. */
static int add_fabric_protocol_arrays(resolver *resolver,pigen_module_id module,
	pigen_scope_id scope,pigen_syntax_id syntax,const pigen_syntax_node *edge,int *handled)
{
	pigen_protocol_array_view source={0},destination={0};int ok=0;
	int a=pigen_resolve_protocol_array_view(resolver->syntax,resolver->model,scope,
		edge->as.fabric_connection.left_expression,resolver->policy,&source);
	int b=pigen_resolve_protocol_array_view(resolver->syntax,resolver->model,scope,
		edge->as.fabric_connection.right_expression,resolver->policy,&destination);
	*handled=a<0 || b<0 || (a && source.dimension_count) || (b && destination.dimension_count);
	if(!*handled) {ok=1;goto done;}
	if(a<0 || b<0)
	{fail_location(resolver,edge->location,"AXI fabric collection selection requires in-bounds constant indices and slices");goto done;}
	if(!a || !b || source.dimension_count!=destination.dimension_count || source.element_count!=destination.element_count)
	{fail_location(resolver,edge->location,"AXI fabric collections require matching rank, bounds and direction");goto done;}
	for(size_t d=0;d<source.dimension_count;d++)
		if(source.dimensions[d].left!=destination.dimensions[d].left || source.dimensions[d].right!=destination.dimensions[d].right)
		{fail_location(resolver,edge->location,"AXI fabric collections require matching rank, bounds and direction");goto done;}
	for(size_t lane=0;lane<source.element_count;lane++)
		if(!add_fabric_protocol_edge(resolver,module,scope,syntax,edge,source.elements[lane],destination.elements[lane])) goto done;
	ok=1;
 done:
	pigen_free_protocol_array_view(&source);pigen_free_protocol_array_view(&destination);return ok;
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
/* Selected occurrences already follow generate iteration/body order. Insert
 * only this root region's occurrences at its source position; sorting template
 * spans would incorrectly group equal statements across different iterations. */
static int collect_region_fabrics(resolver *resolver,pigen_module_id module,pigen_syntax_id region)
{
	pigen_semantic_model *model=resolver->model;
	for(size_t i=0;i<model->generate_item_count;i++)
	{
		pigen_semantic_generate_item item=model->generate_items[i];
		if(item.module.index!=module.index) continue;
		const pigen_syntax_node *fabric=pigen_syntax_get(resolver->syntax,item.syntax);
		if(fabric->kind!=PIGEN_SYNTAX_FABRIC) continue;
		pigen_syntax_id ancestor=item.syntax;
		while(ancestor.index!=PIGEN_INVALID_ID && ancestor.index!=region.index)
		{
			const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,ancestor);
			ancestor=node ? node->parent : INVALID_ID(pigen_syntax_id);
		}
		if(ancestor.index!=region.index) continue;
		for(pigen_syntax_id e=fabric->first_child;e.index!=PIGEN_INVALID_ID;)
		{
			const pigen_syntax_node *edge=pigen_syntax_get(resolver->syntax,e);
			if(edge->kind==PIGEN_SYNTAX_FABRIC_CONNECTION)
			{
				int handled;
				if(!add_fabric_protocol_arrays(resolver,item.module,item.scope,e,edge,&handled)) return 0;
				if(handled) {e=edge->next_sibling;continue;}
				pigen_protocol_endpoint_id source=pigen_resolve_protocol_reference(resolver->syntax,model,item.scope,edge->as.fabric_connection.left_expression, resolver->policy);
				pigen_protocol_endpoint_id destination=pigen_resolve_protocol_reference(resolver->syntax,model,item.scope,edge->as.fabric_connection.right_expression, resolver->policy);
				if(!add_fabric_protocol_edge(resolver,item.module,item.scope,e,edge,source,destination)) return 0;
			}
			e=edge->next_sibling;
		}
	}
	return 1;
}

static int validate_fabric_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	for (size_t m = 0; m < model->module_count; m++)
	{
		const pigen_syntax_node *module = pigen_syntax_get(resolver->syntax, model->modules[m].syntax);
		for (pigen_syntax_id item = module->first_child; item.index != PIGEN_INVALID_ID; )
		{
			pigen_syntax_id root_item=item;
			const pigen_syntax_node *fabric = pigen_syntax_get(resolver->syntax, item);
			item = fabric->next_sibling;
			if(fabric->kind==PIGEN_SYNTAX_GENERATE_REGION)
			{
				if(!collect_region_fabrics(resolver,(pigen_module_id){(uint32_t)m},root_item)) return 0;
				continue;
			}
			if (fabric->kind != PIGEN_SYNTAX_FABRIC) continue;
			for (pigen_syntax_id e = fabric->first_child; e.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *edge = pigen_syntax_get(resolver->syntax, e);
				e = edge->next_sibling;
				if (edge->kind != PIGEN_SYNTAX_FABRIC_CONNECTION) continue;
				int handled;
				pigen_syntax_id edge_id={(uint32_t)(edge-resolver->syntax->nodes)};
				if(!add_fabric_protocol_arrays(resolver,(pigen_module_id){(uint32_t)m},model->modules[m].scope,edge_id,edge,&handled)) return 0;
				if(handled) continue;
				pigen_protocol_endpoint_id native_source = pigen_resolve_protocol_reference(resolver->syntax, model,
					model->modules[m].scope, edge->as.fabric_connection.left_expression, resolver->policy);
				pigen_protocol_endpoint_id native_destination = pigen_resolve_protocol_reference(resolver->syntax, model,
					model->modules[m].scope, edge->as.fabric_connection.right_expression, resolver->policy);
				if (native_source.index != PIGEN_INVALID_ID || native_destination.index != PIGEN_INVALID_ID)
				{
					pigen_syntax_id edge_id = {(uint32_t)(edge - resolver->syntax->nodes)};
					if (!add_fabric_protocol_edge(resolver,(pigen_module_id){(uint32_t)m},model->modules[m].scope,edge_id,edge,
						native_source, native_destination)) return 0;
					continue;
				}
				if(edge->as.fabric_connection.has_window) return fail_location(resolver,edge->location,"address windows require native memory-mapped AXI endpoints");
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
				if (endpoint_has_readonly_control(model, dst, PIGEN_SEMANTIC_INPUT))
					return fail(resolver, edge->location.source_span, "automatic producer conflicts with read-only child valid binding");
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

static void mark_endpoint_view_driven(pigen_semantic_model *model, pigen_signal_id signal)
{
	if (signal.index == PIGEN_INVALID_ID) return;
	for (size_t c = 0; c < model->instance_connection_count; c++)
		if (model->instance_connections[c].direction == PIGEN_SEMANTIC_INPUT &&
			model->instance_connections[c].endpoint_view.index == signal.index)
			model->instance_connections[c].endpoint_view_driven = 1;
}

static void resolve_endpoint_view_drivers(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	for (size_t c = 0; c < model->instance_connection_count; c++)
	{
		const pigen_semantic_instance_connection *connection = &model->instance_connections[c];
		if (connection->direction != PIGEN_SEMANTIC_OUTPUT) continue;
		const pigen_semantic_expr *value = pigen_expr_get(model, connection->value);
		while (value && value->kind == PIGEN_EXPR_CONVERSION)
			value = pigen_expr_get(model, value->as.conversion.operand);
		if (value && value->kind == PIGEN_EXPR_SYMBOL)
			mark_endpoint_view_driven(model, pigen_symbol_signal(model, value->as.symbol));
	}
	for (size_t t = 0; t < model->transfer_count; t++)
	{
		const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(model,
			(pigen_transfer_id){(uint32_t)t});
		for (size_t u = 0; u < model->transfers[t].signal_use_count; u++)
			if (uses[u].roles & PIGEN_TRANSFER_SIGNAL_WRITE) mark_endpoint_view_driven(model, uses[u].signal);
	}
	for (size_t m = 0; m < model->module_count; m++)
	{
		const pigen_syntax_node *module = pigen_syntax_get(resolver->syntax, model->modules[m].syntax);
		for (pigen_syntax_id i = module->first_child; i.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *item = pigen_syntax_get(resolver->syntax, i);
			i = item->next_sibling;
			if (item->kind != PIGEN_SYNTAX_FABRIC) continue;
			for (pigen_syntax_id e = item->first_child; e.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *edge = pigen_syntax_get(resolver->syntax, e);
				e = edge->next_sibling;
				if (edge->kind != PIGEN_SYNTAX_FABRIC_CONNECTION) continue;
				const pigen_semantic_instance_connection *destination = resolved_fabric_port(resolver,
					(pigen_module_id){(uint32_t)m}, edge->as.fabric_connection.right_instance, edge->as.fabric_connection.right_port);
				if (destination) mark_endpoint_view_driven(model, destination->endpoint_view);
			}
		}
	}
}

/* Endpoint identities can differ while their externally bound payload overlaps.
 * Validate the physical destinations of output views and driven input views. */
static pigen_signal_id endpoint_view_destination(pigen_semantic_model *model,
	const pigen_semantic_instance_connection *connection)
{
	if (connection->endpoint_view.index == PIGEN_INVALID_ID ||
		(connection->direction != PIGEN_SEMANTIC_OUTPUT && !connection->endpoint_view_driven))
		return INVALID_ID(pigen_signal_id);
	const pigen_semantic_expr *value = ownership_expression(model, connection->value);
	if (!value) return INVALID_ID(pigen_signal_id);
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(model,
		pigen_lvalue_resolve(model, (pigen_expr_id){(uint32_t)(value-model->expressions)}));
	return lvalue && lvalue->kind == PIGEN_LVALUE_PROJECTION ?
		lvalue->as.projection.signal : INVALID_ID(pigen_signal_id);
}

static int validate_endpoint_view_destinations(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	for (size_t later = 0; later < model->instance_connection_count; later++)
	{
		const pigen_semantic_instance_connection *b = &model->instance_connections[later];
		pigen_signal_id destination = endpoint_view_destination(model, b);
		if (destination.index == PIGEN_INVALID_ID) continue;
		for (size_t earlier = 0; earlier < later; earlier++)
		{
			const pigen_semantic_instance_connection *a = &model->instance_connections[earlier];
			if (a->endpoint_view.index == b->endpoint_view.index ||
				endpoint_view_destination(model, a).index != destination.index) continue;
			const pigen_semantic_signal *signal = pigen_signal_get(model, destination);
			if (pigen_transfer_type_descriptor_get(signal->transfer_type)->is_static &&
				ownership_disjoint(model, a->value, b->value)) continue;
			return fail(resolver, b->span, "child endpoint aliases have overlapping producers");
		}
	}
	return 1;
}

/* Resolve the completed graph: a later fan-out link can turn an earlier
 * destination merge into a switched merge, irrespective of source order. */
static int validate_stream_switch_contracts(resolver *resolver)
{
	pigen_semantic_model *model=resolver->model;
	if(!model->protocol_fabric_edge_count) return 1;
	size_t *incoming=pigen_resize(NULL,model->protocol_endpoint_count*sizeof(*incoming));
	size_t *outgoing=pigen_resize(NULL,model->protocol_endpoint_count*sizeof(*outgoing));
	memset(incoming,0,model->protocol_endpoint_count*sizeof(*incoming));
	memset(outgoing,0,model->protocol_endpoint_count*sizeof(*outgoing));
	for(size_t e=0;e<model->protocol_fabric_edge_count;e++)
	{
		const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[e];
		incoming[edge->destination.index]++;outgoing[edge->source.index]++;
	}
	int ok=1;
	for(size_t e=0;e<model->protocol_fabric_edge_count;e++)
	{
		const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[e];
		if(incoming[edge->destination.index]<2 || outgoing[edge->source.index]<2) continue;
		const pigen_protocol *source=pigen_protocol_type_get(model,model->protocol_endpoints[edge->source.index].type);
		if(source->family!=PIGEN_PROTOCOL_AXI4_STREAM) continue;
		const pigen_protocol *target=pigen_protocol_type_get(model,model->protocol_endpoints[edge->destination.index].type);
		size_t tag_bits=0;for(size_t n=incoming[edge->destination.index]-1;n;n>>=1) tag_bits++;
		if(source->id_bits>SIZE_MAX-tag_bits || target->id_bits<source->id_bits+tag_bits)
		{ok=fail(resolver,edge->span,"switched Stream fabric target TID must fit source ID plus ceil(log2(source count)) provenance bits");break;}
	}
	/* An exact-ID merge tracks the full (TID,TDEST) namespace. This is a
	 * storage requirement, not permission to truncate or alias identities. */
	for(size_t e=0;ok && e<model->protocol_fabric_edge_count;e++)
	{
		const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[e];
		if(incoming[edge->destination.index]<2) continue;
		const pigen_protocol *source=pigen_protocol_type_get(model,model->protocol_endpoints[edge->source.index].type);
		const pigen_protocol *target=pigen_protocol_type_get(model,model->protocol_endpoints[edge->destination.index].type);
		if(source->family!=PIGEN_PROTOCOL_AXI4_STREAM || source->id_bits!=target->id_bits || !(source->stream_fields&PIGEN_STREAM_LAST)) continue;
		if(source->id_bits>SIZE_MAX-source->destination_bits)
		{ok=fail(resolver,edge->span,"exact-ID Stream packet tracking exceeds generated-bit policy; widen target TID for provenance");break;}
		size_t keys=source->id_bits+source->destination_bits,entries=1;
		for(size_t bit=0;bit<keys;bit++)
		{
			if(entries>resolver->policy->maximum_generated_bits/2)
			{ok=fail(resolver,edge->span,"exact-ID Stream packet tracking exceeds generated-bit policy; widen target TID for provenance");break;}
			entries*=2;
		}
	}
	free(incoming);free(outgoing);return ok;
}

static int validate_signal_ownership(resolver *resolver)
{
	pigen_semantic_model *model = resolver->model;
	size_t later_index;

	resolve_endpoint_view_drivers(resolver);
	if (!validate_instance_destination_ownership(resolver) ||
		!validate_instance_consumer_ownership(resolver) ||
		!validate_fabric_ownership(resolver) ||
		!validate_stream_switch_contracts(resolver) ||
		!validate_endpoint_view_destinations(resolver)) return 0;

	for (later_index = 0; later_index < model->transfer_count; later_index++)
	{
		const pigen_semantic_transfer *later = pigen_transfer_get(model,
			(pigen_transfer_id){(uint32_t)later_index});
		const pigen_transfer_signal_use *later_uses =
			pigen_transfer_signal_uses(model,
				(pigen_transfer_id){(uint32_t)later_index});
		size_t earlier_index;
		size_t i;

		for (i = 0; i < later->signal_use_count; i++)
			if ((later_uses[i].roles & PIGEN_TRANSFER_SIGNAL_WRITE) &&
				!pigen_transfer_type_descriptor_get(model->signals[later_uses[i].signal.index].transfer_type)->is_static &&
				endpoint_has_readonly_control(model, later_uses[i].signal, PIGEN_SEMANTIC_INPUT))
				return fail(resolver, later->span, "automatic producer conflicts with read-only child valid binding");

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
					const pigen_semantic_process *a_process = pigen_process_get(model, earlier->process);
					const pigen_semantic_process *b_process = pigen_process_get(model, later->process);
					if ((earlier_uses[j].roles & later_uses[i].roles & PIGEN_TRANSFER_SIGNAL_WRITE) &&
						((a_process && a_process->is_continuous) || (b_process && b_process->is_continuous) ||
						 (earlier->process.index != later->process.index &&
						 ((a_process && a_process->is_combinational) || (b_process && b_process->is_combinational)))))
					{
						if (!continuous_destinations_disjoint(model, earlier->destination, later->destination, later_uses[i].signal))
							return fail(resolver, later->span, "continuous assignment has overlapping writers");
					}
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

static int resolve_package_exports(resolver *resolver,pigen_scope_id scope,
 const pigen_syntax_node *package,unsigned char *done,size_t at,int final)
{
 for(pigen_syntax_id id=package->first_child;id.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,id);
  if(item->kind==PIGEN_SYNTAX_EXPORT && !done[id.index] &&
   item->location.extent.first.index<=at && (final || !item->as.package_import.wildcard))
  {
   pigen_source_span name=token_spelling(resolver,item->as.package_import.package);
   size_t length=0;const char *text=pigen_source_span_text(resolver->model->sources,name,&length);
   int all=text && length==1 && text[0]=='*';
   pigen_symbol_id source=all ? INVALID_ID(pigen_symbol_id) : pigen_symbol_lookup(resolver->model,scope,name);
   if(!all && source.index==PIGEN_INVALID_ID)
    return fail_location(resolver,item->location,"package export requires a declared package");
   pigen_declare_result result=pigen_export_package(resolver->model,scope,source,
    token_spelling(resolver,item->as.package_import.member),item->as.package_import.wildcard,at,item->location.source_span);
   if(result==PIGEN_DECLARE_INVALID && !item->as.package_import.wildcard)
   {
    /* A named export is a reference even when its corresponding import is
     * authored later. Only that name becomes visible at this point. */
    for(pigen_syntax_id candidate=package->first_child;candidate.index!=PIGEN_INVALID_ID;)
    {
     const pigen_syntax_node *import=pigen_syntax_get(resolver->syntax,candidate);
     if(import->kind==PIGEN_SYNTAX_IMPORT &&
      spelling_text_equal(resolver,token_spelling(resolver,import->as.package_import.package),name) &&
      (import->as.package_import.wildcard || spelling_text_equal(resolver,
       token_spelling(resolver,import->as.package_import.member),token_spelling(resolver,item->as.package_import.member))))
     {
      result=pigen_import_package(resolver->model,scope,source,
       token_spelling(resolver,item->as.package_import.member),0,at,item->location.source_span);
      if(result==PIGEN_DECLARE_OK) result=pigen_export_package(resolver->model,scope,source,
       token_spelling(resolver,item->as.package_import.member),0,at,item->location.source_span);
      break;
     }
     candidate=import->next_sibling;
    }
   }
   if(result==PIGEN_DECLARE_OK) done[id.index]=1;
   else if(result==PIGEN_DECLARE_DUPLICATE || final)
    return fail_location(resolver,item->location,result==PIGEN_DECLARE_DUPLICATE ?
     "package export conflicts with a declaration or import" : "package export requires a matching imported declaration");
  }
  id=item->next_sibling;
 }
 return 1;
}

typedef struct {
 pigen_scope_id *scopes;
 unsigned char *opaque_wildcard;
} retained_package_bindings;

/* Find a native channel prefix within a retained primary. Remaining DATA
 * members/indices stay ordinary SV, with the channel's exact packed type. */
static int retained_native_prefix(resolver *resolver,pigen_scope_id scope,pigen_syntax_expr_id id,
 pigen_signal_id *signal,pigen_syntax_expr_id *projection,int *field,size_t depth)
{
 if(depth>128) return 0;
 const pigen_syntax_expr *expr=pigen_syntax_expr_get(&resolver->syntax->expressions,id);
 if(!expr) return 0;
 if(expr->kind==PIGEN_SYNTAX_EXPR_NAME)
 {
  pigen_protocol_endpoint_id endpoint=pigen_resolve_protocol_reference(resolver->syntax,resolver->model,scope,id,resolver->policy);
  const pigen_semantic_protocol_endpoint *native=pigen_protocol_endpoint_get(resolver->model,endpoint);
  const pigen_protocol *protocol=native ? pigen_protocol_type_get(resolver->model,native->type) : NULL;
  if(!protocol || protocol->family!=PIGEN_PROTOCOL_AXI4_STREAM) return 0;
  *signal=native->channels[PIGEN_CHANNEL_BEAT];*projection=id;*field=0;return 1;
 }
 if(expr->kind==PIGEN_SYNTAX_EXPR_INDEX)
  return retained_native_prefix(resolver,scope,expr->as.index.base,signal,projection,field,depth+1);
 if(expr->kind==PIGEN_SYNTAX_EXPR_SELECT)
  return retained_native_prefix(resolver,scope,expr->as.select.base,signal,projection,field,depth+1);
 if(expr->kind!=PIGEN_SYNTAX_EXPR_MEMBER || expr->as.member.scoped) return 0;
 pigen_protocol_endpoint_id endpoint=pigen_resolve_protocol_reference(resolver->syntax,resolver->model,
  scope,expr->as.member.base,resolver->policy);
 const pigen_semantic_protocol_endpoint *native=pigen_protocol_endpoint_get(resolver->model,endpoint);
 if(native)
 {
  const pigen_protocol *protocol=pigen_protocol_type_get(resolver->model,native->type);
  size_t n=0;const char *name=pigen_source_span_text(resolver->model->sources,
   declaration_name_span(resolver,expr->as.member.member),&n);
  for(pigen_protocol_channel channel=0;channel<PIGEN_CHANNEL_COUNT;channel++)
  {
   const char *word=pigen_protocol_channel_name(channel);
   if(name && strlen(word)==n && !memcmp(name,word,n) && native->channels[channel].index!=PIGEN_INVALID_ID)
   {*signal=native->channels[channel];*projection=id;*field=0;return 1;}
  }
  if(protocol && protocol->family==PIGEN_PROTOCOL_AXI4_STREAM)
  {
   *signal=native->channels[PIGEN_CHANNEL_BEAT];*projection=id;*field=1;return 1;
  }
 }
 if(!retained_native_prefix(resolver,scope,expr->as.member.base,signal,projection,field,depth+1)) return 0;
 if(!*field) {*projection=id;*field=1;}
 return 1;
}

static int retained_token_is(resolver *resolver,size_t at,const char *word)
{
 const pigen_expanded_token *token=pigen_expanded_token_get(resolver->syntax->expanded,(pigen_token_id){(uint32_t)at});
 size_t n=0;const char *text=pigen_expanded_token_text(resolver->syntax->expanded,token,&n);
 return text && strlen(word)==n && !memcmp(text,word,n);
}

static int bind_retained_observation(resolver *resolver,pigen_module_id module,pigen_scope_id scope,pigen_scope_id event_scope,
 const pigen_syntax_node *item,const pigen_syntax_retained_name *use,pigen_syntax_location location)
{
 pigen_signal_id signal=INVALID_ID(pigen_signal_id);pigen_syntax_expr_id projection=INVALID_ID(pigen_syntax_expr_id);int field=0;
 if(!retained_native_prefix(resolver,scope,use->primary,&signal,&projection,&field,0))
 {
  pigen_symbol_id root=pigen_symbol_lookup(resolver->model,scope,token_spelling(resolver,use->name));
  if(pigen_symbol_protocol_endpoint(resolver->model,root).index!=PIGEN_INVALID_ID ||
   pigen_symbol_protocol_array(resolver->model,root).index!=PIGEN_INVALID_ID)
   return fail_location(resolver,location,"retained native AXI observation requires a valid channel or constant endpoint selection");
  return 1;
 }
 if(use->written) return fail_location(resolver,location,"retained native AXI payload writes require a structured transfer");
 pigen_semantic_model *model=resolver->model;
 size_t first=item->location.extent.first.index;
 if((retained_token_is(resolver,first,"always") || retained_token_is(resolver,first,"always_ff")) &&
  retained_token_is(resolver,first+1,"@") && retained_token_is(resolver,first+2,"(") &&
  (retained_token_is(resolver,first+3,"posedge") || retained_token_is(resolver,first+3,"negedge") ||
   retained_token_is(resolver,first+3,"edge")))
 {
  if(!retained_token_is(resolver,first+3,"posedge") || !retained_token_is(resolver,first+5,")"))
   return fail_location(resolver,item->location,"AXI process requires supported statements on its bound rising-edge clock");
  const pigen_semantic_signal *channel=pigen_signal_get(model,signal);
  const pigen_semantic_clock_domain *domain=channel ? pigen_clock_domain_get(model,channel->domain) : NULL;
  pigen_symbol_id clock=pigen_symbol_lookup(model,event_scope,token_spelling(resolver,(pigen_token_id){(uint32_t)(first+4)}));
  if(!domain || domain->clock_symbol.index!=clock.index)
   return fail_location(resolver,item->location,"signal used across clock domains");
 }
 const pigen_syntax_expr *written=pigen_syntax_expr_get(&resolver->syntax->expressions,projection);
 size_t after=written->location.extent.after.index;
 pigen_expr_id value=pigen_resolve_expression(resolver->syntax,model,scope,projection,resolver->policy,resolver->error);
 if(value.index==PIGEN_INVALID_ID) return fail_location(resolver,location,"invalid retained native AXI payload observation");
 value=pigen_expr_add_peek(model,value,written->location.source_span);
 if(value.index==PIGEN_INVALID_ID) return 0;
 for(size_t i=0;i<model->retained_observation_count;i++)
 {
  const pigen_semantic_retained_observation *known=&model->retained_observations[i];
  if(known->module.index==module.index && known->first==use->name.index)
   return known->signal.index==signal.index && known->after==after;
 }
 model->retained_observations=pigen_resize(model->retained_observations,
  (model->retained_observation_count+1)*sizeof(*model->retained_observations));
 model->retained_observations[model->retained_observation_count++]=
  (pigen_semantic_retained_observation){module,signal,use->name.index,after,value};
 return 1;
}

/* Replay syntax-owned lexical facts into namespace-only scopes. Retained
 * declarations have no DATA or hardware objects; imports use the common API. */
static int bind_retained_package_uses(resolver *resolver,pigen_scope_id package,const pigen_syntax_node *routine,
 retained_package_bindings *bindings,pigen_module_id module)
{
 const pigen_syntax_tree *syntax=resolver->syntax;
 size_t first=routine->as.retained_subroutine.first_scope,count=routine->as.retained_subroutine.scope_count;
 pigen_scope_id *scopes=bindings->scopes;unsigned char *opaque_wildcard=bindings->opaque_wildcard;int ok=1;
 for(size_t i=0;i<count;i++)
 {
  const pigen_syntax_retained_scope *lexical=&syntax->retained_scopes[first+i];
  pigen_scope_id parent=lexical->parent==SIZE_MAX ? package : scopes[lexical->parent];
  scopes[first+i]=pigen_scope_add(resolver->model,parent,lexical->location.source_span);
 }
 for(size_t i=0;ok && i<routine->as.retained_subroutine.name_count;i++)
 {
  const pigen_syntax_retained_name *use=&syntax->retained_names[routine->as.retained_subroutine.first_name+i];
  if(use->kind==PIGEN_RETAINED_CALL || use->kind==PIGEN_RETAINED_BARE_CALL || use->kind==PIGEN_RETAINED_ARGUMENT) continue;
  pigen_scope_id scope=scopes[use->scope];
  pigen_syntax_location location=pigen_syntax_location_from_extent(syntax->expanded,use->name.index,use->name.index+1);
  if(use->kind==PIGEN_RETAINED_DECLARATION)
  {
   pigen_source_span name=declaration_name_span(resolver,use->name);
   const pigen_symbol *prior=pigen_symbol_get(resolver->model,pigen_symbol_lookup_local(resolver->model,scope,name));
   /* Old-style ports can repeat their direction/type declaration. The backend
    * checks the retained declaration's full SV legality. */
   if(prior && prior->kind==PIGEN_SYMBOL_RETAINED_LOCAL) continue;
   if(pigen_symbol_declare(resolver->model,scope,PIGEN_SYMBOL_RETAINED_LOCAL,INVALID_ID(pigen_data_type_id),
    name,location.source_span,NULL,NULL)!=PIGEN_DECLARE_OK)
    ok=fail_location(resolver,location,"retained declaration conflicts with a local import");
  }
  else if(use->kind==PIGEN_RETAINED_IMPORT)
  {
   pigen_symbol_id source=pigen_symbol_lookup(resolver->model,scope,token_spelling(resolver,use->package));
   /* An opaque package stays the backend's responsibility. Its import must
    * not accidentally activate an unrelated enclosing package candidate. */
   if(source.index==PIGEN_INVALID_ID)
   {
    if(use->wildcard) opaque_wildcard[use->scope]=1;
    else
     (void)pigen_symbol_declare(resolver->model,scope,PIGEN_SYMBOL_RETAINED_LOCAL,INVALID_ID(pigen_data_type_id),
      declaration_name_span(resolver,use->name),location.source_span,NULL,NULL);
    continue;
   }
   const pigen_symbol *target=pigen_symbol_get(resolver->model,source);
   if(!use->wildcard && target && target->kind==PIGEN_SYMBOL_PACKAGE)
   {
    target=pigen_symbol_get(resolver->model,pigen_symbol_lookup_local(resolver->model,target->object.scope,token_spelling(resolver,use->name)));
    if(target && target->kind==PIGEN_SYMBOL_PROTOCOL_TYPE)
    {ok=fail_location(resolver,location,"retained package subroutine requires ordinary SystemVerilog constructs");continue;}
   }
   pigen_declare_result result=pigen_import_package(resolver->model,scope,source,token_spelling(resolver,use->name),
    use->wildcard,use->name.index,location.source_span);
   if(result!=PIGEN_DECLARE_OK) ok=fail_location(resolver,location,"retained subroutine import requires a nonconflicting exported declaration");
  }
  else
  {
   size_t lexical=use->scope;
   while(lexical!=SIZE_MAX && !opaque_wildcard[lexical]) lexical=syntax->retained_scopes[lexical].parent;
   if(lexical!=SIZE_MAX) continue;
   const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,use->name);
   size_t n=0;const char *name=pigen_expanded_token_text(syntax->expanded,token,&n);
   pigen_symbol_id symbol=pigen_symbol_resolve_text_at(resolver->model,scope,name,n,use->name.index);
   const pigen_symbol *target=pigen_symbol_get(resolver->model,symbol);
   if(target && target->kind==PIGEN_SYMBOL_PROTOCOL_TYPE && module.index==PIGEN_INVALID_ID)
    ok=fail_location(resolver,location,"retained package subroutine requires ordinary SystemVerilog constructs");
   else if(symbol.index==PIGEN_INVALID_ID && pigen_symbol_import_ambiguous(resolver->model,scope,
    token_spelling(resolver,use->name),use->name.index))
    ok=fail_location(resolver,location,"ambiguous wildcard package import in retained subroutine");
   else if(module.index!=PIGEN_INVALID_ID)
    ok=bind_retained_observation(resolver,module,scope,package,routine,use,location);
  }
 }
 return ok;
}

/* Follow target value flow before backend folding can erase casts/operators.
 * Coordinates and conditional selectors remain readonly dependencies. */
static int retained_actual_writes_reference(const pigen_syntax_tree *syntax,pigen_syntax_expr_id id,size_t token,size_t depth)
{
 if(depth>128) return 1;
 const pigen_syntax_expr *expr=pigen_syntax_expr_get(&syntax->expressions,id);
 if(!expr) return 1;
 switch(expr->kind)
 {
 case PIGEN_SYNTAX_EXPR_NAME:return expr->location.extent.first.index==token;
 case PIGEN_SYNTAX_EXPR_GROUP:return retained_actual_writes_reference(syntax,expr->as.group.operand,token,depth+1);
 case PIGEN_SYNTAX_EXPR_MEMBER:return retained_actual_writes_reference(syntax,expr->as.member.base,token,depth+1);
 case PIGEN_SYNTAX_EXPR_INDEX:return retained_actual_writes_reference(syntax,expr->as.index.base,token,depth+1);
 case PIGEN_SYNTAX_EXPR_SELECT:return retained_actual_writes_reference(syntax,expr->as.select.base,token,depth+1);
 case PIGEN_SYNTAX_EXPR_CAST:return retained_actual_writes_reference(syntax,expr->as.cast.value,token,depth+1);
 case PIGEN_SYNTAX_EXPR_UNARY:return retained_actual_writes_reference(syntax,expr->as.unary.operand,token,depth+1);
 case PIGEN_SYNTAX_EXPR_BINARY:return retained_actual_writes_reference(syntax,expr->as.binary.left,token,depth+1) ||
  retained_actual_writes_reference(syntax,expr->as.binary.right,token,depth+1);
 case PIGEN_SYNTAX_EXPR_CONDITIONAL:return retained_actual_writes_reference(syntax,expr->as.conditional.when_true,token,depth+1) ||
  retained_actual_writes_reference(syntax,expr->as.conditional.when_false,token,depth+1);
 case PIGEN_SYNTAX_EXPR_CALL:
  for(size_t i=0;i<expr->as.call.argument_count;i++)
   if(retained_actual_writes_reference(syntax,syntax->expressions.children[expr->as.call.first_argument+i],token,depth+1)) return 1;
  return 0;
 case PIGEN_SYNTAX_EXPR_CONCATENATION:
  for(size_t i=0;i<expr->as.sequence.child_count;i++)
   if(retained_actual_writes_reference(syntax,syntax->expressions.children[expr->as.sequence.first_child+i],token,depth+1)) return 1;
  return 0;
 default:return 0;
 }
}

static const pigen_syntax_expr *retained_callee_expression(resolver *resolver,pigen_syntax_expr_id id)
{
 const pigen_syntax_expr *expr=pigen_syntax_expr_get(&resolver->syntax->expressions,id);
 for(size_t depth=0;expr && expr->kind==PIGEN_SYNTAX_EXPR_GROUP && depth<128;depth++)
  expr=pigen_syntax_expr_get(&resolver->syntax->expressions,expr->as.group.operand);
 return expr;
}

static int retained_readonly_system_call(resolver *resolver,const pigen_syntax_expr *callee)
{
 static const char *const names[]={"$signed","$unsigned","$bits","$size","$left","$right","$low","$high","$increment",
  "$dimensions","$unpacked_dimensions","$clog2","$isunknown","$countbits","$countones","$onehot","$onehot0",
  "$itor","$rtoi","$realtobits","$bitstoreal","$shortrealtobits","$bitstoshortreal","$ceil","$floor","$ln","$log10",
  "$exp","$sqrt","$pow","$sin","$cos","$tan","$asin","$acos","$atan","$atan2","$sinh","$cosh","$tanh",
  "$asinh","$acosh","$atanh","$hypot","$rose","$fell","$stable","$changed","$past","$sampled",
  "$display","$write","$strobe","$monitor","$fdisplay","$fwrite","$fstrobe","$fmonitor","$fatal","$error","$warning","$info"};
 if(!callee || callee->kind!=PIGEN_SYNTAX_EXPR_NAME) return 0;
 for(size_t i=0;i<sizeof names/sizeof *names;i++) if(retained_token_is(resolver,callee->as.atom.token.index,names[i])) return 1;
 return 0;
}

static const pigen_syntax_node *retained_scope_routine(resolver *resolver,const pigen_syntax_node *scope,pigen_token_id name)
{
 if(!scope) return NULL;
 for(pigen_syntax_id id=scope->first_child;id.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *node=pigen_syntax_get(resolver->syntax,id);
  if(node->kind==PIGEN_SYNTAX_RETAINED && node->as.retained_subroutine.has_name &&
   spelling_text_equal(resolver,declaration_name_span(resolver,node->as.retained_subroutine.name),declaration_name_span(resolver,name))) return node;
  id=node->next_sibling;
 }
 return NULL;
}

static int retained_inner_callee_barrier(resolver *resolver,pigen_scope_id scope,pigen_scope_id module,
 const pigen_syntax_expr *callee,const pigen_syntax_node *item)
{
 pigen_source_span name=declaration_name_span(resolver,callee->as.atom.token);
 while(scope.index!=module.index && pigen_scope_get(resolver->model,scope))
 {
  const pigen_symbol *local=pigen_symbol_get(resolver->model,pigen_symbol_lookup_local(resolver->model,scope,name));
  if(local)
  {
   int result_name=0;
   if(item->as.retained_subroutine.has_name)
   {
    pigen_token_id token=item->as.retained_subroutine.name;
    pigen_syntax_location location=pigen_syntax_location_from_extent(resolver->syntax->expanded,token.index,token.index+1);
    result_name=local->kind==PIGEN_SYMBOL_RETAINED_LOCAL && local->declaration.source.index==location.source_span.source.index &&
     local->declaration.start==location.source_span.start && local->declaration.end==location.source_span.end;
   }
   if(!result_name) return 1;
  }
  for(size_t i=0;i<resolver->model->import_count;i++)
  {
   const pigen_semantic_import *import=&resolver->model->imports[i];
   if(import->scope.index!=scope.index || (import->wildcard==1 && import->at>callee->location.extent.first.index)) continue;
   const pigen_symbol *target=pigen_symbol_get(resolver->model,import->wildcard==1 ?
    pigen_symbol_lookup_local(resolver->model,import->package_scope,name) : import->target);
   if(target && spelling_text_equal(resolver,target->name,name)) return 1;
  }
  scope=resolver->model->scopes[scope.index].parent;
 }
 return 0;
}

static int retained_signature_matches_symbol(resolver *resolver,const pigen_syntax_node *routine,const pigen_symbol *symbol)
{
 if(!routine || !symbol || routine->kind!=PIGEN_SYNTAX_RETAINED || !routine->as.retained_subroutine.has_name ||
  symbol->kind!=PIGEN_SYMBOL_RETAINED_SUBROUTINE ||
  routine->location.source_span.source.index!=symbol->declaration.source.index ||
  routine->location.source_span.start!=symbol->declaration.start || routine->location.source_span.end!=symbol->declaration.end ||
  !spelling_text_equal(resolver,symbol->name,declaration_name_span(resolver,routine->as.retained_subroutine.name))) return 0;
 const pigen_syntax_node *owner=pigen_syntax_get(resolver->syntax,routine->parent);
 if(!owner) return 0;
 if(owner->kind==PIGEN_SYNTAX_PACKAGE)
  for(size_t i=0;i<resolver->model->symbol_count;i++)
  {
   const pigen_symbol *package=&resolver->model->symbols[i];
   if(package->kind==PIGEN_SYMBOL_PACKAGE && package->object.scope.index==symbol->scope.index)
    return spelling_text_equal(resolver,package->name,declaration_name_span(resolver,owner->as.module.name));
  }
 return 0;
}

static const pigen_syntax_node *retained_call_signature(resolver *resolver,pigen_module_id module,pigen_scope_id scope,
 const pigen_syntax_node *item,const pigen_syntax_retained_name *actual)
{
 const pigen_syntax_expr *callee=retained_callee_expression(resolver,actual->primary);
 if(!callee) return NULL;
 if(callee->kind==PIGEN_SYNTAX_EXPR_MEMBER && callee->as.member.scoped)
 {
  const pigen_syntax_expr *base=retained_callee_expression(resolver,callee->as.member.base);
  if(base && base->kind==PIGEN_SYNTAX_EXPR_NAME && retained_token_is(resolver,base->as.atom.token.index,"$unit"))
   return retained_scope_routine(resolver,pigen_syntax_get(resolver->syntax,(pigen_syntax_id){0}),callee->as.member.member);
 }
 const pigen_semantic_module *module_owner=pigen_module_get(resolver->model,module);
 if(callee->kind==PIGEN_SYNTAX_EXPR_NAME && module_owner &&
  !retained_inner_callee_barrier(resolver,scope,module_owner->scope,callee,item))
 {
  const pigen_syntax_node *routine=retained_scope_routine(resolver,
   pigen_syntax_get(resolver->syntax,module_owner->syntax),callee->as.atom.token);
  if(routine) return routine;
 }
 pigen_symbol_id target=INVALID_ID(pigen_symbol_id);
 if(callee->kind==PIGEN_SYNTAX_EXPR_NAME)
 {
  const pigen_expanded_token *token=pigen_expanded_token_get(resolver->syntax->expanded,callee->as.atom.token);
  size_t n=0;const char *name=pigen_expanded_token_text(resolver->syntax->expanded,token,&n);int ambiguous=0;
  target=pigen_symbol_resolve_call_text_at(resolver->model,scope,name,n,callee->location.extent.first.index,&ambiguous);
  if(ambiguous) return NULL;
 }
 else target=pigen_lookup_callee_symbol(resolver->syntax,resolver->model,scope,actual->primary);
 const pigen_symbol *symbol=pigen_symbol_get(resolver->model,target);
 if(symbol)
 {
  if(symbol->kind!=PIGEN_SYMBOL_RETAINED_SUBROUTINE) return NULL;
  for(size_t i=0;i<resolver->syntax->node_count;i++)
  {
   const pigen_syntax_node *routine=&resolver->syntax->nodes[i];
   if(retained_signature_matches_symbol(resolver,routine,symbol)) return routine;
  }
  return NULL;
 }
 if(callee->kind==PIGEN_SYNTAX_EXPR_NAME)
 {
  for(pigen_syntax_id parent=item->parent;parent.index!=PIGEN_INVALID_ID;)
  {
   const pigen_syntax_node *owner=pigen_syntax_get(resolver->syntax,parent);
   const pigen_syntax_node *routine=retained_scope_routine(resolver,owner,callee->as.atom.token);
   if(routine) return routine;
   parent=owner->parent;
  }
 }
 /* A scalar child's retained routine is owned by its definition, while the
  * actual's value namespace remains the caller's. */
 if(callee->kind==PIGEN_SYNTAX_EXPR_MEMBER && !callee->as.member.scoped)
 {
  const pigen_syntax_expr *base=retained_callee_expression(resolver,callee->as.member.base);
  if(base && base->kind==PIGEN_SYNTAX_EXPR_NAME)
  {
   const pigen_symbol *owner=pigen_symbol_get(resolver->model,pigen_symbol_lookup(resolver->model,scope,token_spelling(resolver,base->as.atom.token)));
   const pigen_semantic_instance *instance=owner && owner->kind==PIGEN_SYMBOL_INSTANCE ? pigen_instance_get(resolver->model,owner->object.instance) : NULL;
   const pigen_semantic_module *child=instance ? pigen_module_get(resolver->model,instance->child_module) : NULL;
   if(child) return retained_scope_routine(resolver,pigen_syntax_get(resolver->syntax,child->syntax),callee->as.member.member);
  }
 }
 return NULL;
}

static int check_retained_argument_effects(resolver *resolver,const pigen_syntax_node *item,
 const retained_package_bindings *bindings,size_t first_observation)
{
 for(size_t i=first_observation;i<resolver->model->retained_observation_count;i++)
 {
  pigen_semantic_retained_observation observation=resolver->model->retained_observations[i];
  for(size_t j=0;j<item->as.retained_subroutine.name_count;j++)
  {
   const pigen_syntax_retained_name *actual=&resolver->syntax->retained_names[item->as.retained_subroutine.first_name+j];
   if(actual->kind!=PIGEN_RETAINED_ARGUMENT || actual->first>observation.first || actual->after<observation.after ||
    !retained_actual_writes_reference(resolver->syntax,actual->argument_value,observation.first,0)) continue;
   const pigen_syntax_expr *callee=retained_callee_expression(resolver,actual->primary);
   if(retained_readonly_system_call(resolver,callee)) continue;
   const pigen_syntax_node *routine=retained_call_signature(resolver,observation.module,bindings->scopes[actual->scope],item,actual);
   pigen_syntax_location location=pigen_syntax_location_from_extent(resolver->syntax->expanded,observation.first,observation.after);
   if(!routine || !routine->as.retained_subroutine.signature_known)
    return fail_location(resolver,location,"native AXI arguments require a known retained subroutine signature");
   const pigen_syntax_retained_name *formal=NULL;
   for(size_t k=0;k<routine->as.retained_subroutine.name_count;k++)
   {
    const pigen_syntax_retained_name *candidate=&resolver->syntax->retained_names[routine->as.retained_subroutine.first_name+k];
    if(candidate->kind!=PIGEN_RETAINED_DECLARATION || !candidate->formal_index) continue;
    int matches=actual->argument_name.index==PIGEN_INVALID_ID ? candidate->formal_index==actual->argument_index+1 :
     spelling_text_equal(resolver,declaration_name_span(resolver,candidate->name),declaration_name_span(resolver,actual->argument_name));
    if(matches) {formal=candidate;break;}
   }
   if(!formal) return fail_location(resolver,location,"native AXI actual requires a matching retained formal argument");
   if(formal->formal_mode!=1)
    return fail_location(resolver,location,"native AXI writable subroutine arguments require a structured transfer");
   const pigen_syntax_node *owner=pigen_syntax_get(resolver->syntax,routine->parent);
   if(callee && callee->kind==PIGEN_SYNTAX_EXPR_NAME && owner && owner->kind==PIGEN_SYNTAX_PACKAGE)
   {
    size_t k=0;
    for(;k<resolver->model->symbol_count;k++)
    {
     const pigen_symbol *target=&resolver->model->symbols[k];
     if(retained_signature_matches_symbol(resolver,routine,target)) break;
    }
    if(k==resolver->model->symbol_count || !pigen_retained_call_bind(resolver->model,callee->as.atom.token,(pigen_symbol_id){(uint32_t)k},0))
     return fail_location(resolver,location,"cannot bind retained native argument callee");
   }
  }
 }
 return 1;
}

static int bind_retained_module_uses(resolver *resolver,pigen_module_id module,
 pigen_scope_id scope,const pigen_syntax_node *node)
{
 if(!node->as.retained_subroutine.scope_count) return 1;
 size_t count=resolver->syntax->retained_scope_count;
 retained_package_bindings bindings={pigen_resize(NULL,(count+1)*sizeof(pigen_scope_id)),pigen_resize(NULL,count+1)};
 memset(bindings.opaque_wildcard,0,count+1);
 size_t first=resolver->model->retained_observation_count;
 int ok=bind_retained_package_uses(resolver,scope,node,&bindings,module);
 if(ok) ok=check_retained_argument_effects(resolver,node,&bindings,first);
 free(bindings.scopes);free(bindings.opaque_wildcard);return ok;
}

static int bind_retained_package_calls(resolver *resolver,const pigen_syntax_node *routine,
 const retained_package_bindings *bindings)
{
 const pigen_syntax_tree *syntax=resolver->syntax;
 for(size_t i=0;i<routine->as.retained_subroutine.name_count;i++)
 {
  size_t index=routine->as.retained_subroutine.first_name+i;
  const pigen_syntax_retained_name *use=&syntax->retained_names[index];
  if(use->kind!=PIGEN_RETAINED_CALL && use->kind!=PIGEN_RETAINED_BARE_CALL) continue;
  size_t lexical=use->scope;
  while(lexical!=SIZE_MAX && !bindings->opaque_wildcard[lexical]) lexical=syntax->retained_scopes[lexical].parent;
  if(lexical!=SIZE_MAX) continue;
  pigen_scope_id scope=bindings->scopes[use->scope];
  pigen_syntax_location location=pigen_syntax_location_from_extent(syntax->expanded,use->name.index,use->name.index+1);
  const pigen_expanded_token *token=pigen_expanded_token_get(syntax->expanded,use->name);
  size_t n=0;const char *name=pigen_expanded_token_text(syntax->expanded,token,&n);
  int ambiguous=0;
  pigen_symbol_id symbol=pigen_symbol_resolve_call_text_at(resolver->model,scope,name,n,use->name.index,&ambiguous);
  const pigen_symbol *target=pigen_symbol_get(resolver->model,symbol);
  if(ambiguous) return fail_location(resolver,location,"ambiguous wildcard package import in retained subroutine call");
  if(target && target->kind==PIGEN_SYMBOL_PROTOCOL_TYPE)
   return fail_location(resolver,location,"retained package subroutine requires ordinary SystemVerilog constructs");
  if(target && target->kind==PIGEN_SYMBOL_RETAINED_SUBROUTINE &&
   !pigen_retained_call_bind(resolver->model,use->name,symbol,use->kind==PIGEN_RETAINED_BARE_CALL))
   return fail_location(resolver,location,"invalid retained subroutine call binding");
 }
 return 1;
}

static int add_package(resolver *resolver,const pigen_syntax_node *node)
{
 pigen_semantic_model *model=resolver->model;pigen_symbol_id symbol;
 pigen_scope_id parent=model->compilation_scope;
 if(pigen_symbol_declare(model,parent,PIGEN_SYMBOL_PACKAGE,INVALID_ID(pigen_data_type_id),
  token_spelling(resolver,node->as.module.name),node->location.source_span,&symbol,NULL)!=PIGEN_DECLARE_OK)
  return fail_location(resolver,node->location,"duplicate or invalid package name");
 pigen_scope_id scope=pigen_scope_add(model,parent,node->location.source_span);
 if(!pigen_symbol_bind_package_scope(model,symbol,scope)) return fail_location(resolver,node->location,"invalid package scope");
 pigen_scope_id saved=resolver->expression_scope;resolver->expression_scope=scope;
 unsigned char *exported=pigen_resize(NULL,resolver->syntax->node_count);memset(exported,0,resolver->syntax->node_count);
 retained_package_bindings bindings={
  pigen_resize(NULL,(resolver->syntax->retained_scope_count+1)*sizeof(pigen_scope_id)),
  pigen_resize(NULL,resolver->syntax->retained_scope_count+1)};
 memset(bindings.opaque_wildcard,0,resolver->syntax->retained_scope_count);
 int ok=1;
 for(pigen_syntax_id child=node->first_child;ok && child.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
  if(item->kind==PIGEN_SYNTAX_IMPORT) ok=add_import(resolver,scope,item);
  else if(item->kind==PIGEN_SYNTAX_RETAINED && item->as.retained_subroutine.has_name)
  {
   ok=pigen_symbol_declare(model,scope,PIGEN_SYMBOL_RETAINED_SUBROUTINE,INVALID_ID(pigen_data_type_id),
    declaration_name_span(resolver,item->as.retained_subroutine.name),item->location.source_span,NULL,NULL)==PIGEN_DECLARE_OK ||
    fail_location(resolver,item->location,"duplicate or invalid package subroutine name");
   if(ok) ok=bind_retained_package_uses(resolver,scope,item,&bindings,INVALID_ID(pigen_module_id));
  }
  else if(item->kind==PIGEN_SYNTAX_TYPEDEF) ok=add_typedef(resolver,scope,item);
  else if(item->kind==PIGEN_SYNTAX_PARAMETER) ok=add_parameter(resolver,INVALID_ID(pigen_module_id),child,item);
  if(ok) ok=resolve_package_exports(resolver,scope,node,exported,item->location.extent.first.index,0);
  child=item->next_sibling;
 }
 for(pigen_syntax_id child=node->first_child;ok && child.index!=PIGEN_INVALID_ID;)
 {
  const pigen_syntax_node *item=pigen_syntax_get(resolver->syntax,child);
  if(item->kind==PIGEN_SYNTAX_RETAINED && item->as.retained_subroutine.has_name)
   ok=bind_retained_package_calls(resolver,item,&bindings);
  child=item->next_sibling;
 }
 if(ok) ok=resolve_package_exports(resolver,scope,node,exported,SIZE_MAX,1);
 free(bindings.scopes);free(bindings.opaque_wildcard);
 free(exported);resolver->expression_scope=saved;return ok;
}

int pigen_resolve_semantics(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	const pigen_source_manager *sources = syntax && syntax->expanded ?
		syntax->expanded->sources : NULL;
	resolver resolver = {
		.syntax = syntax, .model = model, .policy = policy, .error = error,
		.expression_scope = (pigen_scope_id){PIGEN_INVALID_ID},
		.fsm_state_expression = INVALID_ID(pigen_expr_id),
		.fsm_state_type = INVALID_ID(pigen_data_type_id),
		.fsm_terminated = INVALID_ID(pigen_expr_id)};
	const pigen_syntax_node *root;
	pigen_syntax_id child;
	pigen_source_span root_span;
	int ok=0;

	pigen_semantic_init(model, sources);
	if (error) *error = (pigen_semantic_error){INVALID_ID(pigen_origin_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0}, NULL};
	if (!policy || !policy->maximum_generated_bits) goto done;
	root = pigen_syntax_get(syntax, (pigen_syntax_id){0});
	if (!root || root->kind != PIGEN_SYNTAX_COMPILATION_UNIT)
		goto done;
	root_span = root->location.source_span;
	model->compilation_scope = pigen_scope_add(model,
		INVALID_ID(pigen_scope_id), root_span);
	if (model->compilation_scope.index == PIGEN_INVALID_ID)
	{
		fail(&resolver,root_span,"cannot create compilation-unit scope");goto done;
	}
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		if (node->kind == PIGEN_SYNTAX_TYPEDEF &&
			!add_typedef(&resolver, model->compilation_scope, node)) goto done;
		if(node->kind==PIGEN_SYNTAX_PACKAGE && !add_package(&resolver,node)) goto done;
		if(node->kind==PIGEN_SYNTAX_IMPORT && !add_import(&resolver,model->compilation_scope,node)) goto done;
		if (node->kind == PIGEN_SYNTAX_MODULE &&
			!add_module(&resolver, node, child, 1)) goto done;
		child = node->next_sibling;
	}
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		if (node->kind == PIGEN_SYNTAX_MODULE &&
			!add_module(&resolver, node, child, 0)) goto done;
		child = node->next_sibling;
	}
	for(size_t i=0;i<model->module_count;i++)
		if(model->modules[i].specialization_of.index!=PIGEN_INVALID_ID)
		{
			pigen_syntax_id template=model->modules[i].syntax;
			if(!resolve_module_body(&resolver,(pigen_module_id){(uint32_t)i},
				pigen_syntax_get(syntax,template))) goto done;
		}
	for(size_t r=0;r<resolver.reset_binding_count;r++)
		if(!resolver.reset_bindings[r].consumed)
		{
			fail(&resolver,resolver.reset_bindings[r].span,"pipeline_reset requires a pipeline in the same process");
			goto done;
		}
	ok=validate_module_hierarchy(&resolver) && validate_signal_ownership(&resolver);
 done:
	free(resolver.reset_bindings);free(resolver.pipelines);free(resolver.foreach_declarations);
	return ok;
}
