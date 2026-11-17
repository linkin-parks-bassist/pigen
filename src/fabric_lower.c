#include <stdio.h>
#include "pigen/fabric_lower.h"
#include <stdlib.h>
#include <string.h>
#include "pigen/util.h"

static int token_text(const pigen_syntax_tree *syntax, pigen_token_id token,
	const char **text, size_t *length)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(
		syntax->expanded, token);
	pigen_source_span span;

	if (!known) return 0;
	span = pigen_origin_spelling_span(syntax->expanded, known->origin);
	*text = pigen_source_span_text(syntax->expanded->sources, span, length);
	return *text != NULL;
}

static const pigen_semantic_instance *find_instance(
	pigen_semantic_model *model, pigen_module_id module,
	const pigen_syntax_tree *syntax, pigen_token_id name)
{
	const char *text;
	size_t length;
	size_t i;

	if (!token_text(syntax, name, &text, &length)) return NULL;
	for (i = 0; i < model->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &model->instances[i];
		const pigen_symbol *symbol;

		if (instance->module.index != module.index) continue;
		symbol = pigen_symbol_get(model, instance->symbol);
		if (!symbol) continue;
		{
			const char *stext;
			size_t slength;
			pigen_source_span span = symbol->name;
			stext = pigen_source_span_text(model->sources, span, &slength);
			if (stext && slength == length && !memcmp(stext, text, length))
				return instance;
		}
	}
	return NULL;
}

static const pigen_semantic_instance_connection *find_port(
	pigen_semantic_model *model, const pigen_semantic_instance *instance,
	const pigen_syntax_tree *syntax, pigen_token_id port, const char *suffix)
{
	const char *text;
	size_t length;
	size_t i;

	if (!token_text(syntax, port, &text, &length)) return NULL;
	for (i = 0; i < instance->connection_count; i++)
	{
		const pigen_semantic_instance_connection *connection =
			&model->instance_connections[instance->first_connection + i];
		const pigen_symbol *symbol = pigen_symbol_get(model,
			connection->port_symbol);
		const char *ptext = NULL;
		size_t plength = 0;

		if (connection->port_name.source.index != PIGEN_INVALID_ID)
			ptext = pigen_source_span_text(model->sources,
				connection->port_name, &plength);
		else if (symbol)
			ptext = pigen_source_span_text(model->sources, symbol->name,
				&plength);
		if (!ptext) continue;
		if (suffix)
		{
			size_t slength = strlen(suffix);
			if (plength != length + slength) continue;
			if (memcmp(ptext, text, length) ||
				memcmp(ptext + length, suffix, slength)) continue;
		}
		else if (plength != length || memcmp(ptext, text, length))
			continue;
		return connection;
	}
	return NULL;
}

static pigen_rtl_object_id connection_payload(pigen_rtl_lowering *lowering,
	const pigen_semantic_instance_connection *connection)
{
	const pigen_semantic_expr *value;

	if (!connection || connection->value.index == PIGEN_INVALID_ID)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	if (connection->endpoint_view.index < lowering->lowered_endpoint_count)
		return lowering->lowered_endpoints[connection->endpoint_view.index].payload;
	value = pigen_expr_get(lowering->semantics, connection->value);
	while (value && value->kind == PIGEN_EXPR_CONVERSION)
		value = pigen_expr_get(lowering->semantics,
			value->as.conversion.operand);
	if (!value || value->kind != PIGEN_EXPR_SYMBOL)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	{
		pigen_signal_id signal = pigen_symbol_signal(lowering->semantics,
			value->as.symbol);
		if (signal.index == PIGEN_INVALID_ID ||
			lowering->lowered_endpoint_count <= signal.index)
			return (pigen_rtl_object_id){PIGEN_INVALID_ID};
		return lowering->lowered_endpoints[signal.index].payload;
	}
}

static int connect(pigen_rtl_lowering *lowering, pigen_rtl_module_id module,
	pigen_rtl_object_id destination, pigen_rtl_object_id source,
	pigen_source_span span)
{
	const pigen_rtl_object *dest = pigen_rtl_object_get(lowering->rtl,
		destination);
	const pigen_rtl_object *src = pigen_rtl_object_get(lowering->rtl, source);
	pigen_rtl_expr_id value;

	if (!dest || !src || dest->type.index != src->type.index)
		return 0;
	value = pigen_rtl_expr_add_object(lowering->rtl, dest->type, source,
		span);
	if (value.index == PIGEN_INVALID_ID) return 0;
	return pigen_rtl_equation_add_with_owner(lowering->rtl, module,
		destination, value, span).index != PIGEN_INVALID_ID;
}

static pigen_rtl_object_id endpoint_control(pigen_rtl_lowering *lowering,
	const pigen_semantic_instance_connection *connection, int ready)
{
	pigen_signal_id signal;
	const pigen_semantic_expr *value;
	const pigen_rtl_signal_endpoints *endpoint;
	const pigen_rtl_expr *expr;

	if (!connection) return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	signal = connection->endpoint_view;
	if (signal.index == PIGEN_INVALID_ID)
	{
		value = pigen_expr_get(lowering->semantics, connection->value);
		while (value && value->kind == PIGEN_EXPR_CONVERSION)
			value = pigen_expr_get(lowering->semantics,
				value->as.conversion.operand);
		if (value && value->kind == PIGEN_EXPR_SYMBOL)
			signal = pigen_symbol_signal(lowering->semantics, value->as.symbol);
	}
	if (signal.index >= lowering->lowered_endpoint_count)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	endpoint = &lowering->lowered_endpoints[signal.index];
	expr = pigen_rtl_expr_get(lowering->rtl,
		ready ? endpoint->ready : endpoint->valid);
	return expr && expr->kind == PIGEN_RTL_EXPR_OBJECT ? expr->object :
		(pigen_rtl_object_id){PIGEN_INVALID_ID};
}

static pigen_rtl_object_id connection_control(pigen_rtl_lowering *lowering,
	const pigen_semantic_instance_connection *payload,
	const pigen_semantic_instance_connection *control, int ready)
{
	pigen_rtl_object_id object = connection_payload(lowering, control);
	return object.index != PIGEN_INVALID_ID ? object :
		endpoint_control(lowering, payload, ready);
}

static pigen_signal_id module_input_named(pigen_semantic_model *model,
	pigen_module_id module, const char *name)
{
	size_t i;
	for (i = 0; i < model->signal_count; i++)
	{
		const pigen_semantic_signal *signal = &model->signals[i];
		const pigen_symbol *symbol;
		const char *text;
		size_t length = 0;
		if (signal->module.index != module.index ||
			signal->direction != PIGEN_SEMANTIC_INPUT) continue;
		symbol = pigen_symbol_get(model, signal->symbol);
		if (!symbol) continue;
		text = pigen_source_span_text(model->sources, symbol->name, &length);
		if (text && strlen(name) == length && !memcmp(text, name, length))
			return (pigen_signal_id){(uint32_t)i};
	}
	return (pigen_signal_id){PIGEN_INVALID_ID};
}

static pigen_rtl_expr_id fab_obj_expr(pigen_rtl_lowering *lowering,
	pigen_rtl_object_id object, pigen_source_span span)
{
	const pigen_rtl_object *known = pigen_rtl_object_get(lowering->rtl, object);
	return known ? pigen_rtl_expr_add_object(lowering->rtl, known->type, object,
		span) : (pigen_rtl_expr_id){PIGEN_INVALID_ID};
}

static pigen_rtl_type_id fab_control_type(pigen_rtl_lowering *lowering)
{
	pigen_data_type_id logic_one = pigen_data_type_sized_logic(
		lowering->semantics, 1, PIGEN_SIGN_UNSIGNED);
	return pigen_lower_rtl_type(lowering, logic_one);
}

static pigen_rtl_expr_id fab_not(pigen_rtl_lowering *lowering,
	pigen_rtl_expr_id operand, pigen_rtl_type_id control, pigen_source_span span)
{
	pigen_unary_resolution resolution;
	pigen_data_type_id one = pigen_data_type_sized_logic(lowering->semantics,
		1, PIGEN_SIGN_UNSIGNED);

	resolution = (pigen_unary_resolution){
		{PIGEN_CONVERSION_IDENTITY, one, one},
		{PIGEN_UNARY_LOGICAL_NOT, one, one}};
	return pigen_rtl_expr_add_unary(lowering->rtl, control, &resolution,
		operand, span);
}

static int route_through_skid(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module,
	pigen_rtl_object_id source_payload, pigen_rtl_object_id source_valid,
	pigen_rtl_object_id source_ready, pigen_rtl_object_id dest_payload,
	pigen_rtl_object_id dest_valid, pigen_rtl_object_id dest_ready,
	pigen_source_span span, const pigen_semantic_protocol_endpoint *native)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
	if (native)
	{
		const pigen_semantic_clock_domain *domain = pigen_clock_domain_get(model, native->binding.domain);
		if (!domain) return 0;
		clk = pigen_symbol_signal(model, domain->clock_symbol);
		reset = pigen_symbol_signal(model, native->binding.reset_symbol);
	}
	const pigen_rtl_object *payload;
	pigen_rtl_instance_id instance;
	pigen_rtl_connection connections[13];
	size_t count = 0;
	pigen_rtl_record_range range;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_rtl_expr_id zero;
	char name[64];
	int written;

	if (clk.index == PIGEN_INVALID_ID ||
		lowering->lowered_endpoint_count <= clk.index)
		return 0;
	payload = pigen_rtl_object_get(rtl, source_payload);
	if (!payload) return 0;
	logic_one = pigen_data_type_sized_logic(model, 1, PIGEN_SIGN_UNSIGNED);
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	zero = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
		control_type, &(pigen_rtl_literal_word){0, 0, 0}, 1, 0, span);
	if (control_type.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID)
		return 0;
	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module, module,
		NULL, 0, NULL, 0, span);
	if (instance.index == PIGEN_INVALID_ID) return 0;
#define PORT(p, o) connections[count++] = (pigen_rtl_connection){(p), (o), (pigen_rtl_expr_id){PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL}
#define VAL(p, v) connections[count++] = (pigen_rtl_connection){(p), (pigen_rtl_object_id){PIGEN_INVALID_ID}, (v), PIGEN_SEMANTIC_INTERNAL}
	PORT("clk", lowering->lowered_endpoints[clk.index].payload);
	if (reset.index != PIGEN_INVALID_ID &&
		lowering->lowered_endpoint_count > reset.index)
	{
		pigen_rtl_object_id reset_object = lowering->lowered_endpoints[reset.index].payload;
		if (native && native->binding.reset_active_low)
			VAL("reset", fab_not(lowering, fab_obj_expr(lowering, reset_object, span), control_type, span));
		else PORT("reset", reset_object);
	}
	else
		VAL("reset", zero);
	VAL("clear", zero); VAL("discard", zero); VAL("force_valid", zero);
	VAL("force_invalid", zero); VAL("force_after_transfer", zero);
	PORT("in_valid", source_valid); PORT("in_ready", source_ready);
	PORT("packet_in", source_payload);
	PORT("out_valid", dest_valid); PORT("out_ready", dest_ready);
	PORT("packet_out", dest_payload);
