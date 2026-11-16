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
			record->sv_keyword == descriptor->sv_keyword &&
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
		conversion->kind > PIGEN_CONVERSION_FOUR_TO_TWO)
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

pigen_rtl_expr_id pigen_rtl_expr_add_member_select(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id left,
	pigen_rtl_expr_id right, const char *name, pigen_source_span origin)
{
	if (!name || !*name) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_expr_id result = pigen_rtl_expr_add_select(model,type,base,left,right,
		PIGEN_SEMANTIC_SELECT_RANGE,origin);
	if (result.index != PIGEN_INVALID_ID)
		model->expressions[result.index].as.select.member_name = pigen_copy_range(name,strlen(name));
	return result;
}

pigen_rtl_expr_id pigen_rtl_expr_add_evaluated_value(pigen_rtl_model *model,
	pigen_rtl_type_id type,pigen_rtl_expr_id value,pigen_rtl_expr_id evaluated,
	int before,pigen_source_span origin)
{
	const pigen_rtl_expr *v=pigen_rtl_expr_get(model,value),*e=pigen_rtl_expr_get(model,evaluated);
	const pigen_rtl_type *vt=v ? pigen_rtl_type_get(model,v->type) : NULL,*et=e ? pigen_rtl_type_get(model,e->type) : NULL;
	if (!v || !e || v->type.index!=type.index || !vt || !et || (!vt->width && vt->width_expression.index==PIGEN_INVALID_ID) ||
		(!et->width && et->width_expression.index==PIGEN_INVALID_ID) ||
		(before!=0 && before!=1)) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_expr node={0};node.kind=PIGEN_RTL_EXPR_EVALUATED_VALUE;
	node.type=type;node.origin=origin;node.as.evaluation.before=before;
	pigen_rtl_expr_id children[]={value,evaluated};
	return expr_append(model,&node,children,2);
}

