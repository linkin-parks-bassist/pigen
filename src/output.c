/* Ordered output model for one generated design (Task 9).
 *
 * The model owns the ordered slot array and the per-module nested-layout
 * ranges. Nesting ownership is implemented: pigen_output_item_open opens a
 * module's nested scope (its range starts empty at the end of the item array)
 * and pigen_output_item_add_child appends a child into that open scope, growing
 * the range and keeping the module item's layout in lockstep.
 * pigen_output_item_layout reads the owner-managed range back. Every other
 * accessor is a shape-only read.
 *
 * pigen_build_output_model is implemented: it walks the completed syntax tree
 * in source child order against the finished RTL lowering, appending an OPAQUE
 * item for each PIGEN_SYNTAX_OPAQUE node (its location.source_span); a MODULE
 * item for each PIGEN_SYNTAX_MODULE node (its published pigen_rtl_module_id),
 * opened with pigen_output_item_open in the top-level run so its structured
 * children nest inside its owner-managed scope; and the matching kind-typed
 * item for each structured node that lowers to a published RTL identity - the
 * signal declarator's payload object, the clocked process, the nonblocking
 * assignment's source-ready equation, and a storage signal's primitive
 * instance. A structured node with no published lowered identity is the
 * missing-lowered-identity error, the next in-order step and excluded here,
 * and the walk succeeds on a witness whose every structured node is lowered. A
 * NULL model returns -1; a call with neither a syntax tree nor a lowering is a
 * no-op that returns 0 with zero items; a call with exactly one of them
 * missing (or one with an empty semantic/RTL pair), a tree with no
 * compilation-unit root, an unresolved identity, or a failed append returns -1
 * and may leave partial state behind (how/should/compiler/builders/fail.md).
 * The monotonic-coverage gate (pigen_output_validate_coverage) and
 * nested-failure restoration arrive later. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/output.h"
#include "pigen/rtl.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"
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

/* The compilation-unit root is the single node of that kind (the parser
 * appends it first); a tree with no such node yields no walk. */
static const pigen_syntax_node *syntax_compilation_root(
	const pigen_syntax_tree *tree)
{
	for (size_t i = 0; i < tree->node_count; i++)
		if (tree->nodes[i].kind == PIGEN_SYNTAX_COMPILATION_UNIT)
			return &tree->nodes[i];
	return NULL;
}

/* Bounds-checked node fetch. The output-model test links only output.c,
 * source.c and util.c (no syntax.c), so pigen_syntax_get is unavailable and
 * the tree's public nodes array is read directly. */
static const pigen_syntax_node *syntax_node_get(const pigen_syntax_tree *tree,
	pigen_syntax_id node)
{
	if (node.index == PIGEN_INVALID_ID || node.index >= tree->node_count)
		return NULL;
	return &tree->nodes[node.index];
}

/* Resolve one MODULE syntax node to its published RTL module identity: the
 * semantic module arena index whose record's syntax field is this node (a
 * pigen_module_id is the arena index), then the RTL module whose
 * semantic_module provenance is that semantic module. */
static pigen_rtl_module_id rtl_module_for_syntax(const pigen_rtl_model *rtl,
	const pigen_semantic_model *sem, pigen_syntax_id node)
{
	for (size_t i = 0; i < sem->module_count; i++)
	{
		if (sem->modules[i].syntax.index != node.index)
			continue;
		pigen_module_id semantic = (pigen_module_id){(uint32_t)i};
		for (size_t m = 0; m < rtl->module_count; m++)
			if (rtl->modules[m].semantic_module.index == semantic.index)
				return (pigen_rtl_module_id){(uint32_t)m};
	}
	return (pigen_rtl_module_id){PIGEN_INVALID_ID};
}

/* Resolve one signal-declarator syntax node to the RTL object that carries its
 * payload: the semantic signal whose syntax field is this declarator (the
 * resolver records the declarator id, not the declaration id), then the RTL
 * object whose semantic_signal provenance is that signal's arena index. A
 * storage signal's declaration also publishes an instance, but only the payload
 * object carries the signal provenance, so this resolves unambiguously. */
static pigen_rtl_object_id signal_payload_for_syntax(const pigen_rtl_model *rtl,
	const pigen_semantic_model *sem, pigen_syntax_id node)
{
	for (size_t i = 0; i < sem->signal_count; i++)
	{
		if (sem->signals[i].syntax.index != node.index)
			continue;
		pigen_signal_id signal = (pigen_signal_id){(uint32_t)i};
		for (size_t o = 0; o < rtl->object_count; o++)
			if (rtl->objects[o].semantic_signal.index == signal.index)
				return (pigen_rtl_object_id){(uint32_t)o};
	}
	return (pigen_rtl_object_id){PIGEN_INVALID_ID};
}

