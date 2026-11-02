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

/* True when TOKEN span lies outside the enclosing ITEM span (a different
 * source, or an offset that crosses the item's start or end). */
static int span_outside(pigen_source_span token, pigen_source_span item)
{
	if (span_invalid(token) || span_invalid(item)) return 1;
	if (token.source.index != item.source.index) return 1;
	return token.start < item.start || token.end > item.end;
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

/* The nearest enclosing IF_STATEMENT ancestor of the pipeline node, walking
 * the node's parent chain (the guarded pipeline sits inside the if's then-
 * block, so the chain runs pipeline -> its procedural block -> the if), or
 * NULL when the pipeline has no enclosing if. The parent chain is walked
 * through the flat node array, the same recovery the owner uses for its
 * children (pipeline_child); the bounded depth guards against a corrupt or
 * cyclic chain. */
static const pigen_syntax_node *pipeline_enclosing_if(
	const pigen_syntax_node *pipeline, pigen_syntax_id syntax)
{
	const pigen_syntax_node *base = pipeline - syntax.index;
	const pigen_syntax_node *node = pipeline;
	int depth = 0;

	while (node->parent.index != PIGEN_INVALID_ID && depth < 128)
	{
		node = base + node->parent.index;
		if (node->kind == PIGEN_SYNTAX_IF_STATEMENT) return node;
		depth++;
	}
	return NULL;
}

/* The token index range of the if statement's condition expression: the
 * tokens strictly between the `(` after the `if` keyword and its matching
 * `)`, both located inside the if node's span through the expanded source's
 * token spelling spans (the same recovery the yield-value span uses). A
 * well-formed if statement always carries a balanced condition, so a failure
 * means a malformed node the parser would have rejected. */
static int guard_condition_range(const pigen_source_manager *sources,
	const pigen_expanded_source *expanded, const pigen_syntax_node *if_node,
	size_t *first, size_t *last)
{
	pigen_source_span if_span = if_node->location.source_span;
	size_t i;
	size_t open_index;
	int depth = 0;

	*first = *last = 0;
	open_index = 0;
	for (i = 0; i < expanded->token_count; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_source_span tok = pigen_origin_spelling_span(expanded,
			token->origin);
		size_t length;
		const char *text;

		if (token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		if (tok.start < if_span.start || tok.end > if_span.end) continue;
		text = pigen_source_span_text(sources, tok, &length);
		if (text && length == 2 && !memcmp(text, "if", 2))
		{
			open_index = i;
			break;
		}
	}
	if (open_index == expanded->token_count) return 0;
	for (i = open_index + 1; i < expanded->token_count; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_source_span tok = pigen_origin_spelling_span(expanded,
			token->origin);
		size_t length;
		const char *text;

		if (token->kind != PIGEN_TOKEN_SYMBOL) continue;
		if (tok.start < if_span.start || tok.end > if_span.end) continue;
		text = pigen_source_span_text(sources, tok, &length);
		if (!text || length != 1) continue;
		if (!depth)
		{
			if (text[0] != '(') return 0; /* not the condition's open paren */
			depth = 1;
			*first = i + 1;
			continue;
		}
		if (text[0] == '(') depth++;
		else if (text[0] == ')')
		{
			depth--;
			if (!depth)
			{
				*last = i - 1;
				return 1;
			}
		}
	}
	return 0;
}

/* The semantic signal whose declared name spells the same text as NAME (read
 * from the source manager), or an invalid identity when no signal matches.
 * The guard name is an identifier whose spelling matches the signal's
 * declaration name; the spans differ (the guard token sits in the if
 * condition, the declaration name in the port list), so the match is by text.
 */
static pigen_signal_id signal_for_name(const pigen_source_manager *sources,
	const pigen_semantic_model *sem, pigen_source_span name)
{
	size_t name_length;
	const char *name_text;
	size_t i;

	if (span_invalid(name)) return (pigen_signal_id){PIGEN_INVALID_ID};
	name_text = pigen_source_span_text(sources, name, &name_length);
	if (!name_text) return (pigen_signal_id){PIGEN_INVALID_ID};
	for (i = 0; i < sem->signal_count; i++)
	{
		const pigen_semantic_signal *signal = &sem->signals[i];
		size_t sig_length;
		const char *sig_text;

		if (span_invalid(signal->span)) continue;
		sig_text = pigen_source_span_text(sources, signal->span, &sig_length);
		if (!sig_text) continue;
		if (sig_length == name_length &&
			!memcmp(sig_text, name_text, name_length))
			return (pigen_signal_id){(uint32_t)i};
	}
	return (pigen_signal_id){PIGEN_INVALID_ID};
}

/* One guard operand's RTL expression: a 1-bit object whose origin is the
 * operand's own spelling span (the terminal renderer spells it from that span,
 * and the contract checks the span's text) and whose data type is its semantic
 * signal's lowered type. An operand that names no declared signal is a
 * failure (the semantic model rejects an undeclared guard signal for a
 * validated source). The object is owned by rtl_module, the parent module
 * record the whole lowering publishes into. */
static int guard_operand(const pigen_source_manager *sources,
	pigen_rtl_lowering *lowering, pigen_rtl_module_id rtl_module,
	const pigen_expanded_source *expanded, const pigen_expanded_token *token,
	pigen_rtl_expr_id *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_source_span span = pigen_origin_spelling_span(expanded,
		token->origin);
	pigen_signal_id signal;
	const pigen_semantic_signal *sig;
	pigen_rtl_type_id operand_type;
	pigen_rtl_object_id object;

	if (span_invalid(span)) return 0;
	signal = signal_for_name(sources, sem, span);
	if (signal.index == PIGEN_INVALID_ID) return 0;
	sig = pigen_signal_get(sem, signal);
	operand_type = pigen_lower_rtl_type(lowering, sig->data_type);
	if (operand_type.index == PIGEN_INVALID_ID) return 0;
	object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, operand_type, sig->direction, signal,
		span);
	if (object.index == PIGEN_INVALID_ID) return 0;
	*out = pigen_rtl_expr_add_object(rtl, operand_type, object, span);
	return (*out).index != PIGEN_INVALID_ID;
}

/* Builds the RTL expression for the if statement's condition, the complete
 * enclosing procedural guard (how/to/reset/and/enable/a/pipeline.md), from
 * the condition's token range. A name is a 1-bit object spelling its own
 * token (guard_operand); `&&` and `||` are logical-and / logical-or over their
 * operands, each resolved through the common binary-operation resolver so the
 * published resolution is the semantic model's, not an invented one; a
 * parenthesized group recurses. The result is 1-bit. Returns 1 on success. */
static int guard_expr_build(const pigen_source_manager *sources,
	pigen_rtl_lowering *lowering, pigen_rtl_module_id rtl_module,
	const pigen_expanded_source *expanded, size_t first, size_t last,
	pigen_rtl_expr_id *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_expr_id left;
	pigen_rtl_expr_id right;
	pigen_binary_resolution resolution;
	pigen_rtl_type_id result_type;
	pigen_source_span tok;
	size_t length;
	const char *text;
	size_t i;

	/* The range must be non-empty. */
	if (first > last) return 0;

	/* A parenthesized group: the range is `( ... )`, recurse on the inner
	 * tokens. */
	if (expanded->tokens[first].kind == PIGEN_TOKEN_SYMBOL)
	{
		tok = pigen_origin_spelling_span(expanded,
			expanded->tokens[first].origin);
		text = pigen_source_span_text(sources, tok, &length);
		if (text && length == 1 && text[0] == '(' &&
			expanded->tokens[last].kind == PIGEN_TOKEN_SYMBOL)
		{
			pigen_source_span last_tok = pigen_origin_spelling_span(expanded,
				expanded->tokens[last].origin);
			size_t last_length;
			const char *last_text = pigen_source_span_text(sources, last_tok,
				&last_length);

			if (last_text && last_length == 1 && last_text[0] == ')')
				return guard_expr_build(sources, lowering, rtl_module,
					expanded, first + 1, last - 1, out);
		}
	}

	/* A single token: a name (the guard signal) or a number literal. */
	if (first == last)
	{
		const pigen_expanded_token *token = &expanded->tokens[first];

		if (token->kind == PIGEN_TOKEN_IDENTIFIER)
			return guard_operand(sources, lowering, rtl_module, expanded,
				token, out);
		if (token->kind == PIGEN_TOKEN_NUMBER)
		{
			pigen_rtl_type bit1 = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_FOUR, 1,
				INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};
			pigen_rtl_type_id bit1_type;
			uint64_t value = 0;

			tok = pigen_origin_spelling_span(expanded, token->origin);
			text = pigen_source_span_text(sources, tok, &length);
			if (text) value = strtoull(text, NULL, 0);
			bit1_type = pigen_rtl_type_intern(rtl, &bit1);
			if (bit1_type.index == PIGEN_INVALID_ID) return 0;
			*out = pigen_rtl_expr_add_integer(rtl, bit1_type, value, tok);
			return (*out).index != PIGEN_INVALID_ID;
		}
		return 0;
	}

	/* A binary operator: find a top-level `&&` or `||` inside the range (the
	 * guard is a flat chain for this lowering; nested groups are
	 * parenthesized, so the operator sits at paren-depth zero). Split on the
	 * first such operator in source order and build the two sides. */
	for (i = first; i < last; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_binary_operator operator;
		int depth = 0;
		int balanced = 1;
		size_t i2;

		if (token->kind != PIGEN_TOKEN_SYMBOL) continue;
		tok = pigen_origin_spelling_span(expanded, token->origin);
		text = pigen_source_span_text(sources, tok, &length);
		if (!text) continue;
		if (length == 2 && !memcmp(text, "&&", 2))
			operator = PIGEN_BINARY_LOGICAL_AND;
		else if (length == 2 && !memcmp(text, "||", 2))
			operator = PIGEN_BINARY_LOGICAL_OR;
		else continue;
		/* The operator must sit at top level (paren-depth zero) within
		 * [first, i): scan the tokens before it. */
		for (i2 = first; i2 < i; i2++)
		{
			if (expanded->tokens[i2].kind != PIGEN_TOKEN_SYMBOL) continue;
			{
				pigen_source_span t2 = pigen_origin_spelling_span(expanded,
					expanded->tokens[i2].origin);
				size_t l2;
				const char *x = pigen_source_span_text(sources, t2, &l2);

				if (!x || l2 != 1) continue;
				if (x[0] == '(') depth++;
				else if (x[0] == ')') depth--;
			}
		}
		balanced = (depth == 0);
		if (!balanced) continue;
		if (!guard_expr_build(sources, lowering, rtl_module, expanded, first,
			i - 1, &left))
			return 0;
		if (!guard_expr_build(sources, lowering, rtl_module, expanded, i + 1,
			last, &right))
			return 0;
		/* The published resolution: the semantic model's binary operation for
		 * the operator over 1-bit logic operands (the guard operands are the
		 * 1-bit guard signals), identity-converted. */
		{
			pigen_data_type_id logic_one =
				pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);

			if (logic_one.index == PIGEN_INVALID_ID) return 0;
			if (!pigen_data_type_resolve_binary_operation(sem, operator,
				logic_one, logic_one, &resolution))
				return 0;
		}
		result_type = pigen_lower_rtl_type(lowering,
			resolution.operation.result_data_type);
		if (result_type.index == PIGEN_INVALID_ID) return 0;
		*out = pigen_rtl_expr_add_binary(rtl, result_type, &resolution, left,
			right, tok);
		return (*out).index != PIGEN_INVALID_ID;
	}
	return 0; /* no recognized top-level operator: a malformed condition */
}

