#include "pigen/sv_emit.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One fixed precedence level per expression kind, from highest (0) to lowest.
 * The packet's paren cases are the authority for what is structurally
 * necessary; the exact ordering is pinned here. */
enum {
	PRC_STRUCT = 0, /* concatenation, index, select */
	PRC_LEAF, /* object, integer, bits, unary, conversion */
	PRC_POWER,
	PRC_MUL, /* multiply, divide, modulo */
	PRC_ADD, /* add, subtract */
	PRC_SHIFT, /* left / right / arithmetic shifts */
	PRC_REL, /* < > <= >= */
	PRC_EQ, /* == != */
	PRC_BIT, /* & ^ | */
	PRC_LOG, /* && || */
	PRC_COND, /* ? : */
	PRC_COUNT
};

/* One private operator-rendering table mapping each resolved operator to its
 * SystemVerilog spelling and precedence level. */
struct op_render {
	const char *text;
	int level;
};

static const struct op_render unary_op_render[] = {
	[PIGEN_UNARY_POSITIVE] = { "+", PRC_LEAF },
	[PIGEN_UNARY_NEGATE] = { "-", PRC_LEAF },
	[PIGEN_UNARY_BITWISE_NOT] = { "~", PRC_LEAF },
	[PIGEN_UNARY_LOGICAL_NOT] = { "!", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_AND] = { "&", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_NAND] = { "~&", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_OR] = { "|", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_NOR] = { "~|", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_XOR] = { "^", PRC_LEAF },
	[PIGEN_UNARY_REDUCTION_XNOR] = { "~^", PRC_LEAF },
};

static const struct op_render binary_op_render[] = {
	[PIGEN_BINARY_ADD] = { " + ", PRC_ADD },
	[PIGEN_BINARY_SUBTRACT] = { " - ", PRC_ADD },
	[PIGEN_BINARY_MULTIPLY] = { " * ", PRC_MUL },
	[PIGEN_BINARY_DIVIDE] = { " / ", PRC_MUL },
	[PIGEN_BINARY_MODULO] = { " % ", PRC_MUL },
	[PIGEN_BINARY_POWER] = { " ** ", PRC_POWER },
	[PIGEN_BINARY_SHIFT_LEFT] = { " << ", PRC_SHIFT },
	[PIGEN_BINARY_SHIFT_RIGHT] = { " >> ", PRC_SHIFT },
	[PIGEN_BINARY_ARITH_SHIFT_LEFT] = { " <<< ", PRC_SHIFT },
	[PIGEN_BINARY_ARITH_SHIFT_RIGHT] = { " >>> ", PRC_SHIFT },
	[PIGEN_BINARY_LESS] = { " < ", PRC_REL },
	[PIGEN_BINARY_LESS_EQUAL] = { " <= ", PRC_REL },
	[PIGEN_BINARY_GREATER] = { " > ", PRC_REL },
	[PIGEN_BINARY_GREATER_EQUAL] = { " >= ", PRC_REL },
	[PIGEN_BINARY_EQUAL] = { " == ", PRC_EQ },
	[PIGEN_BINARY_NOT_EQUAL] = { " != ", PRC_EQ },
	[PIGEN_BINARY_CASE_EQUAL] = { " === ", PRC_EQ },
	[PIGEN_BINARY_CASE_NOT_EQUAL] = { " !== ", PRC_EQ },
	[PIGEN_BINARY_WILDCARD_EQUAL] = { " ==? ", PRC_EQ },
	[PIGEN_BINARY_WILDCARD_NOT_EQUAL] = { " !=? ", PRC_EQ },
	[PIGEN_BINARY_BITWISE_AND] = { " & ", PRC_BIT },
	[PIGEN_BINARY_BITWISE_XOR] = { " ^ ", PRC_BIT },
	[PIGEN_BINARY_BITWISE_XNOR] = { " ~^ ", PRC_BIT },
	[PIGEN_BINARY_BITWISE_OR] = { " | ", PRC_BIT },
	[PIGEN_BINARY_LOGICAL_AND] = { " && ", PRC_LOG },
	[PIGEN_BINARY_LOGICAL_OR] = { " || ", PRC_LOG },
};

