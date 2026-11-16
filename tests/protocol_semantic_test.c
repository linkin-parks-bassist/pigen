#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/integer.h"

#define INVALID(type) ((type){PIGEN_INVALID_ID})

static pigen_source_span name_span(pigen_source_manager *sources, const char *name)
{
	pigen_source_id source = pigen_source_add(sources, name, name, strlen(name));
	assert(source.index != PIGEN_INVALID_ID);
	return (pigen_source_span){source, 0, strlen(name)};
}

static pigen_symbol_id declare(pigen_semantic_model *model, pigen_scope_id scope,
	pigen_symbol_kind kind, pigen_data_type_id type, pigen_source_span span)
{
	pigen_symbol_id symbol;
	assert(pigen_symbol_declare(model, scope, kind, type, span, span,
		&symbol, NULL) == PIGEN_DECLARE_OK);
	return symbol;
}

static pigen_const_expr_id bound(pigen_semantic_model *model, int64_t value)
{
	pigen_integer_id integer = pigen_integer_intern_u64(model,
		value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value);
	if (value < 0) integer = pigen_integer_negate(model, integer);
	return pigen_const_expr_intern_exact_integer(model, integer,
		pigen_data_type_exact_integer(model, integer));
}

static void array_case(pigen_semantic_model *model, pigen_source_manager *sources,
	pigen_scope_id scope, pigen_protocol_endpoint_id original)
{
	pigen_semantic_protocol_endpoint prototype = *pigen_protocol_endpoint_get(model, original);
	pigen_protocol_endpoint_id lanes[12];
	for (size_t n = 0; n < 12; n++)
	{
		char name[64]; snprintf(name, sizeof(name), "lane_%zu", n);
		pigen_semantic_protocol_endpoint endpoint = prototype;
		endpoint.binding.is_bound = 0;
		if (n >= 4) endpoint.role = prototype.role == PIGEN_PROTOCOL_TARGET ?
			PIGEN_PROTOCOL_INITIATOR : PIGEN_PROTOCOL_TARGET;
		endpoint.span = name_span(sources, name);
		endpoint.symbol = declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
			INVALID(pigen_data_type_id), endpoint.span);
		for (size_t c = 0; c < PIGEN_CHANNEL_COUNT; c++)
		{
			if (prototype.channels[c].index == PIGEN_INVALID_ID) continue;
			pigen_semantic_signal signal = *pigen_signal_get(model, prototype.channels[c]);
			if (n >= 4 && prototype.is_port) signal.direction = signal.direction == PIGEN_SEMANTIC_INPUT ?
				PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT;
			snprintf(name, sizeof(name), "lane_%zu_channel_%zu", n, c);
			pigen_source_span span = name_span(sources, name);
			pigen_symbol_id symbol = declare(model, scope, PIGEN_SYMBOL_SIGNAL, signal.data_type, span);
			endpoint.channels[c] = pigen_signal_add(model, signal.syntax, signal.module,
				symbol, signal.data_type, signal.shape, INVALID(pigen_expr_id),
				signal.transfer_type, signal.direction, span);
		}
		lanes[n] = pigen_protocol_endpoint_add(model, &endpoint);
		assert(lanes[n].index != PIGEN_INVALID_ID);
	}
	pigen_shape_dimension dimensions[2] = {
		{.form = PIGEN_SHAPE_DIMENSION_RANGE, .as.range = {bound(model, -2), bound(model, -1)}},
		{.form = PIGEN_SHAPE_DIMENSION_COUNT, .as.count = bound(model, 2)}
	};
	pigen_source_span span = name_span(sources, "array_owner");
	pigen_semantic_protocol_array array = {
		.syntax = {10}, .module = prototype.module, .type = prototype.type,
		.role = prototype.role, .is_port = prototype.is_port, .span = span,
		.symbol = declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ARRAY,
			INVALID(pigen_data_type_id), span),
		.shape = pigen_shape_intern(model, dimensions, 2)
	};
	assert(pigen_protocol_array_add(model, &array, lanes, 3).index == PIGEN_INVALID_ID);
	pigen_protocol_endpoint_id duplicate[] = {lanes[0], lanes[1], lanes[2], lanes[0]};
	assert(pigen_protocol_array_add(model, &array, duplicate, 4).index == PIGEN_INVALID_ID);
	pigen_semantic_protocol_array bad = array; bad.role = (pigen_protocol_role)99;
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	bad = array; bad.is_port = !array.is_port;
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	bad = array;
	pigen_protocol other_protocol = *pigen_protocol_type_get(model, array.type);
	other_protocol.data_bits = 64;
	bad.type = pigen_protocol_type_intern(model, &other_protocol);
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	bad = array; bad.shape = pigen_semantic_scalar_shape(model);
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	pigen_shape_dimension invalid_dimension = {.form = PIGEN_SHAPE_DIMENSION_COUNT, .as.count = bound(model, 0)};
	bad = array; bad.shape = pigen_shape_intern(model, &invalid_dimension, 1);
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	invalid_dimension = (pigen_shape_dimension){.form = PIGEN_SHAPE_DIMENSION_RANGE,
		.as.range = {bound(model, INT64_MIN), bound(model, INT64_MAX)}};
	bad.shape = pigen_shape_intern(model, &invalid_dimension, 1);
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	bad = array; bad.role = prototype.role == PIGEN_PROTOCOL_TARGET ? PIGEN_PROTOCOL_INITIATOR : PIGEN_PROTOCOL_TARGET;
	assert(pigen_protocol_array_add(model, &bad, lanes, 4).index == PIGEN_INVALID_ID);
	assert(model->protocol_array_count == 0 && model->protocol_array_element_count == 0);
	assert(model->protocol_array_dimension_count == 0);
	assert(pigen_symbol_protocol_array(model, array.symbol).index == PIGEN_INVALID_ID);
	pigen_protocol_array_id id = pigen_protocol_array_add(model, &array, lanes, 4);
	assert(id.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_protocol_array(model, array.symbol).index == id.index);
	assert(pigen_symbol_protocol_endpoint(model, array.symbol).index == PIGEN_INVALID_ID);
	for (size_t n = 0; n < 4; n++)
	{
		int64_t coordinates[] = {-2 + (int64_t)(n / 2), 1 - (int64_t)(n % 2)};
		assert(pigen_protocol_array_element(model, id, coordinates, 2).index == lanes[n].index);
		size_t ordinal = 99;
		assert(pigen_protocol_array_for_endpoint(model, lanes[n], &ordinal).index == id.index);
		assert(ordinal == n);
	}
	assert(pigen_protocol_array_element(model, id, (int64_t[]){-3, 1}, 2).index == PIGEN_INVALID_ID);
	assert(pigen_protocol_array_element(model, id, (int64_t[]){-2, 2}, 2).index == PIGEN_INVALID_ID);
	assert(pigen_protocol_array_element(model, id, (int64_t[]){-2}, 1).index == PIGEN_INVALID_ID);
	assert(!pigen_protocol_arrays_can_connect(model, id, id));
	assert(pigen_protocol_array_for_endpoint(model, original, NULL).index == PIGEN_INVALID_ID);
	assert(pigen_protocol_array_element(model, INVALID(pigen_protocol_array_id), (int64_t[]){0}, 1).index == PIGEN_INVALID_ID);
	assert(pigen_protocol_array_element(model, id, NULL, 2).index == PIGEN_INVALID_ID);

	span = name_span(sources, "second_array_owner");
	array.symbol = declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ARRAY, INVALID(pigen_data_type_id), span);
	array.span = span;
	assert(pigen_protocol_array_add(model, &array, lanes, 4).index == PIGEN_INVALID_ID);
	assert(model->protocol_array_count == 1 && model->protocol_array_element_count == 4);
	array.role = prototype.role == PIGEN_PROTOCOL_TARGET ? PIGEN_PROTOCOL_INITIATOR : PIGEN_PROTOCOL_TARGET;
	/* Explicit descending bounds and count dimensions describe the same shape. */
	dimensions[1] = (pigen_shape_dimension){.form = PIGEN_SHAPE_DIMENSION_RANGE,
		.as.range = {bound(model, 1), bound(model, 0)}};
	array.shape = pigen_shape_intern(model, dimensions, 2);
	pigen_protocol_array_id target = pigen_protocol_array_add(model, &array, lanes + 4, 4);
	assert(target.index != PIGEN_INVALID_ID);
	assert(pigen_protocol_arrays_can_connect(model, id, target));
	assert(pigen_protocol_arrays_can_connect(model, target, id));
	span = name_span(sources, "reversed_array_owner");
	array.symbol = declare(model, scope, PIGEN_SYMBOL_PROTOCOL_ARRAY, INVALID(pigen_data_type_id), span);
	array.span = span;
	dimensions[1].as.range.left = bound(model, 0);
	dimensions[1].as.range.right = bound(model, 1);
	array.shape = pigen_shape_intern(model, dimensions, 2);
	pigen_protocol_array_id reversed = pigen_protocol_array_add(model, &array, lanes + 8, 4);
	assert(reversed.index != PIGEN_INVALID_ID);
	/* Connections map equal-sized dimensions in declaration order. */
	assert(pigen_protocol_arrays_can_connect(model, id, reversed));

}

