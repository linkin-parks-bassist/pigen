/* Ordered output model for one generated design (Task 9).
 *
 * The model owns the ordered slot array and the per-module nested-layout
 * ranges. Nesting ownership is implemented: pigen_output_item_open opens a
 * module's nested scope (its range starts empty at the end of the item array)
 * and pigen_output_item_add_child appends a child into that open scope, growing
 * the range and keeping the module item's layout in lockstep.
 * pigen_output_item_layout reads the owner-managed range back. The syntax-tree
 * build walk, the monotonic-coverage gate and the missing-lowered-identity
 * diagnostic are implemented: pigen_build_output_model walks a fully-lowered
 * tree to a model of the same order as the source, and reports a missing
 * identity at its node's span. */
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

/* A half-open span is valid when its source is a real source id. */
static int out_span_valid(const pigen_source_span *span)
{
	return span->source.index != PIGEN_INVALID_ID;
}

/* True when the half-open span A lies entirely within the half-open span B
 * (the same source). */
static int out_span_within(const pigen_source_span *a, const pigen_source_span *b)
{
	if (!out_span_valid(a) || !out_span_valid(b))
		return 0;
	return a->source.index == b->source.index &&
		a->start >= b->start && a->end <= b->end;
}

/* Mark every object the expression subtree rooted at E references - the
 * subtree's direct OBJECT operands and each child's subtree - in the boolean
 * set (capacity count). */
static void out_expr_objects_mark(const pigen_rtl_model *rtl,
	pigen_rtl_expr_id e, int *marked, size_t count)
{
	const pigen_rtl_expr *expr;
	const pigen_rtl_expr_id *children;
	size_t child_count = 0;
	size_t i;

	expr = pigen_rtl_expr_get(rtl, e);
	if (!expr)
		return;
	if (expr->kind == PIGEN_RTL_EXPR_OBJECT &&
		expr->object.index != PIGEN_INVALID_ID &&
		expr->object.index < count)
		marked[expr->object.index] = 1;
	children = pigen_rtl_expr_children(rtl, e, &child_count);
	if (!children)
		return;
	for (i = 0; i < child_count; i++)
		out_expr_objects_mark(rtl, children[i], marked, count);
}

/* Emit one pipeline statement's published RTL records as the module's
 * children: the production pipeline step (src/rtl_lower.c,
 * pigen_lower_rtl_pipelines) publishes the pipeline's one-entry buf stage
 * storage, its per-stage next-value equations, its guarded ready/valid
 * handshake and its yield write into the parent module's shared record, and
 * the pipeline's own field/stage syntax nodes publish no declarator
 * identities, so the walk adds the pipeline's records directly instead of
 * recursing into the pipeline's children. The records are the module's RTL
 * objects, instances and equations whose origin lies within the pipeline
 * statement's span, plus the pipeline's synthetic records with an invalid
 * origin (the ready object and ready equation), plus every object the
 * pipeline's equations and instance connections reference from outside the
 * span (the enclosing guard object, an enclosing module-signal read object).
 * A pipeline node outside any open module is a container the walk keeps
 * recursing (the resolve stage diagnoses a module-level pipeline, so this
 * walk only sees one inside a module). */
