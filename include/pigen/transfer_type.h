#ifndef PIGEN_TRANSFER_TYPE_H
#define PIGEN_TRANSFER_TYPE_H

#include <stddef.h>

typedef enum {
	PIGEN_TRANSFER_TYPE_ABSTRACT,
	PIGEN_TRANSFER_TYPE_WIRE,
	PIGEN_TRANSFER_TYPE_REG,
	PIGEN_TRANSFER_TYPE_LOGIC,
	PIGEN_TRANSFER_TYPE_BUF,
	PIGEN_TRANSFER_TYPE_PORT,
	PIGEN_TRANSFER_TYPE_FIFO,
	PIGEN_TRANSFER_TYPE_SKID
} pigen_transfer_type;

typedef enum {
	PIGEN_TRANSFER_PARAMETER_NONE,
	PIGEN_TRANSFER_PARAMETER_DEPTH
} pigen_transfer_parameter;

typedef enum {
	PIGEN_TRANSFER_REALIZATION_INVALID,
	PIGEN_TRANSFER_REALIZATION_BOUNDARY,
	PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET,
	PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
	PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT,
	PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER,
	PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE,
	PIGEN_TRANSFER_REALIZATION_SKID_QUEUE
} pigen_transfer_realization;

typedef enum {
	PIGEN_TRANSFER_CAPACITY_INVALID,
	PIGEN_TRANSFER_CAPACITY_NONE,
	PIGEN_TRANSFER_CAPACITY_FIXED,
	PIGEN_TRANSFER_CAPACITY_ARGUMENT
} pigen_transfer_capacity_source;

typedef enum {
	PIGEN_TRANSFER_READY_INVALID,
	PIGEN_TRANSFER_READY_EXTERNAL,
	PIGEN_TRANSFER_READY_CONSTANT,
	PIGEN_TRANSFER_READY_DOWNSTREAM,
	PIGEN_TRANSFER_READY_OCCUPANCY
} pigen_transfer_ready_dependency;

typedef enum {
	PIGEN_TRANSFER_RESET_INVALID,
	PIGEN_TRANSFER_RESET_NONE,
	PIGEN_TRANSFER_RESET_PROCEDURAL,
	PIGEN_TRANSFER_RESET_EMPTY
} pigen_transfer_reset;

typedef struct {
	pigen_transfer_capacity_source capacity_source;
	size_t fixed_capacity;
	pigen_transfer_ready_dependency ready_dependency;
	int has_occupancy;
	pigen_transfer_reset reset;
} pigen_transfer_realization_descriptor;

/* A negative constant means that the control is context-dependent. */
typedef struct {
	const char *spelling;
	int is_concrete;
	int is_static;
	int accepts_write;
	pigen_transfer_parameter parameter;
	pigen_transfer_realization realization;
	int valid_constant;
	int ready_constant;
	int consumes_on_read;
	int produces_on_write;
	int requires_ownership;
	int binds_domain;
} pigen_transfer_type_descriptor;

const pigen_transfer_realization_descriptor *
pigen_transfer_realization_descriptor_get(
	pigen_transfer_realization realization);
int pigen_transfer_realization_is_valid(
	pigen_transfer_realization realization);
const pigen_transfer_type_descriptor *pigen_transfer_type_descriptor_get(
	pigen_transfer_type transfer_type);
int pigen_transfer_type_is_valid(pigen_transfer_type transfer_type);
int pigen_transfer_type_from_spelling(const char *spelling, size_t length,
	pigen_transfer_type *transfer_type);

#endif
