#include <stddef.h>
#include <string.h>

#include "pigen/instance_lower.h"

#include "pigen/data_type.h"
#include "pigen/source.h"

/* The instance lowering owner (step (i3) of the child-instance frontier). This
 * file lowers one PIGEN_SYNTAX_INSTANCE statement (an ordinary SV child
 * instantiation inside a module body) through the common RTL model, publishing
 * into the parent module's RTL model as one child-instance record. The sibling
 * precedent is the FSM parent-module lowering (src/fsm_lower.c, step (s5)):
 * that owner lowers one PIGEN_SYNTAX_FSM statement through the common RTL
 * model into the parent module's records, and this owner lowers one
 * PIGEN_SYNTAX_INSTANCE the same way, composing and publishing its records
 * through the public RTL model constructors (src/rtl.c, which IS linked) and
 * never calling pigen_lower_rtl_expression (src/rtl_lower.c is not linked into
 * the standalone target, mirroring the FSM seam).
 *
 * The published record is a pigen_rtl_instance owned by the parent module's
 * pigen_rtl_module record: origin is the instance node's original span,
 * semantic_module is the resolved child module's provenance (the module whose
 * name token text equals the instance type-name token text, the (i2)
 * resolution read from the node), parameters is the ordered source-order
 * parameter range and connections is the ordered source-order connection range
 * (what/is/elastic/rtl/task/eight.md: a module record is composed from
 * already-published state). The record carries the instance-name text as the
 * only name it carries - surface-independent: it keys off the resolved child
 * module identity and the port identities, never off the instance spelling.
 *
 * This owner consumes the (i1) node and the (i2) identity facts only as
 * validated inputs read from the node - the (i2) identity owner is not linked
 * into this standalone target, so the facts are read here the way the identity
 * owner establishes them. A failed call returns -1 and the compile stops; it
 * may leave partial state behind, and nothing uses the model after a failed
 * compile, so no caller or test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SPAN \
	((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

#define INST_MAX 64

/* The syntax node at the raw node-array index CHILD within the tree that owns
 * NODE. The node is an element of that tree's contiguous node array, at the
 * position given by its own syntax id (the caller passes the instance's id
 * through syntax), so the array base is the node minus its id and the child
 * node is the base plus the child's raw index. This recovers the child nodes
 * (the parameter and connection children, whose first_parameter and
 * first_connection fields are raw node-array indices, not syntax ids) without
 * the tree handle, which the owner seam does not carry - the same flat-array
 * recovery the step-(s5) FSM lowering uses for its syntax-id children. */
static const pigen_syntax_node *inst_child(const pigen_syntax_node *node,
	pigen_syntax_id syntax, size_t index)
{
	return (node - syntax.index) + index;
}

/* Whether C is an identifier character (the SystemVerilog identifier alphabet
 * minus the leading ` prefix, which a token text never carries). */
static int ident_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '$';
}

/* The first identifier in TEXT of LENGTH (a leading run of identifier
 * characters, which must be non-empty). Fills *out and *out_length. Returns 1
 * on a non-empty leading identifier and 0 otherwise (TEXT is empty or does not
 * begin with an identifier character). The type name is the first token of an
 * instance and a NAME atom is a single identifier, so the leading identifier of
 * the relevant span text is exactly the token text. */
static int first_identifier(const char *text, size_t length, const char **out,
	size_t *out_length)
{
	size_t i = 0;

	if (!text || length == 0 || !ident_char((unsigned char)text[0]))
		return 0;
	while (i < length && ident_char((unsigned char)text[i]))
		i++;
	*out = text;
	*out_length = i;
	return 1;
}

/* True when TEXT of TEXT_LENGTH equals the span's source text exactly. */
static int span_equals_text(const pigen_source_manager *sources,
	pigen_source_span span, const char *text, size_t text_length)
{
	size_t known_length;
	const char *known;

	if (text_length == 0 || span.source.index == PIGEN_INVALID_ID) return 0;
	known = pigen_source_span_text(sources, span, &known_length);
	return known && known_length == text_length &&
		!memcmp(known, text, text_length);
}

