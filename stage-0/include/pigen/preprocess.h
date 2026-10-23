#ifndef PIGEN_PREPROCESS_H
#define PIGEN_PREPROCESS_H

#include <stddef.h>

#include "pigen/lexer.h"
#include "pigen/source.h"

typedef enum {
	PIGEN_ORIGIN_SOURCE,
	PIGEN_ORIGIN_MACRO_REPLACEMENT,
	PIGEN_ORIGIN_MACRO_ARGUMENT
} pigen_origin_kind;

typedef struct {
	pigen_origin_kind kind;
	union {
		pigen_source_span source;
		struct {
			pigen_origin_id invocation;
			pigen_origin_id replacement;
			pigen_macro_id macro;
		} macro_replacement;
		struct {
			pigen_origin_id invocation;
			pigen_origin_id parameter;
			pigen_origin_id argument;
			pigen_macro_id macro;
		} macro_argument;
	} as;
} pigen_token_origin;

typedef struct {
	pigen_token_kind kind;
	pigen_origin_id origin;
} pigen_expanded_token;

typedef struct {
	pigen_source_span name;
	pigen_origin_id origin;
} pigen_macro_parameter;

typedef struct {
	pigen_source_span name;
	pigen_source_span definition;
	size_t first_parameter;
	size_t parameter_count;
	size_t first_replacement;
	size_t replacement_count;
	int function_like;
	int active;
} pigen_macro_definition;

/* One immutable physical file, tokenized without expansion.  The source
 * manager retains the exact bytes, including every gap between tokens. */
typedef struct {
	pigen_source_id source;
	pigen_token *tokens;
	size_t token_count;
} pigen_source_file_view;

/* A written include site and the physical file selected by the provider. */
typedef struct {
	pigen_source_span directive;
	pigen_source_span path;
	pigen_source_id included_source;
} pigen_source_include;

typedef struct {
	const pigen_source_manager *sources;
	pigen_source_id root_source;
	pigen_source_file_view *files;
	size_t file_count;
	size_t file_capacity;
	pigen_source_include *includes;
	size_t include_count;
	size_t include_capacity;
} pigen_source_view;

typedef struct {
	const pigen_source_manager *sources;
	pigen_source_id root_source;
	pigen_expanded_token *tokens;
	size_t token_count;
	size_t token_capacity;
	pigen_token_origin *origins;
	size_t origin_count;
	size_t origin_capacity;
	pigen_macro_definition *macros;
	size_t macro_count;
	size_t macro_capacity;
	pigen_macro_parameter *macro_parameters;
	size_t macro_parameter_count;
	size_t macro_parameter_capacity;
	pigen_expanded_token *replacement_tokens;
	size_t replacement_token_count;
	size_t replacement_token_capacity;
} pigen_expanded_source;

typedef struct {
	pigen_source_view written;
	pigen_expanded_source expanded;
} pigen_preprocess_result;

typedef struct {
	pigen_source_span span;
	const char *message;
} pigen_preprocess_error;

typedef int (*pigen_include_loader)(void *context,
	pigen_source_id including_source, const char *path, size_t path_length,
	pigen_source_id *included_source, const char **error_message);

typedef struct {
	void *context;
	pigen_include_loader load;
} pigen_source_provider;

int pigen_preprocess(const pigen_source_manager *sources,
	pigen_source_id source, const pigen_source_provider *provider,
	pigen_preprocess_result *result,
	pigen_preprocess_error *error);
const pigen_expanded_token *pigen_expanded_token_get(
	const pigen_expanded_source *source, pigen_token_id token);
const pigen_token_origin *pigen_origin_get(
	const pigen_expanded_source *source, pigen_origin_id origin);
pigen_source_span pigen_origin_spelling_span(
	const pigen_expanded_source *source, pigen_origin_id origin);
pigen_source_span pigen_origin_expansion_span(
	const pigen_expanded_source *source, pigen_origin_id origin);
const char *pigen_expanded_token_text(
	const pigen_expanded_source *source,
	const pigen_expanded_token *token, size_t *length);
const pigen_source_file_view *pigen_source_view_file(
	const pigen_source_view *view, pigen_source_id source);
void pigen_free_preprocess_result(pigen_preprocess_result *result);

#endif
