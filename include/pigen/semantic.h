#ifndef PIGEN_SEMANTIC_H
#define PIGEN_SEMANTIC_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/source.h"

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
	PIGEN_SYMBOL_VALUE,
	PIGEN_SYMBOL_MODULE,
	PIGEN_SYMBOL_PARAMETER,
	PIGEN_SYMBOL_TYPEDEF,
	PIGEN_SYMBOL_TRANSPORT,
	PIGEN_SYMBOL_PIPELINE,
	PIGEN_SYMBOL_STAGE,
	PIGEN_SYMBOL_FSM,
	PIGEN_SYMBOL_FABRIC
} pigen_symbol_kind;

typedef enum {
	PIGEN_EXPR_INTEGER,
	PIGEN_EXPR_BITS,
	PIGEN_EXPR_SYMBOL,
	PIGEN_EXPR_GROUP,
	PIGEN_EXPR_UNARY,
	PIGEN_EXPR_BINARY,
	PIGEN_EXPR_CONDITIONAL,
	PIGEN_EXPR_INDEX,
	PIGEN_EXPR_SELECT,
	PIGEN_EXPR_CONCATENATION
} pigen_semantic_expr_kind;

typedef enum {
	PIGEN_CONST_EXPR_INTEGER,
	PIGEN_CONST_EXPR_BITS,
	PIGEN_CONST_EXPR_SYMBOL,
	PIGEN_CONST_EXPR_UNARY,
	PIGEN_CONST_EXPR_BINARY,
	PIGEN_CONST_EXPR_CONDITIONAL,
	PIGEN_CONST_EXPR_INDEX,
	PIGEN_CONST_EXPR_SELECT,
	PIGEN_CONST_EXPR_SELECT_WIDTH,
	PIGEN_CONST_EXPR_CONCATENATION,
	PIGEN_CONST_EXPR_WIDTH_SUM,
	PIGEN_CONST_EXPR_WIDTH_PRODUCT
} pigen_const_expr_kind;

typedef uint8_t pigen_bit_state;
enum {
	PIGEN_BIT_ZERO,
	PIGEN_BIT_ONE,
	PIGEN_BIT_X,
	PIGEN_BIT_Z
};

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

typedef enum {
	PIGEN_SEMANTIC_BUF,
	PIGEN_SEMANTIC_PORT,
	PIGEN_SEMANTIC_SKID,
	PIGEN_SEMANTIC_FIFO
} pigen_semantic_transport_kind;

typedef enum {
	PIGEN_SEMANTIC_INTERNAL,
	PIGEN_SEMANTIC_INPUT,
	PIGEN_SEMANTIC_OUTPUT,
	PIGEN_SEMANTIC_INOUT
} pigen_semantic_direction;

typedef enum {
	PIGEN_SEMANTIC_VALUE_NET,
	PIGEN_SEMANTIC_VALUE_VARIABLE
} pigen_semantic_value_storage;

typedef enum {
	PIGEN_SEMANTIC_POSEDGE,
	PIGEN_SEMANTIC_NEGEDGE
} pigen_semantic_edge;

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

typedef struct {
	pigen_scope_id parent;
	pigen_symbol_id last_symbol;
	/* Invalid for synthetic or cross-file scopes such as a compilation unit. */
	pigen_source_span span;
} pigen_scope;

typedef struct {
	pigen_symbol_kind kind;
	pigen_scope_id scope;
	pigen_symbol_id previous_in_scope;
	pigen_type_id type;
	pigen_source_span name;
	pigen_source_span declaration;
	union {
		pigen_module_id module;
		pigen_parameter_id parameter;
		pigen_value_id value;
		pigen_transport_id transport;
		pigen_pipeline_id pipeline;
		pigen_stage_id stage;
		pigen_fsm_id fsm;
		pigen_fabric_id fabric;
	} object;
} pigen_symbol;

typedef struct {
	pigen_semantic_expr_kind kind;
	pigen_type_id type;
	pigen_source_span span;
	pigen_const_expr_id constant;
	pigen_lvalue_id lvalue;
	union {
		uint64_t integer;
		struct { size_t first_state; size_t state_count; } bits;
		pigen_symbol_id symbol;
		struct { pigen_expr_id operand; } group;
		struct {
			pigen_unary_operator operator;
			pigen_expr_id operand;
		} unary;
		struct {
			pigen_binary_operator operator;
			pigen_expr_id left;
			pigen_expr_id right;
		} binary;
		struct {
			pigen_expr_id condition;
			pigen_expr_id when_true;
			pigen_expr_id when_false;
		} conditional;
		struct {
			pigen_expr_id base;
			pigen_expr_id index;
		} index;
		struct {
			pigen_expr_id base;
			pigen_expr_id left;
			pigen_expr_id right;
			pigen_select_kind kind;
		} select;
		struct {
			size_t first_child;
			size_t child_count;
		} sequence;
	} as;
} pigen_semantic_expr;

