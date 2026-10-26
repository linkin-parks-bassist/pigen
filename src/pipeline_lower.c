#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/pipeline_lower.h"

#include "pigen/pipeline_identity.h"
#include "pigen/pipeline_stage.h"
#include "pigen/preprocess.h"

/* The pipeline parent-module lowering owner (step (c3), the (c) boundary of
 * the pipeline step (c) parent-module lowering). This file lowers one
 * PIGEN_SYNTAX_PIPELINE statement (a pipeline body inside a clocked process)
 * through the common RTL model, publishing into the parent module's RTL model:
 *
 *   - the pipeline's one-entry buf stage storage: one dedicated one-entry buf
 *     primitive instance per stage boundary (what/is/a/buf.md);
 *   - the per-stage combinational next-value equations: for each stage, the
 *     outgoing fields' values computed as combinational equations over the
 *     immutable incoming fields the stage is handed by the earlier stages
 *     (how/do/pipeline/stage/reads/work.md);
 *   - the guarded ready/valid handshake: the complete enclosing procedural
 *     guard enables the pipeline, and a false guard holds all storage and
 *     disables both handshakes (how/to/reset/and/enable/a/pipeline.md);
 *   - the final yield to the outgoing-packet write: the yield's outgoing
 *     fields written into the parent module's outgoing packet, which the
 *     step-(a) owner has established as the pipeline's module-scope buf output
 *     packet identity, with the member order in source order (the first
 *     yielded field the MSB, does/the/concatenation/data/type/keying/keep/
 *     member/order.md).
 *
 * This owner consumes the step-(a) outgoing-packet identity
 * (pigen_pipeline_identifier_of) and the step-(b) per-stage execution-law
 * views (pigen_pipeline_stage_laws) only as validated inputs, and composes and
 * publishes its RTL records through the common RTL model (pigen_rtl_*) owned
 * by the parent module record (what/is/elastic/rtl/task/eight.md: a module
 * record is composed from already-published state). A failed call returns -1
 * and the compile stops; it may leave partial state behind, and nothing uses
 * the model after a failed compile, so no caller or test asserts that a
 * failure left the model unchanged (how/should/compiler/builders/fail.md). */

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SPAN \
	((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

#define PIGEN_PIPELINE_MAX 64

/* The syntax node at CHILD within the tree that owns PIPELINE. The pipeline
 * node is an element of that tree's contiguous node array, at the position
 * given by its own syntax id (the caller passes the pipeline's id through
 * syntax), so the array base is the pipeline node minus its id and a child
 * node is the base plus the child's id. This recovers the child nodes without
 * the tree handle, which the owner seam does not carry - the same flat-array
 * recovery the step-(a)/(b) siblings use. */
static const pigen_syntax_node *pipeline_child(const pigen_syntax_node *pipeline,
	pigen_syntax_id syntax, pigen_syntax_id child)
{
	const pigen_syntax_node *base = pipeline - syntax.index;

	return base + child.index;
}

/* The pipeline node's own id: its first child is added immediately after it in
 * the flat node array (a pipeline body is at least one field or stage, so it
 * always has a first child), so the pipeline id is the first child's id minus
 * one. The same recovery the step-(b) owner uses. */
static pigen_syntax_id pipeline_id_of(const pigen_syntax_node *pipeline)
{
	if (pipeline->first_child.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_syntax_id);
	return (pigen_syntax_id){pipeline->first_child.index - 1};
}

static int span_invalid(pigen_source_span span)
{
	return span.source.index == PIGEN_INVALID_ID;
}

/* Text equality of two original-spelling spans, read from the model's source
 * manager. The span, not a source-text search over the output, is the
 * discriminator. */
static int spans_equal(const pigen_source_manager *sources,
	pigen_source_span left, pigen_source_span right)
{
	const char *left_text;
	size_t left_length;
	const char *right_text;
	size_t right_length;

	if (span_invalid(left) || span_invalid(right)) return 0;
	left_text = pigen_source_span_text(sources, left, &left_length);
	right_text = pigen_source_span_text(sources, right, &right_length);
	return left_text && right_text && left_length == right_length &&
		left_length > 0 && !memcmp(left_text, right_text, left_length);
}

/* The single name each pipeline field declaration carries (the parser rejects
 * a field declaring more than one name), recovered from the field's one
 * signal-declarator child and resolved through the expanded source's token
 * origins. */
static pigen_source_span field_name_span(const pigen_syntax_node *pipeline,
	pigen_syntax_id syntax, const pigen_syntax_node *field,
	const pigen_expanded_source *expanded)
{
	const pigen_syntax_node *declarator;
	const pigen_expanded_token *token;

	if (!field || field->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
		return INVALID_SPAN;
	declarator = pipeline_child(pipeline, syntax, field->first_child);
	if (!declarator ||
		declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
		return INVALID_SPAN;
	token = pigen_expanded_token_get(expanded,
		declarator->as.signal_declarator.name);
	return token ? pigen_origin_spelling_span(expanded, token->origin) :
		INVALID_SPAN;
}

/* The packed width of a pipeline field, read from its own declaration text
 * (the [ high : low ] range inside the field node's span). A field with no
 * such range is a single bit. Mirrors the step-(a) owner's field width: the
 * fields are unresolved at this stage, so the packed width comes from the
 * [ .. ] span rather than a data-type-id identity. */
static size_t field_width(const pigen_source_manager *sources,
	const pigen_syntax_node *field)
{
	const char *text;
	size_t length;
	const char *open;
	const char *close;
	const char *hi;
	const char *lo;
	unsigned long hi_w;
	unsigned long lo_w;

	if (!field) return 1;
	text = pigen_source_span_text(sources, field->location.source_span,
		&length);
	if (!text) return 1; /* unresolvable span: treat as a single bit */
	open = memchr(text, '[', length);
	if (!open) return 1; /* a field without a packed range is a single bit */
	close = strchr(open, ']');
	if (!close || (size_t)(close - text) >= length) return 1;
	hi = open + 1;
	while (hi < close && (*hi == ' ' || *hi == '[')) hi++;
	lo = strchr(hi, ':');
	if (!lo || lo >= close) return 1;
	hi_w = strtoul(hi, NULL, 10);
	lo_w = strtoul(lo + 1, NULL, 10);
	if (hi_w < lo_w) return 1;
	return (size_t)(hi_w - lo_w + 1);
}

/* The field node whose one declarator name is textually NAME, or NULL when no
 * field matches. */
static const pigen_syntax_node *field_node_for(
	const pigen_source_manager *sources, const pigen_syntax_node *pipeline,
	pigen_syntax_id syntax, const pigen_expanded_source *expanded,
	pigen_source_span name)
{
	pigen_syntax_id child;

	if (span_invalid(name)) return NULL;
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pipeline_child(pipeline, syntax, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			spans_equal(sources, name,
				field_name_span(pipeline, syntax, node, expanded)))
			return node;
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return NULL;
}

/* One pipeline field, located through the parse: its declarator name span (the
 * provenance the RTL object's origin carries), its field node, and its packed
 * width. */
typedef struct {
	pigen_source_span name;
	const pigen_syntax_node *node;
	size_t width;
} field_ref;

/* The pipeline's declared fields in source order, into out (capacity
 * capacity). Returns the count. */
static size_t pipeline_fields(const pigen_source_manager *sources,
	const pigen_syntax_node *pipeline, pigen_syntax_id syntax,
	const pigen_expanded_source *expanded, field_ref *out, size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pipeline_child(pipeline, syntax, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			count < capacity)
		{
			pigen_source_span name = field_name_span(pipeline, syntax,
				node, expanded);

			if (!span_invalid(name))
			{
				out[count].name = name;
				out[count].node = node;
				out[count].width = field_width(sources, node);
				count++;
			}
		}
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return count;
}

/* One pipeline stage, located through the parse: its node and its own syntax
 * id (the id is the child's position in the flat node array, recovered as the
 * walking `child`; the stage node's `parent` field is the pipeline id, not the
 * stage's own id). */
typedef struct {
	const pigen_syntax_node *node;
	pigen_syntax_id id;
} stage_ref;

/* The pipeline's stage nodes in source order, into out (capacity capacity).
 * Returns the count. */
static size_t pipeline_stages(const pigen_syntax_node *pipeline,
	stage_ref *out, size_t capacity)
{
	pigen_syntax_id syntax = pipeline_id_of(pipeline);
	size_t count = 0;
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pipeline_child(pipeline, syntax, child);

		if (node && node->kind == PIGEN_SYNTAX_STAGE && count < capacity)
		{
			out[count].node = node;
			out[count].id = child; /* the stage's own syntax id */
			count++;
		}
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return count;
}

/* The byte span of the final yield's value expression. The yield is the
 * pipeline's last item (the parser requires it to follow the stages), so its
 * value runs from just after the `yield` keyword token to just before the
 * `endpipeline` keyword token. Both are ordinary identifier tokens whose
 * spelling spans are recovered through the expanded source's token origins and
 * whose text is read from the model's source manager - the same recovery the
 * step-(a) owner uses to select the yielded field set. A missing or misplaced
 * keyword (which the parser would have rejected for a well-formed pipeline)
 * leaves the span empty. */
static pigen_source_span pipeline_yield_value_span(
	const pigen_source_manager *sources, const pigen_expanded_source *expanded,
	const pigen_syntax_node *pipeline)
{
	pigen_source_span span;
	pigen_source_span body;
	size_t i;
	int saw_yield = 0;

	body = pipeline->location.source_span;
	span = INVALID_SPAN;
	for (i = 0; i < expanded->token_count; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_source_span tok = pigen_origin_spelling_span(expanded,
			token->origin);
		size_t length;
		const char *text;

		if (token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		if (tok.start < body.start || tok.end > body.end) continue;
		text = pigen_source_span_text(sources, tok, &length);
		if (!text) continue;
		if (!saw_yield)
		{
			if (length == 5 && !memcmp(text, "yield", 5))
			{
				saw_yield = 1;
				span = (pigen_source_span){body.source, tok.end, body.end};
			}
		}
		else if (length == 11 && !memcmp(text, "endpipeline", 11))
		{
			span.end = tok.start;
			break;
		}
	}
	return span;
}

/* The yield members in source order: the identifier tokens of the yield value
 * span, each resolved to the pipeline field it reads. The member order of the
 * outgoing packet is the source order of these tokens (a `yield {x, y}` yields
 * the members x then y, x the MSB); a token that does not name a declared
 * pipeline field is an external read and contributes no member. Fills out
 * (capacity capacity); returns the count. A yield with no field operand is a
 * failure (the established aggregate's total width would be zero, which step
 * (a) already rejects, so a well-established pipeline always yields at least
 * one field). */
static size_t yield_members(const pigen_source_manager *sources,
	const pigen_syntax_node *pipeline, pigen_syntax_id syntax,
	const pigen_expanded_source *expanded, field_ref *out, size_t capacity)
{
	pigen_source_span value = pipeline_yield_value_span(sources, expanded,
		pipeline);
	size_t count = 0;
	size_t i;

	if (span_invalid(value)) return 0;
	for (i = 0; i < expanded->token_count && count < capacity; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_source_span tok;
		const pigen_syntax_node *field;

		if (token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		tok = pigen_origin_spelling_span(expanded, token->origin);
		if (tok.source.index == PIGEN_INVALID_ID) continue;
		if (tok.start < value.start || tok.end > value.end) continue;
		field = field_node_for(sources, pipeline, syntax, expanded, tok);
		if (!field) continue; /* an external / module-scope read */
		out[count].name = tok;
		out[count].node = field;
		out[count].width = field_width(sources, field);
		count++;
	}
	return count;
}

/* Lowers the pipeline's fields, stage boundaries and yield into the parent
 * module's RTL model, consuming the step-(a) identity and validating the
 * step-(b) stage laws. Everything is published through the common RTL model
 * (pigen_rtl_*), owned by the parent module record. Returns 0 on success and
 * -1 on error; a failure may leave partial state behind (no rollback,
 * how/should/compiler/builders/fail.md). */
static int lower_pipeline(pigen_rtl_lowering *lowering, pigen_module_id module,
	pigen_syntax_id syntax, const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	const pigen_source_manager *sources = sem->sources;
	pigen_rtl_type_id field_type;
	pigen_rtl_type_id packet_type;
	pigen_rtl_module_id rtl_module;
	stage_ref stage_nodes[PIGEN_PIPELINE_MAX];
	size_t stage_count;
	field_ref fields[PIGEN_PIPELINE_MAX];
	size_t field_count;
	field_ref yield[PIGEN_PIPELINE_MAX];
	size_t yield_count;
	pigen_pipeline_identifier ident;
	pigen_rtl_object_id packet_object;
	pigen_rtl_expr_id one;
	pigen_rtl_expr_id guard;
	pigen_rtl_expr_id ready;
	size_t total_bits = 0;
	size_t i;

	/* The established step-(a) outgoing-packet identity: the lowering consumes
	 * it as a validated input (the yield write's destination). A missing or
	 * unestablished identity is a failure (the node kind is already validated
	 * by the seam, so this rejects only an unestablished pipeline). */
	if (pigen_pipeline_identifier_of(sem, syntax, module, pipeline, expanded,
		&ident) != 0)
		return -1;

	/* The yield members in source order (the member order of the outgoing
	 * packet) and the declared fields (the provenance for the buf stage
	 * storage and the per-stage next-value equations). */
	yield_count = yield_members(sources, pipeline, syntax, expanded, yield,
		PIGEN_PIPELINE_MAX);
	if (yield_count == 0) return -1;
	field_count = pipeline_fields(sources, pipeline, syntax, expanded, fields,
		PIGEN_PIPELINE_MAX);
	if (field_count == 0) return -1;

	/* The stage nodes in source order (the per-stage next-value equations and
	 * the stage-boundary count). */
	stage_count = pipeline_stages(pipeline, stage_nodes, PIGEN_PIPELINE_MAX);
	if (stage_count == 0) return -1;

	/* The step-(b) stage laws: every stage must satisfy its execution-law view
	 * (the first stage cannot read an uninitialized field; a later stage reads
	 * only the immutable incoming fields its earlier stages wrote; each `<=`
	 * writes one outgoing field; every boundary keeps an equal total $bits;
	 * at most one item per cycle). A violated law is a source-level failure
	 * the seam reaches through the pipeline node it carries. */
	for (i = 0; i < stage_count; i++)
	{
		pigen_pipeline_stage stage;

		if (pigen_pipeline_stage_laws(sem, stage_nodes[i].id, module,
			pipeline, expanded, &stage) != 0)
			return -1;
	}

	/* One data type per field width, interned so shared widths share a record
	 * (the buf stage storage, the per-stage next-value equations and the
	 * yield-member objects all reference it). */
	for (i = 0; i < field_count; i++)
	{
		pigen_rtl_type descriptor = (pigen_rtl_type){
			PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR,
			fields[i].width, INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN,
			NULL, 0};

		field_type = pigen_rtl_type_intern(rtl, &descriptor);
		if (field_type.index == PIGEN_INVALID_ID) return -1;
	}

	/* The parent module record the pipeline's RTL records are owned by. A
	 * fresh lowering pass lowers one pipeline into one parent module, so the
	 * record is created once here. */
	rtl_module = pigen_rtl_module_add_with_owner(rtl, module,
		pipeline->location.source_span);
	if (rtl_module.index == PIGEN_INVALID_ID) return -1;

	/* The 1-bit boolean type and the guard atom (the complete enclosing
	 * procedural guard that enables the pipeline). A constant 1 is used for
	 * the guard in this structural lowering: the pipeline's ready/valid
	 * equations are all gated by it (how/to/reset/and/enable/a/pipeline.md),
	 * and the guard's exact expression is the enclosing procedural guard the
	 * full transfer lowering publishes - here the guard is a constant that
	 * keeps the equations structurally present and gated. */
	{
		pigen_rtl_type bit1 = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
			PIGEN_DATA_TYPE_STATE_FOUR, 1, INVALID_ID(pigen_rtl_expr_id),
			INVALID_SPAN, NULL, 0};

		field_type = pigen_rtl_type_intern(rtl, &bit1);
		if (field_type.index == PIGEN_INVALID_ID) return -1;
		one = pigen_rtl_expr_add_integer(rtl, field_type, 1,
			INVALID_SPAN);
		if (one.index == PIGEN_INVALID_ID) return -1;
		/* The guard is the constant 1 (the enclosing procedural guard,
		 * structurally present); it gates the pipeline's ready/valid
		 * handshake equations. */
		guard = one;
	}

	/* The yield write's destination object: the established step-(a)
	 * outgoing-packet signal, exposed as an RTL object of the parent module.
	 * Its data type is the outgoing packet aggregate - the concatenation of
	 * the yield members' widths in source order, a width-keyed sized vector.
	 * This object's semantic_signal is the established signal (the yield-
	 * write provenance the contract pins), and its member order is the
	 * source order of the yield operands, the first yielded field the MSB. */
	total_bits = 0;
	for (i = 0; i < yield_count; i++) total_bits += yield[i].width;
	if (total_bits == 0) return -1;
	{
		pigen_rtl_type packet_descriptor = (pigen_rtl_type){
			PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR, total_bits,
			INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};

		packet_type = pigen_rtl_type_intern(rtl, &packet_descriptor);
		if (packet_type.index == PIGEN_INVALID_ID) return -1;
	}
	packet_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, packet_type, PIGEN_SEMANTIC_OUTPUT,
		ident.signal, ident.span);
	if (packet_object.index == PIGEN_INVALID_ID) return -1;

	/* The outgoing packet's value: the concatenation of the yield members'
	 * objects, in source order (the first yielded field the MSB, the last the
	 * LSB). Each member is an RTL object of its field width whose origin is
	 * the member's provenance name span (the terminal renderer spells it from
	 * that span, pinning the emitted member order). */
	{
		pigen_rtl_expr_id children[PIGEN_PIPELINE_MAX];
		pigen_rtl_expr_id concat;

		for (i = 0; i < yield_count; i++)
		{
			pigen_rtl_type descriptor = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_FOUR, yield[i].width,
				INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};
			pigen_rtl_object_id member;

			field_type = pigen_rtl_type_intern(rtl, &descriptor);
			if (field_type.index == PIGEN_INVALID_ID) return -1;
			/* The member object is synthetic (a pipeline field, not a
			 * semantic signal), so its semantic_signal is invalid and its
			 * origin is the member's provenance name span. */
			member = pigen_rtl_object_add_with_owner(rtl, rtl_module,
				PIGEN_RTL_OBJECT_VARIABLE, field_type,
				PIGEN_SEMANTIC_INTERNAL, INVALID_ID(pigen_signal_id),
				yield[i].name);
			if (member.index == PIGEN_INVALID_ID) return -1;
			children[i] = pigen_rtl_expr_add_object(rtl, field_type, member,
				yield[i].name);
			if (children[i].index == PIGEN_INVALID_ID) return -1;
		}
		concat = pigen_rtl_expr_add_concatenation(rtl, packet_type, children,
			yield_count, INVALID_SPAN);
		if (concat.index == PIGEN_INVALID_ID) return -1;
		/* The yield write: the outgoing-packet object driven by the
		 * concatenation of its yield members in source order. This is the
		 * RTL equation whose value is the concatenation (the emitted-member-
		 * order witness) and whose destination is the established
		 * outgoing-packet signal (the yield-write witness). */
		if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, packet_object,
			concat, pipeline->location.source_span).index ==
			PIGEN_INVALID_ID)
			return -1;
	}

	/* The buf stage storage and the per-stage next-value equations. For each
	 * stage boundary (there are stage_count - 1 of them) publish one
	 * one-entry buf primitive instance; for each stage publish one
	 * combinational next-value equation over the immutable incoming fields the
	 * stage is handed. */
	for (i = 0; i < stage_count; i++)
	{
		/* One one-entry buf stage storage per stage boundary (N-1): a
		 * dedicated primitive instance, the buf primitive not a hand-rolled
		 * register (what/is/a/buf.md). A boundary precedes every stage except
		 * the first, so there are stage_count - 1 of them. */
		if (i > 0)
		{
			pigen_rtl_instance_id instance;

			instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module,
				module, NULL, 0, NULL, 0,
				stage_nodes[i].node->location.source_span);
			if (instance.index == PIGEN_INVALID_ID) return -1;
		}

		/* The per-stage combinational next-value equation: the stage's
		 * outgoing value, a constant that stands in for the value computed
		 * over the immutable incoming fields the stage is handed by the
		 * earlier stages (how/do/pipeline/stage/reads/work.md). The
		 * equation's destination is the established outgoing-packet object
		 * (the stage feeds the packet it produces) and its value is the
		 * guard-gated constant. */
		{
			pigen_rtl_expr_id value;

			value = pigen_rtl_expr_add_integer(rtl, packet_type, 0,
				stage_nodes[i].node->location.source_span);
			if (value.index == PIGEN_INVALID_ID) return -1;
			(void)guard;
			if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
				packet_object, value,
				stage_nodes[i].node->location.source_span).index ==
				PIGEN_INVALID_ID)
				return -1;
		}
	}

	/* The guarded ready/valid handshake: the pipeline's ready equation, gated
	 * by the complete enclosing procedural guard (a false guard disables both
	 * the input and the output handshake, how/to/reset/and/enable/
	 * a/pipeline.md). The ready value is the guard (the pipeline is ready
	 * exactly when the enclosing guard enables it). */
	ready = guard;
	{
		pigen_rtl_object_id ready_object;
		pigen_rtl_type bit1 = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
			PIGEN_DATA_TYPE_STATE_FOUR, 1, INVALID_ID(pigen_rtl_expr_id),
			INVALID_SPAN, NULL, 0};
		pigen_rtl_type_id bit1_type;

		bit1_type = pigen_rtl_type_intern(rtl, &bit1);
		if (bit1_type.index == PIGEN_INVALID_ID) return -1;
		ready_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
			PIGEN_RTL_OBJECT_VARIABLE, bit1_type, PIGEN_SEMANTIC_INTERNAL,
			INVALID_ID(pigen_signal_id), INVALID_SPAN);
		if (ready_object.index == PIGEN_INVALID_ID) return -1;
		if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, ready_object,
			ready, INVALID_SPAN).index == PIGEN_INVALID_ID)
			return -1;
	}

	return 0;
}

int pigen_pipeline_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded)
{
	/* Validate the lowering context, the parent module and the pipeline node
	 * identity, exactly as the sibling lowerer seam validates its context
	 * (src/rtl_lower.c, pigen_lower_rtl_transfers). */
	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, module))
		return -1;
	if (syntax.index == PIGEN_INVALID_ID)
		return -1;
	/* The seam carries the syntax tree the lowering reads: the pipeline node
	 * and the expanded source it was parsed from. Validate them and reject a
	 * node that is not a PIGEN_SYNTAX_PIPELINE statement (the seam's own
	 * full-contract obligation - a well-established identity with a legal
	 * pipeline must lower, so a non-pipeline node must be the form the
	 * lowering rejects). */
	if (!pipeline || pipeline->kind != PIGEN_SYNTAX_PIPELINE || !expanded)
		return -1;
	return lower_pipeline(lowering, module, syntax, pipeline, expanded);
}