/* Resolve a storage signal's published primitive instance by its origin span:
 * the lowering sets the instance origin to the owning signal's declaration
 * span, so the instance whose origin equals that span is the one. Only storage
 * signals publish instances, so a non-storage witness has none. */
static pigen_rtl_instance_id instance_for_span(const pigen_rtl_model *rtl,
	const pigen_source_span span)
{
	for (size_t x = 0; x < rtl->instance_count; x++)
	{
		const pigen_source_span *origin = &rtl->instances[x].origin;

		if (origin->source.index == span.source.index &&
			origin->start == span.start && origin->end == span.end)
			return (pigen_rtl_instance_id){(uint32_t)x};
	}
	return (pigen_rtl_instance_id){PIGEN_INVALID_ID};
}

/* Resolve one clocked-process syntax node to its published RTL process: the
 * semantic process whose syntax field is this node (the resolver records the
 * process node id), then the RTL process whose origin is that semantic
 * process's span. */
static pigen_rtl_process_id process_for_syntax(const pigen_rtl_model *rtl,
	const pigen_semantic_model *sem, pigen_syntax_id node)
{
	for (size_t i = 0; i < sem->process_count; i++)
	{
		if (sem->processes[i].syntax.index != node.index)
			continue;
		const pigen_source_span *span = &sem->processes[i].span;
		for (size_t p = 0; p < rtl->process_count; p++)
			if (rtl->processes[p].origin.source.index == span->source.index &&
				rtl->processes[p].origin.start == span->start &&
				rtl->processes[p].origin.end == span->end)
				return (pigen_rtl_process_id){(uint32_t)p};
	}
	return (pigen_rtl_process_id){PIGEN_INVALID_ID};
}

/* Resolve one nonblocking-assignment syntax node to the RTL source-ready
 * equation its transfer publishes: the semantic transfer whose syntax field is
 * this node (the resolver records the assignment node id), then the RTL
 * equation whose origin is that transfer's span. */
static pigen_rtl_equation_id equation_for_syntax(const pigen_rtl_model *rtl,
	const pigen_semantic_model *sem, pigen_syntax_id node)
{
	for (size_t i = 0; i < sem->transfer_count; i++)
	{
		if (sem->transfers[i].syntax.index != node.index)
			continue;
		const pigen_source_span *span = &sem->transfers[i].span;
		for (size_t e = 0; e < rtl->equation_count; e++)
			if (rtl->equations[e].origin.source.index == span->source.index &&
				rtl->equations[e].origin.start == span->start &&
				rtl->equations[e].origin.end == span->end)
				return (pigen_rtl_equation_id){(uint32_t)e};
	}
	return (pigen_rtl_equation_id){PIGEN_INVALID_ID};
}

/* Recurse one syntax node's children in source order. module_scope names the
 * open module whose nested scope receives the structured children (NULL for
 * the top-level run, which appends OPAQUE items and opens module scopes). */