/* Parses a SystemVerilog based literal `<width>'[s]<base><digits>` (for
 * example `8'h3D`) into its packed width and its unsigned value. The width is
 * the decimal run before the apostrophe; the base letter selects the digit
 * base (b/o/d/h) and the digits are read in that base. A literal that is not a
 * well-formed based literal (no apostrophe, no width, an unknown base, or
 * empty digits) returns 0 and leaves its outputs untouched. */
static int parse_based_literal(const char *text, size_t length, size_t *width,
	uint64_t *value)
{
	const char *apostrophe;
	size_t at;
	unsigned base;
	unsigned long long v;

	apostrophe = text ? memchr(text, '\'', length) : NULL;
	if (!apostrophe || apostrophe == text) return 0;
	*width = strtoul(text, NULL, 10);
	if (!*width) return 0;
	at = (size_t)(apostrophe - text) + 1;
	if (at < length && (text[at] == 's' || text[at] == 'S')) at++;
	if (at == length) return 0;
	switch (text[at++])
	{
		case 'b': case 'B': base = 2; break;
		case 'o': case 'O': base = 8; break;
		case 'd': case 'D': base = 10; break;
		case 'h': case 'H': base = 16; break;
		default: return 0;
	}
	v = 0;
	for (; at < length; at++)
	{
		unsigned digit;
		char c = text[at];

		if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
		else return 0;
		if (digit >= base) return 0;
		v = v * (unsigned long long)base + (unsigned long long)digit;
	}
	*value = (uint64_t)v;
	return 1;
}