typedef struct {
	pigen_const_expr_kind kind;
	pigen_type_id type;
	union {
		uint64_t integer;
		struct { size_t first_state; size_t state_count; } bits;
		pigen_symbol_id symbol;
		struct {
			pigen_unary_operator operator;
			pigen_const_expr_id operand;
		} unary;
		struct {
			pigen_binary_operator operator;
			pigen_const_expr_id left;
			pigen_const_expr_id right;
		} binary;
		struct {
			pigen_const_expr_id condition;
			pigen_const_expr_id when_true;
			pigen_const_expr_id when_false;
		} conditional;
		struct {
			pigen_const_expr_id base;
			pigen_const_expr_id index;
		} index;
		struct {
			pigen_const_expr_id base;
			pigen_const_expr_id left;
			pigen_const_expr_id right;
			pigen_select_kind kind;
		} select;
		struct {
			pigen_const_expr_id left;
			pigen_const_expr_id right;
			pigen_select_kind kind;
		} select_width;
		struct {
			size_t first_child;
			size_t child_count;
		} sequence;
	} as;
} pigen_const_expr;

typedef struct {
	pigen_expr_id condition;
	int expected;
} pigen_predicate_atom;

typedef struct {
	size_t first_atom;
	size_t atom_count;
	int impossible;
} pigen_predicate;

typedef enum {
	PIGEN_LVALUE_PROJECTION,
	PIGEN_LVALUE_CONCATENATION
} pigen_lvalue_kind;

typedef struct {
	pigen_lvalue_kind kind;
	pigen_expr_id expression;
	pigen_type_id type;
	pigen_source_span span;
	union {
		struct {
			pigen_symbol_id base_symbol;
			pigen_transport_id transport;
		} projection;
		struct {
			size_t first_child;
			size_t child_count;
		} sequence;
	} as;
} pigen_semantic_lvalue;

typedef struct {
	pigen_syntax_id syntax;
	pigen_symbol_id symbol;
	pigen_scope_id scope;
	pigen_source_span span;
} pigen_semantic_module;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_expr_id value;
	int is_local;
	pigen_source_span span;
} pigen_semantic_parameter;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_type_id type;
	pigen_semantic_value_storage storage;
	pigen_semantic_direction direction;
	pigen_source_span span;
} pigen_semantic_value;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_type_id payload_type;
	pigen_expr_id fifo_depth;
	pigen_semantic_transport_kind kind;
	pigen_semantic_direction direction;
	pigen_clock_domain_id domain;
	pigen_source_span span;
} pigen_semantic_transport;

typedef struct {
	pigen_symbol_id clock_symbol;
	pigen_semantic_edge edge;
} pigen_semantic_clock_domain;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_clock_domain_id domain;
	pigen_expr_id clock;
	pigen_source_span span;
} pigen_semantic_process;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_process_id process;
	pigen_lvalue_id destination;
	pigen_expr_id value;
	pigen_predicate_id guard;
	pigen_clock_domain_id domain;
	size_t first_transport_use;
	size_t transport_use_count;
	pigen_source_span span;
} pigen_semantic_transfer;

typedef enum {
	PIGEN_TRANSFER_CONSUMER = 1u << 0,
	PIGEN_TRANSFER_PRODUCER = 1u << 1
} pigen_transfer_transport_role;

typedef struct {
	pigen_transport_id transport;
	unsigned roles;
} pigen_transfer_transport_use;

