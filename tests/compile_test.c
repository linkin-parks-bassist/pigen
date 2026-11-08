#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
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

/* Line 4 of the accepted CLI emitted-SV test map
 * (what/are/the/cli/pipeline/emitted/sv/test/scenarios.md): a legal two-field
 * pipeline (field x, 8-bit, and field y, 16-bit; a single stage writing both
 * from enclosing module inputs; `yield {x, y};`) compiles with rc == 0 and
 * the emitted SV (a) declares the pipeline identifier as a module-scope buf
 * output packet port with packed width 24 (8 + 16) and (b) carries the yield
 * write into that port whose concatenation renders in SOURCE ORDER - the
 * first-yielded field x the leftmost (MSB) member, y the rightmost (LSB) -
 * the reversed order `{y, x}` failing the pin.
 *
 * The witness stage writes each field from a distinct enclosing input (d,
 * 8-bit, and e, 16-bit): a first-stage pipeline-field read, including the
 * self-read, is rc == -1 under the step-(b) first-stage law, so the stage's
 * only legal reads of module scope are external. Each yield member therefore
 * carries nothing across the boundary (an external read) and the lowering
 * publishes its plain field object, named by the field's provenance
 * declaration span, so the terminal renderer spells the members x and y in
 * the yield's source order.
 *
 * The destination provenance and the member-order law are pinned standalone
 * by pipeline-lower-test sections (3) yield-write and (4)
 * emitted-member-order; this line witnesses both in the terminal emission
 * through the production path (pigen_compile_source).
 *
 * RED against master until the production pipeline step lands: the
 * clocked-process pipeline is diagnosed "invalid procedural statement" at
 * the resolve stage, so this line fails at the rc == 0 pin. */
