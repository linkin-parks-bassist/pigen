#ifndef PIGEN_PIPELINE_IDENTITY_H
#define PIGEN_PIPELINE_IDENTITY_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"
#include "pigen/transfer_type.h"

/* The pipeline identifier identity owner: the owner seam that turns a parsed
 * pipeline (PIGEN_SYNTAX_PIPELINE) into a BUF packet identity. The default
 * entries use module scope; scoped entries use an explicit lexical scope.
 *
 * With the default entries, the identifier token names a module-scope signal.
 * This owner establishes that signal in the model: one
 * pigen_semantic_signal with transfer_type = PIGEN_TRANSFER_TYPE_BUF and
 * direction = PIGEN_SEMANTIC_OUTPUT, bound to a module-scope symbol (the
 * identifier), carrying a data_type that is the outgoing packet aggregate and
 * a span anchored at the identifier's original span. The established signal is
 * the pipeline's outgoing packet identity: the single buf output the pipeline
 * emits to its parent module.
 *
 * Ownership: the buf output packet's data type is the outgoing packet
 * aggregate, the concatenation of the data types of the pipeline's outgoing
 * fields in source order (the fields the final yield reads). The
 * pigen_pipeline_identifier.data_type field is that aggregate and this owner
 * owns where it comes from: it is built from the outgoing fields via the
 * data-type owner (pigen_data_type.h, pigen_data_type_concatenation) and is
 * the buf output signal's data_type. The exact outgoing-field / repartition /
 * stage-execution laws that select and order the outgoing fields are not part
 * of this seam; they are the next bounded stage and build on this identity.
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This seam owns the pipeline identifier identity on the structured path
 * (include/pigen/pipeline_identity.h + src/pipeline_identity.c); the
 * quarantined textual prototype's include/pigen/pipeline.h
 * (pigen_prepare_pipeline_models / pigen_finish_pipeline_models) is a
 * different owner and is not touched. */

/* The established identity of one pipeline's outgoing buf output packet.
 * signal is the BUF packet signal established in the requested scope;
 * symbol is its local symbol (the identifier);
 * data_type is the outgoing packet aggregate, the concatenation of the
 * outgoing field data types in source order and the buf output signal's
 * data_type; span is the original span anchored at the pipeline identifier.
 * syntax is the owning pipeline node. An invalid identity carries the
 * PIGEN_INVALID_ID sentinel in every id field and an empty span. */
typedef struct {
	pigen_signal_id signal;
	pigen_symbol_id symbol;
	pigen_data_type_id data_type;
	pigen_source_span span;
	pigen_syntax_id syntax;
} pigen_pipeline_identifier;

/* Establishes the pipeline identifier as a module-scope buf output signal in
 * the model. pipeline is the parsed pipeline node (PIGEN_SYNTAX_PIPELINE) and
 * its id; module is the resolved module the pipeline lives in; expanded is
 * the parsed expanded source the pipeline node was parsed from (the source
 * that resolves the identifier token's origin to its original spelling
 * span, as the syntax tree itself does through its expanded source); the
 * identifier token is pipeline->as.pipeline.name. On success it declares the
 * module-scope symbol for the identifier, builds the outgoing packet
 * aggregate data type (the concatenation of the outgoing field data types in
 * source order) and creates the buf output pigen_semantic_signal bound to
 * that symbol, then fills *identity. Returns 0 on success and -1 on error; a
 * failed call reports the error and the compile stops - it may leave partial
 * state behind, and no test asserts that a failure left the model unchanged.
 */
int pigen_pipeline_identifier_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity);

/* Recovers the established identity of a pipeline's outgoing buf output
 * packet from its parsed pipeline node (PIGEN_SYNTAX_PIPELINE), the
 * resolved module and the parsed expanded source (used to anchor the
 * identifier span at its original spelling span). Fills *identity with the
 * signal, symbol, outgoing packet aggregate data type and identifier span.
 * Returns 0 on success and -1 when the identity has not been established or
 * the arguments are invalid; a failed call reports the error and the
 * compile stops. */
int pigen_pipeline_identifier_of(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity);

/* Scoped production entries declare/recover the packet in an explicit module
 * descendant scope. Nested packets are INTERNAL; the module-scope wrappers
 * above retain their OUTPUT identity contract. Lookup requires a direct local
 * binding, and both entries reject scopes outside the owning module. */
int pigen_pipeline_identifier_establish_in_scope(pigen_semantic_model *model,
	pigen_syntax_id syntax,pigen_module_id module,pigen_scope_id scope,
	const pigen_syntax_node *pipeline,const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity);
int pigen_pipeline_identifier_of_in_scope(pigen_semantic_model *model,
	pigen_syntax_id syntax,pigen_module_id module,pigen_scope_id scope,
	const pigen_syntax_node *pipeline,const pigen_expanded_source *expanded,
	pigen_pipeline_identifier *identity);

#endif
