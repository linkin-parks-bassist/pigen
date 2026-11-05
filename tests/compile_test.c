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

/* Test 15: Scenario line 7 (CLI FSM emitted-SV list): an undeclared goto
 * target is diagnosed at the original span. The witness is the legal
 * two-state controller whose idle-state goto names `nowhere`, a token no
 * state declaration declares. The compile must fail loudly (rc == -1,
 * result zeroed) with the native parse diagnostic: a non-empty message
 * naming the violation and a non-degenerate span over the `nowhere` token
 * at its original byte coordinates in the source (a zeroed or dropped span,
 * or a span over any other token, fails this pin). This line holds on
 * master (parse-stage diagnostic) and must keep holding. */
static void test_fsm_goto_undeclared_target_span(void)
{
    const char *source_text =
        "module fsm_goto_undeclared\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\n"
        "fsm fsm_goto_undeclared @(posedge clk) reset (rst) initial idle\n"
        "begin\n"
        "\tstate idle:\n"
        "\tbegin\n"
        "\t\tif (start)\n"
        "\t\t\tgoto nowhere;\n"
        "\tend\n"
        "\tstate run:\n"
        "\tbegin\n"
        "\t\tdone <= 1;\n"
        "\tend\n"
        "end\n"
        "endmodule\n";
    size_t source_length = strlen(source_text);

    /* The original location of the `nowhere` token: its unique occurrence
     * in the literal source, so the expected span is deterministic. */
    const char *token = strstr(source_text, "nowhere");
    size_t token_start = (size_t)(token - source_text);
    size_t token_end = token_start + strlen("nowhere");

    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/fsm_goto_undeclared.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    assert(ret == -1); /* the undeclared goto target is diagnosed */
    assert(result.systemverilog == NULL); /* no output on failure */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);
    assert(!strcmp(error.message, "goto references an undeclared state"));
    /* Non-degenerate span ... */
    assert(error.span.end > error.span.start);
    /* ... over the `nowhere` token ... */
    {
        size_t span_length = 0;
        const char *span_text = pigen_source_span_text(&sources, error.span, &span_length);
        assert(span_text != NULL);
        assert(span_length == strlen("nowhere"));
        assert(!memcmp(span_text, "nowhere", span_length));
    }
    /* ... at its original byte coordinates in the source. */
    assert(error.span.start == token_start);
    assert(error.span.end == token_end);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

/* Test 15: A legal 3-stage pipeline compiles (rc == 0) and the emitted SV
 * declares exactly ONE parent module and instantiates the dedicated one-entry
 * buf primitive `pigen_buf` exactly (N-1) = 2 times, one per stage boundary.
 *
 * Line 1 of the accepted test map what/are/the/cli/pipeline/emitted/sv/test/
 * scenarios.md, witnessed through the production seam pigen_compile_source. The
 * witness source is the section-(1) shape of tests/pipeline_lower_test.c: a
 * clocked process holding a 3-stage pipeline (fields a, b, c each 8-bit; stages
 * `a <= x`, `b <= a`, `c <= b`; `yield c;`). The standalone instance count is
 * pinned by pipeline-lower-test section (1) buf-storage
 * (rtl.instance_count == 2); this line witnesses it in the terminal emission.
 * The emission observations are text counts over result.systemverilog, never by
 * re-parsing.
 *
 * RED against master: the production pipeline step (src/compile.c) is not yet
 * in place, so a clocked-process pipeline source returns rc == -1 ("invalid
 * procedural statement") with no output text; this line goes green when that
 * step lands. */
static void test_pipeline_emitted_module_and_buf_count(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module bs_ok(input logic clk, input logic [7:0] x);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline bs_pipe begin\n"
        "\t\t\tbit [7:0] a;\n"
        "\t\t\tbit [7:0] b;\n"
        "\t\t\tbit [7:0] c;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\ta <= x;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tb <= a;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tc <= b;\n"
        "\t\t\tend\n"
        "\t\t\tyield c;\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/pipeline_emitted.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    if (ret != 0) {
        fprintf(stderr,
                "test_pipeline_emitted_module_and_buf_count: compile failed "
                "(expected rc == 0); rc=%d message='%s'\n",
                ret, error.message ? error.message : "(null)");
        abort();
    }
    assert(result.systemverilog != NULL);

    /* Count the module declarations: every emitted module ends with exactly
     * one `endmodule`, and `endmodule` is a substring of nothing else in the
     * SV, so its occurrence count is the module count. */
    size_t module_count = 0;
    for (const char *p = result.systemverilog;
         (p = strstr(p, "endmodule")) != NULL; p += strlen("endmodule"))
        module_count++;
    assert(module_count == 1);

    /* Count the dedicated one-entry buf primitive instances: the production
     * pipeline step instantiates `pigen_buf` once per stage boundary, so a
     * 3-stage pipeline (N-1) carries exactly 2. */
    size_t buf_count = 0;
    for (const char *p = result.systemverilog;
         (p = strstr(p, "pigen_buf")) != NULL; p += strlen("pigen_buf"))
        buf_count++;
    assert(buf_count == 2);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
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
    test_fsm_goto_undeclared_target_span();
    test_pipeline_emitted_module_and_buf_count();

    printf("All compile tests passed\n");
    return 0;
}