pigen_rtl_expr_id pigen_rtl_expr_add_array_pattern(pigen_rtl_model *model,
 pigen_rtl_type_id type,const pigen_rtl_expr_id *children,size_t count,pigen_source_span origin)
{
 if(!model || !children || !count) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
 for(size_t i=0;i<count;i++)
 {
  const pigen_rtl_expr *child=pigen_rtl_expr_get(model,children[i]);
  if(!child || child->type.index!=type.index) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
 }
 return expr_add_children(model,PIGEN_RTL_EXPR_ARRAY_PATTERN,type,origin,children,count);
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
	*record = (pigen_rtl_object){0};
	record->initial_value = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
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
	*record = (pigen_rtl_instance){0};
	record->origin = origin;
	record->semantic_instance = (pigen_instance_id){PIGEN_INVALID_ID};
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
	*record = (pigen_rtl_equation){0};
	record->destination_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
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
	*record = (pigen_rtl_update){0};
	record->enable = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
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
	*record = (pigen_rtl_process){.body={PIGEN_INVALID_ID}};
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
	*record = (pigen_rtl_module){0};
	record->origin = origin;
	record->semantic_module=(pigen_module_id){PIGEN_INVALID_ID};
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

/* Task 3 resolved-hardware constructors. Owner identity is validated before
 * any arena is touched, so a failing add publishes nothing: arena counts and
 * every existing owner range stay unchanged. */
static int range_append(pigen_rtl_record_range *range, size_t *count,
	size_t additions)
{
	if (additions > SIZE_MAX - *count)
		return 0;
	if (*count < range->count ||
		*count - range->first != range->count)
		return 0;
	if (additions > SIZE_MAX - range->first - range->count)
		return 0;
	range->count += additions;
	*count += additions;
	return 1;
}

pigen_rtl_module_id pigen_rtl_module_add_with_owner(pigen_rtl_model *model,
	pigen_module_id semantic_module, pigen_source_span span)
{
	pigen_rtl_module *record;
	pigen_rtl_module_id result;

	if (!model || semantic_module.index == PIGEN_INVALID_ID ||
		model->module_count == PIGEN_INVALID_ID)
		return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	if (model->module_count == model->module_capacity)
	{
		model->module_capacity =
			model->module_capacity ? model->module_capacity * 2 : 8;
		model->modules = pigen_resize(model->modules,
			model->module_capacity * sizeof(*model->modules));
	}
	result.index = (uint32_t)model->module_count;
	record = &model->modules[model->module_count++];
	*record = (pigen_rtl_module){0};
	record->origin = span;
	record->semantic_module = semantic_module;
	record->objects = (pigen_rtl_record_range){model->object_count, 0};
	record->instances = (pigen_rtl_record_range){model->instance_count, 0};
	record->equations = (pigen_rtl_record_range){model->equation_count, 0};
	record->processes = (pigen_rtl_record_range){model->process_count, 0};
	return result;
}

pigen_rtl_object_id pigen_rtl_object_add_with_owner(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_object_kind kind,
	pigen_rtl_type_id type, pigen_semantic_direction direction,
	pigen_signal_id semantic_signal, pigen_source_span span)
{
	pigen_rtl_module *owner;
	pigen_rtl_object *record;
	pigen_rtl_object_id result;

	if (!model || !pigen_rtl_type_get(model, type) ||
		kind < PIGEN_RTL_OBJECT_VARIABLE || kind > PIGEN_RTL_OBJECT_PARAMETER ||
		direction < PIGEN_SEMANTIC_INTERNAL ||
		direction > PIGEN_SEMANTIC_INOUT ||
		model->object_count == PIGEN_INVALID_ID)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	if (!pigen_rtl_module_get(model, module) ||
		model->modules[module.index].objects.first +
			model->modules[module.index].objects.count !=
		model->object_count)
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	owner = &model->modules[module.index];
	if (model->object_count == model->object_capacity)
	{
		model->object_capacity =
			model->object_capacity ? model->object_capacity * 2 : 8;
		model->objects = pigen_resize(model->objects,
			model->object_capacity * sizeof(*model->objects));
	}
	result.index = (uint32_t)model->object_count;
	record = &model->objects[model->object_count++];
	*record = (pigen_rtl_object){0};
	record->initial_value = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	record->origin = span;
	record->kind = kind;
	record->type = type;
	record->direction = direction;
	record->semantic_signal = semantic_signal;
	record->module = module;
	owner->objects.count += 1;
	return result;
}

pigen_rtl_equation_id pigen_rtl_equation_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_object_id destination, pigen_rtl_expr_id value,
	pigen_source_span span)
{
	pigen_rtl_module *owner;
	const pigen_rtl_object *dest;
	pigen_rtl_equation *record;
	pigen_rtl_equation_id result;

	if (!model || !pigen_rtl_expr_get(model, value) ||
		model->equation_count == PIGEN_INVALID_ID)
		return (pigen_rtl_equation_id){PIGEN_INVALID_ID};
	dest = pigen_rtl_object_get(model, destination);
	if (!dest)
		return (pigen_rtl_equation_id){PIGEN_INVALID_ID};
	if (dest->module.index != module.index ||
		!pigen_rtl_module_get(model, module) ||
		model->modules[module.index].equations.first +
			model->modules[module.index].equations.count !=
		model->equation_count)
		return (pigen_rtl_equation_id){PIGEN_INVALID_ID};
	owner = &model->modules[module.index];
	if (model->equation_count == model->equation_capacity)
	{
		model->equation_capacity =
			model->equation_capacity ? model->equation_capacity * 2 : 8;
		model->equations = pigen_resize(model->equations,
			model->equation_capacity * sizeof(*model->equations));
	}
	result.index = (uint32_t)model->equation_count;
	record = &model->equations[model->equation_count++];
	*record = (pigen_rtl_equation){0};
	record->destination_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	record->origin = span;
	record->destination = destination;
	record->value = value;
	record->module = module;
	owner->equations.count += 1;
	return result;
}

/* A concatenated target belongs to every leaf object. Its first object is
 * retained only as the equation's compatibility bookkeeping destination. */
static int equation_lvalue_owner(const pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_expr_id id,
	pigen_rtl_object_id *first, size_t depth)
{
	const pigen_rtl_expr *base = pigen_rtl_expr_get(model, id);
	if (!base || depth > model->expression_count) return 0;
	if (base->kind == PIGEN_RTL_EXPR_CONCATENATION)
	{
		if (!base->child_count || base->first_child > model->expression_child_count ||
			base->child_count > model->expression_child_count - base->first_child) return 0;
		for (size_t i = 0; i < base->child_count; i++)
			if (!equation_lvalue_owner(model, module,
				model->expression_children[base->first_child+i], first, depth+1)) return 0;
		return 1;
	}
	while (base && (base->kind == PIGEN_RTL_EXPR_INDEX || base->kind == PIGEN_RTL_EXPR_SELECT))
	{
		if (++depth > model->expression_count || !base->child_count ||
			base->first_child >= model->expression_child_count) return 0;
		base = pigen_rtl_expr_get(model, model->expression_children[base->first_child]);
	}
	if (!base || base->kind != PIGEN_RTL_EXPR_OBJECT) return 0;
	const pigen_rtl_object *object = pigen_rtl_object_get(model, base->object);
	if (!object || object->module.index != module.index) return 0;
	if (first->index == PIGEN_INVALID_ID) *first = base->object;
	return 1;
}

pigen_rtl_equation_id pigen_rtl_equation_add_projection_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_expr_id destination, pigen_rtl_expr_id value,
	pigen_source_span span)
{
	pigen_rtl_object_id first = {PIGEN_INVALID_ID};
	if (!equation_lvalue_owner(model, module, destination, &first, 0))
		return (pigen_rtl_equation_id){PIGEN_INVALID_ID};
	pigen_rtl_equation_id result = pigen_rtl_equation_add_with_owner(model,
		module, first, value, span);
	if (result.index != PIGEN_INVALID_ID)
		model->equations[result.index].destination_expression = destination;
	return result;
}

