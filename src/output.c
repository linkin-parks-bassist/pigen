/* Ordered output model for one generated design (Task 9).
 *
 * The model owns the ordered slot array and the per-module nested-layout
 * ranges. Nesting ownership is implemented: pigen_output_item_open opens a
 * module's nested scope (its range starts empty at the end of the item array)
 * and pigen_output_item_add_child appends a child into that open scope, growing
 * the range and keeping the module item's layout in lockstep.
 * pigen_output_item_layout reads the owner-managed range back. Every other
 * accessor is a shape-only read. The syntax-tree build walk, the
 * monotonic-coverage gate, nested-failure restoration and the
 * missing-identity diagnostic still arrive later; the builder stub succeeds
 * with zero items. */
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

/* Grow the owner-managed nested-layout array so that index < needed is
 * addressable, zeroing the grown tail so a sparse module identity reads an
 * empty range until a scope is opened for it. The allocator exits on failure,
 * so this cannot return NULL. */
static void nested_ensure(pigen_output_model *model, size_t needed)
{
	size_t old_capacity = model->nested_layout_capacity;
	size_t capacity = old_capacity;

	if (needed <= old_capacity)
		return;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	model->nested_layouts =
		pigen_resize(model->nested_layouts, capacity * sizeof(*model->nested_layouts));
	model->nested_layout_capacity = capacity;
	if (needed > model->nested_layout_count)
		model->nested_layout_count = needed;
	memset(model->nested_layouts + old_capacity, 0,
		(capacity - old_capacity) * sizeof(*model->nested_layouts));
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

size_t pigen_output_item_open(pigen_output_model *model, pigen_output_item item)
{
	size_t slot;
	uint32_t key;

	if (!model || item.kind != PIGEN_OUTPUT_MODULE)
		return PIGEN_INVALID_ID;
	key = item.as.module.index;
	if (key == PIGEN_INVALID_ID)
		return PIGEN_INVALID_ID;
	nested_ensure(model, (size_t)key + 1);
	item.layout = (pigen_rtl_record_range){ model->item_count + 1, 0 };
	slot = pigen_output_item_add(model, item);
	if (slot == PIGEN_INVALID_ID)
		return PIGEN_INVALID_ID;
	model->nested_layouts[key] = item.layout;
	return slot;
}

size_t pigen_output_item_add_child(pigen_output_model *model,
	pigen_rtl_module_id module, pigen_output_item child)
{
	size_t slot;
	uint32_t key;

	if (!model)
		return PIGEN_INVALID_ID;
	key = module.index;
	if (key == PIGEN_INVALID_ID || (size_t)key >= model->nested_layout_count)
		return PIGEN_INVALID_ID;
	if (model->nested_layouts[key].count == 0 &&
		model->nested_layouts[key].first == 0)
		return PIGEN_INVALID_ID;
	child.layout = (pigen_rtl_record_range){0, 0};
	slot = pigen_output_item_add(model, child);
	if (slot == PIGEN_INVALID_ID)
		return PIGEN_INVALID_ID;
	model->nested_layouts[key].count++;
	model->items[model->nested_layouts[key].first - 1].layout =
		model->nested_layouts[key];
	return slot;
}

pigen_rtl_record_range pigen_output_item_layout(const pigen_output_model *model,
	size_t module_slot)
{
	pigen_rtl_record_range empty = {0, 0};
	pigen_output_item item;
	uint32_t key;

	if (!model || module_slot >= model->item_count)
		return empty;
	item = model->items[module_slot];
	if (item.kind != PIGEN_OUTPUT_MODULE)
		return empty;
	key = item.as.module.index;
	if (key == PIGEN_INVALID_ID || (size_t)key >= model->nested_layout_capacity)
		return empty;
	return model->nested_layouts[key];
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

pigen_output_coverage_result pigen_output_validate_coverage(
	const pigen_output_model *model, const pigen_source_manager *manager)
{
	size_t i;
	size_t opaque_count = 0;
	size_t cursor = 0;
	size_t end;
	const pigen_source_file *file;

	if (!model || !manager)
		return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_INVALID };

	/* Count the OPAQUE spans in slot order first so that a model with none of
	 * them reads EMPTY regardless of its structured items. */
	for (i = 0; i < model->item_count; i++)
		if (model->items[i].kind == PIGEN_OUTPUT_OPAQUE)
			opaque_count++;
	if (opaque_count == 0)
		return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_EMPTY };

	/* The manager is the single-source authority the caller passes; its first
	 * file's length is the expected end the run must reach exactly. */
	file = pigen_source_get(manager, (pigen_source_id){0});
	if (!file)
		return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_INVALID };
	end = file->length;

	for (i = 0; i < model->item_count; i++)
	{
		pigen_source_span s;

		if (model->items[i].kind != PIGEN_OUTPUT_OPAQUE)
			continue;
		s = model->items[i].as.span;
		if (!pigen_source_get(manager, s.source))
			return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_INVALID };
		if (s.source.index != 0u)
			return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_WRONG_SOURCE };
		if (s.end <= s.start || s.end > end)
			return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_REVERSAL };
		if (s.start < cursor)
			return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_OVERLAP };
		if (s.start > cursor)
			return (pigen_output_coverage_result){ 0, PIGEN_OUTPUT_COVERAGE_GAP };
		cursor = s.end;
	}

	if (cursor != end)
		return (pigen_output_coverage_result){ 0,
			cursor < end ? PIGEN_OUTPUT_COVERAGE_GAP
			             : PIGEN_OUTPUT_COVERAGE_OVERLAP };
	return (pigen_output_coverage_result){ 1, PIGEN_OUTPUT_COVERAGE_OK };
}

int pigen_build_output_model(pigen_output_model *model,
	const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering)
{
	if (!model)
		return -1;
	/* The exact ordered build arrives with the implementation stage; the
	 * stub succeeds with zero items. */
	(void)syntax;
	(void)lowering;
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