/* A growing NUL-terminated text buffer; the renderer commits *out only on
 * success, so a failure leaves no partial output. */
struct buf {
	char *data;
	size_t length;
	size_t capacity;
	int failed;
};

static void buf_append(struct buf *b, const char *text, size_t length)
{
	if (b->failed)
		return;
	if (b->length + length + 1 > b->capacity)
	{
		size_t capacity = b->capacity ? b->capacity : 64;
		char *grown;

		while (capacity < b->length + length + 1)
			capacity *= 2;
		grown = realloc(b->data, capacity);
		if (!grown)
		{
			b->failed = 1;
			return;
		}
		b->data = grown;
		b->capacity = capacity;
	}
	memcpy(b->data + b->length, text, length);
	b->length += length;
	b->data[b->length] = 0;
}

static void buf_append_str(struct buf *b, const char *text)
{
	buf_append(b, text, strlen(text));
}

static void buf_append_int(struct buf *b, int64_t value)
{
	char digits[24];
	size_t length = 0;
	uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u
		: (uint64_t)value;

	digits[length++] = (char)('0' + (char)(magnitude % 10u));
	magnitude /= 10u;
	while (magnitude)
	{
		digits[length++] = (char)('0' + (char)(magnitude % 10u));
		magnitude /= 10u;
	}
	if (value < 0)
		buf_append_str(b, "-");
	while (length)
		buf_append(b, digits + --length, 1);
}

static void buf_append_ull(struct buf *b, unsigned long long value);

static int span_equal(const pigen_source_span *a, const pigen_source_span *b)
{
	return a->source.index == b->source.index &&
		a->start == b->start && a->end == b->end;
}

/* A SOURCE-kind name whose origin span equals SPAN: its provenance spelling. */
static const char *name_for_span(const pigen_rtl_model *rtl,
	const pigen_source_span *span)
{
	size_t i;

	for (i = 0; i < rtl->name_count; i++)
	{
		const pigen_rtl_name *name = &rtl->names[i];

		if (name->kind == PIGEN_RTL_NAME_SOURCE &&
			span_equal(&name->origin, span))
			return name->text;
	}
	return NULL;
}

/* The object's final name: the arena name whose identity (role + origin span)
 * matches the object's role and origin. Source roles carry the provenance
 * identifier span, so the role is PIGEN_RTL_NAME_SOURCE. */
static const char *object_name(const pigen_rtl_model *rtl,
	const pigen_rtl_object *object)
{
	return name_for_span(rtl, &object->origin);
}

/* The module's final name: a SOURCE-kind name whose provenance spelling is the
 * module's identifier. The module record's own origin span is the whole
 * statement (or an invalid span), which is distinct from the identifier span,
 * so the identifier is matched by the unique SOURCE name that no other record
 * owns (no object and no other record carries that origin span). */
static const char *module_name(const pigen_rtl_model *rtl,
	const pigen_rtl_module *module)
{
	const pigen_source_span *origin = &module->origin;
	const char *fallback = NULL;
	size_t i;

	for (i = 0; i < rtl->name_count; i++)
	{
		const pigen_rtl_name *name = &rtl->names[i];
		size_t j;

		if (name->kind != PIGEN_RTL_NAME_SOURCE || !name->text)
			continue;
		/* This name is the module's identifier when no other record claims
		 * the same origin span (objects and the module's own record do not
		 * own it). */
		for (j = 0; j < rtl->object_count; j++)
		{
			if (span_equal(&rtl->objects[j].origin, &name->origin))
				break;
		}
		if (j < rtl->object_count)
			continue;
		if (span_equal(&rtl->modules[0].origin, &name->origin) &&
			origin->source.index != PIGEN_INVALID_ID)
			continue;
		return name->text;
	}
	(void)fallback;
	return NULL;
}

/* The instance's final name: its synthetic INSTANCE-kind name (the role stem,
 * e.g. `instance`) when its origin is invalid, else the SOURCE name on its
 * origin span. */