static int equation_lvalue_contains(const pigen_rtl_model *model,
	pigen_rtl_expr_id id, pigen_rtl_object_id object, size_t depth)
{
	const pigen_rtl_expr *expression = pigen_rtl_expr_get(model, id);
	if (!expression || depth > model->expression_count) return 0;
	if (expression->kind == PIGEN_RTL_EXPR_OBJECT) return expression->object.index == object.index;
	if (expression->kind != PIGEN_RTL_EXPR_CONCATENATION &&
		expression->kind != PIGEN_RTL_EXPR_INDEX && expression->kind != PIGEN_RTL_EXPR_SELECT) return 0;
	if (expression->first_child > model->expression_child_count ||
		expression->child_count > model->expression_child_count-expression->first_child) return 0;
	size_t count = expression->child_count;
	if (expression->kind != PIGEN_RTL_EXPR_CONCATENATION && count) count = 1;
	for (size_t i = 0; i < count; i++)
		if (equation_lvalue_contains(model, model->expression_children[expression->first_child+i], object, depth+1)) return 1;
	return 0;
}

int pigen_rtl_equation_drives_object(const pigen_rtl_model *model,
	pigen_rtl_equation_id id, pigen_rtl_object_id object)
{
	const pigen_rtl_equation *equation = pigen_rtl_equation_get(model, id);
	if (!equation || !pigen_rtl_object_get(model, object)) return 0;
	if (equation->destination_expression.index == PIGEN_INVALID_ID)
		return equation->destination.index == object.index;
	return equation_lvalue_contains(model, equation->destination_expression, object, 0);
}

