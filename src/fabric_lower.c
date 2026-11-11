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

static int token_equal(const pigen_syntax_tree *syntax, pigen_token_id a,
	pigen_token_id b)
{
	const char *at, *bt;
	size_t al = 0, bl = 0;
	if (!token_text(syntax, a, &at, &al) ||
		!token_text(syntax, b, &bt, &bl)) return 0;
	return al == bl && !memcmp(at, bt, al);
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
	const pigen_semantic_expr *value = connection ? pigen_expr_get(lowering->semantics, connection->value) : NULL;
	while(value && value->kind==PIGEN_EXPR_CONVERSION)
		value=pigen_expr_get(lowering->semantics,value->as.conversion.operand);
	if(value && value->kind==PIGEN_EXPR_SYMBOL)
	{
		pigen_signal_id signal=pigen_symbol_signal(lowering->semantics,value->as.symbol);
		if(signal.index<lowering->lowered_endpoint_count)
		{
			const pigen_rtl_signal_endpoints *endpoint=&lowering->lowered_endpoints[signal.index];
			const pigen_rtl_expr *expr=pigen_rtl_expr_get(lowering->rtl,ready?endpoint->ready:endpoint->valid);
			if(expr && expr->kind==PIGEN_RTL_EXPR_OBJECT) return expr->object;
		}
	}
	return (pigen_rtl_object_id){PIGEN_INVALID_ID};
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
	pigen_source_span span)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
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
#define PORT(p, o) connections[count++] = (pigen_rtl_connection){(p), (o), (pigen_rtl_expr_id){PIGEN_INVALID_ID}}
#define VAL(p, v) connections[count++] = (pigen_rtl_connection){(p), (pigen_rtl_object_id){PIGEN_INVALID_ID}, (v)}
	PORT("clk", lowering->lowered_endpoints[clk.index].payload);
	if (reset.index != PIGEN_INVALID_ID &&
		lowering->lowered_endpoint_count > reset.index)
		PORT("reset", lowering->lowered_endpoints[reset.index].payload);
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

static int route_through_merge2(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module,
	pigen_rtl_object_id p0, pigen_rtl_object_id v0, pigen_rtl_object_id r0,
	pigen_rtl_object_id p1, pigen_rtl_object_id v1, pigen_rtl_object_id r1,
	pigen_rtl_object_id dp, pigen_rtl_object_id dv, pigen_rtl_object_id dr,
	pigen_source_span span)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
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
#define PORT(p, o) connections[count++] = (pigen_rtl_connection){(p), (o), (pigen_rtl_expr_id){PIGEN_INVALID_ID}}
#define VAL(p, v) connections[count++] = (pigen_rtl_connection){(p), (pigen_rtl_object_id){PIGEN_INVALID_ID}, (v)}
	PORT("clk", lowering->lowered_endpoints[clk.index].payload);
	if (reset.index != PIGEN_INVALID_ID &&
		lowering->lowered_endpoint_count > reset.index)
		PORT("reset", lowering->lowered_endpoints[reset.index].payload);
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
	pigen_rtl_object_id dest_ready, pigen_source_span span)
{
	pigen_semantic_model *model = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id clk = module_input_named(model, module, "clk");
	pigen_signal_id reset = module_input_named(model, module, "reset");
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
		pigen_rtl_literal_word word = {(uint64_t)i, 0, 0};
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
	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module, module,
		&parameter, 1, NULL, 0, span);
	if (instance.index == PIGEN_INVALID_ID) return 0;
	connections[0] = (pigen_rtl_connection){"clk", lowering->lowered_endpoints[clk.index].payload, {PIGEN_INVALID_ID}};
	connections[1] = (pigen_rtl_connection){"reset", reset.index != PIGEN_INVALID_ID && lowering->lowered_endpoint_count > reset.index ? lowering->lowered_endpoints[reset.index].payload : (pigen_rtl_object_id){PIGEN_INVALID_ID}, {PIGEN_INVALID_ID}};
	connections[2] = (pigen_rtl_connection){"in_valid", valid_object, {PIGEN_INVALID_ID}};
	connections[3] = (pigen_rtl_connection){"in_ready", ready_object, {PIGEN_INVALID_ID}};
	connections[4] = (pigen_rtl_connection){"packet_in", payload_object, {PIGEN_INVALID_ID}};
	connections[5] = (pigen_rtl_connection){"out_valid", dest_valid, {PIGEN_INVALID_ID}};
	connections[6] = (pigen_rtl_connection){"out_ready", dest_ready, {PIGEN_INVALID_ID}};
	connections[7] = (pigen_rtl_connection){"packet_out", dest_payload, {PIGEN_INVALID_ID}};
	range = pigen_rtl_connection_range_add(rtl, connections, 8);
	if (range.first == (size_t)PIGEN_INVALID_ID) return 0;
	if (!pigen_rtl_instance_set_primitive(rtl, instance,
			PIGEN_RTL_PRIMITIVE_MERGE, payload->type, range))
		return 0;
	written = snprintf(name, sizeof(name), "fabric_mergeN_%u", instance.index);
	if (written > 0 && (size_t)written < sizeof(name))
		pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name, name);
	return 1;
}