static const char *instance_name(const pigen_rtl_model *rtl,
	const pigen_rtl_instance *instance)
{
	const pigen_source_span *origin = &instance->origin;
	size_t i;

	if (origin->source.index != PIGEN_INVALID_ID)
		return name_for_span(rtl, origin);
	for (i = 0; i < rtl->name_count; i++)
	{
		const pigen_rtl_name *name = &rtl->names[i];

		if (name->kind == PIGEN_RTL_NAME_INSTANCE)
			return name->text;
	}
	return NULL;
}

/* Render one expression into the buffer through the precedence-aware renderer. */
static int render(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr);

static int emit_expr(struct buf *b, const pigen_rtl_model *rtl,
	pigen_rtl_expr_id id)
{
	const pigen_rtl_expr *expr = pigen_rtl_expr_get(rtl, id);

	if (!expr)
		return -1;
	return render(b, rtl, expr);
}

/* Copy an opaque span's text verbatim from the source manager (byte-exact, no
 * trim, no re-parse). */
static int emit_opaque(struct buf *b, const pigen_source_manager *sources,
	const pigen_output_item *item)
{
	const char *text;
	size_t length;

	if (!pigen_source_span_valid(sources, item->as.span))
		return -1;
	text = pigen_source_span_text(sources, item->as.span, &length);
	if (!text)
		return -1;
	buf_append(b, text, length);
	return b->failed ? -1 : 0;
}

/* The `[h:l]` packed range of a type, or the empty string for a width-1
 * (scalar) type. The range is the type width: `[width-1:0]`. */
static int emit_range(struct buf *b, const pigen_rtl_type *type)
{
	uint64_t width = type->width;

	if (width <= 1)
		return 0;
	buf_append_str(b, "[");
	buf_append_ull(b, width - 1);
	buf_append_str(b, ":0]");
	return b->failed ? -1 : 0;
}

/* A signal declaration: `  logic [h:l] name;` (internal) or the port header
 * line `  <direction> logic [h:l] name,` (direction right-aligned to six
 * columns). `logic` is ALWAYS present; `[h:l]` is the first packed dimension
 * range, present only when width > 1; `comma` ends a port. */
static int emit_object(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_object *object, int is_port, int comma)
{
	const pigen_rtl_type *type;
	const char *name;
	uint64_t width;
	const char *direction = NULL;
	size_t pad;

	type = pigen_rtl_type_get(rtl, object->type);
	if (!type)
		return -1;
	name = object_name(rtl, object);
	if (!name)
		return -1;
	width = type->width;

	buf_append_str(b, "  ");
	if (is_port)
	{
		switch (object->direction)
		{
		case PIGEN_SEMANTIC_INPUT:
			direction = "input";
			break;
		case PIGEN_SEMANTIC_OUTPUT:
			direction = "output";
			break;
		case PIGEN_SEMANTIC_INOUT:
			direction = "inout";
			break;
		default:
			return -1;
		}
		buf_append_str(b, direction);
		for (pad = strlen(direction); pad < 6; pad++)
			buf_append_str(b, " ");
		buf_append_str(b, " ");
	}
	buf_append_str(b, "logic");
	if (width > 1)
	{
		buf_append_str(b, " ");
		if (emit_range(b, type) != 0)
			return -1;
	}
	buf_append_str(b, " ");
	buf_append_str(b, name);
	if (is_port)
		buf_append_str(b, comma ? "," : "");
	else
		buf_append_str(b, ";");
	return b->failed ? -1 : 0;
}

/* An instance: `  name #(.P(v),...) (.p(o),...);` each side present only when
 * its range is non-empty. A literal parameter renders its value. */