/* One operand of a stage next-value expression: an identifier token binds the
 * object it reads - a pipeline field reads its immutable incoming field object
 * (the object the earlier stage wrote, how/do/pipeline/stage/reads/work.md)
 * whose origin is the field's provenance name span and whose type is the
 * field's packed width; an enclosing module signal binds its own object, whose
 * type is the signal's lowered data type and whose origin is the signal's
 * declaration span (the terminal renderer spells it from that span). Returns 1
 * on success and fills *out. A name that is neither a pipeline field nor a
 * declared signal is a failure (the step-(b) stage law already validated the
 * source, so this rejects only an unresolvable read). */
static int stage_expr_name(const pigen_source_manager *sources,
	pigen_rtl_lowering *lowering, pigen_rtl_module_id rtl_module,
	const pigen_expanded_source *expanded, size_t at,
	const field_ref *fields, size_t field_count, pigen_rtl_expr_id *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	const pigen_expanded_token *token = &expanded->tokens[at];
	pigen_source_span span;
	size_t i;

	span = pigen_origin_spelling_span(expanded, token->origin);
	if (span_invalid(span)) return 0;

	/* A pipeline-field read: the immutable incoming field object, bound by
	 * the field's provenance name span (the read's own span is a different
	 * occurrence of the same field name, so the match is by text). */
	for (i = 0; i < field_count; i++)
	{
		if (!spans_equal(sources, span, fields[i].name)) continue;
		{
			pigen_rtl_type descriptor = (pigen_rtl_type){
				PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR,
				fields[i].width, INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN,
				NULL, 0};
			pigen_rtl_type_id field_type = pigen_rtl_type_intern(rtl,
				&descriptor);
			pigen_rtl_object_id object;

			if (field_type.index == PIGEN_INVALID_ID) return 0;
			object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
				PIGEN_RTL_OBJECT_VARIABLE, field_type,
				PIGEN_SEMANTIC_INTERNAL, INVALID_ID(pigen_signal_id),
				fields[i].name);
			if (object.index == PIGEN_INVALID_ID) return 0;
			*out = pigen_rtl_expr_add_object(rtl, field_type, object,
				fields[i].name);
			return (*out).index != PIGEN_INVALID_ID;
		}
	}

	/* An enclosing module-signal read: the signal's own object. */
	{
		pigen_signal_id signal = signal_for_name(sources, sem, span);
		const pigen_semantic_signal *sig;
		pigen_rtl_type_id operand_type;
		pigen_rtl_object_id object;

		if (signal.index == PIGEN_INVALID_ID) return 0;
		sig = pigen_signal_get(sem, signal);
		if (!sig) return 0;
		operand_type = pigen_lower_rtl_type(lowering, sig->data_type);
		if (operand_type.index == PIGEN_INVALID_ID) return 0;
		object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
			PIGEN_RTL_OBJECT_VARIABLE, operand_type, sig->direction, signal,
			sig->span);
		if (object.index == PIGEN_INVALID_ID) return 0;
		*out = pigen_rtl_expr_add_object(rtl, operand_type, object, sig->span);
		return (*out).index != PIGEN_INVALID_ID;
	}
}

/* The precedence rank of a binary operator token's text (a higher rank binds
 * tighter), or -1 when the token is not a binary operator. The rank order is
 * the SystemVerilog operator precedence: power, then unary, then multiply,
 * then add, then shift, then relational, then equality, then case equality,
 * then bitwise-and, bitwise-xor, bitwise-or, then logical-and, logical-or. */
