#ifndef PIGEN_TRANSFER_TYPE_H
#define PIGEN_TRANSFER_TYPE_H

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

/* A negative constant means that the control is context-dependent. */
typedef struct {
	int is_static;
	int valid_constant;
	int ready_constant;
	int consumes_on_read;
	int produces_on_write;
	int requires_ownership;
	int binds_domain;
} pigen_transfer_type_laws;

const pigen_transfer_type_laws *pigen_transfer_type_get(
	pigen_transfer_type transfer_type);

#endif
