#ifndef PIGEN_RTL_NAME_H
#define PIGEN_RTL_NAME_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/rtl.h"
#include "pigen/source.h"

/* One module-local allocator assigns collision-free, immutable RTL names.
 * Source roles keep the provenance spelling of a checked identifier;
 * internal roles derive a stem and gain a numeric suffix only on
 * collision. A failed assignment publishes no partial names. */

/* One named slot to allocate: its role and the identifier span a source
 * role copies, or an invalid span for a synthetic internal role. */
typedef struct {
	pigen_rtl_name_kind kind;
	pigen_source_span origin;
} pigen_rtl_name_request;

/* Returns an invalid ID when unimplemented or when any request fails. */
pigen_rtl_name_id pigen_rtl_assign_names(pigen_rtl_model *model,
	const pigen_source_manager *sources,
	const pigen_rtl_name_request *requests, size_t request_count);
const pigen_rtl_name *pigen_rtl_name_get(const pigen_rtl_model *model,
	pigen_rtl_name_id name);

#endif
