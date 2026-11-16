#ifndef PIGEN_EXPRESSION_H
#define PIGEN_EXPRESSION_H

#include <stddef.h>

#include "pigen/type_syntax.h"

typedef enum {
	PIGEN_SYNTAX_EXPR_LITERAL,
	PIGEN_SYNTAX_EXPR_NAME,
	PIGEN_SYNTAX_EXPR_GROUP,
	PIGEN_SYNTAX_EXPR_UNARY,
	PIGEN_SYNTAX_EXPR_BINARY,
	PIGEN_SYNTAX_EXPR_CONDITIONAL,
	PIGEN_SYNTAX_EXPR_CONCATENATION,
	PIGEN_SYNTAX_EXPR_REPLICATION,
	PIGEN_SYNTAX_EXPR_CALL,
	PIGEN_SYNTAX_EXPR_MEMBER,
	PIGEN_SYNTAX_EXPR_INDEX,
	PIGEN_SYNTAX_EXPR_SELECT,
	PIGEN_SYNTAX_EXPR_CAST,
	PIGEN_SYNTAX_EXPR_RECORD,
	PIGEN_SYNTAX_EXPR_ARRAY_PATTERN,
	PIGEN_SYNTAX_EXPR_ARRAY_PATTERN_REPLICATION,
	PIGEN_SYNTAX_EXPR_RECORD_FIELD
} pigen_syntax_expr_kind;

typedef enum {
	PIGEN_SYNTAX_OP_POSITIVE,
	PIGEN_SYNTAX_OP_NEGATE,
	PIGEN_SYNTAX_OP_LOGICAL_NOT,
	PIGEN_SYNTAX_OP_BITWISE_NOT,
	PIGEN_SYNTAX_OP_REDUCTION_AND,
	PIGEN_SYNTAX_OP_REDUCTION_NAND,
	PIGEN_SYNTAX_OP_REDUCTION_OR,
	PIGEN_SYNTAX_OP_REDUCTION_NOR,
	PIGEN_SYNTAX_OP_REDUCTION_XOR,
	PIGEN_SYNTAX_OP_REDUCTION_XNOR,
	PIGEN_SYNTAX_OP_POWER,
	PIGEN_SYNTAX_OP_MULTIPLY,
	PIGEN_SYNTAX_OP_DIVIDE,
	PIGEN_SYNTAX_OP_MODULO,
	PIGEN_SYNTAX_OP_ADD,
	PIGEN_SYNTAX_OP_SUBTRACT,
	PIGEN_SYNTAX_OP_SHIFT_LEFT,
	PIGEN_SYNTAX_OP_SHIFT_RIGHT,
	PIGEN_SYNTAX_OP_ARITH_SHIFT_LEFT,
	PIGEN_SYNTAX_OP_ARITH_SHIFT_RIGHT,
	PIGEN_SYNTAX_OP_LESS,
	PIGEN_SYNTAX_OP_LESS_EQUAL,
	PIGEN_SYNTAX_OP_GREATER,
	PIGEN_SYNTAX_OP_GREATER_EQUAL,
	PIGEN_SYNTAX_OP_EQUAL,
	PIGEN_SYNTAX_OP_NOT_EQUAL,
	PIGEN_SYNTAX_OP_CASE_EQUAL,
	PIGEN_SYNTAX_OP_CASE_NOT_EQUAL,
	PIGEN_SYNTAX_OP_WILDCARD_EQUAL,
	PIGEN_SYNTAX_OP_WILDCARD_NOT_EQUAL,
	PIGEN_SYNTAX_OP_BITWISE_AND,
	PIGEN_SYNTAX_OP_BITWISE_XOR,
	PIGEN_SYNTAX_OP_BITWISE_XNOR,
	PIGEN_SYNTAX_OP_BITWISE_OR,
	PIGEN_SYNTAX_OP_LOGICAL_AND,
	PIGEN_SYNTAX_OP_LOGICAL_OR
} pigen_syntax_operator;

typedef enum {
	PIGEN_SELECT_RANGE,
	PIGEN_SELECT_INDEXED_UP,
	PIGEN_SELECT_INDEXED_DOWN
} pigen_syntax_select_kind;

typedef struct {
	pigen_syntax_expr_kind kind;
	pigen_syntax_location location;
	union {
		struct { pigen_token_id token; } atom;
		struct { pigen_syntax_expr_id operand; } group;
		struct {
			pigen_syntax_operator operator;
			pigen_syntax_location operator_location;
			pigen_syntax_expr_id operand;
		} unary;
		struct {
			pigen_syntax_operator operator;
			pigen_syntax_location operator_location;
			pigen_syntax_expr_id left;
			pigen_syntax_expr_id right;
		} binary;
		struct {
			pigen_syntax_expr_id condition;
			pigen_syntax_expr_id when_true;
			pigen_syntax_expr_id when_false;
		} conditional;
		struct { size_t first_child; size_t child_count; } sequence;
		struct {
			pigen_syntax_expr_id count;
			size_t first_child;
			size_t child_count;
		} replication;
		struct {
			pigen_syntax_expr_id callee;
			size_t first_argument;
			size_t argument_count;
			/* Type inquiries: 1 is type-only; 2 retains an ambiguous type candidate. */
			int type_argument_kind;
			pigen_syntax_type_id type_argument;
		} call;
		struct {
			pigen_syntax_expr_id base;
			pigen_token_id member;
			int scoped;
			/* RECORD_FIELD retains its constant array key as well as a
			 * simple record member token when one was written. */
			pigen_syntax_expr_id pattern_key;
		} member;
		struct {
			pigen_syntax_expr_id base;
			pigen_syntax_expr_id index;
		} index;
		struct {
			pigen_syntax_expr_id base;
			pigen_syntax_expr_id left;
			pigen_syntax_expr_id right;
			pigen_syntax_select_kind kind;
		} select;
		struct {
			pigen_syntax_type_id type;
			pigen_syntax_expr_id value;
			/* Explicit expression-sized casts; simple names remain ambiguous
			 * with typedef casts until lexical symbol resolution. */
			int is_size;
			pigen_syntax_expr_id size;
		} cast;
	} as;
} pigen_syntax_expr;

struct pigen_syntax_expr_arena {
	pigen_syntax_expr *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_syntax_expr_id *children;
	size_t child_count;
	size_t child_capacity;
};

pigen_syntax_location pigen_syntax_location_from_extent(
	const pigen_expanded_source *source, size_t first, size_t after);
int pigen_parse_expression(const pigen_expanded_source *source,
	size_t first, size_t after, pigen_syntax_expr_arena *arena,
	pigen_syntax_type_arena *types,
	pigen_syntax_expr_id *expression, pigen_syntax_error *error);
const pigen_syntax_expr *pigen_syntax_expr_get(
	const pigen_syntax_expr_arena *arena, pigen_syntax_expr_id expression);
const pigen_syntax_expr_id *pigen_syntax_expr_children(
	const pigen_syntax_expr_arena *arena, size_t first, size_t count);
/* True for trees made only from unsized decimal integer literals and operators,
 * with no named values, typed casts, concatenations or calls. */
int pigen_syntax_expr_is_decimal_constant(const pigen_expanded_source *source,
	const pigen_syntax_expr_arena *arena,pigen_syntax_expr_id expression);
void pigen_free_syntax_expr_arena(pigen_syntax_expr_arena *arena);

#endif
