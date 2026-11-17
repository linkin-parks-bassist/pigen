#ifndef PIGEN_PIPELINE_LOWER_H
#define PIGEN_PIPELINE_LOWER_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/preprocess.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* Legacy structural scaffold used by pipeline-lower-test. Production pipelines
 * resolve stage versions and packet transfers in src/resolve.c.
 *
 * This API validates syntax/stage summaries and publishes RTL shape/provenance
 * records plus yielded-member ordering. Its stage-value equations are zero
 * placeholders; it does not implement functional stage expressions or runtime
 * elastic scheduling. Callers must establish the pipeline output identity first.
 *
 * Returns 0 on success or -1 with a diagnostic. Failure may leave partial model
 * state, which callers must discard. */
int pigen_pipeline_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded);

#endif
