#ifndef PIGEN_SEMANTIC_H
#define PIGEN_SEMANTIC_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/data_type.h"
#include "pigen/operation.h"
#include "pigen/source.h"
#include "pigen/transfer_type.h"

typedef enum {
	PIGEN_SYMBOL_SIGNAL,
	PIGEN_SYMBOL_MODULE,
	PIGEN_SYMBOL_PARAMETER,
	PIGEN_SYMBOL_TYPEDEF,
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
	PIGEN_SEMANTIC_INTERNAL,
	PIGEN_SEMANTIC_INPUT,
	PIGEN_SEMANTIC_OUTPUT,
	PIGEN_SEMANTIC_INOUT
} pigen_semantic_direction;

typedef enum {
	PIGEN_SEMANTIC_POSEDGE,
	PIGEN_SEMANTIC_NEGEDGE
} pigen_semantic_edge;

typedef enum {
	PIGEN_SHAPE_DIMENSION_COUNT,
	PIGEN_SHAPE_DIMENSION_RANGE
} pigen_shape_dimension_form;

typedef struct {
	pigen_shape_dimension_form form;
	union {
		pigen_const_expr_id count;
		struct {
			pigen_const_expr_id left;
			pigen_const_expr_id right;
		} range;
	} as;
} pigen_shape_dimension;

typedef struct {
	size_t first_dimension;
	size_t dimension_count;
} pigen_semantic_shape;

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
	pigen_data_type_id data_type;
	pigen_source_span name;
	pigen_source_span declaration;
	union {
		pigen_module_id module;
		pigen_parameter_id parameter;
		pigen_signal_id signal;
		pigen_pipeline_id pipeline;
		pigen_stage_id stage;
		pigen_fsm_id fsm;
		pigen_fabric_id fabric;
	} object;
} pigen_symbol;

