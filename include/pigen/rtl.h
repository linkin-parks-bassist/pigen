#ifndef PIGEN_RTL_H
#define PIGEN_RTL_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/data_type.h"
#include "pigen/ids.h"
#include "pigen/semantic.h"
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
	int sv_keyword;
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
	PIGEN_RTL_EXPR_CONCATENATION,
	PIGEN_RTL_EXPR_EVALUATED_VALUE,
	PIGEN_RTL_EXPR_ARRAY_PATTERN
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
			char *member_name; /* Optional static record field; model-owned. */
		} select;
		struct { int before; } evaluation;
	} as;
} pigen_rtl_expr;

/* An owner range is a half-open arena interval [first, first + count); it
 * stays empty until the owner's children are appended. Owner identity is
 * validated before any range is published, and a cross-module failure
 * leaves both arena counts and owner ranges unchanged. Semantic IDs are
 * provenance and may be invalid for synthetic hardware. */
typedef struct {
	size_t first;
	size_t count;
} pigen_rtl_record_range;

typedef enum {
	PIGEN_RTL_OBJECT_KIND_INVALID,
	PIGEN_RTL_OBJECT_VARIABLE,
	PIGEN_RTL_OBJECT_MEMORY,
	PIGEN_RTL_OBJECT_PARAMETER
} pigen_rtl_object_kind;

/* Backend storage or arbitration primitive selected during RTL lowering.
 * The emitter owns the primitive definition spellings and port names; the
 * record carries only the semantic selection. */
typedef enum {
	PIGEN_RTL_PRIMITIVE_INVALID,
	PIGEN_RTL_PRIMITIVE_BUF,
	PIGEN_RTL_PRIMITIVE_PORT,
	PIGEN_RTL_PRIMITIVE_FIFO,
	PIGEN_RTL_PRIMITIVE_SKID,
	PIGEN_RTL_PRIMITIVE_MERGE2,
	PIGEN_RTL_PRIMITIVE_MERGE,
	PIGEN_RTL_PRIMITIVE_AXIS_MERGE,
	PIGEN_RTL_PRIMITIVE_AXI_MERGE,
	PIGEN_RTL_PRIMITIVE_AXIS_DEMUX,
	PIGEN_RTL_PRIMITIVE_AXI_DEMUX
} pigen_rtl_primitive;

typedef struct {
	pigen_source_span origin;
	pigen_rtl_object_kind kind;
	pigen_rtl_type_id type;
	pigen_semantic_direction direction;
	pigen_signal_id semantic_signal; /* Provenance; invalid if synthetic. */
	pigen_rtl_module_id module; /* Invalid until owner resolution publishes it. */
	char *name; /* Final terminal name owned by the model; NULL falls back to
	             * the name arena (the t10 emitter contract). */
	char *shape; /* Rendered unpacked dimensions, e.g. " [0:15]"; ownership
	              * belongs to the model. */
	pigen_rtl_expr_id initial_value; /* Required for local parameter objects. */
	char *declaration_type; /* Prepared static packed-record type; model-owned.
	                         * Expression types remain canonical RTL types. */
} pigen_rtl_object;

/* One instance port connection: a named port bound to an object, or to a
 * literal/expression value when object is invalid. */
typedef struct {
	const char *port;
	pigen_rtl_object_id object;
	pigen_rtl_expr_id value;
	pigen_semantic_direction direction; /* Child port direction; INTERNAL when unspecified. */
} pigen_rtl_connection;

typedef struct {
	pigen_source_span origin;
	pigen_module_id semantic_module; /* Provenance; invalid if synthetic. */
	pigen_rtl_record_range parameters; /* Ordered instance parameter range. */
	pigen_rtl_record_range connections; /* Ordered instance connection range. */
	pigen_rtl_module_id module; /* Invalid until owner resolution publishes it. */
	pigen_rtl_primitive primitive;
	pigen_rtl_type_id payload_type; /* PAYLOAD_T type parameter; invalid if none. */
	pigen_rtl_record_range connection_records; /* Named connections; empty uses
	                                            * the positional object range. */
	char *name; /* Final instance name; NULL falls back to the name arena. */
	char *definition; /* Resolved module name for non-primitive instances. */
	pigen_instance_id semantic_instance;
} pigen_rtl_instance;

typedef struct {
	pigen_source_span origin;
	pigen_rtl_object_id destination; /* First leaf; compound drivers require the query below. */
	pigen_rtl_expr_id value;
	pigen_rtl_module_id module; /* Invalid until owner resolution publishes it. */
	pigen_rtl_expr_id destination_expression; /* Optional projection or concatenated lvalue. */
} pigen_rtl_equation;