static int walk_node(pigen_output_model *model,
	const pigen_syntax_tree *tree, const pigen_rtl_model *rtl,
	const pigen_semantic_model *sem, pigen_syntax_id node,
	const pigen_rtl_module_id *module_scope)
{
	const pigen_syntax_node *n = syntax_node_get(tree, node);

	if (!n)
		return -1;
	for (pigen_syntax_id child = n->first_child;
		child.index != PIGEN_INVALID_ID;
		child = tree->nodes[child.index].next_sibling)
	{
		const pigen_syntax_node *c = syntax_node_get(tree, child);

		if (!c)
			return -1;
		switch (c->kind) {
		case PIGEN_SYNTAX_OPAQUE:
			{
				pigen_output_item item = {0};

				item.kind = PIGEN_OUTPUT_OPAQUE;
				item.as.span = c->location.source_span;
				if (module_scope)
				{
					if (pigen_output_item_add_child(model, *module_scope, item)
						== PIGEN_INVALID_ID)
						return -1;
				}
				else
				{
					if (pigen_output_item_add(model, item) == PIGEN_INVALID_ID)
						return -1;
				}
			}
			break;
		case PIGEN_SYNTAX_MODULE:
			{
				pigen_rtl_module_id rtl_module;
				pigen_output_item item = {0};
				size_t slot;

				rtl_module = rtl_module_for_syntax(rtl, sem, child);
				if (rtl_module.index == PIGEN_INVALID_ID)
					return -1;
				item.kind = PIGEN_OUTPUT_MODULE;
				item.as.module = rtl_module;
				if (module_scope)
				{
					/* A module nested inside another: the owner-managed nested
					 * layout only tracks the top-level run, so nest it as a
					 * structured child without opening or recursing its scope. */
					if (pigen_output_item_add_child(model, *module_scope, item)
						== PIGEN_INVALID_ID)
						return -1;
				}
				else
				{
					slot = pigen_output_item_open(model, item);
					if (slot == PIGEN_INVALID_ID)
						return -1;
					if (walk_node(model, tree, rtl, sem, child, &rtl_module))
						return -1;
				}
			}
			break;
		case PIGEN_SYNTAX_SIGNAL_DECLARATION:
			{
				for (pigen_syntax_id d = c->first_child;
					d.index != PIGEN_INVALID_ID;
					d = tree->nodes[d.index].next_sibling)
				{
					pigen_rtl_object_id payload;
					pigen_rtl_instance_id instance;
					pigen_output_item item = {0};

					if (tree->nodes[d.index].kind !=
						PIGEN_SYNTAX_SIGNAL_DECLARATOR)
						continue;
					payload = signal_payload_for_syntax(rtl, sem, d);
					if (payload.index == PIGEN_INVALID_ID)
						return -1;
					item.kind = PIGEN_OUTPUT_RTL_OBJECT;
					item.as.object = payload;
					if (!module_scope ||
						pigen_output_item_add_child(model, *module_scope, item)
							== PIGEN_INVALID_ID)
						return -1;
					instance = instance_for_span(rtl,
						c->location.source_span);
					if (instance.index != PIGEN_INVALID_ID)
					{
						item.kind = PIGEN_OUTPUT_RTL_INSTANCE;
						item.as.instance = instance;
						if (!module_scope ||
							pigen_output_item_add_child(model, *module_scope, item)
								== PIGEN_INVALID_ID)
							return -1;
					}
				}
			}
			break;
		case PIGEN_SYNTAX_CLOCKED_PROCESS:
			{
				pigen_rtl_process_id proc;
				pigen_output_item item = {0};

				proc = process_for_syntax(rtl, sem, child);
				if (proc.index == PIGEN_INVALID_ID)
					return -1;
				item.kind = PIGEN_OUTPUT_RTL_PROCESS;
				item.as.process = proc;
				if (!module_scope ||
					pigen_output_item_add_child(model, *module_scope, item) ==
						PIGEN_INVALID_ID)
					return -1;
			}
			break;
		case PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT:
			{
				pigen_rtl_equation_id eq;
				pigen_output_item item = {0};

				eq = equation_for_syntax(rtl, sem, child);
				if (eq.index == PIGEN_INVALID_ID)
					return -1;
				item.kind = PIGEN_OUTPUT_RTL_EQUATION;
				item.as.equation = eq;
				if (!module_scope ||
					pigen_output_item_add_child(model, *module_scope, item) ==
						PIGEN_INVALID_ID)
					return -1;
			}
			break;
		case PIGEN_SYNTAX_PROCEDURAL_BLOCK:
			if (walk_node(model, tree, rtl, sem, child, module_scope))
				return -1;
			break;
		default:
			break;
		}
	}
	return 0;
}

/* Walk the completed syntax tree in source child order against the finished
 * lowering and fill the ordered output model: each PIGEN_SYNTAX_OPAQUE node
 * appends an OPAQUE item carrying its location.source_span; each MODULE node
 * appends the MODULE item (its published pigen_rtl_module_id) and, when it is
 * in the top-level run, opens its nested scope so its structured children are
 * appended inside it; each structured node that lowers to a published RTL
 * identity appends the matching kind-typed item. A structured node with no
 * published lowered identity is the missing-lowered-identity error, the next
 * in-order step and excluded here. The walk succeeds on a witness whose every
 * structured node is lowered. Returns 0 on success, -1 on error (NULL
 * model/syntax/lowering, no compilation-unit root, an unresolved identity, or
 * a failed append); a failure may leave partial state behind (how/should/
 * compiler/builders/fail.md). */
int pigen_build_output_model(pigen_output_model *model,
	const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering)
{
	const pigen_syntax_node *root;

	if (!model)
		return -1;
	if (!syntax && !lowering)
		return 0;
	if (!syntax || !lowering)
		return -1;
	if (!lowering->semantics || !lowering->rtl)
		return -1;
	root = syntax_compilation_root(syntax);
	if (!root)
		return -1;
	return walk_node(model, syntax, lowering->rtl, lowering->semantics,
		(pigen_syntax_id){root - syntax->nodes}, NULL);
}

void pigen_free_output_model(pigen_output_model *model)
{
	if (!model)
		return;
	free(model->items);
	free(model->nested_layouts);
	*model = (pigen_output_model){0};
}