static int stage_operator_rank(const char *text, size_t length,
	pigen_binary_operator *op)
{
	if (!text || !length) return -1;
#define OP(n, r) \
	if (length == (sizeof n - 1) && !memcmp(text, n, length)) \
		return r;
	OP("||", 1); OP("&&", 2);
	OP("|", 3); OP("^~", 4); OP("~^", 4); OP("^", 4); OP("&", 5);
	OP("==?", 6); OP("!=?", 6); OP("===", 7); OP("!==", 7);
	OP("==", 8); OP("!=", 8);
	OP("<=", 9); OP(">=", 9); OP("<", 9); OP(">", 9);
	OP("<<", 10); OP(">>", 10); OP("<<<", 10); OP(">>>", 10);
	OP("+", 11); OP("-", 11);
	OP("*", 12); OP("/", 12); OP("%", 12);
	OP("**", 13);
#undef OP
	(void)op;
	return -1;
}

/* The binary operator a binary-operator token's text lowers to (filled into
 * *op), or -1 when the text is not a binary operator. */
static int stage_operator_binary(const char *text, size_t length,
	pigen_binary_operator *op)
{
	int rank = stage_operator_rank(text, length, op);
	pigen_binary_operator b;

	if (rank < 0) return -1;
	switch (rank)
	{
	case 1: b = PIGEN_BINARY_LOGICAL_OR; break;
	case 2: b = PIGEN_BINARY_LOGICAL_AND; break;
	case 3: b = PIGEN_BINARY_BITWISE_OR; break;
	case 4: b = (text[0] == '~') ? PIGEN_BINARY_BITWISE_XNOR :
		PIGEN_BINARY_BITWISE_XOR; break;
	case 5: b = PIGEN_BINARY_BITWISE_AND; break;
	case 6: b = (text[0] == '!') ? PIGEN_BINARY_WILDCARD_NOT_EQUAL :
		PIGEN_BINARY_WILDCARD_EQUAL; break;
	case 7: b = (text[0] == '!') ? PIGEN_BINARY_CASE_NOT_EQUAL :
		PIGEN_BINARY_CASE_EQUAL; break;
	case 8: b = (text[0] == '!') ? PIGEN_BINARY_NOT_EQUAL :
		PIGEN_BINARY_EQUAL; break;
	case 9:
		if (length == 1)
			b = (text[0] == '<') ? PIGEN_BINARY_LESS : PIGEN_BINARY_GREATER;
		else
			b = (text[0] == '<') ? PIGEN_BINARY_LESS_EQUAL :
				PIGEN_BINARY_GREATER_EQUAL;
		break;
	case 10:
		if (length == 2)
			b = (text[0] == '<') ? PIGEN_BINARY_SHIFT_LEFT :
				PIGEN_BINARY_SHIFT_RIGHT;
		else
			b = (text[0] == '<') ? PIGEN_BINARY_ARITH_SHIFT_LEFT :
				PIGEN_BINARY_ARITH_SHIFT_RIGHT;
		break;
	case 11: b = (text[0] == '+') ? PIGEN_BINARY_ADD : PIGEN_BINARY_SUBTRACT;
		break;
	case 12: b = (text[0] == '*') ? PIGEN_BINARY_MULTIPLY :
		(text[0] == '/') ? PIGEN_BINARY_DIVIDE : PIGEN_BINARY_MODULO;
		break;
	case 13: b = PIGEN_BINARY_POWER; break;
	default: return -1;
	}
	*op = b;
	return rank;
}

/* The RTL expression of the tokens FIRST..LAST (inclusive) in the expanded
 * source, the right-hand side of a stage `<=` lowered to a structural
 * expression over the immutable incoming fields (never a constant stand-in).
 * A single identifier lowers to the object it reads (stage_expr_name); a
 * single number to an integer of its own width; a parenthesized group recurses
 * on its body; a concatenation `{ a, b, ... }` recurses on its comma-separated
 * operands in source order; a binary chain splits on its lowest-precedence
 * top-level operator and resolves through the semantic model. Returns 1 on
 * success and fills *out; an empty range or an unrecognised token is a
 * failure, so a stage whose value cannot be lowered reports its diagnostic at
 * the original source span and the compile stops
 * (how/should/compiler/builders/fail.md). */
