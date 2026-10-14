#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/preprocess.h"
#include "pigen/lexer.h"
#include "pigen/syntax.h"
#include "pigen/semantic.h"
#include "pigen/rtl_lower.h"
#include "pigen/sv_emit.h"
#include <stdlib.h>
#include <string.h>

/*
 * Implementation of the compiler composition interface.
 *
 * This wires up the existing pigen compiler pipeline:
 * 1. Add source to source manager
 * 2. Preprocess (with include handling)
 * 3. Lex the preprocessed source
 * 4. Parse syntax
 * 5. Semantic analysis
 * 6. RTL lowering
 * 7. SystemVerilog emission
 */

int pigen_compile_source(pigen_source_manager *sources,
    const char *source_path,
    const char *source_text,
    size_t source_length,
    const pigen_compile_options *options,
    pigen_compile_result *result,
    pigen_compile_error *error)
{
    /* Zero the result on entry. */
    if (result) {
        result->systemverilog = NULL;
        result->length = 0;
    }

    /* Unused parameter. */
    (void)options;

    /* If sources is NULL, create a local source manager. */
    int own_sources = 0;
    if (!sources) {
        sources = malloc(sizeof(pigen_source_manager));
        if (!sources) {
            if (error) {
                error->origin = (pigen_origin_id){0};
                error->span = (pigen_source_span){0};
                error->message = "memory allocation failed";
            }
            return -1;
        }
        sources->files = NULL;
        sources->count = 0;
        sources->capacity = 0;
        own_sources = 1;
    }

    /* Add the source file to the source manager. */
    pigen_source_id source_id = pigen_source_add(sources, source_path, source_text, source_length);
    if (source_id.index == PIGEN_INVALID_ID) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = (pigen_source_span){0};
            error->message = "failed to add source file";
        }
        if (own_sources) free(sources);
        return -1;
    }

    /* Preprocess the source. */
    pigen_preprocess_result preprocess_result = {0};
    pigen_preprocess_error preprocess_error = {0};
    
    /* Use a simple include provider that rejects all includes (no include support yet). */
    pigen_source_provider provider = {0, NULL};
    
    int preprocess_ret = pigen_preprocess(sources, source_id, &provider, &preprocess_result, &preprocess_error);
    if (preprocess_ret != 0) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = preprocess_error.span;
            error->message = preprocess_error.message ? preprocess_error.message : "preprocessing failed";
        }
        pigen_free_preprocess_result(&preprocess_result);
        if (own_sources) free(sources);
        return -1;
    }

    /* Lex the preprocessed source. */
    pigen_tokens tokens = {0};
    const pigen_source_file *src_file = pigen_source_get(sources, source_id);
    if (!src_file) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = (pigen_source_span){0};
            error->message = "source file not found";
        }
        pigen_free_preprocess_result(&preprocess_result);
        if (own_sources) free(sources);
        return -1;
    }
    pigen_lex_source(src_file->text, src_file->length, &tokens);

    /* Parse syntax. */
    pigen_syntax_tree syntax_tree = {0};
    pigen_syntax_error syntax_error = {0};
    
    const pigen_expanded_source *expanded = &preprocess_result.expanded;
    int parse_ret = pigen_parse_syntax(expanded, &syntax_tree, &syntax_error);
    if (parse_ret != 1) {
        if (error) {
            error->origin = syntax_error.origin;
            error->span = syntax_error.span;
            error->message = syntax_error.message ? syntax_error.message : "syntax parsing failed";
        }
        pigen_free_syntax_tree(&syntax_tree);
        pigen_free_preprocess_result(&preprocess_result);
        pigen_free_tokens(&tokens);
        if (own_sources) free(sources);
        return -1;
    }

    /* Semantic analysis. */
    pigen_semantic_model semantic_model = {0};
    pigen_semantic_init(&semantic_model, sources);

    /* RTL lowering. */
    pigen_rtl_model rtl_model = {0};
    pigen_rtl_lowering lowering = {0};
    pigen_rtl_lowering_init(&lowering, &semantic_model, &rtl_model);

    /* SystemVerilog emission. */
    char *sv_output = NULL;
    int emit_ret = pigen_emit_systemverilog(sources, NULL, &rtl_model, &sv_output);
    
    if (emit_ret != 0) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = (pigen_source_span){0};
            error->message = "SystemVerilog emission failed";
        }
        pigen_free_preprocess_result(&preprocess_result);
        pigen_free_syntax_tree(&syntax_tree);
        pigen_free_semantic_model(&semantic_model);
        pigen_rtl_lowering_free(&lowering);
        pigen_free_tokens(&tokens);
        if (own_sources) free(sources);
        return -1;
    }

    /* Success. */
    if (result) {
        result->systemverilog = sv_output;
        result->length = strlen(sv_output);
    }

    /* Cleanup. */
    pigen_free_preprocess_result(&preprocess_result);
    pigen_free_syntax_tree(&syntax_tree);
    pigen_free_semantic_model(&semantic_model);
    pigen_rtl_lowering_free(&lowering);
    pigen_free_tokens(&tokens);
    if (own_sources) free(sources);

    return 0;
}

void pigen_free_compile_result(pigen_compile_result *result)
{
    if (result && result->systemverilog) {
        free(result->systemverilog);
        result->systemverilog = NULL;
        result->length = 0;
    }
}