#include <stdlib.h>
#include <string.h>

#include "pigen/pipeline_identity.h"

#include "pigen/preprocess.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* The outgoing packet aggregate is the concatenation, in source order, of the
 * data types of the pipeline's outgoing fields: the fields the final yield
 * expression reads (what/is/the/spec.md "yield exposes final outgoing value";
 * what/does/pipeline/yield/mean.md "evaluates its outgoing fields"). A
 * pipeline may declare more fields than the yield reads; only the yielded
 * fields make up the outgoing packet. The aggregate's packed width is the sum
 * of the yielded fields' packed widths; that width, not a fragile data-type-id
 * identity, is the discriminator the contract pins. Member order of the
 * concatenation is a separate concern pinned by the emitted SV, so this step
 * selects the field set by name and sums widths in source order. */

/* The syntax node at CHILD within the tree that owns PIPELINE. The pipeline
 * node is an element of that tree's contiguous node array, at the position
 * given by its own syntax id (the caller passes the pipeline's id through
 * syntax), so the array base is the pipeline node minus its id and a child
 * node is the base plus the child's id. This recovers the child nodes without
 * the tree handle, which the owner seam does not carry. */
static const pigen_syntax_node *pipeline_child(const pigen_syntax_node *pipeline,
	pigen_syntax_id syntax, pigen_syntax_id child)
{
	const pigen_syntax_node *base = pipeline - syntax.index;

	return base + child.index;
}

/* A field's declarator name span: the single name each pipeline field
 * declaration carries (the parser rejects a field declaring more than one
 * name), recovered from the field's one signal-declarator child. The name
 * token's original spelling span is resolved through the expanded source's
 * token origins, which do not require the expanded source's (possibly
 * reclaimed) source manager. */
