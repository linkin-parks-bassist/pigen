#include <assert.h>
#include <stdio.h>

#include "pigen/rtl.h"
#include "pigen/source.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

int main(void)
{
	pigen_rtl_model model = {0};
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "rtl.pigen",
		"module rtl; endmodule\n", 22);
	pigen_source_span first = {source, 7, 11};
	pigen_source_span second = {source, 13, 19};
	pigen_source_span synthetic = {INVALID_ID(pigen_source_id), 0, 0};
	pigen_rtl_type_id type;
	pigen_rtl_expr_id expression;
	pigen_rtl_object_id object;
	pigen_rtl_object_id synthetic_object;
	pigen_rtl_instance_id instance;
	pigen_rtl_equation_id equation;
	pigen_rtl_update_id update;
	pigen_rtl_process_id process;
	pigen_rtl_module_id module;
	size_t i;

	assert(!pigen_rtl_type_get(&model, INVALID_ID(pigen_rtl_type_id)));
	assert(!pigen_rtl_type_get(&model, (pigen_rtl_type_id){0}));
	assert(!pigen_rtl_expr_get(&model, INVALID_ID(pigen_rtl_expr_id)));
	assert(!pigen_rtl_expr_get(&model, (pigen_rtl_expr_id){0}));
	assert(!pigen_rtl_object_get(&model, INVALID_ID(pigen_rtl_object_id)));
	assert(!pigen_rtl_object_get(&model, (pigen_rtl_object_id){0}));
	assert(!pigen_rtl_instance_get(&model, INVALID_ID(pigen_rtl_instance_id)));
	assert(!pigen_rtl_instance_get(&model, (pigen_rtl_instance_id){0}));
	assert(!pigen_rtl_equation_get(&model, INVALID_ID(pigen_rtl_equation_id)));
	assert(!pigen_rtl_equation_get(&model, (pigen_rtl_equation_id){0}));
	assert(!pigen_rtl_update_get(&model, INVALID_ID(pigen_rtl_update_id)));
	assert(!pigen_rtl_update_get(&model, (pigen_rtl_update_id){0}));
	assert(!pigen_rtl_process_get(&model, INVALID_ID(pigen_rtl_process_id)));
	assert(!pigen_rtl_process_get(&model, (pigen_rtl_process_id){0}));
	assert(!pigen_rtl_module_get(&model, INVALID_ID(pigen_rtl_module_id)));
	assert(!pigen_rtl_module_get(&model, (pigen_rtl_module_id){0}));
	assert(!pigen_rtl_type_get(NULL, INVALID_ID(pigen_rtl_type_id)));
	assert(!pigen_rtl_expr_get(NULL, INVALID_ID(pigen_rtl_expr_id)));
	assert(!pigen_rtl_object_get(NULL, INVALID_ID(pigen_rtl_object_id)));
	assert(!pigen_rtl_instance_get(NULL, INVALID_ID(pigen_rtl_instance_id)));
	assert(!pigen_rtl_equation_get(NULL, INVALID_ID(pigen_rtl_equation_id)));
	assert(!pigen_rtl_update_get(NULL, INVALID_ID(pigen_rtl_update_id)));
	assert(!pigen_rtl_process_get(NULL, INVALID_ID(pigen_rtl_process_id)));
	assert(!pigen_rtl_module_get(NULL, INVALID_ID(pigen_rtl_module_id)));
	assert(!model.type_count && !model.type_capacity);
	assert(!model.expression_count && !model.expression_capacity);
	assert(!model.object_count && !model.object_capacity);
	assert(!model.instance_count && !model.instance_capacity);
	assert(!model.equation_count && !model.equation_capacity);
	assert(!model.update_count && !model.update_capacity);
	assert(!model.process_count && !model.process_capacity);
	assert(!model.module_count && !model.module_capacity);

	type = pigen_rtl_type_add(&model, first);
	expression = pigen_rtl_expr_add(&model, first);
	object = pigen_rtl_object_add(&model, first);
	instance = pigen_rtl_instance_add(&model, first);
	equation = pigen_rtl_equation_add(&model, first);
	update = pigen_rtl_update_add(&model, first);
	process = pigen_rtl_process_add(&model, first);
	module = pigen_rtl_module_add(&model, first);

	assert(type.index == 0 && expression.index == 0 && object.index == 0 &&
		instance.index == 0 && equation.index == 0 && update.index == 0 &&
		process.index == 0 && module.index == 0);
	assert(pigen_rtl_type_get(&model, type)->origin.start == 7 &&
		pigen_rtl_type_get(&model, type)->origin.end == 11);
	assert(pigen_rtl_expr_get(&model, expression)->origin.start == 7);
	assert(pigen_rtl_object_get(&model, object)->origin.start == 7);
	assert(pigen_rtl_instance_get(&model, instance)->origin.start == 7);
	assert(pigen_rtl_equation_get(&model, equation)->origin.start == 7);
	assert(pigen_rtl_update_get(&model, update)->origin.start == 7);
	assert(pigen_rtl_process_get(&model, process)->origin.start == 7);
	assert(pigen_rtl_module_get(&model, module)->origin.start == 7);

	synthetic_object = pigen_rtl_object_add(&model, synthetic);
	assert(pigen_rtl_object_get(&model, synthetic_object)
		->origin.source.index == PIGEN_INVALID_ID);
	assert(pigen_rtl_object_get(&model, object)
		->origin.source.index == source.index);

	for (i = 0; i < 32; i++)
		(void)pigen_rtl_object_add(&model, second);
	assert(model.object_count == 34);
	assert(pigen_rtl_object_get(&model, object)->origin.start == 7);
	assert(!pigen_rtl_object_get(&model,
		(pigen_rtl_object_id){model.object_count}));
	assert(!pigen_rtl_object_get(&model,
		(pigen_rtl_object_id){model.object_count + 5}));
	assert(pigen_rtl_object_get(&model, (pigen_rtl_object_id){33})
		->origin.start == 13);

	pigen_free_rtl_model(&model);
	assert(!model.types && !model.type_count && !model.type_capacity);
	assert(!model.expressions && !model.expression_count &&
		!model.expression_capacity);
	assert(!model.objects && !model.object_count && !model.object_capacity);
	assert(!model.instances && !model.instance_count &&
		!model.instance_capacity);
	assert(!model.equations && !model.equation_count &&
		!model.equation_capacity);
	assert(!model.updates && !model.update_count && !model.update_capacity);
	assert(!model.processes && !model.process_count &&
		!model.process_capacity);
	assert(!model.modules && !model.module_count && !model.module_capacity);

	pigen_free_sources(&sources);
	puts("PASS: rtl arena holds distinct identities with checked accessors");
	return 0;
}
