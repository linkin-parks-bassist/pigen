#ifndef PIGEN_COMPILE_H
#define PIGEN_COMPILE_H

#include <stddef.h>

#include "pigen/source.h"
#include "pigen/ids.h"

/* Compilation options. */
typedef struct {
    size_t maximum_generated_bits;
} pigen_compile_options;

/* Compile error: preserves native origin and source span. */
typedef struct {
    pigen_origin_id origin;
    pigen_source_span span;
    const char *message;
} pigen_compile_error;

/* Compiled result: contains the generated SystemVerilog output. */
typedef struct {
    char *systemverilog;
    size_t length;
} pigen_compile_result;

/*
 * Compile a Pigen source file.
 *
 * parameters:
 *   sources     - source manager owning the input sources
 *   source_path - path to the source file (for diagnostics)
 *   source_text - contents of the source file
 *   source_length - length of source_text
 *   options     - compilation options (e.g. maximum_generated_bits)
 *   result      - output: generated SystemVerilog text
 *   error       - optional diagnostic on failure; if non-NULL, filled on error
 *
 * returns:
 *   0 on success, -1 on failure
 *
 * On success, result->systemverilog points to allocated memory that must be
 * freed via pigen_free_compile_result(). On failure, result is zeroed and
 * error (if provided) contains the diagnostic.
 */
int pigen_compile_source(pigen_source_manager *sources,
    const char *source_path,
    const char *source_text,
    size_t source_length,
    const pigen_compile_options *options,
    pigen_compile_result *result,
    pigen_compile_error *error);

/*
 * Free a compile result. After this call, result is zeroed.
 */
void pigen_free_compile_result(pigen_compile_result *result);

#endif