typedef struct {
	pigen_semantic_expr_kind kind;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_source_span span;
	pigen_const_expr_id constant;
	pigen_lvalue_id lvalue;
	union {
		uint64_t integer;
		struct { size_t first_state; size_t state_count; } bits;
		pigen_symbol_id symbol;
		struct { pigen_expr_id operand; } group;
		struct {
			pigen_unary_operation operation;
			pigen_expr_id operand;
		} unary;
		struct {
			pigen_binary_operation operation;
			pigen_expr_id left;
			pigen_expr_id right;
		} binary;
		struct {
			pigen_conditional_operation operation;
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
	pigen_data_type_id data_type;
	union {
		uint64_t integer;
		struct { size_t first_state; size_t state_count; } bits;
		pigen_symbol_id symbol;
		struct {
			pigen_unary_operation operation;
			pigen_const_expr_id operand;
		} unary;
		struct {
			pigen_binary_operation operation;
			pigen_const_expr_id left;
			pigen_const_expr_id right;
		} binary;
		struct {
			pigen_conditional_operation operation;
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
	pigen_data_type_id data_type;
	pigen_source_span span;
	union {
		struct {
			pigen_symbol_id base_symbol;
			pigen_signal_id signal;
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
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_expr_id fifo_depth;
	pigen_transfer_type transfer_type;
	pigen_semantic_direction direction;
	pigen_clock_domain_id domain;
	pigen_source_span span;
} pigen_semantic_signal;

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
	size_t first_signal_use;
	size_t signal_use_count;
	pigen_source_span span;
} pigen_semantic_transfer;

typedef enum {
	PIGEN_TRANSFER_SIGNAL_READ = 1u << 0,
	PIGEN_TRANSFER_SIGNAL_WRITE = 1u << 1,
	PIGEN_TRANSFER_CONSUMER = 1u << 2,
	PIGEN_TRANSFER_PRODUCER = 1u << 3
} pigen_transfer_signal_role;

typedef struct {
	pigen_signal_id signal;
	unsigned roles;
} pigen_transfer_signal_use;

struct pigen_semantic_model {
	const pigen_source_manager *sources;
	pigen_data_type *data_types;
	size_t data_type_count;
	size_t data_type_capacity;
	pigen_packed_dimension *data_type_dimensions;
	size_t data_type_dimension_count;
	size_t data_type_dimension_capacity;
	pigen_semantic_shape *shapes;
	size_t shape_count;
	size_t shape_capacity;
	pigen_shape_dimension *shape_dimensions;
	size_t shape_dimension_count;
	size_t shape_dimension_capacity;
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
	pigen_semantic_signal *signals;
	size_t signal_count;
	size_t signal_capacity;
	pigen_semantic_clock_domain *clock_domains;
	size_t clock_domain_count;
	size_t clock_domain_capacity;
	pigen_semantic_process *processes;
	size_t process_count;
	size_t process_capacity;
	pigen_semantic_transfer *transfers;
	size_t transfer_count;
	size_t transfer_capacity;
	pigen_transfer_signal_use *transfer_signal_uses;
	size_t transfer_signal_use_count;
	size_t transfer_signal_use_capacity;
	pigen_scope_id compilation_scope;
	pigen_data_type_id unsized_integer_data_type;
	pigen_data_type_id boolean_data_type;
	pigen_shape_id scalar_shape;
	pigen_predicate_id true_predicate;
	pigen_predicate_id false_predicate;
};

typedef enum {
	PIGEN_DECLARE_INVALID,
	PIGEN_DECLARE_OK,
	PIGEN_DECLARE_DUPLICATE
} pigen_declare_result;

void pigen_semantic_init(pigen_semantic_model *model,
	const pigen_source_manager *sources);
pigen_shape_id pigen_shape_intern(pigen_semantic_model *model,
	const pigen_shape_dimension *dimensions, size_t dimension_count);
pigen_shape_id pigen_semantic_scalar_shape(pigen_semantic_model *model);
const pigen_semantic_shape *pigen_shape_get(
	const pigen_semantic_model *model, pigen_shape_id shape);
const pigen_shape_dimension *pigen_shape_dimensions(
	const pigen_semantic_model *model, pigen_shape_id shape);
pigen_shape_id pigen_shape_element(pigen_semantic_model *model,
	pigen_shape_id shape);
pigen_const_expr_id pigen_const_expr_intern_integer(
	pigen_semantic_model *model, uint64_t value, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_symbol(
	pigen_semantic_model *model, pigen_symbol_id symbol, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_unary(
	pigen_semantic_model *model, pigen_unary_operation operation,
	pigen_const_expr_id operand);
pigen_const_expr_id pigen_const_expr_intern_binary(
	pigen_semantic_model *model, pigen_binary_operation operation,
	pigen_const_expr_id left, pigen_const_expr_id right);
pigen_const_expr_id pigen_const_expr_intern_conditional(
	pigen_semantic_model *model, pigen_conditional_operation operation,
	pigen_const_expr_id condition,
	pigen_const_expr_id when_true, pigen_const_expr_id when_false);
pigen_const_expr_id pigen_const_expr_intern_index(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id index, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_select(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id left, pigen_const_expr_id right,
	pigen_select_kind kind, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_select_width(
	pigen_semantic_model *model, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind);
pigen_const_expr_id pigen_const_expr_intern_concatenation(
	pigen_semantic_model *model, const pigen_const_expr_id *children,
	size_t count, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_width_sum(
	pigen_semantic_model *model, const pigen_const_expr_id *terms,
	size_t count);
pigen_const_expr_id pigen_const_expr_intern_width_product(
	pigen_semantic_model *model, const pigen_const_expr_id *factors,
	size_t count);
const pigen_const_expr_id *pigen_const_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count);
const pigen_const_expr *pigen_const_expr_get(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
const pigen_bit_state *pigen_const_expr_bits(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
pigen_expr_id pigen_expr_add_integer(pigen_semantic_model *model,
	uint64_t value, pigen_data_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_data_type_id type,
	pigen_source_span span);
pigen_expr_id pigen_expr_add_symbol(pigen_semantic_model *model,
	pigen_symbol_id symbol, pigen_data_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_group(pigen_semantic_model *model,
	pigen_expr_id operand, pigen_source_span span);
pigen_expr_id pigen_expr_add_unary(pigen_semantic_model *model,
	pigen_unary_operation operation, pigen_expr_id operand,
	pigen_source_span span);
pigen_expr_id pigen_expr_add_binary(pigen_semantic_model *model,
	pigen_binary_operation operation, pigen_expr_id left,
	pigen_expr_id right, pigen_source_span span);
pigen_expr_id pigen_expr_add_conditional(pigen_semantic_model *model,
	pigen_conditional_operation operation, pigen_expr_id condition,
	pigen_expr_id when_true, pigen_expr_id when_false, pigen_source_span span);
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
	pigen_scope_id scope, pigen_symbol_kind kind, pigen_data_type_id type,
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
pigen_signal_id pigen_signal_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_data_type_id data_type, pigen_shape_id shape, pigen_expr_id fifo_depth,
	pigen_transfer_type transfer_type, pigen_semantic_direction direction,
	pigen_source_span span);
int pigen_signal_bind_domain(pigen_semantic_model *model,
	pigen_signal_id signal, pigen_clock_domain_id domain);
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
	const pigen_transfer_signal_use *signal_uses,
	size_t signal_use_count,
	pigen_source_span span);
pigen_module_id pigen_symbol_module(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_parameter_id pigen_symbol_parameter(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_signal_id pigen_symbol_signal(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
const pigen_semantic_module *pigen_module_get(const pigen_semantic_model *model,
	pigen_module_id module);
const pigen_semantic_parameter *pigen_parameter_get(
	const pigen_semantic_model *model, pigen_parameter_id parameter);
const pigen_semantic_signal *pigen_signal_get(
	const pigen_semantic_model *model, pigen_signal_id signal);
const pigen_semantic_clock_domain *pigen_clock_domain_get(
	const pigen_semantic_model *model, pigen_clock_domain_id domain);
const pigen_semantic_process *pigen_process_get(
	const pigen_semantic_model *model, pigen_process_id process);
const pigen_semantic_transfer *pigen_transfer_get(
	const pigen_semantic_model *model, pigen_transfer_id transfer);
const pigen_transfer_signal_use *pigen_transfer_signal_uses(
	const pigen_semantic_model *model, pigen_transfer_id transfer);
void pigen_free_semantic_model(pigen_semantic_model *model);

#endif