static int emit_pipeline_records(pigen_output_model *model,
	const pigen_rtl_lowering *lowering, const pigen_syntax_node *pipeline,
	pigen_rtl_module_id module, pigen_output_error *error)
{
	const pigen_rtl_model *rtl = lowering->rtl;
	const pigen_source_span span = pipeline->location.source_span;
	int *marked = NULL;
	size_t i;
	int rc = -1;

	if (!out_span_valid(&span))
		goto out;
	marked = calloc(rtl->object_count ? rtl->object_count : 1, sizeof(*marked));
	if (!marked)
		goto out;

	/* The objects and instances the pipeline's own records reference. */
	for (i = 0; i < rtl->equation_count; i++)
	{
		const pigen_rtl_equation *equation = &rtl->equations[i];

		if (equation->module.index != module.index)
			continue;
		if (out_span_valid(&equation->origin) &&
			!out_span_within(&equation->origin, &span))
			continue;
		out_expr_objects_mark(rtl, equation->value, marked,
			rtl->object_count);
		/* The synthetic ready equation carries an invalid origin and its
		 * value (the guard, a constant when unguarded) references no object,
		 * so its destination - the synthetic ready object - is marked here or
		 * the equation's destination would have no emitted record. */
		if (!out_span_valid(&equation->origin) &&
			equation->destination.index != PIGEN_INVALID_ID &&
			equation->destination.index < rtl->object_count &&
			!out_span_valid(&rtl->objects[equation->destination.index].origin))
			marked[equation->destination.index] = 1;
	}
	for (i = 0; i < rtl->instance_count; i++)
	{
		const pigen_rtl_instance *instance = &rtl->instances[i];
		size_t j;

		if (instance->module.index != module.index ||
			!out_span_within(&instance->origin, &span))
			continue;
		for (j = 0; j < instance->connections.count; j++)
		{
			pigen_rtl_object_id c =
				rtl->instance_connections[instance->connections.first + j];

			if (c.index != PIGEN_INVALID_ID && c.index < rtl->object_count)
				marked[c.index] = 1;
		}
	}

	/* The objects, in arena order. */
	for (i = 0; i < rtl->object_count; i++)
	{
		const pigen_rtl_object *object = &rtl->objects[i];
		pigen_output_item item = {0};

		if (object->module.index != module.index)
			continue;
		if (out_span_valid(&object->origin))
		{
			if (!out_span_within(&object->origin, &span) && !marked[i])
				continue;
		}
		else if (!marked[i])
			continue;
		item.kind = PIGEN_OUTPUT_RTL_OBJECT;
		item.as.object = (pigen_rtl_object_id){(uint32_t)i};
		if (pigen_output_item_add_child(model, module, item) ==
			PIGEN_INVALID_ID)
			goto out;
	}
	/* The buf stage-storage instances, in arena order. */
	for (i = 0; i < rtl->instance_count; i++)
	{
		const pigen_rtl_instance *instance = &rtl->instances[i];
		pigen_output_item item = {0};

		if (instance->module.index != module.index ||
			!out_span_within(&instance->origin, &span))
			continue;
		item.kind = PIGEN_OUTPUT_RTL_INSTANCE;
		item.as.instance = (pigen_rtl_instance_id){(uint32_t)i};
		if (pigen_output_item_add_child(model, module, item) ==
			PIGEN_INVALID_ID)
			goto out;
	}
	/* The per-stage next-value, yield-write and ready equations, in arena
	 * order. */
	for (i = 0; i < rtl->equation_count; i++)
	{
		const pigen_rtl_equation *equation = &rtl->equations[i];
		pigen_output_item item = {0};

		if (equation->module.index != module.index)
			continue;
		if (out_span_valid(&equation->origin) &&
			!out_span_within(&equation->origin, &span))
			continue;
		item.kind = PIGEN_OUTPUT_RTL_EQUATION;
		item.as.equation = (pigen_rtl_equation_id){(uint32_t)i};
		if (pigen_output_item_add_child(model, module, item) ==
			PIGEN_INVALID_ID)
			goto out;
	}
	rc = 0;
out:
	free(marked);
	if (rc != 0 && error)
	{
		error->span = span;
		error->message = "pipeline RTL records are not publishable";
	}
	return rc;
}

static int emit_node(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, uint32_t node_index,
	pigen_rtl_module_id module, int in_module, pigen_output_error *error);

/* Append one published record as a child of the module's open scope. */
static int add_record_item(pigen_output_model *model,
	pigen_rtl_module_id module, pigen_output_item item)
{
	return pigen_output_item_add_child(model, module, item) ==
		PIGEN_INVALID_ID ? -1 : 0;
}

