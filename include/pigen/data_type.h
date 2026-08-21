#ifndef PIGEN_DATA_TYPE_H
#define PIGEN_DATA_TYPE_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/operation.h"
#include "pigen/source.h"

typedef struct pigen_semantic_model pigen_semantic_model;
typedef struct pigen_data_type pigen_data_type;

typedef enum {
	PIGEN_SIGN_INVALID,
	PIGEN_SIGN_IMPLICIT,
	PIGEN_SIGN_UNSIGNED,
	PIGEN_SIGN_SIGNED
} pigen_signedness;

typedef enum {
	PIGEN_DATA_TYPE_STATE_INVALID,
	PIGEN_DATA_TYPE_STATE_TWO,
	PIGEN_DATA_TYPE_STATE_FOUR
} pigen_state_domain;

typedef struct {
	pigen_const_expr_id left;
	pigen_const_expr_id right;
} pigen_packed_dimension;

pigen_data_type_id pigen_data_type_primitive_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
pigen_data_type_id pigen_data_type_implicit(pigen_semantic_model *model,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
pigen_data_type_id pigen_data_type_alias(pigen_semantic_model *model,
	pigen_symbol_id alias, pigen_data_type_id target,
	pigen_signedness signedness,
	const pigen_packed_dimension *dimensions, size_t dimension_count);
int pigen_data_type_exists(const pigen_semantic_model *model,
	pigen_data_type_id data_type);
pigen_signedness pigen_data_type_signedness(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
size_t pigen_data_type_dimension_count(const pigen_semantic_model *model,
	pigen_data_type_id data_type);
pigen_symbol_id pigen_data_type_alias_symbol(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
pigen_data_type_id pigen_data_type_alias_target(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
const pigen_packed_dimension *pigen_data_type_dimensions(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
pigen_data_type_id pigen_data_type_packed_element(pigen_semantic_model *model,
	pigen_data_type_id type);
pigen_data_type_id pigen_data_type_packed_select(pigen_semantic_model *model,
	pigen_data_type_id type, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind);
pigen_const_expr_id pigen_data_type_packed_width(pigen_semantic_model *model,
	pigen_data_type_id type);
pigen_data_type_id pigen_data_type_concatenation(pigen_semantic_model *model,
	const pigen_data_type_id *data_types, size_t count);
pigen_data_type_id pigen_data_type_integer(pigen_semantic_model *model);
pigen_data_type_id pigen_data_type_boolean(pigen_semantic_model *model);
int pigen_data_type_is_integral(const pigen_semantic_model *model,
	pigen_data_type_id type);
pigen_state_domain pigen_data_type_state_domain(
	const pigen_semantic_model *model, pigen_data_type_id type);
pigen_data_type_id pigen_data_type_sized_logic(pigen_semantic_model *model,
	size_t width, pigen_signedness signedness);
pigen_data_type_id pigen_data_type_unary_result(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_data_type_id operand);
pigen_data_type_id pigen_data_type_binary_result(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_data_type_id left, pigen_data_type_id right);
pigen_data_type_id pigen_data_type_conditional_result(pigen_semantic_model *model,
	pigen_data_type_id condition, pigen_data_type_id when_true,
	pigen_data_type_id when_false);

#endif
