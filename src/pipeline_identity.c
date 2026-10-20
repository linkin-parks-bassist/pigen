#include "pigen/pipeline_identity.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* SKELETON: the pipeline identifier identity seam (establishing the pipeline
 * identifier as a module-scope buf output signal in the resolved semantic
 * model, with the outgoing packet aggregate as its data type) is filled in
 * by the implementation stage. Until then both entry points report -1 and
 * leave the identity in its invalid state (the PIGEN_INVALID_ID sentinel in
 * every id field and an empty span); a failed call reports the error and the
 * compile stops (how/should/compiler/builders/fail.md). */

int pigen_pipeline_identifier_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	pigen_pipeline_identifier *identity)
{
	(void)model;
	(void)syntax;
	(void)module;
	(void)pipeline;
	*identity = (pigen_pipeline_identifier){
		INVALID_ID(pigen_signal_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_data_type_id), INVALID_SPAN,
		INVALID_ID(pigen_syntax_id)
	};
	return -1;
}

int pigen_pipeline_identifier_of(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	pigen_pipeline_identifier *identity)
{
	(void)model;
	(void)syntax;
	(void)module;
	(void)pipeline;
	*identity = (pigen_pipeline_identifier){
		INVALID_ID(pigen_signal_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_data_type_id), INVALID_SPAN,
		INVALID_ID(pigen_syntax_id)
	};
	return -1;
}