#undef PORT
#undef VAL
	range = pigen_rtl_connection_range_add(rtl, connections, count);
	if (range.first == (size_t)PIGEN_INVALID_ID) return 0;
	if (!pigen_rtl_instance_set_primitive(rtl, instance,
		PIGEN_RTL_PRIMITIVE_SKID, payload->type, range)) return 0;
	written = snprintf(name, sizeof(name), "fabric_route_%u", instance.index);
	if (written > 0 && (size_t)written < sizeof(name))
		pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name, name);
	return 1;
}

static pigen_syntax_id next_fabric_edge(const pigen_syntax_tree *syntax,
	pigen_syntax_id module, pigen_syntax_id edge)
{
	const pigen_syntax_node *owner = pigen_syntax_get(syntax, module);
	pigen_syntax_id item = owner->first_child;
	if (edge.index != PIGEN_INVALID_ID)
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, edge);
		if (node->next_sibling.index != PIGEN_INVALID_ID)
			return node->next_sibling;
		item = pigen_syntax_get(syntax, node->parent)->next_sibling;
	}
	while (item.index != PIGEN_INVALID_ID)
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, item);
		if (node->kind == PIGEN_SYNTAX_FABRIC &&
			node->first_child.index != PIGEN_INVALID_ID) return node->first_child;
		item = node->next_sibling;
	}
	return (pigen_syntax_id){PIGEN_INVALID_ID};
}

/* Quarantined native AXI fabric lowering: not connected to production. */
#if 0
static pigen_rtl_expr_id fab_logic(pigen_rtl_lowering *lowering,
	pigen_binary_operator operator, pigen_rtl_expr_id left,
	pigen_rtl_expr_id right, pigen_rtl_type_id control, pigen_source_span span)
{
	pigen_binary_resolution resolution;
	pigen_data_type_id one = pigen_data_type_sized_logic(lowering->semantics,
		1, PIGEN_SIGN_UNSIGNED);

	resolution = (pigen_binary_resolution){
		{PIGEN_CONVERSION_IDENTITY, one, one},
		{PIGEN_CONVERSION_IDENTITY, one, one},
		{operator, one, one, one}};
	return pigen_rtl_expr_add_binary(lowering->rtl, control, &resolution,
		left, right, span);
}

static int route_through_merge2(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module,
	pigen_rtl_object_id p0, pigen_rtl_object_id v0, pigen_rtl_object_id r0,
	pigen_rtl_object_id p1, pigen_rtl_object_id v1, pigen_rtl_object_id r1,
	pigen_rtl_object_id dp, pigen_rtl_object_id dv, pigen_rtl_object_id dr,
	pigen_source_span span, const pigen_semantic_protocol_endpoint *native)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
	if (native)
	{
		const pigen_semantic_clock_domain *domain = pigen_clock_domain_get(model, native->binding.domain);
		if (!domain) return 0;
		clk = pigen_symbol_signal(model, domain->clock_symbol);
		reset = pigen_symbol_signal(model, native->binding.reset_symbol);
	}
	const pigen_rtl_object *payload;
	pigen_rtl_instance_id instance;
	pigen_rtl_connection connections[11];
	size_t count = 0;
	pigen_rtl_record_range range;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_rtl_expr_id zero;
	char name[64];
	int written;

	if (clk.index == PIGEN_INVALID_ID ||
		lowering->lowered_endpoint_count <= clk.index)
		return 0;
	payload = pigen_rtl_object_get(rtl, dp);
	if (!payload) return 0;
	logic_one = pigen_data_type_sized_logic(model, 1, PIGEN_SIGN_UNSIGNED);
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	zero = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
		control_type, &(pigen_rtl_literal_word){0, 0, 0}, 1, 0, span);
	if (control_type.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID)
		return 0;
	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module, module,
		NULL, 0, NULL, 0, span);
	if (instance.index == PIGEN_INVALID_ID) return 0;
#define PORT(p, o) connections[count++] = (pigen_rtl_connection){(p), (o), (pigen_rtl_expr_id){PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL}
#define VAL(p, v) connections[count++] = (pigen_rtl_connection){(p), (pigen_rtl_object_id){PIGEN_INVALID_ID}, (v), PIGEN_SEMANTIC_INTERNAL}
	PORT("clk", lowering->lowered_endpoints[clk.index].payload);
	if (reset.index != PIGEN_INVALID_ID &&
		lowering->lowered_endpoint_count > reset.index)
	{
		pigen_rtl_object_id reset_object = lowering->lowered_endpoints[reset.index].payload;
		if (native && native->binding.reset_active_low)
			VAL("reset", fab_not(lowering, fab_obj_expr(lowering, reset_object, span), control_type, span));
		else PORT("reset", reset_object);
	}
	else
		VAL("reset", zero);
	PORT("i0_valid", v0); PORT("i0_ready", r0); PORT("packet_in0", p0);
	PORT("i1_valid", v1); PORT("i1_ready", r1); PORT("packet_in1", p1);
	PORT("out_valid", dv); PORT("out_ready", dr); PORT("packet_out", dp);
#undef PORT
#undef VAL
	range = pigen_rtl_connection_range_add(rtl, connections, count);
	if (range.first == (size_t)PIGEN_INVALID_ID) return 0;
	if (!pigen_rtl_instance_set_primitive(rtl, instance,
		PIGEN_RTL_PRIMITIVE_MERGE2, payload->type, range)) return 0;
	written = snprintf(name, sizeof(name), "fabric_merge_%u", instance.index);
	if (written > 0 && (size_t)written < sizeof(name))
		pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name, name);
	return 1;
}

