#ifndef PIGEN_MODEL_H
#define PIGEN_MODEL_H

#include <stddef.h>

typedef struct { char *data; size_t length; size_t capacity; } pigen_string;
typedef struct { size_t start; size_t end; size_t line; size_t column; } pigen_span;
typedef struct { char *name; char kind; int is_internal; int is_output; char *payload_type; char *fifo_depth; } pigen_primitive;
typedef struct { pigen_primitive *items; size_t count; size_t capacity; } pigen_primitives;
typedef struct { char *destination; char *expression; char *guard; char *domain; char destination_kind; size_t group; size_t order; } pigen_assignment;
typedef struct { pigen_assignment *items; size_t count; size_t capacity; size_t next_group; } pigen_assignments;
typedef struct { char *lhs; char *rhs; size_t group; } pigen_width_check;
typedef struct { pigen_width_check *items; size_t count; size_t capacity; } pigen_width_checks;
typedef struct { char *target; char *guard; char *domain; int is_flush; size_t order; } pigen_clear;
typedef struct { pigen_clear *items; size_t count; size_t capacity; } pigen_clears;
typedef struct { const char *keyword; char kind; int is_storage; const char *primitive_module; } pigen_type_descriptor;

#endif
