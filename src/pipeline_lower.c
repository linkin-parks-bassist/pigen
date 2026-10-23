#include <stddef.h>

#include "pigen/pipeline_lower.h"

#include "pigen/semantic.h"

/* The pipeline parent-module lowering owner (step (c1), the (c) boundary of
 * the pipeline step (c) parent-module lowering). This file defines the owner
 * seam's entry point, pigen_pipeline_lower, as a fail-loud stub.
 *
 * The full behavior (what/is/the/pipeline/parent/module/lowering.md, step (c1))
 * arrives with the implementation stage (step (c3)): lowering one
 * PIGEN_SYNTAX_PIPELINE statement (a pipeline body inside a clocked process)
 * through the common RTL model, publishing the pipeline's one-entry buf stage
 * storage, the per-stage combinational next-value equations over the immutable
 * incoming fields, the guarded ready/valid handshake (the complete enclosing
 * procedural guard enables the pipeline; false holds all storage and disables
 * the input and output handshakes), and the final yield to the outgoing-packet
 * write, all as common RTL objects, equations and processes of the parent
 * module, with the outgoing packet's member order in source order (the first
 * yielded field the MSB).
 *
 * The stub validates its arguments and reports the diagnostic without linking
 * the step-(a)/(b) owners or mutating the RTL model. A failed call returns -1
 * and the compile stops; it may leave partial state behind, and nothing uses
 * the model after a failed compile, so no caller or test asserts that a
 * failure left the model unchanged (how/should/compiler/builders/fail.md). */
int pigen_pipeline_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *pipeline,
	const pigen_expanded_source *expanded)
{
	/* Validate the lowering context, the parent module and the pipeline node
	 * identity, exactly as the sibling lowerer seam validates its context
	 * (src/rtl_lower.c, pigen_lower_rtl_transfers). */
	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, module))
		return -1;
	if (syntax.index == PIGEN_INVALID_ID)
		return -1;
	/* The seam carries the syntax tree the lowering reads: the pipeline node
	 * and the expanded source it was parsed from (consistent with the
	 * step-(a)/(b) siblings, which both take const pigen_syntax_node
	 * *pipeline). Validate them and reject a node that is not a
	 * PIGEN_SYNTAX_PIPELINE statement (the seam's own full-contract obligation
	 * - a well-established identity with a legal pipeline must lower, so a
	 * non-pipeline node must be the form the lowering rejects). */
	if (!pipeline || pipeline->kind != PIGEN_SYNTAX_PIPELINE || !expanded)
		return -1;
	/* SKELETON: the lowering of the PIGEN_SYNTAX_PIPELINE statement through
	 * the common RTL model is unimplemented. Report the diagnostic and return
	 * -1 without linking the step-(a)/(b) owners (pipeline identity / stage
	 * laws) or publishing any RTL object, equation or process; the compile
	 * stops (how/should/compiler/builders/fail.md). */
	return -1;
}
