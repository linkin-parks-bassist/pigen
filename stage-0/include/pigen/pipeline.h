#ifndef PIGEN_PIPELINE_H
#define PIGEN_PIPELINE_H

#include <stddef.h>

#include "pigen/model.h"
#include "pigen/procedural.h"

typedef struct pigen_pipelines pigen_pipelines;

/* Parse pipeline declarations and elaborate their ingress, state, and output. */
char *pigen_prepare_pipeline_models(const char *source, size_t length,
	size_t *output_length, pigen_pipelines **pipelines);

/* Emit parent-module declarations and elastic stage RTL after procedural
 * guards/domains have been recovered from the prepared source. */
char *pigen_finish_pipeline_models(const char *source, size_t length,
	const pigen_procedural_ast *ast, pigen_pipelines *pipelines,
	size_t *output_length);

void pigen_free_pipeline_models(pigen_pipelines *pipelines);

#endif
