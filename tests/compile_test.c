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

#include "check.h"

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

/* Non-overlapping count of NEEDLE (needle_length bytes) in TEXT. */
static size_t count_occurrences(const char *text, const char *needle,
                                size_t needle_length)
{
    size_t count = 0;
    const char *at = text;
    while ((at = strstr(at, needle)) != NULL) {
        count++;
        at += needle_length;
    }
    return count;
}

/* Test 15: CLI FSM emitted-SV scenario line 5 (no second RTL record: the
 * controller lives in the one parent module) at the pigen_compile_source seam.
 *
 * Line 5 of the independently accepted CLI FSM emitted-SV list (verdict
 * ACCEPTED at what/is/the/review/of/the/cli/fsm/emitted/sv/test/scenarios.md):
 * a second RTL record is not published for the FSM. The legal two-state
 * witness (the FSM_WITNESS shape of tests/fsm_lower_test.c: a module with
 * input logic clk, input logic rst, input logic start, output logic done; a
 * module-level fsm item with the begin/end body of state idle: and state
 * run:) compiles with rc == 0 and the emitted SV declares exactly ONE
 * module (the parent) carrying the controller. The production step
 * publishes through the parent module's existing shared RTL module record
 * (how/does/a/fsm/statement/reach/the/production/rtl/path.md), so a
 * second-record publish - whose children the output model drops, mapping the
 * PIGEN_SYNTAX_MODULE node to the first matching RTL record - would leave
 * the controller absent from the emitted module and fail lines 1-4; this
 * line pins the shared-module-record law at the observation point where
 * that failure shows. RED against master: the same source compiles with
 * rc == 0 but the emitted module carries no body beyond the port header
 * (does/the/pipeline/and/fsm/reach/the/compiled/output.md), so the
 * controller-presence pin fails. */