static void endpoint_case(pigen_protocol_family family,
	pigen_protocol_role role, int is_port, pigen_transfer_type storage)
{
	pigen_source_manager sources = {0};
	pigen_semantic_model model;
	pigen_semantic_init(&model, &sources);
	pigen_source_span module_span = name_span(&sources, "module_owner");
	model.compilation_scope = pigen_scope_add(&model, INVALID(pigen_scope_id),
		(pigen_source_span){INVALID(pigen_source_id), 0, 0});
	pigen_scope_id scope = pigen_scope_add(&model, model.compilation_scope, module_span);
	pigen_symbol_id module_symbol = declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID(pigen_data_type_id), module_span);
	pigen_module_id module = pigen_module_add(&model, (pigen_syntax_id){0},
		module_symbol, scope, module_span);
	assert(module.index != PIGEN_INVALID_ID);
	pigen_protocol protocol = {.family = family, .data_bits = 32,
		.address_bits = family == PIGEN_PROTOCOL_AXI4_STREAM ? 0 : 40,
		.id_bits = family == PIGEN_PROTOCOL_AXI4_LITE ? 0 : 4};
	if (family == PIGEN_PROTOCOL_AXI4_STREAM)
	{
		protocol.stream_fields = PIGEN_STREAM_KEEP | PIGEN_STREAM_LAST;
		protocol.user_bits[PIGEN_CHANNEL_BEAT] = 3;
	}
	pigen_protocol_type_id type = pigen_protocol_type_intern(&model, &protocol);
	assert(type.index != PIGEN_INVALID_ID);
	assert(pigen_protocol_type_intern(&model, &protocol).index == type.index);
	pigen_protocol changed = protocol;
	changed.address_bits += family == PIGEN_PROTOCOL_AXI4_STREAM ? 0 : 1;
	changed.data_bits = 64;
	assert(pigen_protocol_type_intern(&model, &changed).index != type.index);
	assert(model.protocol_type_count == 2);
	assert(pigen_protocol_equal(pigen_protocol_type_get(&model, type), &protocol));
	assert(!pigen_protocol_type_get(&model, INVALID(pigen_protocol_type_id)));
	assert(pigen_protocol_type_intern(&model, &(pigen_protocol){0}).index == PIGEN_INVALID_ID);
	assert(model.protocol_type_count == 2);
	pigen_source_span bus_span = name_span(&sources, "bus");
	pigen_semantic_protocol_endpoint endpoint = {
		.syntax = {1}, .module = module, .type = type, .role = role,
		.is_port = is_port, .span = bus_span,
		.symbol = declare(&model, scope, PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
			INVALID(pigen_data_type_id), bus_span)
	};
	for (pigen_protocol_channel channel = PIGEN_CHANNEL_BEAT;
		channel < PIGEN_CHANNEL_COUNT; channel++)
	{
		pigen_protocol_channel_layout layout;
		endpoint.channels[channel] = INVALID(pigen_signal_id);
		if (!pigen_protocol_layout(&protocol, channel, &layout)) continue;
		pigen_source_span span = name_span(&sources, pigen_protocol_channel_name(channel));
		pigen_data_type_id payload = pigen_data_type_sized_logic(&model,
			layout.payload_bits, PIGEN_SIGN_UNSIGNED);
		pigen_symbol_id symbol = declare(&model, scope, PIGEN_SYMBOL_SIGNAL, payload, span);
		int from_target = channel == PIGEN_CHANNEL_B || channel == PIGEN_CHANNEL_R;
		int output = from_target == (role == PIGEN_PROTOCOL_TARGET);
		pigen_semantic_direction direction = !is_port ? PIGEN_SEMANTIC_INTERNAL :
			output ? PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT;
		endpoint.channels[channel] = pigen_signal_add(&model, (pigen_syntax_id){2}, module,
			symbol, payload, pigen_semantic_scalar_shape(&model), INVALID(pigen_expr_id),
			storage, direction, span);
		assert(endpoint.channels[channel].index != PIGEN_INVALID_ID);
	}
	pigen_semantic_protocol_endpoint bad = endpoint;
	bad.role = (pigen_protocol_role)99;
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	bad = endpoint; bad.type = INVALID(pigen_protocol_type_id);
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	bad = endpoint; bad.is_port = 2;
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	bad = endpoint; bad.binding.is_bound = 1;
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	/* Missing channels and unexpected channels cannot bind an endpoint. */
	pigen_protocol_channel first = family == PIGEN_PROTOCOL_AXI4_STREAM ?
		PIGEN_CHANNEL_BEAT : PIGEN_CHANNEL_AW;
	bad = endpoint; bad.channels[first] = INVALID(pigen_signal_id);
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	bad = endpoint;
	bad.channels[family == PIGEN_PROTOCOL_AXI4_STREAM ? PIGEN_CHANNEL_AW : PIGEN_CHANNEL_BEAT] =
		endpoint.channels[first];
	assert(pigen_protocol_endpoint_add(&model, &bad).index == PIGEN_INVALID_ID);
	/* Width and direction failures leave the original symbol unbound. */
	pigen_semantic_signal *signal = &model.signals[endpoint.channels[first].index];
	pigen_data_type_id saved_type = signal->data_type;
	signal->data_type = pigen_data_type_boolean(&model);
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	signal->data_type = saved_type;
	pigen_semantic_direction saved_direction = signal->direction;
	signal->direction = PIGEN_SEMANTIC_INOUT;
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	signal->direction = saved_direction;
	pigen_transfer_type saved_storage = signal->transfer_type;
	signal->transfer_type = PIGEN_TRANSFER_TYPE_PORT;
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	signal->transfer_type = PIGEN_TRANSFER_TYPE_WIRE;
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	signal->transfer_type = saved_storage;
	assert(model.protocol_endpoint_count == 0);
	assert(pigen_symbol_protocol_endpoint(&model, endpoint.symbol).index == PIGEN_INVALID_ID);
	pigen_protocol_endpoint_id id = pigen_protocol_endpoint_add(&model, &endpoint);
	assert(id.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_protocol_endpoint(&model, endpoint.symbol).index == id.index);
	assert(pigen_protocol_endpoint_get(&model, id)->channels[first].index == endpoint.channels[first].index);
	assert(pigen_symbol_signal(&model, endpoint.symbol).index == PIGEN_INVALID_ID);
	/* A binding is validated separately from endpoint/role/type identity. */
	pigen_symbol_id controls[3];
	const char *control_names[] = {"clk", "reset", "other_clock"};
	for (size_t c = 0; c < 3; c++)
	{
		pigen_source_span span = name_span(&sources, control_names[c]);
		pigen_data_type_id bit = pigen_data_type_boolean(&model);
		controls[c] = declare(&model, scope, PIGEN_SYMBOL_SIGNAL, bit, span);
		assert(pigen_signal_add(&model, (pigen_syntax_id){3}, module, controls[c], bit,
			pigen_semantic_scalar_shape(&model), INVALID(pigen_expr_id), PIGEN_TRANSFER_TYPE_WIRE,
			PIGEN_SEMANTIC_INPUT, span).index != PIGEN_INVALID_ID);
	}
	pigen_clock_domain_id clock = pigen_clock_domain_intern(&model, controls[0], PIGEN_SEMANTIC_POSEDGE);
	pigen_clock_domain_id falling = pigen_clock_domain_intern(&model, controls[0], PIGEN_SEMANTIC_NEGEDGE);
	pigen_clock_domain_id other_clock = pigen_clock_domain_intern(&model, controls[2], PIGEN_SEMANTIC_POSEDGE);
	assert(!pigen_protocol_endpoint_bind(&model, id, falling, controls[1], 0));
	assert(!pigen_protocol_endpoint_bind(&model, id, clock, module_symbol, 0));
	assert(!pigen_protocol_endpoint_bind(&model, id, clock, controls[1], 2));
	assert(!pigen_protocol_endpoint_get(&model, id)->binding.is_bound);
	assert(pigen_protocol_endpoint_bind(&model, id, clock, controls[1], 1));
	assert(pigen_protocol_endpoint_bind(&model, id, clock, controls[1], 1));
	assert(!pigen_protocol_endpoint_bind(&model, id, other_clock, controls[1], 1));
	assert(!pigen_protocol_endpoint_bind(&model, id, clock, controls[1], 0));
	assert(pigen_protocol_endpoint_get(&model, id)->binding.reset_active_low);
	for (size_t c = 0; c < PIGEN_CHANNEL_COUNT; c++)
		if (endpoint.channels[c].index != PIGEN_INVALID_ID)
		{
			assert(pigen_signal_get(&model, endpoint.channels[c])->domain.index == clock.index);
			assert(!pigen_signal_bind_domain(&model, endpoint.channels[c], other_clock));
		}
	assert(pigen_symbol_protocol_endpoint(&model, module_symbol).index == PIGEN_INVALID_ID);
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	/* A second endpoint cannot steal an existing channel, even with an equal type. */
	pigen_source_span other = name_span(&sources, "other_bus");
	endpoint.symbol = declare(&model, scope, PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
		INVALID(pigen_data_type_id), other);
	endpoint.span = other;
	assert(pigen_protocol_endpoint_add(&model, &endpoint).index == PIGEN_INVALID_ID);
	assert(model.protocol_endpoint_count == 1);
	array_case(&model, &sources, scope, id);
	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
}

int main(void)
{
	for (pigen_protocol_family family = PIGEN_PROTOCOL_AXI4_STREAM;
		family <= PIGEN_PROTOCOL_AXI4; family++)
	for (pigen_protocol_role role = PIGEN_PROTOCOL_INITIATOR;
		role <= PIGEN_PROTOCOL_TARGET; role++)
	for (int port = 0; port <= 1; port++)
	{
		endpoint_case(family, role, port, PIGEN_TRANSFER_TYPE_BUF);
		endpoint_case(family, role, port, PIGEN_TRANSFER_TYPE_SKID);
	}
	puts("PASS: canonical AXI types, independent channel endpoints and checked multidimensional lane collections");
	return 0;
}