typedef struct {
	const pigen_source_manager *sources;
	pigen_semantic_type *types;
	size_t type_count;
	size_t type_capacity;
	pigen_packed_dimension *dimensions;
	size_t dimension_count;
	size_t dimension_capacity;
	pigen_scope *scopes;
	size_t scope_count;
	size_t scope_capacity;
	pigen_symbol *symbols;
	size_t symbol_count;
	size_t symbol_capacity;
	pigen_semantic_expr *expressions;
	size_t expression_count;
	size_t expression_capacity;
	pigen_expr_id *expression_children;
	size_t expression_child_count;
	size_t expression_child_capacity;
	pigen_const_expr *constant_expressions;
	size_t constant_expression_count;
	size_t constant_expression_capacity;
	pigen_const_expr_id *constant_expression_children;
	size_t constant_expression_child_count;
	size_t constant_expression_child_capacity;
	pigen_bit_state *literal_states;
	size_t literal_state_count;
	size_t literal_state_capacity;
	pigen_predicate *predicates;
	size_t predicate_count;
	size_t predicate_capacity;
	pigen_predicate_atom *predicate_atoms;
	size_t predicate_atom_count;
	size_t predicate_atom_capacity;
	pigen_semantic_lvalue *lvalues;
	size_t lvalue_count;
	size_t lvalue_capacity;
	pigen_lvalue_id *lvalue_children;
	size_t lvalue_child_count;
	size_t lvalue_child_capacity;
	pigen_semantic_module *modules;
	size_t module_count;
	size_t module_capacity;
	pigen_semantic_parameter *parameters;
	size_t parameter_count;
	size_t parameter_capacity;
	pigen_semantic_value *values;
	size_t value_count;
	size_t value_capacity;
	pigen_semantic_transport *transports;
	size_t transport_count;
	size_t transport_capacity;
	pigen_semantic_clock_domain *clock_domains;
	size_t clock_domain_count;
	size_t clock_domain_capacity;
	pigen_semantic_process *processes;
	size_t process_count;
	size_t process_capacity;
	pigen_semantic_transfer *transfers;
	size_t transfer_count;
	size_t transfer_capacity;
	pigen_transfer_transport_use *transfer_transport_uses;
	size_t transfer_transport_use_count;
	size_t transfer_transport_use_capacity;
	pigen_scope_id compilation_scope;
	pigen_type_id integer_type;
	pigen_type_id boolean_result_type;
	pigen_predicate_id true_predicate;
	pigen_predicate_id false_predicate;
} pigen_semantic_model;

typedef enum {
	PIGEN_DECLARE_INVALID,
	PIGEN_DECLARE_OK,
	PIGEN_DECLARE_DUPLICATE
} pigen_declare_result;

void pigen_semantic_init(pigen_semantic_model *model,
	const pigen_source_manager *sources);