pigen_rtl_procedural_id pigen_rtl_procedural_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_procedural_kind kind,
	pigen_rtl_expr_id destination, pigen_rtl_expr_id value, pigen_rtl_expr_id condition,
	const pigen_rtl_procedural_id *children, size_t count, pigen_source_span span)
{
	pigen_rtl_procedural_id invalid={PIGEN_INVALID_ID};
	if (!pigen_rtl_module_get(model,module) || (unsigned)kind>PIGEN_PROCEDURAL_CONTINUE ||
		(count && !children) || model->procedural_count==PIGEN_INVALID_ID ||
		count>SIZE_MAX-model->procedural_child_count ||
		model->procedural_child_count+count>SIZE_MAX/sizeof(*children)) return invalid;
	if (kind==PIGEN_PROCEDURAL_IF &&
		(!pigen_rtl_expr_get(model,condition) || count<1 || count>2)) return invalid;
	if ((kind==PIGEN_PROCEDURAL_WHILE || kind==PIGEN_PROCEDURAL_REPEAT || kind==PIGEN_PROCEDURAL_DO_WHILE) &&
		(!pigen_rtl_expr_get(model,condition) || count!=1)) return invalid;
	if (kind==PIGEN_PROCEDURAL_FOREVER && (count!=1 || condition.index!=PIGEN_INVALID_ID)) return invalid;
	if ((kind==PIGEN_PROCEDURAL_BREAK || kind==PIGEN_PROCEDURAL_CONTINUE) &&
		(count || condition.index!=PIGEN_INVALID_ID || destination.index!=PIGEN_INVALID_ID || value.index!=PIGEN_INVALID_ID)) return invalid;
	int selection=kind>=PIGEN_PROCEDURAL_CASE && kind<=PIGEN_PROCEDURAL_CASEX;
	if (selection && !pigen_rtl_expr_get(model,condition)) return invalid;
	if (kind==PIGEN_PROCEDURAL_CASE_ITEM && (count!=1 ||
		(condition.index!=PIGEN_INVALID_ID && !pigen_rtl_expr_get(model,condition)))) return invalid;
	if (kind==PIGEN_PROCEDURAL_ASSIGN)
	{
		pigen_rtl_object_id first={PIGEN_INVALID_ID};
		if (count || !pigen_rtl_expr_get(model,value) ||
			!equation_lvalue_owner(model,module,destination,&first,0)) return invalid;
	}
	int defaults=0;
	for (size_t i=0;i<count;i++)
	{
		if (children[i].index>=model->procedural_count ||
			model->procedural[children[i].index].module.index!=module.index) return invalid;
		const pigen_rtl_procedural *child=&model->procedural[children[i].index];
		if ((child->kind==PIGEN_PROCEDURAL_CASE_ITEM)!=selection) return invalid;
		if ((unsigned)child->case_qualifier>PIGEN_CASE_PRIORITY ||
			(child->case_qualifier!=PIGEN_CASE_UNQUALIFIED && child->kind!=PIGEN_PROCEDURAL_IF && (child->kind<PIGEN_PROCEDURAL_CASE || child->kind>PIGEN_PROCEDURAL_CASEX))) return invalid;
		if (child->else_if && (child->else_if!=1 || child->kind!=PIGEN_PROCEDURAL_IF || child->child_count!=2 ||
			child->first_child>model->procedural_child_count || 2>model->procedural_child_count-child->first_child ||
			model->procedural_children[child->first_child+1].index>=model->procedural_count ||
			model->procedural[model->procedural_children[child->first_child+1].index].kind!=PIGEN_PROCEDURAL_IF)) return invalid;
		if (child->case_continuation)
		{
			if (!selection || !i || child->case_continuation!=1 || child->condition.index==PIGEN_INVALID_ID ||
				child->child_count!=1 || child->first_child>=model->procedural_child_count) return invalid;
			const pigen_rtl_procedural *previous=&model->procedural[children[i-1].index];
			if (previous->condition.index==PIGEN_INVALID_ID || previous->child_count!=1 ||
				previous->first_child>=model->procedural_child_count ||
				model->procedural_children[previous->first_child].index!=model->procedural_children[child->first_child].index) return invalid;
		}
		if (selection && child->condition.index==PIGEN_INVALID_ID && ++defaults>1) return invalid;
		if (selection && child->condition.index!=PIGEN_INVALID_ID)
		{
			const pigen_rtl_expr *selector=pigen_rtl_expr_get(model,condition),*label=pigen_rtl_expr_get(model,child->condition);
			const pigen_rtl_type *st=selector ? pigen_rtl_type_get(model,selector->type) : NULL,*lt=label ? pigen_rtl_type_get(model,label->type) : NULL;
			if (!st || !lt || !st->width || st->width!=lt->width || st->signedness!=lt->signedness) return invalid;
		}
	}
	if (kind==PIGEN_PROCEDURAL_FOR)
	{
		if (count!=3 || !pigen_rtl_expr_get(model,condition) ||
			model->procedural[children[0].index].kind!=PIGEN_PROCEDURAL_BLOCK ||
			model->procedural[children[1].index].kind!=PIGEN_PROCEDURAL_BLOCK) return invalid;
		for(size_t phase=0;phase<2;phase++)
		{
			const pigen_rtl_procedural *step=&model->procedural[children[phase].index];
			for(size_t i=0;i<step->child_count;i++)
				if (model->procedural[model->procedural_children[step->first_child+i].index].kind!=PIGEN_PROCEDURAL_ASSIGN) return invalid;
		}
	}
	pigen_rtl_procedural_id *copy=count ? pigen_resize(NULL,count*sizeof(*children)) : NULL;
	if (count) memcpy(copy,children,count*sizeof(*children));
	if (model->procedural_count==model->procedural_capacity)
	{
		model->procedural_capacity=model->procedural_capacity ? model->procedural_capacity*2 : 16;
		model->procedural=pigen_resize(model->procedural,model->procedural_capacity*sizeof(*model->procedural));
	}
	size_t first=model->procedural_child_count,needed=first+count;
	if (needed>model->procedural_child_capacity)
	{
		model->procedural_child_capacity=needed;
		model->procedural_children=pigen_resize(model->procedural_children,needed*sizeof(*children));
	}
	if (count) memcpy(model->procedural_children+first,copy,count*sizeof(*children));
	free(copy);
	model->procedural_child_count=needed;
	pigen_rtl_procedural_id result={(uint32_t)model->procedural_count++};
	model->procedural[result.index]=(pigen_rtl_procedural){kind,module,destination,value,condition,first,count,span,0,PIGEN_CASE_UNQUALIFIED,0};
	return result;
}