static int stage_expr_build(const pigen_source_manager *sources,
	pigen_rtl_lowering *lowering, pigen_rtl_module_id rtl_module,
	const pigen_expanded_source *expanded, size_t first, size_t last,
	const field_ref *fields, size_t field_count, pigen_rtl_expr_id *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	size_t i;

	if (first > last) return 0;

	/* A parenthesized group: `( ... )` recurses on the body. */
	if (expanded->tokens[first].kind == PIGEN_TOKEN_SYMBOL)
	{
		const pigen_expanded_token *t = &expanded->tokens[first];
		pigen_source_span tok = pigen_origin_spelling_span(expanded,
			t->origin);
		size_t length;
		const char *text = pigen_source_span_text(sources, tok, &length);

		if (text && length == 1 && text[0] == '(')
		{
			const pigen_expanded_token *tl = &expanded->tokens[last];
			pigen_source_span tokl = pigen_origin_spelling_span(expanded,
				tl->origin);
			size_t ll;
			const char *txtl = pigen_source_span_text(sources, tokl, &ll);

			if (txtl && ll == 1 && txtl[0] == ')')
				return stage_expr_build(sources, lowering, rtl_module,
					expanded, first + 1, last - 1, fields, field_count,
					out);
		}
	}

	/* A single token: a name (a read) or a number (a literal). */
	if (first == last)
	{
		const pigen_expanded_token *token = &expanded->tokens[first];

		if (token->kind == PIGEN_TOKEN_IDENTIFIER)
			return stage_expr_name(sources, lowering, rtl_module, expanded,
				first, fields, field_count, out);
		if (token->kind == PIGEN_TOKEN_NUMBER)
		{
			pigen_source_span tok = pigen_origin_spelling_span(expanded,
				token->origin);
			const char *text;
			size_t length;
			size_t width;
			uint64_t value;
			pigen_rtl_type descriptor;
			pigen_rtl_type_id type;

			text = pigen_source_span_text(sources, tok, &length);
			if (!text) return 0;
			/* A based literal W'base digits; a plain number is a decimal
			 * literal of the width that holds its digits. */
			if (!parse_based_literal(text, length, &width, &value))
			{
				width = 1;
				value = strtoull(text, NULL, 0);
				while (value > (1ull << (width * 8 - 1)))
				{
					if (width * 8 >= 64) break;
					width = width * 2 < 16 ? width * 2 : 16;
					width = width < 8 ? 8 : width;
				}
				if (width < 8) width = 8;
			}
			descriptor = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
				PIGEN_DATA_TYPE_STATE_FOUR, width,
				INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};
			type = pigen_rtl_type_intern(rtl, &descriptor);
			if (type.index == PIGEN_INVALID_ID) return 0;
			*out = pigen_rtl_expr_add_integer(rtl, type, value, tok);
			return (*out).index != PIGEN_INVALID_ID;
		}
		return 0;
	}

	/* A concatenation `{ a, b, ... }`: split the body on top-level commas and
	 * publish each operand in source order. */
	if (expanded->tokens[first].kind == PIGEN_TOKEN_SYMBOL)
	{
		const pigen_expanded_token *t = &expanded->tokens[first];
		pigen_source_span tok = pigen_origin_spelling_span(expanded,
			t->origin);
		size_t length;
		const char *text = pigen_source_span_text(sources, tok, &length);

		if (text && length == 1 && text[0] == '{')
		{
			const pigen_expanded_token *tl = &expanded->tokens[last];
			pigen_source_span tokl = pigen_origin_spelling_span(expanded,
				tl->origin);
			size_t ll;
			const char *txtl = pigen_source_span_text(sources, tokl, &ll);
			pigen_rtl_expr_id child_ids[PIGEN_PIPELINE_MAX];
			pigen_rtl_expr_id concat;
			uint64_t total = 0;
			size_t child_count = 0;
			int depth = 0;
			size_t part_first = first + 1;
			size_t c;
			pigen_rtl_type_id concat_type;

			if (!(txtl && ll == 1 && txtl[0] == '}')) return 0;
			for (i = first + 1; i < last; i++)
			{
				const pigen_expanded_token *ti = &expanded->tokens[i];
				pigen_source_span tki = pigen_origin_spelling_span(expanded,
					ti->origin);
				size_t li;
				const char *txti = pigen_source_span_text(sources, tki, &li);

				if (ti->kind != PIGEN_TOKEN_SYMBOL || !txti) continue;
				if (txti[0] == '{' || txti[0] == '(') depth++;
				else if (txti[0] == '}' || txti[0] == ')') depth--;
				else if (!depth && txti[0] == ',')
				{
					if (child_count >= PIGEN_PIPELINE_MAX) return 0;
					if (!stage_expr_build(sources, lowering, rtl_module,
						expanded, part_first, i - 1, fields, field_count,
						&child_ids[child_count]))
						return 0;
					{
						const pigen_rtl_expr *ce = pigen_rtl_expr_get(rtl,
							child_ids[child_count]);
						const pigen_rtl_type *ct;

						if (!ce) return 0;
						ct = pigen_rtl_type_get(rtl, ce->type);
						if (!ct) return 0;
						total += (uint64_t)ct->width;
					}
					child_count++;
					part_first = i + 1;
				}
			}
			if (!stage_expr_build(sources, lowering, rtl_module, expanded,
				part_first, last - 1, fields, field_count,
				&child_ids[child_count]))
				return 0;
			{
				const pigen_rtl_expr *ce = pigen_rtl_expr_get(rtl,
					child_ids[child_count]);
				const pigen_rtl_type *ct;

				if (!ce) return 0;
				ct = pigen_rtl_type_get(rtl, ce->type);
				if (!ct) return 0;
				total += (uint64_t)ct->width;
			}
			child_count++;
			if (!child_count) return 0;
			{
				pigen_rtl_type descriptor = (pigen_rtl_type){
					PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR,
					(uint64_t)total, INVALID_ID(pigen_rtl_expr_id),
					INVALID_SPAN, NULL, 0};

				concat_type = pigen_rtl_type_intern(rtl, &descriptor);
				if (concat_type.index == PIGEN_INVALID_ID) return 0;
			}
			concat = pigen_rtl_expr_add_concatenation(rtl, concat_type,
				child_ids, child_count,
				pigen_origin_spelling_span(expanded,
					expanded->tokens[first].origin));
			if (concat.index == PIGEN_INVALID_ID)
				return 0;
			*out = concat;
			(void)c;
			return 1;
		}
	}

	/* A binary chain: split on the lowest-precedence top-level operator (the
	 * one at paren/brace-depth zero with the smallest rank), recurse on the two
	 * sides, resolve through the semantic model, and publish the binary. */
	{
		int best_rank = 999;
		size_t best_at = last + 1;
		int depth = 0;

		for (i = first; i <= last; i++)
		{
			const pigen_expanded_token *ti = &expanded->tokens[i];
			pigen_source_span tki;
			size_t li;
			const char *txti;
			int rank;

			if (ti->kind != PIGEN_TOKEN_SYMBOL) continue;
			tki = pigen_origin_spelling_span(expanded, ti->origin);
			txti = pigen_source_span_text(sources, tki, &li);
			if (!txti) continue;
			if (txti[0] == '{' || txti[0] == '(')
			{
				depth++;
				continue;
			}
			if (txti[0] == '}' || txti[0] == ')')
			{
				depth--;
				continue;
			}
			if (depth) continue;
			rank = stage_operator_rank(txti, li, NULL);
			if (rank < 0) continue;
			if (rank < best_rank)
			{
				best_rank = rank;
				best_at = i;
			}
		}
		if (best_at > last) return 0; /* no top-level operator: malformed */
		{
			pigen_rtl_expr_id left;
			pigen_rtl_expr_id right;
			pigen_binary_operator op;
			pigen_data_type_id left_data;
			pigen_data_type_id right_data;
			pigen_binary_resolution resolution;
			pigen_rtl_type_id result_type;
			size_t left_width;
			size_t right_width;
			const pigen_rtl_expr *left_rtl;
			const pigen_rtl_expr *right_rtl;
			const pigen_rtl_type *lt;
			const pigen_rtl_type *rt;
			pigen_source_span span;
			pigen_source_span optok;
			const char *optext;
			size_t oplen;

			optok = pigen_origin_spelling_span(expanded,
				expanded->tokens[best_at].origin);
			optext = pigen_source_span_text(sources, optok, &oplen);
			if (stage_operator_binary(optext, oplen, &op) < 0)
				return 0;
			if (!stage_expr_build(sources, lowering, rtl_module, expanded,
				first, best_at - 1, fields, field_count, &left))
				return 0;
			if (!stage_expr_build(sources, lowering, rtl_module, expanded,
				best_at + 1, last, fields, field_count, &right))
				return 0;
			left_rtl = pigen_rtl_expr_get(rtl, left);
			right_rtl = pigen_rtl_expr_get(rtl, right);
			lt = left_rtl ? pigen_rtl_type_get(rtl, left_rtl->type) : NULL;
			rt = right_rtl ? pigen_rtl_type_get(rtl, right_rtl->type) : NULL;
			if (!lt || !rt) return 0;
			left_width = (size_t)lt->width;
			right_width = (size_t)rt->width;
			left_data = pigen_data_type_sized_logic(sem, left_width,
				PIGEN_SIGN_UNSIGNED);
			right_data = pigen_data_type_sized_logic(sem, right_width,
				PIGEN_SIGN_UNSIGNED);
			if (left_data.index == PIGEN_INVALID_ID ||
				right_data.index == PIGEN_INVALID_ID)
				return 0;
			if (!pigen_data_type_resolve_binary_operation(sem, op,
				left_data, right_data, &resolution))
				return 0;
			result_type = pigen_lower_rtl_type(lowering,
				resolution.operation.result_data_type);
			if (result_type.index == PIGEN_INVALID_ID) return 0;
			span = pigen_origin_spelling_span(expanded,
				expanded->tokens[best_at].origin);
			*out = pigen_rtl_expr_add_binary(rtl, result_type, &resolution,
				left, right, span);
			return (*out).index != PIGEN_INVALID_ID;
		}
	}
}

