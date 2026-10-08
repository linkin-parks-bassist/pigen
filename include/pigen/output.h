#ifndef PIGEN_OUTPUT_H
#define PIGEN_OUTPUT_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/rtl.h"
#include "pigen/source.h"

/* Ordered output slots for one generated design (Task 9). The model owns an
 * ordered array of output items: the top-level emission order is the array
 * order, and a structured module item carries the half-open range of its
 * nested items in the same array. OPAQUE slots carry only a source span (their
 * text stays in the source manager); a structured slot carries only the
 * matching structured identity, copied by value. No API returns or searches
 * opaque source text. */
typedef enum {
	PIGEN_OUTPUT_OPAQUE,
	PIGEN_OUTPUT_MODULE,
	PIGEN_OUTPUT_RTL_OBJECT,
	PIGEN_OUTPUT_RTL_INSTANCE,
	PIGEN_OUTPUT_RTL_EQUATION,
	PIGEN_OUTPUT_RTL_PROCESS
} pigen_output_kind;

/* One ordered output slot. OPAQUE holds only span; a structured kind holds
 * only its matching identity (a value copy, no ownership transfer) and its
 * span is invalid. The layout field is the half-open range [first, first +
 * count) of this item's nested items in the model's item array; it is
 * meaningful for a module item and empty for every other kind. */
typedef struct {
	pigen_output_kind kind;
	union {
		pigen_source_span span;
		pigen_rtl_module_id module;
		pigen_rtl_object_id object;
		pigen_rtl_instance_id instance;
		pigen_rtl_equation_id equation;
		pigen_rtl_process_id process;
	} as;
	pigen_rtl_record_range layout;
} pigen_output_item;

/* The ordered output model. It owns items (the ordered slot array) and the
 * per-module nested-layout ranges; the layout of each module item lives in
 * the item itself, and the ranges array is the parallel, owner-managed view
 * keyed by arena index. All three pointers are NULL and all counts zero for a
 * freshly initialized model. */
typedef struct {
	pigen_output_item *items;
	size_t item_count;
	size_t item_capacity;
	pigen_rtl_record_range *nested_layouts;
	size_t nested_layout_count;
	size_t nested_layout_capacity;
} pigen_output_model;

/* Zeroes the model: no owned arrays, no counts. */
void pigen_output_model_init(pigen_output_model *model);

/* Appends one output item, growing the owned arrays on demand, and returns
 * the appended slot's index (PIGEN_INVALID_ID on failure). The identity or
 * span is copied by value; the model takes no ownership of it. */
size_t pigen_output_item_add(pigen_output_model *model, pigen_output_item item);

/* The ordered slot count. */
size_t pigen_output_item_count(const pigen_output_model *model);

/* Read slot i by value; (pigen_output_item){0} when i is out of range. */
pigen_output_item pigen_output_item_get(const pigen_output_model *model,
	size_t index);

/* The slot's kind, or PIGEN_OUTPUT_OPAQUE for an out-of-range index. */
pigen_output_kind pigen_output_item_kind(const pigen_output_model *model,
	size_t index);

/* The slot's opaque span, or an all-invalid span when the slot is not OPAQUE
 * or is out of range. */
pigen_source_span pigen_output_item_span(const pigen_output_model *model,
	size_t index);

/* The slot's matching structured identity for KIND, or the PIGEN_INVALID_ID
 * identity when the slot is not KIND or is out of range. */
pigen_rtl_module_id pigen_output_item_module(const pigen_output_model *model,
	size_t index);
pigen_rtl_object_id pigen_output_item_object(const pigen_output_model *model,
	size_t index);
pigen_rtl_instance_id pigen_output_item_instance(const pigen_output_model *model,
	size_t index);
pigen_rtl_equation_id pigen_output_item_equation(const pigen_output_model *model,
	size_t index);
pigen_rtl_process_id pigen_output_item_process(const pigen_output_model *model,
	size_t index);

/* Builds the ordered output model for one generated design. The exact
 * syntax-tree walk, monotonic-coverage gate and nested-failure restoration
 * arrive with the implementation stage; the stub returns 0 (succeeds) with
 * zero items. A failed call returns -1 and may leave partial state behind,
 * and no test asserts what a failure left (how/should/compiler/builders/fail.md). */
int pigen_build_output_model(pigen_output_model *model);

/* Releases the owned arrays and zeroes the model: every pointer NULL, every
 * count zero. */
void pigen_free_output_model(pigen_output_model *model);

#endif