static int emit_instance(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_instance *instance)
{
	const char *name = instance_name(rtl, instance);
	const pigen_rtl_expr_id *parameters;
	const pigen_rtl_object_id *connections;
	size_t parameter_first = instance->parameters.first;
	size_t parameter_count = instance->parameters.count;
	size_t connection_first = instance->connections.first;
	size_t connection_count = instance->connections.count;
	size_t i;

	if (!name)
		return -1;
	parameters = parameter_count ? &rtl->instance_parameters[parameter_first]
		: NULL;
	connections = connection_count ? &rtl->instance_connections[connection_first]
		: NULL;

	buf_append_str(b, "  ");
	buf_append_str(b, name);
	if (parameter_count)
	{
		buf_append_str(b, " #(");
		for (i = 0; i < parameter_count; i++)
		{
			buf_append_str(b, i ? ", " : "");
			if (emit_expr(b, rtl, parameters[i]) != 0)
				return -1;
		}
		buf_append_str(b, ")");
	}
	if (connection_count)
	{
		buf_append_str(b, " (");
		for (i = 0; i < connection_count; i++)
		{
			const pigen_rtl_object *port;
			const char *port_name;

			port = pigen_rtl_object_get(rtl, connections[i]);
			if (!port)
				return -1;
			port_name = object_name(rtl, port);
			if (!port_name)
				return -1;
			buf_append_str(b, i ? ", " : "");
			buf_append_str(b, ".");
			buf_append_str(b, port_name);
			buf_append_str(b, "(");
			buf_append_str(b, port_name);
			buf_append_str(b, ")");
		}
		buf_append_str(b, ");");
	}
	else
		buf_append_str(b, ";");
	return b->failed ? -1 : 0;
}

/* A continuous assignment: `  assign dest = value;`. */
static int emit_equation(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_equation *equation)
{
	const pigen_rtl_object *dest;
	const char *dest_name;

	dest = pigen_rtl_object_get(rtl, equation->destination);
	if (!dest)
		return -1;
	dest_name = object_name(rtl, dest);
	if (!dest_name)
		return -1;
	buf_append_str(b, "  assign ");
	buf_append_str(b, dest_name);
	buf_append_str(b, " = ");
	if (emit_expr(b, rtl, equation->value) != 0)
		return -1;
	buf_append_str(b, ";");
	return b->failed ? -1 : 0;
}

/* A clocked process: `  always @(<edge> <clock>)` then each update as
 * `    dest <= value;` (four-space indent). */
static int emit_process(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_process *process)
{
	const pigen_rtl_update *updates;
	const char *edge;
	size_t first = process->updates.first;
	size_t count = process->updates.count;
	size_t i;

	updates = count ? &rtl->updates[first] : NULL;
	switch (process->edge)
	{
	case PIGEN_SEMANTIC_POSEDGE:
		edge = "posedge";
		break;
	case PIGEN_SEMANTIC_NEGEDGE:
		edge = "negedge";
		break;
	default:
		return -1;
	}
	buf_append_str(b, "  always @(");
	buf_append_str(b, edge);
	buf_append_str(b, " ");
	if (emit_expr(b, rtl, process->clock) != 0)
		return -1;
	buf_append_str(b, ")\n");
	for (i = 0; i < count; i++)
	{
		const pigen_rtl_object *dest;
		const char *dest_name;

		dest = pigen_rtl_object_get(rtl, updates[i].destination);
		if (!dest)
			return -1;
		dest_name = object_name(rtl, dest);
		if (!dest_name)
			return -1;
		buf_append_str(b, "    ");
		buf_append_str(b, dest_name);
		buf_append_str(b, " <= ");
		if (emit_expr(b, rtl, updates[i].value) != 0)
			return -1;
		buf_append_str(b, ";\n");
	}
	return b->failed ? -1 : 0;
}

/* Render one module: `module <name> (` then its nested items in nested order
 * (ports, internals, instances, equations, processes), then `);` (immediately
 * after the name when it has no ports) and `endmodule`. Each line is
 * newline-terminated; the module's last line ends the module. */
