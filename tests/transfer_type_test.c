#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/transfer_type.h"

static pigen_transfer_type resolve(const char *spelling)
{
	pigen_transfer_type transfer_type;
	assert(pigen_transfer_type_from_spelling(spelling, strlen(spelling),
		&transfer_type));
	assert(pigen_transfer_type_descriptor_get(transfer_type));
	return transfer_type;
}

static const pigen_transfer_realization_descriptor *realization_for(
	const pigen_transfer_type_descriptor *transfer_type)
{
	assert(transfer_type);
	return pigen_transfer_realization_descriptor_get(
		transfer_type->realization);
}

int main(void)
{
	const pigen_transfer_type_descriptor *abstract_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_ABSTRACT);
	const pigen_transfer_type_descriptor *wire_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_WIRE);
	const pigen_transfer_type_descriptor *reg_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_REG);
	const pigen_transfer_type_descriptor *logic_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_LOGIC);
	const pigen_transfer_type_descriptor *buf_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
	const pigen_transfer_type_descriptor *port_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_PORT);
	const pigen_transfer_type_descriptor *fifo_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
	const pigen_transfer_type_descriptor *skid_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_SKID);
	const pigen_transfer_realization_descriptor *boundary =
		realization_for(abstract_descriptor);
	const pigen_transfer_realization_descriptor *net =
		realization_for(wire_descriptor);
	const pigen_transfer_realization_descriptor *variable =
		realization_for(reg_descriptor);
	const pigen_transfer_realization_descriptor *elastic =
		realization_for(buf_descriptor);
	const pigen_transfer_realization_descriptor *pulse =
		realization_for(port_descriptor);
	const pigen_transfer_realization_descriptor *queue =
		realization_for(fifo_descriptor);
	const pigen_transfer_realization_descriptor *skid =
		realization_for(skid_descriptor);
	pigen_transfer_type invalid;

	assert(pigen_transfer_type_is_valid(PIGEN_TRANSFER_TYPE_ABSTRACT));
	assert(pigen_transfer_type_is_valid(PIGEN_TRANSFER_TYPE_SKID));
	assert(!pigen_transfer_type_is_valid((pigen_transfer_type)-1));
	assert(!pigen_transfer_type_is_valid((pigen_transfer_type)99));
	assert(abstract_descriptor && !abstract_descriptor->spelling &&
		!abstract_descriptor->is_concrete && !abstract_descriptor->is_static &&
		!abstract_descriptor->accepts_write &&
		abstract_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE);
	assert(wire_descriptor && !strcmp(wire_descriptor->spelling, "wire") &&
		wire_descriptor->is_concrete && wire_descriptor->is_static &&
		!wire_descriptor->accepts_write &&
		wire_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE);
	assert(buf_descriptor && !strcmp(buf_descriptor->spelling, "buf") &&
		buf_descriptor->is_concrete && !buf_descriptor->is_static &&
		buf_descriptor->accepts_write &&
		buf_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE);
	assert(port_descriptor && !strcmp(port_descriptor->spelling, "port") &&
		port_descriptor->valid_constant < 0 &&
		port_descriptor->ready_constant == 1);
	assert(fifo_descriptor && !strcmp(fifo_descriptor->spelling, "fifo") &&
		fifo_descriptor->is_concrete && !fifo_descriptor->is_static &&
		fifo_descriptor->accepts_write &&
		fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH);
	assert(abstract_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_BOUNDARY);
	assert(wire_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET);
	assert(reg_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE);
	assert(logic_descriptor->realization == reg_descriptor->realization);
	assert(buf_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
	assert(port_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER);
	assert(fifo_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
	assert(skid_descriptor->realization ==
		PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);

	assert(boundary &&
		boundary->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
		boundary->ready_dependency == PIGEN_TRANSFER_READY_EXTERNAL &&
		!boundary->has_occupancy &&
		boundary->reset == PIGEN_TRANSFER_RESET_NONE);
	assert(net &&
		net->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
		net->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
		!net->has_occupancy && net->reset == PIGEN_TRANSFER_RESET_NONE);
	assert(variable &&
		variable->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
		variable->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
		!variable->has_occupancy &&
		variable->reset == PIGEN_TRANSFER_RESET_PROCEDURAL);
	assert(elastic &&
		elastic->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
		elastic->fixed_capacity == 1 &&
		elastic->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM &&
		elastic->has_occupancy &&
		elastic->reset == PIGEN_TRANSFER_RESET_EMPTY);
	assert(pulse &&
		pulse->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
		pulse->fixed_capacity == 1 &&
		pulse->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
		!pulse->has_occupancy &&
		pulse->reset == PIGEN_TRANSFER_RESET_EMPTY);
	assert(queue &&
		queue->capacity_source == PIGEN_TRANSFER_CAPACITY_ARGUMENT &&
		queue->fixed_capacity == 0 &&
		queue->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY &&
		queue->has_occupancy &&
		queue->reset == PIGEN_TRANSFER_RESET_EMPTY &&
		fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH);
	assert(skid &&
		skid->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
		skid->fixed_capacity == 2 &&
		skid->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY &&
		skid->has_occupancy &&
		skid->reset == PIGEN_TRANSFER_RESET_EMPTY);

	assert(pigen_transfer_realization_is_valid(
		PIGEN_TRANSFER_REALIZATION_BOUNDARY));
	assert(pigen_transfer_realization_is_valid(
		PIGEN_TRANSFER_REALIZATION_SKID_QUEUE));
	assert(!pigen_transfer_realization_is_valid(
		PIGEN_TRANSFER_REALIZATION_INVALID));
	assert(!pigen_transfer_realization_is_valid(
		(pigen_transfer_realization)-1));
	assert(!pigen_transfer_realization_is_valid(
		(pigen_transfer_realization)99));
	assert(!pigen_transfer_realization_descriptor_get(
		PIGEN_TRANSFER_REALIZATION_INVALID));

	assert(buf_descriptor->realization != port_descriptor->realization);
	assert(buf_descriptor->realization != fifo_descriptor->realization);
	assert(buf_descriptor->realization != skid_descriptor->realization);
	assert(port_descriptor->realization != fifo_descriptor->realization);
	assert(port_descriptor->realization != skid_descriptor->realization);
	assert(fifo_descriptor->realization != skid_descriptor->realization);

	for (pigen_transfer_type transfer_type = PIGEN_TRANSFER_TYPE_ABSTRACT;
		transfer_type <= PIGEN_TRANSFER_TYPE_SKID; transfer_type++)
	{
		const pigen_transfer_type_descriptor *transfer_descriptor =
			pigen_transfer_type_descriptor_get(transfer_type);
		const pigen_transfer_realization_descriptor *realization =
			realization_for(transfer_descriptor);
		assert((transfer_descriptor->parameter ==
			PIGEN_TRANSFER_PARAMETER_DEPTH) ==
			(realization->capacity_source ==
				PIGEN_TRANSFER_CAPACITY_ARGUMENT));
	}
	assert(resolve("wire") == PIGEN_TRANSFER_TYPE_WIRE);
	assert(resolve("logic") == PIGEN_TRANSFER_TYPE_LOGIC);
	assert(resolve("buf") == PIGEN_TRANSFER_TYPE_BUF);
	assert(resolve("port") == PIGEN_TRANSFER_TYPE_PORT);
	assert(resolve("fifo") == PIGEN_TRANSFER_TYPE_FIFO);
	assert(resolve("skid") == PIGEN_TRANSFER_TYPE_SKID);
	assert(!pigen_transfer_type_from_spelling("buffer", 6, &invalid));
	assert(!pigen_transfer_type_from_spelling("buf", 3, NULL));

	puts("PASS: transfer types have one descriptor catalogue");
	return 0;
}