/* The instance's type-name token text (the first token of the instance, which
 * the node's original span begins at), read from the source text. Fills *text
 * and *text_length. Returns 1 when a non-empty leading identifier is present. */
static int instance_type_name(const pigen_semantic_model *sem,
	const pigen_syntax_node *instance, const char **text,
	size_t *text_length)
{
	size_t span_length;
	const char *span_text;
	const char *ident;
	size_t ident_length;

	span_text = pigen_source_span_text(sem->sources,
		instance->location.source_span, &span_length);
	if (!first_identifier(span_text, span_length, &ident, &ident_length))
		return 0;
	*text = ident;
	*text_length = ident_length;
	return 1;
}

/* The child module the instance instantiates: the model's module whose
 * PIGEN_SYNTAX_MODULE name token text equals the instance's type-name token
 * text. Matched by token text (never by spelling, layout or the node's
 * children) the way the step-(i2) identity owner resolves the type name, so a
 * later grammar change cannot alter the established identity. Returns the
 * module id, or the invalid id when no module's name token text matches. */
static pigen_module_id resolve_type_module(pigen_semantic_model *model,
	const char *type_text, size_t type_length)
{
	size_t i;

	for (i = 0; i < model->module_count; i++)
	{
		const pigen_semantic_module *module = &model->modules[i];
		const pigen_symbol *symbol =
			pigen_symbol_get(model, module->symbol);
		pigen_source_span name;

		if (!symbol || symbol->kind != PIGEN_SYMBOL_MODULE) continue;
		name = symbol->name;
		if (span_equals_text(model->sources, name, type_text, type_length))
			return (pigen_module_id){(uint32_t)i};
	}
	return INVALID_ID(pigen_module_id);
}

/* The parent's signal whose symbol name token text equals TEXT of TEXT_LENGTH
 * (a connection NAME atom's token text). Returns the signal index, or the
 * invalid index when no parent-module signal's name token text matches. */
static pigen_signal_id resolve_parent_signal(pigen_semantic_model *model,
	pigen_module_id parent, const char *text, size_t text_length)
{
	size_t i;

	for (i = 0; i < model->signal_count; i++)
	{
		const pigen_semantic_signal *signal = &model->signals[i];
		const pigen_symbol *symbol;

		if (signal->module.index != parent.index) continue;
		symbol = pigen_symbol_get(model, signal->symbol);
		if (!symbol) continue;
		if (span_equals_text(model->sources, symbol->name, text, text_length))
			return (pigen_signal_id){(uint32_t)i};
	}
	return INVALID_ID(pigen_signal_id);
}

/* One connection's bound-signal RTL type: the interned RTL record for the
 * signal's semantic data type, read through the shared data-type services
 * (signedness, state domain and the packed width evaluated to a concrete bit
 * count) and interned so shared shapes share a record. The descriptor is
 * surface-independent: it carries the data type's resolved properties, never a
 * spelling. Returns the invalid type id when the width cannot be evaluated. */
static pigen_rtl_type_id signal_rtl_type(pigen_semantic_model *sem,
	pigen_rtl_model *rtl, pigen_data_type_id data_type, pigen_source_span span)
{
	pigen_const_expr_id width_expr;
	uint64_t width = 0;
	pigen_rtl_type descriptor;

	if (data_type.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_rtl_type_id);
	width_expr = pigen_data_type_packed_width(sem, data_type);
	if (width_expr.index == PIGEN_INVALID_ID ||
		!pigen_const_expr_evaluate_u64(sem, width_expr, &width) ||
		width == 0)
		return INVALID_ID(pigen_rtl_type_id);
	descriptor = (pigen_rtl_type){
		pigen_data_type_signedness(sem, data_type),
		pigen_data_type_state_domain(sem, data_type), width,
		INVALID_ID(pigen_rtl_expr_id), span, NULL, 0};
	return pigen_rtl_type_intern(rtl, &descriptor);
}

/* The parent's RTL module record: the record whose semantic_module is PARENT,
 * or a freshly published one when none exists (on a fresh model this is the
 * only module record). */