static int emit_module(struct buf *b, const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl,
	const pigen_output_item *item)
{
	const pigen_rtl_module *module;
	const pigen_rtl_record_range range = item->layout;
	const char *name;
	size_t i;

	(void)sources;
	module = pigen_rtl_module_get(rtl, item->as.module);
	if (!module)
		return -1;
	name = module_name(rtl, module);
	if (!name)
		return -1;

	buf_append_str(b, "module ");
	buf_append_str(b, name);
	buf_append_str(b, " (\n");
	for (i = 0; i < range.count; i++)
	{
		const pigen_output_item *child = &layout->items[range.first + i];
		const pigen_rtl_object *object;

		if (child->kind != PIGEN_OUTPUT_RTL_OBJECT)
			continue;
		object = pigen_rtl_object_get(rtl, child->as.object);
		if (!object)
			return -1;
		if (object->direction != PIGEN_SEMANTIC_INPUT &&
			object->direction != PIGEN_SEMANTIC_OUTPUT &&
			object->direction != PIGEN_SEMANTIC_INOUT)
			continue;
		/* A port takes a comma on every port but the last one. */
		{
			int last_port = 1;
			size_t j;

			for (j = i + 1; j < range.count; j++)
			{
				const pigen_output_item *later =
					&layout->items[range.first + j];
				const pigen_rtl_object *later_object;

				if (later->kind != PIGEN_OUTPUT_RTL_OBJECT)
					continue;
				later_object = pigen_rtl_object_get(rtl, later->as.object);
				if (!later_object)
					return -1;
				if (later_object->direction == PIGEN_SEMANTIC_INPUT ||
					later_object->direction == PIGEN_SEMANTIC_OUTPUT ||
					later_object->direction == PIGEN_SEMANTIC_INOUT)
				{
					last_port = 0;
					break;
				}
			}
			if (emit_object(b, rtl, object, 1, !last_port) != 0)
				return -1;
		}
		buf_append_str(b, "\n");
	}
	buf_append_str(b, ");\n");
	for (i = 0; i < range.count; i++)
	{
		const pigen_output_item *child = &layout->items[range.first + i];
		const pigen_rtl_object *object;
		const pigen_rtl_instance *instance;
		const pigen_rtl_equation *equation;
		const pigen_rtl_process *process;
		int is_port;

		switch (child->kind)
		{
		case PIGEN_OUTPUT_RTL_OBJECT:
			object = pigen_rtl_object_get(rtl, child->as.object);
			if (!object)
				return -1;
			is_port = object->direction == PIGEN_SEMANTIC_INPUT ||
				object->direction == PIGEN_SEMANTIC_OUTPUT ||
				object->direction == PIGEN_SEMANTIC_INOUT;
			if (is_port)
				continue;
			if (emit_object(b, rtl, object, 0, 0) != 0)
				return -1;
			buf_append_str(b, "\n");
			break;
		case PIGEN_OUTPUT_RTL_INSTANCE:
			instance = pigen_rtl_instance_get(rtl, child->as.instance);
			if (!instance)
				return -1;
			if (emit_instance(b, rtl, instance) != 0)
				return -1;
			buf_append_str(b, "\n");
			break;
		case PIGEN_OUTPUT_RTL_EQUATION:
			equation = pigen_rtl_equation_get(rtl, child->as.equation);
			if (!equation)
				return -1;
			if (emit_equation(b, rtl, equation) != 0)
				return -1;
			buf_append_str(b, "\n");
			break;
		case PIGEN_OUTPUT_RTL_PROCESS:
			process = pigen_rtl_process_get(rtl, child->as.process);
			if (!process)
				return -1;
			if (emit_process(b, rtl, process) != 0)
				return -1;
			break;
		default:
			return -1;
		}
	}
	buf_append_str(b, "endmodule\n");
	return b->failed ? -1 : 0;
}

