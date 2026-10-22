#ifndef PIGEN_PIPELINE_STAGE_H
#define PIGEN_PIPELINE_STAGE_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"
#include "pigen/transfer_type.h"

/* The pipeline stage execution-laws owner: the owner seam that turns one
 * stage of a parsed pipeline (a PIGEN_SYNTAX_STAGE child of the
 * PIGEN_SYNTAX_PIPELINE node) into that stage's execution-law view in the
 * resolved semantic model.
 *
 * A pipeline is a chain of stages. Each stage is a procedural block of
 * nonblocking assignments (the `<=` items) that reads the pipeline fields
 * the earlier stages established (its incoming fields), writes the fields
 * it produces (its outgoing fields, the `<=` items), and may consume
 * external or module-scope signals. The stage execution laws are the
 * invariants that make that chain a single atomic transfer from the
 * pipeline's first stage to its final yield:
 *
 *   - the first stage cannot read a pipeline field that is not initialized
 *     (it may only read external or module-scope signals, which are already
 *     live);
 *   - a later stage reads only the immutable incoming fields it is handed by
 *     the earlier stages; it does not read a field the chain has not written;
 *   - each stage's `<=` writes exactly one outgoing field, and that field is
 *     visible in the following stage;
 *   - every boundary between adjacent stages keeps an equal total $bits
 *     (the incoming packet's $bits equal the outgoing packet's $bits, so the
 *     chain neither widens nor narrows the packet it carries);
 *   - an external consuming read is atomic under the full guard;
 *   - at most one item per cycle.
 *
 * This owner is the SIBLING owner of the landed step-(a) seam, the pipeline
 * identifier identity owner (include/pigen/pipeline_identity.h +
 * src/pipeline_identity.c): that owner establishes the pipeline's module-scope
 * buf output packet identity, and this owner establishes the per-stage
 * execution-law view that law-checks the stages that feed it. It is a NEW
 * structured owner on the structured path; the quarantined textual
 * prototype's include/pigen/pipeline.h / src/pipeline.c is a different owner
 * and is not touched, not reused, and not linked here.
 *
 * Ownership: pigen_pipeline_stage is one stage's execution-law view. It
 * carries the stage node's syntax id (the PIGEN_SYNTAX_STAGE node), the
 * stage's outgoing field writes (the nonblocking `<=` items, as the syntax
 * ids of the stage's nonblocking-assignment nodes in source order), the
 * stage's consuming external/module-scope reads (as the syntax ids of the
 * read nodes in source order), and the stage's in/out packet $bits widths
 * (in_bits is the total $bits of the incoming fields the stage reads and
 * out_bits is the total $bits of the outgoing fields the stage writes; the
 * equal-total-$bits law says in_bits == out_bits at every boundary). An
 * invalid view carries the PIGEN_INVALID_ID sentinel in its syntax id and
 * its first_write / first_read ids, a write_count of 0, a read_count of 0,
 * and in_bits / out_bits of 0.
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */

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

/* Establishes one stage's execution-law view and validates the stage
 * execution laws for that stage. stage is the parsed stage node
 * (PIGEN_SYNTAX_STAGE) and its id; pipeline is the owning parsed pipeline
 * node (PIGEN_SYNTAX_PIPELINE); module is the resolved module the pipeline
 * lives in; expanded is the parsed expanded source the pipeline node was
 * parsed from (the source that resolves the stage's token origins to their
 * original spelling spans, as the syntax tree itself does through its
 * expanded source). On success it fills *out with the stage's syntax id,
 * its outgoing field writes (the nonblocking `<=` items), its consuming
 * external/module-scope reads, and its in/out packet $bits widths, and
 * validates the stage execution laws: the first stage cannot read a
 * pipeline field that is not initialized; a later stage reads only the
 * immutable incoming fields it is handed; each `<=` writes exactly one
 * outgoing field visible in the following stage; every boundary keeps an
 * equal total $bits; an external consuming read is atomic under the full
 * guard; and at most one item per cycle. Returns 0 on success and -1 on
 * error; a failed call reports the error and the compile stops - it may
 * leave partial state behind, and no test asserts that a failure left the
 * model unchanged. The entry point validates the four source-level-
 * discriminable stage laws on the structured path: the first stage may read
 * only enclosing / module-scope signals plus stage-local combinational values
 * (a pipeline-field read is diagnosed); a later stage reads only the immutable
 * incoming fields the earlier stages wrote (a read of any other field is
 * diagnosed); each <= destination must be a pipeline field (an outgoing
 * field) - a module-input or stage-local-wire destination is diagnosed; and
 * every later-stage boundary keeps an equal total $bits (in_bits == out_bits).
 * The two named laws with no source-level red form - an external consuming
 * read atomic under the full guard, and at most one item per cycle - are
 * pinned by the parent-module lowering's emission / behavioral witnesses, not
 * this seam. Returns 0 on success and -1 on error. */
int pigen_pipeline_stage_laws(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded,
	pigen_pipeline_stage *out);

#endif
