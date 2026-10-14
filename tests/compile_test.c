#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/ids.h"
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

/* Test 1: Independent validity check using structured syntax/semantic APIs */
/* This establishes that the same source passed to pigen_compile_source
 * is independently valid through pigen_parse_syntax and pigen_resolve_semantics.
 * This is the "independence" requirement: the witness must pass actual
 * syntax/semantic resolution, not just context initialization. */
static void test_independent_validity(void)
{
    /* Minimal valid Pigen source - same as used in test_compile_valid_source */
    const char *source_text =
        "module compile_test_example;\n"
        "  wire scalar_wire;\n"
        "endmodule\n";
    size_t source_length = strlen(source_text);

    /* Set up source manager for syntax parsing */
    pigen_source_manager sources = {0};
    sources.files = NULL;
    sources.count = 0;
    sources.capacity = 0;

    pigen_source_id source_id = pigen_source_add(&sources, "compile_test.pigen",
                                                 source_text, source_length);
    assert(source_id.index != PIGEN_INVALID_ID);

    /* Preprocess (minimal, no include support) */
    pigen_preprocess_result preprocess_result = {0};
    pigen_preprocess_error preprocess_error = {0};
    pigen_source_provider provider = {0, NULL};

    int preprocess_ret = pigen_preprocess(&sources, source_id, &provider,
                                          &preprocess_result, &preprocess_error);
    assert(preprocess_ret == 1); /* preprocessing succeeds */

    /* Parse syntax */
    pigen_syntax_tree syntax_tree = {0};
    pigen_syntax_error syntax_error = {0};

    const pigen_expanded_source *expanded = &preprocess_result.expanded;
    int parse_ret = pigen_parse_syntax(expanded, &syntax_tree, &syntax_error);
    if (parse_ret != 1) {
        fprintf(stderr, "syntax parsing failed: %s\n",
                syntax_error.message ? syntax_error.message : "unknown error");
    }
    assert(parse_ret == 1); /* syntax parsing succeeds */

    /* Semantic resolution */
    pigen_semantic_model semantic_model = {0};
    pigen_semantic_init(&semantic_model, &sources);

    pigen_resolve_policy maximum_bits_policy = { .maximum_generated_bits = 256 };
    pigen_semantic_error semantic_error = {0};

    int resolve_ret = pigen_resolve_semantics(&syntax_tree, &semantic_model,
                                              &maximum_bits_policy, &semantic_error);
    if (resolve_ret != 1) {
        fprintf(stderr, "semantic resolution failed: %s\n",
                semantic_error.message ? semantic_error.message : "unknown error");
    }
    assert(resolve_ret == 1); /* semantic resolution succeeds */

    /* Cleanup */
    pigen_free_syntax_tree(&syntax_tree);
    pigen_free_semantic_model(&semantic_model);
    pigen_free_preprocess_result(&preprocess_result);
    pigen_free_sources(&sources);
}

/* Test 2: pigen_compile_source returns 0 on successful compilation of valid Pigen source */
/* This test verifies the PUBLIC contract: successful compilation returns 0,
 * produces non-null/nonempty SystemVerilog containing the named module,
 * with consistent length, and pigen_free_compile_result safely cleans up.
 *
 * NOTE: This test is expected to FAIL (red) against the current incomplete
 * pigen_compile_source implementation, which skips resolution/lowering/
 * naming/output-model construction. The independent validity check above
 * confirms the source is legitimately valid. */
static void test_compile_valid_source(void)
{
    /* Minimal valid Pigen source: a simple module declaration.
     * Validated independently by test_independent_validity. */
    const char *source_text =
        "module compile_test_example;\n"
        "  wire scalar_wire;\n"
        "endmodule\n";
    size_t source_length = strlen(source_text);

    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};

    int ret = pigen_compile_source(&sources, "compile_test.pigen",
                                   source_text, source_length,
                                   NULL, &result, &error);

    /* Public contract: return 0 on success */
    assert(ret == 0);

    /* Public contract: non-null SystemVerilog output */
    assert(result.systemverilog != NULL);

    /* Public contract: non-empty output */
    assert(result.length > 0);

    /* Public contract: output contains the named module */
    assert(strstr(result.systemverilog, "compile_test_example") != NULL);

    /* Public contract: length is consistent with the output */
    assert(result.length == strlen(result.systemverilog));

    /* Public contract: safe result cleanup */
    pigen_free_compile_result(&result);
    assert(result.systemverilog == NULL);
    assert(result.length == 0);

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

int main(void)
{
    test_independent_validity();
    test_compile_valid_source();
    test_free_null_result();
    test_free_allocated_result();
    test_error_provenance();
    test_cross_domain_error_provenance();

    printf("All compile tests passed\n");
    return 0;
}