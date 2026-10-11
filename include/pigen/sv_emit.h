#ifndef PIGEN_SV_EMIT_H
#define PIGEN_SV_EMIT_H

#include "pigen/output.h"
#include "pigen/rtl.h"
#include "pigen/rtl_name.h"
#include "pigen/source.h"

/* Renders the whole generated design as one SystemVerilog string. Returns 0 on
 * success, with *output_string set to a newly-allocated NUL-terminated string
 * that the caller must free, or -1 on failure, with *output_string left NULL
 * and no partial output produced. */
int pigen_emit_systemverilog(const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl,
	char **output_string);

#endif
