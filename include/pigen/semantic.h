#ifndef PIGEN_SEMANTIC_H
#define PIGEN_SEMANTIC_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/data_type.h"
#include "pigen/integer.h"
#include "pigen/operation.h"
#include "pigen/protocol.h"
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
	PIGEN_SYMBOL_FABRIC,
	PIGEN_SYMBOL_INSTANCE,
	PIGEN_SYMBOL_PROTOCOL_ENDPOINT,
	PIGEN_SYMBOL_PROTOCOL_ARRAY,
	PIGEN_SYMBOL_GENVAR,
	PIGEN_SYMBOL_PROCEDURAL_SCOPE,
	PIGEN_SYMBOL_PROTOCOL_TYPE,
	PIGEN_SYMBOL_PACKAGE,
	PIGEN_SYMBOL_RETAINED_SUBROUTINE,
	PIGEN_SYMBOL_RETAINED_LOCAL
} pigen_symbol_kind;

typedef enum {
	PIGEN_EXPR_INTEGER,
	PIGEN_EXPR_EXACT_INTEGER,
	PIGEN_EXPR_BITS,
	PIGEN_EXPR_SYMBOL,
	PIGEN_EXPR_GROUP,
	PIGEN_EXPR_UNARY,
	PIGEN_EXPR_BINARY,
	PIGEN_EXPR_CONDITIONAL,
	PIGEN_EXPR_CONVERSION,
	PIGEN_EXPR_INDEX,
	PIGEN_EXPR_SELECT,
	PIGEN_EXPR_CONCATENATION,
	PIGEN_EXPR_ACCEPTS,
	PIGEN_EXPR_VALID,
	PIGEN_EXPR_READY,
	PIGEN_EXPR_PEEK,
	PIGEN_EXPR_EVALUATED_VALUE,
	PIGEN_EXPR_ARRAY_PATTERN
} pigen_semantic_expr_kind;

typedef enum {
	PIGEN_CONST_EXPR_INTEGER,
	PIGEN_CONST_EXPR_EXACT_INTEGER,
	PIGEN_CONST_EXPR_BITS,
	PIGEN_CONST_EXPR_SYMBOL,
	PIGEN_CONST_EXPR_UNARY,
	PIGEN_CONST_EXPR_BINARY,
	PIGEN_CONST_EXPR_CONDITIONAL,
	PIGEN_CONST_EXPR_CONVERSION,
	PIGEN_CONST_EXPR_INDEX,
	PIGEN_CONST_EXPR_SELECT,
	PIGEN_CONST_EXPR_SELECT_WIDTH,
	PIGEN_CONST_EXPR_CONCATENATION,
	PIGEN_CONST_EXPR_WIDTH_SUM,
	PIGEN_CONST_EXPR_WIDTH_PRODUCT,
	PIGEN_CONST_EXPR_WIDTH_MAXIMUM,
	PIGEN_CONST_EXPR_NUMERICAL_RANGE_WIDTH
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
	int is_procedural; /* Private static block locals need distinct RTL names. */
} pigen_scope;

typedef struct {
	pigen_symbol_kind kind;
	pigen_scope_id scope;
	pigen_symbol_id previous_in_scope;
	pigen_data_type_id data_type;
	pigen_source_span name;
	pigen_source_span declaration;
	pigen_symbol_id alias_of; /* Lexical binding to a canonical procedural symbol. */
	union {
		pigen_module_id module;
		pigen_parameter_id parameter;
		pigen_signal_id signal;
		pigen_pipeline_id pipeline;
		pigen_stage_id stage;
		pigen_fsm_id fsm;
		pigen_fabric_id fabric;
		pigen_instance_id instance;
		pigen_protocol_endpoint_id protocol_endpoint;
		pigen_protocol_array_id protocol_array;
		pigen_protocol_type_id protocol_type;
		pigen_scope_id scope;
	} object;
} pigen_symbol;