/* Splits a stage item (`field <= expression;`) into its destination field and
 * the token range of its value expression. The destination is the first
 * identifier token; the value is the tokens after the first top-level `<=`
 * (through the last token before the terminating `;`/`end`). Fills *dest with
 * the destination's field index (-1 when the destination names no pipeline
 * field) and fills the value's inclusive token range. Returns 1 on success. */
static int stage_item_tokens(const pigen_source_manager *sources,
	const pigen_expanded_source *expanded, pigen_source_span item,
	const field_ref *fields, size_t field_count, int *dest,
	size_t *first, size_t *last)
{
	const pigen_expanded_token *tokens = expanded->tokens;
	size_t count = expanded->token_count;
	size_t i;
	size_t dest_at = (size_t)-1;
	size_t le_at = (size_t)-1;

	*dest = -1;
	for (i = 0; i < count; i++)
	{
		pigen_source_span span = pigen_origin_spelling_span(expanded,
			tokens[i].origin);

		if (span_outside(span, item)) continue;
		if (tokens[i].kind == PIGEN_TOKEN_IDENTIFIER &&
			dest_at == (size_t)-1)
			dest_at = i;
	}
	if (dest_at == (size_t)-1) return 0;
	{
		pigen_source_span span = pigen_origin_spelling_span(expanded,
			tokens[dest_at].origin);
		const char *text;
		size_t length;
		size_t k;

		text = pigen_source_span_text(sources, span, &length);
		for (k = 0; k < field_count; k++)
		{
			pigen_source_span name = fields[k].name;
			const char *ntext;
			size_t nlen;

			if (span_invalid(name)) continue;
			ntext = pigen_source_span_text(sources, name, &nlen);
			if (text && ntext && length == nlen &&
				!memcmp(text, ntext, length))
			{
				*dest = (int)k;
				break;
			}
		}
	}
	for (i = 0; i < count; i++)
	{
		pigen_source_span span = pigen_origin_spelling_span(expanded,
			tokens[i].origin);
		const char *text;
		size_t length;

		if (span_outside(span, item)) continue;
		if (tokens[i].kind != PIGEN_TOKEN_SYMBOL) continue;
		text = pigen_source_span_text(sources, span, &length);
		if (text && length == 2 && !memcmp(text, "<=", 2))
		{
			le_at = i;
			break;
		}
	}
	if (le_at == (size_t)-1) return 0;
	*first = le_at + 1;
	/* The value runs from the token after `<=` to the last non-terminator
	 * token in the item. */
	*last = le_at;
	for (i = le_at + 1; i < count; i++)
	{
		pigen_source_span span = pigen_origin_spelling_span(expanded,
			tokens[i].origin);
		const char *text;
		size_t length;

		if (span_outside(span, item)) break;
		if (tokens[i].kind == PIGEN_TOKEN_SYMBOL)
		{
			text = pigen_source_span_text(sources, span, &length);
			if (text && length == 1 && text[0] == ';') break;
		}
		*last = i;
	}
	if (*last < *first) return 0;
	return 1;
}

