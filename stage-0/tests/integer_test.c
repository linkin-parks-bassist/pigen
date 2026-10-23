#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/integer.h"
#include "pigen/semantic.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_integer_id decimal(pigen_semantic_model *model, const char *text)
{
	return pigen_integer_intern_decimal(model, text, strlen(text));
}

static void assert_equal(const pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right)
{
	assert(left.index != PIGEN_INVALID_ID);
	assert(right.index != PIGEN_INVALID_ID);
	assert(pigen_integer_compare(model, left, right) == 0);
}

int main(void)
{
	pigen_source_manager sources = {0};
	pigen_semantic_model model;
	pigen_integer_id zero;
	pigen_integer_id one;
	pigen_integer_id huge;
	pigen_integer_id negative_huge;
	pigen_integer_id sum;
	pigen_integer_id product;
	pigen_integer_id shifted;
	pigen_integer_id powered;
	pigen_integer_id beyond_size;
	size_t size_value;
	uint64_t u64_value;
	uint64_t power_width;

	pigen_semantic_init(&model, &sources);
	zero = decimal(&model, "0");
	one = decimal(&model, "1");
	huge = decimal(&model, "1208925819614629174706175");
	assert(zero.index != PIGEN_INVALID_ID);
	assert(one.index != PIGEN_INVALID_ID);
	assert(huge.index != PIGEN_INVALID_ID);
	assert(zero.index == decimal(&model, "000_000").index);
	assert(huge.index ==
		decimal(&model, "001208925819614629174706175").index);
	assert(decimal(&model, "").index == PIGEN_INVALID_ID);
	assert(decimal(&model, "_").index == PIGEN_INVALID_ID);
	assert(decimal(&model, "12x3").index == PIGEN_INVALID_ID);
	assert(decimal(&model, "-1").index == PIGEN_INVALID_ID);

	negative_huge = pigen_integer_negate(&model, huge);
	assert(negative_huge.index != PIGEN_INVALID_ID);
	assert(pigen_integer_compare(&model, negative_huge, huge) < 0);
	assert(pigen_integer_negate(&model, zero).index == zero.index);
	assert(pigen_integer_negate(&model, negative_huge).index == huge.index);
	sum = pigen_integer_add(&model, negative_huge, huge);
	assert(sum.index == zero.index);
	assert(pigen_integer_is_zero(&model, sum));

	assert_equal(&model,
		pigen_integer_add(&model, decimal(&model, "100"),
			pigen_integer_negate(&model, decimal(&model, "40"))),
		decimal(&model, "60"));
	assert_equal(&model,
		pigen_integer_subtract(&model, decimal(&model, "40"),
			decimal(&model, "100")),
		pigen_integer_negate(&model, decimal(&model, "60")));
	assert_equal(&model,
		pigen_integer_multiply(&model,
			pigen_integer_negate(&model, decimal(&model, "12")),
			decimal(&model, "11")),
		pigen_integer_negate(&model, decimal(&model, "132")));

	product = pigen_integer_multiply(&model, huge, huge);
	assert(product.index != PIGEN_INVALID_ID);
	assert(pigen_integer_unsigned_width(&model, product) == 160);
	assert(pigen_integer_signed_width(&model, negative_huge) == 81);
	assert(pigen_integer_signed_width(&model, decimal(&model, "127")) == 8);
	assert(pigen_integer_signed_width(&model, decimal(&model, "128")) == 9);
	assert(pigen_integer_signed_width(&model,
		pigen_integer_negate(&model, decimal(&model, "128"))) == 8);
	assert(pigen_integer_unsigned_width(&model, zero) == 1);
	assert(pigen_integer_unsigned_width(&model, negative_huge) == 0);
	assert(pigen_integer_to_size(&model, zero, &size_value) && !size_value);
	assert(pigen_integer_to_size(&model,
		pigen_integer_intern_u64(&model, UINT32_MAX), &size_value) &&
		size_value == UINT32_MAX);
	assert(pigen_integer_to_size(&model,
		pigen_integer_intern_u64(&model, (uint64_t)SIZE_MAX), &size_value) &&
		size_value == SIZE_MAX);
	beyond_size = pigen_integer_shift_left(&model, one,
		sizeof(size_t) * 8, sizeof(size_t) * 8 + 1);
	assert(beyond_size.index != PIGEN_INVALID_ID);
	assert(!pigen_integer_to_size(&model, beyond_size, &size_value));
	assert(pigen_integer_to_u64(&model,
		pigen_integer_intern_u64(&model, UINT64_MAX), &u64_value));
	assert(u64_value == UINT64_MAX);
	assert(!pigen_integer_to_u64(&model, beyond_size, &u64_value));

	shifted = pigen_integer_shift_left(&model, one, 127, 128);
	assert(shifted.index != PIGEN_INVALID_ID);
	assert(pigen_integer_unsigned_width(&model, shifted) == 128);
	assert(pigen_integer_shift_left(&model, one, 128, 128).index ==
		PIGEN_INVALID_ID);
	powered = pigen_integer_power(&model, decimal(&model, "2"),
		decimal(&model, "10"), 128);
	assert_equal(&model, powered, decimal(&model, "1024"));
	assert(pigen_integer_power(&model, one, huge, 128).index == one.index);
	assert_equal(&model,
		pigen_integer_power(&model, pigen_integer_negate(&model, one), huge,
			128),
		pigen_integer_negate(&model, one));
	assert(pigen_integer_power(&model, decimal(&model, "2"), huge, 128).index ==
		PIGEN_INVALID_ID);
	assert(pigen_integer_power(&model, decimal(&model, "2"),
		pigen_integer_negate(&model, one), 128).index == PIGEN_INVALID_ID);
	assert(pigen_integer_power_width_u64(3, 3, &power_width));
	assert(power_width == 5);
	assert(pigen_integer_power_width_u64(3, 1000000000, &power_width));
	assert(power_width == 1584962501);
	assert(pigen_integer_power_width_u64(UINT64_MAX, 2, &power_width));
	assert(power_width == 128);
	{
		uint64_t base_value;
		uint64_t exponent_value;

		for (base_value = 0; base_value < 256; base_value++)
			for (exponent_value = 0; exponent_value < 17; exponent_value++)
			{
				pigen_integer_id exact_power = pigen_integer_power(&model,
					pigen_integer_intern_u64(&model, base_value),
					pigen_integer_intern_u64(&model, exponent_value), 256);

				assert(exact_power.index != PIGEN_INVALID_ID);
				assert(pigen_integer_power_width_u64(base_value,
					exponent_value, &power_width));
				assert(power_width ==
					pigen_integer_unsigned_width(&model, exact_power));
			}
	}
	assert(pigen_integer_add(&model, INVALID_ID(pigen_integer_id), one).index ==
		PIGEN_INVALID_ID);

	pigen_free_semantic_model(&model);
	puts("PASS: exact integers canonicalize beyond host width");
	return 0;
}
