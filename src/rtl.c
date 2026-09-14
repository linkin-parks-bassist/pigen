/* Hardware IR arena for the elastic RTL vertical slice. */
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl.h"
#include "pigen/util.h"

pigen_rtl_type_id pigen_rtl_type_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_type *record;
	pigen_rtl_type_id result;

	if (model->type_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl types");
	if (model->type_count == model->type_capacity)
	{
		model->type_capacity =
			model->type_capacity ? model->type_capacity * 2 : 8;
		model->types = pigen_resize(model->types,
			model->type_capacity * sizeof(*model->types));
	}
	result.index = (uint32_t)model->type_count;
	record = &model->types[model->type_count++];
	*record = (pigen_rtl_type){0};
	record->origin = origin;
	return result;
}

const pigen_rtl_type *pigen_rtl_type_get(const pigen_rtl_model *model,
	pigen_rtl_type_id type)
{
	if (!model || type.index == PIGEN_INVALID_ID ||
		type.index >= model->type_count)
		return NULL;
	return &model->types[type.index];
}

static int bound_valid(const pigen_rtl_model *model, pigen_rtl_bound bound)
{
	return bound.expression.index == PIGEN_INVALID_ID ||
		(pigen_rtl_expr_get(model, bound.expression) &&
		 pigen_rtl_expr_get(model, bound.expression)->kind != PIGEN_RTL_EXPR_INVALID);
}

static int bound_equal(pigen_rtl_bound a, pigen_rtl_bound b)
{
	return a.expression.index == b.expression.index &&
		(a.expression.index != PIGEN_INVALID_ID || a.value == b.value);
}

static int dimensions_equal(const pigen_rtl_type *a, const pigen_rtl_type *b)
{
	if (a->dimension_count != b->dimension_count) return 0;
	for (size_t i = 0; i < a->dimension_count; ++i)
		if (!bound_equal(a->dimensions[i].left, b->dimensions[i].left) ||
			!bound_equal(a->dimensions[i].right, b->dimensions[i].right)) return 0;
	return 1;
}

pigen_rtl_type_id pigen_rtl_type_intern(pigen_rtl_model *model,
	const pigen_rtl_type *descriptor)
{
	size_t i;
	pigen_rtl_type *record;
	pigen_rtl_type_id result;
	pigen_rtl_type copy;

	if (!model || !descriptor ||
		descriptor->signedness < PIGEN_SIGN_IMPLICIT ||
		descriptor->signedness > PIGEN_SIGN_SIGNED ||
		(descriptor->state_domain != PIGEN_DATA_TYPE_STATE_TWO &&
		descriptor->state_domain != PIGEN_DATA_TYPE_STATE_FOUR) ||
		(descriptor->width_expression.index != PIGEN_INVALID_ID &&
		!bound_valid(model, (pigen_rtl_bound){0, descriptor->width_expression})) ||
		(descriptor->dimension_count && !descriptor->dimensions) ||
		descriptor->dimension_count > SIZE_MAX / sizeof(*descriptor->dimensions) ||
		model->type_count == PIGEN_INVALID_ID)
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	for (i = 0; i < descriptor->dimension_count; ++i)
		if (!bound_valid(model, descriptor->dimensions[i].left) ||
			!bound_valid(model, descriptor->dimensions[i].right))
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	for (i = 0; i < model->type_count; i++)
	{
		record = &model->types[i];
		if (record->signedness == descriptor->signedness &&
			record->state_domain == descriptor->state_domain &&
			(descriptor->width_expression.index != PIGEN_INVALID_ID ||
			record->width == descriptor->width) &&
			record->width_expression.index ==
			descriptor->width_expression.index && dimensions_equal(record, descriptor))
			return (pigen_rtl_type_id){(uint32_t)i};
	}
	copy = *descriptor;
	copy.dimensions = NULL;
	if (copy.dimension_count) {
		pigen_rtl_packed_dimension *dimensions = pigen_resize(NULL,
			copy.dimension_count * sizeof(*dimensions));
		memcpy(dimensions, descriptor->dimensions, copy.dimension_count * sizeof(*dimensions));
		copy.dimensions = dimensions;
	}
	if (model->type_count == model->type_capacity)
	{
		model->type_capacity =
			model->type_capacity ? model->type_capacity * 2 : 8;
		model->types = pigen_resize(model->types,
			model->type_capacity * sizeof(*model->types));
	}
	result.index = (uint32_t)model->type_count;
	record = &model->types[model->type_count++];
	*record = copy;
	return result;
}