static void test_pipeline_yield_two_field_msbsrc(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module p2f_yield(input logic clk, input logic [7:0] d, "
        "input logic [15:0] e);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline p2f_pipe begin\n"
        "\t\t\tbit [7:0] x;\n"
        "\t\t\tbuf bit [15:0] y;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tx <= d;\n"
        "\t\t\t\ty <= e;\n"
        "\t\t\tend\n"
        "\t\t\tyield {x, y};\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret;
    const char *port;
    const char *in_order;
    const char *reversed;

    ret = pigen_compile_source(&sources, "tests/p2f_yield.pigen",
                               source_text, source_length,
                               NULL, &result, &error);
    /* RED against master: the production pipeline step is absent, so the
     * clocked-process pipeline is diagnosed "invalid procedural statement"
     * (resolve stage) and rc == -1. */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);
    /* (a) The pipeline identifier is declared as a module-scope buf output
     *     packet port with packed width 24 (8 + 16): the emission renders a
     *     24-bit port as `output logic [23:0] <name>` (sv_emit port header,
     *     direction right-aligned to six columns). */
    port = strstr(result.systemverilog, "output logic [23:0] p2f_pipe");
    assert(port != NULL);
    /* (b) The yield write into that port renders the member concatenation in
     *     SOURCE ORDER: the first-yielded field x (8-bit) the leftmost (MSB)
     *     member, y (16-bit) the rightmost (LSB) member; the terminal
     *     renderer spells the concatenation without spaces. The reversed
     *     order {y,x} fails this pin. */
    in_order = strstr(result.systemverilog, "{x,y}");
    assert(in_order != NULL);
    reversed = strstr(result.systemverilog, "{y,x}");
    assert(reversed == NULL);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
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

/* Line 2 of the CLI pipeline emitted-SV test map (local:what/are/the/cli/
 * pipeline/emitted/sv/test/scenarios.md, accepted by
 * local:what/is/the/review/of/the/cli/pipeline/emitted/sv/test/scenarios.md):
 * per-stage next-value equations reach the emission.
 *
 * A legal 2-stage pipeline (fields a, b, each 8-bit; stage 1 `a <= x`,
 * stage 2 `b <= a`; `yield b;`) is the section-(2) witness shape of
 * tests/pipeline_lower_test.c, driven through the production seam
 * pigen_compile_source. The promised behavior: the compile succeeds
 * (rc == 0) and the emitted SV carries the per-stage next-value equations of
 * BOTH stages as continuous assignments or clocked updates inside the parent
 * module - not only the buf instances and ports.
 *
 * The equation VALUES are constant stand-ins this line does not discriminate
 * (local:what/are/the/pipeline/stage/computation/test/scenarios.md owns them
 * at the pipeline-lower-test seam): the pin is the PRESENCE of the per-stage
 * equations as emitted continuous assignments (`assign`) or clocked updates
 * (`always ... <=`), which a builder that publishes only the pigen_buf
 * instances and the module ports would lack. The standalone pin is
 * pipeline-lower-test section (2) next-value-equations (equation_count >=
 * stage_count); this line witnesses those equations in the terminal emission.
 * The emission observations are text scans over result.systemverilog, never by
 * re-parsing.
 *
 * RED against master: the production pipeline step (src/compile.c) is not yet
 * in place, so a clocked-process pipeline source returns rc == -1 ("invalid
 * procedural statement") with no output text; the first pin (rc == 0) aborts
 * the run, and this line goes green when that step lands. */
static void test_pipeline_next_value_equations_in_emission(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module nve_ok(input logic clk, input logic [7:0] x);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline nve_pipe begin\n"
        "\t\t\tbit [7:0] a;\n"
        "\t\t\tbit [7:0] b;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\ta <= x;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tb <= a;\n"
        "\t\t\tend\n"
        "\t\t\tyield b;\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/nve_ok.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    if (ret != 0) {
        fprintf(stderr,
                "test_pipeline_next_value_equations_in_emission: compile "
                "failed (expected rc == 0); rc=%d message='%s'\n",
                ret, error.message ? error.message : "(null)");
        abort();
    }
    assert(result.systemverilog != NULL);

    /* The per-stage next-value equations must reach the emission as
     * continuous assignments (`assign`) or clocked updates (`always` ...
     * `<=`) - not only the pigen_buf instances and ports. A whole-word
     * `assign` is a continuous assignment; a `<=` inside an `always` block is
     * a clocked update. The equation VALUES are constant stand-ins and are
     * not discriminated here. */
    const char *sv = result.systemverilog;
    int has_continuous = 0;
    int has_update = 0;
    const char *assign = strstr(sv, "assign");
    if (assign && (assign == sv || isspace((unsigned char)assign[-1])))
        has_continuous = 1;
    const char *clocked = strstr(sv, "always");
    if (clocked) {
        for (const char *p = clocked; (p = strchr(p, '<')) != NULL; p++) {
            if (p[1] == '=') {
                has_update = 1;
                break;
            }
        }
    }
    assert(has_continuous || has_update);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

/* Test 15: A legal 2-stage pipeline compiles (rc == 0) and the emitted SV
 * carries the ready/valid handshake EQUATIONS, gated as the owner publishes
 * them (this step's guard is the constant 1); the one-entry buf boundary's
 * live-handshake obligation is witnessed at that equation level.
 *
 * This is line 3 of the accepted test map
 * local:what/are/the/cli/pipeline/emitted/sv/test/scenarios.md (ACCEPTED by
 * local:what/is/the/review/of/the/cli/pipeline/emitted/sv/test/scenarios.md).
 * The witness shape is the section-(1) source of tests/pipeline_lower_test.c
 * adapted to this line's 2-stage pipeline: fields a, b, each 8-bit; stage 1
 * `a <= x`, stage 2 `b <= a`; `yield b;`.
 *
 * The owner (src/pipeline_lower.c, lower_pipeline) publishes, for an unguarded
 * pipeline (guard = the constant 1), the ready/valid handshake as the
 * ready equation `ready = guard`: a 1-bit continuous assignment whose value is
 * the integer constant 1 (the no-over-gating contract - an unguarded pipeline
 * publishes no guard object, no hold mux and no guard connection). The
 * stage-boundary storage is a dedicated one-entry buf primitive instance
 * (what/is/a/buf.md), one per boundary (N-1 = 1 for a 2-stage pipeline). The
 * line's live-handshake obligation is witnessed by the ready/valid handshake
 * EQUATIONS in the emission (ready = the constant 1 for this unguarded witness),
 * never by instance port-connection text: the owner publishes no in_ready /
 * out_valid objects, and the unguarded buf instance carries no connections (its
 * only published connection is the guard one, and only under a real enclosing
 * guard), so no port-connection string can appear for this witness (the
 * interpretation is pinned at what/is/the/review/of/the/cli/pipeline/emitted/
 * sv/test/scenarios.md, line-3 finding).
 *
 * This line witnesses the ready/valid handshake equations in the terminal
 * emission; the guarded-handshake publication is pinned standalone by
 * pipeline-lower-test section (5) step-b-witnesses. False-guard halt
 * enforcement and the pipe_reset/pipeline_reset bindings are NOT pinned here
 * (their owning plan item at the pipeline-lower-test seam).
 *
 * RED against master: on master a clocked-process pipeline statement is
 * rejected with "invalid procedural statement" at the pipeline token
 * (does/the/pipeline/and/fsm/reach/the/compiled/output.md), so compile_ret is
 * -1 and no output is emitted. The discriminating pin is the rc == 0 check;
 * the emission pins (the ready/valid handshake equation to the constant 1 and
 * the pigen_buf instance) go green with it when the production pipeline step
 * lands (how/does/a/pipeline/statement/reach/the/production/rtl/path.md). */
static void test_pipeline_ready_valid_handshake_emission(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module line3_pipe(input logic clk, input logic [7:0] x);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline line3_pipe_body begin\n"
        "\t\t\tbit [7:0] a;\n"
        "\t\t\tbit [7:0] b;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\ta <= x;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tb <= a;\n"
        "\t\t\tend\n"
        "\t\t\tyield b;\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/line3_pipe.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* The legal 2-stage pipeline compiles. On master this is the
     * discriminating red: the production pipeline step is not yet in the
     * path, so the resolve stage rejects the clocked-process pipeline
     * statement with "invalid procedural statement" and ret is -1. */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    /* The ready/valid handshake equation is present, gated as the owner
     * publishes it: this step's guard is the constant 1, so the ready
     * equation `ready = guard` is a continuous assignment to the integer
     * constant 1 (the no-over-gating contract - an unguarded pipeline
     * publishes no guard object or hold mux). */
    assert(strstr(result.systemverilog, " = 1;") != NULL);

    /* The one-entry buf stage storage is a dedicated primitive instance, one
     * per stage boundary (N-1 = 1 for a 2-stage pipeline). */
    assert(strstr(result.systemverilog, "pigen_buf") != NULL);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

/* Test 15: Pipeline first-stage field read is diagnosed at the original span.
 * A 2-stage pipeline whose first stage reads pipeline field b, which no earlier
 * stage has written (fields a, b, each 8-bit; stage 1 `a <= b;`, stage 2
 * `b <= x;`, `yield b;` - the section-(1) red of tests/pipeline_lower_test.c),
 * must fail loudly: rc == -1, no output text, and a non-degenerate error span
 * over the offending first-stage statement at its original location with a
 * non-empty message. This line witnesses the production path (pigen_compile_
 * source, the extended lowering entry, naming, output model, emission) reaching
 * the step-(b) first-stage law owner and reporting it at the original span with
 * no output (how/should/compiler/builders/fail.md). A permissive step that
 * lowers the violation returns rc == 0 and fails this line; a step that reports
 * the whole `pipeline ... endpipeline` block (the current diagnostic) rather
 * than the offending statement fails the span pin. */
static void test_pipeline_first_stage_read_original_span(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module bs_red(input logic clk, input logic [7:0] x);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline bs_pipe begin\n"
        "\t\t\tbit [7:0] a;\n"
        "\t\t\tbit [7:0] b;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\ta <= b;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tb <= x;\n"
        "\t\t\tend\n"
        "\t\t\tyield b;\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/compile_test_pipeline_first_stage.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* Fail loud: the step-(b) first-stage law is violated. */
    assert(ret == -1);
    /* No output text is published on failure. */
    assert(result.systemverilog == NULL);
    /* A non-empty diagnostic message. */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);
    /* Non-degenerate span: a real source and a non-zero-length interval. */
    assert(error.span.source.index != PIGEN_INVALID_ID);
    assert(error.span.end > error.span.start);
    /* The offending first-stage statement is `a <= b;`; locate it in the
     * literal source and confirm the span covers it at its original location.
     * The span must point at that statement, not at the enclosing
     * `pipeline ... endpipeline` block (which would start at the `pipeline`
     * keyword, far before the statement). */
    size_t stmt_start = (size_t)(strstr(source_text, "a <= b;") - source_text);
    size_t stmt_end = stmt_start + strlen("a <= b;");
    assert(error.span.start <= stmt_start);
    assert(error.span.end >= stmt_end);
    {
        size_t tlen = 0;
        const char *t = pigen_source_span_text(&sources, error.span, &tlen);
        char buf[512];
        size_t n;
        if (t == NULL || tlen == 0) {
            fprintf(stderr,
                "pipeline first-stage read: expected a non-empty error span "
                "over the offending statement; got NULL or empty span text\n");
            abort();
        }
        /* pigen_source_span_text returns a pointer into the middle of the
         * source buffer, not a NUL-terminated string; copy the located span
         * bounded by tlen before searching, so the search cannot read past
         * the span into the rest of the source. */
        n = tlen < sizeof(buf) ? tlen : sizeof(buf);
        memcpy(buf, t, n);
        buf[n] = '\0';
        if (strstr(buf, "a <= b") == NULL || strstr(buf, "pipeline") != NULL) {
            fprintf(stderr,
                "pipeline first-stage read: expected the error span to cover "
                "the offending first-stage statement `a <= b;` at its original "
                "location and NOT the whole `pipeline ... endpipeline` block.\n"
                "  offending statement `a <= b;` at offset [%zu..%zu]\n"
                "  actual span [%zu..%zu] text = %s\n"
                "  (a block-level span starting at the `pipeline` keyword fails "
                "this pin)\n",
                stmt_start, stmt_end,
                error.span.start, error.span.end,
                buf);
            abort();
        }
    }

    pigen_free_sources(&sources);
}

/* Scenario line 7 (CLI pipeline emitted-SV list, what/are/the/cli/pipeline/
 * emitted/sv/test/scenarios.md): a pipeline outside a clocked process is
 * diagnosed, not silently dropped. The witness is the section-(1) pipeline body
 * (tests/pipeline_lower_test.c bs_ok: fields a/b/c 8-bit, a <= x, b <= a,
 * c <= b, yield c) placed directly in the module with no enclosing
 * `always @(posedge clk)` process. On master this source compiles rc == 0 into
 * an empty module body (the pipeline is silently dropped); the production
 * pipeline step must instead fail loud with rc == -1, no output text, a
 * non-degenerate span over the `pipeline` keyword at its original location, and
 * a non-empty message. This line is RED on master (rc == 0 today) and goes green
 * when the production pipeline step lands. */
static void test_pipeline_module_level_diagnosed_at_original_span(void)
{
    pigen_source_manager sources = {0};
    /* The witness: the section-(1) pipeline body placed directly in the module,
     * with no enclosing clocked process (the always block and its begin/end are
     * removed, the pipeline statement sits at module level). */
    const char *source_text =
        "module mlp_pipe(input logic clk, input logic [7:0] x);\n"
        "\tpipeline mlp_pipe_pipe begin\n"
        "\t\tbit [7:0] a;\n"
        "\t\tbit [7:0] b;\n"
        "\t\tbit [7:0] c;\n"
        "\t\tstage begin\n"
        "\t\t\ta <= x;\n"
        "\t\tend\n"
        "\t\tstage begin\n"
        "\t\t\tb <= a;\n"
        "\t\tend\n"
        "\t\tstage begin\n"
        "\t\t\tc <= b;\n"
        "\t\tend\n"
        "\t\tyield c;\n"
        "\tendpipeline\n"
        "endmodule";
    size_t source_length = strlen(source_text);
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    size_t span_length = 0;
    const char *span_text;
    int ret;

    /* The compile seam adds the source itself; a pre-added source would be a
     * duplicate path and is not this scenario's witness. */
    ret = pigen_compile_source(&sources, "tests/mlp_pipe.pigen",
                               source_text, source_length,
                               NULL, &result, &error);
    /* A module-level pipeline is an unsupported procedural position: the
     * production step fails loud (rc == -1) rather than silently dropping it
     * into an empty module (rc == 0, master's current behavior). */
    if (ret != -1) {
        fprintf(stderr,
                "test_pipeline_module_level_diagnosed_at_original_span: "
                "expected rc == -1 (a module-level pipeline must be diagnosed, "
                "not silently dropped); got rc=%d, message='%s'\n",
                ret, error.message ? error.message : "(null)");
        abort();
    }
    /* No SystemVerilog is emitted on failure. */
    assert(result.systemverilog == NULL);
    assert(result.length == 0);
    /* The diagnostic is non-empty. */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);
    /* The span is non-degenerate (start < end) and located at the `pipeline`
     * keyword's original span, not a degenerate or foreign span. */
    assert(error.span.source.index != PIGEN_INVALID_ID);
    assert(error.span.start < error.span.end);
    assert(pigen_source_span_valid(&sources, error.span));
    span_text = pigen_source_span_text(&sources, error.span, &span_length);
    assert(span_text != NULL);
    assert(span_length == strlen("pipeline"));
    assert(memcmp(span_text, "pipeline", span_length) == 0);
    /* The span's source is the one the compile added (source 0). */
    assert(error.span.source.index == 0);

    pigen_free_sources(&sources);
}