pigen_type_id pigen_type_intern(pigen_semantic_model *model,
	pigen_semantic_type_kind kind, pigen_signedness signedness,
	pigen_symbol_id named_symbol, const pigen_packed_dimension *dimensions,
	size_t dimension_count);
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
pigen_const_expr_id pigen_const_expr_intern_integer(
	pigen_semantic_model *model, uint64_t value, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_symbol(
	pigen_semantic_model *model, pigen_symbol_id symbol, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_unary(
	pigen_semantic_model *model, pigen_unary_operator operator,
	pigen_const_expr_id operand, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_binary(
	pigen_semantic_model *model, pigen_binary_operator operator,
	pigen_const_expr_id left, pigen_const_expr_id right, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_conditional(
	pigen_semantic_model *model, pigen_const_expr_id condition,
	pigen_const_expr_id when_true, pigen_const_expr_id when_false,
	pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_index(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id index, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_select(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id left, pigen_const_expr_id right,
	pigen_select_kind kind, pigen_type_id type);
pigen_const_expr_id pigen_const_expr_intern_select_width(
	pigen_semantic_model *model, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind);
pigen_const_expr_id pigen_const_expr_intern_concatenation(
	pigen_semantic_model *model, const pigen_const_expr_id *children,
	size_t count, pigen_type_id type);
const pigen_const_expr_id *pigen_const_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count);
const pigen_const_expr *pigen_const_expr_get(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
const pigen_bit_state *pigen_const_expr_bits(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
pigen_expr_id pigen_expr_add_integer(pigen_semantic_model *model,
	uint64_t value, pigen_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_type_id type,
	pigen_source_span span);
pigen_expr_id pigen_expr_add_symbol(pigen_semantic_model *model,
	pigen_symbol_id symbol, pigen_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_group(pigen_semantic_model *model,
	pigen_expr_id operand, pigen_source_span span);
pigen_expr_id pigen_expr_add_unary(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_expr_id operand,
	pigen_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_binary(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_expr_id left,
	pigen_expr_id right, pigen_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_conditional(pigen_semantic_model *model,
	pigen_expr_id condition, pigen_expr_id when_true, pigen_expr_id when_false,
	pigen_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_index(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id index, pigen_source_span span);
pigen_expr_id pigen_expr_add_select(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id left, pigen_expr_id right,
	pigen_select_kind kind, pigen_source_span span);
pigen_expr_id pigen_expr_add_concatenation(pigen_semantic_model *model,
	const pigen_expr_id *children, size_t count, pigen_source_span span);
const pigen_expr_id *pigen_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count);
const pigen_semantic_expr *pigen_expr_get(const pigen_semantic_model *model,
	pigen_expr_id expression);
pigen_const_expr_id pigen_expr_constant(const pigen_semantic_model *model,
	pigen_expr_id expression);
pigen_lvalue_id pigen_lvalue_resolve(pigen_semantic_model *model,
	pigen_expr_id expression);
const pigen_semantic_lvalue *pigen_lvalue_get(
	const pigen_semantic_model *model, pigen_lvalue_id lvalue);
const pigen_lvalue_id *pigen_lvalue_children(
	const pigen_semantic_model *model, size_t first, size_t count);
pigen_scope_id pigen_scope_add(pigen_semantic_model *model,
	pigen_scope_id parent, pigen_source_span span);
const pigen_scope *pigen_scope_get(const pigen_semantic_model *model,
	pigen_scope_id scope);
pigen_declare_result pigen_symbol_declare(pigen_semantic_model *model,
	pigen_scope_id scope, pigen_symbol_kind kind, pigen_type_id type,
	pigen_source_span name, pigen_source_span declaration,
	pigen_symbol_id *declared, pigen_symbol_id *shadowed);
pigen_symbol_id pigen_symbol_lookup(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name);
const pigen_symbol *pigen_symbol_get(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_module_id pigen_module_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_symbol_id symbol, pigen_scope_id scope,
	pigen_source_span span);
pigen_parameter_id pigen_parameter_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_expr_id value, int is_local, pigen_source_span span);
pigen_value_id pigen_value_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_type_id type, pigen_semantic_value_storage storage,
	pigen_semantic_direction direction, pigen_source_span span);
pigen_transport_id pigen_transport_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_type_id payload_type, pigen_expr_id fifo_depth,
	pigen_semantic_transport_kind kind, pigen_semantic_direction direction,
	pigen_source_span span);
int pigen_transport_bind_domain(pigen_semantic_model *model,
	pigen_transport_id transport, pigen_clock_domain_id domain);
pigen_clock_domain_id pigen_clock_domain_intern(
	pigen_semantic_model *model, pigen_symbol_id clock_symbol,
	pigen_semantic_edge edge);
pigen_process_id pigen_process_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	pigen_clock_domain_id domain, pigen_expr_id clock,
	pigen_source_span span);
pigen_transfer_id pigen_transfer_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_process_id process,
	pigen_lvalue_id destination, pigen_expr_id value,
	pigen_predicate_id guard, pigen_clock_domain_id domain,
	const pigen_transfer_transport_use *transport_uses,
	size_t transport_use_count,
	pigen_source_span span);
pigen_module_id pigen_symbol_module(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_parameter_id pigen_symbol_parameter(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_value_id pigen_symbol_value(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_transport_id pigen_symbol_transport(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
const pigen_semantic_module *pigen_module_get(const pigen_semantic_model *model,
	pigen_module_id module);
const pigen_semantic_parameter *pigen_parameter_get(
	const pigen_semantic_model *model, pigen_parameter_id parameter);
const pigen_semantic_value *pigen_value_get(
	const pigen_semantic_model *model, pigen_value_id value);
const pigen_semantic_transport *pigen_transport_get(
	const pigen_semantic_model *model, pigen_transport_id transport);
const pigen_semantic_clock_domain *pigen_clock_domain_get(
	const pigen_semantic_model *model, pigen_clock_domain_id domain);
const pigen_semantic_process *pigen_process_get(
	const pigen_semantic_model *model, pigen_process_id process);
const pigen_semantic_transfer *pigen_transfer_get(
	const pigen_semantic_model *model, pigen_transfer_id transfer);
const pigen_transfer_transport_use *pigen_transfer_transport_uses(
	const pigen_semantic_model *model, pigen_transfer_id transfer);
void pigen_free_semantic_model(pigen_semantic_model *model);

#endif