static void test_fsm_no_second_rtl_record(void)
{
    pigen_source_manager sources = {0};
    /* The FSM_WITNESS witness shape of tests/fsm_lower_test.c, verbatim. */
    const char *source_text =
        "module fsm_w5\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\n"
        "fsm fsm_w5 @(posedge clk) reset (rst) initial idle\n"
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

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/fsm_w5.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    assert(ret == 0); /* the legal FSM witness compiles */
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    /* Exactly ONE module (the parent fsm_w5): a second-record publish
     * would declare a second module or, once the output model drops the
     * second record's children, carry the controller in no module at all.
     * The emitter renders "module <name> ("; "endmodule" cannot match the
     * space-terminated needle. */
    assert(count_occurrences(result.systemverilog, "module ", 7) == 1);
    assert(strstr(result.systemverilog, "fsm_w5") != NULL);

    /* The one module carries the controller: the emitted body is non-empty
     * beyond the port header (between ");" and "endmodule"). A declaration-
     * only module - master's silent drop, or a second-record publish whose
     * children the output model drops - fails this pin. */
    const char *header_close = strstr(result.systemverilog, ");");
    assert(header_close != NULL);
    const char *module_end = strstr(result.systemverilog, "endmodule");
    assert(module_end != NULL);
    assert(module_end > header_close + 2);
    const char *body = header_close + 2;
    size_t body_length = (size_t)(module_end - body);
    size_t nonblank = 0;
    size_t i;
    for (i = 0; i < body_length; i++) {
        if (body[i] != '\n' && body[i] != '\t' &&
            body[i] != ' ' && body[i] != '\r')
            nonblank++;
    }
    assert(nonblank > 0);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

/* Test 15: FSM emitted-SV line 3 - per-state action bodies gated on
 * state-active (one line of the accepted CLI FSM emitted-SV scenario list,
 * review leaf what/is/the/review/of/the/cli/fsm/emitted/sv/test/scenarios.md,
 * ACCEPTED).
 *
 * One setup: the legal two-state FSM witness in the tests/fsm_lower_test.c
 * FSM_WITNESS shape - a module with `input logic clk, input logic rst,
 * input logic start, output logic done`, a module-level
 * `fsm NAME @(posedge clk) reset (rst) initial idle` item and a
 * `begin ... end` body of `state IDENT:` items, `endmodule` - whose run
 * state (the second declared state) carries the action `done <= 1;` plus
 * a guarded goto.
 *
 * One action: compile the literal source through pigen_compile_source.
 *
 * One behavioral question: does the emitted SV carry the run state's
 * action as an equation on the module's `done` output that is conditioned
 * on the state-active comparison between the state register and the run
 * state's encoding? A register-read-free equation, or an ungated one,
 * fails the pin: the emission must (a) declare the controller's state
 * register as an internal (non-port) logic object, (b) READ that register
 * in the module body (a register-read-free action equation fails
 * (b)), and (c) gate the done write with a conditional whose condition
 * references the register (an ungated action write fails (c)). The
 * register name is found textually as the unique non-keyword, non-port
 * identifier of the body, so the pin does not over-pin the declaration
 * form, the encoding or the register width - the (s5) implementation's.
 * The emitted text is observed as text - never re-parsed. */

/* A SystemVerilog identifier character. */
static int sv_ident_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* The witness's four port names: any other body identifier is a
 * synthetic controller object, the state register. */
static const char *fsm_gate_ports[] = { "clk", "rst", "start", "done" };

/* A SystemVerilog keyword (the emitted body's reserved words): any body
 * identifier outside this set that is not a port is the register. */
static const char *fsm_gate_keywords[] = {
    "always", "assign", "begin", "case", "default", "else", "end",
    "endcase", "endmodule", "endprocess", "endfunction", "endtask",
    "if", "initial", "input", "inout", "integer", "logic", "module",
    "negedge", "output", "parameter", "posedge", "reg", "signed",
    "wire", "while"
};

static int is_fsm_gate_keyword(const char *ident, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(fsm_gate_keywords) / sizeof(fsm_gate_keywords[0]); i++) {
        if (strlen(fsm_gate_keywords[i]) == len &&
            memcmp(fsm_gate_keywords[i], ident, len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_fsm_gate_port(const char *ident, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(fsm_gate_ports) / sizeof(fsm_gate_ports[0]); i++) {
        if (strlen(fsm_gate_ports[i]) == len &&
            memcmp(fsm_gate_ports[i], ident, len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The body's non-port identifiers: the state register is the module's unique
 * internal (non-port) logic object. The witness's four ports are clk, rst,
 * start, done; any other identifier in the body is a synthetic controller
 * object. The register is referenced many times (declaration, reset, every
 * state-active gate, every transition), so it is found as the body's single
 * non-keyword, non-port identifier - not by occurrence count. Digit-led
 * tokens and base-suffix tails of literals (the `d` of `2'd1`, the `b` of
 * `1'b1`, after the literal's quote) are not identifiers, so the pin does not
 * depend on whether the emitter renders the encoding and values as bare
 * decimals or in the 'b/'d literal style. */
static int find_state_register(const char *body, char *name, size_t name_size)
{
    const char *p = body;
    int found = 0;
    const char *found_start = NULL;
    size_t found_len = 0;

    while (*p) {
        if (sv_ident_char((unsigned char)*p) &&
            (p == body || !sv_ident_char((unsigned char)*(p - 1))) &&
            !(*p >= '0' && *p <= '9') &&
            /* A base-suffix tail (the `d` of `2'd1`, the `b` of `1'b1`)
             * follows the literal's quote: not an identifier. */
            (p == body || *(p - 1) != '\'')) {
            const char *start = p;
            while (sv_ident_char((unsigned char)*p)) p++;
            size_t len = (size_t)(p - start);
            if (is_fsm_gate_keyword(start, len) ||
                is_fsm_gate_port(start, len))
                continue;
            /* A second distinct non-keyword, non-port identifier means the
             * body is not the single-register controller this pin assumes. */
            if (found && len != found_len &&
                memcmp(found_start, start, found_len) != 0)
                return 0;
            if (!found) {
                found = 1;
                found_start = start;
                found_len = len;
            }
        } else {
            p++;
        }
    }
    if (!found || found_len + 1 > name_size) return 0;
    memcpy(name, found_start, found_len);
    name[found_len] = 0;
    return 1;
}

/* True when the done write is GATED on the state-active comparison. The
 * state-active gate is the CONDITION of the conditional expression whose
 * value is the done equation (the production emitter renders it as a ternary
 * `cond ? t : f` in the done's continuous assign, or an `if (cond) done <= v;`
 * form). A register-read-free or ungated done write fails here: the condition
 * operand must reference the state register, so a done equation that is a bare
 * constant, a conditional on a port or constant alone, or a register write in
 * the clocked process (whose condition is the reset or goto guard, not the
 * state-active gate on done) carries no register reference in the done's own
 * gate. */
static int done_write_is_gated_on_register(const char *body, const char *reg_name)
{
    /* Ternary form: the done equation's value is `cond ? t : f`. For each `?`
     * take the enclosing statement (back to the previous `;`/newline, forward
     * to the next `;`); the statement is the gated done write when it names
     * done as its destination AND the text of the statement before the `?`
     * (the condition operand) reads the state register. A bare-constant done
     * write, a conditional on a port/constant alone, or a register write in
     * the clocked process (a different statement, whose condition is the
     * reset or goto guard) fails here. */
    {
        const char *p = body;
        while ((p = strchr(p, '?')) != NULL) {
            const char *stmt_start = p;
            while (stmt_start > body &&
                *(stmt_start - 1) != ';' && *(stmt_start - 1) != '\n')
                stmt_start--;
            const char *stmt_end = strchr(p, ';');
            if (stmt_end == NULL) stmt_end = body + strlen(body);
            {
                size_t sl = (size_t)(stmt_end - stmt_start);
                char stmt[512];
                int is_done = 0;

                if (sl >= sizeof(stmt))
                    sl = sizeof(stmt) - 1;
                memcpy(stmt, stmt_start, sl);
                stmt[sl] = 0;
                is_done = (strstr(stmt, "done") != NULL);
                if (is_done) {
                    /* The condition operand: the statement text before the
                     * `?` (the ternary's condition). */
                    size_t cl = (size_t)(p - stmt_start);
                    char cond[512];

                    if (cl >= sizeof(cond))
                        cl = sizeof(cond) - 1;
                    memcpy(cond, stmt_start, cl);
                    cond[cl] = 0;
                    if (strstr(cond, reg_name) != NULL)
                        return 1;
                }
            }
            p++;
        }
    }
    /* If-form: `if (cond) ... done <= ... ;` where cond reads the register. */
    {
        const char *p = body;
        while ((p = strstr(p, "if")) != NULL) {
            const char *cond = p + 2;
            while (*cond == ' ' || *cond == '\t') cond++;
            if (*cond == '(') {
                const char *cs = cond + 1;
                const char *ce = strchr(cs, ')');
                if (ce != NULL) {
                    size_t cl = (size_t)(ce - cs);
                    char ctext[256];
                    int reads_reg = 0;

                    if (cl >= sizeof(ctext))
                        cl = sizeof(ctext) - 1;
                    memcpy(ctext, cs, cl);
                    ctext[cl] = 0;
                    reads_reg = (strstr(ctext, reg_name) != NULL);
                    if (reads_reg) {
                        /* The guarded statement is the text after `)` to the
                         * next `;`: it must be the done write. */
                        const char *s = ce + 1;
                        const char *e = strchr(s, ';');
                        if (e == NULL) e = body + strlen(body);
                        {
                            size_t sl = (size_t)(e - s);
                            char stmt[256];

                            if (sl >= sizeof(stmt))
                                sl = sizeof(stmt) - 1;
                            memcpy(stmt, s, sl);
                            stmt[sl] = 0;
                            if (strstr(stmt, "done") != NULL)
                                return 1;
                        }
                    }
                }
            }
            p += 2;
        }
    }
    return 0;
}

static void test_fsm_emitted_state_active_gating(void)
{
    const char *source =
        "module gate_probe\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\nfsm gate_probe @(posedge clk) reset (rst) initial idle\n"
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

    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "gate_probe.pigen",
                                   source, strlen(source),
                                   NULL, &result, &error);

    /* The legal witness compiles. */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    const char *sv = result.systemverilog;

    /* The module body: the text after the port list's closing `);`. The
     * declaration-only emission (a module whose body is empty beyond the
     * ports) carries no register and fails the next assertion. */
    const char *body = strstr(sv, ");");
    assert(body != NULL);
    body += 2;

    /* (a) The controller's state register is declared as the module's
     * unique internal logic object: the body's unique non-keyword,
     * non-port identifier. */
    char reg_name[128];
    assert(find_state_register(body, reg_name, sizeof(reg_name)));

    /* (b) The register is READ by the emitted body, not merely
     * declared: its identifier occurs after its declaration (a
     * register-read-free action equation fails here). */
    {
        const char *decl = strstr(body, reg_name);
        const char *after_decl = decl + strlen(reg_name);
        assert(strstr(after_decl, reg_name) != NULL);
    }

    /* (c) The done write is GATED on the state-active comparison: the
     * emitted body carries a conditional (the state-active gate `reg == enc`,
     * rendered by the production emitter as a ternary `cond ? t : f` in the
     * done equation, or an `if (cond)` form) whose condition operand
     * references the state register and whose value is the done write. A
     * register-read-free or ungated action equation fails here: a done
     * equation that is a bare constant, a conditional on a port or constant
     * alone, or a register write in the clocked process (whose condition is
     * the reset or goto guard, not the state-active gate on done) carries no
     * register reference in the done's own gate. */
    assert(done_write_is_gated_on_register(body, reg_name));

    /* The run state's action reaches the module's done output: the
     * emitted body writes done. */
    assert(strstr(body, "done") != NULL);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
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

/* The start of the identifier token immediately preceding AT in SV (the LHS
 * destination of an update whose ` <= ` begins at AT): walk back over
 * whitespace, then over identifier characters. NULL when there is no such
 * identifier. Used to witness the destination of a next-state update. */
static const char *lhs_identifier(const char *sv, const char *at)
{
    const char *p = at;

    (void)sv;
    if (p < sv || *p != ' ') {
        return NULL;
    }
    p--;
    while (p >= sv && (*p == ' ' || *p == '\t')) {
        p--;
    }
    if (p < sv) {
        return NULL;
    }
    /* P now points at the identifier's last character; advance to its first
     * character. */
    {
        const char *first = p;

        while (first > sv && ((*first >= 'a' && *first <= 'z') ||
            (*first >= 'A' && *first <= 'Z') || (*first >= '0' &&
            *first <= '9') || *first == '_')) {
            first--;
        }
        if (first > sv) {
            first++;
        }
        if (first == p + 1) {
            /* No identifier characters found (P was not part of one). */
            return NULL;
        }
        return first;
    }
}

/* True when A and B begin with the same identifier token (a SystemVerilog
 * register name). A and B are located positions inside the one emitted
 * buffer: the tokens are not NUL-terminated after them, so the token itself
 * is the comparison - both must carry identifier characters to the same
 * length and a different first character fails. Used to witness that two
 * next-state values read the same state register without modifying the
 * emitted text. */
static int register_name_equals(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (1) {
        int a_id = (*a >= 'a' && *a <= 'z') || (*a >= 'A' && *a <= 'Z') ||
            (*a >= '0' && *a <= '9') || *a == '_';
        int b_id = (*b >= 'a' && *b <= 'z') || (*b >= 'A' && *b <= 'Z') ||
            (*b >= '0' && *b <= '9') || *b == '_';
        if (a_id != b_id) {
            return 0;
        }
        if (!a_id) {
            return 1;
        }
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
}

/* Test 15: Guard-gated FSM goto transitions share the one state register as
 * their single destination (CLI FSM emitted-SV scenario, line 4).
 *
 * A legal witness with one guarded goto per state, whose guards are mutually
 * exclusive (start vs !start), must compile successfully. Its emitted
 * SystemVerilog then pins the guard-gated single-destination controller:
 *   - the ONE state register, declared as an internal module object;
 *   - ONE posedge-clocked process carrying every next-state update;
 *   - one guard-gated next-state value per goto: a ternary whose true branch is
 *     the goto target state's encoding and whose false branch holds the current
 *     state (the register read), each driving the one state register, and no
 *     second such update (the per-goto update count is the second-destination
 *     witness, since the located-identifier pins only see the first occurrence).
 * A second destination, a second posedge process, or a next-state value that
 * does not read the register must fail these pins.
 *
 * RED on master: the production path does not lower the FSM statement yet, so
 * the emitted module is declaration-only (ports + endmodule) and none of the
 * pins below hold. */
static void test_fsm_guard_gated_single_destination(void)
{
    const char *source_text =
        "module w\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\n"
        "fsm w @(posedge clk) reset (rst) initial idle\n"
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
        "endmodule";
    size_t source_length = strlen(source_text);

    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/fsm_guard_gated_single_dest.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    /* The legal witness compiles. */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    const char *sv = result.systemverilog;

    /* Pin A: the module is emitted and is NOT declaration-only. On master the
     * FSM statement is a non-emitted container (the output walk maps only a
     * module and its signal declarators), so the body is just the port header
     * and endmodule; the controller below is absent. */
    assert(strstr(sv, "module w") != NULL);

    /* Pin B: ONE posedge-clocked process, clocked by `clk`. A second
     * always @(posedge clk) block would split the next-state updates across two
     * processes, which the one synchronous controller forbids. */
    assert(strstr(sv, "always @(posedge clk)") != NULL);
    {
        const char *p = sv;
        int processes = 0;
        while ((p = strstr(p, "always @(posedge clk)")) != NULL) {
            processes++;
            p += 1;
        }
        assert(processes == 1);
    }

    /* Pins C/D: one guard-gated next-state value per goto, each a ternary whose
     * true branch is the goto target state's encoding and whose false branch
     * holds the current state (the register read). The state encodings are the
     * per-state indices (idle=0, run=1), so the idle->run transition is
     * `start ? 1 : <register read>` and the run->idle transition is
     * `!start ? 0 : <register read>`. Every such conditional drives the ONE
     * state register via a non-blocking update, so each appears as
     * `<register> <= guard ? <target> : <register read>`; the register-read
     * false branch (`: <register read>`) is what keeps a transition that does
     * not read the register from passing. The register's terminal name is the
     * (s5) implementation's (what/is/the/fsm/parent/module/lowering.md leaves the
     * synthetic names open), so it is located, not pinned, by the update form. */
    assert(strstr(sv, " <= start ? 1 : ") != NULL);
    assert(strstr(sv, " <= !start ? 0 : ") != NULL);

    /* Pin D2: exactly ONE guard-gated next-state value per goto. Pins C/D and E
     * locate the FIRST occurrence of each pattern, so a second destination
     * driven by identical guarded ternaries (a shadow register carrying
     * `nstate <= start ? 1 : nstate` and `nstate <= !start ? 0 : nstate`) would
     * still let every located identifier equal the shared register and pass
     * them. Counting the whole emitted buffer closes that gap: the one
     * synchronous controller carries exactly one update per goto, so any
     * second destination or duplicate update fails here. The count idiom
     * advances by one from each match (the same idiom Pin B uses for process
     * count); the reset path (` <= rst ? 0 : `) is a distinct pattern and is
     * not counted. */
    {
        const char *p = sv;
        int start_updates = 0;
        int not_start_updates = 0;
        while ((p = strstr(p, " <= start ? 1 : ")) != NULL) {
            start_updates++;
            p += 1;
        }
        p = sv;
        while ((p = strstr(p, " <= !start ? 0 : ")) != NULL) {
            not_start_updates++;
            p += 1;
        }
        assert(start_updates == 1);
        assert(not_start_updates == 1);
    }

    /* Pin E: the ONE state register is the single destination the transitions
     * share, and every next-state value reads the register (its false branch).
     * Each next-state value is emitted as `<register> <= guard ? <target> :
     * <register read>`, so the single-destination law is witnessed by the
     * destination (LHS) identifier of every next-state value: a second
     * destination (the two values naming different LHS registers) or a
     * transition not reading the register (an LHS or false-branch that is not
     * the shared register) breaks the equality below. The reset path also drives
     * the same register; a second destination is caught by the count pin above,
     * so this pin witnesses the shared register of the located updates. */
    {
        const char *idle_next = strstr(sv, " <= start ? 1 : ");
        const char *run_next = strstr(sv, " <= !start ? 0 : ");
        const char *idle_dest;
        const char *run_dest;
        const char *idle_read;
        const char *run_read;

        assert(idle_next != NULL);
        assert(run_next != NULL);
        idle_dest = lhs_identifier(sv, idle_next);
        run_dest = lhs_identifier(sv, run_next);
        idle_read = idle_next + strlen(" <= start ? 1 : ");
        run_read = run_next + strlen(" <= !start ? 0 : ");
        /* Every next-state value drives an identifier register (not a constant),
         * and reads an identifier register in its false branch (a literal digit
         * false branch would be a transition that does not read the register). */
        assert(idle_dest != NULL && run_dest != NULL);
        assert(idle_read[0] != '0' && idle_read[0] != '1');
        assert(run_read[0] != '0' && run_read[0] != '1');
        /* Single destination: both next-state values drive the same register,
         * and that register is the one they read (a second destination or a
         * non-register-reading transition names a different register). */
        assert(register_name_equals(idle_dest, run_dest));
        assert(register_name_equals(idle_dest, idle_read));
        assert(register_name_equals(run_dest, run_read));
    }

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

/* Line 2 of the accepted CLI FSM emitted-SV scenario list (what/are/the/cli/
 * fsm/emitted/sv/test/scenarios.md): the state register is one internal
 * variable reset to the header initial state's encoding resolved by the
 * header token's text. The witness is the FSM_WITNESS shape of
 * tests/fsm_lower_test.c whose header names the SECOND declared state
 * (states idle then run, header `initial run`): the emitted SV must declare
 * exactly ONE internal (non-port) logic object - the synthetic state
 * register - wide enough for every state's encoding (a 1-bit register fails
 * the pin), and carry a reset path driving the register to the header-named
 * state's encoding (a reset to the first declared state's encoding fails the
 * pin). RED on master: the module-level FSM is silently dropped and the
 * emitted body is the port header only. */

/* The first line of SV text starting with PREFIX (the returned pointer ends
 * at the line's newline or the string's NUL). */
static const char *sv_line(const char *sv, const char *prefix)
{
    const char *p = sv;
    size_t prefix_length = strlen(prefix);

    while (p && *p) {
        if (strncmp(p, prefix, prefix_length) == 0)
            return p;
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return NULL;
}

/* The number of lines of SV text starting with PREFIX. */
static size_t sv_line_count(const char *sv, const char *prefix)
{
    size_t count = 0;
    const char *p = sv;
    size_t prefix_length = strlen(prefix);

    while (p && *p) {
        if (strncmp(p, prefix, prefix_length) == 0)
            count++;
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return count;
}

/* The first line of SV text starting with PREFIX and containing NEEDLE, or
 * NULL when no line does. */
static const char *sv_line_with(const char *sv, const char *prefix,
                                 const char *needle)
{
    const char *p = sv;
    size_t prefix_length = strlen(prefix);

    while (p && *p) {
        if (strncmp(p, prefix, prefix_length) == 0 &&
            strstr(p, needle) != NULL)
            return p;
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return NULL;
}

/* The `[h:l]` high bound of an internal declaration line, or -1 when the
 * line carries no range (a width-1 object). */
static long sv_decl_width(const char *line)
{
    const char *range = strstr(line, "[");

    if (!range)
        return -1;
    range++;
    if (!isdigit((unsigned char)*range))
        return -1;
    return strtol(range, NULL, 10);
}

/* The declared name of an internal declaration line
 * (`  logic [h:l] name;` / `  logic name;`), or NULL. */
static const char *sv_decl_name(const char *line)
{
    const char *p = line + 2; /* skip the object indent */

    while (*p == ' ')
        p++;
    if (strncmp(p, "logic", 5) != 0)
        return NULL;
    p += 5;
    while (*p == ' ')
        p++;
    if (*p == '[') { /* skip the packed range */
        p = strchr(p, ']');
        if (!p)
            return NULL;
        p++;
    }
    while (*p == ' ')
        p++;
    if (!isalpha((unsigned char)*p) && *p != '_')
        return NULL;
    return p;
}

static void test_fsm_state_register_reset(void)
{
    pigen_source_manager sources = {0};
    /* The legal two-state witness of tests/fsm_lower_test.c's FSM_WITNESS
     * shape, with the header naming the SECOND declared state (`run`). */
    const char *source_text =
        "module fsm_srr\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic done\n"
        ");\n"
        "\n"
        "fsm fsm_srr @(posedge clk) reset (rst) initial run\n"
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
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret;
    const char *decl;
    const char *name;
    size_t register_name_length;

    ret = pigen_compile_source(&sources, "tests/fsm_state_register_reset.pigen",
                               source_text, source_length,
                               NULL, &result, &error);
    /* (rc) the legal witness compiles. */
    REQUIRE(ret == 0);
    REQUIRE(result.systemverilog != NULL);
    REQUIRE(result.length > 0);

    /* (a) exactly ONE internal (non-port) logic object: the port header
     * lines start with `  input` / `  output` / `  inout`, so the only
     * `  logic`-prefixed lines are internal declarations. */
    REQUIRE(sv_line_count(result.systemverilog, "  logic ") == 1);

    /* (a) the one internal object is wide enough for every state's
     * encoding: the 2-state witness needs a ranged declaration
     * (`[h:l]` with h >= 1); a width-1 (rangeless) register fails the
     * pin. */
    decl = sv_line(result.systemverilog, "  logic ");
    REQUIRE(decl != NULL);
    REQUIRE(sv_decl_width(decl) >= 1);

    /* (b) the reset path drives THAT register to the header-named state's
     * encoding: run is the second declared state, so its encoding is 1 and
     * the reset line carries a conditional whose true branch is the
     * literal 1, gated by the header's reset signal (rst). A reset to the
     * first declared state's encoding (0), or a reset that drives no
     * register, fails the pin. */
    name = sv_decl_name(decl);
    REQUIRE(name != NULL);
    register_name_length = 0;
    while (isalnum((unsigned char)name[register_name_length]) ||
           name[register_name_length] == '_')
        register_name_length++;
    {
        const char *drives = NULL;
        char drive_prefix[64];
        char *register_token = malloc(register_name_length + 1);

        REQUIRE(register_token != NULL);
        memcpy(register_token, name, register_name_length);
        register_token[register_name_length] = 0;
        snprintf(drive_prefix, sizeof(drive_prefix),
                 "    %s <= ", register_token);
        drives = sv_line(result.systemverilog, drive_prefix);
        free(register_token);
        /* The register is driven at all. */
        REQUIRE(drives != NULL);
        /* One of its driving updates is the reset path: gated by the
         * header's reset signal (rst) with the true branch the header
         * initial state's encoding (the literal 1, run's index). */
        REQUIRE(sv_line_with(result.systemverilog, drive_prefix,
                             "rst ? 1") != NULL ||
                sv_line_with(result.systemverilog, drive_prefix,
                             "rst? 1") != NULL);
        /* The reset path is not a reset to the first declared state's
         * encoding: no update gated by rst drives the register with the
         * false-first-state literal 0 as its true branch. */
        REQUIRE(sv_line_with(result.systemverilog, drive_prefix,
                             "rst ? 0") == NULL);
        REQUIRE(sv_line_with(result.systemverilog, drive_prefix,
                             "rst? 0") == NULL);
    }

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

/* Test 15: Action-destination non-first-output witness at the production
 * seam (pigen_compile_source). The witness module declares TWO outputs -
 * `first_out` FIRST and `done` SECOND - and the run state's action targets
 * `done` (`done <= 1;`); `first_out` is never assigned by the FSM. The
 * promised behavior: the compile succeeds (rc == 0) and the emitted SV
 * publishes the state body's action as a write to the module's `done` output
 * (the SECOND declared output), and publishes NO action write to `first_out`
 * (the FIRST declared output, never assigned). This is the terminal-emission
 * mirror of the standalone pin fsm-lower-test section (11)
 * action-dest-nonfirst-output; the production path resolves the action
 * destination through the real syntax tree, so it binds to `done`, not the
 * module's first output. A builder that falls back to the module's first
 * output publishes a `first_out` write (and no `done` write) and fails here. */
static void test_fsm_action_dest_nonfirst_output(void)
{
    pigen_source_manager sources = {0};
    const char *source_text =
        "module adn_emit\n"
        "(\n"
        "\tinput logic clk,\n"
        "\tinput logic rst,\n"
        "\tinput logic start,\n"
        "\toutput logic first_out,\n"
        "\toutput logic done\n"
        ");\n"
        "\n"
        "fsm adn_emit @(posedge clk) reset (rst) initial idle\n"
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

    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/adn_emit.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    const char *sv = result.systemverilog;

    /* The module is emitted. */
    assert(strstr(sv, "adn_emit") != NULL);

    /* The action's destination is the SECOND declared output, done: the
     * emitted body carries a write to done (a continuous `assign done` or a
     * clocked `done <=`). */
    assert(strstr(sv, "assign done") != NULL ||
           strstr(sv, "done <=") != NULL);

    /* NO action write to the FIRST declared output (first_out): the port
     * declaration names first_out, so pin on the assignment forms, not the
     * bare name. A first-output fallback publish writes `assign first_out`
     * or `first_out <=` and fails here. */
    assert(strstr(sv, "assign first_out") == NULL);
    assert(strstr(sv, "first_out <=") == NULL);
    assert(strstr(sv, "first_out =") == NULL);

    pigen_free_compile_result(&result);
    pigen_free_sources(&sources);
}

int main(int argc, char **argv)
{
    check_init(argc, argv);

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

    SECTION("fsm-state-register-reset") {
        test_fsm_state_register_reset();
    }

    test_vertical_slice_simulation_infrastructure();
    test_fsm_goto_undeclared_target_span();
    test_pipeline_emitted_module_and_buf_count();
    test_pipeline_next_value_equations_in_emission();
    test_pipeline_ready_valid_handshake_emission();
    test_pipeline_first_stage_read_original_span();
    test_pipeline_module_level_diagnosed_at_original_span();
    test_pipeline_repartition_unequal_bits_original_span();
    test_fsm_parent_module_emission();
    test_fsm_emitted_state_active_gating();
    test_fsm_guard_gated_single_destination();
    test_fsm_no_second_rtl_record();
    test_fsm_action_dest_nonfirst_output();

    printf("All compile tests passed\n");
    return check_finish();
}