int pigen_emit_systemverilog(const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl,
	char **output_string)
{
	struct buf b = {0};
	size_t i;

	if (!sources || !layout || !rtl)
	{
		if (output_string)
			*output_string = NULL;
		return -1;
	}
	if (output_string)
		*output_string = NULL;
	for (i = 0; i < layout->item_count; i++)
	{
		const pigen_output_item *item = &layout->items[i];

		switch (item->kind)
		{
		case PIGEN_OUTPUT_OPAQUE:
			if (emit_opaque(&b, sources, item) != 0)
				goto fail;
			break;
		case PIGEN_OUTPUT_MODULE:
			if (emit_module(&b, sources, layout, rtl, item) != 0)
				goto fail;
			/* The model is a flat array: a module's nested items occupy the
			 * owner-managed range item->layout and are rendered inside the
			 * module emit, so skip past them in this top-level walk. */
			i = item->layout.first + item->layout.count - 1;
			break;
		default:
			/* A structured item outside a module is malformed; the
			 * whole-design walk renders modules and opaque spans only. */
			goto fail;
		}
	}
	if (output_string)
		*output_string = b.data;
	return 0;
fail:
	free(b.data);
	if (output_string)
		*output_string = NULL;
	return -1;
}

static int render_expression_level(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr);
static int render(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr);

static void buf_append_ull(struct buf *b, unsigned long long value)
{
	char digits[24];
	size_t length = 0;

	digits[length++] = (char)('0' + (char)(value % 10ull));
	value /= 10ull;
	while (value)
	{
		digits[length++] = (char)('0' + (char)(value % 10ull));
		value /= 10ull;
	}
	while (length)
		buf_append(b, digits + --length, 1);
}

static int render_children(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, size_t count, int level,
	const char *join);

static void render_child(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, size_t i,
	int parent_level, pigen_rtl_expr_kind parent_kind,
	const char *join, int is_right)
{
	const pigen_rtl_expr *child;
	int child_level;
	int paren;

	child = pigen_rtl_expr_get(rtl,
		rtl->expression_children[expr->first_child + i]);
	if (!child)
	{
		b->failed = 1;
		return;
	}
	child_level = render_expression_level(rtl, child);
	/* Struct parents (concatenation, index, select) bind their operands
	 * without paren rules. A binary/conditional child needs parens only when
	 * its precedence is strictly lower than the parent's, or when it is a
	 * same-precedence binary right-child (associativity), or a conditional
	 * operand under a conditional. */
	paren = 0;
	if (parent_kind == PIGEN_RTL_EXPR_CONCATENATION ||
		parent_kind == PIGEN_RTL_EXPR_INDEX ||
		parent_kind == PIGEN_RTL_EXPR_SELECT)
		paren = 0;
	else if (child_level > parent_level)
		paren = 1;
	else if (child_level == parent_level)
	{
		if (child->kind == PIGEN_RTL_EXPR_BINARY &&
			parent_kind == PIGEN_RTL_EXPR_BINARY && is_right)
			paren = 1;
		if (child->kind == PIGEN_RTL_EXPR_CONDITIONAL &&
			parent_kind == PIGEN_RTL_EXPR_CONDITIONAL)
			paren = 1;
	}
	if (join)
		buf_append_str(b, join);
	if (paren)
		buf_append_str(b, "(");
	render(b, rtl, child);
	if (b->failed)
		return;
	if (paren)
		buf_append_str(b, ")");
}

static int render_children(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, size_t count, int parent_level,
	const char *join)
{
	size_t i;

	for (i = 0; i < count; i++)
		render_child(b, rtl, expr, i, parent_level, expr->kind,
			i ? join : NULL, i % 2 == 1);
	return !b->failed;
}

