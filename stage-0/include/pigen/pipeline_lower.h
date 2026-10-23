#ifndef PIGEN_PIPELINE_LOWER_H
#define PIGEN_PIPELINE_LOWER_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* The pipeline parent-module lowering owner: the owner seam that turns one
 * PIGEN_SYNTAX_PIPELINE statement (a pipeline body inside a clocked process)
 * into common RTL objects, equations and processes of the parent module,
 * lowering the pipeline through the common RTL model.
 *
 * A pipeline is a chain of stages (the `stage begin ... end` items) that ends
 * in a single `yield` expression (what/does/pipeline/yield/mean.md): the yield
 * evaluates the pipeline's outgoing fields, including any final transfers, and
 * exposes the final outgoing value. This owner lowers that whole statement so
 * the pipeline participates in the parent module's RTL exactly like any other
 * declaration or transfer, publishing into the parent module's RTL model:
 *
 *   - the pipeline's one-entry buf stage storage: one-entry elastic storage at
 *     each stage boundary (what/is/a/buf.md), valid when occupied and ready
 *     according to elastic readiness, holding its payload under valid
 *     backpressure for one item per cycle; the intended lowering uses a
 *     dedicated primitive instance (the buf primitive, not a hand-rolled
 *     register);
 *   - the per-stage combinational next-value equations: for each stage, the
 *     outgoing fields' values computed as combinational equations over the
 *     immutable incoming fields the stage is handed by the earlier stages
 *     (how/do/pipeline/stage/reads/work.md);
 *   - the guarded ready/valid handshake: the complete enclosing procedural
 *     guard enables the pipeline, and a false guard holds all storage and
 *     disables the input and output handshakes (how/to/reset/and/enable/a/
 *     pipeline.md);
 *   - the final yield to the outgoing-packet write: the yield's outgoing
 *     fields written into the parent module's outgoing packet, which the
 *     step-(a) owner (include/pigen/pipeline_identity.h) has established as the
 *     pipeline's module-scope buf output packet identity.
 *
 * The outgoing packet's member order is SOURCE ORDER, the first yielded field
 * the MOST SIGNIFICANT BIT (MSB): the outgoing fields are packed left to right
 * in the order the final yield reads them, the first yielded field occupying
 * the highest-numbered bits. The established aggregate is a width-keyed sized
 * vector (the concatenation is interned keyed only on the summed packed width,
 * does/the/concatenation/data/type/keying/keep/member/order.md), so a
 * width-correct but misordered aggregate is indistinguishable at the data-type
 * and owner/route level; the member-order law is enforced structurally by this
 * owner and pinned by the emitted SV, not by a contract-level negative witness.
 *
 * The guard law is the complete enclosing procedural guard: when the guard is
 * true the pipeline advances (stages update, handshakes are live); when it is
 * false the pipeline holds every stage's storage and disables both the input
 * and the output handshake, so no item is accepted or emitted that cycle. The
 * pipeline's ready/valid equations are all gated by that guard.
 *
 * This owner is the SIBLING owner of the landed step-(a) seam, the pipeline
 * identifier identity owner (include/pigen/pipeline_identity.h +
 * src/pipeline_identity.c: pigen_pipeline_identifier_establish /
 * pigen_pipeline_identifier_of, which establishes the pipeline's module-scope
 * buf output packet identity), and of the step-(b) seam, the pipeline stage
 * execution-laws owner (include/pigen/pipeline_stage.h + src/pipeline_stage.c:
 * pigen_pipeline_stage_laws / pigen_pipeline_stage_is_invalid, which validates
 * the per-stage execution-law view). It is a NEW structured owner on the
 * structured path; the quarantined textual prototype's include/pigen/pipeline.h
 * / src/pipeline.c is a different owner and is not touched, not reused, and not
 * linked here.
 *
 * Like the sibling seams this owner is STANDALONE: the test target does not
 * link the step-(a)/(b) owners or the full RTL lowerer (src/rtl_lower.c). It
 * composes and publishes its RTL records through the common RTL model,
 * consuming the step-(a) outgoing-packet identity and the step-(b) stage-law
 * views only as validated inputs (what/is/elastic/rtl/task/eight.md:
 * pigen_lower_rtl_module composes its module record from already-published
 * state).
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind (partial RTL objects, equations or processes in the parent module's
 * RTL model), and nothing uses the model after a failed compile, so no caller
 * or test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This header fixes only the seam's shape. SKELETON: the entry point returns -
 * 1 and reports the diagnostic without linking the step-(a)/(b) owners or
 * mutating the RTL model, until the implementation stage (step (c3)). */

/* Lowers one PIGEN_SYNTAX_PIPELINE statement (a pipeline body inside a clocked
 * process) through the common RTL model, publishing its one-entry buf stage
 * storage, its per-stage combinational next-value equations over the immutable
 * incoming fields, its guarded ready/valid handshake, and the final yield to
 * the outgoing-packet write, all as common RTL objects, equations and processes
 * of the parent module, with the outgoing packet's member order in source order
 * (the first yielded field the MSB). lowering is the lowering context (the
 * validated source model and the target RTL model the records are published
 * into); module is the parent module identity the pipeline statement lives
 * inside (the module the pipeline's enclosing clocked process belongs to);
 * syntax is the pipeline node identity (PIGEN_SYNTAX_PIPELINE). On success it
 * returns 0. On error it returns -1 and reports the diagnostic and the compile
 * stops - it may leave partial state behind, and no test asserts that a failure
 * left the RTL model or the lowering maps unchanged (how/should/compiler/
 * builders/fail.md). SKELETON: validates its arguments (lowering, module and
 * the pipeline node are required; syntax must name a PIGEN_SYNTAX_PIPELINE
 * node), returns -1 and reports the diagnostic without linking the step-(a)/(b)
 * owners or mutating the RTL model, until the implementation stage. SKELETON:
 * the lowering context holds no syntax tree, so the stub validates only the
 * present argument set - lowering, its semantics and RTL models, a valid
 * parent module, and a non-PIGEN_INVALID_ID pipeline node identity - and
 * returns -1 for any rejected argument; the PIGEN_SYNTAX_PIPELINE kind check is
 * part of the full contract the implementation stage adds. */
int pigen_pipeline_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax);

#endif
