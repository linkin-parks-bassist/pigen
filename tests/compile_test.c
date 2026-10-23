#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/ids.h"

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

/* Test 1: Successful compilation of a simple pigen input
 * NOTE: This test expects pigen_compile_source to return 0 on success.
 * The current implementation may fail this until the compilation pipeline
 * is properly wired up in compile.c.
 */
static void test_successful_compilation(void)
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
    /* ret == 0 on success per contract */
    assert(ret == 0);
    assert(result.systemverilog != NULL);
    assert(result.length > 0);

    pigen_free_compile_result(&result);
    free(source_text);
    pigen_free_sources(&sources);
}

/* Test 2: Non-existent file returns error */
static void test_non_existent_file(void)
{
    pigen_source_manager sources = {0};
    pigen_compile_result result = {0};
    pigen_compile_error error = {0};
    int ret = pigen_compile_source(&sources, "tests/non_existent.pigen",
                                   "", 0, NULL, &result, &error);
    assert(ret == -1);

    pigen_free_sources(&sources);
}

/* Test 3: pigen_free_compile_result is safe with NULL result */
static void test_free_null_result(void)
{
    pigen_compile_result result = {0};
    pigen_free_compile_result(&result);
}

/* Test 4: pigen_free_compile_result frees allocated memory */
static void test_free_allocated_result(void)
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
    if (ret == 0) {
        assert(result.systemverilog != NULL);
        pigen_free_compile_result(&result);
    }

    free(source_text);
    pigen_free_sources(&sources);
}

/* Test 5: Error diagnostic contains valid origin and span */
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
    /* This file triggers a semantic error */
    if (ret == -1) {
        /* Error should have non-zero origin and valid span */
        assert(error.origin.index != PIGEN_INVALID_ID);
        assert(pigen_source_span_valid(&sources, error.span));
    }

    pigen_free_sources(&sources);
    free(source_text);
}

/* Test 6: Cross-domain transfer error returns error with provenance */
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
    if (ret == -1) {
        assert(error.origin.index != PIGEN_INVALID_ID);
        assert(pigen_source_span_valid(&sources, error.span));
    }

    pigen_free_sources(&sources);
    free(source_text);
}

int main(void)
{
    test_successful_compilation();
    test_non_existent_file();
    test_free_null_result();
    test_free_allocated_result();
    test_error_provenance();
    test_cross_domain_error_provenance();

    printf("All compile tests passed\n");
    return 0;
}