static int route_through_mergeN(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module,
	const pigen_rtl_object_id *payloads, const pigen_rtl_object_id *valids,
	const pigen_rtl_object_id *readies, size_t count,
	pigen_rtl_object_id dest_payload, pigen_rtl_object_id dest_valid,
	pigen_rtl_object_id dest_ready,pigen_source_span span,const pigen_semantic_protocol_endpoint *native,int packet_lock,int track_streams)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
	if (native)
	{
		const pigen_semantic_clock_domain *domain = pigen_clock_domain_get(model, native->binding.domain);
		if (!domain) return 0;
		clk = pigen_symbol_signal(model, domain->clock_symbol);
		reset = pigen_symbol_signal(model, native->binding.reset_symbol);
	}
	const pigen_rtl_object *payload;
	pigen_data_type_id valid_data, payload_data, index_data;
	pigen_rtl_type_id valid_type, payload_type, index_type;
	pigen_rtl_object_id valid_object, payload_object, ready_object;
	pigen_rtl_expr_id *valid_parts, *payload_parts, valid_expr, payload_expr;
	pigen_rtl_instance_id instance;
	pigen_rtl_connection connections[8];
	pigen_rtl_expr_id parameter;
	pigen_rtl_record_range range;
	char name[64];
	size_t i, total_width;
	int written;

	if (clk.index == PIGEN_INVALID_ID || !count ||
		lowering->lowered_endpoint_count <= clk.index)
		return 0;
	payload = pigen_rtl_object_get(rtl, dest_payload);
	if (!payload) return 0;
	total_width = (size_t)(payload->type.index != PIGEN_INVALID_ID ?
		pigen_rtl_type_get(rtl, payload->type)->width : 0);
	if (!total_width) return 0;
	valid_data = pigen_data_type_sized_logic(model, count, PIGEN_SIGN_UNSIGNED);
	payload_data = pigen_data_type_sized_logic(model, count * total_width,
		PIGEN_SIGN_UNSIGNED);
	index_data = pigen_data_type_sized_logic(model, 32, PIGEN_SIGN_UNSIGNED);
	valid_type = pigen_lower_rtl_type(lowering, valid_data);
	payload_type = pigen_lower_rtl_type(lowering, payload_data);
	index_type = pigen_lower_rtl_type(lowering, index_data);
	if (valid_type.index == PIGEN_INVALID_ID ||
		payload_type.index == PIGEN_INVALID_ID ||
		index_type.index == PIGEN_INVALID_ID)
		return 0;
	valid_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, valid_type, PIGEN_SEMANTIC_INTERNAL,
		(pigen_signal_id){PIGEN_INVALID_ID}, span);
	payload_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, payload_type, PIGEN_SEMANTIC_INTERNAL,
		(pigen_signal_id){PIGEN_INVALID_ID}, span);
	ready_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, valid_type, PIGEN_SEMANTIC_INTERNAL,
		(pigen_signal_id){PIGEN_INVALID_ID}, span);
	if (valid_object.index == PIGEN_INVALID_ID ||
		payload_object.index == PIGEN_INVALID_ID ||
		ready_object.index == PIGEN_INVALID_ID)
		return 0;
	{
		char object_name[64];
		int n = snprintf(object_name, sizeof(object_name),
			"fabric_mergeN_valid_%u", valid_object.index);
		if (n > 0 && (size_t)n < sizeof(object_name))
			pigen_rtl_set_name(rtl, &rtl->objects[valid_object.index].name,
				object_name);
		n = snprintf(object_name, sizeof(object_name),
			"fabric_mergeN_payload_%u", payload_object.index);
		if (n > 0 && (size_t)n < sizeof(object_name))
			pigen_rtl_set_name(rtl, &rtl->objects[payload_object.index].name,
				object_name);
		n = snprintf(object_name, sizeof(object_name),
			"fabric_mergeN_ready_%u", ready_object.index);
		if (n > 0 && (size_t)n < sizeof(object_name))
			pigen_rtl_set_name(rtl, &rtl->objects[ready_object.index].name,
				object_name);
	}
	valid_parts = pigen_resize(NULL, count * sizeof(*valid_parts));
	payload_parts = pigen_resize(NULL, count * sizeof(*payload_parts));
	for (i = 0; i < count; i++)
	{
		valid_parts[i] = pigen_rtl_expr_add_object(rtl, valid_type,
			valids[i], span);
		payload_parts[i] = pigen_rtl_expr_add_object(rtl,
			pigen_rtl_object_get(rtl, payloads[i])->type, payloads[i], span);
		if (valid_parts[i].index == PIGEN_INVALID_ID ||
			payload_parts[i].index == PIGEN_INVALID_ID)
		{
			free(valid_parts); free(payload_parts); return 0;
		}
	}
	valid_expr = pigen_rtl_expr_add_concatenation(rtl, valid_type,
		valid_parts, count, span);
	payload_expr = pigen_rtl_expr_add_concatenation(rtl, payload_type,
		payload_parts, count, span);
	free(valid_parts);
	free(payload_parts);
	if (valid_expr.index == PIGEN_INVALID_ID ||
		payload_expr.index == PIGEN_INVALID_ID)
		return 0;
	if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, valid_object,
		valid_expr, span).index == PIGEN_INVALID_ID ||
		pigen_rtl_equation_add_with_owner(rtl, rtl_module, payload_object,
			payload_expr, span).index == PIGEN_INVALID_ID)
		return 0;
	for (i = 0; i < count; i++)
	{
		/* Concatenation places source zero in the most significant lane. */
		pigen_rtl_literal_word word = {(uint64_t)(count - 1 - i), 0, 0};
		pigen_rtl_expr_id index = pigen_rtl_expr_add_literal(rtl,
			PIGEN_RTL_EXPR_INTEGER, index_type, &word, 64, 0, span);
		pigen_rtl_expr_id ready_expr = pigen_rtl_expr_add_object(rtl,
			valid_type, ready_object, span);
		pigen_rtl_expr_id bit = pigen_rtl_expr_add_index(rtl, valid_type,
			ready_expr, index, span);

		if (bit.index == PIGEN_INVALID_ID ||
			pigen_rtl_equation_add_with_owner(rtl, rtl_module, readies[i],
				bit, span).index == PIGEN_INVALID_ID)
			return 0;
	}
	{
		pigen_rtl_literal_word word = {(uint64_t)count, 0, 0};
		pigen_rtl_type_id int_type = pigen_lower_rtl_type(lowering,
			pigen_data_type_sized_logic(model, 32, PIGEN_SIGN_UNSIGNED));

		parameter = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
			int_type, &word, 64, 0, span);
	}
	if (parameter.index == PIGEN_INVALID_ID) return 0;
	pigen_rtl_expr_id parameters[9] = {parameter};
	size_t parameter_count = 1;
	if (native)
	{
		pigen_protocol_channel_layout layout;
		if (!pigen_protocol_layout(pigen_protocol_type_get(model, native->type), PIGEN_CHANNEL_BEAT, &layout)) return 0;
		size_t last_bit = 0, has_last = 0;
		for (size_t f = 0; f < layout.field_count; f++)
			if (layout.fields[f].field == PIGEN_FIELD_LAST) { last_bit = layout.fields[f].offset; has_last = 1; }
		pigen_rtl_literal_word last_word = {last_bit, 0, 0}, has_word = {has_last, 0, 0};
		parameters[1] = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER, index_type, &last_word, 32, 0, span);
		parameters[2] = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER, index_type, &has_word, 32, 0, span);
		pigen_rtl_literal_word lock_word={(uint64_t)packet_lock,0,0};
		parameters[3]=pigen_rtl_expr_add_literal(rtl,PIGEN_RTL_EXPR_INTEGER,index_type,&lock_word,32,0,span);
        const pigen_protocol *protocol=pigen_protocol_type_get(model,native->type);
        const pigen_protocol_field_layout *id=pigen_protocol_layout_field(&layout,PIGEN_FIELD_ID);
        const pigen_protocol_field_layout *dest=pigen_protocol_layout_field(&layout,PIGEN_FIELD_DEST);
        size_t values[5]={(size_t)(track_streams && packet_lock && has_last),id ? id->offset : 0,protocol->id_bits,
            dest ? dest->offset : 0,protocol->destination_bits};
        for(size_t i=0;i<5;i++)
        {
            pigen_rtl_literal_word word={(uint64_t)values[i],0,0};
            parameters[4+i]=pigen_rtl_expr_add_literal(rtl,PIGEN_RTL_EXPR_INTEGER,index_type,&word,32,0,span);
            if(parameters[4+i].index==PIGEN_INVALID_ID) return 0;
        }
        parameter_count = 9;
	}
	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module, module,
		parameters, parameter_count, NULL, 0, span);
	if (instance.index == PIGEN_INVALID_ID) return 0;
	connections[0] = (pigen_rtl_connection){"clk", lowering->lowered_endpoints[clk.index].payload, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[1] = (pigen_rtl_connection){"reset", reset.index != PIGEN_INVALID_ID && lowering->lowered_endpoint_count > reset.index ? lowering->lowered_endpoints[reset.index].payload : (pigen_rtl_object_id){PIGEN_INVALID_ID}, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	if (native && native->binding.reset_active_low)
	{
		connections[1].value = fab_not(lowering, fab_obj_expr(lowering, connections[1].object, span), fab_control_type(lowering), span);
		connections[1].object = (pigen_rtl_object_id){PIGEN_INVALID_ID};
	}
	connections[2] = (pigen_rtl_connection){"in_valid", valid_object, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[3] = (pigen_rtl_connection){"in_ready", ready_object, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[4] = (pigen_rtl_connection){"packet_in", payload_object, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[5] = (pigen_rtl_connection){"out_valid", dest_valid, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[6] = (pigen_rtl_connection){"out_ready", dest_ready, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	connections[7] = (pigen_rtl_connection){"packet_out", dest_payload, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	range = pigen_rtl_connection_range_add(rtl, connections, 8);
	if (range.first == (size_t)PIGEN_INVALID_ID) return 0;
	if (!pigen_rtl_instance_set_primitive(rtl, instance,
			native ? PIGEN_RTL_PRIMITIVE_AXIS_MERGE : PIGEN_RTL_PRIMITIVE_MERGE, pigen_rtl_object_get(rtl, dest_payload)->type, range))
		return 0;
	written = snprintf(name, sizeof(name), "fabric_mergeN_%u", instance.index);
	if (written > 0 && (size_t)written < sizeof(name))
		pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name, name);
	return 1;
}

/* Walk fabric edges in module order, crossing block boundaries. */
static pigen_rtl_expr_id fabric_integer(pigen_rtl_lowering *lowering,
	size_t value, pigen_source_span span)
{
	pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
		pigen_data_type_sized_logic(lowering->semantics, 64, PIGEN_SIGN_UNSIGNED));
	pigen_rtl_literal_word word = {value, 0, 0};
	return pigen_rtl_expr_add_literal(lowering->rtl, PIGEN_RTL_EXPR_INTEGER, type, &word, 64, 0, span);
}

static pigen_rtl_object_id fabric_vector(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id module, size_t width, pigen_source_span span)
{
	pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
		pigen_data_type_sized_logic(lowering->semantics, width, PIGEN_SIGN_UNSIGNED));
	pigen_rtl_object_id object = pigen_rtl_object_add_with_owner(lowering->rtl, module,
		PIGEN_RTL_OBJECT_VARIABLE, type, PIGEN_SEMANTIC_INTERNAL, (pigen_signal_id){PIGEN_INVALID_ID}, span);
	if (object.index != PIGEN_INVALID_ID)
	{
		char name[64]; snprintf(name, sizeof name, "fabric_axi_vector_%u", object.index);
		pigen_rtl_set_name(lowering->rtl, &lowering->rtl->objects[object.index].name, name);
	}
	return object;
}

static pigen_rtl_expr_id repack_protocol_channel(pigen_rtl_lowering *lowering,
	const pigen_protocol *input, const pigen_protocol *output,
	pigen_protocol_channel channel, pigen_rtl_expr_id base, size_t base_offset,
	pigen_rtl_type_id result_type, size_t tag, int inject_tag, pigen_source_span span)
{
	pigen_protocol_channel_layout from, to;
	if (!pigen_protocol_layout(input, channel, &from) || !pigen_protocol_layout(output, channel, &to))
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_expr_id parts[PIGEN_FIELD_COUNT];
	for (size_t i = 0; i < to.field_count; i++)
	{
		const pigen_protocol_field_layout *field = &to.fields[to.field_count-1-i];
		const pigen_protocol_field_layout *source = pigen_protocol_layout_field(&from, field->field);
		size_t copied = field->bits;
		if (field->field == PIGEN_FIELD_ID && inject_tag && output->id_bits > input->id_bits) copied = input->id_bits;
		pigen_rtl_expr_id value = {PIGEN_INVALID_ID};
		if (copied)
		{
			if (!source || source->bits < copied) return value;
			pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
				pigen_data_type_sized_logic(lowering->semantics, copied, PIGEN_SIGN_UNSIGNED));
			pigen_rtl_expr_id low = fabric_integer(lowering, base_offset+source->offset, span);
			pigen_rtl_expr_id high = fabric_integer(lowering, base_offset+source->offset+copied-1, span);
			value = pigen_rtl_expr_add_select(lowering->rtl, type, base, high, low, PIGEN_SEMANTIC_SELECT_RANGE, span);
		}
		if (copied < field->bits)
		{
			pigen_rtl_type_id prefix_type = pigen_lower_rtl_type(lowering,
				pigen_data_type_sized_logic(lowering->semantics, field->bits-copied, PIGEN_SIGN_UNSIGNED));
			pigen_rtl_expr_id prefix = pigen_rtl_expr_add_bits(lowering->rtl, prefix_type, tag, span);
			if (!copied) value = prefix;
			else
			{
				pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
					pigen_data_type_sized_logic(lowering->semantics, field->bits, PIGEN_SIGN_UNSIGNED));
				pigen_rtl_expr_id id_parts[] = {prefix, value};
				value = pigen_rtl_expr_add_concatenation(lowering->rtl, type, id_parts, 2, span);
			}
		}
		parts[i] = value;
	}
	return pigen_rtl_expr_add_concatenation(lowering->rtl, result_type, parts, to.field_count, span);
}

/* A fabric reads stored tokens from the consumer side and writes complete
 * tokens to the producer side. Child proxies expose the same triplet on both. */
static pigen_rtl_signal_endpoints native_fabric_channel(pigen_rtl_lowering *lowering,
	pigen_signal_id signal, int producer, pigen_source_span span)
{
	pigen_rtl_signal_endpoints result = lowering->lowered_endpoints[signal.index];
	if (producer)
	{
		result.payload = result.input_payload;
		result.valid = fab_obj_expr(lowering, result.input_valid, span);
		result.ready = fab_obj_expr(lowering, result.input_ready, span);
	}
	else result.ready = fab_obj_expr(lowering, result.source_ready, span);
	return result;
}

typedef struct {
	pigen_rtl_signal_endpoints channels[PIGEN_CHANNEL_COUNT];
} native_fabric_route;

static int route_axi_merge(pigen_rtl_lowering *lowering, pigen_module_id module,
	pigen_rtl_module_id rtl_module,size_t edge_index,size_t source_count,const native_fabric_route *routes)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	const pigen_semantic_protocol_fabric_edge *edge = &model->protocol_fabric_edges[edge_index];
	const pigen_semantic_protocol_endpoint *destination = pigen_protocol_endpoint_get(model, edge->destination);
	const pigen_protocol *protocol = pigen_protocol_type_get(model, destination->type);
	pigen_source_span span = edge->span;
	if (source_count > 32) return 0;
	const pigen_semantic_protocol_endpoint *sources[32];size_t edge_indices[32];
	size_t count = 0;
	for (size_t e = edge_index; e < model->protocol_fabric_edge_count; e++)
		if (model->protocol_fabric_edges[e].destination.index == edge->destination.index)
		{edge_indices[count]=e;sources[count++] = pigen_protocol_endpoint_get(model, model->protocol_fabric_edges[e].source);}
	if (count != source_count) return 0;
	pigen_rtl_connection connections[32];
	size_t connection_count = 0;
	pigen_rtl_expr_id parameters[9];
	parameters[0] = fabric_integer(lowering, count, span);
	parameters[6] = fabric_integer(lowering, protocol->family == PIGEN_PROTOCOL_AXI4, span);
	parameters[7] = fabric_integer(lowering, 0, span); parameters[8] = parameters[7];
	const pigen_semantic_clock_domain *domain = pigen_clock_domain_get(model, destination->binding.domain);
	pigen_signal_id clock = pigen_symbol_signal(model, domain->clock_symbol);
	pigen_signal_id reset = pigen_symbol_signal(model, destination->binding.reset_symbol);
	connections[connection_count++] = (pigen_rtl_connection){"clk", lowering->lowered_endpoints[clock.index].payload, {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	pigen_rtl_expr_id reset_value = fab_obj_expr(lowering, lowering->lowered_endpoints[reset.index].payload, span);
	if (destination->binding.reset_active_low) reset_value = fab_not(lowering, reset_value, fab_control_type(lowering), span);
	connections[connection_count++] = (pigen_rtl_connection){"reset", {PIGEN_INVALID_ID}, reset_value, PIGEN_SEMANTIC_INTERNAL};
	static const char *names[5][6] = {
		{"s_aw_payload", "s_aw_valid", "s_aw_ready", "m_aw_payload", "m_aw_valid", "m_aw_ready"},
		{"s_w_payload", "s_w_valid", "s_w_ready", "m_w_payload", "m_w_valid", "m_w_ready"},
		{"s_b_payload", "s_b_valid", "s_b_ready", "m_b_payload", "m_b_valid", "m_b_ready"},
		{"s_ar_payload", "s_ar_valid", "s_ar_ready", "m_ar_payload", "m_ar_valid", "m_ar_ready"},
		{"s_r_payload", "s_r_valid", "s_r_ready", "m_r_payload", "m_r_valid", "m_r_ready"}
	};
	for (pigen_protocol_channel channel = PIGEN_CHANNEL_AW; channel <= PIGEN_CHANNEL_R; channel++)
	{
		pigen_protocol_channel_layout layout;
		if (!pigen_protocol_layout(protocol, channel, &layout) || layout.payload_bits > SIZE_MAX/count) return 0;
		size_t width = layout.payload_bits, row = channel - PIGEN_CHANNEL_AW;
		parameters[row+1] = fabric_integer(lowering, width, span);
		for (size_t f = 0; f < layout.field_count; f++)
			if (layout.fields[f].field == PIGEN_FIELD_LAST)
				parameters[channel == PIGEN_CHANNEL_W ? 7 : 8] = fabric_integer(lowering, layout.fields[f].offset, span);
		int reverse = channel == PIGEN_CHANNEL_B || channel == PIGEN_CHANNEL_R;
		pigen_rtl_object_id vectors[3] = {fabric_vector(lowering, rtl_module, width*count, span),
			fabric_vector(lowering, rtl_module, count, span), fabric_vector(lowering, rtl_module, count, span)};
		if (vectors[0].index == PIGEN_INVALID_ID || vectors[1].index == PIGEN_INVALID_ID ||
			vectors[2].index == PIGEN_INVALID_ID) return 0;
		pigen_rtl_expr_id payloads[32], controls[32];
		for (size_t i = 0; i < count; i++)
		{
			pigen_rtl_signal_endpoints source_end = routes[edge_indices[i]].channels[channel].payload.index!=PIGEN_INVALID_ID ?
				routes[edge_indices[i]].channels[channel] : native_fabric_channel(lowering,sources[i]->channels[channel],reverse,span);
			const pigen_rtl_signal_endpoints *s = &source_end;
			const pigen_rtl_expr *valid = pigen_rtl_expr_get(rtl, s->valid), *ready = pigen_rtl_expr_get(rtl, s->ready);
			if (!valid || !ready || valid->kind != PIGEN_RTL_EXPR_OBJECT || ready->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
			pigen_rtl_object_id valid_object = valid->object, ready_object = ready->object;
			const pigen_protocol *source_protocol = pigen_protocol_type_get(model, sources[i]->type);
			pigen_rtl_type_id target_payload_type = pigen_rtl_object_get(rtl,
				lowering->lowered_endpoints[destination->channels[channel].index].payload)->type;
			payloads[i] = !reverse ? repack_protocol_channel(lowering, source_protocol, protocol, channel,
				fab_obj_expr(lowering, s->payload, span), 0, target_payload_type, i, 1, span) :
				(pigen_rtl_expr_id){PIGEN_INVALID_ID};
			controls[i] = fab_obj_expr(lowering, reverse ? ready_object : valid_object, span);
			pigen_rtl_expr_id lane = fabric_integer(lowering, count-1-i, span);
			pigen_rtl_expr_id control_value = pigen_rtl_expr_add_index(rtl, fab_control_type(lowering),
				fab_obj_expr(lowering, vectors[reverse ? 1 : 2], span), lane, span);
			if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, reverse ? valid_object : ready_object,
				control_value, span).index == PIGEN_INVALID_ID) return 0;
			if (reverse)
			{
				pigen_rtl_type_id payload_type = pigen_rtl_object_get(rtl, s->payload)->type;
				pigen_rtl_expr_id selected = repack_protocol_channel(lowering, protocol, source_protocol, channel,
					fab_obj_expr(lowering, vectors[0], span), (count-1-i)*width, payload_type, 0, 0, span);
				if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, s->payload, selected, span).index == PIGEN_INVALID_ID) return 0;
			}
		}
		if (!reverse)
		{
			pigen_rtl_type_id type = pigen_rtl_object_get(rtl, vectors[0])->type;
			pigen_rtl_expr_id packet = pigen_rtl_expr_add_concatenation(rtl, type, payloads, count, span);
			if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, vectors[0], packet, span).index == PIGEN_INVALID_ID) return 0;
		}
		pigen_rtl_object_id control_vector = vectors[reverse ? 2 : 1];
		pigen_rtl_expr_id control_packet = pigen_rtl_expr_add_concatenation(rtl,
			pigen_rtl_object_get(rtl, control_vector)->type, controls, count, span);
		if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, control_vector, control_packet, span).index == PIGEN_INVALID_ID) return 0;
		pigen_rtl_signal_endpoints destination_end = native_fabric_channel(lowering, destination->channels[channel], !reverse, span);
		const pigen_rtl_signal_endpoints *d = &destination_end;
		const pigen_rtl_expr *valid = pigen_rtl_expr_get(rtl, d->valid), *ready = pigen_rtl_expr_get(rtl, d->ready);
		if (!valid || !ready || valid->kind != PIGEN_RTL_EXPR_OBJECT || ready->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
		pigen_rtl_object_id objects[6] = {vectors[0], vectors[1], vectors[2], d->payload, valid->object, ready->object};
		for (size_t n = 0; n < 6; n++) connections[connection_count++] =
			(pigen_rtl_connection){names[row][n], objects[n], {PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL};
	}
	pigen_rtl_instance_id instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module, module, parameters, 9, NULL, 0, span);
	pigen_rtl_record_range range = pigen_rtl_connection_range_add(rtl, connections, connection_count);
	if (instance.index == PIGEN_INVALID_ID || range.first == (size_t)PIGEN_INVALID_ID ||
		!pigen_rtl_instance_set_primitive(rtl, instance, PIGEN_RTL_PRIMITIVE_AXI_MERGE, (pigen_rtl_type_id){PIGEN_INVALID_ID}, range)) return 0;
	char name[64]; snprintf(name, sizeof name, "fabric_axi_merge_%u", instance.index);
	pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name, name);
	return 1;
}

