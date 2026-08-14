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
	PIGEN_EXPR_INTEGER
} pigen_semantic_expr_kind;

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

typedef struct {
	pigen_expr_id left;
	pigen_expr_id right;
	pigen_source_span span;
} pigen_packed_dimension;

typedef struct {
	pigen_semantic_type_kind kind;
	pigen_signedness signedness;
	pigen_symbol_id named_symbol;
	size_t first_dimension;
	size_t dimension_count;
	pigen_source_span span;
} pigen_semantic_type;

typedef struct {
	pigen_scope_id parent;
	pigen_symbol_id last_symbol;
	pigen_source_span span;
} pigen_scope;

typedef struct {
	pigen_symbol_kind kind;
	pigen_scope_id scope;
	pigen_symbol_id previous_in_scope;
	pigen_type_id type;
	pigen_source_span name;
	pigen_source_span declaration;
} pigen_symbol;

typedef struct {
	pigen_semantic_expr_kind kind;
	pigen_type_id type;
	pigen_source_span span;
	uint64_t integer;
} pigen_semantic_expr;

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
	pigen_type_id payload_type;
	pigen_expr_id fifo_depth;
	pigen_semantic_transport_kind kind;
	pigen_semantic_direction direction;
	pigen_source_span span;
} pigen_semantic_transport;

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
	pigen_semantic_module *modules;
	size_t module_count;
	size_t module_capacity;
	pigen_semantic_transport *transports;
	size_t transport_count;
	size_t transport_capacity;
	pigen_scope_id compilation_scope;
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
	size_t dimension_count, pigen_source_span span);
const pigen_semantic_type *pigen_type_get(const pigen_semantic_model *model,
	pigen_type_id type);
const pigen_packed_dimension *pigen_type_dimensions(
	const pigen_semantic_model *model, pigen_type_id type);
pigen_expr_id pigen_expr_intern_integer(pigen_semantic_model *model,
	uint64_t value, pigen_type_id type, pigen_source_span span);
const pigen_semantic_expr *pigen_expr_get(const pigen_semantic_model *model,
	pigen_expr_id expression);
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
const pigen_semantic_module *pigen_module_get(const pigen_semantic_model *model,
	pigen_module_id module);
const pigen_semantic_transport *pigen_transport_get(
	const pigen_semantic_model *model, pigen_transport_id transport);
void pigen_free_semantic_model(pigen_semantic_model *model);

#endif
