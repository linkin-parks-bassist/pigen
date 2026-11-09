#ifndef PIGEN_FABRIC_LOWER_H
#define PIGEN_FABRIC_LOWER_H

#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* Lower inline parent-owned fabric blocks into wiring equations over the
 * parent signals connected to child instance ports. The child instantiation
 * text itself remains emitted verbatim; this pass binds instance.port endpoints
 * through the resolved semantic instance records. */
int pigen_fabric_lower(pigen_rtl_lowering *lowering, pigen_module_id module,
	const pigen_syntax_tree *syntax);

#endif