static int procedural_loop_controls_valid(const pigen_rtl_model *model,
	pigen_rtl_procedural_id id,size_t loops,size_t depth)
{
	if (id.index>=model->procedural_count || depth>128) return 0;
	const pigen_rtl_procedural *node=&model->procedural[id.index];
	if (node->case_continuation && (node->kind!=PIGEN_PROCEDURAL_CASE_ITEM || node->case_continuation!=1)) return 0;
	if ((unsigned)node->case_qualifier>PIGEN_CASE_PRIORITY ||
		(node->case_qualifier!=PIGEN_CASE_UNQUALIFIED && node->kind!=PIGEN_PROCEDURAL_IF && (node->kind<PIGEN_PROCEDURAL_CASE || node->kind>PIGEN_PROCEDURAL_CASEX))) return 0;
	if (node->kind==PIGEN_PROCEDURAL_BREAK || node->kind==PIGEN_PROCEDURAL_CONTINUE)
		return loops && !node->child_count;
	if (node->first_child>model->procedural_child_count || node->child_count>model->procedural_child_count-node->first_child) return 0;
	if (node->else_if && (node->else_if!=1 || node->kind!=PIGEN_PROCEDURAL_IF || node->child_count!=2 ||
		model->procedural_children[node->first_child+1].index>=model->procedural_count ||
		model->procedural[model->procedural_children[node->first_child+1].index].kind!=PIGEN_PROCEDURAL_IF)) return 0;
	for(size_t i=0;i<node->child_count;i++)
	{
		pigen_rtl_procedural_id child_id=model->procedural_children[node->first_child+i];
		if (child_id.index>=model->procedural_count) return 0;
		const pigen_rtl_procedural *child=&model->procedural[child_id.index];
		if (child->case_continuation)
		{
			if (node->kind<PIGEN_PROCEDURAL_CASE || node->kind>PIGEN_PROCEDURAL_CASEX || !i ||
				child->condition.index==PIGEN_INVALID_ID || child->child_count!=1 || child->first_child>=model->procedural_child_count) return 0;
			const pigen_rtl_procedural *previous=&model->procedural[model->procedural_children[node->first_child+i-1].index];
			if (previous->condition.index==PIGEN_INVALID_ID || previous->child_count!=1 ||
				previous->first_child>=model->procedural_child_count ||
				model->procedural_children[previous->first_child].index!=model->procedural_children[child->first_child].index) return 0;
		}
		int body=node->kind>=PIGEN_PROCEDURAL_WHILE && node->kind<=PIGEN_PROCEDURAL_FOREVER &&
			(node->kind!=PIGEN_PROCEDURAL_FOR || i==2);
		if (!procedural_loop_controls_valid(model,model->procedural_children[node->first_child+i],loops+(size_t)body,depth+1)) return 0;
	}
	return 1;
}

pigen_rtl_process_id pigen_rtl_combinational_process_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_procedural_id body, pigen_source_span span)
{
	pigen_rtl_process_id invalid={PIGEN_INVALID_ID};
	if (!pigen_rtl_module_get(model,module) || body.index>=model->procedural_count ||
		model->procedural[body.index].module.index!=module.index ||
		model->procedural[body.index].kind==PIGEN_PROCEDURAL_CASE_ITEM ||
		!procedural_loop_controls_valid(model,body,0,0) ||
		model->process_count==PIGEN_INVALID_ID ||
		model->modules[module.index].processes.first+model->modules[module.index].processes.count!=model->process_count) return invalid;
	pigen_rtl_process_id result=pigen_rtl_process_add(model,span);
	model->processes[result.index].module=module;
	model->processes[result.index].clock=(pigen_rtl_expr_id){PIGEN_INVALID_ID};
	model->processes[result.index].is_combinational=1;
	model->processes[result.index].body=body;
	model->processes[result.index].updates=(pigen_rtl_record_range){model->update_count,0};
	model->modules[module.index].processes.count++;return result;
}

static int procedural_drives_object(const pigen_rtl_model *model,
	pigen_rtl_procedural_id id,pigen_rtl_object_id object,size_t depth)
{
	if (id.index>=model->procedural_count || depth>model->procedural_count) return 0;
	const pigen_rtl_procedural *node=&model->procedural[id.index];
	if (node->kind==PIGEN_PROCEDURAL_ASSIGN) return equation_lvalue_contains(model,node->destination,object,0);
	if (node->first_child>model->procedural_child_count || node->child_count>model->procedural_child_count-node->first_child) return 0;
	for (size_t i=0;i<node->child_count;i++)
		if (procedural_drives_object(model,model->procedural_children[node->first_child+i],object,depth+1)) return 1;
	return 0;
}

int pigen_rtl_process_drives_object(const pigen_rtl_model *model,
	pigen_rtl_process_id id,pigen_rtl_object_id object)
{
	const pigen_rtl_process *process=pigen_rtl_process_get(model,id);
	if (!process || !pigen_rtl_object_get(model,object)) return 0;
	if (process->is_combinational) return procedural_drives_object(model,process->body,object,0);
	if (process->updates.first>model->update_count || process->updates.count>model->update_count-process->updates.first) return 0;
	for (size_t i=0;i<process->updates.count;i++)
		if (model->updates[process->updates.first+i].destination.index==object.index) return 1;
	return 0;
}

