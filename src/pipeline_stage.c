#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/pipeline_stage.h"

#include "pigen/preprocess.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

#define PIGEN_STAGE_MAX_READS 64

/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* The node array a parsed pipeline lives in is flat: node i sits at
 * (pipeline - pipeline_id) + i. The owner seam carries the owning pipeline
 * node and the stage's own syntax id, so the array base is the pipeline node
 * minus the pipeline's id, and the stage node and every descendant are
 * recovered by offsetting that base - the sibling step-(a) owner does exactly
 * this (src/pipeline_identity.c) without the tree handle. */
static const pigen_syntax_node *stage_node(const pigen_syntax_node *pipeline,
	pigen_syntax_id pipeline_id, pigen_syntax_id child)
{
	const pigen_syntax_node *base = pipeline - pipeline_id.index;

	return base + child.index;
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
	const char *right_text;
	size_t left_length;
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
	pigen_syntax_id pipeline_id, const pigen_syntax_node *field,
	const pigen_expanded_source *expanded)
{
	const pigen_syntax_node *declarator;
	const pigen_expanded_token *token;

	if (!field || field->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
		return INVALID_SPAN;
	declarator = stage_node(pipeline, pipeline_id, field->first_child);
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

/* True when NAME (an original-spelling span) names a pipeline field - that is,
 * when it textually matches one of the field declarator name spans the
 * pipeline node declares. */
static int is_pipeline_field(const pigen_source_manager *sources,
	const pigen_syntax_node *pipeline, pigen_syntax_id pipeline_id,
	const pigen_expanded_source *expanded, pigen_source_span name)
{
	pigen_syntax_id child;

	if (span_invalid(name)) return 0;
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = stage_node(pipeline, pipeline_id, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			spans_equal(sources, name,
				field_name_span(pipeline, pipeline_id, node, expanded)))
			return 1;
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return 0;
}

/* The field node whose one declarator name is textually NAME, or NULL when no
 * field matches. */
static const pigen_syntax_node *field_node_for(
	const pigen_source_manager *sources, const pigen_syntax_node *pipeline,
	pigen_syntax_id pipeline_id, const pigen_expanded_source *expanded,
	pigen_source_span name)
{
	pigen_syntax_id child;

	if (span_invalid(name)) return NULL;
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = stage_node(pipeline, pipeline_id, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			spans_equal(sources, name,
				field_name_span(pipeline, pipeline_id, node, expanded)))
			return node;
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return NULL;
}

/* The original-spelling span of the first identifier token of SPAN. A
 * pipeline field or a consuming read is a plain single-token name, so the
 * destination of a `<=` is the item's first identifier token. */
static pigen_source_span first_identifier_in(
	const pigen_expanded_source *expanded, pigen_source_span span)
{
	size_t i;

	if (span_invalid(span)) return INVALID_SPAN;
	for (i = 0; i < expanded->token_count; i++)
	{
		const pigen_expanded_token *token = &expanded->tokens[i];
		pigen_source_span tok;

		if (token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		tok = pigen_origin_spelling_span(expanded, token->origin);
		if (tok.source.index == PIGEN_INVALID_ID) continue;
		if (tok.start < span.start || tok.end > span.end) continue;
		return tok;
	}
	return INVALID_SPAN;
}

/* Collects, into SPANS, the original-spelling name spans of every outgoing
 * field written by every stage that precedes STAGE_ID in source order (the
 * incoming-field set a later stage may read). Returns the count. A stage's
 * outgoing fields are the destinations of its nonblocking-assignment items,
 * each a pipeline field by the outgoing-visibility law. Bounded to
 * PIGEN_STAGE_MAX_READS. */
static size_t collect_produced_fields(
	const pigen_syntax_node *pipeline, pigen_syntax_id pipeline_id,
	const pigen_expanded_source *expanded, pigen_syntax_id stage_id,
	pigen_source_span *spans, size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = stage_node(pipeline, pipeline_id, child);

		if (node && node->kind == PIGEN_SYNTAX_STAGE)
		{
			pigen_syntax_id item;

			if (child.index == stage_id.index)
				break;
			for (item = node->first_child; item.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *assign = stage_node(pipeline,
					pipeline_id, item);

				if (assign &&
					assign->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
				{
					pigen_source_span destination = first_identifier_in(
						expanded, assign->location.source_span);

					if (!span_invalid(destination) && count < capacity)
						spans[count++] = destination;
				}
				item = assign ? assign->next_sibling :
					INVALID_ID(pigen_syntax_id);
			}
		}
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	return count;
}

int pigen_pipeline_stage_is_invalid(const pigen_pipeline_stage *stage)
{
	return stage &&
		stage->syntax.index == PIGEN_INVALID_ID &&
		stage->first_write.index == PIGEN_INVALID_ID &&
		stage->first_read.index == PIGEN_INVALID_ID &&
		stage->write_count == 0 &&
		stage->read_count == 0 &&
		stage->in_bits == 0 &&
		stage->out_bits == 0;
}

int pigen_pipeline_stage_laws(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_stage *out)
{
	const pigen_syntax_node *stage;
	const pigen_syntax_node *item;
	const pigen_source_manager *sources;
	pigen_syntax_id pipeline_id;
	pigen_syntax_id child;
	pigen_syntax_id first_write;
	pigen_syntax_id first_read;
	size_t write_count;
	size_t read_count;
	size_t in_bits;
	size_t out_bits;
	int is_first_stage;
	int saw_item;
	pigen_source_span produced[PIGEN_STAGE_MAX_READS];
	size_t produced_count;

	if (out) {
		*out = (pigen_pipeline_stage){
			INVALID_ID(pigen_syntax_id),
			INVALID_ID(pigen_syntax_id), 0,
			INVALID_ID(pigen_syntax_id), 0, 0, 0
		};
	}
	if (!model || !pipeline || !expanded ||
		syntax.index == PIGEN_INVALID_ID ||
		pipeline->kind != PIGEN_SYNTAX_PIPELINE)
		return -1;
	sources = model->sources;
	/* The pipeline node's own id: its first child is added immediately after
	 * it in the flat node array (a pipeline body is at least one field or
	 * stage, so it always has a first child), so the pipeline id is the
	 * first child's id minus one. */
	if (pipeline->first_child.index == PIGEN_INVALID_ID) return -1;
	pipeline_id = (pigen_syntax_id){pipeline->first_child.index - 1};
	stage = stage_node(pipeline, pipeline_id, syntax);
	if (!stage || stage->kind != PIGEN_SYNTAX_STAGE) return -1;

	first_write = INVALID_ID(pigen_syntax_id);
	first_read = INVALID_ID(pigen_syntax_id);
	write_count = 0;
	read_count = 0;
	in_bits = 0;
	out_bits = 0;
	(void)module;

	/* The stage is the first stage of the pipeline when no stage sibling
	 * precedes it in source order. */
	is_first_stage = 1;
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = stage_node(pipeline, pipeline_id, child);

		if (node && node->kind == PIGEN_SYNTAX_STAGE) {
			if (child.index == syntax.index) break;
			is_first_stage = 0;
		}
		child = node ? node->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	produced_count = 0;
	if (!is_first_stage)
		produced_count = collect_produced_fields(pipeline,
			pipeline_id, expanded, syntax, produced,
			PIGEN_STAGE_MAX_READS);

	/* Walk the stage's items in source order. A nonblocking assignment is an
	 * outgoing field write (its destination must be a pipeline field) whose
	 * value is the read set; a stage-local declaration is an internal signal,
	 * not a write. */
	saw_item = 0;
	for (child = stage->first_child; child.index != PIGEN_INVALID_ID; )
	{
		item = stage_node(pipeline, pipeline_id, child);
		if (item && item->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
		{
			pigen_source_span destination;
			const pigen_syntax_node *field;

			saw_item = 1;
			write_count++;
			if (first_write.index == PIGEN_INVALID_ID)
				first_write = child;

			/* The destination must be a pipeline field (an outgoing
			 * field). A <= whose destination is not a field (a module
			 * input, a stage-local wire) is diagnosed. */
			destination = first_identifier_in(expanded,
				item->location.source_span);
			field = field_node_for(sources, pipeline, pipeline_id, expanded,
				destination);
			if (span_invalid(destination) || !field)
				return -1;
			out_bits += field_width(sources, field);

			/* The value's identifier reads split into incoming pipeline
			 * fields (counted into in_bits) and external / module-scope
			 * reads (counted into read_count; the first-stage law checks
			 * them). A read is the identifier tokens of the value, i.e.
			 * every identifier in the item span other than the
			 * destination's first. */
			{
				size_t i;

				for (i = 0; i < expanded->token_count; i++)
				{
					const pigen_expanded_token *token =
						&expanded->tokens[i];
					pigen_source_span tok;
					const char *text;
					size_t length;

					if (token->kind != PIGEN_TOKEN_IDENTIFIER) continue;
					tok = pigen_origin_spelling_span(expanded,
						token->origin);
					if (tok.source.index == PIGEN_INVALID_ID) continue;
					if (tok.start < item->location.source_span.start ||
						tok.end > item->location.source_span.end)
						continue;
					if (spans_equal(sources, tok, destination))
						continue; /* the destination itself */
					text = pigen_source_span_text(sources, tok,
						&length);
					if (!text) continue;
					if (is_pipeline_field(sources, pipeline, pipeline_id,
						expanded, tok))
					{
						const pigen_syntax_node *read_field =
							field_node_for(sources, pipeline,
								pipeline_id, expanded, tok);
						size_t j;
						int produced_here = 0;

						/* an incoming field read */
						if (read_field)
							in_bits += field_width(sources,
								read_field);
						if (is_first_stage)
							return -1; /* first stage reading
							 * an uninitialized field */
						/* a later stage reads only the immutable
						 * incoming fields its earlier stages wrote */
						for (j = 0; j < produced_count; j++)
							if (spans_equal(sources, produced[j], tok))
								produced_here = 1;
						if (!produced_here)
							return -1;
					}
					else
					{
						/* a consuming external / module-scope
						 * read */
						read_count++;
						if (first_read.index == PIGEN_INVALID_ID)
							first_read = child;
					}
				}
			}
		}
		child = item ? item->next_sibling : INVALID_ID(pigen_syntax_id);
	}
	if (!saw_item) return -1;

	/* Every boundary between adjacent stages keeps an equal total $bits: a
	 * later stage's incoming packet width must equal its outgoing packet
	 * width (the chain neither widens nor narrows the carried packet). */
	if (!is_first_stage && in_bits != out_bits)
		return -1;

	*out = (pigen_pipeline_stage){
		{syntax.index},
		{first_write.index}, write_count,
		{first_read.index}, read_count,
		in_bits, out_bits
	};
	return 0;
}
