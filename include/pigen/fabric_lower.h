#ifndef PIGEN_FABRIC_LOWER_H
#define PIGEN_FABRIC_LOWER_H

#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* Lower resolved inline fabrics into parent-owned wiring and arbitration.
 * Native edges carry aggregate protocol identities; ordinary edges resolve
 * through scalar child-connection records. Child pin mapping belongs to the
 * output builder rather than this routing pass. */
int pigen_fabric_lower(pigen_rtl_lowering *lowering, pigen_module_id module,
	const pigen_syntax_tree *syntax);

#endif
