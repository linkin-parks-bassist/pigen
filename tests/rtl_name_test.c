#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pigen/rtl_name.h"

int main(void)
{
	pigen_rtl_model model = {0};

	assert(!pigen_rtl_name_get(&model, (pigen_rtl_name_id){PIGEN_INVALID_ID}));
	assert(!pigen_rtl_name_get(&model, (pigen_rtl_name_id){0}));
	assert(!pigen_rtl_name_get(NULL, (pigen_rtl_name_id){0}));
	assert(pigen_rtl_assign_names(&model, 0, 0).index == PIGEN_INVALID_ID);
	assert(pigen_rtl_assign_names(NULL, 0, 0).index == PIGEN_INVALID_ID);
	assert(!model.name_count && !model.name_capacity);

	free(model.names);
	model = (pigen_rtl_model){0};
	assert(!model.names && !model.name_count && !model.name_capacity);
	puts("PASS: rtl name identity shape with checked accessors and stubs");
	return 0;
}
