#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/preprocess.h"
#include "pigen/syntax.h"
#include "pigen/semantic.h"
#include "pigen/resolve.h"
#include "pigen/rtl_lower.h"
#include "pigen/rtl_name.h"
#include "pigen/output.h"
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

/* True when the names arena already carries a SOURCE-kind name on ORIGIN
 * (same source and half-open byte range). */
static int has_source_name(const pigen_rtl_model *model,
	const pigen_source_span *origin)
{
    size_t i;

    for (i = 0; i < model->name_count; i++) {
        const pigen_rtl_name *name = &model->names[i];

        if (name->kind != PIGEN_RTL_NAME_SOURCE)
            continue;
        if (name->origin.source.index == origin->source.index &&
            name->origin.start == origin->start &&
            name->origin.end == origin->end)
            return 1;
    }
    return 0;
}

/* Append one SOURCE-kind name on ORIGIN with the explicit TEXT to the
 * model-owned arena. The arena allocator exits on failure. */
static void add_source_name(pigen_rtl_model *model,
	const pigen_source_span *origin, const char *text, size_t length)
{
    pigen_rtl_name *record;

    if (model->name_count == model->name_capacity) {
        size_t capacity = model->name_capacity ? model->name_capacity * 2 : 8;

        model->names = realloc(model->names, capacity * sizeof(*model->names));
        model->name_capacity = capacity;
    }
    record = &model->names[model->name_count];
    record->origin = *origin;
    record->kind = PIGEN_RTL_NAME_SOURCE;
    record->text = malloc(length + 1);
    memcpy(record->text, text, length);
    record->text[length] = '\0';
    model->name_count++;
}

static int span_equal(const pigen_source_span *a, const pigen_source_span *b)
{
    return a->source.index == b->source.index &&
        a->start == b->start && a->end == b->end;
}

static int span_invalid(const pigen_source_span *s)
{
    return s->source.index == PIGEN_INVALID_ID;
}

/* One pipeline field's declarator name span (the provenance the pipeline
 * lowering's field objects carry as their origin), resolved through the
 * expanded source's token origins; INVALID when the declaration carries no
 * usable name. */
