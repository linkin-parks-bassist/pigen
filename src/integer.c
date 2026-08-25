/* Canonical arbitrary-precision signed integers. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/integer.h"
#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID ((pigen_integer_id){PIGEN_INVALID_ID})
#define LIMB_BITS 32u

static const pigen_integer *integer_get(const pigen_semantic_model *model,
	pigen_integer_id integer)
{
	if (!model || integer.index == PIGEN_INVALID_ID ||
		integer.index >= model->integer_count)
		return NULL;
	return &model->integers[integer.index];
}

static const uint32_t *integer_limbs(const pigen_semantic_model *model,
	const pigen_integer *integer)
{
	if (!integer || !integer->limb_count) return NULL;
	if (integer->first_limb > model->integer_limb_count ||
		integer->limb_count > model->integer_limb_count - integer->first_limb)
		return NULL;
	return model->integer_limbs + integer->first_limb;
}

static size_t normalized_count(const uint32_t *limbs, size_t count)
{
	while (count && !limbs[count - 1]) count--;
	return count;
}

static int limbs_equal(const pigen_semantic_model *model,
	const pigen_integer *integer, const uint32_t *limbs, size_t count)
{
	const uint32_t *known = integer_limbs(model, integer);
	return integer->limb_count == count && (!count ||
		(known && !memcmp(known, limbs, count * sizeof(*limbs))));
}

static pigen_integer_id integer_intern(pigen_semantic_model *model,
	int negative, const uint32_t *limbs, size_t count)
{
	pigen_integer_id result;
	size_t i;
	size_t needed;
	size_t capacity;

	if (!model || (count && !limbs)) return INVALID_ID;
	count = normalized_count(limbs, count);
	negative = count && negative;
	for (i = 0; i < model->integer_count; i++)
		if (model->integers[i].negative == negative &&
			limbs_equal(model, &model->integers[i], limbs, count))
			return (pigen_integer_id){(uint32_t)i};
	if (model->integer_count >= PIGEN_INVALID_ID ||
		count > SIZE_MAX - model->integer_limb_count)
		return INVALID_ID;
	if (model->integer_count == model->integer_capacity)
	{
		capacity = model->integer_capacity ? model->integer_capacity * 2 : 16;
		model->integers = pigen_resize(model->integers,
			capacity * sizeof(*model->integers));
		model->integer_capacity = capacity;
	}
	needed = model->integer_limb_count + count;
	if (needed > model->integer_limb_capacity)
	{
		capacity = model->integer_limb_capacity ?
			model->integer_limb_capacity * 2 : 32;
		while (capacity < needed) capacity *= 2;
		model->integer_limbs = pigen_resize(model->integer_limbs,
			capacity * sizeof(*model->integer_limbs));
		model->integer_limb_capacity = capacity;
	}
	result = (pigen_integer_id){(uint32_t)model->integer_count};
	model->integers[model->integer_count++] = (pigen_integer){negative,
		model->integer_limb_count, count};
	if (count)
		memcpy(model->integer_limbs + model->integer_limb_count, limbs,
			count * sizeof(*limbs));
	model->integer_limb_count = needed;
	return result;
}

static void magnitude_multiply_small(uint32_t **limbs, size_t *count,
	size_t *capacity, uint32_t multiplier)
{
	uint64_t carry = 0;
	size_t i;

	for (i = 0; i < *count; i++)
	{
		uint64_t product = (uint64_t)(*limbs)[i] * multiplier + carry;
		(*limbs)[i] = (uint32_t)product;
		carry = product >> LIMB_BITS;
	}
	if (carry)
	{
		if (*count == *capacity)
		{
			*capacity = *capacity ? *capacity * 2 : 4;
			*limbs = pigen_resize(*limbs, *capacity * sizeof(**limbs));
		}
		(*limbs)[(*count)++] = (uint32_t)carry;
	}
}

static void magnitude_add_small(uint32_t **limbs, size_t *count,
	size_t *capacity, uint32_t addend)
{
	uint64_t carry = addend;
	size_t i = 0;

	while (carry && i < *count)
	{
		uint64_t sum = (uint64_t)(*limbs)[i] + carry;
		(*limbs)[i++] = (uint32_t)sum;
		carry = sum >> LIMB_BITS;
	}
	if (carry)
	{
		if (*count == *capacity)
		{
			*capacity = *capacity ? *capacity * 2 : 4;
			*limbs = pigen_resize(*limbs, *capacity * sizeof(**limbs));
		}
		(*limbs)[(*count)++] = (uint32_t)carry;
	}
}

pigen_integer_id pigen_integer_intern_decimal(pigen_semantic_model *model,
	const char *digits, size_t length)
{
	uint32_t *limbs = NULL;
	size_t count = 0;
	size_t capacity = 0;
	size_t i;
	int any = 0;
	pigen_integer_id result;

	if (!model || !digits || !length) return INVALID_ID;
	for (i = 0; i < length; i++)
	{
		if (digits[i] == '_') continue;
		if (digits[i] < '0' || digits[i] > '9')
		{
			free(limbs);
			return INVALID_ID;
		}
		magnitude_multiply_small(&limbs, &count, &capacity, 10);
		magnitude_add_small(&limbs, &count, &capacity,
			(uint32_t)(digits[i] - '0'));
		any = 1;
	}
	if (!any)
	{
		free(limbs);
		return INVALID_ID;
	}
	result = integer_intern(model, 0, limbs, count);
	free(limbs);
	return result;
}

pigen_integer_id pigen_integer_intern_u64(pigen_semantic_model *model,
	uint64_t value)
{
	uint32_t limbs[2] = {(uint32_t)value, (uint32_t)(value >> LIMB_BITS)};
	return integer_intern(model, 0, limbs, value >> LIMB_BITS ? 2 : value ? 1 : 0);
}

pigen_integer_id pigen_integer_negate(pigen_semantic_model *model,
	pigen_integer_id value)
{
	const pigen_integer *known = integer_get(model, value);
	const uint32_t *limbs;
	uint32_t *copy;
	pigen_integer_id result;
	if (!known) return INVALID_ID;
	if (!known->limb_count) return value;
	limbs = integer_limbs(model, known);
	if (!limbs) return INVALID_ID;
	copy = pigen_resize(NULL, known->limb_count * sizeof(*copy));
	memcpy(copy, limbs, known->limb_count * sizeof(*copy));
	result = integer_intern(model, !known->negative, copy, known->limb_count);
	free(copy);
	return result;
}

static int magnitude_compare(const uint32_t *left, size_t left_count,
	const uint32_t *right, size_t right_count)
{
	if (left_count != right_count) return left_count < right_count ? -1 : 1;
	while (left_count--)
		if (left[left_count] != right[left_count])
			return left[left_count] < right[left_count] ? -1 : 1;
	return 0;
}

int pigen_integer_compare(const pigen_semantic_model *model,
	pigen_integer_id left_id, pigen_integer_id right_id)
{
	const pigen_integer *left = integer_get(model, left_id);
	const pigen_integer *right = integer_get(model, right_id);
	const uint32_t *left_limbs;
	const uint32_t *right_limbs;
	int comparison;

	if (!left || !right) return 0;
	if (left->negative != right->negative) return left->negative ? -1 : 1;
	left_limbs = integer_limbs(model, left);
	right_limbs = integer_limbs(model, right);
	if ((left->limb_count && !left_limbs) ||
		(right->limb_count && !right_limbs)) return 0;
	comparison = magnitude_compare(left_limbs, left->limb_count,
		right_limbs, right->limb_count);
	return left->negative ? -comparison : comparison;
}

static uint32_t *magnitude_add(const uint32_t *left, size_t left_count,
	const uint32_t *right, size_t right_count, size_t *result_count)
{
	size_t count = left_count > right_count ? left_count : right_count;
	uint32_t *result = pigen_resize(NULL, (count + 1) * sizeof(*result));
	uint64_t carry = 0;
	size_t i;

	for (i = 0; i < count; i++)
	{
		uint64_t sum = carry;
		if (i < left_count) sum += left[i];
		if (i < right_count) sum += right[i];
		result[i] = (uint32_t)sum;
		carry = sum >> LIMB_BITS;
	}
	if (carry) result[count++] = (uint32_t)carry;
	*result_count = count;
	return result;
}

static uint32_t *magnitude_subtract(const uint32_t *larger,
	size_t larger_count, const uint32_t *smaller, size_t smaller_count,
	size_t *result_count)
{
	uint32_t *result = pigen_resize(NULL, larger_count * sizeof(*result));
	uint64_t borrow = 0;
	size_t i;

	for (i = 0; i < larger_count; i++)
	{
		uint64_t subtrahend = (i < smaller_count ? smaller[i] : 0) + borrow;
		uint64_t value = larger[i];
		result[i] = (uint32_t)(value - subtrahend);
		borrow = value < subtrahend;
	}
	*result_count = normalized_count(result, larger_count);
	return result;
}

static uint32_t *magnitude_multiply(const uint32_t *left, size_t left_count,
	const uint32_t *right, size_t right_count, size_t *result_count)
{
	size_t count;
	size_t i;
	uint32_t *result;

	if (!left_count || !right_count)
	{
		*result_count = 0;
		return NULL;
	}
	if (left_count > SIZE_MAX - right_count ||
		left_count + right_count > SIZE_MAX / sizeof(*result)) return NULL;
	count = left_count + right_count;
	result = pigen_resize(NULL, count * sizeof(*result));
	memset(result, 0, count * sizeof(*result));
	for (i = 0; i < left_count; i++)
	{
		uint64_t carry = 0;
		size_t j;
		for (j = 0; j < right_count; j++)
		{
			uint64_t product = (uint64_t)left[i] * right[j] +
				result[i + j] + carry;
			result[i + j] = (uint32_t)product;
			carry = product >> LIMB_BITS;
		}
		result[i + right_count] = (uint32_t)carry;
	}
	*result_count = normalized_count(result, count);
	return result;
}

pigen_integer_id pigen_integer_add(pigen_semantic_model *model,
	pigen_integer_id left_id, pigen_integer_id right_id)
{
	const pigen_integer *left = integer_get(model, left_id);
	const pigen_integer *right = integer_get(model, right_id);
	const uint32_t *left_limbs;
	const uint32_t *right_limbs;
	uint32_t *result_limbs;
	size_t result_count;
	int result_negative;
	int comparison;
	pigen_integer_id result;

	if (!left || !right) return INVALID_ID;
	left_limbs = integer_limbs(model, left);
	right_limbs = integer_limbs(model, right);
	if ((left->limb_count && !left_limbs) ||
		(right->limb_count && !right_limbs)) return INVALID_ID;
	if (left->negative == right->negative)
	{
		result_limbs = magnitude_add(left_limbs, left->limb_count, right_limbs,
			right->limb_count, &result_count);
		result_negative = left->negative;
	}
	else
	{
		comparison = magnitude_compare(left_limbs, left->limb_count,
			right_limbs, right->limb_count);
		if (!comparison) return pigen_integer_intern_u64(model, 0);
		if (comparison > 0)
		{
			result_limbs = magnitude_subtract(left_limbs, left->limb_count,
				right_limbs, right->limb_count, &result_count);
			result_negative = left->negative;
		}
		else
		{
			result_limbs = magnitude_subtract(right_limbs, right->limb_count,
				left_limbs, left->limb_count, &result_count);
			result_negative = right->negative;
		}
	}
	result = integer_intern(model, result_negative, result_limbs, result_count);
	free(result_limbs);
	return result;
}

pigen_integer_id pigen_integer_subtract(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right)
{
	pigen_integer_id negative_right = pigen_integer_negate(model, right);
	return negative_right.index == PIGEN_INVALID_ID ? INVALID_ID :
		pigen_integer_add(model, left, negative_right);
}

static pigen_integer_id integer_multiply_bounded(pigen_semantic_model *model,
	pigen_integer_id left_id, pigen_integer_id right_id, size_t maximum_bits)
{
	const pigen_integer *left = integer_get(model, left_id);
	const pigen_integer *right = integer_get(model, right_id);
	const uint32_t *left_limbs;
	const uint32_t *right_limbs;
	uint32_t *result_limbs;
	size_t count;
	pigen_integer_id result;

	if (!left || !right || !maximum_bits) return INVALID_ID;
	if (!left->limb_count || !right->limb_count)
		return pigen_integer_intern_u64(model, 0);
	if (left->limb_count > SIZE_MAX - right->limb_count) return INVALID_ID;
	left_limbs = integer_limbs(model, left);
	right_limbs = integer_limbs(model, right);
	if (!left_limbs || !right_limbs) return INVALID_ID;
	result_limbs = magnitude_multiply(left_limbs, left->limb_count,
		right_limbs, right->limb_count, &count);
	if (!result_limbs) return INVALID_ID;
	if (count && ((count - 1) > (maximum_bits - 1) / LIMB_BITS ||
		(count - 1) * LIMB_BITS +
			(LIMB_BITS - (size_t)__builtin_clz(result_limbs[count - 1])) >
			maximum_bits))
	{
		free(result_limbs);
		return INVALID_ID;
	}
	result = integer_intern(model, left->negative != right->negative,
		result_limbs, count);
	free(result_limbs);
	return result;
}

pigen_integer_id pigen_integer_multiply(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right)
{
	return integer_multiply_bounded(model, left, right, SIZE_MAX);
}

static size_t magnitude_width(const uint32_t *limbs, size_t count)
{
	if (!count) return 0;
	if (count - 1 > (SIZE_MAX - LIMB_BITS) / LIMB_BITS) return SIZE_MAX;
	return (count - 1) * LIMB_BITS +
		(LIMB_BITS - (size_t)__builtin_clz(limbs[count - 1]));
}

size_t pigen_integer_unsigned_width(const pigen_semantic_model *model,
	pigen_integer_id value)
{
	const pigen_integer *known = integer_get(model, value);
	const uint32_t *limbs;
	if (!known || known->negative) return 0;
	if (!known->limb_count) return 1;
	limbs = integer_limbs(model, known);
	return limbs ? magnitude_width(limbs, known->limb_count) : 0;
}

static int magnitude_is_power_of_two(const uint32_t *limbs, size_t count)
{
	int found = 0;
	size_t i;
	for (i = 0; i < count; i++)
	{
		uint32_t value = limbs[i];
		if (!value) continue;
		if (found || (value & (value - 1))) return 0;
		found = 1;
	}
	return found;
}

size_t pigen_integer_signed_width(const pigen_semantic_model *model,
	pigen_integer_id value)
{
	const pigen_integer *known = integer_get(model, value);
	const uint32_t *limbs;
	size_t width;
	if (!known) return 0;
	if (!known->limb_count) return 1;
	limbs = integer_limbs(model, known);
	if (!limbs) return 0;
	width = magnitude_width(limbs, known->limb_count);
	if (width == SIZE_MAX) return 0;
	return known->negative && magnitude_is_power_of_two(limbs,
		known->limb_count) ? width : width + 1;
}

int pigen_integer_is_zero(const pigen_semantic_model *model,
	pigen_integer_id value)
{
	const pigen_integer *known = integer_get(model, value);
	return known && !known->limb_count;
}

pigen_integer_id pigen_integer_shift_left(pigen_semantic_model *model,
	pigen_integer_id value_id, size_t amount, size_t maximum_bits)
{
	const pigen_integer *value = integer_get(model, value_id);
	const uint32_t *source;
	uint32_t *result_limbs;
	size_t source_width;
	size_t word_shift;
	unsigned bit_shift;
	size_t count;
	size_t i;
	pigen_integer_id result;

	if (!value || !maximum_bits) return INVALID_ID;
	if (!value->limb_count) return value_id;
	source = integer_limbs(model, value);
	if (!source) return INVALID_ID;
	source_width = magnitude_width(source, value->limb_count);
	if (source_width > maximum_bits || amount > maximum_bits - source_width)
		return INVALID_ID;
	word_shift = amount / LIMB_BITS;
	bit_shift = (unsigned)(amount % LIMB_BITS);
	if (value->limb_count > SIZE_MAX - word_shift - 1) return INVALID_ID;
	count = value->limb_count + word_shift + (bit_shift ? 1 : 0);
	result_limbs = pigen_resize(NULL, count * sizeof(*result_limbs));
	memset(result_limbs, 0, count * sizeof(*result_limbs));
	for (i = 0; i < value->limb_count; i++)
	{
		uint64_t shifted = (uint64_t)source[i] << bit_shift;
		result_limbs[i + word_shift] |= (uint32_t)shifted;
		if (bit_shift) result_limbs[i + word_shift + 1] |=
			(uint32_t)(shifted >> LIMB_BITS);
	}
	result = integer_intern(model, value->negative, result_limbs, count);
	free(result_limbs);
	return result;
}

int pigen_integer_is_negative(const pigen_semantic_model *model,
	pigen_integer_id value_id)
{
	const pigen_integer *known = integer_get(model, value_id);

	return known && known->negative;
}

int pigen_integer_is_odd(const pigen_semantic_model *model,
	pigen_integer_id value_id)
{
	const pigen_integer *known = integer_get(model, value_id);
	const uint32_t *limbs = known ? integer_limbs(model, known) : NULL;

	return known && known->limb_count && limbs && (limbs[0] & 1u);
}

int pigen_integer_to_size(const pigen_semantic_model *model,
	pigen_integer_id value_id, size_t *value)
{
	const pigen_integer *known = integer_get(model, value_id);
	const uint32_t *limbs;
	size_t result = 0;
	size_t i;
	if (!known || known->negative || !value) return 0;
	limbs = integer_limbs(model, known);
	if (known->limb_count && !limbs) return 0;
	if (known->limb_count > (sizeof(size_t) * CHAR_BIT + LIMB_BITS - 1) /
		LIMB_BITS) return 0;
	for (i = known->limb_count; i-- > 0; )
	{
		if (result > (SIZE_MAX >> LIMB_BITS)) return 0;
		result = (result << LIMB_BITS) | limbs[i];
	}
	*value = result;
	return 1;
}

int pigen_integer_to_u64(const pigen_semantic_model *model,
	pigen_integer_id value_id, uint64_t *value)
{
	const pigen_integer *known = integer_get(model, value_id);
	const uint32_t *limbs;
	uint64_t result = 0;
	size_t i;

	if (!known || known->negative || !value) return 0;
	limbs = integer_limbs(model, known);
	if (known->limb_count && !limbs) return 0;
	if (known->limb_count > 2) return 0;
	for (i = known->limb_count; i-- > 0; )
		result = (result << LIMB_BITS) | limbs[i];
	*value = result;
	return 1;
}

typedef struct {
	uint32_t *lower;
	size_t lower_count;
	uint32_t *upper;
	size_t upper_count;
	uint64_t shift;
} magnitude_interval;

static void magnitude_interval_free(magnitude_interval *interval)
{
	free(interval->lower);
	free(interval->upper);
	*interval = (magnitude_interval){0};
}

static magnitude_interval magnitude_interval_u64(uint64_t value)
{
	magnitude_interval result = {0};
	uint32_t limbs[2] = {(uint32_t)value, (uint32_t)(value >> LIMB_BITS)};
	size_t count = limbs[1] ? 2 : value ? 1 : 0;

	if (!count) return result;
	result.lower = pigen_resize(NULL, count * sizeof(*result.lower));
	result.upper = pigen_resize(NULL, count * sizeof(*result.upper));
	memcpy(result.lower, limbs, count * sizeof(*limbs));
	memcpy(result.upper, limbs, count * sizeof(*limbs));
	result.lower_count = count;
	result.upper_count = count;
	return result;
}

static size_t magnitude_shift_right(uint32_t *limbs, size_t count,
	size_t amount, int round_up)
{
	size_t word_shift = amount / LIMB_BITS;
	unsigned bit_shift = (unsigned)(amount % LIMB_BITS);
	int discarded = 0;
	size_t i;
	size_t result_count;

	for (i = 0; i < word_shift && i < count; i++)
		discarded |= limbs[i] != 0;
	if (bit_shift && word_shift < count)
		discarded |= (limbs[word_shift] &
			(((uint32_t)1 << bit_shift) - 1)) != 0;
	if (word_shift >= count)
		result_count = 0;
	else
	{
		result_count = count - word_shift;
		for (i = 0; i < result_count; i++)
		{
			uint64_t value = limbs[i + word_shift];
			if (bit_shift && i + word_shift + 1 < count)
				value |= (uint64_t)limbs[i + word_shift + 1] << LIMB_BITS;
			limbs[i] = (uint32_t)(value >> bit_shift);
		}
		result_count = normalized_count(limbs, result_count);
	}
	if (round_up && discarded)
	{
		uint64_t carry = 1;
		for (i = 0; carry && i < result_count; i++)
		{
			uint64_t sum = (uint64_t)limbs[i] + carry;
			limbs[i] = (uint32_t)sum;
			carry = sum >> LIMB_BITS;
		}
		if (carry) limbs[result_count++] = (uint32_t)carry;
		if (!result_count) limbs[result_count++] = 1;
	}
	return result_count;
}

static int magnitude_interval_multiply(const magnitude_interval *left,
	const magnitude_interval *right, size_t precision,
	magnitude_interval *result)
{
	size_t upper_width;
	size_t drop = 0;
	uint64_t shift;

	*result = (magnitude_interval){0};
	if (left->shift > UINT64_MAX - right->shift) return 0;
	shift = left->shift + right->shift;
	result->lower = magnitude_multiply(left->lower, left->lower_count,
		right->lower, right->lower_count, &result->lower_count);
	result->upper = magnitude_multiply(left->upper, left->upper_count,
		right->upper, right->upper_count, &result->upper_count);
	if (!result->lower || !result->upper)
	{
		magnitude_interval_free(result);
		return 0;
	}
	upper_width = magnitude_width(result->upper, result->upper_count);
	if (upper_width > precision) drop = upper_width - precision;
	if (drop)
	{
		result->lower_count = magnitude_shift_right(result->lower,
			result->lower_count, drop, 0);
		result->upper_count = magnitude_shift_right(result->upper,
			result->upper_count, drop, 1);
		if (shift > UINT64_MAX - drop)
		{
			magnitude_interval_free(result);
			return 0;
		}
		shift += (uint64_t)drop;
	}
	result->shift = shift;
	return 1;
}

/* Calculates only the exact bit width.  Dyadic intervals retain bounded
 * leading magnitudes while exponentiation by squaring discards irrelevant
 * low bits; precision grows only when a power-of-two boundary is unresolved. */
