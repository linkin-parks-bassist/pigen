#ifndef PIGEN_OPERATION_H
#define PIGEN_OPERATION_H

#include "pigen/ids.h"

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

typedef struct {
	pigen_unary_operator operator;
	pigen_data_type_id operand_data_type;
	pigen_data_type_id result_data_type;
} pigen_unary_operation;

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

typedef struct {
	pigen_binary_operator operator;
	pigen_data_type_id left_data_type;
	pigen_data_type_id right_data_type;
	pigen_data_type_id result_data_type;
} pigen_binary_operation;

typedef struct {
	pigen_data_type_id condition_data_type;
	pigen_data_type_id when_true_data_type;
	pigen_data_type_id when_false_data_type;
	pigen_data_type_id result_data_type;
} pigen_conditional_operation;

typedef enum {
	PIGEN_SEMANTIC_SELECT_RANGE,
	PIGEN_SEMANTIC_SELECT_INDEXED_UP,
	PIGEN_SEMANTIC_SELECT_INDEXED_DOWN
} pigen_select_kind;

int pigen_select_kind_is_valid(pigen_select_kind kind);
int pigen_unary_operator_is_valid(pigen_unary_operator operator);
int pigen_binary_operator_is_valid(pigen_binary_operator operator);

#endif
