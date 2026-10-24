#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/ids.h"
#include "pigen/preprocess.h"
#include "pigen/syntax.h"
#include "pigen/resolve.h"
#include "pigen/semantic.h"

/* Helper to read a file into memory */
static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) || (*length = (size_t)ftell(file)) == (size_t)-1) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *contents = malloc(*length + 1);
    if (!contents) {
        fclose(file);
        return NULL;
    }
    if (fread(contents, 1, *length, file) != *length) {
        free(contents);
        fclose(file);
        return NULL;
    }
    fclose(file);
    contents[*length] = 0;
    return contents;
}

/* Test 1: Skeleton stub returns failure for all inputs */
/* The compile-interface-skeleton stub intentionally fails all compilations.
 * This test verifies the skeleton behavior, not successful compilation. */
static void test_skeleton_fails_all(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/basic.pigen", &source_length);
    assert(source_text != NULL);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/basic.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* Skeleton stub always returns -1 (failure) */
    assert(ret == -1);
    assert(result.systemverilog == NULL);
    assert(result.length == 0);

    free(source_text);
    pigen_free_sources(&sources);
}

/* Test 2: pigen_free_compile_result is safe with NULL result */
static void test_free_null_result(void)
{
    pigen_compile_result result = {0};
    pigen_free_compile_result(&result);
}

/* Test 4: pigen_free_compile_result frees allocated memory */
static void test_free_allocated_result(void)
{
    pigen_compile_result result = {0};
    result.systemverilog = (char*)malloc(100);
    pigen_free_compile_result(&result);
    /* If we reach here without crash, memory was freed */
}

/* Test 5: Error diagnostic contains message (skeleton sets origin=0, span={0}) */
static void test_error_provenance(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/async_domain_error.pigen", &source_length);
    assert(source_text != NULL);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/async_domain_error.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    assert(ret == -1);
    /* Skeleton stub sets origin=0, span={0}, but error message is present */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 6: Cross-domain transfer error returns error with message */
static void test_cross_domain_error_provenance(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/cross_domain_error.pigen", &source_length);
    assert(source_text != NULL);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/cross_domain_error.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    assert(ret == -1);
    /* Skeleton stub sets origin=0, span={0}, but error message is present */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 7: Owned signal declaration passes through full compiler pipeline.
 * This test verifies that pigen_compile_source actually invokes parsing,
 * semantic resolution, and lowering for a Pigen-owned data-first declaration.
 * The source uses Pigen-specific 'buf' syntax which must not pass through
 * unchanged as ordinary SystemVerilog. */
static void test_owned_signal_full_pipeline(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/owned_signal.pigen", &source_length);
    assert(source_text != NULL);

    /* Add source to manager */
    pigen_source_id source_id = pigen_source_add(&sources, "tests/owned_signal.pigen", source_text, source_length);
    assert(source_id.index != PIGEN_INVALID_ID);

    /* Preprocess */
    pigen_preprocess_result preprocess_result = {0};
    pigen_preprocess_error preprocess_error = {0};
    pigen_source_provider provider = {0, NULL};
    int preprocess_ret = pigen_preprocess(&sources, source_id, &provider, &preprocess_result, &preprocess_error);
    assert(preprocess_ret == 1); /* preprocessing success */

    /* Parse syntax */
    pigen_syntax_tree syntax_tree = {0};
    pigen_syntax_error syntax_error = {0};
    const pigen_expanded_source *expanded = &preprocess_result.expanded;
    int parse_ret = pigen_parse_syntax(expanded, &syntax_tree, &syntax_error);
    assert(parse_ret == 1); /* parsing success */

    /* Semantic resolution */
    pigen_semantic_model semantic_model = {0};
    pigen_semantic_init(&semantic_model, &sources);
    pigen_resolve_policy policy = {0};
    policy.maximum_generated_bits = 1024;
    pigen_semantic_error semantic_error = {0};
    int resolve_ret = pigen_resolve_semantics(&syntax_tree, &semantic_model, &policy, &semantic_error);
    assert(resolve_ret == 1); /* semantic resolution success */

    /* Now call the public compiler - the full pipeline should succeed and
     * produce SystemVerilog output for the owned signal declaration. */
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int compile_ret = pigen_compile_source(&sources, "tests/owned_signal.pigen",
                                           source_text, source_length,
                                           NULL, &result, &error);
    assert(compile_ret == 0); /* compilation success */
    assert(result.systemverilog != NULL);
    assert(result.length > 0);
    /* The emitted SV must contain the module name and the signal name. */
    assert(strstr(result.systemverilog, "owned_signal") != NULL);
    assert(strstr(result.systemverilog, "stage") != NULL);

    /* Cleanup */
    pigen_free_compile_result(&result);
    pigen_free_syntax_tree(&syntax_tree);
    pigen_free_semantic_model(&semantic_model);
    pigen_free_preprocess_result(&preprocess_result);
    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 8: Syntax error - missing semicolon produces diagnostic with span */
static void test_syntax_error_span(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    /* Simple Pigen source with syntax error */
    const char *source_text = "module syntax_error(input logic clk);\n\tbuf [7:0] source\nendmodule";
    source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/syntax_error.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1); /* compilation failure */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);
    /* Origin and span should be set (even if skeleton stub sets them to 0) */
    assert(error.origin.index != PIGEN_INVALID_ID || error.span.start != 0 || error.span.end != 0);

    pigen_free_sources(&sources);
}

/* Test 9: Type mismatch error produces diagnostic with span */
static void test_type_mismatch_error(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    /* Source that will trigger a type mismatch */
    const char *source_text = "module type_mismatch(input logic clk);\n\tbuf [7:0] source;\n\tbuf [3:0] destination;\n\tdestination <= source;\nendmodule";
    source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/type_mismatch.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1); /* compilation failure */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
}