static int route_stream_demux(pigen_rtl_lowering *lowering,pigen_module_id module,
	pigen_rtl_module_id rtl_module,size_t first,size_t count,native_fabric_route *routes)
{
	pigen_semantic_model *model=lowering->semantics;pigen_rtl_model *rtl=lowering->rtl;
	const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[first];
	const pigen_semantic_protocol_endpoint *source=pigen_protocol_endpoint_get(model,edge->source);
	pigen_source_span span=edge->span;
	pigen_protocol_channel_layout layout;
	if(!pigen_protocol_layout(pigen_protocol_type_get(model,source->type),PIGEN_CHANNEL_BEAT,&layout)) return 0;
	const pigen_protocol_field_layout *dest=pigen_protocol_layout_field(&layout,PIGEN_FIELD_DEST);
	if(!dest || !count || count>32 || layout.payload_bits>SIZE_MAX/count) return 0;
	pigen_rtl_signal_endpoints input=native_fabric_channel(lowering,source->channels[PIGEN_CHANNEL_BEAT],0,span);
	const pigen_rtl_expr *valid=pigen_rtl_expr_get(rtl,input.valid),*ready=pigen_rtl_expr_get(rtl,input.ready);
	if(!valid || !ready || valid->kind!=PIGEN_RTL_EXPR_OBJECT || ready->kind!=PIGEN_RTL_EXPR_OBJECT) return 0;
	pigen_rtl_object_id input_valid=valid->object,input_ready=ready->object;
	pigen_rtl_type_id payload_type=pigen_rtl_object_get(rtl,input.payload)->type;
	pigen_rtl_object_id packets=fabric_vector(lowering,rtl_module,count*layout.payload_bits,span);
	pigen_rtl_object_id valids=fabric_vector(lowering,rtl_module,count,span);
	pigen_rtl_object_id readies=fabric_vector(lowering,rtl_module,count,span);
	if(packets.index==PIGEN_INVALID_ID || valids.index==PIGEN_INVALID_ID || readies.index==PIGEN_INVALID_ID) return 0;
	pigen_rtl_expr_id ready_parts[32];size_t lane=0;
	for(size_t e=first;e<model->protocol_fabric_edge_count;e++)
	{
		if(model->protocol_fabric_edges[e].source.index!=edge->source.index) continue;
		pigen_rtl_object_id payload=fabric_vector(lowering,rtl_module,layout.payload_bits,span);
		pigen_rtl_object_id lane_valid=fabric_vector(lowering,rtl_module,1,span);
		pigen_rtl_object_id lane_ready=fabric_vector(lowering,rtl_module,1,span);
		if(payload.index==PIGEN_INVALID_ID || lane_valid.index==PIGEN_INVALID_ID || lane_ready.index==PIGEN_INVALID_ID) return 0;
		pigen_rtl_expr_id low=fabric_integer(lowering,lane*layout.payload_bits,span);
		pigen_rtl_expr_id high=fabric_integer(lowering,(lane+1)*layout.payload_bits-1,span);
		pigen_rtl_expr_id packet=pigen_rtl_expr_add_select(rtl,pigen_rtl_object_get(rtl,payload)->type,
			fab_obj_expr(lowering,packets,span),high,low,PIGEN_SEMANTIC_SELECT_RANGE,span);
		pigen_rtl_expr_id bit=pigen_rtl_expr_add_index(rtl,pigen_rtl_object_get(rtl,lane_valid)->type,
			fab_obj_expr(lowering,valids,span),fabric_integer(lowering,lane,span),span);
		if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,payload,packet,span).index==PIGEN_INVALID_ID ||
			pigen_rtl_equation_add_with_owner(rtl,rtl_module,lane_valid,bit,span).index==PIGEN_INVALID_ID) return 0;
		routes[e].channels[PIGEN_CHANNEL_BEAT]=(pigen_rtl_signal_endpoints){.payload=payload,.valid=fab_obj_expr(lowering,lane_valid,span),
			.ready=fab_obj_expr(lowering,lane_ready,span)};
		ready_parts[count-1-lane]=routes[e].channels[PIGEN_CHANNEL_BEAT].ready;lane++;
	}
	if(lane!=count) return 0;
	pigen_rtl_expr_id controls=pigen_rtl_expr_add_concatenation(rtl,pigen_rtl_object_get(rtl,readies)->type,ready_parts,count,span);
	if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,readies,controls,span).index==PIGEN_INVALID_ID) return 0;
	const pigen_semantic_clock_domain *domain=pigen_clock_domain_get(model,source->binding.domain);
	pigen_signal_id clock=pigen_symbol_signal(model,domain->clock_symbol);
	pigen_signal_id reset=pigen_symbol_signal(model,source->binding.reset_symbol);
	pigen_rtl_expr_id reset_value=fab_obj_expr(lowering,lowering->lowered_endpoints[reset.index].payload,span);
	if(source->binding.reset_active_low) reset_value=fab_not(lowering,reset_value,fab_control_type(lowering),span);
	pigen_rtl_connection connections[8]={
		{"clk",lowering->lowered_endpoints[clock.index].payload,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"reset",{PIGEN_INVALID_ID},reset_value,PIGEN_SEMANTIC_INTERNAL},
		{"in_valid",input_valid,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"in_ready",input_ready,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"packet_in",input.payload,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"out_valid",valids,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"out_ready",readies,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
		{"packet_out",packets,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL}};
	pigen_rtl_expr_id parameters[3]={fabric_integer(lowering,count,span),fabric_integer(lowering,dest->offset,span),fabric_integer(lowering,dest->bits,span)};
	pigen_rtl_instance_id instance=pigen_rtl_instance_add_with_owner(rtl,rtl_module,module,parameters,3,NULL,0,span);
	pigen_rtl_record_range range=pigen_rtl_connection_range_add(rtl,connections,8);
	if(instance.index==PIGEN_INVALID_ID || range.first==(size_t)PIGEN_INVALID_ID ||
		!pigen_rtl_instance_set_primitive(rtl,instance,PIGEN_RTL_PRIMITIVE_AXIS_DEMUX,payload_type,range)) return 0;
	char name[64];snprintf(name,sizeof name,"fabric_axis_demux_%u",instance.index);
	pigen_rtl_set_name(rtl,&rtl->instances[instance.index].name,name);return 1;
}

