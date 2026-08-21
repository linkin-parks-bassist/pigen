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

/* A negative constant means that the control is context-dependent. */
typedef struct {
	const char *spelling;
	int is_concrete;
	int is_static;
	int accepts_write;
	pigen_transfer_parameter parameter;
	int valid_constant;
	int ready_constant;
	int consumes_on_read;
	int produces_on_write;
	int requires_ownership;
	int binds_domain;
} pigen_transfer_type_descriptor;

const pigen_transfer_type_descriptor *pigen_transfer_type_descriptor_get(
	pigen_transfer_type transfer_type);
int pigen_transfer_type_is_valid(pigen_transfer_type transfer_type);
int pigen_transfer_type_from_spelling(const char *spelling, size_t length,
	pigen_transfer_type *transfer_type);

#endif
