#ifndef PIGEN_RTL_H
#define PIGEN_RTL_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/source.h"

typedef struct {
	pigen_source_span origin;
} pigen_rtl_type;

typedef struct {
	pigen_source_span origin;
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
pigen_rtl_expr_id pigen_rtl_expr_add(pigen_rtl_model *model,
	pigen_source_span origin);
const pigen_rtl_expr *pigen_rtl_expr_get(const pigen_rtl_model *model,
	pigen_rtl_expr_id expression);
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