typedef struct {
	pigen_source_span origin;
	pigen_rtl_object_id destination;
	pigen_rtl_expr_id value;
	pigen_rtl_expr_id enable; /* Invalid means unconditional. */
	pigen_rtl_expr_id path; /* Selected/indexed destination when valid. */
	pigen_rtl_module_id module; /* Invalid until owner resolution publishes it. */
} pigen_rtl_update;

typedef struct {
	pigen_source_span origin;
	pigen_rtl_expr_id clock;
	pigen_semantic_edge edge;
	pigen_rtl_record_range updates;
	int is_combinational;
	pigen_rtl_procedural_id body;
	pigen_rtl_module_id module; /* Invalid until owner resolution publishes it. */
} pigen_rtl_process;

typedef struct {
	pigen_procedural_kind kind;
	pigen_rtl_module_id module;
	pigen_rtl_expr_id destination, value, condition;
	size_t first_child, child_count;
	pigen_source_span origin;
	/* This label continues the preceding comma-separated case item. */
	int case_continuation;
	pigen_case_qualifier case_qualifier;
	int else_if;
} pigen_rtl_procedural;


typedef struct {
	pigen_source_span origin;
	pigen_module_id semantic_module; /* Provenance; invalid if synthetic. */
	pigen_rtl_record_range objects;
	pigen_rtl_record_range instances;
	pigen_rtl_record_range equations;
	pigen_rtl_record_range processes;
	char *name; /* Final module name; NULL falls back to the name arena. */
} pigen_rtl_module;

typedef enum {
	PIGEN_RTL_NAME_KIND_INVALID,
	PIGEN_RTL_NAME_SOURCE,
	PIGEN_RTL_NAME_PAYLOAD,
	PIGEN_RTL_NAME_VALID,
	PIGEN_RTL_NAME_READY,
	PIGEN_RTL_NAME_INSTANCE,
	PIGEN_RTL_NAME_TEMPORARY
} pigen_rtl_name_kind;

/* Origin is the checked identifier span for source roles and may be
 * invalid for synthetic internal roles; text is model-owned. */
typedef struct {
	pigen_source_span origin;
	pigen_rtl_name_kind kind;
	char *text;
} pigen_rtl_name;

/* Source references describe a one-to-many relation: aliases may name the
 * same object, and one channel may have several storage/interface objects.
 * Paths are module-local metadata, not additional authored lookup syntax. */
typedef enum {
	PIGEN_RTL_SOURCE_SCOPE,
	PIGEN_RTL_SOURCE_VALUE,
	PIGEN_RTL_SOURCE_VALID,
	PIGEN_RTL_SOURCE_READY,
	PIGEN_RTL_SOURCE_INPUT_VALUE,
	PIGEN_RTL_SOURCE_INPUT_VALID,
	PIGEN_RTL_SOURCE_INPUT_READY,
	PIGEN_RTL_SOURCE_FIELD,
	PIGEN_RTL_SOURCE_INSTANCE,
	PIGEN_RTL_SOURCE_INSTANCE_PORT
} pigen_rtl_source_role;

typedef struct {
	pigen_rtl_source_role role;
	pigen_rtl_module_id module;
	pigen_scope_id scope; /* Semantic provenance; no borrowed storage. */
	pigen_signal_id signal;
	pigen_rtl_object_id object; /* Invalid for scope/instance references. */
	pigen_rtl_instance_id instance; /* Valid for instance/instance-port references. */
	char *source_path;
	char *emitted_name; /* A scope reference carries its flattened prefix. */
} pigen_rtl_source_reference;

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
	pigen_rtl_procedural *procedural;
	size_t procedural_count, procedural_capacity;
	pigen_rtl_procedural_id *procedural_children;
	size_t procedural_child_count, procedural_child_capacity;
	pigen_rtl_process *processes;
	size_t process_count;
	size_t process_capacity;
	pigen_rtl_module *modules;
	size_t module_count;
	size_t module_capacity;
	pigen_rtl_expr_id *instance_parameters;
	size_t instance_parameter_count;
	size_t instance_parameter_capacity;
	pigen_rtl_object_id *instance_connections;
	size_t instance_connection_count;
	size_t instance_connection_capacity;
	pigen_rtl_connection *connections;
	size_t connection_count;
	size_t connection_capacity;
	pigen_rtl_name *names;
	size_t name_count;
	size_t name_capacity;
	pigen_rtl_source_reference *source_references;
	size_t source_reference_count, source_reference_capacity;
} pigen_rtl_model;

/* Copies both strings. Repeating an identical reference is idempotent;
 * an invalid/conflicting request returns SIZE_MAX without publication. */