pigen_rtl_expr_id pigen_rtl_expr_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_expr *record;
	pigen_rtl_expr_id result;

	if (model->expression_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl expressions");
	if (model->expression_count == model->expression_capacity)
	{
		model->expression_capacity =
			model->expression_capacity ? model->expression_capacity * 2 : 8;
		model->expressions = pigen_resize(model->expressions,
			model->expression_capacity * sizeof(*model->expressions));
	}
	result.index = (uint32_t)model->expression_count;
	record = &model->expressions[model->expression_count++];
	*record = (pigen_rtl_expr){0};
	record->origin = origin;
	return result;
}

const pigen_rtl_expr *pigen_rtl_expr_get(const pigen_rtl_model *model,
	pigen_rtl_expr_id expression)
{
	if (!model || expression.index == PIGEN_INVALID_ID ||
		expression.index >= model->expression_count)
		return NULL;
	return &model->expressions[expression.index];
}

static pigen_rtl_expr_id expr_append(pigen_rtl_model *model,
	const pigen_rtl_expr *node, const pigen_rtl_expr_id *children,
	size_t child_count)
{
	pigen_rtl_expr *record;
	pigen_rtl_expr_id result;
	size_t i;
	pigen_rtl_expr_id *child_copy = NULL;

	if (!model || !pigen_rtl_type_get(model, node->type) ||
		(child_count && !children) ||
		child_count > SIZE_MAX / sizeof(*children) - model->expression_child_count ||
		model->expression_count == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	for (i = 0; i < child_count; i++)
		if (!pigen_rtl_expr_get(model, children[i]))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (child_count) {
		child_copy = pigen_resize(NULL, child_count * sizeof(*child_copy));
		memcpy(child_copy, children, child_count * sizeof(*child_copy));
	}
	if (model->expression_count == model->expression_capacity)
	{
		model->expression_capacity =
			model->expression_capacity ? model->expression_capacity * 2 : 8;
		model->expressions = pigen_resize(model->expressions,
			model->expression_capacity * sizeof(*model->expressions));
	}
	if (model->expression_child_count + child_count >
		model->expression_child_capacity)
	{
		size_t required = model->expression_child_count + child_count;
		size_t capacity = model->expression_child_capacity ?
			model->expression_child_capacity : 8;
		while (capacity < required) {
			if (capacity > SIZE_MAX / sizeof(*children) / 2) {
				capacity = required;
				break;
			}
			capacity *= 2;
		}
		model->expression_child_capacity = capacity;
		model->expression_children = pigen_resize(model->expression_children,
			model->expression_child_capacity *
			sizeof(*model->expression_children));
	}
	result.index = (uint32_t)model->expression_count;
	record = &model->expressions[model->expression_count++];
	*record = *node;
	record->first_child = model->expression_child_count;
	record->child_count = child_count;
	if (child_count)
		memcpy(model->expression_children + model->expression_child_count,
			child_copy, child_count * sizeof(*children));
	free(child_copy);
	model->expression_child_count += child_count;
	return result;
}

static pigen_rtl_expr_id expr_add_children(pigen_rtl_model *model,
	pigen_rtl_expr_kind kind, pigen_rtl_type_id type, pigen_source_span origin,
	const pigen_rtl_expr_id *children, size_t child_count)
{
	pigen_rtl_expr node = {0};

	node.kind = kind;
	node.type = type;
	node.origin = origin;
	return expr_append(model, &node, children, child_count);
}

pigen_rtl_expr_id pigen_rtl_expr_add_literal(pigen_rtl_model *model,
	pigen_rtl_expr_kind kind, pigen_rtl_type_id type,
	const pigen_rtl_literal_word *words, size_t bit_count, int negative,
	pigen_source_span origin)
{
	const pigen_rtl_type *descriptor = pigen_rtl_type_get(model, type);
	size_t count = bit_count / 64 + (bit_count % 64 != 0);
	pigen_rtl_expr node = {0};
	pigen_rtl_literal_word *copy;
	pigen_rtl_expr_id result;
	if (!descriptor || !words || !bit_count ||
		(kind != PIGEN_RTL_EXPR_INTEGER && kind != PIGEN_RTL_EXPR_BITS) ||
		(negative != 0 && negative != 1) ||
		(kind == PIGEN_RTL_EXPR_BITS && negative) ||
		count > SIZE_MAX / sizeof(*words))
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	for (size_t i = 0; i < count; ++i) {
		uint64_t masks = words[i].x_mask | words[i].z_mask;
		if ((words[i].x_mask & words[i].z_mask) || (words[i].value & masks) ||
			(masks && (kind == PIGEN_RTL_EXPR_INTEGER ||
			 descriptor->state_domain != PIGEN_DATA_TYPE_STATE_FOUR)) ||
			(i == count - 1 && bit_count % 64 &&
			 ((words[i].value | masks) >> (bit_count % 64))))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	copy = pigen_resize(NULL, count * sizeof(*copy));
	memcpy(copy, words, count * sizeof(*copy));
	node.kind = kind;
	node.type = type;
	node.origin = origin;
	node.value = words[0].value;
	node.literal_words = copy;
	node.literal_bit_count = bit_count;
	node.literal_negative = negative;
	result = expr_append(model, &node, NULL, 0);
	if (result.index == PIGEN_INVALID_ID) free(copy);
	return result;
}

pigen_rtl_expr_id pigen_rtl_expr_add_integer(pigen_rtl_model *model,
	pigen_rtl_type_id type, uint64_t value, pigen_source_span origin)
{
	pigen_rtl_literal_word word = {value, 0, 0};
	return pigen_rtl_expr_add_literal(model, PIGEN_RTL_EXPR_INTEGER,
		type, &word, 64, 0, origin);
}

pigen_rtl_expr_id pigen_rtl_expr_add_bits(pigen_rtl_model *model,
	pigen_rtl_type_id type, uint64_t value, pigen_source_span origin)
{
	pigen_rtl_literal_word word = {value, 0, 0};
	return pigen_rtl_expr_add_literal(model, PIGEN_RTL_EXPR_BITS,
		type, &word, 64, 0, origin);
}

pigen_rtl_expr_id pigen_rtl_expr_add_object(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_object_id object,
	pigen_source_span origin)
{
	pigen_rtl_expr node = {0};

	node.kind = PIGEN_RTL_EXPR_OBJECT;
	node.type = type;
	node.origin = origin;
	if (!pigen_rtl_object_get(model, object))
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	node.object = object;
	return expr_append(model, &node, 0, 0);
}

pigen_rtl_expr_id pigen_rtl_expr_add_unary(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_unary_resolution *resolution,
	pigen_rtl_expr_id operand, pigen_source_span origin)
{
	pigen_rtl_expr node = {0};
	pigen_rtl_expr_id child;

	node.kind = PIGEN_RTL_EXPR_UNARY;
	node.type = type;
	node.origin = origin;
	if (!resolution ||
		resolution->operation.operator < PIGEN_UNARY_POSITIVE ||
		resolution->operation.operator > PIGEN_UNARY_REDUCTION_XNOR)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	child = operand;
	node.as.unary.resolution = *resolution;
	return expr_append(model, &node, &child, 1);
}

pigen_rtl_expr_id pigen_rtl_expr_add_binary(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_binary_resolution *resolution,
	pigen_rtl_expr_id left, pigen_rtl_expr_id right, pigen_source_span origin)
{
	pigen_rtl_expr node = {0};
	pigen_rtl_expr_id children[2];

	node.kind = PIGEN_RTL_EXPR_BINARY;
	node.type = type;
	node.origin = origin;
	if (!resolution ||
		resolution->operation.operator < PIGEN_BINARY_ADD ||
		resolution->operation.operator > PIGEN_BINARY_LOGICAL_OR)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	children[0] = left;
	children[1] = right;
	node.as.binary.resolution = *resolution;
	return expr_append(model, &node, children, 2);
}

pigen_rtl_expr_id pigen_rtl_expr_add_conditional(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_conditional_resolution *resolution,
	pigen_rtl_expr_id condition, pigen_rtl_expr_id when_true,
	pigen_rtl_expr_id when_false, pigen_source_span origin)
{
	pigen_rtl_expr node = {0};
	pigen_rtl_expr_id children[3];

	node.kind = PIGEN_RTL_EXPR_CONDITIONAL;
	node.type = type;
	node.origin = origin;
	if (!resolution)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	children[0] = condition;
	children[1] = when_true;
	children[2] = when_false;
	node.as.conditional.resolution = *resolution;
	return expr_append(model, &node, children, 3);
}

pigen_rtl_expr_id pigen_rtl_expr_add_conversion(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_conversion *conversion,
	pigen_rtl_expr_id operand, pigen_source_span origin)
{
	pigen_rtl_expr node = {0};
	pigen_rtl_expr_id child;

	node.kind = PIGEN_RTL_EXPR_CONVERSION;
	node.type = type;
	node.origin = origin;
	if (!conversion ||
		conversion->kind == PIGEN_CONVERSION_INVALID ||
		conversion->kind > PIGEN_CONVERSION_INTEGER_TO_VECTOR)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	child = operand;
	node.as.conversion.conversion = *conversion;
	return expr_append(model, &node, &child, 1);
}

pigen_rtl_expr_id pigen_rtl_expr_add_index(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id index,
	pigen_source_span origin)
{
	pigen_rtl_expr_id children[2];

	children[0] = base;
	children[1] = index;
	return expr_add_children(model, PIGEN_RTL_EXPR_INDEX, type, origin,
		children, 2);
}

pigen_rtl_expr_id pigen_rtl_expr_add_select(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id left,
	pigen_rtl_expr_id right, pigen_select_kind kind, pigen_source_span origin)
{
	pigen_rtl_expr node = {0};
	pigen_rtl_expr_id children[3];

	node.kind = PIGEN_RTL_EXPR_SELECT;
	node.type = type;
	node.origin = origin;
	if (kind < PIGEN_SEMANTIC_SELECT_RANGE ||
		kind > PIGEN_SEMANTIC_SELECT_INDEXED_DOWN)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	children[0] = base;
	children[1] = left;
	children[2] = right;
	node.as.select.kind = kind;
	return expr_append(model, &node, children, 3);
}

pigen_rtl_expr_id pigen_rtl_expr_add_concatenation(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_rtl_expr_id *children, size_t count,
	pigen_source_span origin)
{
	return expr_add_children(model, PIGEN_RTL_EXPR_CONCATENATION, type,
		origin, children, count);
}

const pigen_rtl_expr_id *pigen_rtl_expr_children(const pigen_rtl_model *model,
	pigen_rtl_expr_id expression, size_t *child_count)
{
	const pigen_rtl_expr *record;

	if (!model || !child_count)
		return NULL;
	record = pigen_rtl_expr_get(model, expression);
	if (!record)
		return NULL;
	*child_count = record->child_count;
	return record->child_count ?
		model->expression_children + record->first_child : NULL;
}

pigen_rtl_object_id pigen_rtl_object_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_object *record;
	pigen_rtl_object_id result;

	if (model->object_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl objects");
	if (model->object_count == model->object_capacity)
	{
		model->object_capacity =
			model->object_capacity ? model->object_capacity * 2 : 8;
		model->objects = pigen_resize(model->objects,
			model->object_capacity * sizeof(*model->objects));
	}
	result.index = (uint32_t)model->object_count;
	record = &model->objects[model->object_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_object *pigen_rtl_object_get(const pigen_rtl_model *model,
	pigen_rtl_object_id object)
{
	if (!model || object.index == PIGEN_INVALID_ID ||
		object.index >= model->object_count)
		return NULL;
	return &model->objects[object.index];
}

pigen_rtl_instance_id pigen_rtl_instance_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_instance *record;
	pigen_rtl_instance_id result;

	if (model->instance_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl instances");
	if (model->instance_count == model->instance_capacity)
	{
		model->instance_capacity =
			model->instance_capacity ? model->instance_capacity * 2 : 8;
		model->instances = pigen_resize(model->instances,
			model->instance_capacity * sizeof(*model->instances));
	}
	result.index = (uint32_t)model->instance_count;
	record = &model->instances[model->instance_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_instance *pigen_rtl_instance_get(const pigen_rtl_model *model,
	pigen_rtl_instance_id instance)
{
	if (!model || instance.index == PIGEN_INVALID_ID ||
		instance.index >= model->instance_count)
		return NULL;
	return &model->instances[instance.index];
}

pigen_rtl_equation_id pigen_rtl_equation_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_equation *record;
	pigen_rtl_equation_id result;

	if (model->equation_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl equations");
	if (model->equation_count == model->equation_capacity)
	{
		model->equation_capacity =
			model->equation_capacity ? model->equation_capacity * 2 : 8;
		model->equations = pigen_resize(model->equations,
			model->equation_capacity * sizeof(*model->equations));
	}
	result.index = (uint32_t)model->equation_count;
	record = &model->equations[model->equation_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_equation *pigen_rtl_equation_get(const pigen_rtl_model *model,
	pigen_rtl_equation_id equation)
{
	if (!model || equation.index == PIGEN_INVALID_ID ||
		equation.index >= model->equation_count)
		return NULL;
	return &model->equations[equation.index];
}

pigen_rtl_update_id pigen_rtl_update_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_update *record;
	pigen_rtl_update_id result;

	if (model->update_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl updates");
	if (model->update_count == model->update_capacity)
	{
		model->update_capacity =
			model->update_capacity ? model->update_capacity * 2 : 8;
		model->updates = pigen_resize(model->updates,
			model->update_capacity * sizeof(*model->updates));
	}
	result.index = (uint32_t)model->update_count;
	record = &model->updates[model->update_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_update *pigen_rtl_update_get(const pigen_rtl_model *model,
	pigen_rtl_update_id update)
{
	if (!model || update.index == PIGEN_INVALID_ID ||
		update.index >= model->update_count)
		return NULL;
	return &model->updates[update.index];
}

pigen_rtl_process_id pigen_rtl_process_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_process *record;
	pigen_rtl_process_id result;

	if (model->process_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl processes");
	if (model->process_count == model->process_capacity)
	{
		model->process_capacity =
			model->process_capacity ? model->process_capacity * 2 : 8;
		model->processes = pigen_resize(model->processes,
			model->process_capacity * sizeof(*model->processes));
	}
	result.index = (uint32_t)model->process_count;
	record = &model->processes[model->process_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_process *pigen_rtl_process_get(const pigen_rtl_model *model,
	pigen_rtl_process_id process)
{
	if (!model || process.index == PIGEN_INVALID_ID ||
		process.index >= model->process_count)
		return NULL;
	return &model->processes[process.index];
}

pigen_rtl_module_id pigen_rtl_module_add(pigen_rtl_model *model,
	pigen_source_span origin)
{
	pigen_rtl_module *record;
	pigen_rtl_module_id result;

	if (model->module_count == PIGEN_INVALID_ID)
		pigen_fail("too many rtl modules");
	if (model->module_count == model->module_capacity)
	{
		model->module_capacity =
			model->module_capacity ? model->module_capacity * 2 : 8;
		model->modules = pigen_resize(model->modules,
			model->module_capacity * sizeof(*model->modules));
	}
	result.index = (uint32_t)model->module_count;
	record = &model->modules[model->module_count++];
	record->origin = origin;
	return result;
}

const pigen_rtl_module *pigen_rtl_module_get(const pigen_rtl_model *model,
	pigen_rtl_module_id module)
{
	if (!model || module.index == PIGEN_INVALID_ID ||
		module.index >= model->module_count)
		return NULL;
	return &model->modules[module.index];
}

void pigen_free_rtl_model(pigen_rtl_model *model)
{
	for (size_t i = 0; i < model->type_count; ++i)
		free((void *)model->types[i].dimensions);
	for (size_t i = 0; i < model->expression_count; ++i)
		free((void *)model->expressions[i].literal_words);
	free(model->types);
	free(model->expressions);
	free(model->expression_children);
	free(model->objects);
	free(model->instances);
	free(model->equations);
	free(model->updates);
	free(model->processes);
	free(model->modules);
	*model = (pigen_rtl_model){0};
}
