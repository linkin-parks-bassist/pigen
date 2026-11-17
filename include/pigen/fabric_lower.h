#ifndef PIGEN_FABRIC_LOWER_H
#define PIGEN_FABRIC_LOWER_H

#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* Lower resolved child-port connections to a parent-owned source-routed tree.
 * Payload conversions happen at ingress; route words and two-entry queues stay
 * internal. All parent fabric blocks share endpoint ownership and topology.
 * Native AXI whole-interface fabrics remain quarantined. */
int pigen_fabric_lower(pigen_rtl_lowering *lowering, pigen_module_id module,
	const pigen_syntax_tree *syntax);

#endif