static size_t native_field_offset(const pigen_protocol_channel_layout *layout,pigen_protocol_field field)
{
	const pigen_protocol_field_layout *found=pigen_protocol_layout_field(layout,field);
	return found ? found->offset : 0;
}

static int route_memory_demux(pigen_rtl_lowering *lowering,pigen_module_id module,
	pigen_rtl_module_id rtl_module,size_t first,size_t count,native_fabric_route *routes)
{
	pigen_semantic_model *model=lowering->semantics;pigen_rtl_model *rtl=lowering->rtl;
	const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[first];
	const pigen_semantic_protocol_endpoint *source=pigen_protocol_endpoint_get(model,edge->source);
	const pigen_protocol *protocol=pigen_protocol_type_get(model,source->type);
	pigen_source_span span=edge->span;
	if(!count || count>32 || protocol->address_bits>SIZE_MAX/count) return 0;
	size_t indices[32],used=0;
	for(size_t e=first;e<model->protocol_fabric_edge_count;e++)
		if(model->protocol_fabric_edges[e].source.index==edge->source.index) indices[used++]=e;
	if(used!=count) return 0;
	pigen_protocol_channel_layout layouts[PIGEN_CHANNEL_COUNT];
	for(pigen_protocol_channel c=PIGEN_CHANNEL_AW;c<=PIGEN_CHANNEL_R;c++)
		if(!pigen_protocol_layout(protocol,c,&layouts[c]) || layouts[c].payload_bits>SIZE_MAX/count) return 0;
	const pigen_semantic_clock_domain *domain=pigen_clock_domain_get(model,source->binding.domain);
	pigen_signal_id clock=pigen_symbol_signal(model,domain->clock_symbol),reset=pigen_symbol_signal(model,source->binding.reset_symbol);
	pigen_rtl_expr_id reset_value=fab_obj_expr(lowering,lowering->lowered_endpoints[reset.index].payload,span);
	if(source->binding.reset_active_low) reset_value=fab_not(lowering,reset_value,fab_control_type(lowering),span);
	pigen_rtl_connection connections[32];size_t connection_count=0;
	connections[connection_count++]=(pigen_rtl_connection){"clk",lowering->lowered_endpoints[clock.index].payload,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL};
	connections[connection_count++]=(pigen_rtl_connection){"reset",{PIGEN_INVALID_ID},reset_value,PIGEN_SEMANTIC_INTERNAL};
	static const char *names[5][6]={
		{"s_aw_payload","s_aw_valid","s_aw_ready","m_aw_payload","m_aw_valid","m_aw_ready"},
		{"s_w_payload","s_w_valid","s_w_ready","m_w_payload","m_w_valid","m_w_ready"},
		{"s_b_payload","s_b_valid","s_b_ready","m_b_payload","m_b_valid","m_b_ready"},
		{"s_ar_payload","s_ar_valid","s_ar_ready","m_ar_payload","m_ar_valid","m_ar_ready"},
		{"s_r_payload","s_r_valid","s_r_ready","m_r_payload","m_r_valid","m_r_ready"}};
	for(pigen_protocol_channel c=PIGEN_CHANNEL_AW;c<=PIGEN_CHANNEL_R;c++)
	{
		int reverse=c==PIGEN_CHANNEL_B || c==PIGEN_CHANNEL_R;
		size_t width=layouts[c].payload_bits,row=c-PIGEN_CHANNEL_AW;
		pigen_rtl_signal_endpoints upstream=native_fabric_channel(lowering,source->channels[c],reverse,span);
		const pigen_rtl_expr *v=pigen_rtl_expr_get(rtl,upstream.valid),*r=pigen_rtl_expr_get(rtl,upstream.ready);
		if(!v || !r || v->kind!=PIGEN_RTL_EXPR_OBJECT || r->kind!=PIGEN_RTL_EXPR_OBJECT) return 0;
		pigen_rtl_object_id scalar[3]={upstream.payload,v->object,r->object};
		pigen_rtl_object_id vector[3]={fabric_vector(lowering,rtl_module,width*count,span),fabric_vector(lowering,rtl_module,count,span),fabric_vector(lowering,rtl_module,count,span)};
		for(size_t k=0;k<3;k++) if(vector[k].index==PIGEN_INVALID_ID) return 0;
		pigen_rtl_expr_id payload_parts[32],control_parts[32];
		for(size_t lane=0;lane<count;lane++)
		{
			pigen_rtl_object_id payload=fabric_vector(lowering,rtl_module,width,span),valid=fabric_vector(lowering,rtl_module,1,span),ready=fabric_vector(lowering,rtl_module,1,span);
			if(payload.index==PIGEN_INVALID_ID || valid.index==PIGEN_INVALID_ID || ready.index==PIGEN_INVALID_ID) return 0;
			routes[indices[lane]].channels[c]=(pigen_rtl_signal_endpoints){.payload=payload,.valid=fab_obj_expr(lowering,valid,span),.ready=fab_obj_expr(lowering,ready,span)};
			pigen_rtl_expr_id control=fab_obj_expr(lowering,vector[reverse ? 2 : 1],span);
			if(count>1) control=pigen_rtl_expr_add_index(rtl,fab_control_type(lowering),control,fabric_integer(lowering,lane,span),span);
			if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,reverse ? ready : valid,control,span).index==PIGEN_INVALID_ID) return 0;
			control_parts[count-1-lane]=fab_obj_expr(lowering,reverse ? valid : ready,span);
			if(reverse) payload_parts[count-1-lane]=fab_obj_expr(lowering,payload,span);
			else
			{
				pigen_rtl_expr_id packet=pigen_rtl_expr_add_select(rtl,pigen_rtl_object_get(rtl,payload)->type,fab_obj_expr(lowering,vector[0],span),
					fabric_integer(lowering,(lane+1)*width-1,span),fabric_integer(lowering,lane*width,span),PIGEN_SEMANTIC_SELECT_RANGE,span);
				if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,payload,packet,span).index==PIGEN_INVALID_ID) return 0;
			}
		}
		pigen_rtl_object_id control_vector=vector[reverse ? 1 : 2];
		pigen_rtl_expr_id controls=pigen_rtl_expr_add_concatenation(rtl,pigen_rtl_object_get(rtl,control_vector)->type,control_parts,count,span);
		if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,control_vector,controls,span).index==PIGEN_INVALID_ID) return 0;
		if(reverse)
		{
			pigen_rtl_expr_id packets=pigen_rtl_expr_add_concatenation(rtl,pigen_rtl_object_get(rtl,vector[0])->type,payload_parts,count,span);
			if(pigen_rtl_equation_add_with_owner(rtl,rtl_module,vector[0],packets,span).index==PIGEN_INVALID_ID) return 0;
		}
		for(size_t k=0;k<3;k++) connections[connection_count++]=(pigen_rtl_connection){names[row][k],scalar[k],{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL};
		for(size_t k=0;k<3;k++) connections[connection_count++]=(pigen_rtl_connection){names[row][k+3],vector[k],{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL};
	}
	size_t values[20]={count,layouts[PIGEN_CHANNEL_AW].payload_bits,layouts[PIGEN_CHANNEL_W].payload_bits,layouts[PIGEN_CHANNEL_B].payload_bits,
		layouts[PIGEN_CHANNEL_AR].payload_bits,layouts[PIGEN_CHANNEL_R].payload_bits,protocol->address_bits,
		native_field_offset(&layouts[PIGEN_CHANNEL_AW],PIGEN_FIELD_ADDR),native_field_offset(&layouts[PIGEN_CHANNEL_AR],PIGEN_FIELD_ADDR),protocol->family==PIGEN_PROTOCOL_AXI4,
		native_field_offset(&layouts[PIGEN_CHANNEL_W],PIGEN_FIELD_LAST),native_field_offset(&layouts[PIGEN_CHANNEL_R],PIGEN_FIELD_LAST),native_field_offset(&layouts[PIGEN_CHANNEL_AR],PIGEN_FIELD_LEN),protocol->id_bits,
		native_field_offset(&layouts[PIGEN_CHANNEL_AW],PIGEN_FIELD_ID),native_field_offset(&layouts[PIGEN_CHANNEL_AR],PIGEN_FIELD_ID),native_field_offset(&layouts[PIGEN_CHANNEL_B],PIGEN_FIELD_ID),native_field_offset(&layouts[PIGEN_CHANNEL_R],PIGEN_FIELD_ID),
		native_field_offset(&layouts[PIGEN_CHANNEL_B],PIGEN_FIELD_RESP),native_field_offset(&layouts[PIGEN_CHANNEL_R],PIGEN_FIELD_RESP)};
	pigen_rtl_expr_id parameters[22],bases[32],limits[32];
	for(size_t i=0;i<20;i++) parameters[i]=fabric_integer(lowering,values[i],span);
	pigen_rtl_type_id address_type=pigen_lower_rtl_type(lowering,pigen_data_type_sized_logic(model,protocol->address_bits,PIGEN_SIGN_UNSIGNED));
	pigen_rtl_type_id windows_type=pigen_lower_rtl_type(lowering,pigen_data_type_sized_logic(model,protocol->address_bits*count,PIGEN_SIGN_UNSIGNED));
	for(size_t i=0;i<count;i++)
	{
		bases[count-1-i]=pigen_rtl_expr_add_bits(rtl,address_type,model->protocol_fabric_edges[indices[i]].address_base,span);
		limits[count-1-i]=pigen_rtl_expr_add_bits(rtl,address_type,model->protocol_fabric_edges[indices[i]].address_limit,span);
	}
	parameters[20]=pigen_rtl_expr_add_concatenation(rtl,windows_type,bases,count,span);
	parameters[21]=pigen_rtl_expr_add_concatenation(rtl,windows_type,limits,count,span);
	pigen_rtl_instance_id instance=pigen_rtl_instance_add_with_owner(rtl,rtl_module,module,parameters,22,NULL,0,span);
	pigen_rtl_record_range range=pigen_rtl_connection_range_add(rtl,connections,connection_count);
	if(instance.index==PIGEN_INVALID_ID || range.first==(size_t)PIGEN_INVALID_ID || !pigen_rtl_instance_set_primitive(rtl,instance,PIGEN_RTL_PRIMITIVE_AXI_DEMUX,(pigen_rtl_type_id){PIGEN_INVALID_ID},range)) return 0;
	char name[64];snprintf(name,sizeof name,"fabric_axi_demux_%u",instance.index);pigen_rtl_set_name(rtl,&rtl->instances[instance.index].name,name);return 1;
}