pigen_rtl_instance_id pigen_rtl_instance_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_module_id semantic_module,
	const pigen_rtl_expr_id *parameters, size_t parameter_count,
	const pigen_rtl_object_id *connections, size_t connection_count,
	pigen_source_span span)
{
	pigen_rtl_module *owner;
	pigen_rtl_instance *record;
	pigen_rtl_instance_id result;
	size_t i;

	if (!model || semantic_module.index == PIGEN_INVALID_ID ||
		(parameter_count && !parameters) ||
		(connection_count && !connections) ||
		model->instance_count == PIGEN_INVALID_ID)
		return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	if (!pigen_rtl_module_get(model, module))
		return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	owner = &model->modules[module.index];
	for (i = 0; i < parameter_count; i++)
		if (!pigen_rtl_expr_get(model, parameters[i]))
			return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	for (i = 0; i < connection_count; i++)
	{
		const pigen_rtl_object *connection =
			pigen_rtl_object_get(model, connections[i]);
		if (!connection ||
			connection->module.index != module.index)
			return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	}
	if (owner->instances.first + owner->instances.count !=
		model->instance_count)
		return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
	if (model->instance_count == model->instance_capacity)
	{
		model->instance_capacity =
			model->instance_capacity ? model->instance_capacity * 2 : 8;
		model->instances = pigen_resize(model->instances,
			model->instance_capacity * sizeof(*model->instances));
	}
	{
		pigen_rtl_record_range parameter_range =
			(pigen_rtl_record_range){model->instance_parameter_count, 0};
		pigen_rtl_record_range connection_range =
			(pigen_rtl_record_range){model->instance_connection_count, 0};

		if (parameter_count && !range_append(&parameter_range,
			&model->instance_parameter_count, parameter_count))
			return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
		if (connection_count && !range_append(&connection_range,
			&model->instance_connection_count, connection_count))
		{
			model->instance_parameter_count = parameter_range.first;
			return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
		}
		if (parameter_count)
		{
			model->instance_parameters = pigen_resize(
				model->instance_parameters,
				model->instance_parameter_count *
				sizeof(*model->instance_parameters));
			memcpy(model->instance_parameters + parameter_range.first,
				parameters, parameter_count * sizeof(*parameters));
		}
		if (connection_count)
		{
			model->instance_connections = pigen_resize(
				model->instance_connections,
				model->instance_connection_count *
				sizeof(*model->instance_connections));
			memcpy(model->instance_connections + connection_range.first,
				connections, connection_count * sizeof(*connections));
		}
		record = &model->instances[model->instance_count];
		result.index = (uint32_t)model->instance_count;
		*record = (pigen_rtl_instance){0};
		record->origin = span;
		record->semantic_module = semantic_module;
		record->module = module;
		record->parameters = parameter_range;
		record->connections = connection_range;
		model->instance_count += 1;
		owner->instances.count += 1;
	}
	return result;
}

pigen_rtl_update_id pigen_rtl_update_add_with_owner(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_object_id destination,
	pigen_rtl_expr_id value, pigen_source_span span)
{
	const pigen_rtl_object *dest;
	pigen_rtl_update *record;
	pigen_rtl_update_id result;

	if (!model || !pigen_rtl_expr_get(model, value) ||
		model->update_count == PIGEN_INVALID_ID)
		return (pigen_rtl_update_id){PIGEN_INVALID_ID};
	dest = pigen_rtl_object_get(model, destination);
	if (!dest)
		return (pigen_rtl_update_id){PIGEN_INVALID_ID};
	if (dest->module.index != module.index ||
		!pigen_rtl_module_get(model, module))
		return (pigen_rtl_update_id){PIGEN_INVALID_ID};
	if (model->update_count == model->update_capacity)
	{
		model->update_capacity =
			model->update_capacity ? model->update_capacity * 2 : 8;
		model->updates = pigen_resize(model->updates,
			model->update_capacity * sizeof(*model->updates));
	}
	result.index = (uint32_t)model->update_count;
	record = &model->updates[model->update_count++];
	*record = (pigen_rtl_update){0};
	record->enable = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	record->path = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	record->origin = span;
	record->destination = destination;
	record->value = value;
	record->module = module;
	return result;
}

int pigen_rtl_update_set_enable(pigen_rtl_model *model,
	pigen_rtl_update_id update, pigen_rtl_expr_id enable)
{
	if (!model || update.index >= model->update_count)
		return 0;
	if (enable.index != PIGEN_INVALID_ID &&
		!pigen_rtl_expr_get(model, enable))
		return 0;
	model->updates[update.index].enable = enable;
	return 1;
}

int pigen_rtl_update_set_path(pigen_rtl_model *model,
	pigen_rtl_update_id update, pigen_rtl_expr_id path)
{
	pigen_rtl_update *record;

	if (!model || update.index == PIGEN_INVALID_ID ||
		update.index >= model->update_count ||
		!pigen_rtl_expr_get(model, path))
		return 0;
	record = &model->updates[update.index];
	record->path = path;
	return 1;
}