typedef struct {
	pigen_semantic_expr_kind kind;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_source_span span;
	pigen_const_expr_id constant;
	pigen_lvalue_id lvalue;
	int is_context_cast; /* Explicit cast boundary in a case operand. */
	int unbased_fill; /* 1 + bit state, or zero for a normal value. */
	union {
		uint64_t integer;
		pigen_integer_id exact_integer;
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
			pigen_conversion conversion;
			pigen_expr_id operand;
		} conversion;
		struct {
			pigen_expr_id base;
			pigen_expr_id index;
		} index;
		struct {
			pigen_expr_id base;
			pigen_expr_id left;
			pigen_expr_id right;
			pigen_select_kind kind;
			int is_member;
			pigen_source_span member_name;
		} select;
		struct {
			size_t first_child;
			size_t child_count;
		} sequence;
		struct { pigen_expr_id value, evaluated; int before; } evaluation;
		struct {
			pigen_lvalue_id destination;
			pigen_expr_id source;
			pigen_signal_id source_signal;
		} accepts;
		struct {
			pigen_signal_id signal;
		} control;
		struct {
			pigen_expr_id source;
		} peek;
	} as;
} pigen_semantic_expr;

typedef struct {
	pigen_const_expr_kind kind;
	pigen_data_type_id data_type;
	union {
		uint64_t integer;
		pigen_integer_id exact_integer;
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
			pigen_conversion conversion;
			pigen_const_expr_id operand;
		} conversion;
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
		pigen_numerical_range_width numerical_range_width;
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
	PIGEN_LVALUE_CONCATENATION,
	PIGEN_LVALUE_PLACEHOLDER
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
	/* A selected instance may elaborate a fresh module from this template. */
	pigen_module_id specialization_of;
	pigen_module_id specialization_parent;
	int is_external;
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
	pigen_syntax_id control;
	pigen_module_id module;
	pigen_scope_id scope;
	pigen_source_span name;
	/* Owned external-interface metadata; never an authored scope name. */
	char *external_name;
	size_t external_ordinal;
	pigen_symbol_id iterator;
	int is_iteration;
	int64_t index;
	pigen_source_span span;
} pigen_semantic_generate_scope;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_scope_id scope;
	pigen_generate_scope_id owner;
} pigen_semantic_generate_item;

typedef struct {
	pigen_symbol_id owner;
	pigen_source_span name;
	pigen_data_type_id data_type;
	size_t offset;
} pigen_semantic_struct_member;

/* A packed record declaration has identity even without an authored typedef.
 * Token extent distinguishes several records in one macro expansion span. */
typedef struct {
	pigen_scope_id scope;
	pigen_source_span declaration;
	pigen_token_id first, after;
	pigen_data_type_id data_type;
} pigen_semantic_record_type;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_data_type_id data_type;
	pigen_shape_id shape;
	pigen_expr_id transfer_argument;
	pigen_transfer_type transfer_type;
	pigen_semantic_direction direction;
	pigen_clock_domain_id domain;
	pigen_source_span span;
	pigen_expr_id initial_value; /* Static declaration initialization, before processes. */
	int is_automatic;
	pigen_expr_id entry_value; /* Explicit automatic initializer, executed on entry. */
} pigen_semantic_signal;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_protocol_type_id type;
	pigen_protocol_role role;
	int is_port;
	int is_instance_view;
	pigen_instance_id instance;
	pigen_protocol_endpoint_id child_endpoint;
	pigen_signal_id channels[PIGEN_CHANNEL_COUNT];
	struct {
		int is_bound;
		pigen_clock_domain_id domain;
		pigen_symbol_id reset_symbol;
		int reset_active_low;
	} binding;
	pigen_source_span span;
} pigen_semantic_protocol_endpoint;

typedef struct {
	int64_t left;
	int64_t right;
	size_t count;
} pigen_protocol_array_dimension;

/* A structural collection of independently handshaken scalar endpoints.
 * Elements are stored in row-major declaration order, left bound first. */
typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_protocol_type_id type;
	pigen_protocol_role role;
	int is_port;
	pigen_shape_id shape;
	size_t first_dimension;
	size_t dimension_count;
	size_t first_element;
	size_t element_count;
	pigen_source_span span;
} pigen_semantic_protocol_array;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_protocol_endpoint_id source;
	pigen_protocol_endpoint_id destination;
	int direct;
	pigen_source_span span;
	int has_window;
	uint64_t address_base,address_limit;
} pigen_semantic_protocol_fabric_edge;

typedef struct {
	pigen_symbol_id clock_symbol;
	pigen_semantic_edge edge;
} pigen_semantic_clock_domain;

typedef struct {
	pigen_const_expr_id width;
	size_t maximum_bits;
	pigen_source_span span;
} pigen_width_constraint;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_clock_domain_id domain;
	pigen_expr_id clock;
	pigen_source_span span;
	int is_continuous; /* Static, ungated combinational assignments. */
	int is_combinational; /* Ordered blocking statements, no transfer clock. */
	pigen_procedural_id body;
} pigen_semantic_process;

typedef enum {
	PIGEN_PROCEDURAL_BLOCK,
	PIGEN_PROCEDURAL_IF,
	PIGEN_PROCEDURAL_ASSIGN,
	PIGEN_PROCEDURAL_CASE,
	PIGEN_PROCEDURAL_CASEZ,
	PIGEN_PROCEDURAL_CASEX,
	PIGEN_PROCEDURAL_CASE_ITEM,
	PIGEN_PROCEDURAL_WHILE,
	PIGEN_PROCEDURAL_REPEAT,
	PIGEN_PROCEDURAL_FOR,
	PIGEN_PROCEDURAL_DO_WHILE,
	PIGEN_PROCEDURAL_FOREVER,
	PIGEN_PROCEDURAL_BREAK,
	PIGEN_PROCEDURAL_CONTINUE
} pigen_procedural_kind;

typedef struct {
	pigen_procedural_kind kind;
	pigen_process_id process;
	pigen_expr_id condition;
	pigen_transfer_id assignment;
	size_t first_child, child_count;
	pigen_source_span span;
	/* This label continues the preceding comma-separated case item. */
	int case_continuation;
	pigen_case_qualifier case_qualifier;
	int else_if;
} pigen_semantic_procedural;


typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_symbol_id symbol;
	pigen_module_id child_module;
	pigen_source_span span;
	size_t first_connection;
	size_t connection_count;
} pigen_semantic_instance;

typedef struct {
	pigen_symbol_id port_symbol;
	pigen_signal_id port_signal;
	pigen_source_span port_name;
	pigen_expr_id value;
	pigen_semantic_direction direction;
	pigen_source_span span;
	/* Typed child-port view when explicit wiring uses static parent nets. */
	pigen_signal_id endpoint_view;
	int endpoint_view_driven; /* An automatic producer drives this input view. */
} pigen_semantic_instance_connection;

typedef enum {
	PIGEN_ACTION_VALIDATE,
	PIGEN_ACTION_INVALIDATE,
	PIGEN_ACTION_FLUSH
} pigen_action_kind;

typedef struct {
	pigen_syntax_id syntax;
	pigen_module_id module;
	pigen_process_id process;
	pigen_clock_domain_id domain;
	pigen_signal_id signal;
	pigen_action_kind action;
	pigen_predicate_id guard;
	pigen_source_span span;
} pigen_semantic_action;

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

typedef struct {
	int negative;
	size_t first_limb;
	size_t limb_count;
} pigen_integer;

typedef struct {
 pigen_scope_id scope,package_scope;
 pigen_symbol_id target;
 int wildcard; /* 1: candidate package; 0: explicit; 2: activated wildcard name. */
 size_t at; /* Expanded-token order; SIZE_MAX is an end-of-scope API use. */
 pigen_source_span span;
} pigen_semantic_import;

/* Canonical namespace choice for a retained SV callee token. */
typedef struct {
	pigen_token_id name;
	pigen_symbol_id package,target;
	int bare;
} pigen_semantic_retained_call;

