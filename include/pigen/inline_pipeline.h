#ifndef PIGEN_INLINE_PIPELINE_H
#define PIGEN_INLINE_PIPELINE_H

#include <stddef.h>

#include "pigen/model.h"
#include "pigen/procedural.h"

typedef struct pigen_inline_pipelines pigen_inline_pipelines;

/* Parse inline pipeline blocks, replace each with its input transfer, and
 * rewrite exported output names to packet projections. */
char *pigen_prepare_inline_pipelines(const char *source, size_t length,
	size_t *output_length, pigen_inline_pipelines **pipelines);

/* Emit parent-module declarations and elastic stage RTL after procedural
 * guards/domains have been recovered from the prepared source. */
char *pigen_finish_inline_pipelines(const char *source, size_t length,
	const pigen_procedural_ast *ast, pigen_inline_pipelines *pipelines,
	size_t *output_length);

void pigen_free_inline_pipelines(pigen_inline_pipelines *pipelines);

#endif
