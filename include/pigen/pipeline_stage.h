#ifndef PIGEN_PIPELINE_STAGE_H
#define PIGEN_PIPELINE_STAGE_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"
#include "pigen/transfer_type.h"

/* Legacy stage-summary service for structural fixtures. Production pipelines
 * use stage-specific fields and atomic packet transfers in src/resolve.c.
 * The summary counts explicitly written/read fields and requires equal total
 * widths for its own repartition fixtures. That summary restriction does not
 * define production packet layout or validate runtime handshakes. */

/* One stage's execution-law view. syntax is the stage node's syntax id
 * (PIGEN_SYNTAX_STAGE); first_write / write_count are the stage's outgoing
 * field writes (the nonblocking `<=` items, the syntax ids of the stage's
 * nonblocking-assignment nodes in source order); first_read / read_count are
 * the stage's consuming external/module-scope reads (the syntax ids of the
 * read nodes in source order); in_bits is the total $bits of the incoming
 * fields the stage reads and out_bits is the total $bits of the outgoing
 * fields the stage writes. An invalid view carries the PIGEN_INVALID_ID
 * sentinel in syntax, first_write and first_read, a write_count of 0, a
 * read_count of 0, and in_bits / out_bits of 0. */
typedef struct {
	pigen_syntax_id syntax;
	pigen_syntax_id first_write;
	size_t write_count;
	pigen_syntax_id first_read;
	size_t read_count;
	size_t in_bits;
	size_t out_bits;
} pigen_pipeline_stage;

/* Tests whether *stage is an invalid stage execution-law view (the
 * PIGEN_INVALID_ID sentinel in its syntax id and its first_write /
 * first_read ids, a write_count of 0, a read_count of 0, and in_bits /
 * out_bits of 0). Returns 1 when the view is invalid and 0 otherwise. */
int pigen_pipeline_stage_is_invalid(const pigen_pipeline_stage *stage);

/* Establish a source-level stage summary, validating definite initialization,
 * field destinations and equal incoming/write summary widths. On success,
 * populate out and return 0. On failure return -1 with a diagnostic; partial
 * model state may remain. Temporal behavior requires independent RTL tests. */
int pigen_pipeline_stage_laws(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_stage *out);

#endif