/* Read-only channel payload observation in retained ordinary SV. */
typedef struct {
	pigen_module_id module;
	pigen_signal_id signal;
	size_t first,after;
	pigen_expr_id value;
} pigen_semantic_retained_observation;


struct pigen_semantic_model {
	const pigen_source_manager *sources;
	pigen_integer *integers;
	size_t integer_count;
	size_t integer_capacity;
	uint32_t *integer_limbs;
	size_t integer_limb_count;
	size_t integer_limb_capacity;
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
	pigen_semantic_import *imports;
	size_t import_count,import_capacity;
	pigen_semantic_retained_call *retained_calls;
	size_t retained_call_count,retained_call_capacity;
	pigen_semantic_retained_observation *retained_observations;
	size_t retained_observation_count;
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
	pigen_semantic_struct_member *struct_members;
	size_t struct_member_count;
	size_t struct_member_capacity;
	pigen_semantic_record_type *record_types;
	size_t record_type_count, record_type_capacity;
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
	pigen_semantic_generate_scope *generate_scopes;
	size_t generate_scope_count;
	size_t generate_scope_capacity;
	pigen_semantic_generate_item *generate_items;
	size_t generate_item_count;
	size_t generate_item_capacity;
	pigen_semantic_signal *signals;
	size_t signal_count;
	size_t signal_capacity;
	pigen_protocol *protocol_types;
	size_t protocol_type_count;
	size_t protocol_type_capacity;
	pigen_semantic_protocol_endpoint *protocol_endpoints;
	size_t protocol_endpoint_count;
	size_t protocol_endpoint_capacity;
	pigen_semantic_protocol_array *protocol_arrays;
	size_t protocol_array_count;
	size_t protocol_array_capacity;
	pigen_protocol_array_dimension *protocol_array_dimensions;
	size_t protocol_array_dimension_count;
	size_t protocol_array_dimension_capacity;
	pigen_protocol_endpoint_id *protocol_array_elements;
	size_t protocol_array_element_count;
	size_t protocol_array_element_capacity;
	pigen_semantic_protocol_fabric_edge *protocol_fabric_edges;
	size_t protocol_fabric_edge_count;
	size_t protocol_fabric_edge_capacity;
	pigen_semantic_clock_domain *clock_domains;
	size_t clock_domain_count;
	size_t clock_domain_capacity;
	pigen_semantic_procedural *procedural;
	size_t procedural_count, procedural_capacity;
	pigen_procedural_id *procedural_children;
	size_t procedural_child_count, procedural_child_capacity;
	pigen_semantic_process *processes;
	size_t process_count;
	size_t process_capacity;
	pigen_semantic_transfer *transfers;
	size_t transfer_count;
	size_t transfer_capacity;
	pigen_semantic_action *actions;
	size_t action_count;
	size_t action_capacity;
	pigen_semantic_instance *instances;
	size_t instance_count;
	size_t instance_capacity;
	pigen_semantic_instance_connection *instance_connections;
	size_t instance_connection_count;
	size_t instance_connection_capacity;
	pigen_transfer_signal_use *transfer_signal_uses;
	size_t transfer_signal_use_count;
	size_t transfer_signal_use_capacity;
	pigen_process_id **module_process_buffers;
	size_t module_process_buffer_count;
	pigen_transfer_id **process_transfer_buffers;
	size_t process_transfer_buffer_count;
	pigen_width_constraint *width_constraints;
	size_t width_constraint_count;
	size_t width_constraint_capacity;
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
/* Fixed bounds in declaration order; rejects symbolic/overflowing extents. */
int pigen_shape_dimension_bounds(pigen_semantic_model *model,pigen_shape_id shape,
	size_t dimension,int64_t *left,int64_t *right,size_t *count);
pigen_shape_id pigen_shape_element(pigen_semantic_model *model,
	pigen_shape_id shape);
pigen_const_expr_id pigen_const_expr_intern_integer(
	pigen_semantic_model *model, uint64_t value, pigen_data_type_id type);
pigen_const_expr_id pigen_const_expr_intern_exact_integer(
	pigen_semantic_model *model, pigen_integer_id value,
	pigen_data_type_id data_type);
pigen_const_expr_id pigen_const_expr_intern_conversion(
	pigen_semantic_model *model, pigen_conversion conversion,
	pigen_const_expr_id operand);
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
pigen_const_expr_id pigen_const_expr_intern_width_maximum(
	pigen_semantic_model *model, const pigen_const_expr_id *values,
	size_t count);
pigen_const_expr_id pigen_const_expr_intern_numerical_range_width(
	pigen_semantic_model *model, pigen_numerical_range_width range);
const pigen_const_expr_id *pigen_const_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count);
const pigen_const_expr *pigen_const_expr_get(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
const pigen_bit_state *pigen_const_expr_bits(
	const pigen_semantic_model *model, pigen_const_expr_id expression);
int pigen_const_expr_evaluate_u64(pigen_semantic_model *model,
	pigen_const_expr_id expression, uint64_t *value);
/* Signed coordinate evaluation; unknown, unsupported or overflowing values fail. */
int pigen_const_expr_evaluate_i64(pigen_semantic_model *model,
	pigen_const_expr_id expression, int64_t *value);
/* One in-range bit of a concrete integral constant; preserves X/Z. */
int pigen_const_expr_evaluate_bit(pigen_semantic_model *model,
	pigen_const_expr_id expression, uint64_t bit, pigen_bit_state *state);
/* Exact four-state comparison after case-wide sizing. Unsupported constant
 * operations fail; unknown bits are data, never an evaluation failure. */
int pigen_const_expr_case_equal(pigen_semantic_model *model,
	pigen_const_expr_id left,pigen_const_expr_id right,size_t width,
	int signed_comparison,int *equal);
int pigen_const_expr_is_symbolic(const pigen_semantic_model *model,
	pigen_const_expr_id expression);
pigen_const_expr_id pigen_const_expr_normalize_count(
	pigen_semantic_model *model, pigen_const_expr_id value);
int pigen_width_constraint_add(pigen_semantic_model *model,
	pigen_const_expr_id width, size_t maximum_bits, pigen_source_span span);
const pigen_width_constraint *pigen_width_constraint_get(
	const pigen_semantic_model *model, size_t index);
pigen_expr_id pigen_expr_add_integer(pigen_semantic_model *model,
	uint64_t value, pigen_data_type_id type, pigen_source_span span);
pigen_expr_id pigen_expr_add_exact_integer(pigen_semantic_model *model,
	pigen_integer_id value, pigen_source_span span);
pigen_expr_id pigen_expr_add_conversion(pigen_semantic_model *model,
	pigen_conversion conversion, pigen_expr_id operand,
	pigen_source_span span);
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
/* Preserve the analysis-owned member type for an equal-width/state projection. */
pigen_expr_id pigen_expr_add_typed_select(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id left, pigen_expr_id right,
	pigen_select_kind kind, pigen_data_type_id result_type, pigen_source_span span);
pigen_expr_id pigen_expr_add_member_select(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id left, pigen_expr_id right,
	pigen_data_type_id result_type, pigen_source_span name, pigen_source_span span);
/* Preserve an evaluated operand contributing no result bits. Never constant-fold. */
pigen_expr_id pigen_expr_add_evaluated_value(pigen_semantic_model *model,
	pigen_expr_id value,pigen_expr_id evaluated,int before,pigen_source_span span);
/* One converted child per immediate unpacked element; never packed bits. */
pigen_expr_id pigen_expr_add_array_pattern(pigen_semantic_model *model,
	pigen_data_type_id type,pigen_shape_id shape,const pigen_expr_id *children,
	size_t count,pigen_source_span span);
pigen_expr_id pigen_expr_add_concatenation(pigen_semantic_model *model,
	const pigen_expr_id *children, size_t count, pigen_source_span span);
pigen_expr_id pigen_expr_add_accepts(pigen_semantic_model *model,
	pigen_lvalue_id destination, pigen_expr_id source,
	pigen_signal_id source_signal, pigen_source_span span);
pigen_expr_id pigen_expr_add_control(pigen_semantic_model *model,
	pigen_semantic_expr_kind kind, pigen_signal_id signal,
	pigen_source_span span);
pigen_expr_id pigen_expr_add_peek(pigen_semantic_model *model,
	pigen_expr_id source, pigen_source_span span);
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
/* Structural writability: projections/concatenations must not target inputs. */
int pigen_lvalue_is_writable(const pigen_semantic_model *model,
	pigen_lvalue_id lvalue);
pigen_lvalue_id pigen_lvalue_add_placeholder(
	pigen_semantic_model *model, pigen_data_type_id data_type,
	pigen_source_span span);
pigen_lvalue_id pigen_lvalue_add_concatenation(
	pigen_semantic_model *model, const pigen_lvalue_id *children,
	size_t child_count, pigen_data_type_id data_type,
	pigen_source_span span);
int pigen_struct_member_add(pigen_semantic_model *model,
	pigen_symbol_id owner, pigen_source_span name,
	pigen_data_type_id data_type, size_t offset);
const pigen_semantic_struct_member *pigen_struct_member_find(
	const pigen_semantic_model *model, pigen_symbol_id owner,
	pigen_source_span name);
/* Resolve a packed record member through typedef alias chains. */
const pigen_semantic_struct_member *pigen_struct_member_for_type(
	const pigen_semantic_model *model, pigen_data_type_id type,
	pigen_source_span name);
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
/* Bind the same static declaration in another specialization of its process.
 * Both scopes must share a procedural root; lookup returns the canonical ID. */
pigen_declare_result pigen_symbol_bind_procedural_alias(pigen_semantic_model *model,
	pigen_scope_id scope,pigen_symbol_id target,pigen_symbol_id *declared);
pigen_symbol_id pigen_symbol_lookup(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name);
/* Pure source-position namespace probes; wildcard candidates are not activated. */
pigen_symbol_id pigen_symbol_lookup_at(const pigen_semantic_model *model,
 pigen_scope_id scope,pigen_source_span name,size_t at);
pigen_symbol_id pigen_symbol_lookup_text_at(const pigen_semantic_model *model,
 pigen_scope_id scope,const char *text,size_t length,size_t at);
/* Direct members only, following canonical aliases without outward lookup. */
pigen_symbol_id pigen_symbol_lookup_local(const pigen_semantic_model *model,
	pigen_scope_id scope,pigen_source_span name);
pigen_declare_result pigen_import_package(pigen_semantic_model *model,pigen_scope_id scope,
 pigen_symbol_id package,pigen_source_span member,int wildcard,size_t at,pigen_source_span span);
/* Export only actual imports; a named export also activates a candidate. An
 * invalid package with wildcard=1 denotes export *::* . */
pigen_declare_result pigen_export_package(pigen_semantic_model *model,
 pigen_scope_id scope,pigen_symbol_id package,pigen_source_span member,
 int wildcard,size_t at,pigen_source_span span);

/* Resolving a use activates a wildcard candidate; pure lookup does not. */
pigen_symbol_id pigen_symbol_resolve(pigen_semantic_model *model,pigen_scope_id scope,pigen_source_span name);
pigen_symbol_id pigen_symbol_resolve_at(pigen_semantic_model *model,pigen_scope_id scope,pigen_source_span name,size_t at);
pigen_symbol_id pigen_symbol_resolve_text_at(pigen_semantic_model *model,pigen_scope_id scope,const char *text,size_t length,size_t at);
/* Retained SV calls see bound names through each scope's end, but wildcard
 * candidates only before the call. Publishes activation at the call position. */
pigen_symbol_id pigen_symbol_resolve_call_text_at(pigen_semantic_model *model,
 pigen_scope_id scope,const char *text,size_t length,size_t at,int *ambiguous);
int pigen_retained_call_bind(pigen_semantic_model *model,pigen_token_id name,pigen_symbol_id target,int bare);
int pigen_symbol_import_ambiguous(const pigen_semantic_model *model,pigen_scope_id scope,pigen_source_span name,size_t at);
int pigen_symbol_bind_package_scope(pigen_semantic_model *model,
 pigen_symbol_id symbol,pigen_scope_id scope);
pigen_symbol_id pigen_symbol_lookup_package_member(const pigen_semantic_model *model,
 pigen_scope_id scope,pigen_source_span package,pigen_source_span member);
int pigen_symbol_bind_procedural_scope(pigen_semantic_model *model,
	pigen_symbol_id symbol,pigen_scope_id scope);
pigen_symbol_id pigen_symbol_lookup_text(pigen_semantic_model *model,
	pigen_scope_id scope, const char *text, size_t length);
const pigen_symbol *pigen_symbol_get(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_module_id pigen_module_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_symbol_id symbol, pigen_scope_id scope,
	pigen_source_span span);
pigen_parameter_id pigen_parameter_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_expr_id value, int is_local, pigen_source_span span);
pigen_generate_scope_id pigen_generate_scope_add(pigen_semantic_model *model,
	const pigen_semantic_generate_scope *scope);
