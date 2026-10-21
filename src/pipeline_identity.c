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
 * data types of the pipeline's outgoing fields. This bounded step carries no
 * separate stage-execution or repartition law, so every pipeline field is an
 * outgoing field. The aggregate's packed width is the sum of the fields'
 * packed widths; that width, not a fragile data-type-id identity, is the
 * discriminator the contract pins. */

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

	/* Sum the outgoing fields' packed widths in source order; every pipeline
	 * field is an outgoing field in this bounded step. */
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pipeline_child(pipeline, syntax, child);

		if (node && node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION)
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
