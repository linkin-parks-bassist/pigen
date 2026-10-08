/* Ordered output model for one generated design (Task 9).
 *
 * The model owns the ordered slot array, the per-module nested-layout ranges
 * (keyed by the module's arena index) and the stack of open nested scopes.
 * Every public accessor is a shape-only read. The syntax-tree build walk, the
 * monotonic-coverage gate, nested-failure restoration and the
 * missing-identity diagnostic arrive with the implementation stage; the
 * builder stub succeeds with zero items. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/output.h"
#include "pigen/util.h"

void pigen_output_model_init(pigen_output_model *model)
{
	if (!model)
		return;
	*model = (pigen_output_model){0};
}

/* Grow the owned slot array so that index < needed is addressable. The
 * allocator exits on failure, so this cannot return NULL. */
static void item_ensure(pigen_output_model *model, size_t needed)
{
	size_t capacity = model->item_capacity;

	if (model->item_count >= needed && model->items)
		return;
	if (needed <= model->item_capacity)
		return;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	model->items = pigen_resize(model->items, capacity * sizeof(*model->items));
	model->item_capacity = capacity;
}

size_t pigen_output_item_add(pigen_output_model *model, pigen_output_item item)
{
	size_t index;

	if (!model)
		return PIGEN_INVALID_ID;
	item_ensure(model, model->item_count + 1);
	index = model->item_count;
	model->items[index] = item;
	model->item_count++;
	return index;
}

/* Grow the per-module nested-layout array so that index < needed is
 * addressable, keyed by the module's arena index (its identity). The
 * allocator exits on failure, so this cannot return NULL. The grown tail is
 * zeroed so unowned slots read back as an empty range. */
static void nested_ensure(pigen_output_model *model, size_t needed)
{
	size_t old_bytes, capacity = model->nested_layout_capacity;
	char *base;

	if (model->nested_layout_count >= needed && model->nested_layouts)
		return;
	if (needed <= model->nested_layout_capacity)
		return;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	old_bytes = model->nested_layout_capacity * sizeof(*model->nested_layouts);
	model->nested_layouts =
		pigen_resize(model->nested_layouts,
			capacity * sizeof(*model->nested_layouts));
	base = (char *)model->nested_layouts;
	memset(base + old_bytes, 0,
		(size_t)capacity * sizeof(*model->nested_layouts) - old_bytes);
	model->nested_layout_capacity = capacity;
}

/* The module's slot index in the item array, or PIGEN_INVALID_ID when no
 * module item carries the identity (the model never validates identities
 * against an RTL owner, so a missing owner simply has no open scope). */
static size_t module_slot(const pigen_output_model *model,
	pigen_rtl_module_id module)
{
	for (size_t i = 0; i < model->item_count; i++) {
		if (model->items[i].kind == PIGEN_OUTPUT_MODULE &&
			model->items[i].as.module.index == module.index)
			return i;
	}
	return PIGEN_INVALID_ID;
}

pigen_rtl_module_id pigen_output_item_open(pigen_output_model *model,
	pigen_output_item module)
{
	size_t index;
	pigen_rtl_module_id invalid = { PIGEN_INVALID_ID };

	if (!model || module.kind != PIGEN_OUTPUT_MODULE ||
		module.as.module.index == PIGEN_INVALID_ID)
		return invalid;
	/* The module's arena index keys the parallel view; ensure it fits and
	 * mark it as an owned (open) scope with an empty range. */
	nested_ensure(model, (size_t)module.as.module.index + 1);
	if ((size_t)module.as.module.index >= model->nested_layout_count)
		model->nested_layout_count = (size_t)module.as.module.index + 1;
	index = pigen_output_item_add(model, module);
	if (index == PIGEN_INVALID_ID)
		return invalid;
	/* Open the module's nested scope: its range is owner-managed and starts
	 * empty at the current end of the item array (the child slots follow the
	 * module item, so first = slot + 1). */
	{
		pigen_rtl_record_range range = { index + 1, 0 };

		model->items[index].layout = range;
		model->nested_layouts[module.as.module.index] = range;
	}
	return module.as.module;
}

