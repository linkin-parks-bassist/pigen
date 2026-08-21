#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/transfer_type.h"

static pigen_transfer_type resolve(const char *spelling)
{
	pigen_transfer_type transfer_type;
	assert(pigen_transfer_type_from_spelling(spelling, strlen(spelling),
		&transfer_type));
	return transfer_type;
}

int main(void)
{
	const pigen_transfer_type_descriptor *abstract_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_ABSTRACT);
	const pigen_transfer_type_descriptor *wire_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_WIRE);
	const pigen_transfer_type_descriptor *buf_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
	const pigen_transfer_type_descriptor *port_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_PORT);
	const pigen_transfer_type_descriptor *fifo_descriptor =
		pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
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