const pigen_semantic_generate_scope *pigen_generate_scope_get(
	const pigen_semantic_model *model, pigen_generate_scope_id scope);
int pigen_generate_item_add(pigen_semantic_model *model,
	const pigen_semantic_generate_item *item);
pigen_signal_id pigen_signal_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_data_type_id data_type, pigen_shape_id shape,
	pigen_expr_id transfer_argument,
	pigen_transfer_type transfer_type, pigen_semantic_direction direction,
	pigen_source_span span);
int pigen_signal_bind_domain(pigen_semantic_model *model,
	pigen_signal_id signal, pigen_clock_domain_id domain);
/* A native type alias pairs the payload data type with a full field contract. */
int pigen_symbol_bind_protocol_type(pigen_semantic_model *model,
	pigen_symbol_id symbol,pigen_protocol_type_id type);
pigen_protocol_type_id pigen_symbol_protocol_type(const pigen_semantic_model *model,
	pigen_symbol_id symbol);
pigen_protocol_type_id pigen_protocol_type_intern(pigen_semantic_model *model,
	const pigen_protocol *protocol);
const pigen_protocol *pigen_protocol_type_get(const pigen_semantic_model *model,
	pigen_protocol_type_id type);
/* Channels retain their own storage, direction, shape and handshake identities.
 * Supply INVALID for channels outside the protocol. A payload-free channel uses
 * a one-bit carrier signal for its handshake; it has no protocol payload fields.
 * Each channel belongs to exactly one endpoint. Invalid additions do not bind. */