/* The source span of the value a pipeline field carries across the boundary
 * into the outgoing packet: the field's last stage write, with a pass-through
 * (a write whose value is a single read of another pipeline field) resolved
 * transitively to that field's own carried span, so a computed value that flows
 * through a pass-through stage still renders on the yield (the cross-boundary
 * contract). The carried span is the source span of the computing stage's value
 * expression (its full token range), so a yield member that carries it publishes
 * a member object whose provenance is that span - the terminal renderer spells
 * the member from the verbatim computed text (the `+` and its operands present).
 * A write that is not a pass-through (a computed expression) yields its own
 * value span. A write that reads an external signal or the field itself yields
 * INVALID_SPAN, and the yield member publishes its plain field object (the
 * emitted-member-order contract: the member renders the field name). */
static pigen_source_span carry_span_for_field(
	const pigen_source_manager *sources, const pigen_rtl_model *rtl,
	const field_ref *fields, size_t field_count,
	const pigen_rtl_expr_id *stage_value, const size_t *stage_value_first,
	const size_t *stage_value_last, const pigen_expanded_source *expanded,
	const int *field_last_writer, int field, int depth)
{
	int writer;
	pigen_rtl_expr_id e;
	const pigen_rtl_expr *ex;

	if (depth > (int)field_count)
		return INVALID_SPAN;
	writer = field_last_writer[field];
	if (writer < 0)
		return INVALID_SPAN;
	e = stage_value[writer];
	if (e.index == PIGEN_INVALID_ID)
		return INVALID_SPAN;
	ex = pigen_rtl_expr_get(rtl, e);
	if (!ex)
		return INVALID_SPAN;
	if (ex->kind != PIGEN_RTL_EXPR_OBJECT)
	{
		/* A computed value: carry the computing stage's value span (its full
		 * token range), the span the member object renders from. */
		pigen_source_span first_span =
			pigen_origin_spelling_span(expanded,
				expanded->tokens[stage_value_first[writer]].origin);
		pigen_source_span last_span =
			pigen_origin_spelling_span(expanded,
				expanded->tokens[stage_value_last[writer]].origin);

		if (span_invalid(first_span) || span_invalid(last_span))
			return INVALID_SPAN;
		return (pigen_source_span){first_span.source, first_span.start,
			last_span.end};
	}
	{
		size_t i;

		for (i = 0; i < field_count; i++)
		{
			if (!spans_equal(sources, ex->origin, fields[i].name)) continue;
			if ((int)i == field)
				return INVALID_SPAN;
			return carry_span_for_field(sources, rtl, fields, field_count,
				stage_value, stage_value_first, stage_value_last, expanded,
				field_last_writer, (int)i, depth + 1);
		}
	}
	/* A read of an external signal: the member renders the field name. */
	return INVALID_SPAN;
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
	pigen_rtl_expr_id stage_value[PIGEN_PIPELINE_MAX];
	int field_last_writer[PIGEN_PIPELINE_MAX];
	size_t stage_value_first[PIGEN_PIPELINE_MAX];
	size_t stage_value_last[PIGEN_PIPELINE_MAX];
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

	/* The 1-bit boolean type and the guard: the complete enclosing procedural
	 * guard that enables the pipeline (how/to/reset/and/enable/a/pipeline.md).
	 * The guard is the enclosing if statement's condition expression, lowered
	 * through the common RTL model as a 1-bit expression (a name object, a
	 * logical-and/or binary, or a constant when the condition is one); when the
	 * pipeline has no enclosing if the guard is the constant 1 (the pipeline is
	 * unconditionally live). The guard gates the pipeline's ready/valid
	 * handshake equations. */
	{
		pigen_rtl_type bit1 = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
			PIGEN_DATA_TYPE_STATE_FOUR, 1, INVALID_ID(pigen_rtl_expr_id),
			INVALID_SPAN, NULL, 0};
		const pigen_syntax_node *if_node;
		size_t cond_first;
		size_t cond_last;

		field_type = pigen_rtl_type_intern(rtl, &bit1);
		if (field_type.index == PIGEN_INVALID_ID) return -1;
		one = pigen_rtl_expr_add_integer(rtl, field_type, 1, INVALID_SPAN);
		if (one.index == PIGEN_INVALID_ID) return -1;
		guard = one;
		if_node = pipeline_enclosing_if(pipeline, syntax);
		if (if_node &&
			guard_condition_range(sources, expanded, if_node, &cond_first,
				&cond_last) &&
			cond_first <= cond_last)
		{
			/* The guard is the enclosing if's condition expression (the
			 * complete enclosing procedural guard). A condition that cannot be
			 * lowered keeps the constant-1 guard. */
			if (!guard_expr_build(sources, lowering, rtl_module, expanded,
				cond_first, cond_last, &guard))
				guard = one;
		}
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

	/* The per-stage computed next-value expressions, built before the yield
	 * write and published as the per-stage next-value equations (each also
	 * drives the outgoing-packet object). For each stage in source order, its
	 * `<=` write lowers its right-hand side to a structural RTL expression over
	 * the immutable incoming fields (the stage law already validated the
	 * source, so a write that cannot be lowered is a builder failure at the
	 * original source span). The yield write itself is the concatenation of the
	 * members' field objects, spelled by each field's declaration span. */
	{
		size_t i, j;

		for (j = 0; j < field_count; j++)
			field_last_writer[j] = -1;
		for (i = 0; i < stage_count; i++)
		{
			const pigen_syntax_node *stage = stage_nodes[i].node;
			pigen_syntax_id child;

			stage_value[i] = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			stage_value_first[i] = 0;
			stage_value_last[i] = 0;
			for (child = stage->first_child; child.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *item = pipeline_child(pipeline, syntax,
					child);

				if (item && item->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
				{
					int dest_field;
					size_t value_first;
					size_t value_last;
					pigen_rtl_expr_id value;

					if (!stage_item_tokens(sources, expanded,
						item->location.source_span, fields, field_count,
						&dest_field, &value_first, &value_last))
						return -1;
					if (!stage_expr_build(sources, lowering, rtl_module,
						expanded, value_first, value_last, fields,
						field_count, &value))
						return -1;
					/* The stage's next value is the computation of its last
					 * `<=` (a validated stage writes at most one field per
					 * cycle); record the field it drives and the value's token
					 * range so the yield member can carry the transitive
					 * computation's source span across the boundary. */
					stage_value[i] = value;
					stage_value_first[i] = value_first;
					stage_value_last[i] = value_last;
					if (dest_field >= 0 && dest_field < (int)field_count)
						field_last_writer[dest_field] = (int)i;
				}
				child = item ? item->next_sibling :
					INVALID_ID(pigen_syntax_id);
			}
		}

		/* The outgoing packet's value: the concatenation of the yield members,
		 * in source order (the first yielded field the MSB, the last the LSB).
		 * A member whose field carries a computed value across the boundary
		 * publishes that carried computation (a pass-through is resolved
		 * transitively, so the computed expression still renders on the yield,
		 * the cross-boundary contract); a member that carries nothing
		 * (no stage write, an external read, or a self-read) publishes its
		 * plain field object, named by the field's provenance declaration span
		 * so the terminal renderer spells the member name (the emitted-member-
		 * order contract). */
		{
			pigen_rtl_expr_id children[PIGEN_PIPELINE_MAX];
			pigen_rtl_expr_id concat;

			for (i = 0; i < yield_count; i++)
			{
				int member_field = -1;
				pigen_source_span name_span = yield[i].name;
				pigen_source_span carry = INVALID_SPAN;
				pigen_source_span member_origin;
				pigen_rtl_type descriptor = (pigen_rtl_type){
					PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR,
					yield[i].width, INVALID_ID(pigen_rtl_expr_id),
					INVALID_SPAN, NULL, 0};
				pigen_rtl_object_id member;
				pigen_rtl_type_id member_type;

				for (j = 0; j < field_count; j++)
					if (spans_equal(sources, yield[i].name, fields[j].name))
					{
						member_field = (int)j;
						name_span = fields[j].name;
						break;
					}
				if (member_field >= 0)
					carry = carry_span_for_field(sources, rtl, fields,
						field_count, stage_value, stage_value_first,
						stage_value_last, expanded, field_last_writer,
						member_field, 0);
				/* A member that carries a computed value across the boundary
				 * is proven from the carried value's source span (the
				 * computing stage's full value expression), so the terminal
				 * renderer spells the member from the verbatim computed text -
				 * the `+` and its operands present (the cross-boundary
				 * contract). A member that carries nothing (no stage write, an
				 * external read, or a self-read) is proven from the field's
				 * declaration span, so the renderer spells the member name
				 * (the emitted-member-order contract). The member object is
				 * synthetic (a pipeline field, not a semantic signal), so its
				 * semantic_signal is invalid. */
				member_origin = span_invalid(carry) ? name_span : carry;
				member_type = pigen_rtl_type_intern(rtl, &descriptor);
				if (member_type.index == PIGEN_INVALID_ID) return -1;
				member = pigen_rtl_object_add_with_owner(rtl, rtl_module,
					PIGEN_RTL_OBJECT_VARIABLE, member_type,
					PIGEN_SEMANTIC_INTERNAL, INVALID_ID(pigen_signal_id),
					member_origin);
				if (member.index == PIGEN_INVALID_ID) return -1;
				children[i] = pigen_rtl_expr_add_object(rtl, member_type,
					member, member_origin);
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
	}

	/* The buf stage storage and the per-stage next-value equations. For each
	 * stage boundary (there are stage_count - 1 of them) publish one
	 * one-entry buf primitive instance; for each stage publish its
	 * computed next-value equation over the immutable incoming fields the
	 * stage is handed - the stage's `<=` right-hand side lowered to a
	 * structural expression (how/do/pipeline/stage/reads/work.md), never a
	 * constant stand-in. */
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
		 * outgoing value, the computation of its `<=` right-hand side over the
		 * immutable incoming fields the stage is handed by the earlier stages
		 * (how/do/pipeline/stage/reads/work.md). A stage that writes no field
		 * (a storage-only stage) has no next value and publishes no equation. */
		if (stage_value[i].index != PIGEN_INVALID_ID)
		{
			if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
				packet_object, stage_value[i],
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
