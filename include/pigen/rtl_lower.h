#ifndef PIGEN_RTL_LOWER_H
#define PIGEN_RTL_LOWER_H

#include <stddef.h>

#include "pigen/data_type.h"
#include "pigen/ids.h"
#include "pigen/rtl.h"
#include "pigen/semantic.h"

/* Owner-based type and expression lowering for the elastic RTL vertical
 * slice. The lowering consumes validated data-type and constant-expression
 * identities from the semantic model and publishes the corresponding RTL
 * records into the RTL model, memoizing by identity so each source identity
 * is lowered exactly once. The symbol-to-endpoint map and every width,
 * signedness, conversion and child-order decision arrive with the
 * test-contract and implementation stages; this header fixes only the shape. */

/* One signal's declaration endpoints in the RTL model. payload is the object
 * that carries the signal's data; valid and ready are the expression controls
 * that gate the ready/valid transfer. input_payload, input_valid and
 * input_ready are the objects the declaration exposes on its input side. A
 * constant control carries invalid object identity and a constant-expression
 * identity; the exact field-population rules per realization are settled by
 * the test-contract and implementation stages, so this type fixes only the
 * shape. */
typedef struct {
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	pigen_rtl_object_id input_payload;
	pigen_rtl_object_id input_valid;
	pigen_rtl_object_id input_ready;
} pigen_rtl_signal_endpoints;

/* One lowering pass. semantics is the validated source model and rtl is the
 * target that receives the lowered records. The two identity memo maps are
 * keyed by the source arena index: lowered_types by pigen_data_type_id.index
 * and lowered_expressions by pigen_const_expr_id.index. Each map slot holds
 * the opaque RTL handle the implementation will fill. The signal-endpoints
 * map is likewise grow-on-demand and keyed by pigen_signal_id.index, holding
 * the pigen_rtl_signal_endpoints the declaration endpoint lowering will fill.
 * A zero map count is the empty state: every identity is still to be
 * lowered. */
typedef struct {
	pigen_semantic_model *semantics;
	pigen_rtl_model *rtl;
	pigen_rtl_type_id *lowered_types;
	size_t lowered_type_count;
	size_t lowered_type_capacity;
	pigen_rtl_expr_id *lowered_expressions;
	size_t lowered_expression_count;
	size_t lowered_expression_capacity;
	pigen_rtl_signal_endpoints *lowered_endpoints;
	size_t lowered_endpoint_count;
	size_t lowered_endpoint_capacity;
} pigen_rtl_lowering;

/* Zeroes the record and leaves all three memo maps empty (no handles
 * allocated). The semantics and rtl pointers are stored as given; the maps
 * grow on demand when the lowering implementation lowers a source identity or
 * a signal's declaration endpoints. */
void pigen_rtl_lowering_init(pigen_rtl_lowering *lowering,
	pigen_semantic_model *semantics, pigen_rtl_model *rtl);

/* Releases the three memo maps and zeroes the record. The owned semantic and
 * RTL models are not released by this call; the caller keeps them. */
void pigen_rtl_lowering_free(pigen_rtl_lowering *lowering);

/* Lowers one validated data-type identity, returning its memoized RTL type
 * handle. The exact return and error contract is settled by the
 * implementation stage; the stub returns the unimplemented/invalid sentinel
 * without touching the memo maps. */
pigen_rtl_type_id pigen_lower_rtl_type(pigen_rtl_lowering *lowering,
	pigen_data_type_id type);

/* Lowers one validated constant-expression identity, returning its memoized
 * RTL expression handle. The exact return and error contract is settled by
 * the implementation stage; the stub returns the unimplemented/invalid
 * sentinel without touching the memo maps. */
pigen_rtl_expr_id pigen_lower_rtl_expression(pigen_rtl_lowering *lowering,
	pigen_const_expr_id expression);

/* Lowers every signal declaration in one module, filling the
 * signal-indexed endpoints map with each signal's payload/valid/ready
 * endpoints. Returns 0 on success and -1 on error; the exact error contract
 * is settled by the implementation stage. The stub returns -1 (the
 * unimplemented sentinel) without touching the lowering maps or the RTL
 * model. */
int pigen_lower_rtl_module_declarations(pigen_rtl_lowering *lowering,
	pigen_module_id module);

#endif