int pigen_integer_power_width_u64(uint64_t base, uint64_t exponent,
	uint64_t *width)
{
	size_t precision = 128;

	if (!width) return 0;
	if (!exponent || base <= 1)
	{
		*width = 1;
		return 1;
	}
	if (!(base & (base - 1)))
	{
		uint64_t base_log = 63u - (uint64_t)__builtin_clzll(base);
		if (exponent > (UINT64_MAX - 1) / base_log) return 0;
		*width = exponent * base_log + 1;
		return 1;
	}
	for (;;)
	{
		magnitude_interval result = magnitude_interval_u64(1);
		magnitude_interval factor = magnitude_interval_u64(base);
		uint64_t power = exponent;
		int valid = result.lower && result.upper && factor.lower && factor.upper;

		while (valid && power)
		{
			magnitude_interval product;
			if (power & 1)
			{
				valid = magnitude_interval_multiply(&result, &factor,
					precision, &product);
				magnitude_interval_free(&result);
				result = product;
			}
			power >>= 1;
			if (valid && power)
			{
				valid = magnitude_interval_multiply(&factor, &factor,
					precision, &product);
				magnitude_interval_free(&factor);
				factor = product;
			}
		}
		magnitude_interval_free(&factor);
		if (valid)
		{
			uint64_t lower_width = result.shift;
			uint64_t upper_width = result.shift;
			size_t lower_magnitude_width = magnitude_width(result.lower,
				result.lower_count);
			size_t upper_magnitude_width = magnitude_width(result.upper,
				result.upper_count);

			if (lower_width > UINT64_MAX - lower_magnitude_width)
				valid = 0;
			else
			{
				lower_width += (uint64_t)lower_magnitude_width;
				if (upper_width <= UINT64_MAX - upper_magnitude_width)
				{
					upper_width += (uint64_t)upper_magnitude_width;
					if (lower_width == upper_width)
					{
						*width = lower_width;
						magnitude_interval_free(&result);
						return 1;
					}
				}
			}
		}
		magnitude_interval_free(&result);
		if (!valid || precision > SIZE_MAX / 2) return 0;
		precision *= 2;
	}
}

