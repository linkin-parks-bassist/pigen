#ifndef PIGEN_RTL_H
#define PIGEN_RTL_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/data_type.h"
#include "pigen/ids.h"
#include "pigen/source.h"

/* A bound is either a concrete signed value or a resolved RTL expression.
 * The value field is ignored when expression is valid. */
typedef struct {
	int64_t value;
	pigen_rtl_expr_id expression;
} pigen_rtl_bound;

typedef struct {
	pigen_rtl_bound left;
	pigen_rtl_bound right;
} pigen_rtl_packed_dimension;

/* Least-significant word first. X/Z masks are disjoint; value bits under
 * either mask are zero. Integer literals use sign and magnitude. */
typedef struct {
	uint64_t value;
	uint64_t x_mask;
	uint64_t z_mask;
} pigen_rtl_literal_word;

typedef struct {
	pigen_signedness signedness;
	pigen_state_domain state_domain;
	uint64_t width;
	pigen_rtl_expr_id width_expression;
	pigen_source_span origin;
	const pigen_rtl_packed_dimension *dimensions;
	size_t dimension_count;
} pigen_rtl_type;

typedef enum {
	PIGEN_RTL_EXPR_INVALID,
	PIGEN_RTL_EXPR_INTEGER,
	PIGEN_RTL_EXPR_BITS,
	PIGEN_RTL_EXPR_OBJECT,
	PIGEN_RTL_EXPR_UNARY,
	PIGEN_RTL_EXPR_BINARY,
	PIGEN_RTL_EXPR_CONDITIONAL,
	PIGEN_RTL_EXPR_CONVERSION,
	PIGEN_RTL_EXPR_INDEX,
	PIGEN_RTL_EXPR_SELECT,
	PIGEN_RTL_EXPR_CONCATENATION
} pigen_rtl_expr_kind;

typedef struct {
	pigen_rtl_expr_kind kind;
	pigen_rtl_type_id type;
	pigen_source_span origin;
	uint64_t value; /* Low word, for the uint64_t convenience constructors. */
	const pigen_rtl_literal_word *literal_words;
	size_t literal_bit_count;
	int literal_negative;
	pigen_rtl_object_id object;
	size_t first_child;
	size_t child_count;
	union {
		struct {
			pigen_unary_resolution resolution;
		} unary;
		struct {
			pigen_binary_resolution resolution;
		} binary;
		struct {
			pigen_conditional_resolution resolution;
		} conditional;
		struct {
			pigen_conversion conversion;
		} conversion;
		struct {
			pigen_select_kind kind;
		} select;
	} as;
} pigen_rtl_expr;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_object;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_instance;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_equation;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_update;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_process;

typedef struct {
	pigen_source_span origin;
} pigen_rtl_module;

typedef struct {
	pigen_rtl_type *types;
	size_t type_count;
	size_t type_capacity;
	pigen_rtl_expr *expressions;
	size_t expression_count;
	size_t expression_capacity;
	pigen_rtl_expr_id *expression_children;
	size_t expression_child_count;
	size_t expression_child_capacity;
	pigen_rtl_object *objects;
	size_t object_count;
	size_t object_capacity;
	pigen_rtl_instance *instances;
	size_t instance_count;
	size_t instance_capacity;
	pigen_rtl_equation *equations;
	size_t equation_count;
	size_t equation_capacity;
	pigen_rtl_update *updates;
	size_t update_count;
	size_t update_capacity;
	pigen_rtl_process *processes;
	size_t process_count;
	size_t process_capacity;
	pigen_rtl_module *modules;
	size_t module_count;
	size_t module_capacity;
} pigen_rtl_model;

/* Arena indices are stable identities; origin may be invalid for
 * synthetic hardware. */
pigen_rtl_type_id pigen_rtl_type_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_type *pigen_rtl_type_get(const pigen_rtl_model *model,
	pigen_rtl_type_id type);
pigen_rtl_type_id pigen_rtl_type_intern(pigen_rtl_model *model,
	const pigen_rtl_type *descriptor);
pigen_rtl_expr_id pigen_rtl_expr_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_expr *pigen_rtl_expr_get(const pigen_rtl_model *model,
	pigen_rtl_expr_id expression);
/* Constructors copy dimensions, literal words and child slices into model
 * ownership. Accessor pointers expire on arena growth; IDs remain stable. */
pigen_rtl_expr_id pigen_rtl_expr_add_literal(pigen_rtl_model *model,
	pigen_rtl_expr_kind kind, pigen_rtl_type_id type,
	const pigen_rtl_literal_word *words, size_t bit_count, int negative,
	pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_integer(pigen_rtl_model *model,
	pigen_rtl_type_id type, uint64_t value, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_bits(pigen_rtl_model *model,
	pigen_rtl_type_id type, uint64_t value, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_object(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_object_id object,
	pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_unary(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_unary_resolution *resolution,
	pigen_rtl_expr_id operand, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_binary(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_binary_resolution *resolution,
	pigen_rtl_expr_id left, pigen_rtl_expr_id right, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_conditional(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_conditional_resolution *resolution,
	pigen_rtl_expr_id condition, pigen_rtl_expr_id when_true,
	pigen_rtl_expr_id when_false, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_conversion(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_conversion *conversion,
	pigen_rtl_expr_id operand, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_index(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id index,
	pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_select(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id left,
	pigen_rtl_expr_id right, pigen_select_kind kind, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_concatenation(pigen_rtl_model *model,
	pigen_rtl_type_id type, const pigen_rtl_expr_id *children, size_t count,
	pigen_source_span origin);
const pigen_rtl_expr_id *pigen_rtl_expr_children(const pigen_rtl_model *model,
	pigen_rtl_expr_id expression, size_t *child_count);
pigen_rtl_object_id pigen_rtl_object_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_object *pigen_rtl_object_get(const pigen_rtl_model *model,
	pigen_rtl_object_id object);
pigen_rtl_instance_id pigen_rtl_instance_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_instance *pigen_rtl_instance_get(const pigen_rtl_model *model,
	pigen_rtl_instance_id instance);
pigen_rtl_equation_id pigen_rtl_equation_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_equation *pigen_rtl_equation_get(const pigen_rtl_model *model,
	pigen_rtl_equation_id equation);
pigen_rtl_update_id pigen_rtl_update_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_update *pigen_rtl_update_get(const pigen_rtl_model *model,
	pigen_rtl_update_id update);
pigen_rtl_process_id pigen_rtl_process_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_process *pigen_rtl_process_get(const pigen_rtl_model *model,
	pigen_rtl_process_id process);
pigen_rtl_module_id pigen_rtl_module_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_module *pigen_rtl_module_get(const pigen_rtl_model *model,
	pigen_rtl_module_id module);
void pigen_free_rtl_model(pigen_rtl_model *model);

#endif