static int render(struct buf *b, const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr)
{
	switch (expr->kind)
	{
	case PIGEN_RTL_EXPR_INTEGER:
	case PIGEN_RTL_EXPR_BITS:
	{
		const pigen_rtl_literal_word *words = expr->literal_words;
		int64_t value;

		if (!words)
		{
			b->failed = 1;
			return -1;
		}
		value = (int64_t)words[0].value;
		if (expr->literal_negative)
			value = -value;
		buf_append_int(b, value);
		return 0;
	}
	case PIGEN_RTL_EXPR_OBJECT:
	{
		const pigen_rtl_object *object =
			pigen_rtl_object_get(rtl, expr->object);
		const char *name;

		if (!object)
		{
			b->failed = 1;
			return -1;
		}
		name = object_name(rtl, object);
		if (!name)
		{
			b->failed = 1;
			return -1;
		}
		buf_append_str(b, name);
		return 0;
	}
	case PIGEN_RTL_EXPR_UNARY:
	{
		const struct op_render *op =
			&unary_op_render[expr->as.unary.resolution.operation.operator];
		const pigen_rtl_expr *operand;
		int operand_level;

		operand = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		if (!operand)
		{
			b->failed = 1;
			return -1;
		}
		buf_append_str(b, op->text);
		operand_level = render_expression_level(rtl, operand);
		if (operand_level < PRC_LEAF)
		{
			buf_append_str(b, "(");
			render(b, rtl, operand);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, operand);
			if (b->failed)
				return -1;
		}
		return 0;
	}
	case PIGEN_RTL_EXPR_BINARY:
	{
		const struct op_render *op =
			&binary_op_render[expr->as.binary.resolution.operation.operator];
		const pigen_rtl_expr *left;
		const pigen_rtl_expr *right;
		int left_level;
		int right_level;
		int level;

		left = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		right = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 1]);
		if (!left || !right)
		{
			b->failed = 1;
			return -1;
		}
		left_level = render_expression_level(rtl, left);
		right_level = render_expression_level(rtl, right);
		level = op->level;
		if (left_level > level ||
			(left_level == level && left->kind == PIGEN_RTL_EXPR_BINARY))
		{
			buf_append_str(b, "(");
			render(b, rtl, left);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, left);
			if (b->failed)
				return -1;
		}
		buf_append_str(b, op->text);
		if (right_level > level ||
			(right_level == level && right->kind == PIGEN_RTL_EXPR_BINARY))
		{
			buf_append_str(b, "(");
			render(b, rtl, right);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, right);
			if (b->failed)
				return -1;
		}
		return 0;
	}
	case PIGEN_RTL_EXPR_CONDITIONAL:
	{
		const pigen_rtl_expr *condition;
		const pigen_rtl_expr *when_true;
		const pigen_rtl_expr *when_false;
		int condition_level;
		int true_level;
		int false_level;

		condition = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		when_true = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 1]);
		when_false = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 2]);
		if (!condition || !when_true || !when_false)
		{
			b->failed = 1;
			return -1;
		}
		condition_level = render_expression_level(rtl, condition);
		true_level = render_expression_level(rtl, when_true);
		false_level = render_expression_level(rtl, when_false);
		if (condition_level > PRC_LOG ||
			(condition_level == PRC_LOG &&
			 condition->kind == PIGEN_RTL_EXPR_BINARY) ||
			condition->kind == PIGEN_RTL_EXPR_CONDITIONAL)
		{
			buf_append_str(b, "(");
			render(b, rtl, condition);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, condition);
			if (b->failed)
				return -1;
		}
		buf_append_str(b, " ? ");
		if (true_level > PRC_COND || true_level == PRC_COND)
		{
			buf_append_str(b, "(");
			render(b, rtl, when_true);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, when_true);
			if (b->failed)
				return -1;
		}
		buf_append_str(b, " : ");
		if (false_level > PRC_COND || false_level == PRC_COND)
		{
			buf_append_str(b, "(");
			render(b, rtl, when_false);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
		}
		else
		{
			render(b, rtl, when_false);
			if (b->failed)
				return -1;
		}
		return 0;
	}
	case PIGEN_RTL_EXPR_CONVERSION:
	{
		const pigen_rtl_type *type =
			pigen_rtl_type_get(rtl, expr->type);
		const pigen_rtl_expr *operand;
		int is_signed;
		pigen_conversion_kind conversion_kind;

		if (!type)
		{
			b->failed = 1;
			return -1;
		}
		operand = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		if (!operand)
		{
			b->failed = 1;
			return -1;
		}
		is_signed = type->signedness == PIGEN_SIGN_SIGNED;
		conversion_kind = expr->as.conversion.conversion.kind;
		if (conversion_kind == PIGEN_CONVERSION_INTEGER_RESIZE)
		{
			/* A signedness resize carries no width: the target cast
			 * applies directly to the operand. */
			if (is_signed)
				buf_append_str(b, "signed'(");
			else
				buf_append_str(b, "unsigned'(");
			render(b, rtl, operand);
			if (b->failed)
				return -1;
			buf_append_str(b, ")");
			return 0;
		}
		/* Every other kind renders the target's full cast on the
		 * operand. */
		if (is_signed)
			buf_append_str(b, "signed'(");
		else
			buf_append_str(b, "unsigned'(");
		buf_append_ull(b, (unsigned long long)type->width);
		buf_append_str(b, "'(");
		render(b, rtl, operand);
		if (b->failed)
			return -1;
		buf_append_str(b, "))");
		return 0;
	}
	case PIGEN_RTL_EXPR_INDEX:
	{
		const pigen_rtl_expr *base;
		const pigen_rtl_expr *index;

		base = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		index = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 1]);
		if (!base || !index)
		{
			b->failed = 1;
			return -1;
		}
		render(b, rtl, base);
		if (b->failed)
			return -1;
		buf_append_str(b, "[");
		render(b, rtl, index);
		if (b->failed)
			return -1;
		buf_append_str(b, "]");
		return 0;
	}
	case PIGEN_RTL_EXPR_SELECT:
	{
		const pigen_rtl_expr *base;
		const pigen_rtl_expr *left;
		const pigen_rtl_expr *right;
		pigen_select_kind kind;

		base = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child]);
		left = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 1]);
		right = pigen_rtl_expr_get(rtl,
			rtl->expression_children[expr->first_child + 2]);
		if (!base || !left || !right)
		{
			b->failed = 1;
			return -1;
		}
		kind = expr->as.select.kind;
		render(b, rtl, base);
		if (b->failed)
			return -1;
		buf_append_str(b, "[");
		render(b, rtl, left);
		if (b->failed)
			return -1;
		if (kind == PIGEN_SEMANTIC_SELECT_RANGE ||
			kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP)
		{
			/* A range and an indexed-up select are both a [left:right]
			 * span; only an indexed-down select is a single bound. */
			buf_append_str(b, ":");
			render(b, rtl, right);
			if (b->failed)
				return -1;
		}
		buf_append_str(b, "]");
		return 0;
	}
	case PIGEN_RTL_EXPR_CONCATENATION:
	{
		buf_append_str(b, "{");
		if (!render_children(b, rtl, expr, expr->child_count,
			PRC_STRUCT, ","))
			return -1;
		buf_append_str(b, "}");
		return 0;
	}
	default:
		b->failed = 1;
		return -1;
	}
}