pigen_integer_id pigen_integer_power(pigen_semantic_model *model,
	pigen_integer_id base_id, pigen_integer_id exponent_id, size_t maximum_bits)
{
	const pigen_integer *base = integer_get(model, base_id);
	const pigen_integer *exponent = integer_get(model, exponent_id);
	pigen_integer_id one;
	pigen_integer_id negative_one;
	pigen_integer_id result;
	pigen_integer_id factor;
	size_t power;
	int base_is_zero;
	int exponent_is_zero;
	int exponent_is_negative;
	int exponent_is_odd;

	if (!base || !exponent || !maximum_bits)
		return INVALID_ID;
	base_is_zero = !base->limb_count;
	exponent_is_zero = !exponent->limb_count;
	exponent_is_negative = exponent->negative;
	exponent_is_odd = exponent->limb_count &&
		(integer_limbs(model, exponent)[0] & 1u);
	if (exponent_is_negative) return INVALID_ID;
	one = pigen_integer_intern_u64(model, 1);
	if (one.index == PIGEN_INVALID_ID) return INVALID_ID;
	if (exponent_is_zero) return one;
	if (base_is_zero) return pigen_integer_intern_u64(model, 0);
	if (pigen_integer_compare(model, base_id, one) == 0) return one;
	negative_one = pigen_integer_negate(model, one);
	if (negative_one.index == PIGEN_INVALID_ID) return INVALID_ID;
	if (pigen_integer_compare(model, base_id, negative_one) == 0)
		return exponent_is_odd ? negative_one : one;
	if (!pigen_integer_to_size(model, exponent_id, &power)) return INVALID_ID;
	result = one;
	factor = base_id;
	while (power)
	{
		if (power & 1)
		{
			result = integer_multiply_bounded(model, result, factor,
				maximum_bits);
			if (result.index == PIGEN_INVALID_ID) return INVALID_ID;
		}
		power >>= 1;
		if (power)
		{
			factor = integer_multiply_bounded(model, factor, factor,
				maximum_bits);
			if (factor.index == PIGEN_INVALID_ID) return INVALID_ID;
		}
	}
	return result;
}
