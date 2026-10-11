#include "pigen/sv_emit.h"

static int render_expression(
	const pigen_rtl_model *rtl, const pigen_rtl_expr *expr, char **buf)
{
	(void)rtl;
	(void)expr;
	(void)buf;
	return -1;
}

int pigen_sv_render_expression(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *expr, char **text)
{
	if (text)
		*text = NULL;
	if (!rtl || !expr || !text)
		return -1;
	return render_expression(rtl, expr, text);
}

int pigen_emit_systemverilog(const pigen_source_manager *sources,
	const pigen_output_model *layout, const pigen_rtl_model *rtl,
	char **output_string)
{
	(void)sources;
	(void)layout;
	(void)rtl;
	if (output_string)
		*output_string = NULL;
	return -1;
}
