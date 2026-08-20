#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/preprocess.h"

typedef struct {
	pigen_source_id including;
	const char *path;
	pigen_source_id included;
} include_mapping;

typedef struct {
	const include_mapping *mappings;
	size_t count;
} include_context;

static int load_include(void *opaque, pigen_source_id including_source,
	const char *path, size_t path_length, pigen_source_id *included_source,
	const char **error_message)
{
	include_context *context = opaque;
	size_t i;
	for (i = 0; i < context->count; i++)
	{
		const include_mapping *mapping = &context->mappings[i];
		if (including_source.index == mapping->including.index &&
			path_length == strlen(mapping->path) &&
			!memcmp(path, mapping->path, path_length))
		{
			*included_source = mapping->included;
			return 1;
		}
	}
	*error_message = "unexpected include path";
	return 0;
}

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static int token_is(const pigen_expanded_source *source,
	const pigen_expanded_token *token, const char *expected)
{
	size_t length;
	const char *text = pigen_expanded_token_text(source, token, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static const pigen_expanded_token *find_token(
	const pigen_expanded_source *source, const char *expected)
{
	size_t i;
	for (i = 0; i < source->token_count; i++)
		if (token_is(source, &source->tokens[i], expected))
			return &source->tokens[i];
	return NULL;
}

static size_t span_newlines(const pigen_source_manager *sources,
	pigen_source_span span)
{
	size_t length;
	size_t count = 0;
	size_t i;
	const char *text = pigen_source_span_text(sources, span, &length);
	assert(text);
	for (i = 0; i < length; i++)
		if (text[i] == '\n') count++;
	return count;
}

static void assert_argument_origin(const pigen_source_manager *sources,
	const pigen_expanded_source *expanded,
	const pigen_expanded_token *token, const char *parameter,
	const char *spelling, const char *invocation)
{
	const pigen_token_origin *origin;
	assert(token);
	origin = pigen_origin_get(expanded, token->origin);
	assert(origin && origin->kind == PIGEN_ORIGIN_MACRO_ARGUMENT);
	assert(span_is(sources, pigen_origin_spelling_span(expanded,
		origin->as.macro_argument.parameter), parameter));
	assert(span_is(sources,
		pigen_origin_spelling_span(expanded, token->origin), spelling));
	assert(span_is(sources,
		pigen_origin_expansion_span(expanded, token->origin), invocation));
}

int main(void)
{
	const char source_text[] =
		"`define EMPTY()\n"
		"`define PICK(first,second) second\n"
		"`define ADD(left,right) ((left)+(right))\n"
		"`define WRAP(value) `ADD(value, 8'h01)\n"
		"`define END endmodule\n"
		"module `EMPTY()`PICK({ignored, also_ignored}, chosen) #(parameter VALUE = `WRAP(8'h0f));\n"
		"`END\n";
	const char wrong_arity_text[] =
		"`define PAIR(left,right) left\n"
		"module `PAIR(only); endmodule\n";
	const char duplicate_parameter_text[] =
		"`define BAD(value,value) value\n";
	const char multiline_text[] =
		"`define MULTI(left,\\\n"
		"right) (\\\n"
		"(left) +\\\n"
		"(right)\\\n"
		")\n"
		"`define OBJECT first +\\\n"
		"second\n"
		"module value #(parameter X = `MULTI(1,2)); endmodule\n"
		"object_result `OBJECT\n";
	const char malformed_continuation_text[] =
		"`define BAD value \\ \n"
		"module ignored; endmodule\n";
	const char conditional_text[] =
		"`define PRESENT\n"
		"`ifdef ABSENT\n"
		"`define GHOST ignored \\\n"
		"`ifdef PRESENT\n"
		"`UNDEFINED\n"
		"`elsif PRESENT selected\n"
		"`ifndef ABSENT nested\n"
		"`else wrong_nested\n"
		"`endif\n"
		"`else wrong_outer\n"
		"`endif\n"
		"`ifdef GHOST wrong_ghost\n"
		"`else clean\n"
		"`endif\n";
	const char unterminated_conditional_text[] =
		"`ifdef OPEN\n"
		"inactive\n";
	const char repeated_else_text[] =
		"`ifdef MISSING\n"
		"first\n"
		"`else\n"
		"second\n"
		"`else\n"
		"third\n"
		"`endif\n";
	const char include_root_text[] =
		"`define CROSS_FILE\n"
		"`include \"conditional.inc\"\n"
		"caller_branch\n"
		"`endif\n";
	const char include_text[] =
		"`ifdef CROSS_FILE\n"
		"included_branch\n";
	const char macro_include_root_text[] =
		"`define QUOTED(path) path\n"
		"`define ANGLED <angle/path.svh>\n"
		"`include `QUOTED(\"macro.inc\")\n"
		"quoted_after\n"
		"`include `ANGLED\n"
		"angle_after\n";
	const char quoted_include_text[] = "quoted_included\n";
	const char angle_include_text[] = "angle_included\n";
	const char malformed_include_text[] =
		"`define BAD_PATH \"first.inc\" \"second.inc\"\n"
		"`include `BAD_PATH\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "macros.pigen",
		source_text, strlen(source_text));
	pigen_source_id wrong_arity_source = pigen_source_add(&sources,
		"wrong_arity.pigen", wrong_arity_text, strlen(wrong_arity_text));
	pigen_source_id duplicate_parameter_source = pigen_source_add(&sources,
		"duplicate_parameter.pigen", duplicate_parameter_text,
		strlen(duplicate_parameter_text));
	pigen_source_id multiline_source = pigen_source_add(&sources,
		"multiline.pigen", multiline_text, strlen(multiline_text));
	pigen_source_id malformed_continuation_source = pigen_source_add(&sources,
		"malformed_continuation.pigen", malformed_continuation_text,
		strlen(malformed_continuation_text));
	pigen_source_id conditional_source = pigen_source_add(&sources,
		"conditional.pigen", conditional_text, strlen(conditional_text));
	pigen_source_id unterminated_conditional_source = pigen_source_add(&sources,
		"unterminated_conditional.pigen", unterminated_conditional_text,
		strlen(unterminated_conditional_text));
	pigen_source_id repeated_else_source = pigen_source_add(&sources,
		"repeated_else.pigen", repeated_else_text,
		strlen(repeated_else_text));
	pigen_source_id include_root_source = pigen_source_add(&sources,
		"include_root.pigen", include_root_text, strlen(include_root_text));
	pigen_source_id included_source = pigen_source_add(&sources,
		"conditional.inc", include_text, strlen(include_text));
	pigen_source_id macro_include_root_source = pigen_source_add(&sources,
		"macro_include_root.pigen", macro_include_root_text,
		strlen(macro_include_root_text));
	pigen_source_id quoted_include_source = pigen_source_add(&sources,
		"macro.inc", quoted_include_text, strlen(quoted_include_text));
	pigen_source_id angle_include_source = pigen_source_add(&sources,
		"angle/path.svh", angle_include_text, strlen(angle_include_text));
	pigen_source_id malformed_include_source = pigen_source_add(&sources,
		"malformed_include.pigen", malformed_include_text,
		strlen(malformed_include_text));
	include_mapping include_mappings[] = {
		{include_root_source, "conditional.inc", included_source},
		{macro_include_root_source, "macro.inc", quoted_include_source},
		{macro_include_root_source, "angle/path.svh", angle_include_source}
	};
	include_context include_loader_context = {include_mappings,
		sizeof(include_mappings) / sizeof(*include_mappings)};
	pigen_source_provider provider = {&include_loader_context, load_include};
	pigen_preprocess_result result = {0};
	pigen_preprocess_result conditional_result = {0};
	pigen_preprocess_result multiline_result = {0};
	pigen_preprocess_result include_result = {0};
	pigen_preprocess_result macro_include_result = {0};
	pigen_preprocess_result invalid_result = {0};
	pigen_preprocess_error error = {0};
	const pigen_expanded_token *chosen;
	const pigen_expanded_token *actual;
	const pigen_expanded_token *fixed;
	const pigen_expanded_token *end;
	const pigen_expanded_token *second;
	const pigen_token_origin *origin;

	if (!pigen_preprocess(&sources, source, NULL, &result, &error))
		fprintf(stderr, "unexpected preprocess error: %s\n",
			error.message ? error.message : "unknown");
	assert(!error.message);
	assert(result.expanded.macro_count == 5);
	assert(result.expanded.macro_parameter_count == 5);
	assert(result.expanded.token_count == 20);
	assert(token_is(&result.expanded, &result.expanded.tokens[0], "module"));
	assert(token_is(&result.expanded, &result.expanded.tokens[1], "chosen"));
	assert(token_is(&result.expanded,
		&result.expanded.tokens[result.expanded.token_count - 1], ""));

	chosen = find_token(&result.expanded, "chosen");
	actual = find_token(&result.expanded, "8'h0f");
	fixed = find_token(&result.expanded, "8'h01");
	end = find_token(&result.expanded, "endmodule");
	assert(chosen && actual && fixed && end);
	assert_argument_origin(&sources, &result.expanded, chosen, "second",
		"chosen", "PICK");
	assert_argument_origin(&sources, &result.expanded, actual, "left",
		"8'h0f", "WRAP");
	assert_argument_origin(&sources, &result.expanded, fixed, "right",
		"8'h01", "WRAP");
	origin = pigen_origin_get(&result.expanded, actual->origin);
	assert(origin->as.macro_argument.macro.index == 2);
	origin = pigen_origin_get(&result.expanded, end->origin);
	assert(origin && origin->kind == PIGEN_ORIGIN_MACRO_REPLACEMENT);
	assert(span_is(&sources, pigen_origin_spelling_span(&result.expanded,
		end->origin), "endmodule"));
	assert(span_is(&sources, pigen_origin_expansion_span(&result.expanded,
		end->origin), "END"));

	assert(!pigen_preprocess(&sources, wrong_arity_source, NULL,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "wrong number"));
	assert(span_is(&sources, error.span, "PAIR"));
	assert(!pigen_preprocess(&sources, duplicate_parameter_source, NULL,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "duplicate macro parameter"));
	assert(span_is(&sources, error.span, "value"));
	assert(pigen_preprocess(&sources, multiline_source, NULL,
		&multiline_result, &error));
	assert(multiline_result.expanded.macro_count == 2);
	assert(multiline_result.expanded.macro_parameter_count == 2);
	assert(multiline_result.expanded.macros[0].replacement_count == 9);
	assert(multiline_result.expanded.macros[1].replacement_count == 3);
	assert(multiline_result.expanded.token_count == 24);
	assert(span_newlines(&sources,
		multiline_result.expanded.macros[0].definition) == 4);
	assert(!find_token(&multiline_result.expanded, "\\"));
	assert_argument_origin(&sources, &multiline_result.expanded,
		find_token(&multiline_result.expanded, "1"), "left", "1", "MULTI");
	assert_argument_origin(&sources, &multiline_result.expanded,
		find_token(&multiline_result.expanded, "2"), "right", "2", "MULTI");
	second = find_token(&multiline_result.expanded, "second");
	assert(second);
	origin = pigen_origin_get(&multiline_result.expanded, second->origin);
	assert(origin && origin->kind == PIGEN_ORIGIN_MACRO_REPLACEMENT);
	assert(span_is(&sources, pigen_origin_expansion_span(
		&multiline_result.expanded, second->origin), "OBJECT"));
	assert(!pigen_preprocess(&sources, malformed_continuation_source, NULL,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "immediately precede newline"));
	assert(span_is(&sources, error.span, "\\"));

	assert(pigen_preprocess(&sources, conditional_source, NULL,
		&conditional_result, &error));
	assert(conditional_result.expanded.macro_count == 1);
	assert(conditional_result.expanded.token_count == 4);
	assert(token_is(&conditional_result.expanded,
		&conditional_result.expanded.tokens[0], "selected"));
	assert(token_is(&conditional_result.expanded,
		&conditional_result.expanded.tokens[1], "nested"));
	assert(token_is(&conditional_result.expanded,
		&conditional_result.expanded.tokens[2], "clean"));
	assert(!find_token(&conditional_result.expanded, "wrong_nested"));
	assert(!find_token(&conditional_result.expanded, "wrong_outer"));
	assert(!find_token(&conditional_result.expanded, "wrong_ghost"));
	assert(!pigen_preprocess(&sources, unterminated_conditional_source, NULL,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "unterminated conditional"));
	assert(span_is(&sources, error.span, "`ifdef OPEN"));
	assert(!pigen_preprocess(&sources, repeated_else_source, NULL,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "more than one `else"));
	assert(span_is(&sources, error.span, "`else"));
	assert(pigen_preprocess(&sources, include_root_source, &provider,
		&include_result, &error));
	assert(include_result.written.include_count == 1);
	assert(include_result.written.includes[0].included_source.index ==
		included_source.index);
	assert(include_result.expanded.token_count == 3);
	assert(token_is(&include_result.expanded,
		&include_result.expanded.tokens[0], "included_branch"));
	assert(token_is(&include_result.expanded,
		&include_result.expanded.tokens[1], "caller_branch"));
	assert(pigen_preprocess(&sources, macro_include_root_source, &provider,
		&macro_include_result, &error));
	assert(macro_include_result.written.include_count == 2);
	assert(span_is(&sources,
		macro_include_result.written.includes[0].path,
		"`QUOTED(\"macro.inc\")"));
	assert(span_is(&sources,
		macro_include_result.written.includes[1].path, "`ANGLED"));
	assert(macro_include_result.expanded.token_count == 5);
	assert(token_is(&macro_include_result.expanded,
		&macro_include_result.expanded.tokens[0], "quoted_included"));
	assert(token_is(&macro_include_result.expanded,
		&macro_include_result.expanded.tokens[1], "quoted_after"));
	assert(token_is(&macro_include_result.expanded,
		&macro_include_result.expanded.tokens[2], "angle_included"));
	assert(token_is(&macro_include_result.expanded,
		&macro_include_result.expanded.tokens[3], "angle_after"));
	assert(!find_token(&macro_include_result.expanded, "\"macro.inc\""));
	assert(!find_token(&macro_include_result.expanded, "angle"));
	assert(!pigen_preprocess(&sources, malformed_include_source, &provider,
		&invalid_result, &error));
	assert(error.message && strstr(error.message, "quoted or angle-bracket"));
	assert(span_is(&sources, error.span, "`BAD_PATH"));

	pigen_free_preprocess_result(&invalid_result);
	pigen_free_preprocess_result(&macro_include_result);
	pigen_free_preprocess_result(&include_result);
	pigen_free_preprocess_result(&multiline_result);
	pigen_free_preprocess_result(&conditional_result);
	pigen_free_preprocess_result(&result);
	pigen_free_sources(&sources);
	puts("PASS: macros and conditional compilation preserve token provenance");
	return 0;
}