static int render_expression_level(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr)
{
	(void)rtl;
	switch (expr->kind)
	{
	case PIGEN_RTL_EXPR_CONCATENATION:
	case PIGEN_RTL_EXPR_INDEX:
	case PIGEN_RTL_EXPR_SELECT:
		return PRC_STRUCT;
	case PIGEN_RTL_EXPR_OBJECT:
	case PIGEN_RTL_EXPR_INTEGER:
	case PIGEN_RTL_EXPR_BITS:
	case PIGEN_RTL_EXPR_UNARY:
	case PIGEN_RTL_EXPR_CONVERSION:
		return PRC_LEAF;
	case PIGEN_RTL_EXPR_BINARY:
		return binary_op_render[
			expr->as.binary.resolution.operation.operator].level;
	case PIGEN_RTL_EXPR_CONDITIONAL:
		return PRC_COND;
	default:
		return PRC_COUNT;
	}
}

static int render_expression(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, char **buf)
{
	struct buf b = {0};
	int result;

	if (!rtl || !expr || !buf)
		return -1;
	*buf = NULL;
	result = render(&b, rtl, expr);
	if (result != 0 || b.failed)
	{
		free(b.data);
		*buf = NULL;
		return -1;
	}
	*buf = b.data;
	return 0;
}

int pigen_sv_render_expression(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, char **text)
{
	if (text)
		*text = NULL;
	if (!rtl || !expr || !text)
		return -1;
	return render_expression(rtl, expr, text);
}