pigen_rtl_process_id pigen_rtl_process_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_expr_id clock_expression, pigen_semantic_edge edge,
	const pigen_rtl_update_id *updates, size_t update_count,
	pigen_source_span span)
{
	pigen_rtl_module *owner;
	pigen_rtl_process *record;
	pigen_rtl_process_id result;
	size_t i;

	if (!model || !pigen_rtl_expr_get(model, clock_expression) ||
		edge < PIGEN_SEMANTIC_POSEDGE || edge > PIGEN_SEMANTIC_NEGEDGE ||
		(update_count && !updates) ||
		model->process_count == PIGEN_INVALID_ID)
		return (pigen_rtl_process_id){PIGEN_INVALID_ID};
	for (i = 0; i < update_count; i++)
	{
		const pigen_rtl_update *update =
			pigen_rtl_update_get(model, updates[i]);
		if (!update || update->module.index != module.index)
			return (pigen_rtl_process_id){PIGEN_INVALID_ID};
	}
	if (!pigen_rtl_module_get(model, module) ||
		model->modules[module.index].processes.first +
			model->modules[module.index].processes.count !=
		model->process_count)
		return (pigen_rtl_process_id){PIGEN_INVALID_ID};
	owner = &model->modules[module.index];
	if (model->process_count == model->process_capacity)
	{
		model->process_capacity =
			model->process_capacity ? model->process_capacity * 2 : 8;
		model->processes = pigen_resize(model->processes,
			model->process_capacity * sizeof(*model->processes));
	}
	result.index = (uint32_t)model->process_count;
	record = &model->processes[model->process_count++];
	*record = (pigen_rtl_process){.body={PIGEN_INVALID_ID}};
	record->origin = span;
	record->clock = clock_expression;
	record->edge = edge;
	record->updates =
		(pigen_rtl_record_range){model->update_count - update_count,
		update_count};
	record->module = module;
	owner->processes.count += 1;
	return result;
}

pigen_rtl_record_range pigen_rtl_connection_range_add(pigen_rtl_model *model,
	const pigen_rtl_connection *connections, size_t count)
{
	pigen_rtl_record_range range = {(size_t)PIGEN_INVALID_ID, 0};

	if (!model || (count && !connections))
		return range;
	if (count > SIZE_MAX - model->connection_count)
		return range;
	if (model->connection_count + count > model->connection_capacity)
	{
		size_t capacity = model->connection_capacity ?
			model->connection_capacity * 2 : 8;
		while (capacity < model->connection_count + count)
			capacity *= 2;
		model->connections = pigen_resize(model->connections,
			capacity * sizeof(*model->connections));
		model->connection_capacity = capacity;
	}
	range.first = model->connection_count;
	range.count = count;
	for (size_t i = 0; i < count; i++)
	{
		model->connections[model->connection_count] = connections[i];
		model->connections[model->connection_count].port =
			connections[i].port ? pigen_copy_range(connections[i].port,
				strlen(connections[i].port)) : NULL;
		model->connection_count++;
	}
	return range;
}

int pigen_rtl_primitive_port_is_output(pigen_rtl_primitive primitive, const char *port)
{
	if (!port || primitive == PIGEN_RTL_PRIMITIVE_INVALID) return 0;
	if (primitive == PIGEN_RTL_PRIMITIVE_AXI_MERGE || primitive == PIGEN_RTL_PRIMITIVE_AXI_DEMUX)
	{
		static const char *outputs[] = {"s_aw_ready", "s_w_ready", "s_b_valid", "s_b_payload",
			"s_ar_ready", "s_r_valid", "s_r_payload", "m_aw_valid", "m_aw_payload",
			"m_w_valid", "m_w_payload", "m_b_ready", "m_ar_valid", "m_ar_payload", "m_r_ready"};
		for (size_t i = 0; i < sizeof(outputs)/sizeof(*outputs); i++)
			if (!strcmp(port, outputs[i])) return 1;
		return 0;
	}
	return !strcmp(port, "packet_out") || !strcmp(port, "out_valid") || !strcmp(port, "in_ready") ||
		(primitive == PIGEN_RTL_PRIMITIVE_MERGE2 && (!strcmp(port, "i0_ready") || !strcmp(port, "i1_ready")));
}

int pigen_rtl_instance_set_primitive(pigen_rtl_model *model,
	pigen_rtl_instance_id instance, pigen_rtl_primitive primitive,
	pigen_rtl_type_id payload_type, pigen_rtl_record_range connection_records)
{
	pigen_rtl_instance *record;

	if (!model || primitive == PIGEN_RTL_PRIMITIVE_INVALID)
		return 0;
	record = instance.index == PIGEN_INVALID_ID ? NULL :
		&model->instances[instance.index];
	if (!record || instance.index >= model->instance_count)
		return 0;
	record->primitive = primitive;
	record->payload_type = payload_type;
	record->connection_records = connection_records;
	return 1;
}

void pigen_rtl_set_name(pigen_rtl_model *model, char **slot, const char *text)
{
	if (!model || !slot || !text)
		return;
	free(*slot);
	*slot = pigen_copy_range(text, strlen(text));
}