pigen_protocol_endpoint_id pigen_protocol_endpoint_add(pigen_semantic_model *model,
	const pigen_semantic_protocol_endpoint *endpoint);
const pigen_semantic_protocol_endpoint *pigen_protocol_endpoint_get(
	const pigen_semantic_model *model, pigen_protocol_endpoint_id endpoint);
pigen_protocol_endpoint_id pigen_symbol_protocol_endpoint(
	const pigen_semantic_model *model, pigen_symbol_id symbol);
pigen_protocol_endpoint_id pigen_protocol_endpoint_for_signal(
	const pigen_semantic_model *model, pigen_signal_id signal,
	pigen_protocol_channel *channel);
pigen_protocol_endpoint_id pigen_instance_protocol_endpoint(
	const pigen_semantic_model *model, pigen_instance_id instance,
	pigen_protocol_endpoint_id child_endpoint);
int pigen_protocol_array_shape_size(pigen_semantic_model *model,
	pigen_shape_id shape, size_t *count);
pigen_protocol_array_id pigen_protocol_array_add(pigen_semantic_model *model,
	const pigen_semantic_protocol_array *array,
	const pigen_protocol_endpoint_id *elements, size_t element_count);
const pigen_semantic_protocol_array *pigen_protocol_array_get(
	const pigen_semantic_model *model, pigen_protocol_array_id array);