static int lower_protocol_fabric_edges(pigen_rtl_lowering *lowering,
	pigen_module_id module,pigen_rtl_module_id rtl_module,const native_fabric_route *routes)
{
	pigen_semantic_model *model = lowering->semantics;
	for (size_t e = 0; e < model->protocol_fabric_edge_count; e++)
	{
		const pigen_semantic_protocol_fabric_edge *edge = &model->protocol_fabric_edges[e];
		if (edge->module.index != module.index) continue;
		const pigen_semantic_protocol_endpoint *source = pigen_protocol_endpoint_get(model, edge->source);
		const pigen_semantic_protocol_endpoint *destination = pigen_protocol_endpoint_get(model, edge->destination);
		size_t source_count = 0;
		for (size_t p = 0; p < model->protocol_fabric_edge_count; p++)
			if (model->protocol_fabric_edges[p].destination.index == edge->destination.index) source_count++;
		if (source_count > 1)
		{
			for (size_t p = 0; p < e; p++)
				if (model->protocol_fabric_edges[p].destination.index == edge->destination.index) goto next_protocol_edge;
			const pigen_protocol *protocol = pigen_protocol_type_get(model, source->type);
			if (source_count > 32) return 0;
			if (protocol->family != PIGEN_PROTOCOL_AXI4_STREAM)
			{
				if (!route_axi_merge(lowering,module,rtl_module,e,source_count,routes)) return 0;
				continue;
			}
			pigen_rtl_object_id payloads[32], valids[32], readies[32];
			const pigen_protocol *target_protocol=pigen_protocol_type_get(model,destination->type);
			pigen_protocol_channel_layout target_layout;
			if(!pigen_protocol_layout(target_protocol,PIGEN_CHANNEL_BEAT,&target_layout)) return 0;
			int packet_lock=1;size_t count = 0;
			for (size_t p = e; p < model->protocol_fabric_edge_count; p++)
			{
				const pigen_semantic_protocol_fabric_edge *part = &model->protocol_fabric_edges[p];
				if (part->destination.index != edge->destination.index) continue;
				const pigen_semantic_protocol_endpoint *src = pigen_protocol_endpoint_get(model, part->source);
				pigen_rtl_signal_endpoints source_end = routes[p].channels[PIGEN_CHANNEL_BEAT].payload.index!=PIGEN_INVALID_ID ? routes[p].channels[PIGEN_CHANNEL_BEAT] :
					native_fabric_channel(lowering, src->channels[PIGEN_CHANNEL_BEAT], 0, edge->span);
				const pigen_rtl_signal_endpoints *s = &source_end;
				const pigen_rtl_expr *valid = pigen_rtl_expr_get(lowering->rtl, s->valid);
				const pigen_rtl_expr *ready = pigen_rtl_expr_get(lowering->rtl, s->ready);
				if (!valid || !ready || valid->kind != PIGEN_RTL_EXPR_OBJECT || ready->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
				payloads[count] = s->payload; valids[count] = valid->object; readies[count] = ready->object;
				if(routes[p].channels[PIGEN_CHANNEL_BEAT].payload.index!=PIGEN_INVALID_ID) packet_lock=0;
				if(protocol->id_bits!=target_protocol->id_bits)
				{
					payloads[count]=fabric_vector(lowering,rtl_module,target_layout.payload_bits,edge->span);
					if(payloads[count].index==PIGEN_INVALID_ID) return 0;
					pigen_rtl_expr_id tagged=repack_protocol_channel(lowering,protocol,target_protocol,PIGEN_CHANNEL_BEAT,
						fab_obj_expr(lowering,s->payload,edge->span),0,pigen_rtl_object_get(lowering->rtl,payloads[count])->type,
						count,1,edge->span);
					if(tagged.index==PIGEN_INVALID_ID || pigen_rtl_equation_add_with_owner(lowering->rtl,rtl_module,
						payloads[count],tagged,edge->span).index==PIGEN_INVALID_ID) return 0;
				}
				count++;
			}
			pigen_rtl_signal_endpoints destination_end = native_fabric_channel(lowering, destination->channels[PIGEN_CHANNEL_BEAT], 1, edge->span);
			const pigen_rtl_signal_endpoints *d = &destination_end;
			const pigen_rtl_expr *valid = pigen_rtl_expr_get(lowering->rtl, d->valid);
			const pigen_rtl_expr *ready = pigen_rtl_expr_get(lowering->rtl, d->ready);
			if (!valid || !ready || valid->kind != PIGEN_RTL_EXPR_OBJECT || ready->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
			pigen_rtl_object_id dest_valid = valid->object, dest_ready = ready->object;
			if (!route_through_mergeN(lowering, module, rtl_module, payloads, valids, readies,
				count, d->payload, dest_valid, dest_ready, edge->span, destination,packet_lock,protocol->id_bits==target_protocol->id_bits)) return 0;
			continue;
		}
		for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
		{
			pigen_signal_id source_signal = source->channels[channel];
			pigen_signal_id destination_signal = destination->channels[channel];
			if (source_signal.index == PIGEN_INVALID_ID) continue;
			int reverse_channel = channel == PIGEN_CHANNEL_B || channel == PIGEN_CHANNEL_R;
			if (reverse_channel)
			{
				pigen_signal_id swap = source_signal;
				source_signal = destination_signal;
				destination_signal = swap;
			}
			pigen_rtl_signal_endpoints source_end = !reverse_channel && routes[e].channels[channel].payload.index!=PIGEN_INVALID_ID ?
				routes[e].channels[channel] : native_fabric_channel(lowering,source_signal,0,edge->span);
			pigen_rtl_signal_endpoints destination_end = reverse_channel && routes[e].channels[channel].payload.index!=PIGEN_INVALID_ID ?
				routes[e].channels[channel] : native_fabric_channel(lowering,destination_signal,1,edge->span);
			const pigen_rtl_signal_endpoints *s = &source_end, *d = &destination_end;
			const pigen_rtl_expr *sv = pigen_rtl_expr_get(lowering->rtl, s->valid);
			const pigen_rtl_expr *sr = pigen_rtl_expr_get(lowering->rtl, s->ready);
			const pigen_rtl_expr *dv = pigen_rtl_expr_get(lowering->rtl, d->valid);
			const pigen_rtl_expr *dr = pigen_rtl_expr_get(lowering->rtl, d->ready);
			if (!sv || !sr || !dv || !dr || sv->kind != PIGEN_RTL_EXPR_OBJECT ||
				sr->kind != PIGEN_RTL_EXPR_OBJECT || dv->kind != PIGEN_RTL_EXPR_OBJECT || dr->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
			pigen_rtl_object_id valid_source = sv->object, ready_source = sr->object;
			pigen_rtl_object_id valid_destination = dv->object, ready_destination = dr->object;
			const pigen_protocol *source_protocol = pigen_protocol_type_get(model, source->type);
			const pigen_protocol *destination_protocol = pigen_protocol_type_get(model, destination->type);
			int reverse = channel == PIGEN_CHANNEL_B || channel == PIGEN_CHANNEL_R;
			pigen_rtl_expr_id adapted = {PIGEN_INVALID_ID};
			pigen_rtl_object_id source_payload = s->payload;
			if (source_protocol->id_bits != destination_protocol->id_bits)
			{
				pigen_rtl_type_id type = pigen_rtl_object_get(lowering->rtl, d->payload)->type;
				adapted = repack_protocol_channel(lowering, reverse ? destination_protocol : source_protocol,
					reverse ? source_protocol : destination_protocol, channel, fab_obj_expr(lowering, s->payload, edge->span),
					0, type, 0, !reverse, edge->span);
				if (adapted.index == PIGEN_INVALID_ID) return 0;
				if (!edge->direct)
				{
					source_payload = pigen_rtl_object_add_with_owner(lowering->rtl, rtl_module, PIGEN_RTL_OBJECT_VARIABLE,
						type, PIGEN_SEMANTIC_INTERNAL, (pigen_signal_id){PIGEN_INVALID_ID}, edge->span);
					if (source_payload.index == PIGEN_INVALID_ID) return 0;
					char name[64]; snprintf(name, sizeof name, "fabric_axi_adapt_%u", source_payload.index);
					pigen_rtl_set_name(lowering->rtl, &lowering->rtl->objects[source_payload.index].name, name);
					if (pigen_rtl_equation_add_with_owner(lowering->rtl, rtl_module, source_payload, adapted, edge->span).index == PIGEN_INVALID_ID) return 0;
				}
			}
			if (edge->direct)
			{
				int payload_connected = adapted.index != PIGEN_INVALID_ID ?
					pigen_rtl_equation_add_with_owner(lowering->rtl, rtl_module, d->payload, adapted, edge->span).index != PIGEN_INVALID_ID :
					connect(lowering, rtl_module, d->payload, s->payload, edge->span);
				if (!payload_connected ||
					!connect(lowering, rtl_module, valid_destination, valid_source, edge->span) ||
					!connect(lowering, rtl_module, ready_source, ready_destination, edge->span)) return 0;
			}
			else if (!route_through_skid(lowering, module, rtl_module,
				source_payload, valid_source, ready_source, d->payload, valid_destination, ready_destination,
				edge->span, source)) return 0;
		}
	next_protocol_edge:;
	}
	return 1;
}

static int lower_protocol_fabrics(pigen_rtl_lowering *lowering,
	pigen_module_id module,pigen_rtl_module_id rtl_module)
{
	pigen_semantic_model *model=lowering->semantics;
	if(!model->protocol_fabric_edge_count) return 1;
	native_fabric_route *routes=pigen_resize(NULL,model->protocol_fabric_edge_count*sizeof(*routes));
	for(size_t e=0;e<model->protocol_fabric_edge_count;e++)
		for(size_t c=0;c<PIGEN_CHANNEL_COUNT;c++) routes[e].channels[c].payload.index=PIGEN_INVALID_ID;
	int ok=1;
	for(size_t e=0;ok && e<model->protocol_fabric_edge_count;e++)
	{
		const pigen_semantic_protocol_fabric_edge *edge=&model->protocol_fabric_edges[e];
		if(edge->module.index!=module.index || routes[e].channels[PIGEN_CHANNEL_BEAT].payload.index!=PIGEN_INVALID_ID ||
			routes[e].channels[PIGEN_CHANNEL_AW].payload.index!=PIGEN_INVALID_ID) continue;
		size_t count=0;
		for(size_t p=e;p<model->protocol_fabric_edge_count;p++)
			if(model->protocol_fabric_edges[p].source.index==edge->source.index) count++;
		const pigen_semantic_protocol_endpoint *source=pigen_protocol_endpoint_get(model,edge->source);
		const pigen_protocol *protocol=pigen_protocol_type_get(model,source->type);
		if(protocol->family==PIGEN_PROTOCOL_AXI4_STREAM)
		{if(count>1) ok=route_stream_demux(lowering,module,rtl_module,e,count,routes);}
		else if(edge->has_window) ok=route_memory_demux(lowering,module,rtl_module,e,count,routes);
	}
	if(ok) ok=lower_protocol_fabric_edges(lowering,module,rtl_module,routes);
	free(routes);return ok;
}

#endif

typedef struct {pigen_rtl_object_id payload,valid,ready;} tree_channel;
typedef struct {tree_channel in[3],out[3];} tree_router;
typedef struct {
 pigen_fabric_port description;
 tree_channel channel;
 const pigen_semantic_instance_connection *connection;
} tree_port;

static pigen_rtl_type_id tree_bits(pigen_rtl_lowering *l,size_t width)
{return pigen_lower_rtl_type(l,pigen_data_type_sized_logic(l->semantics,width,PIGEN_SIGN_UNSIGNED));}
static pigen_rtl_expr_id tree_number(pigen_rtl_lowering *l,size_t width,uint64_t n,pigen_source_span span)
{return pigen_rtl_expr_add_bits(l->rtl,tree_bits(l,width),n,span);}
static pigen_rtl_expr_id tree_index(pigen_rtl_lowering *l,size_t n,pigen_source_span span)
{
 pigen_rtl_literal_word word={(uint64_t)n,0,0};
 return pigen_rtl_expr_add_literal(l->rtl,PIGEN_RTL_EXPR_INTEGER,tree_bits(l,64),&word,64,0,span);
}
static char *tree_private_name(pigen_rtl_lowering *l,pigen_rtl_module_id m,const char *kind,uint32_t id)
{
 for(size_t suffix=0;;suffix++) {
  char name[96];snprintf(name,sizeof name,"__pigen_fabric_%s_%u_%zu",kind,id,suffix);
  int occupied=0;
  pigen_module_id module=l->rtl->modules[m.index].semantic_module;
  if(pigen_symbol_lookup_text(l->semantics,l->semantics->modules[module.index].scope,name,strlen(name)).index!=PIGEN_INVALID_ID)occupied=1;
  for(size_t i=0;i<l->rtl->object_count;i++)if(l->rtl->objects[i].module.index==m.index && l->rtl->objects[i].name && !strcmp(l->rtl->objects[i].name,name))occupied=1;
  for(size_t i=0;i<l->rtl->instance_count;i++)if(l->rtl->instances[i].module.index==m.index && l->rtl->instances[i].name && !strcmp(l->rtl->instances[i].name,name))occupied=1;
  if(!occupied)return pigen_copy_range(name,strlen(name));
 }
}
static pigen_rtl_object_id tree_wire(pigen_rtl_lowering *l,pigen_rtl_module_id m,pigen_rtl_type_id type,pigen_source_span span)
{
 pigen_rtl_object_id o=pigen_rtl_object_add_with_owner(l->rtl,m,PIGEN_RTL_OBJECT_VARIABLE,type,PIGEN_SEMANTIC_INTERNAL,(pigen_signal_id){PIGEN_INVALID_ID},span);
 if(o.index==PIGEN_INVALID_ID)return o;
 char *name=tree_private_name(l,m,"wire",o.index);pigen_rtl_set_name(l->rtl,&l->rtl->objects[o.index].name,name);free(name);return o;
}
static int tree_assign(pigen_rtl_lowering *l,pigen_rtl_module_id m,pigen_rtl_object_id d,pigen_rtl_expr_id v,pigen_source_span span)
{return v.index!=PIGEN_INVALID_ID && pigen_rtl_equation_add_with_owner(l->rtl,m,d,v,span).index!=PIGEN_INVALID_ID;}
static pigen_rtl_expr_id tree_slice(pigen_rtl_lowering *l,pigen_rtl_object_id o,pigen_rtl_type_id type,size_t low,size_t width,pigen_source_span span)
{
 return pigen_rtl_expr_add_select(l->rtl,type,fab_obj_expr(l,o,span),tree_index(l,low+width-1,span),tree_index(l,low,span),PIGEN_SEMANTIC_SELECT_RANGE,span);
}
static tree_channel tree_new_channel(pigen_rtl_lowering *l,pigen_rtl_module_id m,pigen_rtl_type_id packet,pigen_source_span span)
{return (tree_channel){tree_wire(l,m,packet,span),tree_wire(l,m,fab_control_type(l),span),tree_wire(l,m,fab_control_type(l),span)};}
static pigen_rtl_expr_id tree_value(pigen_rtl_lowering *l,tree_port source,tree_port destination,pigen_source_span span)
{
 pigen_conversion conversion;
 pigen_data_type_id from=l->semantics->signals[source.connection->port_signal.index].data_type;
 pigen_data_type_id to=l->semantics->signals[destination.connection->port_signal.index].data_type;
 if(!pigen_data_type_resolve_assignment_conversion(l->semantics,from,to,&conversion))return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
 pigen_rtl_expr_id value=fab_obj_expr(l,source.channel.payload,span);
 return conversion.kind==PIGEN_CONVERSION_IDENTITY ? value:pigen_rtl_expr_add_conversion(l->rtl,pigen_rtl_object_get(l->rtl,destination.channel.payload)->type,&conversion,value,span);
}
static int tree_join(pigen_rtl_lowering *l,pigen_rtl_module_id m,tree_channel destination,tree_channel source,pigen_source_span span)
{
 return connect(l,m,destination.payload,source.payload,span) && connect(l,m,destination.valid,source.valid,span) && connect(l,m,source.ready,destination.ready,span);
}
static char *tree_token_copy(const pigen_syntax_tree *s,pigen_token_id token)
{const char *text;size_t n;if(!token_text(s,token,&text,&n))return NULL;return pigen_copy_range(text,n);}
static size_t tree_add_port(pigen_rtl_lowering *l,const pigen_syntax_tree *syntax,pigen_module_id module,
 pigen_token_id instance,pigen_token_id port,int source,tree_port **ports,size_t *count)
{
 const pigen_semantic_instance *child=find_instance(l->semantics,module,syntax,instance);
 const pigen_semantic_instance_connection *connection=child ? find_port(l->semantics,child,syntax,port,NULL):NULL;
 if(!connection)return SIZE_MAX;
 for(size_t i=0;i<*count;i++)if((*ports)[i].connection==connection)return i;
 const pigen_semantic_instance_connection *valid=find_port(l->semantics,child,syntax,port,"_valid"),*ready=find_port(l->semantics,child,syntax,port,"_ready");
 tree_port item={{tree_token_copy(syntax,instance),tree_token_copy(syntax,port),source},
  {connection_payload(l,connection),connection_control(l,connection,valid,0),connection_control(l,connection,ready,1)},connection};
 *ports=pigen_resize(*ports,(*count+1)*sizeof(**ports));(*ports)[*count]=item;return (*count)++;
}
static int tree_make_router(pigen_rtl_lowering *l,pigen_module_id module,pigen_rtl_module_id m,
 tree_router *router,size_t packet_width,size_t path_width,pigen_source_span span)
{
 pigen_rtl_type_id packet=tree_bits(l,packet_width),vector=tree_bits(l,packet_width*3),control=fab_control_type(l),three=tree_bits(l,3);
 pigen_rtl_object_id input_ready=tree_wire(l,m,three,span),output_valid=tree_wire(l,m,three,span),output_packet=tree_wire(l,m,vector,span);
 pigen_rtl_expr_id payloads[3],valids[3],readies[3];
 for(size_t p=0;p<3;p++) {
  router->in[p]=tree_new_channel(l,m,packet,span);router->out[p]=tree_new_channel(l,m,packet,span);
  payloads[2-p]=fab_obj_expr(l,router->in[p].payload,span);valids[2-p]=fab_obj_expr(l,router->in[p].valid,span);readies[2-p]=fab_obj_expr(l,router->out[p].ready,span);
  if(!tree_assign(l,m,router->in[p].ready,tree_slice(l,input_ready,control,p,1,span),span) ||
   !tree_assign(l,m,router->out[p].valid,tree_slice(l,output_valid,control,p,1,span),span) ||
   !tree_assign(l,m,router->out[p].payload,tree_slice(l,output_packet,packet,p*packet_width,packet_width,span),span))return 0;
 }
 pigen_signal_id clk=module_input_named(l->semantics,module,"clk"),reset=module_input_named(l->semantics,module,"reset");
 if(clk.index==PIGEN_INVALID_ID)return 0;
 pigen_rtl_expr_id parameters[]={tree_number(l,32,path_width,span)};
 pigen_rtl_instance_id instance=pigen_rtl_instance_add_with_owner(l->rtl,m,module,parameters,1,NULL,0,span);
 if(instance.index==PIGEN_INVALID_ID)return 0;
 char *name=tree_private_name(l,m,"router",instance.index);pigen_rtl_set_name(l->rtl,&l->rtl->instances[instance.index].name,name);free(name);
 pigen_rtl_connection c[]={
  {"clk",l->lowered_endpoints[clk.index].payload,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
  {"reset",{PIGEN_INVALID_ID},reset.index==PIGEN_INVALID_ID ? tree_number(l,1,0,span):fab_obj_expr(l,l->lowered_endpoints[reset.index].payload,span),PIGEN_SEMANTIC_INTERNAL},
  {"packet_in",{PIGEN_INVALID_ID},pigen_rtl_expr_add_concatenation(l->rtl,vector,payloads,3,span),PIGEN_SEMANTIC_INTERNAL},
  {"in_valid",{PIGEN_INVALID_ID},pigen_rtl_expr_add_concatenation(l->rtl,three,valids,3,span),PIGEN_SEMANTIC_INTERNAL},
  {"out_ready",{PIGEN_INVALID_ID},pigen_rtl_expr_add_concatenation(l->rtl,three,readies,3,span),PIGEN_SEMANTIC_INTERNAL},
  {"in_ready",input_ready,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
  {"out_valid",output_valid,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL},
  {"packet_out",output_packet,{PIGEN_INVALID_ID},PIGEN_SEMANTIC_INTERNAL}};
 pigen_rtl_record_range records=pigen_rtl_connection_range_add(l->rtl,c,sizeof c/sizeof *c);
 return pigen_rtl_instance_set_primitive(l->rtl,instance,PIGEN_RTL_PRIMITIVE_TREE_ROUTER,packet,records);
}

int pigen_fabric_lower(pigen_rtl_lowering *l,pigen_module_id module,const pigen_syntax_tree *syntax)
{
 if(!l || !l->semantics || !l->rtl || !syntax || l->semantics->protocol_fabric_edge_count)return -1;
 const pigen_semantic_module *owner=pigen_module_get(l->semantics,module);if(!owner)return -1;
 pigen_rtl_module_id m={PIGEN_INVALID_ID};for(size_t i=0;i<l->rtl->module_count;i++)if(l->rtl->modules[i].semantic_module.index==module.index)m=(pigen_rtl_module_id){(uint32_t)i};
 if(m.index==PIGEN_INVALID_ID)return -1;
 tree_port *ports=NULL;size_t port_count=0,edge_count=0,payload_width=0;
 pigen_fabric_edge *edges=NULL;pigen_fabric_tree tree={0};tree_router *routers=NULL;
 pigen_source_span span={0};int ok=0;
 for(pigen_syntax_id edge=next_fabric_edge(syntax,owner->syntax,(pigen_syntax_id){PIGEN_INVALID_ID});edge.index!=PIGEN_INVALID_ID;edge=next_fabric_edge(syntax,owner->syntax,edge)) {
  const pigen_syntax_node *node=pigen_syntax_get(syntax,edge);if(!node || node->kind!=PIGEN_SYNTAX_FABRIC_CONNECTION)continue;
  span=node->location.source_span;
  size_t s=tree_add_port(l,syntax,module,node->as.fabric_connection.left_instance,node->as.fabric_connection.left_port,1,&ports,&port_count);
  size_t d=tree_add_port(l,syntax,module,node->as.fabric_connection.right_instance,node->as.fabric_connection.right_port,0,&ports,&port_count);
  if(s==SIZE_MAX || d==SIZE_MAX)goto done;
  tree_channel source=ports[s].channel,destination=ports[d].channel;
  if(source.payload.index==PIGEN_INVALID_ID || destination.payload.index==PIGEN_INVALID_ID)goto done;
  int direct=node->as.fabric_connection.direct;
  int controls=(source.valid.index!=PIGEN_INVALID_ID)+(destination.valid.index!=PIGEN_INVALID_ID)+
   (source.ready.index!=PIGEN_INVALID_ID)+(destination.ready.index!=PIGEN_INVALID_ID);
  if(controls && controls!=4)goto done;
  if(!controls)direct=1;
  edges=pigen_resize(edges,(edge_count+1)*sizeof(*edges));edges[edge_count++]=(pigen_fabric_edge){s,d,node->as.fabric_connection.tier,direct};
  if(direct) {
   if(!tree_assign(l,m,destination.payload,tree_value(l,ports[s],ports[d],span),span))goto done;
   if(source.valid.index!=PIGEN_INVALID_ID && destination.valid.index!=PIGEN_INVALID_ID && !connect(l,m,destination.valid,source.valid,span))goto done;
   if(source.ready.index!=PIGEN_INVALID_ID && destination.ready.index!=PIGEN_INVALID_ID && !connect(l,m,source.ready,destination.ready,span))goto done;
  } else {
   size_t width=pigen_rtl_type_get(l->rtl,pigen_rtl_object_get(l->rtl,destination.payload)->type)->width;
   if(!width)goto done;
   if(width>payload_width)payload_width=width;
  }
 }
 if(!edge_count){ok=1;goto done;}
 pigen_fabric_port *descriptions=pigen_resize(NULL,port_count*sizeof(*descriptions));for(size_t i=0;i<port_count;i++)descriptions[i]=ports[i].description;
 const pigen_symbol *module_symbol=pigen_symbol_get(l->semantics,owner->symbol);size_t name_length=0;
 const char *name_text=module_symbol ? pigen_source_span_text(l->semantics->sources,module_symbol->name,&name_length):NULL;
 char *name=name_text ? pigen_copy_range(name_text,name_length):pigen_copy_range("fabric",6);
 int built=pigen_build_fabric_tree(name,descriptions,port_count,edges,edge_count,&tree);free(descriptions);
 if(!built){free(name);goto done;}
 size_t packet_width=payload_width+tree.path_width;pigen_rtl_type_id packet=tree.router_count ? tree_bits(l,packet_width):fab_control_type(l);
 routers=pigen_resize(NULL,tree.router_count*sizeof(*routers));
 for(size_t r=0;r<tree.router_count;r++)if(!tree_make_router(l,module,m,&routers[r],packet_width,tree.path_width,span)){free(name);goto done;}
 for(size_t r=0;r<tree.router_count;r++)for(size_t p=0;p<3;p++) {
  pigen_fabric_attachment a=tree.routers[r].ports[p];tree_channel in=routers[r].in[p],out=routers[r].out[p];
  if(a.kind==1) {if(!tree_join(l,m,in,routers[a.target].out[a.port],span)){free(name);goto done;}}
  else {
   if(a.kind==0 || !ports[a.target].description.source) {
    if(!tree_assign(l,m,in.payload,tree_number(l,packet_width,0,span),span) || !tree_assign(l,m,in.valid,tree_number(l,1,0,span),span)){free(name);goto done;}
   }
   if(a.kind==0 || ports[a.target].description.source)if(!tree_assign(l,m,out.ready,tree_number(l,1,0,span),span)){free(name);goto done;}
  }
 }
 for(size_t i=0;i<port_count;i++)if(tree.port_router[i]>=0) {
  tree_port port=ports[i];tree_channel endpoint=port.channel;size_t r=(size_t)tree.port_router[i],p=(size_t)tree.port_number[i];
  pigen_rtl_type_id data_type=pigen_rtl_object_get(l->rtl,endpoint.payload)->type;
  size_t width=pigen_rtl_type_get(l->rtl,data_type)->width;
  pigen_rtl_object_id packed=tree_wire(l,m,packet,span);
  if(port.description.source) {
   size_t e=0;while(e<edge_count && (edges[e].direct || edges[e].source!=i))e++;
   if(e==edge_count){free(name);goto done;}
   width=pigen_rtl_type_get(l->rtl,pigen_rtl_object_get(l->rtl,ports[edges[e].destination].channel.payload)->type)->width;
   pigen_rtl_expr_id parts[3];size_t n=0;parts[n++]=tree_number(l,tree.path_width,tree.paths[e],span);
   if(width<payload_width)parts[n++]=tree_number(l,payload_width-width,0,span);
   parts[n++]=tree_value(l,port,ports[edges[e].destination],span);
   if(!tree_assign(l,m,packed,pigen_rtl_expr_add_concatenation(l->rtl,packet,parts,n,span),span) ||
    !route_through_skid(l,module,m,packed,endpoint.valid,endpoint.ready,routers[r].in[p].payload,routers[r].in[p].valid,routers[r].in[p].ready,span,NULL)){free(name);goto done;}
  } else {
   if(!route_through_skid(l,module,m,routers[r].out[p].payload,routers[r].out[p].valid,routers[r].out[p].ready,packed,endpoint.valid,endpoint.ready,span,NULL) ||
    !tree_assign(l,m,endpoint.payload,tree_slice(l,packed,data_type,0,width,span),span)){free(name);goto done;}
  }
 }
 pigen_fabric_diagrams *diagrams=&l->rtl->fabric_diagrams;
 diagrams->items=pigen_resize(diagrams->items,(diagrams->count+1)*sizeof(*diagrams->items));
 diagrams->items[diagrams->count++]=(pigen_fabric_diagram){name,tree.svg};tree.svg=NULL;ok=1;
 done:
 for(size_t i=0;i<port_count;i++){free((void *)ports[i].description.instance);free((void *)ports[i].description.port);}
 free(ports);free(edges);free(routers);pigen_free_fabric_tree(&tree);return ok ? 0:-1;
}