size_t pigen_rtl_source_reference_add(pigen_rtl_model *model,
	const pigen_rtl_source_reference *reference);
const pigen_rtl_source_reference *pigen_rtl_source_reference_get(
	const pigen_rtl_model *model, size_t index);

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
pigen_rtl_expr_id pigen_rtl_expr_add_member_select(pigen_rtl_model *model,
	pigen_rtl_type_id type, pigen_rtl_expr_id base, pigen_rtl_expr_id left,
	pigen_rtl_expr_id right, const char *name, pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_evaluated_value(pigen_rtl_model *model,
	pigen_rtl_type_id type,pigen_rtl_expr_id value,pigen_rtl_expr_id evaluated,
	int before,pigen_source_span origin);
pigen_rtl_expr_id pigen_rtl_expr_add_array_pattern(pigen_rtl_model *model,
	pigen_rtl_type_id type,const pigen_rtl_expr_id *children,size_t count,
	pigen_source_span origin);
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
/* Task 3 planned constructors extend the origin-only arena constructors
 * above with resolved owner identities. The contract's exact planned names
 * are already held by the origin-only foundations that tests/rtl_test.c
 * calls with two arguments, and C has no overloading; the planned shapes
 * therefore carry a _with_owner suffix. Stubs return the unimplemented
 * error; owner validation and range publication arrive with the Task 3
 * implementation. */
pigen_rtl_module_id pigen_rtl_module_add_with_owner(pigen_rtl_model *model,
	pigen_module_id semantic_module, pigen_source_span span);
pigen_rtl_object_id pigen_rtl_object_add_with_owner(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_object_kind kind,
	pigen_rtl_type_id type, pigen_semantic_direction direction,
	pigen_signal_id semantic_signal, pigen_source_span span);
pigen_rtl_equation_id pigen_rtl_equation_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_object_id destination, pigen_rtl_expr_id value,
	pigen_source_span span);
pigen_rtl_equation_id pigen_rtl_equation_add_projection_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_expr_id destination, pigen_rtl_expr_id value,
	pigen_source_span span);
/* Tests every destination leaf, excluding index/select read operands. */
int pigen_rtl_equation_drives_object(const pigen_rtl_model *model,
	pigen_rtl_equation_id equation, pigen_rtl_object_id object);
pigen_rtl_instance_id pigen_rtl_instance_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_module_id semantic_module,
	const pigen_rtl_expr_id *parameters, size_t parameter_count,
	const pigen_rtl_object_id *connections, size_t connection_count,
	pigen_source_span span);
pigen_rtl_update_id pigen_rtl_update_add_with_owner(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_object_id destination,
	pigen_rtl_expr_id value, pigen_source_span span);
int pigen_rtl_update_set_enable(pigen_rtl_model *model,
	pigen_rtl_update_id update, pigen_rtl_expr_id enable);
int pigen_rtl_update_set_path(pigen_rtl_model *model,
	pigen_rtl_update_id update, pigen_rtl_expr_id path);
int pigen_rtl_object_set_shape(pigen_rtl_model *model,
	pigen_rtl_object_id object, const char *shape);
pigen_rtl_procedural_id pigen_rtl_procedural_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_procedural_kind kind,
	pigen_rtl_expr_id destination, pigen_rtl_expr_id value, pigen_rtl_expr_id condition,
	const pigen_rtl_procedural_id *children, size_t count, pigen_source_span span);
pigen_rtl_process_id pigen_rtl_combinational_process_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_procedural_id body, pigen_source_span span);
int pigen_rtl_process_drives_object(const pigen_rtl_model *model,
	pigen_rtl_process_id process, pigen_rtl_object_id object);
pigen_rtl_process_id pigen_rtl_process_add_with_owner(
	pigen_rtl_model *model, pigen_rtl_module_id module,
	pigen_rtl_expr_id clock_expression, pigen_semantic_edge edge,
	const pigen_rtl_update_id *updates, size_t update_count,
	pigen_source_span span);
/* Append a named connection range owned by the model. */
pigen_rtl_record_range pigen_rtl_connection_range_add(pigen_rtl_model *model,
	const pigen_rtl_connection *connections, size_t count);
/* Attach a primitive selection to an instance. */
int pigen_rtl_instance_set_primitive(pigen_rtl_model *model,
	pigen_rtl_instance_id instance, pigen_rtl_primitive primitive,
	pigen_rtl_type_id payload_type, pigen_rtl_record_range connection_records);
int pigen_rtl_primitive_port_is_output(pigen_rtl_primitive primitive, const char *port);
void pigen_rtl_set_name(pigen_rtl_model *model, char **slot, const char *text);
void pigen_free_rtl_model(pigen_rtl_model *model);

#endif