pigen_protocol_array_id pigen_symbol_protocol_array(
	const pigen_semantic_model *model, pigen_symbol_id symbol);
pigen_protocol_endpoint_id pigen_protocol_array_element(
	const pigen_semantic_model *model, pigen_protocol_array_id array,
	const int64_t *coordinates, size_t coordinate_count);
pigen_protocol_array_id pigen_protocol_array_for_endpoint(
	const pigen_semantic_model *model, pigen_protocol_endpoint_id endpoint, size_t *ordinal);
int pigen_protocol_arrays_can_connect(const pigen_semantic_model *model,
	pigen_protocol_array_id source, pigen_protocol_array_id destination);
int pigen_protocol_endpoint_bind(pigen_semantic_model *model,
	pigen_protocol_endpoint_id endpoint, pigen_clock_domain_id domain,
	pigen_symbol_id reset_symbol, int reset_active_low);
pigen_clock_domain_id pigen_clock_domain_intern(
	pigen_semantic_model *model, pigen_symbol_id clock_symbol,
	pigen_semantic_edge edge);
pigen_process_id pigen_process_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	pigen_clock_domain_id domain, pigen_expr_id clock,
	pigen_source_span span);
/* Storage-free structural transfers have no clock or domain. */
pigen_process_id pigen_structural_process_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_source_span span);
pigen_procedural_id pigen_procedural_add(pigen_semantic_model *model,
	pigen_process_id process, pigen_procedural_kind kind,
	pigen_expr_id condition, pigen_transfer_id assignment,
	const pigen_procedural_id *children, size_t count, pigen_source_span span);
