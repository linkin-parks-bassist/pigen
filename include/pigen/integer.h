#ifndef PIGEN_INTEGER_H
#define PIGEN_INTEGER_H

#include <stddef.h>
#include <stdint.h>

#include "pigen/ids.h"

typedef struct pigen_semantic_model pigen_semantic_model;

pigen_integer_id pigen_integer_intern_decimal(pigen_semantic_model *model,
	const char *digits, size_t length);
pigen_integer_id pigen_integer_intern_u64(pigen_semantic_model *model,
	uint64_t value);
pigen_integer_id pigen_integer_negate(pigen_semantic_model *model,
	pigen_integer_id value);
pigen_integer_id pigen_integer_add(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_subtract(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_multiply(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_shift_left(pigen_semantic_model *model,
	pigen_integer_id value, size_t amount, size_t maximum_bits);
pigen_integer_id pigen_integer_power(pigen_semantic_model *model,
	pigen_integer_id base, pigen_integer_id exponent, size_t maximum_bits);
int pigen_integer_compare(const pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
int pigen_integer_is_zero(const pigen_semantic_model *model,
	pigen_integer_id value);
int pigen_integer_is_negative(const pigen_semantic_model *model,
	pigen_integer_id value);
int pigen_integer_is_odd(const pigen_semantic_model *model,
	pigen_integer_id value);
int pigen_integer_to_size(const pigen_semantic_model *model,
	pigen_integer_id value, size_t *result);
size_t pigen_integer_unsigned_width(const pigen_semantic_model *model,
	pigen_integer_id value);
size_t pigen_integer_signed_width(const pigen_semantic_model *model,
	pigen_integer_id value);

#endif