static pigen_rtl_module_id parent_module_record(pigen_rtl_lowering *lowering,
	pigen_module_id parent, pigen_source_span span)
{
	size_t i;

	for (i = 0; i < lowering->rtl->module_count; i++)
	{
		const pigen_rtl_module *record = pigen_rtl_module_get(lowering->rtl,
			(pigen_rtl_module_id){(uint32_t)i});

		if (record && record->semantic_module.index == parent.index)
			return (pigen_rtl_module_id){(uint32_t)i};
	}
	return pigen_rtl_module_add_with_owner(lowering->rtl, parent, span);
}

/* One connection NAME atom's token text (a single identifier). A positional
 * connection's whole original span is the NAME atom; a named connection's NAME
 * atom is the argument, the last identifier in the span text (the port
 * identifier precedes the `(`). Fills *text and *text_length and returns 1 on
 * success. */
static int connection_atom_text(pigen_semantic_model *sem,
	const pigen_syntax_node *conn, const char **text, size_t *text_length)
{
	size_t span_length;
	const char *span_text;
	size_t i;

	span_text = pigen_source_span_text(sem->sources,
		conn->location.source_span, &span_length);
	if (!span_text || span_length == 0) return 0;
	if (conn->as.instance_connection.port.index == PIGEN_INVALID_ID)
	{
		/* Positional: the whole span is the single-identifier NAME atom. */
		if (!first_identifier(span_text, span_length, text, text_length))
			return 0;
		return 1;
	}
	/* Named: the NAME atom is the argument - the last identifier in the span
	 * text (the port identifier sits before the `(`, the argument after it). */
	for (i = span_length; i > 0; )
	{
		i--;
		if (ident_char((unsigned char)span_text[i]))
		{
			size_t start = i;

			while (start > 0 &&
				ident_char((unsigned char)span_text[start - 1]))
				start--;
			/* A single-character argument (start == i) is a valid NAME
			 * atom too. */
			*text = span_text + start;
			*text_length = i - start + 1;
			return 1;
		}
	}
	return 0;
}

/* One parameter LITERAL's value: the parameter node's original span text,
 * which must be plain decimal digits (the owner binds only plain-decimal
 * literals; a non-decimal literal fails at the parameter value's span).
 * Parses the value through pigen_integer_intern_decimal +
 * pigen_integer_to_u64 on the semantic model (both in SEMANTIC_SOURCES). Fills
 * *value and returns 1 on success. */
static int parameter_value(pigen_semantic_model *sem,
	const pigen_syntax_node *parameter, uint64_t *value)
{
	size_t span_length;
	const char *digits;
	pigen_integer_id integer;
	size_t i;

	digits = pigen_source_span_text(sem->sources,
		parameter->location.source_span, &span_length);
	if (!digits || span_length == 0) return 0;
	for (i = 0; i < span_length; i++)
		if (digits[i] < '0' || digits[i] > '9')
			return 0;
	integer = pigen_integer_intern_decimal(sem, digits, span_length);
	if (integer.index == PIGEN_INVALID_ID) return 0;
	return pigen_integer_to_u64(sem, integer, value) != 0;
}