pigen_transfer_id pigen_transfer_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_process_id process,
	pigen_lvalue_id destination, pigen_expr_id value,
	pigen_predicate_id guard, pigen_clock_domain_id domain,
	const pigen_transfer_signal_use *signal_uses,
	size_t signal_use_count,
	pigen_source_span span);
pigen_action_id pigen_action_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_process_id process,
	pigen_clock_domain_id domain, pigen_signal_id signal,
	pigen_action_kind action, pigen_predicate_id guard, pigen_source_span span);
const pigen_semantic_action *pigen_action_get(const pigen_semantic_model *model,
	size_t index);
pigen_instance_id pigen_instance_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module, pigen_symbol_id symbol,
	pigen_module_id child_module, pigen_source_span span);
int pigen_instance_add_connection(pigen_semantic_model *model,
	pigen_instance_id instance, pigen_symbol_id port_symbol,
	pigen_signal_id port_signal, pigen_source_span port_name,
	pigen_expr_id value, pigen_semantic_direction direction,
	pigen_source_span span);
const pigen_semantic_instance *pigen_instance_get(
	const pigen_semantic_model *model, pigen_instance_id instance);
const pigen_semantic_instance_connection *pigen_instance_connections(
	const pigen_semantic_model *model, pigen_instance_id instance);
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
int pigen_signal_is_instance_endpoint(const pigen_semantic_model *model, pigen_signal_id signal);
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
/* Owner-level enumeration accessors for the flat process and transfer
 * arenas: the module's processes and one process's transfers, each in arena
 * (addition) order. The signature shape follows pigen_expr_children: an id
 * pointer with a count out-param. Each call returns a model-owned buffer
 * (a snapshot of stable arena indices), freed by
 * pigen_free_semantic_model and never by the caller. An invalid or empty
 * owner returns NULL and leaves the count at zero. */
pigen_process_id *pigen_module_processes(
	pigen_semantic_model *model, pigen_module_id module,
	size_t *process_count);
pigen_transfer_id *pigen_process_transfers(
	pigen_semantic_model *model, pigen_process_id process,
	size_t *transfer_count);
void pigen_free_semantic_model(pigen_semantic_model *model);

#endif