static pigen_source_span pipeline_field_name_span(const pigen_syntax_node *pipeline,
	pigen_syntax_id syntax, const pigen_syntax_node *field,
	const pigen_expanded_source *expanded)
{
	const pigen_syntax_node *declarator =
		pipeline_child(pipeline, syntax, field->first_child);
	const pigen_expanded_token *token;

	if (!declarator ||
		declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
		return INVALID_SPAN;
	token = pigen_expanded_token_get(expanded,
		declarator->as.signal_declarator.name);
	return token ? pigen_origin_spelling_span(expanded, token->origin) :
		INVALID_SPAN;
}

/* The byte span of the final yield's value expression. The yield is the
 * pipeline's last item (the parser requires it to follow the stages), so its
 * value runs from just after the `yield` keyword token to just before the
 * `endpipeline` keyword token. Both are ordinary identifier tokens whose
 * spelling spans are recovered through the expanded source's token origins and
 * whose text is read from the model's source manager (the expanded source's
 * own source manager may already have been reclaimed by the time this runs).
 * A missing or misplaced keyword (which the parser would have rejected for a
 * well-formed pipeline) leaves the span empty. */
static pigen_source_span pipeline_yield_value_span(
	pigen_semantic_model *model, const pigen_expanded_source *expanded,
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
		text = pigen_source_span_text(model->sources, tok, &length);
		if (!text) continue;
		if (!saw_yield)
		{
			if (length == 5 && !memcmp(text, "yield", 5))
			{
				saw_yield = 1;
				span = (pigen_source_span){body.source, tok.end,
					body.end};
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

/* True when the final yield value expression reads NAME: an identifier token
 * of that exact spelling lies within the yield value span. The yield references
 * the declared pipeline fields by name (each field is exactly one name), so
 * spelling equality selects the yielded field set; the yield subspan excludes
 * the field declarations and the stages, so only the yielded fields match. */
static int pipeline_yield_reads(pigen_semantic_model *model,
	const pigen_expanded_source *expanded, const pigen_source_span value,
	const pigen_source_span name)
{
	size_t name_length, i;
	const char *name_text;

	name_text = pigen_source_span_text(model->sources, name, &name_length);
	if (!name_text || name_length == 0) return 0;
	for (i = 0; i < expanded->token_count; i++)
	{
		const pigen_expanded_token *tok = &expanded->tokens[i];
		pigen_source_span span = pigen_origin_spelling_span(expanded,
			tok->origin);
		size_t length;
		const char *text;

		if (tok->kind != PIGEN_TOKEN_IDENTIFIER) continue;
		if (span.start < value.start || span.end > value.end) continue;
		text = pigen_source_span_text(model->sources, span, &length);
		if (text && length == name_length &&
			!memcmp(text, name_text, name_length))
			return 1;
	}
	return 0;
}

/* The pipeline identifier token (pipeline->as.pipeline.name) resolved to its
 * original spelling span through the expanded source's token origins, exactly
 * as the syntax tree anchors its own tokens. A missing or unresolvable token
 * leaves the span empty (the invalid source id with no range), which the
 * callers treat as a rejected argument. */
static pigen_source_span pipeline_name_span(const pigen_expanded_source *expanded,
	const pigen_syntax_node *pipeline)
{
	const pigen_expanded_token *token = pigen_expanded_token_get(
		expanded, pipeline->as.pipeline.name);

	return token ? pigen_origin_spelling_span(expanded, token->origin) :
		INVALID_SPAN;
}

/* The packed width of one pipeline field, read from its own declaration text
 * (the [ high : low ] range within the field's node span). Returns 0 when the
 * field carries no such range (a scalar field: one bit). The field's node span
 * is the whole signal declaration (for example "bit [7:0] a"); the range is
 * the first "[ ... ]" that lies wholly inside that span. */
static size_t pipeline_field_width(pigen_semantic_model *model,
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

	text = pigen_source_span_text(model->sources, field->location.source_span,
		&length);
	if (!text) return 0;
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

/* Establishes the pipeline identifier as a module-scope buf output signal in
 * the model. The identifier becomes one pigen_semantic_signal: transfer_type
 * buf, direction a module output, bound to a module-scope symbol named at the
 * identifier's original span, carrying the outgoing packet aggregate (the
 * concatenation of the outgoing field data types in source order). A failed
 * call reports the error and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */
int pigen_pipeline_identifier_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity)
{
	const pigen_semantic_module *mod;
	const pigen_scope *module_scope;
	pigen_source_span name;
	pigen_source_span value;
	pigen_data_type_id aggregate;
	pigen_symbol_id symbol;
	pigen_declare_result declared;
	pigen_signal_id signal;
	size_t total = 0;
	pigen_syntax_id child;

	*identity = (pigen_pipeline_identifier){
		INVALID_ID(pigen_signal_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_data_type_id), INVALID_SPAN,
		INVALID_ID(pigen_syntax_id)
	};
	if (!model || !identity || !expanded || !pipeline ||
		pipeline->kind != PIGEN_SYNTAX_PIPELINE)
		return -1;
	mod = pigen_module_get(model, module);
	if (!mod) return -1;
	module_scope = pigen_scope_get(model, mod->scope);
	if (!module_scope) return -1;
	name = pipeline_name_span(expanded, pipeline);
	if (name.source.index == PIGEN_INVALID_ID || name.start == name.end)
		return -1;
	value = pipeline_yield_value_span(model, expanded, pipeline);
	if (value.source.index == PIGEN_INVALID_ID || value.start >= value.end)
		return -1;

	/* Sum, in source order, the packed widths of only the fields the final
	 * yield reads; a pipeline may declare more fields than the yield
	 * exposes, and the outgoing packet carries just the yielded ones. */
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pipeline_child(pipeline, syntax, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION &&
			pipeline_yield_reads(model, expanded, value,
				pipeline_field_name_span(pipeline, syntax, node, expanded)))
			total += pipeline_field_width(model, node);
		child = node ? node->next_sibling : (pigen_syntax_id){PIGEN_INVALID_ID};
	}
	if (total == 0) return -1;
	aggregate = pigen_data_type_sized_logic(model, total, PIGEN_SIGN_UNSIGNED);
	if (aggregate.index == PIGEN_INVALID_ID) return -1;

	/* The identifier is a module-scope signal: declare the module-scope symbol
	 * (name anchored at the identifier's original span; the declaration span is
	 * the same span, so it contains the name span) and add the buf output
	 * signal bound to it. */
	declared = pigen_symbol_declare(model, mod->scope, PIGEN_SYMBOL_SIGNAL,
		aggregate, name, name, &symbol, NULL);
	if (declared != PIGEN_DECLARE_OK) return -1;
	signal = pigen_signal_add(model, syntax, module, symbol, aggregate,
		pigen_semantic_scalar_shape(model), INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_BUF, PIGEN_SEMANTIC_OUTPUT, name);
	if (signal.index == PIGEN_INVALID_ID) return -1;

	*identity = (pigen_pipeline_identifier){
		signal, symbol, aggregate, name, syntax
	};
	return 0;
}

/* Recovers the established identity of a pipeline's outgoing buf output
 * packet from its parsed pipeline node, the resolved module and the parsed
 * expanded source (used to anchor the identifier span at its original spelling
 * span). The identifier is looked up in the module scope by its original name
 * span and the bound signal is recovered through the symbol's typed binding.
 * Returns 0 on success and -1 when the identity has not been established or
 * the arguments are invalid; a failed call reports the error and the compile
 * stops (how/should/compiler/builders/fail.md). */
int pigen_pipeline_identifier_of(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity)
{
	const pigen_semantic_module *mod;
	pigen_symbol_id symbol;
	const pigen_symbol *known_symbol;
	pigen_signal_id signal;
	pigen_source_span name;
	const pigen_semantic_signal *sig;

	*identity = (pigen_pipeline_identifier){
		INVALID_ID(pigen_signal_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_data_type_id), INVALID_SPAN,
		INVALID_ID(pigen_syntax_id)
	};
	if (!model || !identity || !expanded || !pipeline ||
		pipeline->kind != PIGEN_SYNTAX_PIPELINE)
		return -1;
	mod = pigen_module_get(model, module);
	if (!mod) return -1;
	name = pipeline_name_span(expanded, pipeline);
	if (name.source.index == PIGEN_INVALID_ID || name.start == name.end)
		return -1;
	symbol = pigen_symbol_lookup(model, mod->scope, name);
	known_symbol = pigen_symbol_get(model, symbol);
	if (!known_symbol || known_symbol->kind != PIGEN_SYMBOL_SIGNAL) return -1;
	signal = pigen_symbol_signal(model, symbol);
	if (signal.index == PIGEN_INVALID_ID) return -1;
	sig = pigen_signal_get(model, signal);
	if (!sig || sig->module.index != module.index) return -1;
	*identity = (pigen_pipeline_identifier){
		signal, symbol, sig->data_type, name, syntax
	};
	return 0;
}
