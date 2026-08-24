/* Canonical transfer-type catalogue and behavioral laws. */
#include <string.h>

#include "pigen/transfer_type.h"

static const pigen_transfer_realization_descriptor realizations[] = {
	[PIGEN_TRANSFER_REALIZATION_BOUNDARY] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_EXTERNAL,
		.reset = PIGEN_TRANSFER_RESET_NONE
	},
	[PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_NONE
	},
	[PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_PROCEDURAL
	},
	[PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 1,
		.ready_dependency = PIGEN_TRANSFER_READY_DOWNSTREAM,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 1,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_ARGUMENT,
		.ready_dependency = PIGEN_TRANSFER_READY_OCCUPANCY,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_SKID_QUEUE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 2,
		.ready_dependency = PIGEN_TRANSFER_READY_OCCUPANCY,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	}
};

static const pigen_transfer_type_descriptor transfer_types[] = {
	[PIGEN_TRANSFER_TYPE_ABSTRACT] = {
		.spelling = NULL,
		.is_concrete = 0,
		.is_static = 0,
		.accepts_write = 0,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_BOUNDARY,
		.valid_constant = -1,
		.ready_constant = -1,
		.consumes_on_read = -1,
		.produces_on_write = -1,
		.requires_ownership = -1,
		.binds_domain = -1
	},
	[PIGEN_TRANSFER_TYPE_WIRE] = {
		.spelling = "wire",
		.is_concrete = 1,
		.is_static = 1,
		.accepts_write = 0,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET,
		.valid_constant = 1,
		.ready_constant = 0
	},
	[PIGEN_TRANSFER_TYPE_REG] = {
		.spelling = "reg",
		.is_concrete = 1,
		.is_static = 1,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
		.valid_constant = 1,
		.ready_constant = 1
	},
	[PIGEN_TRANSFER_TYPE_LOGIC] = {
		.spelling = "logic",
		.is_concrete = 1,
		.is_static = 1,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
		.valid_constant = 1,
		.ready_constant = 1
	},
	[PIGEN_TRANSFER_TYPE_BUF] = {
		.spelling = "buf",
		.is_concrete = 1,
		.is_static = 0,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT,
		.valid_constant = -1,
		.ready_constant = -1,
		.consumes_on_read = 1,
		.produces_on_write = 1,
		.requires_ownership = 1,
		.binds_domain = 1
	},
	[PIGEN_TRANSFER_TYPE_PORT] = {
		.spelling = "port",
		.is_concrete = 1,
		.is_static = 0,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER,
		.valid_constant = -1,
		.ready_constant = 1,
		.consumes_on_read = 1,
		.produces_on_write = 1,
		.requires_ownership = 1,
		.binds_domain = 1
	},
	[PIGEN_TRANSFER_TYPE_FIFO] = {
		.spelling = "fifo",
		.is_concrete = 1,
		.is_static = 0,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_DEPTH,
		.realization = PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE,
		.valid_constant = -1,
		.ready_constant = -1,
		.consumes_on_read = 1,
		.produces_on_write = 1,
		.requires_ownership = 1,
		.binds_domain = 1
	},
	[PIGEN_TRANSFER_TYPE_SKID] = {
		.spelling = "skid",
		.is_concrete = 1,
		.is_static = 0,
		.accepts_write = 1,
		.parameter = PIGEN_TRANSFER_PARAMETER_NONE,
		.realization = PIGEN_TRANSFER_REALIZATION_SKID_QUEUE,
		.valid_constant = -1,
		.ready_constant = -1,
		.consumes_on_read = 1,
		.produces_on_write = 1,
		.requires_ownership = 1,
		.binds_domain = 1
	}
};

const pigen_transfer_realization_descriptor *
pigen_transfer_realization_descriptor_get(
	pigen_transfer_realization realization)
{
	const pigen_transfer_realization_descriptor *descriptor;

	if ((size_t)realization >=
		sizeof(realizations) / sizeof(*realizations)) return NULL;
	descriptor = &realizations[realization];
	if (descriptor->capacity_source == PIGEN_TRANSFER_CAPACITY_INVALID ||
		descriptor->ready_dependency == PIGEN_TRANSFER_READY_INVALID ||
		descriptor->reset == PIGEN_TRANSFER_RESET_INVALID) return NULL;
	if ((descriptor->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED) !=
		(descriptor->fixed_capacity != 0)) return NULL;
	return descriptor;
}

int pigen_transfer_realization_is_valid(
	pigen_transfer_realization realization)
{
	return pigen_transfer_realization_descriptor_get(realization) != NULL;
}

int pigen_transfer_type_is_valid(pigen_transfer_type transfer_type)
{
	return (size_t)transfer_type <
		sizeof(transfer_types) / sizeof(*transfer_types) &&
		pigen_transfer_realization_is_valid(
			transfer_types[transfer_type].realization);
}

const pigen_transfer_type_descriptor *pigen_transfer_type_descriptor_get(
	pigen_transfer_type transfer_type)
{
	return pigen_transfer_type_is_valid(transfer_type) ?
		&transfer_types[transfer_type] : NULL;
}

int pigen_transfer_type_from_spelling(const char *spelling, size_t length,
	pigen_transfer_type *transfer_type)
{
	size_t i;

	if (!spelling || !transfer_type) return 0;
	for (i = 0; i < sizeof(transfer_types) / sizeof(*transfer_types); i++)
		if (transfer_types[i].spelling &&
			strlen(transfer_types[i].spelling) == length &&
			!memcmp(transfer_types[i].spelling, spelling, length))
		{
			*transfer_type = (pigen_transfer_type)i;
			return 1;
		}
	return 0;
}