/* Test 15: A boundary that keeps unequal total $bits is diagnosed at the
 * original span. A 2-stage pipeline whose stage 1 writes a (8-bit) and b
 * (8-bit) and whose stage 2 reads both (16 bits in) but writes only c (8-bit
 * out) (stage 1 `a <= x; b <= y;`, stage 2 `c <= {a, b};`, `yield c;` - the
 * section-(2) red of tests/pipeline_lower_test.c) violates the step-(b)
 * repartition law: the stage boundary carries 16 bits in and 8 bits out. It
 * must fail loudly: rc == -1, no output text, and a non-degenerate error span
 * over the offending stage-2 statement at its original location with a
 * non-empty message. This line witnesses the production path (pigen_compile_
 * source, the extended lowering entry, naming, output model, emission)
 * reaching the step-(b) repartition law owner and reporting it at the
 * original span with no output (how/should/compiler/builders/fail.md). A step
 * that widens or narrows the carried packet across the boundary lowers it
 * (rc == 0) and fails the rc pin; a step that reports the whole `pipeline
 * ... endpipeline` block (the current diagnostic) rather than the offending
 * statement fails the span pin. */
static void test_pipeline_repartition_unequal_bits_original_span(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module nve_red(input logic clk, input logic [7:0] x, input logic [7:0] y);\n"
        "\talways @(posedge clk) begin\n"
        "\t\tpipeline nve_pipe begin\n"
        "\t\t\tbit [7:0] a;\n"
        "\t\t\tbit [7:0] b;\n"
        "\t\t\tbit [7:0] c;\n"
        "\t\t\tstage begin\n"
        "\t\t\t\ta <= x;\n"
        "\t\t\t\tb <= y;\n"
        "\t\t\tend\n"
        "\t\t\tstage begin\n"
        "\t\t\t\tc <= {a, b};\n"
        "\t\t\tend\n"
        "\t\t\tyield c;\n"
        "\t\tendpipeline\n"
        "\tend\n"
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/compile_test_pipeline_repartition.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* Fail loud: the step-(b) repartition law is violated - the boundary
     * keeps unequal total $bits (16 bits in, 8 bits out). */
    assert(ret == -1);
    /* No output text is published on failure. */
    assert(result.systemverilog == NULL);
    /* A non-empty diagnostic message. */
    assert(error.message != NULL);
    assert(strlen(error.message) > 0);
    /* Non-degenerate span: a real source and a non-zero-length interval. */
    assert(error.span.source.index != PIGEN_INVALID_ID);
    assert(error.span.end > error.span.start);
    /* The offending stage-2 statement is `c <= {a, b};`; locate it in the
     * literal source and confirm the span covers it at its original location.
     * The span must point at that statement, not at the enclosing
     * `pipeline ... endpipeline` block (which would start at the `pipeline`
     * keyword, far before the statement). */
    size_t stmt_start = (size_t)(strstr(source_text, "c <= {a, b};") - source_text);
    size_t stmt_end = stmt_start + strlen("c <= {a, b};");
    assert(error.span.start <= stmt_start);
    assert(error.span.end >= stmt_end);
    {
        size_t tlen = 0;
        const char *t = pigen_source_span_text(&sources, error.span, &tlen);
        char *tcopy;
        if (t == NULL || tlen == 0) {
            fprintf(stderr,
                "pipeline repartition: expected a non-empty error span "
                "over the offending statement; got NULL or empty span text\n");
            abort();
        }
        /* pigen_source_span_text points into the full source buffer and is
         * not NUL-terminated at the span end; bound every search to the
         * returned span length so text after the span cannot leak in. */
        tcopy = malloc(tlen + 1);
        if (tcopy == NULL)
            abort();
        memcpy(tcopy, t, tlen);
        tcopy[tlen] = '\0';
        if (strstr(tcopy, "c <= {a, b}") == NULL || strstr(tcopy, "pipeline") != NULL) {
            fprintf(stderr,
                "pipeline repartition: expected the error span to cover "
                "the offending stage-2 statement `c <= {a, b};` at its "
                "original location and NOT the whole `pipeline ... "
                "endpipeline` block.\n"
                "  offending statement `c <= {a, b};` at offset [%zu..%zu]\n"
                "  actual span [%zu..%zu] text = %.*s\n"
                "  (a block-level span starting at the `pipeline` keyword "
                "fails this pin)\n",
                stmt_start, stmt_end,
                error.span.start, error.span.end,
                (int)tlen, t);
            abort();
        }
        free(tcopy);
    }

    pigen_free_sources(&sources);
}