int pigen_rtl_object_set_shape(pigen_rtl_model *model,
	pigen_rtl_object_id object, const char *shape)
{
	pigen_rtl_object *record;
	char *copy;

	if (!model || object.index == PIGEN_INVALID_ID ||
		object.index >= model->object_count || !shape)
		return 0;
	copy = pigen_resize(NULL, strlen(shape) + 1);
	memcpy(copy, shape, strlen(shape) + 1);
	record = &model->objects[object.index];
	free(record->shape);
	record->shape = copy;
	return 1;
}

size_t pigen_rtl_source_reference_add(pigen_rtl_model *model,
	const pigen_rtl_source_reference *reference)
{
	if(!model || !reference || reference->role<PIGEN_RTL_SOURCE_SCOPE ||
		reference->role>PIGEN_RTL_SOURCE_INSTANCE_PORT ||
		!pigen_rtl_module_get(model,reference->module) || reference->scope.index==PIGEN_INVALID_ID ||
		!reference->source_path || !reference->source_path[0] ||
		!reference->emitted_name || !reference->emitted_name[0]) return SIZE_MAX;
	const pigen_rtl_object *object=pigen_rtl_object_get(model,reference->object);
	const pigen_rtl_instance *instance=pigen_rtl_instance_get(model,reference->instance);
	if(reference->role==PIGEN_RTL_SOURCE_SCOPE)
	{
		if(reference->object.index!=PIGEN_INVALID_ID || reference->instance.index!=PIGEN_INVALID_ID) return SIZE_MAX;
	}
	else if(reference->role==PIGEN_RTL_SOURCE_INSTANCE || reference->role==PIGEN_RTL_SOURCE_INSTANCE_PORT)
	{
		if(!instance || instance->module.index!=reference->module.index || reference->object.index!=PIGEN_INVALID_ID) return SIZE_MAX;
	}
	else if(!object || object->module.index!=reference->module.index || reference->instance.index!=PIGEN_INVALID_ID) return SIZE_MAX;
	for(size_t i=0;i<model->source_reference_count;i++)
	{
		const pigen_rtl_source_reference *other=&model->source_references[i];
		if(other->role==reference->role && other->module.index==reference->module.index &&
			other->scope.index==reference->scope.index && other->signal.index==reference->signal.index &&
			other->object.index==reference->object.index && other->instance.index==reference->instance.index &&
			!strcmp(other->source_path,reference->source_path))
			return !strcmp(other->emitted_name,reference->emitted_name) ? i : SIZE_MAX;
	}
	pigen_rtl_source_reference copy=*reference;
	copy.source_path=pigen_copy_range(reference->source_path,strlen(reference->source_path));
	copy.emitted_name=pigen_copy_range(reference->emitted_name,strlen(reference->emitted_name));
	if(model->source_reference_count==model->source_reference_capacity)
	{
		model->source_reference_capacity=model->source_reference_capacity ? model->source_reference_capacity*2 : 32;
		model->source_references=pigen_resize(model->source_references,
			model->source_reference_capacity*sizeof(*model->source_references));
	}
	size_t index=model->source_reference_count++;
	model->source_references[index]=copy;return index;
}

const pigen_rtl_source_reference *pigen_rtl_source_reference_get(
	const pigen_rtl_model *model,size_t index)
{
	return model && index<model->source_reference_count ? &model->source_references[index] : NULL;
}

void pigen_free_rtl_model(pigen_rtl_model *model)
{
    for(size_t i=0;i<model->source_reference_count;i++)
    {
        free(model->source_references[i].source_path);
        free(model->source_references[i].emitted_name);
    }
    free(model->source_references);
	for (size_t i = 0; i < model->type_count; ++i)
		free((void *)model->types[i].dimensions);
	for (size_t i = 0; i < model->expression_count; ++i)
	{
		free((void *)model->expressions[i].literal_words);
		if (model->expressions[i].kind == PIGEN_RTL_EXPR_SELECT)
			free(model->expressions[i].as.select.member_name);
	}
	for (size_t i = 0; i < model->name_count; ++i)
		free(model->names[i].text);
	for (size_t i = 0; i < model->connection_count; ++i)
		free((void *)model->connections[i].port);
	for (size_t i = 0; i < model->object_count; ++i)
	{
		free(model->objects[i].name);
		free(model->objects[i].shape);
		free(model->objects[i].declaration_type);
	}
	for (size_t i = 0; i < model->instance_count; ++i)
	{
		free(model->instances[i].name);
		free(model->instances[i].definition);
	}
	for (size_t i = 0; i < model->module_count; ++i)
		free(model->modules[i].name);
	free(model->connections);
	free(model->types);
	free(model->expressions);
	free(model->expression_children);
	free(model->objects);
	free(model->instances);
	free(model->equations);
	free(model->updates);
	free(model->procedural);
	free(model->procedural_children);
	free(model->processes);
	free(model->modules);
	free(model->instance_parameters);
	free(model->instance_connections);
	free(model->names);
	*model = (pigen_rtl_model){0};
}