/* The FSM's published records inside the module's shared RTL record, in
 * source order. The lowering composes the record (one payload object per
 * signal, one source-ready equation per transfer, one process per semantic
 * process) and the FSM step appends its own records at each range's tail, one
 * FSM per module child in source order: the k-th FSM child publishes the k-th
 * synthetic state register (the record's only kind-VARIABLE object without
 * signal provenance), one action equation past the composed per-transfer
 * equations, and one posedge process past the composed per-process processes.
 * RECORD is the parent module's RTL record (the walk's open module); RANK is
 * the FSM's source order among the module's FSM children. Returns 0 on
 * success, -1 when the record carries no published state register or process
 * for this FSM (a missing-lowered-identity condition the caller reports). */
static int emit_fsm_records(pigen_output_model *model,
	const pigen_rtl_lowering *lowering, pigen_rtl_module_id module,
	size_t fsm_rank)
{
	const pigen_semantic_model *sem = lowering->semantics;
	const pigen_rtl_model *rtl = lowering->rtl;
	const pigen_rtl_module *module_record;
	size_t composed_equations = 0;
	size_t composed_processes = 0;
	size_t synthetic = 0;
	size_t i;
	int emitted_object = 0;
	int emitted_equation = 0;
	int emitted_process = 0;

	module_record = pigen_rtl_module_get(rtl, module);
	if (!module_record)
		return -1;

	for (i = 0; i < sem->transfer_count; i++)
		if (sem->transfers[i].module.index ==
			module_record->semantic_module.index)
			composed_equations++;
	for (i = 0; i < sem->process_count; i++)
		if (sem->processes[i].module.index ==
			module_record->semantic_module.index)
			composed_processes++;

	for (i = 0; i < module_record->objects.count; i++)
	{
		const pigen_rtl_object *object =
			&rtl->objects[module_record->objects.first + i];

		if (object->kind != PIGEN_RTL_OBJECT_VARIABLE ||
			object->semantic_signal.index != PIGEN_INVALID_ID)
			continue;
		if (synthetic == fsm_rank)
		{
			pigen_output_item item = {0};

			item.kind = PIGEN_OUTPUT_RTL_OBJECT;
			item.as.object = (pigen_rtl_object_id){
				(uint32_t)(module_record->objects.first + i)};
			if (add_record_item(model, module, item))
				return -1;
			emitted_object = 1;
		}
		synthetic++;
	}

	if (module_record->equations.count > composed_equations + fsm_rank)
	{
		pigen_output_item item = {0};

		item.kind = PIGEN_OUTPUT_RTL_EQUATION;
		item.as.equation = (pigen_rtl_equation_id){
			(uint32_t)(module_record->equations.first +
				composed_equations + fsm_rank)};
		if (add_record_item(model, module, item))
			return -1;
		emitted_equation = 1;
	}

	if (module_record->processes.count > composed_processes + fsm_rank)
	{
		pigen_output_item item = {0};

		item.kind = PIGEN_OUTPUT_RTL_PROCESS;
		item.as.process = (pigen_rtl_process_id){
			(uint32_t)(module_record->processes.first +
				composed_processes + fsm_rank)};
		if (add_record_item(model, module, item))
			return -1;
		emitted_process = 1;
	}

	if (!emitted_object || !emitted_process)
		return -1;
	(void)emitted_equation;
	return 0;
}

/* The module's FSM children in source order: the index of NODE among the
 * PIGEN_SYNTAX_FSM children of MODULE_NODE (its publication rank), or -1 when
 * NODE is not one of them. */
static long fsm_child_rank(const pigen_syntax_tree *syntax,
	const pigen_syntax_node *module_node, const pigen_syntax_node *fsm_node)
{
	uint32_t child;
	long rank = -1;

	for (child = module_node->first_child.index; child != PIGEN_INVALID_ID;
		child = pigen_syntax_get(syntax, (pigen_syntax_id){child})->next_sibling.index)
	{
		const pigen_syntax_node *node = pigen_syntax_get(syntax, (pigen_syntax_id){child});

		if (node->kind != PIGEN_SYNTAX_FSM)
			continue;
		rank++;
		if (node == fsm_node)
			return rank;
	}
	return -1;
}

