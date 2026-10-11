#include "check.h"
#include "pigen/sv_emit.h"

int main(int argc, char **argv)
{
	check_init(argc, argv);

	SECTION("t10-skeleton") {
		pigen_source_manager sources = {0};
		pigen_output_model layout = {0};
		pigen_rtl_model rtl = {0};
		char *out = (char *)"sentinel";
		int ret;

		/* The stub is unimplemented: it returns -1 and leaves
		 * *output_string NULL, no partial output. */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, &out);
		REQUIRE(ret == -1);
		REQUIRE(out == NULL);

		/* A NULL out pointer is tolerated (the stub touches nothing). */
		ret = pigen_emit_systemverilog(&sources, &layout, &rtl, NULL);
		REQUIRE(ret == -1);

		(void)out;
	}

	return check_finish();
}