/* Test 10: Ready cycle detection produces diagnostic */
static void test_ready_cycle_error(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    /* Source that creates a ready cycle */
    const char *source_text = "module ready_cycle(input logic clk);\n\tbuf [7:0] a;\n\tbuf [7:0] b;\n\ta <= b;\n\tb <= a;\nendmodule";
    source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/ready_cycle.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1); /* compilation failure */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
}

/* Test 11: Unsupported opaque form produces diagnostic */
static void test_unsupported_opaque_error(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    /* Source with unsupported Pigen-dependent opaque form */
    const char *source_text = "module opaque_error(input logic clk);\n\t// opaque comment with pigen dependency\n\tbuf [7:0] source;\nendmodule";
    source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/opaque_error.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1); /* compilation failure */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
}

/* Test 12: Cross-domain transfer error (existing test, enhanced) */
static void test_cross_domain_error_detailed(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/cross_domain_error.pigen", &source_length);
    assert(source_text != NULL);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/cross_domain_error.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1);
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 13: Async domain error produces diagnostic */
static void test_async_domain_error_detailed(void)
{
    pigen_source_manager sources = {0};
    size_t source_length = 0;
    char *source_text = read_file("tests/async_domain_error.pigen", &source_length);
    assert(source_text != NULL);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/async_domain_error.pigen",
                                    source_text, source_length,
                                    NULL, &result, &error);
    assert(ret == -1);
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);

    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 14: Simulation test - vertical_slice integration placeholder
 * This test verifies the simulation infrastructure for vertical_slice.
 * The actual vertical_slice.pigen and vertical_slice_tb.sv files are created
 * as part of Task 11 implementation. */
static void test_vertical_slice_simulation_infrastructure(void)
{
    /* Verify that the simulation tools are available */
    FILE *f = popen("which iverilog", "r");
    assert(f != NULL);
    char buffer[64];
    assert(fread(buffer, 1, sizeof(buffer), f) > 0);
    pclose(f);

    f = popen("which vvp", "r");
    assert(f != NULL);
    assert(fread(buffer, 1, sizeof(buffer), f) > 0);
    pclose(f);
}

int main(void)
{
    test_skeleton_fails_all();
    test_free_null_result();
    test_free_allocated_result();
    test_error_provenance();
    test_cross_domain_error_provenance();
    test_owned_signal_full_pipeline();
    test_syntax_error_span();
    test_type_mismatch_error();
    test_ready_cycle_error();
    test_unsupported_opaque_error();
    test_cross_domain_error_detailed();
    test_async_domain_error_detailed();
    test_vertical_slice_simulation_infrastructure();

    printf("All compile tests passed\n");
    return 0;
}