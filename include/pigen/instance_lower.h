#ifndef PIGEN_INSTANCE_LOWER_H
#define PIGEN_INSTANCE_LOWER_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* The instance lowering owner: the owner seam that turns one parsed ordinary
 * SV child instantiation (a PIGEN_SYNTAX_INSTANCE node) into one child-instance
 * record in the common RTL model, owned by the parent module. This is the step
 * (i3) sibling of the FSM's step-(s5) owner (include/pigen/fsm_lower.h +
 * src/fsm_lower.c): the FSM owner lowers one PIGEN_SYNTAX_FSM statement through
 * the common RTL model into the parent module's RTL records, and this owner
 * lowers one PIGEN_SYNTAX_INSTANCE the same way, publishing the instance record
 * the fabric stage will resolve two-component instance.port endpoints against.
 *
 * The published record is a pigen_rtl_instance owned by the parent module's
 * pigen_rtl_module record: origin is the instance node's original span,
 * semantic_module is the resolved child module's provenance (the module whose
 * name token text equals the instance type-name token text, the (i2)
 * resolution read from the node), parameters is the ordered source-order
 * parameter range and connections is the ordered source-order connection range
 * (what/is/elastic/rtl/task/eight.md: a module record is composed from
 * already-published state; the RTL record model in include/pigen/rtl.h). The
 * record carries the instance-name text as the only name - surface-independent:
 * it keys off the resolved child module identity and the port identities, never
 * off the instance spelling.
 *
 * The owner consumes the (i1) node and the (i2) identity facts only as
 * validated inputs read from the node - the (i2) identity owner is not linked
 * into the standalone test target, so the facts are read here the way the
 * identity owner establishes them. It composes and publishes its records
 * through the public RTL model constructors owned by the parent module record
 * (pigen_rtl_module_add_with_owner, pigen_rtl_object_add_with_owner,
 * pigen_rtl_expr_add_integer and pigen_rtl_instance_add_with_owner in
 * include/pigen/rtl.h, backed by src/rtl.c, which IS linked) and never calls
 * pigen_lower_rtl_expression (src/rtl_lower.c is not linked into the
 * standalone target, mirroring the FSM seam).
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This seam owns the instance lowering on the structured path
 * (include/pigen/instance_lower.h + src/instance_lower.c); the quarantined
 * textual prototype is not touched, reused or linked. */

/* Lowers one PIGEN_SYNTAX_INSTANCE node into the parent module's RTL model,
 * publishing one pigen_rtl_instance record owned by the parent module record.
 * lowering carries the validated semantic model (lowering->semantics, which
 * owns the sources the token-text resolution reads through) and the target RTL
 * model (lowering->rtl); parent is the resolved module the instance lives in;
 * syntax_id and instance are the owning instance node's syntax id and node (a
 * PIGEN_SYNTAX_INSTANCE). On success it publishes the parent module record (if
 * not already published), one VARIABLE object per source-order connection
 * bound to the parent's matching signal, one integer expression per source-
 * order decimal parameter, and the instance record, then fills *out and
 * returns 0. On error it returns -1 and the compile stops: a non-INSTANCE
 * node kind, a connection expression that is not an atom, a NAME atom that
 * matches no parent-module signal, or a parameter LITERAL that is not plain
 * decimal, each reported at the offending original span. It may leave partial
 * state behind and no test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */
int pigen_instance_lower(pigen_rtl_lowering *lowering,
	pigen_module_id parent, pigen_syntax_id syntax_id,
	const pigen_syntax_node *instance, pigen_rtl_instance_id *out);

#endif
