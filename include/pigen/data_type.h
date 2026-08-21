#ifndef PIGEN_DATA_TYPE_H
#define PIGEN_DATA_TYPE_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/source.h"

typedef struct pigen_semantic_model pigen_semantic_model;

typedef enum {
	PIGEN_TYPE_LOGIC,
	PIGEN_TYPE_BIT,
	PIGEN_TYPE_INTEGER,
	PIGEN_TYPE_NAMED
} pigen_semantic_type_kind;

typedef enum {
	PIGEN_SIGN_IMPLICIT,
	PIGEN_SIGN_UNSIGNED,
	PIGEN_SIGN_SIGNED
} pigen_signedness;

typedef enum {
	PIGEN_DATA_TYPE_STATE_INVALID,
	PIGEN_DATA_TYPE_STATE_TWO,
	PIGEN_DATA_TYPE_STATE_FOUR
} pigen_state_domain;

typedef enum {
	PIGEN_UNARY_POSITIVE,
	PIGEN_UNARY_NEGATE,
	PIGEN_UNARY_BITWISE_NOT,
	PIGEN_UNARY_LOGICAL_NOT,
	PIGEN_UNARY_REDUCTION_AND,
	PIGEN_UNARY_REDUCTION_NAND,
	PIGEN_UNARY_REDUCTION_OR,
	PIGEN_UNARY_REDUCTION_NOR,
	PIGEN_UNARY_REDUCTION_XOR,
	PIGEN_UNARY_REDUCTION_XNOR
} pigen_unary_operator;

typedef enum {
	PIGEN_BINARY_ADD,
	PIGEN_BINARY_SUBTRACT,
	PIGEN_BINARY_MULTIPLY,
	PIGEN_BINARY_DIVIDE,
	PIGEN_BINARY_MODULO,
	PIGEN_BINARY_POWER,
	PIGEN_BINARY_SHIFT_LEFT,
	PIGEN_BINARY_SHIFT_RIGHT,
	PIGEN_BINARY_ARITH_SHIFT_LEFT,
	PIGEN_BINARY_ARITH_SHIFT_RIGHT,
	PIGEN_BINARY_LESS,
	PIGEN_BINARY_LESS_EQUAL,
	PIGEN_BINARY_GREATER,
	PIGEN_BINARY_GREATER_EQUAL,
	PIGEN_BINARY_EQUAL,
	PIGEN_BINARY_NOT_EQUAL,
	PIGEN_BINARY_CASE_EQUAL,
	PIGEN_BINARY_CASE_NOT_EQUAL,
	PIGEN_BINARY_WILDCARD_EQUAL,
	PIGEN_BINARY_WILDCARD_NOT_EQUAL,
	PIGEN_BINARY_BITWISE_AND,
	PIGEN_BINARY_BITWISE_XOR,
	PIGEN_BINARY_BITWISE_XNOR,
	PIGEN_BINARY_BITWISE_OR,
	PIGEN_BINARY_LOGICAL_AND,
	PIGEN_BINARY_LOGICAL_OR
} pigen_binary_operator;

typedef enum {
	PIGEN_SEMANTIC_SELECT_RANGE,
	PIGEN_SEMANTIC_SELECT_INDEXED_UP,
	PIGEN_SEMANTIC_SELECT_INDEXED_DOWN
} pigen_select_kind;

int pigen_select_kind_is_valid(pigen_select_kind kind);

typedef struct {
	pigen_const_expr_id left;
	pigen_const_expr_id right;
} pigen_packed_dimension;

typedef struct {
	pigen_semantic_type_kind kind;
	pigen_signedness signedness;
	pigen_symbol_id named_symbol;
	size_t first_dimension;
	size_t dimension_count;
} pigen_semantic_type;

pigen_type_id pigen_type_intern(pigen_semantic_model *model,
	pigen_semantic_type_kind kind, pigen_signedness signedness,
	pigen_symbol_id named_symbol, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
pigen_type_id pigen_data_type_primitive_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
pigen_type_id pigen_data_type_implicit(pigen_semantic_model *model,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
pigen_type_id pigen_data_type_alias(pigen_semantic_model *model,
	pigen_symbol_id alias, pigen_signedness signedness,
	const pigen_packed_dimension *dimensions, size_t dimension_count);
const pigen_semantic_type *pigen_type_get(const pigen_semantic_model *model,
	pigen_type_id type);
const pigen_packed_dimension *pigen_type_dimensions(
	const pigen_semantic_model *model, pigen_type_id type);
pigen_type_id pigen_type_packed_element(pigen_semantic_model *model,
	pigen_type_id type);
pigen_type_id pigen_type_packed_select(pigen_semantic_model *model,
	pigen_type_id type, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind);
pigen_const_expr_id pigen_type_packed_width(pigen_semantic_model *model,
	pigen_type_id type);
pigen_type_id pigen_type_concatenation(pigen_semantic_model *model,
	const pigen_type_id *types, size_t count);
pigen_type_id pigen_semantic_integer_type(pigen_semantic_model *model);
pigen_type_id pigen_semantic_boolean_result_type(pigen_semantic_model *model);
int pigen_data_type_is_integral(const pigen_semantic_model *model,
	pigen_type_id type);
pigen_state_domain pigen_data_type_state_domain(
	const pigen_semantic_model *model, pigen_type_id type);
pigen_type_id pigen_data_type_sized_logic(pigen_semantic_model *model,
	size_t width, pigen_signedness signedness);
pigen_type_id pigen_data_type_unary_result(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_type_id operand);
pigen_type_id pigen_data_type_binary_result(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_type_id left, pigen_type_id right);
pigen_type_id pigen_data_type_conditional_result(pigen_semantic_model *model,
	pigen_type_id condition, pigen_type_id when_true,
	pigen_type_id when_false);

#endif
