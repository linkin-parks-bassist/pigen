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

/* Resolve the published RTL identity a structured syntax node lowers to, if
 * any. node_index is the node's own id (its position in the syntax tree's
 * node array). The node maps to its semantic record (the record whose syntax
 * field is node_index), then to the matching RTL record through that record's
 * semantic provenance field (the module's semantic_module, the object's
 * semantic_signal). A structured node with no published identity is the
 * missing-lowered-identity error, not part of this walk. Returns 1 and fills
 * *item (its layout stays zero; the model owns it) when an identity is
 * published, 0 otherwise. */
static int node_identity(uint32_t node_index, const pigen_syntax_node *node,
	const pigen_rtl_lowering *lowering, pigen_output_item *item)
{
	const pigen_semantic_model *sem = lowering->semantics;
	const pigen_rtl_model *rtl = lowering->rtl;
	size_t i;

	switch (node->kind)
	{
	case PIGEN_SYNTAX_MODULE:
		for (i = 0; i < sem->module_count; i++)
			if (sem->modules[i].syntax.index == node_index)
			{
				for (size_t j = 0; j < rtl->module_count; j++)
					if (rtl->modules[j].semantic_module.index == i)
					{
						item->kind = PIGEN_OUTPUT_MODULE;
						item->as.module = (pigen_rtl_module_id){j};
						return 1;
					}
			}
		return 0;
	case PIGEN_SYNTAX_SIGNAL_DECLARATOR:
		for (i = 0; i < sem->signal_count; i++)
			if (sem->signals[i].syntax.index == node_index)
			{
				for (size_t j = 0; j < rtl->object_count; j++)
					if (rtl->objects[j].semantic_signal.index == i)
					{
						item->kind = PIGEN_OUTPUT_RTL_OBJECT;
						item->as.object = (pigen_rtl_object_id){j};
						return 1;
					}
			}
		return 0;
	default:
		/* A kind whose lowering does not publish a record through its
		 * semantic provenance is not an emitted identity in this walk. */
		return 0;
	}
}

static int emit_node(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, uint32_t node_index,
	pigen_rtl_module_id module, int in_module);

/* Recurse over one node's children in source order. */
static int emit_children(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, const pigen_syntax_node *node,
	pigen_rtl_module_id module, int in_module)
{
	for (uint32_t child = node->first_child.index;
		child != PIGEN_INVALID_ID;
		child = pigen_syntax_get(syntax, (pigen_syntax_id){child})->next_sibling.index)
		if (emit_node(model, syntax, lowering, child, module, in_module))
			return -1;
	return 0;
}

/* Emit one node and, for a module, open its nested scope so its children nest
 * inside it. Top-level items (outside any open module) are appended flat; the
 * compilation-unit root itself is never emitted. */
static int emit_node(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, uint32_t node_index,
	pigen_rtl_module_id module, int in_module)
{
	const pigen_syntax_node *node;
	pigen_output_item item = {0};

	node = pigen_syntax_get(syntax, (pigen_syntax_id){node_index});
	if (!node)
		return -1;
	if (node->kind == PIGEN_SYNTAX_OPAQUE)
	{
		item.kind = PIGEN_OUTPUT_OPAQUE;
		item.as.span = node->location.source_span;
		if (in_module)
			return pigen_output_item_add_child(model, module, item) ==
				PIGEN_INVALID_ID ? -1 : 0;
		return pigen_output_item_add(model, item) == PIGEN_INVALID_ID ? -1 : 0;
	}
	if (node->kind != PIGEN_SYNTAX_COMPILATION_UNIT &&
		node_identity(node_index, node, lowering, &item))
	{
		if (item.kind == PIGEN_OUTPUT_MODULE)
		{
			module = item.as.module;
			in_module = 1;
			/* The module item opens its nested scope: its children nest
			 * inside it, so it is appended with open, not a flat add. */
			return pigen_output_item_open(model, item) == PIGEN_INVALID_ID ?
				-1 : emit_children(model, syntax, lowering, node, module, 1);
		}
		if (in_module)
			return pigen_output_item_add_child(model, module, item) ==
				PIGEN_INVALID_ID ? -1 : emit_children(model, syntax, lowering, node, module, 1);
		return pigen_output_item_add(model, item) == PIGEN_INVALID_ID ? -1 :
			emit_children(model, syntax, lowering, node, module, in_module);
	}
	/* A structured node with no published identity is the next step, not an
	 * error here; a non-emitted container is skipped. */
	return emit_children(model, syntax, lowering, node, module, in_module);
}

int pigen_output_error_is_set(const pigen_output_error *error)
{
	if (!error || !error->message)
		return 0;
	if (error->span.start > error->span.end)
		return 0;
	return 1;
}

int pigen_build_output_model(pigen_output_model *model,
	const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering,
	pigen_output_error *error)
{
	const pigen_syntax_node *root;

	if (error)
		*error = (pigen_output_error){0};
	if (!model || !syntax || !lowering)
		return -1;
	(void)error;
	root = pigen_syntax_get(syntax, (pigen_syntax_id){0});
	if (!root)
		return -1;
	return emit_node(model, syntax, lowering, 0, (pigen_rtl_module_id){PIGEN_INVALID_ID}, 0);
}

void pigen_free_output_model(pigen_output_model *model)
{
	if (!model)
		return;
	free(model->items);
	free(model->nested_layouts);
	*model = (pigen_output_model){0};
}