int pigen_fabric_lower(pigen_rtl_lowering *lowering, pigen_module_id module,
	const pigen_syntax_tree *syntax)
{
	pigen_semantic_model *model;
	pigen_rtl_model *rtl;
	const pigen_semantic_module *owner;
	pigen_rtl_module_id rtl_module = (pigen_rtl_module_id){PIGEN_INVALID_ID};
	const pigen_syntax_node *module_node;
	pigen_syntax_id child;
	size_t i;

	if (!lowering || !lowering->semantics || !lowering->rtl || !syntax)
		return -1;
	model = lowering->semantics;
	rtl = lowering->rtl;
	owner = pigen_module_get(model, module);
	if (!owner) return -1;
	module_node = pigen_syntax_get(syntax, owner->syntax);
	if (!module_node) return -1;
	for (i = 0; i < lowering->rtl->module_count; i++)
		if (lowering->rtl->modules[i].semantic_module.index == module.index)
		{
			rtl_module = (pigen_rtl_module_id){(uint32_t)i};
			break;
		}
	if (rtl_module.index == PIGEN_INVALID_ID) return -1;

	for (child = module_node->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, child);
		pigen_syntax_id edge;

		if (!node) break;
		if (node->kind != PIGEN_SYNTAX_FABRIC)
		{
			child = node->next_sibling;
			continue;
		}
		for (edge = node->first_child; edge.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *first = pigen_syntax_get(syntax, edge);
			pigen_syntax_id other;

			if (!first) break;
			if (first->kind == PIGEN_SYNTAX_FABRIC_CONNECTION &&
				first->as.fabric_connection.direct)
				for (other = first->next_sibling;
					other.index != PIGEN_INVALID_ID; )
				{
					const pigen_syntax_node *second = pigen_syntax_get(syntax,
						other);

					if (!second) break;
					if (second->kind == PIGEN_SYNTAX_FABRIC_CONNECTION &&
						token_equal(syntax,
							first->as.fabric_connection.right_instance,
							second->as.fabric_connection.right_instance) &&
						token_equal(syntax,
							first->as.fabric_connection.right_port,
							second->as.fabric_connection.right_port))
						pigen_fail("direct fabric destination cannot appear in "
							"another connection");
					other = second->next_sibling;
				}
			edge = first->next_sibling;
		}
		for (edge = node->first_child; edge.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *connection = pigen_syntax_get(syntax,
				edge);
			const pigen_semantic_instance *source;
			const pigen_semantic_instance *destination;
			const pigen_semantic_instance_connection *sp, *sv, *sr;
			const pigen_semantic_instance_connection *dp, *dv, *dr;
			pigen_rtl_object_id sp_obj, sv_obj, sr_obj, dp_obj, dv_obj, dr_obj;

			if (!connection) break;
			if (connection->kind != PIGEN_SYNTAX_FABRIC_CONNECTION)
			{
				edge = connection->next_sibling;
				continue;
			}
			source = find_instance(model, module, syntax,
				connection->as.fabric_connection.left_instance);
			destination = find_instance(model, module, syntax,
				connection->as.fabric_connection.right_instance);
			if (!source || !destination) return -1;
			sp = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, NULL);
			sv = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, "_valid");
			sr = find_port(model, source, syntax,
				connection->as.fabric_connection.left_port, "_ready");
			dp = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, NULL);
			dv = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, "_valid");
			dr = find_port(model, destination, syntax,
				connection->as.fabric_connection.right_port, "_ready");
			if (!sp || !dp) return -1;
			sp_obj = connection_payload(lowering, sp);
			dp_obj = connection_payload(lowering, dp);
			sv_obj = connection_payload(lowering, sv);
			dv_obj = connection_payload(lowering, dv);
			sr_obj = connection_payload(lowering, sr);
			dr_obj = connection_payload(lowering, dr);
			if (sv_obj.index==PIGEN_INVALID_ID) sv_obj=endpoint_control(lowering,sp,0);
			if (sr_obj.index==PIGEN_INVALID_ID) sr_obj=endpoint_control(lowering,sp,1);
			if (dv_obj.index==PIGEN_INVALID_ID) dv_obj=endpoint_control(lowering,dp,0);
			if (dr_obj.index==PIGEN_INVALID_ID) dr_obj=endpoint_control(lowering,dp,1);
			if (sp_obj.index == PIGEN_INVALID_ID ||
				dp_obj.index == PIGEN_INVALID_ID)
				return -1;
			if (!connection->as.fabric_connection.direct)
			{
				pigen_syntax_id prior;
				pigen_syntax_id scan;
				size_t group_count = 0;
				pigen_rtl_object_id sources_p[32], sources_v[32], sources_r[32];
				pigen_source_span span = connection->location.source_span;

				for (prior = node->first_child; prior.index !=
					PIGEN_INVALID_ID && prior.index != edge.index; )
				{
					const pigen_syntax_node *p = pigen_syntax_get(syntax,
						prior);
					if (!p) break;
					if (p->kind == PIGEN_SYNTAX_FABRIC_CONNECTION &&
						!p->as.fabric_connection.direct &&
						token_equal(syntax,
							p->as.fabric_connection.right_instance,
							connection->as.fabric_connection.right_instance) &&
						token_equal(syntax,
							p->as.fabric_connection.right_port,
							connection->as.fabric_connection.right_port))
						break;
					prior = p->next_sibling;
				}
				if (prior.index != PIGEN_INVALID_ID &&
					prior.index != edge.index)
				{
					edge = connection->next_sibling;
					continue;
				}
				for (scan = edge; scan.index != PIGEN_INVALID_ID; )
				{
					const pigen_syntax_node *q = pigen_syntax_get(syntax, scan);
					if (!q) break;
					if (q->kind == PIGEN_SYNTAX_FABRIC_CONNECTION &&
						!q->as.fabric_connection.direct &&
						token_equal(syntax,
							q->as.fabric_connection.right_instance,
							connection->as.fabric_connection.right_instance) &&
						token_equal(syntax,
							q->as.fabric_connection.right_port,
							connection->as.fabric_connection.right_port))
						group_count++;
					scan = q->next_sibling;
				}
				if (group_count > 32) return -1;
				if (group_count == 1)
				{
					if (sv_obj.index == PIGEN_INVALID_ID ||
						dv_obj.index == PIGEN_INVALID_ID ||
						sr_obj.index == PIGEN_INVALID_ID ||
						dr_obj.index == PIGEN_INVALID_ID)
						return -1;
					if (!route_through_skid(lowering, module, rtl_module,
						sp_obj, sv_obj, sr_obj, dp_obj, dv_obj, dr_obj,
						span))
						return -1;
				}
				else if (group_count >= 2)
				{
					size_t c = 0;

					for (scan = edge; scan.index != PIGEN_INVALID_ID &&
						c < group_count; )
					{
						const pigen_syntax_node *q = pigen_syntax_get(syntax,
							scan);
						const pigen_semantic_instance *sinst;
						const pigen_semantic_instance_connection *qp, *qv, *qr;
						pigen_rtl_object_id lsp, lsv, lsr;

						if (!q) break;
						if (q->kind != PIGEN_SYNTAX_FABRIC_CONNECTION ||
							q->as.fabric_connection.direct ||
							!token_equal(syntax,
								q->as.fabric_connection.right_instance,
								connection->as.fabric_connection.right_instance) ||
							!token_equal(syntax,
								q->as.fabric_connection.right_port,
								connection->as.fabric_connection.right_port))
						{
							scan = q->next_sibling;
							continue;
						}
						sinst = find_instance(model, module, syntax,
							q->as.fabric_connection.left_instance);
						if (!sinst) return -1;
						qp = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, NULL);
						qv = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, "_valid");
						qr = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, "_ready");
						lsp = connection_payload(lowering, qp);
						lsv = connection_payload(lowering, qv);
						lsr = connection_payload(lowering, qr);
						if (lsp.index == PIGEN_INVALID_ID ||
							lsv.index == PIGEN_INVALID_ID ||
							lsr.index == PIGEN_INVALID_ID)
							return -1;
						sources_p[c] = lsp;
						sources_v[c] = lsv;
						sources_r[c] = lsr;
						c++;
						scan = q->next_sibling;
					}
					if (c != group_count) return -1;
					if (group_count == 2)
					{
						if (!route_through_merge2(lowering, module, rtl_module,
							sources_p[0], sources_v[0], sources_r[0],
							sources_p[1], sources_v[1], sources_r[1],
							dp_obj, dv_obj, dr_obj, span))
							return -1;
					}
					else if (!route_through_mergeN(lowering, module,
						rtl_module, sources_p, sources_v, sources_r,
						group_count, dp_obj, dv_obj, dr_obj, span))
						return -1;
				}
				else
				{
					pigen_rtl_type_id control = fab_control_type(lowering);
					const pigen_rtl_object *dest_payload_obj =
						pigen_rtl_object_get(rtl, dp_obj);
					pigen_rtl_type_id dest_type = dest_payload_obj ?
						dest_payload_obj->type :
						(pigen_rtl_type_id){PIGEN_INVALID_ID};
					pigen_rtl_expr_id valid = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
					pigen_rtl_expr_id payload = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
					pigen_rtl_expr_id dest_ready = fab_obj_expr(lowering,
						dr_obj, span);
					size_t c = 0;

					if (control.index == PIGEN_INVALID_ID ||
						dest_type.index == PIGEN_INVALID_ID ||
						dest_ready.index == PIGEN_INVALID_ID)
						return -1;
					for (scan = edge; scan.index != PIGEN_INVALID_ID &&
						c < group_count; )
					{
						const pigen_syntax_node *q = pigen_syntax_get(syntax,
							scan);
						const pigen_semantic_instance *sinst;
						const pigen_semantic_instance_connection *qp, *qv, *qr;
						pigen_rtl_object_id lsp, lsv, lsr;

						if (!q) break;
						if (q->kind != PIGEN_SYNTAX_FABRIC_CONNECTION ||
							q->as.fabric_connection.direct ||
							!token_equal(syntax,
								q->as.fabric_connection.right_instance,
								connection->as.fabric_connection.right_instance) ||
							!token_equal(syntax,
								q->as.fabric_connection.right_port,
								connection->as.fabric_connection.right_port))
						{
							scan = q->next_sibling;
							continue;
						}
						sinst = find_instance(model, module, syntax,
							q->as.fabric_connection.left_instance);
						if (!sinst) return -1;
						qp = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, NULL);
						qv = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, "_valid");
						qr = find_port(model, sinst, syntax,
							q->as.fabric_connection.left_port, "_ready");
						lsp = connection_payload(lowering, qp);
						lsv = connection_payload(lowering, qv);
						lsr = connection_payload(lowering, qr);
						if (lsp.index == PIGEN_INVALID_ID ||
							lsv.index == PIGEN_INVALID_ID ||
							lsr.index == PIGEN_INVALID_ID)
							return -1;
						sources_p[c] = lsp;
						sources_v[c] = lsv;
						sources_r[c] = lsr;
						c++;
						scan = q->next_sibling;
					}
					if (c != group_count) return -1;
					for (c = 0; c < group_count; c++)
					{
						pigen_rtl_expr_id v = fab_obj_expr(lowering,
							sources_v[c], span);
						valid = valid.index == PIGEN_INVALID_ID ? v :
							fab_logic(lowering, PIGEN_BINARY_LOGICAL_OR,
								valid, v, control, span);
						if (valid.index == PIGEN_INVALID_ID) return -1;
					}
					{
						pigen_rtl_literal_word word = {0, 0, 0};
						payload = pigen_rtl_expr_add_literal(rtl,
							PIGEN_RTL_EXPR_INTEGER, dest_type, &word, 1, 0,
							span);
						if (payload.index == PIGEN_INVALID_ID) return -1;
					}
					for (c = group_count; c-- > 0; )
					{
						pigen_rtl_expr_id cond = fab_obj_expr(lowering,
							sources_v[c], span);
						pigen_conditional_resolution resolution = {0};
						pigen_rtl_expr_id value = fab_obj_expr(lowering,
							sources_p[c], span);

						if (cond.index == PIGEN_INVALID_ID ||
							value.index == PIGEN_INVALID_ID)
							return -1;
						payload = pigen_rtl_expr_add_conditional(rtl,
							dest_type, &resolution, cond, value, payload,
							span);
						if (payload.index == PIGEN_INVALID_ID) return -1;
					}
					if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
						dp_obj, payload, span).index == PIGEN_INVALID_ID ||
						pigen_rtl_equation_add_with_owner(rtl, rtl_module,
							dv_obj, valid, span).index == PIGEN_INVALID_ID)
						return -1;
					{
						pigen_rtl_expr_id earlier = (pigen_rtl_expr_id){
							PIGEN_INVALID_ID};

						for (c = 0; c < group_count; c++)
						{
							pigen_rtl_expr_id v = fab_obj_expr(lowering,
								sources_v[c], span);
							pigen_rtl_expr_id grant;
							pigen_rtl_expr_id ready;

							grant = earlier.index == PIGEN_INVALID_ID ? v :
								fab_logic(lowering,
									PIGEN_BINARY_LOGICAL_AND, v,
									fab_not(lowering, earlier, control,
										span), control, span);
							if (grant.index == PIGEN_INVALID_ID) return -1;
							ready = fab_logic(lowering,
								PIGEN_BINARY_LOGICAL_AND, grant,
								dest_ready, control, span);
							if (ready.index == PIGEN_INVALID_ID ||
								pigen_rtl_equation_add_with_owner(rtl,
									rtl_module, sources_r[c], ready,
									span).index == PIGEN_INVALID_ID)
								return -1;
							earlier = earlier.index == PIGEN_INVALID_ID ? v :
								fab_logic(lowering, PIGEN_BINARY_LOGICAL_OR,
									earlier, v, control, span);
							if (earlier.index == PIGEN_INVALID_ID) return -1;
						}
					}
				}
			}
			else
			{
				if (!connect(lowering, rtl_module, dp_obj, sp_obj,
					connection->location.source_span)) return -1;
				if (sv_obj.index != PIGEN_INVALID_ID &&
					dv_obj.index != PIGEN_INVALID_ID &&
					!connect(lowering, rtl_module, dv_obj, sv_obj,
						connection->location.source_span)) return -1;
				if (sr_obj.index != PIGEN_INVALID_ID &&
					dr_obj.index != PIGEN_INVALID_ID &&
					!connect(lowering, rtl_module, sr_obj, dr_obj,
						connection->location.source_span)) return -1;
			}
			edge = connection->next_sibling;
		}
		child = node->next_sibling;
	}
	return 0;
}