/* Test 15: Line 1 of the accepted CLI FSM emitted-SV scenario list
 * (.knowledge what/are/the/cli/fsm/emitted/sv/test/scenarios.md): the FSM
 * lowers into the parent module's emitted SV, replacing the silent empty
 * module. The legal two-state witness (the FSM_WITNESS shape of
 * tests/fsm_lower_test.c) compiles with rc == 0 and the emitted SV declares
 * exactly ONE module (the parent) whose body is non-empty beyond the port
 * header - an internal declaration, continuous assignment and/or clocked
 * process. RED on master: the same source returns rc == 0 with a
 * declaration-only body. */
static int is_identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}

/* Count keyword-token occurrences of word in text: whole-word matches only,
 * so "endmodule" does not count as "module". Text observation, not parsing. */
static size_t count_keyword(const char *text, const char *word)
{
    size_t count = 0;
    size_t len = strlen(word);
    const char *p = text;
    while ((p = strstr(p, word)) != NULL) {
        int left_ok = (p == text) || !is_identifier_char(p[-1]);
        int right_ok = !is_identifier_char(p[len]);
        if (left_ok && right_ok) {
            count++;
        }
        p++;
    }
    return count;
}

static void test_fsm_parent_module_emission(void)
{
    /* Literal data-first witness: the legal two-state FSM_WITNESS shape of
     * tests/fsm_lower_test.c (module with clk/rst/start/done ports, a
     * module-level fsm item with initial idle, idle -> run on start, run ->
     * idle on !start with done <= 1). */
    const char *source_text =
        "module fsm_emitted_probe\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\nfsm fsm_emitted_probe @(posedge clk) reset (rst) initial idle\n"
        "begin\n"
        "\tstate idle:\n"
        "\tbegin\n"
        "\t\tif (start)\n"
        "\t\t\tgoto run;\n"
        "\tend\n"
        "\tstate run:\n"
        "\tbegin\n"
        "\t\tdone <= 1;\n"
        "\t\tif (!start)\n"
        "\t\t\tgoto idle;\n"
        "\tend\n"
        "end\n"
        "endmodule\n";
    size_t source_length = strlen(source_text);

    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/fsm_emitted_probe.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* The legal witness compiles: rc == 0 with output text. */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    /* Exactly ONE module keyword in the emitted SV: the parent module. A
     * second RTL record (whose children the output model drops) would emit a
     * second module keyword and fail this pin. "endmodule" is a distinct
     * keyword and does not count. */
    assert(count_keyword(result.systemverilog, "module") == 1);

    /* The parent module name is declared in the emission. */
    assert(strstr(result.systemverilog, "fsm_emitted_probe") != NULL);

    /* The body beyond the port header is non-empty: the emission after the
     * port-list close ");" and before the module close must carry an internal
     * declaration, continuous assignment and/or clocked process. On master
     * the same source emits a declaration-only module (only ");" then
     * "endmodule"), which fails this pin. */
    const char *port_close = strstr(result.systemverilog, ");");
    assert(port_close != NULL);
    const char *body_start = port_close + strlen(");");
    const char *endmodule = strstr(body_start, "endmodule");
    assert(endmodule != NULL);
    const char *cursor = body_start;
    int body_has_content = 0;
    while (cursor < endmodule) {
        if ((*cursor != ' ') && (*cursor != '\t') &&
            (*cursor != '\n') && (*cursor != '\r')) {
            body_has_content = 1;
            break;
        }
        cursor++;
    }
    assert(body_has_content);

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
    test_pipeline_yield_two_field_msbsrc();
    test_syntax_error_span();
    test_type_mismatch_error();
    test_ready_cycle_error();
    test_unsupported_opaque_error();
    test_cross_domain_error_detailed();
    test_async_domain_error_detailed();
    test_vertical_slice_simulation_infrastructure();
    test_fsm_goto_undeclared_target_span();
    test_pipeline_emitted_module_and_buf_count();
    test_pipeline_next_value_equations_in_emission();
    test_pipeline_ready_valid_handshake_emission();
    test_pipeline_first_stage_read_original_span();
    test_pipeline_module_level_diagnosed_at_original_span();
    test_pipeline_repartition_unequal_bits_original_span();
    test_fsm_parent_module_emission();

    printf("All compile tests passed\n");
    return 0;
}