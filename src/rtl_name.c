/* Collision-safe RTL name identity for the elastic RTL vertical slice.
 * Stubs: no collision suffixing, span copying or text extraction. */
#include "pigen/rtl_name.h"

pigen_rtl_name_id pigen_rtl_assign_names(pigen_rtl_model *model,
	const pigen_rtl_name_request *requests, size_t request_count)
{
	(void)model;
	(void)requests;
	(void)request_count;
	return (pigen_rtl_name_id){PIGEN_INVALID_ID};
}

const pigen_rtl_name *pigen_rtl_name_get(const pigen_rtl_model *model,
	pigen_rtl_name_id name)
{
	(void)model;
	(void)name;
	return NULL;
}