/* Recurse over one node's children in source order. */
static int emit_children(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, const pigen_syntax_node *node,
	pigen_rtl_module_id module, int in_module, pigen_output_error *error)
{
	for (uint32_t child = node->first_child.index;
		child != PIGEN_INVALID_ID;
		child = pigen_syntax_get(syntax, (pigen_syntax_id){child})->next_sibling.index)
		if (emit_node(model, syntax, lowering, child, module, in_module, error))
			return -1;
	return 0;
}

/* Emit one node and, for a module, open its nested scope so its children nest
 * inside it. Top-level items (outside any open module) are appended flat; the
 * compilation-unit root itself is never emitted. A node the walk is required to
 * lower (a module or a signal declarator) that has no published identity is the
 * missing-lowered-identity diagnostic: it is reported at the node's own span
 * through error (when non-NULL) and the walk stops, returning -1. */
static int emit_node(pigen_output_model *model, const pigen_syntax_tree *syntax,
	const pigen_rtl_lowering *lowering, uint32_t node_index,
	pigen_rtl_module_id module, int in_module, pigen_output_error *error)
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
	if (node->kind == PIGEN_SYNTAX_PIPELINE)
	{
		/* The pipeline's published records nest in the parent module; the
		 * pipeline's own children publish no declarator identities, so the
		 * walk never recurses into them. */
		if (in_module)
			return emit_pipeline_records(model, lowering, node, module, error);
		return emit_children(model, syntax, lowering, node, module, in_module, error);
	}
	/* A module-level FSM is a non-emitted container whose lowering publishes
	 * its records into the shared module record: emit those published records
	 * (the state register, the action equation and the posedge process) as
	 * children of the enclosing module in source order, then keep recursing. */
	if (node->kind == PIGEN_SYNTAX_FSM && in_module)
	{
		const pigen_syntax_node *module_node;
		long rank;

		module_node = pigen_syntax_get(syntax, node->parent);
		rank = (module_node &&
			module_node->kind == PIGEN_SYNTAX_MODULE) ?
			fsm_child_rank(syntax, module_node, node) : -1;
		if (rank < 0 ||
			emit_fsm_records(model, lowering, module, (size_t)rank))
		{
			if (error)
			{
				error->span = node->location.source_span;
				error->message = "structured node has no published RTL identity";
			}
			return -1;
		}
		return emit_children(model, syntax, lowering, node, module, in_module, error);
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
				-1 : emit_children(model, syntax, lowering, node, module, 1, error);
		}
		if (in_module)
			return pigen_output_item_add_child(model, module, item) ==
				PIGEN_INVALID_ID ? -1 : emit_children(model, syntax, lowering, node, module, 1, error);
		return pigen_output_item_add(model, item) == PIGEN_INVALID_ID ? -1 :
			emit_children(model, syntax, lowering, node, module, in_module, error);
	}
	/* A node the walk is required to lower (a module or a signal declarator)
	 * with no published identity is the missing-lowered-identity error: report
	 * it at the node's own span and stop. Any other kind with no identity is a
	 * non-emitted container and keeps recursing. */
	if (node->kind == PIGEN_SYNTAX_MODULE ||
		node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR)
	{
		if (error)
		{
			error->span = node->location.source_span;
			error->message = "structured node has no published RTL identity";
		}
		return -1;
	}
	return emit_children(model, syntax, lowering, node, module, in_module, error);
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
	root = pigen_syntax_get(syntax, (pigen_syntax_id){0});
	if (!root)
		return -1;
	return emit_node(model, syntax, lowering, 0, (pigen_rtl_module_id){PIGEN_INVALID_ID}, 0,
		error);
}

void pigen_free_output_model(pigen_output_model *model)
{
	if (!model)
		return;
	free(model->items);
	free(model->nested_layouts);
	*model = (pigen_output_model){0};
}
