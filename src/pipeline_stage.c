#include "pigen/pipeline_stage.h"

/* Tests whether *stage is an invalid stage execution-law view. */
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

/* Establishes one stage's execution-law view and validates the stage
 * execution laws for that stage. SKELETON: returns -1 and leaves *out
 * invalid until the implementation stage. */
int pigen_pipeline_stage_laws(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_stage *out)
{
	(void)model;
	(void)syntax;
	(void)module;
	(void)pipeline;
	(void)expanded;

	if (out) {
		*out = (pigen_pipeline_stage){
			{PIGEN_INVALID_ID}, {PIGEN_INVALID_ID}, 0,
			{PIGEN_INVALID_ID}, 0, 0, 0
		};
	}
	return -1;
}