size_t pigen_output_item_add_child(pigen_output_model *model,
	pigen_rtl_module_id module, pigen_output_item item)
{
	size_t slot, index;
	pigen_rtl_record_range range;

	if (!model || module.index == PIGEN_INVALID_ID)
		return PIGEN_INVALID_ID;
	/* The child is only appended into a scope the model opened: the module
	 * item must carry the identity and have an owned range. A module appended
	 * without an open scope (top-level add) has no owned range, so its slot
	 * exists but its index is beyond the owned view. */
	slot = module_slot(model, module);
	if (slot == PIGEN_INVALID_ID)
		return PIGEN_INVALID_ID;
	if ((size_t)module.index >= model->nested_layout_count)
		return PIGEN_INVALID_ID;
	range = model->nested_layouts[module.index];
	item_ensure(model, model->item_count + 1);
	index = model->item_count;
	model->items[index] = item;
	model->item_count++;
	/* The child occupies the next slot of the module's range, which grows to
	 * include it (first is unchanged, count advances by one). */
	range.count++;
	model->nested_layouts[module.index] = range;
	model->items[slot].layout = range;
	return index;
}

pigen_rtl_record_range pigen_output_item_layout(const pigen_output_model *model,
	pigen_rtl_module_id module)
{
	pigen_rtl_record_range empty = { 0, 0 };

	if (!model || module.index == PIGEN_INVALID_ID)
		return empty;
	if ((size_t)module.index >= model->nested_layout_count)
		return empty;
	return model->nested_layouts[module.index];
}

size_t pigen_output_item_count(const pigen_output_model *model)
{
	if (!model)
		return 0;
	return model->item_count;
}

pigen_output_item pigen_output_item_get(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item empty = {0};

	if (!model || index >= model->item_count)
		return empty;
	return model->items[index];
}

pigen_output_kind pigen_output_item_kind(const pigen_output_model *model,
	size_t index)
{
	return pigen_output_item_get(model, index).kind;
}

pigen_source_span pigen_output_item_span(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_OPAQUE)
		return (pigen_source_span){ (pigen_source_id){PIGEN_INVALID_ID}, 0, 0 };
	return item.as.span;
}

pigen_rtl_module_id pigen_output_item_module(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;
	pigen_rtl_module_id invalid = {PIGEN_INVALID_ID};

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_MODULE)
		return invalid;
	return item.as.module;
}

pigen_rtl_object_id pigen_output_item_object(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;
	pigen_rtl_object_id invalid = {PIGEN_INVALID_ID};

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_RTL_OBJECT)
		return invalid;
	return item.as.object;
}

pigen_rtl_instance_id pigen_output_item_instance(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;
	pigen_rtl_instance_id invalid = {PIGEN_INVALID_ID};

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_RTL_INSTANCE)
		return invalid;
	return item.as.instance;
}

pigen_rtl_equation_id pigen_output_item_equation(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;
	pigen_rtl_equation_id invalid = {PIGEN_INVALID_ID};

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_RTL_EQUATION)
		return invalid;
	return item.as.equation;
}

pigen_rtl_process_id pigen_output_item_process(const pigen_output_model *model,
	size_t index)
{
	pigen_output_item item;
	pigen_rtl_process_id invalid = {PIGEN_INVALID_ID};

	item = pigen_output_item_get(model, index);
	if (item.kind != PIGEN_OUTPUT_RTL_PROCESS)
		return invalid;
	return item.as.process;
}

int pigen_build_output_model(pigen_output_model *model)
{
	if (!model)
		return -1;
	/* The exact ordered build arrives with the implementation stage; the
	 * stub succeeds with zero items. */
	(void)pigen_output_item_count(model);
	return 0;
}

void pigen_free_output_model(pigen_output_model *model)
{
	if (!model)
		return;
	free(model->items);
	free(model->nested_layouts);
	*model = (pigen_output_model){0};
}
