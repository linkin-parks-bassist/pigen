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
	free(model->types);
	free(model->expressions);
	free(model->objects);
	free(model->instances);
	free(model->equations);
	free(model->updates);
	free(model->processes);
	free(model->modules);
	*model = (pigen_rtl_model){0};
}