static pigen_source_span pipeline_field_name_span(
    const pigen_syntax_tree *tree, const pigen_syntax_node *field,
    const pigen_expanded_source *expanded)
{
    const pigen_syntax_node *declarator;
    const pigen_expanded_token *token;
    pigen_source_span invalid =
        (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

    if (!field || field->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
        return invalid;
    declarator = pigen_syntax_get(tree, field->first_child);
    if (!declarator ||
        declarator->kind != PIGEN_SYNTAX_SIGNAL_DECLARATOR)
        return invalid;
    token = pigen_expanded_token_get(expanded,
        declarator->as.signal_declarator.name);
    if (!token)
        return invalid;
    return pigen_origin_spelling_span(expanded, token->origin);
}

/* Depth-first post-order successor of NODE in the syntax tree. */
static const pigen_syntax_node *next_syntax_node(const pigen_syntax_tree *tree,
    const pigen_syntax_node *node)
{
    if (node->first_child.index != PIGEN_INVALID_ID)
        return pigen_syntax_get(tree, node->first_child);
    if (node->next_sibling.index != PIGEN_INVALID_ID)
        return pigen_syntax_get(tree, node->next_sibling);
    while (node->parent.index != PIGEN_INVALID_ID) {
        node = pigen_syntax_get(tree, node->parent);
        if (node->next_sibling.index != PIGEN_INVALID_ID)
            return pigen_syntax_get(tree, node->next_sibling);
    }
    return NULL;
}

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
    if (preprocess_ret == 0) {
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

    /* Semantic analysis initialization. */
    pigen_semantic_model semantic_model = {0};
    pigen_semantic_init(&semantic_model, sources);

    /* The resolve policy must carry a positive maximum_generated_bits or the
     * resolver treats the input as no-policy and returns with no model. A
     * caller-provided option wins; otherwise the entry supplies a valid
     * default so a composed compile never resolves under a no-policy stance. */
    pigen_resolve_policy resolve_policy = {0};
    resolve_policy.maximum_generated_bits =
        options && options->maximum_generated_bits ?
        options->maximum_generated_bits : 1024;
    pigen_semantic_error semantic_error = {0};
    
    int resolve_ret = pigen_resolve_semantics(&syntax_tree, &semantic_model, &resolve_policy, &semantic_error);
    if (resolve_ret != 1) {
        if (error) {
            error->origin = semantic_error.origin;
            error->span = semantic_error.span;
            error->message = semantic_error.message ? semantic_error.message : "semantic resolution failed";
        }
        pigen_free_preprocess_result(&preprocess_result);
        pigen_free_syntax_tree(&syntax_tree);
        pigen_free_semantic_model(&semantic_model);
        pigen_free_tokens(&tokens);
        if (own_sources) free(sources);
        return -1;
    }

    /* RTL lowering initialization. */
    pigen_rtl_model rtl_model = {0};
    pigen_rtl_lowering lowering = {0};
    pigen_rtl_lowering_init(&lowering, &semantic_model, &rtl_model);

    /* RTL module lowering. pigen_lower_rtl_transfers runs the declaration
     * lowering internally (it owns the single module-memo reset and publishes
     * exactly one module), so the entry must NOT call the declaration lowering
     * separately: a second call would reset the memo and create a second,
     * empty module the composition gate rejects. */
    for (size_t i = 0; i < semantic_model.module_count; i++) {
        pigen_semantic_module *sem_module = &semantic_model.modules[i];
        pigen_module_id module = pigen_symbol_module(&semantic_model, sem_module->symbol);
        
        int transfer_ret = pigen_lower_rtl_transfers(&lowering, module);
        if (transfer_ret != 0) {
            if (error) {
                error->origin = (pigen_origin_id){0};
                error->span = (pigen_source_span){0};
                error->message = "RTL transfer lowering failed";
            }
            pigen_free_preprocess_result(&preprocess_result);
            pigen_free_syntax_tree(&syntax_tree);
            pigen_free_semantic_model(&semantic_model);
            pigen_rtl_lowering_free(&lowering);
            pigen_free_tokens(&tokens);
            if (own_sources) free(sources);
            return -1;
        }
        
        pigen_source_span pipeline_span = (pigen_source_span){0};
        int pipeline_ret = pigen_lower_rtl_pipelines(&lowering, module,
            &syntax_tree, &preprocess_result.expanded, &pipeline_span);
        if (pipeline_ret != 0) {
            if (error) {
                error->origin = (pigen_origin_id){0};
                error->span = pipeline_span;
                error->message = "RTL pipeline lowering failed";
            }
            pigen_free_preprocess_result(&preprocess_result);
            pigen_free_syntax_tree(&syntax_tree);
            pigen_free_semantic_model(&semantic_model);
            pigen_rtl_lowering_free(&lowering);
            pigen_free_tokens(&tokens);
            if (own_sources) free(sources);
            return -1;
        }

    pigen_rtl_module_id rtl_module = pigen_lower_rtl_module(&lowering, module);
        if (rtl_module.index == PIGEN_INVALID_ID) {
            if (error) {
                error->origin = (pigen_origin_id){0};
                error->span = (pigen_source_span){0};
                error->message = "RTL module lowering failed";
            }
            pigen_free_preprocess_result(&preprocess_result);
            pigen_free_syntax_tree(&syntax_tree);
            pigen_free_semantic_model(&semantic_model);
            pigen_rtl_lowering_free(&lowering);
            pigen_free_tokens(&tokens);
            if (own_sources) free(sources);
            return -1;
        }
    }

    /* RTL naming. The emitter resolves each object's final name as the
     * SOURCE-kind name whose origin span equals the object's origin (the
     * signal's identifier span, per the lowering), and the module's name as the
     * SOURCE name no object claims. So the entry requests one SOURCE name per
     * module identifier span, one per signal identifier span, and one per
     * pipeline field declarator name span (the pipeline lowering's field
     * objects carry that span as their origin), each taken from the
     * identifier, not the whole statement. */
    pigen_rtl_name_request *name_requests = NULL;
    size_t name_request_count = 0;
    size_t field_span_count = 0;
    {
        const pigen_syntax_node *node;

        for (node = pigen_syntax_get(&syntax_tree, (pigen_syntax_id){0});
             node; node = next_syntax_node(&syntax_tree, node)) {
            const pigen_syntax_node *child;

            if (node->kind != PIGEN_SYNTAX_PIPELINE)
                continue;
            for (child = pigen_syntax_get(&syntax_tree, node->first_child);
                 child; child = pigen_syntax_get(&syntax_tree,
                     child->next_sibling)) {
                if (child->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
                    continue;
                {
                    pigen_source_span field_span =
                        pipeline_field_name_span(&syntax_tree, child,
                            &preprocess_result.expanded);

                    if (!span_invalid(&field_span))
                        field_span_count++;
                }
            }
        }
    }
    size_t name_request_capacity = semantic_model.module_count +
        semantic_model.signal_count + field_span_count;
    if (name_request_capacity > 0) {
        name_requests = malloc(name_request_capacity * sizeof(pigen_rtl_name_request));
        if (!name_requests) {
            if (error) {
                error->origin = (pigen_origin_id){0};
                error->span = (pigen_source_span){0};
                error->message = "memory allocation failed for name requests";
            }
            pigen_free_preprocess_result(&preprocess_result);
            pigen_free_syntax_tree(&syntax_tree);
            pigen_free_semantic_model(&semantic_model);
            pigen_rtl_lowering_free(&lowering);
            pigen_free_tokens(&tokens);
            if (own_sources) free(sources);
            return -1;
        }
        
        for (size_t i = 0; i < semantic_model.module_count; i++) {
            const pigen_semantic_module *sem_module = &semantic_model.modules[i];
            const pigen_symbol *module_symbol = pigen_symbol_get(&semantic_model,
                sem_module->symbol);
            name_requests[name_request_count].kind = PIGEN_RTL_NAME_SOURCE;
            name_requests[name_request_count].origin =
                module_symbol ? module_symbol->name : sem_module->span;
            name_request_count++;
        }
        for (size_t i = 0; i < semantic_model.signal_count; i++) {
            const pigen_semantic_signal *sem_signal = &semantic_model.signals[i];
            const pigen_symbol *signal_symbol = pigen_symbol_get(&semantic_model,
                sem_signal->symbol);
            name_requests[name_request_count].kind = PIGEN_RTL_NAME_SOURCE;
            name_requests[name_request_count].origin =
                signal_symbol ? signal_symbol->name : sem_signal->span;
            name_request_count++;
        }
        for (const pigen_syntax_node *node =
                 pigen_syntax_get(&syntax_tree, (pigen_syntax_id){0});
             node; node = next_syntax_node(&syntax_tree, node)) {
            const pigen_syntax_node *child;

            if (node->kind != PIGEN_SYNTAX_PIPELINE)
                continue;
            for (child = pigen_syntax_get(&syntax_tree, node->first_child);
                 child; child = pigen_syntax_get(&syntax_tree,
                     child->next_sibling)) {
                if (child->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
                    continue;
                {
                    pigen_source_span field_span =
                        pipeline_field_name_span(&syntax_tree, child,
                            &preprocess_result.expanded);

                    if (span_invalid(&field_span))
                        continue;
                    name_requests[name_request_count].kind =
                        PIGEN_RTL_NAME_SOURCE;
                    name_requests[name_request_count].origin = field_span;
                    name_request_count++;
                }
            }
        }
    }
    
    pigen_rtl_name_id naming_result = pigen_rtl_assign_names(&rtl_model, sources, name_requests, name_request_count);
    if (name_requests) free(name_requests);
    
    if (naming_result.index == PIGEN_INVALID_ID) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = (pigen_source_span){0};
            error->message = "RTL naming failed";
        }
        pigen_free_preprocess_result(&preprocess_result);
        pigen_free_syntax_tree(&syntax_tree);
        pigen_free_semantic_model(&semantic_model);
        pigen_rtl_lowering_free(&lowering);
        pigen_free_tokens(&tokens);
        if (own_sources) free(sources);
        return -1;
    }

    /* The stage-boundary storage instances are published with the stage
     * nodes' origin spans (src/pipeline_lower.c) and no source name exists
     * on those spans, so the generic naming pass leaves them unnamed; the
     * emission gives every one of them the storage-primitive name
     * (does/the/pipeline/and/fsm/reach/the/compiled/output.md). */
    for (size_t i = 0; i < rtl_model.instance_count; i++) {
        const pigen_source_span *origin = &rtl_model.instances[i].origin;
        const pigen_syntax_node *node;
        int stage_span = 0;

        if (origin->source.index == PIGEN_INVALID_ID)
            continue;
        for (node = pigen_syntax_get(&syntax_tree, (pigen_syntax_id){0});
             node; node = next_syntax_node(&syntax_tree, node)) {
            const pigen_syntax_node *child;

            if (node->kind != PIGEN_SYNTAX_PIPELINE)
                continue;
            for (child = pigen_syntax_get(&syntax_tree, node->first_child);
                 child; child = pigen_syntax_get(&syntax_tree,
                     child->next_sibling)) {
                if (child->kind == PIGEN_SYNTAX_STAGE &&
                    span_equal(origin, &child->location.source_span)) {
                    stage_span = 1;
                    break;
                }
            }
            if (stage_span)
                break;
        }
        if (stage_span && !has_source_name(&rtl_model, origin))
            add_source_name(&rtl_model, origin, "pigen_buf", 9);
    }

    /* The pipeline's synthetic ready object carries an invalid origin (the
     * ready equation's destination, src/pipeline_lower.c), so the shared
     * naming pass cannot name it through a checked identifier span; the
     * emission resolves an object's name through a SOURCE-kind record on its
     * exact origin span, so the object gets the role stem on that same
     * invalid span, the way the stage instances get the storage-primitive
     * name. A real guard value already carries its guard object's valid span. */
    for (size_t i = 0; i < rtl_model.object_count; i++) {
        const pigen_source_span *origin = &rtl_model.objects[i].origin;
        int ready_dest = 0;

        if (origin->source.index != PIGEN_INVALID_ID)
            continue;
        for (size_t e = 0; e < rtl_model.equation_count && !ready_dest; e++)
            ready_dest =
                rtl_model.equations[e].origin.source.index == PIGEN_INVALID_ID &&
                rtl_model.equations[e].destination.index == (uint32_t)i;
        if (ready_dest && !has_source_name(&rtl_model, origin))
            add_source_name(&rtl_model, origin, "ready", 5);
    }

    /* Build the output model from the syntax tree and RTL lowering. */
    pigen_output_model output_model = {0};
    pigen_output_error output_error = {0};
    
    int build_ret = pigen_build_output_model(&output_model, &syntax_tree, &lowering, &output_error);
    if (build_ret != 0) {
        if (error) {
            error->origin = (pigen_origin_id){0};
            error->span = output_error.span;
            error->message = output_error.message ? output_error.message : "output model build failed";
        }
        pigen_free_preprocess_result(&preprocess_result);
        pigen_free_syntax_tree(&syntax_tree);
        pigen_free_semantic_model(&semantic_model);
        pigen_rtl_lowering_free(&lowering);
        pigen_free_tokens(&tokens);
        if (own_sources) free(sources);
        return -1;
    }

    /* SystemVerilog emission. */
    char *sv_output = NULL;
    int emit_ret = pigen_emit_systemverilog(sources, &output_model, &rtl_model, &sv_output);
    
    pigen_free_output_model(&output_model);
    
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