static int lower_instance(pigen_rtl_lowering *lowering,
	pigen_module_id parent, pigen_syntax_id syntax,
	const pigen_syntax_node *instance, pigen_rtl_instance_id *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_module_id rtl_module;
	pigen_module_id type_module;
	pigen_rtl_type_id param_type;
	const char *type_text;
	size_t type_length;
	pigen_rtl_expr_id parameters[INST_MAX];
	pigen_rtl_object_id connections[INST_MAX];
	size_t parameter_count = 0;
	size_t connection_count = 0;
	size_t k;

	/* The child module the instance instantiates: the model's module whose name
	 * token text equals the instance type-name token text (the (i2) resolution
	 * read from the node). An unknown type name (no module matches) fails
	 * loudly at the instance's original span. */
	if (!instance_type_name(sem, instance, &type_text, &type_length))
		return -1;
	type_module = resolve_type_module(sem, type_text, type_length);
	if (type_module.index == PIGEN_INVALID_ID)
		return -1;

	/* (a) The parent module's RTL record: find the existing record owned by
	 * PARENT, or publish one. */
	rtl_module = parent_module_record(lowering, parent,
		instance->location.source_span);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;

	/* (c) The parameter expressions, in source order: one integer expression
	 * per source-order parameter child, under one interned unsigned RTL type
	 * (the FSM bit1-style descriptor). A parameter LITERAL that is not plain
	 * decimal fails at the parameter value's original span. */
	{
		pigen_rtl_type param_descriptor = (pigen_rtl_type){
			PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR, 64,
			INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};

		param_type = pigen_rtl_type_intern(rtl, &param_descriptor);
		if (param_type.index == PIGEN_INVALID_ID)
			return -1;
	}
	for (k = 0; k < instance->as.instance.parameter_count &&
		parameter_count < INST_MAX; k++)
	{
		const pigen_syntax_node *parameter = inst_child(instance, syntax,
			instance->as.instance.first_parameter + k);
		uint64_t value;

		if (!parameter || parameter->kind != PIGEN_SYNTAX_PARAMETER ||
			parameter->as.parameter.is_local)
			return -1;
		if (!parameter_value(sem, parameter, &value))
			return -1;
		parameters[parameter_count] = pigen_rtl_expr_add_integer(rtl,
			param_type, value, parameter->location.source_span);
		if (parameters[parameter_count].index == PIGEN_INVALID_ID)
			return -1;
		parameter_count++;
	}

	/* (b) The connection objects, in source order: one VARIABLE object per
	 * source-order connection child, bound to the parent's matching signal by
	 * the NAME atom's token text (the surface-independent token-text match the
	 * (i2) owner uses). A connection expression that is not an atom, or a NAME
	 * atom that matches no parent-module signal, fails at the connection
	 * expression's original span. */
	for (k = 0; k < instance->as.instance.connection_count &&
		connection_count < INST_MAX; k++)
	{
		const pigen_syntax_node *conn = inst_child(instance, syntax,
			instance->as.instance.first_connection + k);
		const char *atom_text;
		size_t atom_length;
		pigen_signal_id signal;
		const pigen_semantic_signal *bound;
		pigen_rtl_type_id object_type;

		if (!conn || conn->kind != PIGEN_SYNTAX_INSTANCE_CONNECTION)
			return -1;
		if (!connection_atom_text(sem, conn, &atom_text, &atom_length))
			return -1;
		signal = resolve_parent_signal(sem, parent, atom_text, atom_length);
		if (signal.index == PIGEN_INVALID_ID)
			return -1;
		bound = &sem->signals[signal.index];
		object_type = signal_rtl_type(sem, rtl, bound->data_type,
			conn->location.source_span);
		if (object_type.index == PIGEN_INVALID_ID)
			return -1;
		connections[connection_count] = pigen_rtl_object_add_with_owner(rtl,
			rtl_module, PIGEN_RTL_OBJECT_VARIABLE, object_type,
			bound->direction, pigen_symbol_signal(sem, bound->symbol),
			conn->location.source_span);
		if (connections[connection_count].index == PIGEN_INVALID_ID)
			return -1;
		connection_count++;
	}

	/* (d) The instance record: owned by the parent module record, carrying the
	 * resolved child module's provenance, the instance's original span and the
	 * ordered source-order parameter and connection ranges. */
	*out = pigen_rtl_instance_add_with_owner(rtl, rtl_module, type_module,
		parameters, parameter_count, connections, connection_count,
		instance->location.source_span);
	return out->index == PIGEN_INVALID_ID ? -1 : 0;
}

int pigen_instance_lower(pigen_rtl_lowering *lowering,
	pigen_module_id parent, pigen_syntax_id syntax_id,
	const pigen_syntax_node *instance, pigen_rtl_instance_id *out)
{
	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	if (parent.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, parent))
		return -1;
	if (syntax_id.index == PIGEN_INVALID_ID)
		return -1;
	if (!instance || instance->kind != PIGEN_SYNTAX_INSTANCE || !out)
		return -1;
	return lower_instance(lowering, parent, syntax_id, instance, out);
}
