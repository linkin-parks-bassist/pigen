#include "pigen/compile.h"
#include <stdlib.h>

/*
 * Skeleton implementation of the compiler composition interface.
 *
 * This is a quarantined skeleton: it provides compilable safe stubs that
 * cannot falsely report successful compilation. No actual phase composition,
 * lowering, or emission is implemented here.
 *
 * All compile attempts fail with a diagnostic, preserving the native
 * origin/span error contract.
 */

int pigen_compile_source(pigen_source_manager *manager,
    pigen_source_id source,
    const pigen_compile_options *options,
    pigen_compile_result *result,
    pigen_output_error *error)
{
    /* Zero the result on entry. */
    if (result) {
        result->systemverilog = NULL;
        result->length = 0;
    }

    /*
     * The skeleton does not implement actual compilation. Any attempt to
     * compile returns failure with a diagnostic. This prevents false
     * positives where compilation appears to succeed.
     *
     * The manager, source, and options parameters are accepted to match the
     * contract but are not used in the skeleton stub.
     */
    (void)manager;
    (void)source;
    (void)options;

    if (error) {
        error->span = (pigen_source_span){0};
        error->message = "compile-interface-skeleton: compilation not implemented (skeleton stub)";
    }

    return -1;
}

void pigen_free_compile_result(pigen_compile_result *result)
{
    if (result && result->systemverilog) {
        free(result->systemverilog);
        result->systemverilog = NULL;
        result->length = 0;
    }
}