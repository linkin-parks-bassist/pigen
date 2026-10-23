/* Token preprocessing with explicit macro-expansion provenance. */
#include <stdlib.h>
#include <string.h>

#include "pigen/preprocess.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	pigen_expanded_source *result;
	pigen_source_view *written;
	pigen_preprocess_error *error;
	const pigen_source_provider *provider;
	pigen_macro_id *expansion_stack;
	size_t expansion_depth;
	size_t expansion_capacity;
	pigen_source_id *include_stack;
	size_t include_depth;
	size_t include_capacity;
	struct conditional_frame *conditionals;
	size_t conditional_depth;
	size_t conditional_capacity;
} preprocessor;

typedef struct {
	const pigen_expanded_token *tokens;
	size_t count;
} macro_argument;

typedef struct conditional_frame {
	pigen_source_span opening;
	int parent_active;
	int branch_taken;
	int branch_active;
	int saw_else;
} conditional_frame;

typedef enum {
	CONDITIONAL_NONE,
	CONDITIONAL_IFDEF,
	CONDITIONAL_IFNDEF,
	CONDITIONAL_ELSIF,
	CONDITIONAL_ELSE,
	CONDITIONAL_ENDIF
} conditional_kind;

typedef struct {
	size_t *indices;
	size_t count;
	size_t capacity;
	size_t after;
	size_t definition_end;
} logical_directive;

typedef struct {
	pigen_expanded_token **tokens;
	size_t *count;
	size_t *capacity;
} expansion_output;

static int fail(preprocessor *preprocessor, pigen_source_span span,
	const char *message);

const pigen_source_file_view *pigen_source_view_file(
	const pigen_source_view *view, pigen_source_id source)
{
	size_t i;
	if (!view) return NULL;
	for (i = 0; i < view->file_count; i++)
		if (view->files[i].source.index == source.index) return &view->files[i];
	return NULL;
}

static int source_tokens(preprocessor *preprocessor, pigen_source_id source,
	const pigen_source_file *file, pigen_tokens *tokens)
{
	const pigen_source_file_view *known = pigen_source_view_file(
		preprocessor->written, source);
	pigen_source_file_view added = {0};

	if (known)
	{
		tokens->items = known->tokens;
		tokens->count = tokens->capacity = known->token_count;
		return 1;
	}
	pigen_lex_source(file->text, file->length, tokens);
	if (preprocessor->written->file_count ==
		preprocessor->written->file_capacity)
	{
		preprocessor->written->file_capacity =
			preprocessor->written->file_capacity ?
			preprocessor->written->file_capacity * 2 : 8;
		preprocessor->written->files = pigen_resize(
			preprocessor->written->files,
			preprocessor->written->file_capacity *
				sizeof(*preprocessor->written->files));
	}
	added.source = source;
	added.tokens = tokens->items;
	added.token_count = tokens->count;
	preprocessor->written->files[preprocessor->written->file_count++] = added;
	return 1;
}

static int record_include(preprocessor *preprocessor,
	pigen_source_include include)
{
	size_t i;
	pigen_source_view *written = preprocessor->written;

	for (i = 0; i < written->include_count; i++)
	{
		pigen_source_include *known = &written->includes[i];
		if (known->directive.source.index == include.directive.source.index &&
			known->directive.start == include.directive.start &&
			known->directive.end == include.directive.end)
		{
			if (known->included_source.index != include.included_source.index)
				return fail(preprocessor, include.path,
					"source provider resolved one include inconsistently");
			return 1;
		}
	}
	if (written->include_count == written->include_capacity)
	{
		written->include_capacity = written->include_capacity ?
			written->include_capacity * 2 : 8;
		written->includes = pigen_resize(written->includes,
			written->include_capacity * sizeof(*written->includes));
	}
	written->includes[written->include_count++] = include;
	return 1;
}

static pigen_source_span invalid_span(void)
{
	return (pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
}

static int fail(preprocessor *preprocessor, pigen_source_span span,
	const char *message)
{
	if (preprocessor->error)
	{
		preprocessor->error->span = span;
		preprocessor->error->message = message;
	}
	return 0;
}

static pigen_origin_id add_origin(pigen_expanded_source *result,
	pigen_token_origin origin)
{
	pigen_origin_id id;

	if (result->origin_count == PIGEN_INVALID_ID)
		pigen_fail("too many token origins");
	if (result->origin_count == result->origin_capacity)
	{
		result->origin_capacity = result->origin_capacity ?
			result->origin_capacity * 2 : 128;
		result->origins = pigen_resize(result->origins,
			result->origin_capacity * sizeof(*result->origins));
	}
	id = (pigen_origin_id){(uint32_t)result->origin_count};
	result->origins[result->origin_count++] = origin;
	return id;
}

static pigen_origin_id add_source_origin(pigen_expanded_source *result,
	pigen_source_span span)
{
	pigen_token_origin origin = {0};
	origin.kind = PIGEN_ORIGIN_SOURCE;
	origin.as.source = span;
	return add_origin(result, origin);
}

static pigen_origin_id add_replacement_origin(pigen_expanded_source *result,
	pigen_origin_id invocation, pigen_origin_id replacement, pigen_macro_id macro)
{
	pigen_token_origin origin = {0};
	origin.kind = PIGEN_ORIGIN_MACRO_REPLACEMENT;
	origin.as.macro_replacement.invocation = invocation;
	origin.as.macro_replacement.replacement = replacement;
	origin.as.macro_replacement.macro = macro;
	return add_origin(result, origin);
}

static pigen_origin_id add_argument_origin(pigen_expanded_source *result,
	pigen_origin_id invocation, pigen_origin_id parameter,
	pigen_origin_id argument, pigen_macro_id macro)
{
	pigen_token_origin origin = {0};
	origin.kind = PIGEN_ORIGIN_MACRO_ARGUMENT;
	origin.as.macro_argument.invocation = invocation;
	origin.as.macro_argument.parameter = parameter;
	origin.as.macro_argument.argument = argument;
	origin.as.macro_argument.macro = macro;
	return add_origin(result, origin);
}

static expansion_output primary_output(pigen_expanded_source *result)
{
	return (expansion_output){&result->tokens, &result->token_count,
		&result->token_capacity};
}

static void append_token(expansion_output *output,
	pigen_expanded_token token)
{
	if (*output->count == *output->capacity)
	{
		*output->capacity = *output->capacity ? *output->capacity * 2 : 128;
		*output->tokens = pigen_resize(*output->tokens,
			*output->capacity * sizeof(**output->tokens));
	}
	(*output->tokens)[(*output->count)++] = token;
}

static void append_replacement_token(pigen_expanded_source *result,
	pigen_expanded_token token)
{
	if (result->replacement_token_count == result->replacement_token_capacity)
	{
		result->replacement_token_capacity = result->replacement_token_capacity ?
			result->replacement_token_capacity * 2 : 64;
		result->replacement_tokens = pigen_resize(result->replacement_tokens,
			result->replacement_token_capacity *
			sizeof(*result->replacement_tokens));
	}
	result->replacement_tokens[result->replacement_token_count++] = token;
}

static pigen_source_span raw_span(pigen_source_id source,
	const pigen_token *token)
{
	return (pigen_source_span){source, token->span.start, token->span.end};
}

static int span_equal_text(const pigen_source_manager *sources,
	pigen_source_span left, pigen_source_span right)
{
	const char *left_text;
	const char *right_text;
	size_t left_length;
	size_t right_length;

	left_text = pigen_source_span_text(sources, left, &left_length);
	right_text = pigen_source_span_text(sources, right, &right_length);
	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
}

static pigen_macro_id find_macro(const pigen_expanded_source *result,
	pigen_source_span name)
{
	size_t i = result->macro_count;

	while (i)
	{
		const pigen_macro_definition *macro = &result->macros[--i];
		if (macro->active && span_equal_text(result->sources, macro->name, name))
			return (pigen_macro_id){(uint32_t)i};
	}
	return INVALID_ID(pigen_macro_id);
}

static pigen_macro_id add_macro(pigen_expanded_source *result,
	pigen_macro_definition macro)
{
	pigen_macro_id previous = find_macro(result, macro.name);
	pigen_macro_id id;

	if (previous.index != PIGEN_INVALID_ID)
		result->macros[previous.index].active = 0;
	if (result->macro_count == PIGEN_INVALID_ID)
		pigen_fail("too many macro definitions");
	if (result->macro_count == result->macro_capacity)
	{
		result->macro_capacity = result->macro_capacity ?
			result->macro_capacity * 2 : 16;
		result->macros = pigen_resize(result->macros,
			result->macro_capacity * sizeof(*result->macros));
	}
	id = (pigen_macro_id){(uint32_t)result->macro_count};
	result->macros[result->macro_count++] = macro;
	return id;
}

static void append_macro_parameter(pigen_expanded_source *result,
	pigen_macro_parameter parameter)
{
	if (result->macro_parameter_count == result->macro_parameter_capacity)
	{
		result->macro_parameter_capacity = result->macro_parameter_capacity ?
			result->macro_parameter_capacity * 2 : 16;
		result->macro_parameters = pigen_resize(result->macro_parameters,
			result->macro_parameter_capacity *
				sizeof(*result->macro_parameters));
	}
	result->macro_parameters[result->macro_parameter_count++] = parameter;
}

static int token_text_is(const pigen_expanded_source *source,
	const pigen_expanded_token *token, const char *expected)
{
	size_t length;
	const char *text = pigen_expanded_token_text(source, token, &length);
	return text && length == strlen(expected) &&
		!memcmp(text, expected, length);
}

static int macro_on_stack(const preprocessor *preprocessor, pigen_macro_id macro)
{
	size_t i;
	for (i = 0; i < preprocessor->expansion_depth; i++)
		if (preprocessor->expansion_stack[i].index == macro.index) return 1;
	return 0;
}

static void push_macro(preprocessor *preprocessor, pigen_macro_id macro)
{
	if (preprocessor->expansion_depth == preprocessor->expansion_capacity)
	{
		preprocessor->expansion_capacity = preprocessor->expansion_capacity ?
			preprocessor->expansion_capacity * 2 : 16;
		preprocessor->expansion_stack = pigen_resize(
			preprocessor->expansion_stack,
			preprocessor->expansion_capacity *
			sizeof(*preprocessor->expansion_stack));
	}
	preprocessor->expansion_stack[preprocessor->expansion_depth++] = macro;
}

static int expand_tokens(preprocessor *preprocessor, expansion_output *output,
	const pigen_expanded_token *tokens, size_t count);

static size_t parameter_index(const pigen_expanded_source *result,
	const pigen_macro_definition *macro, const pigen_expanded_token *token)
{
	pigen_source_span spelling;
	size_t i;

	if (token->kind != PIGEN_TOKEN_IDENTIFIER) return SIZE_MAX;
	spelling = pigen_origin_spelling_span(result, token->origin);
	for (i = 0; i < macro->parameter_count; i++)
		if (span_equal_text(result->sources,
			result->macro_parameters[macro->first_parameter + i].name,
			spelling))
			return i;
	return SIZE_MAX;
}

static void append_local_token(pigen_expanded_token **tokens, size_t *count,
	size_t *capacity, pigen_expanded_token token)
{
	if (*count == *capacity)
	{
		*capacity = *capacity ? *capacity * 2 : 16;
		*tokens = pigen_resize(*tokens, *capacity * sizeof(**tokens));
	}
	(*tokens)[(*count)++] = token;
}

static int expand_macro(preprocessor *preprocessor, expansion_output *output,
	pigen_macro_id macro_id, pigen_origin_id invocation,
	const macro_argument *arguments)
{
	pigen_expanded_source *result = preprocessor->result;
	const pigen_macro_definition *macro = &result->macros[macro_id.index];
	pigen_expanded_token *expanded = NULL;
	size_t expanded_count = 0;
	size_t expanded_capacity = 0;
	size_t i;
	int ok;

	if (macro_on_stack(preprocessor, macro_id))
		return fail(preprocessor, pigen_origin_expansion_span(result, invocation),
			"recursive macro expansion");
	for (i = 0; i < macro->replacement_count; i++)
	{
		pigen_expanded_token replacement =
			result->replacement_tokens[macro->first_replacement + i];
		size_t parameter = parameter_index(result, macro, &replacement);

		if (parameter != SIZE_MAX)
		{
			const pigen_macro_parameter *formal =
				&result->macro_parameters[macro->first_parameter + parameter];
			size_t argument_at;
			for (argument_at = 0; argument_at < arguments[parameter].count;
				argument_at++)
			{
				pigen_expanded_token token =
					arguments[parameter].tokens[argument_at];
				token.origin = add_argument_origin(result, invocation,
					formal->origin, token.origin, macro_id);
				append_local_token(&expanded, &expanded_count,
					&expanded_capacity, token);
			}
		}
		else
		{
			replacement.origin = add_replacement_origin(result, invocation,
				replacement.origin, macro_id);
			append_local_token(&expanded, &expanded_count, &expanded_capacity,
				replacement);
		}
	}
	push_macro(preprocessor, macro_id);
	ok = expand_tokens(preprocessor, output, expanded, expanded_count);
	preprocessor->expansion_depth--;
	free(expanded);
	return ok;
}

static int parse_macro_arguments(preprocessor *preprocessor,
	const pigen_expanded_token *tokens, size_t count, size_t open,
	const pigen_macro_definition *macro, pigen_origin_id invocation,
	macro_argument *arguments, size_t *after)
{
	pigen_expanded_source *result = preprocessor->result;
	size_t at = open + 1;
	size_t start = at;
	size_t actual_count = 0;
	size_t parentheses = 1;
	size_t brackets = 0;
	size_t braces = 0;

	for (; at < count; at++)
	{
		if (token_text_is(result, &tokens[at], "(")) parentheses++;
		else if (token_text_is(result, &tokens[at], ")"))
		{
			if (--parentheses) continue;
			if (at != start || macro->parameter_count)
			{
				if (actual_count < macro->parameter_count)
					arguments[actual_count] =
						(macro_argument){tokens + start, at - start};
				actual_count++;
			}
			*after = at + 1;
			if (actual_count != macro->parameter_count)
				return fail(preprocessor,
					pigen_origin_expansion_span(result, invocation),
					"macro invocation has the wrong number of arguments");
			return 1;
		}
		else if (token_text_is(result, &tokens[at], "[")) brackets++;
		else if (token_text_is(result, &tokens[at], "]"))
		{
			if (brackets) brackets--;
		}
		else if (token_text_is(result, &tokens[at], "{")) braces++;
		else if (token_text_is(result, &tokens[at], "}"))
		{
			if (braces) braces--;
		}
		else if (parentheses == 1 && !brackets && !braces &&
			token_text_is(result, &tokens[at], ","))
		{
			if (actual_count < macro->parameter_count)
				arguments[actual_count] =
					(macro_argument){tokens + start, at - start};
			actual_count++;
			start = at + 1;
		}
	}
	return fail(preprocessor, pigen_origin_expansion_span(result, invocation),
		"unterminated macro argument list");
}

static int expand_tokens(preprocessor *preprocessor, expansion_output *output,
	const pigen_expanded_token *tokens, size_t count)
{
	pigen_expanded_source *result = preprocessor->result;
	size_t i;

	for (i = 0; i < count; i++)
	{
		if (token_text_is(result, &tokens[i], "`") && i + 1 < count &&
			tokens[i + 1].kind == PIGEN_TOKEN_IDENTIFIER)
		{
			pigen_source_span name = pigen_origin_spelling_span(result,
				tokens[i + 1].origin);
			pigen_macro_id macro = find_macro(result, name);
			const pigen_macro_definition *definition;
			if (macro.index == PIGEN_INVALID_ID)
				return fail(preprocessor,
					pigen_origin_expansion_span(result, tokens[i + 1].origin),
					"undefined macro or unsupported compiler directive");
			definition = &result->macros[macro.index];
			if (definition->function_like)
			{
				macro_argument *arguments = definition->parameter_count ?
					pigen_resize(NULL, definition->parameter_count *
						sizeof(*arguments)) : NULL;
				size_t after;
				int ok;
				if (i + 2 >= count ||
					!token_text_is(result, &tokens[i + 2], "("))
				{
					free(arguments);
					return fail(preprocessor,
						pigen_origin_expansion_span(result,
							tokens[i + 1].origin),
						"function-like macro requires an argument list");
				}
				ok = parse_macro_arguments(preprocessor, tokens, count, i + 2,
					definition, tokens[i + 1].origin, arguments, &after) &&
					expand_macro(preprocessor, output, macro,
						tokens[i + 1].origin, arguments);
				free(arguments);
				if (!ok) return 0;
				i = after - 1;
			}
			else
			{
				if (!expand_macro(preprocessor, output, macro,
					tokens[i + 1].origin, NULL)) return 0;
				i++;
			}
			continue;
		}
		append_token(output, tokens[i]);
	}
	return 1;
}

static int raw_token_is(const char *source_text,
	const pigen_token *token, const char *text)
{
	return pigen_token_is(source_text, token, text);
}

static int raw_directive_is(const char *source_text,
	const pigen_tokens *raw, size_t at, const char *name)
{
	return at + 1 < raw->count &&
		raw_token_is(source_text, &raw->items[at], "`") &&
		raw->items[at + 1].kind == PIGEN_TOKEN_IDENTIFIER &&
		raw->items[at + 1].span.line == raw->items[at].span.line &&
		raw_token_is(source_text, &raw->items[at + 1], name);
}

static conditional_kind raw_conditional_kind(const char *source_text,
	const pigen_tokens *raw, size_t at)
{
	if (raw_directive_is(source_text, raw, at, "ifdef"))
		return CONDITIONAL_IFDEF;
	if (raw_directive_is(source_text, raw, at, "ifndef"))
		return CONDITIONAL_IFNDEF;
	if (raw_directive_is(source_text, raw, at, "elsif"))
		return CONDITIONAL_ELSIF;
	if (raw_directive_is(source_text, raw, at, "else"))
		return CONDITIONAL_ELSE;
	if (raw_directive_is(source_text, raw, at, "endif"))
		return CONDITIONAL_ENDIF;
	return CONDITIONAL_NONE;
}

static size_t raw_line_after(const pigen_tokens *raw, size_t at)
{
	size_t line = raw->items[at].span.line;
	while (at < raw->count && raw->items[at].kind != PIGEN_TOKEN_EOF &&
		raw->items[at].span.line == line)
		at++;
	return at;
}

static int continuation_status(const char *source_text, size_t source_length,
	const pigen_token *token)
{
	size_t at;

	if (!raw_token_is(source_text, token, "\\")) return 0;
	at = token->span.end;
	if (at < source_length && source_text[at] == '\r') at++;
	return at < source_length && source_text[at] == '\n' ? 1 : -1;
}

static size_t continued_directive_after(const char *source_text,
	size_t source_length, const pigen_tokens *raw, size_t directive)
{
	size_t at = directive;

	for (;;)
	{
		size_t line = raw->items[at].span.line;
		size_t line_after = raw_line_after(raw, at);
		int continued = line_after > at ? continuation_status(source_text,
			source_length, &raw->items[line_after - 1]) : 0;
		if (continued != 1 || line_after >= raw->count ||
			raw->items[line_after].kind == PIGEN_TOKEN_EOF ||
			raw->items[line_after].span.line != line + 1)
			return line_after;
		at = line_after;
	}
}

static void append_logical_index(logical_directive *logical, size_t index)
{
	if (logical->count == logical->capacity)
	{
		logical->capacity = logical->capacity ? logical->capacity * 2 : 16;
		logical->indices = pigen_resize(logical->indices,
			logical->capacity * sizeof(*logical->indices));
	}
	logical->indices[logical->count++] = index;
}

static int collect_logical_directive(preprocessor *preprocessor,
	pigen_source_id source, const char *source_text, size_t source_length,
	const pigen_tokens *raw, size_t directive, logical_directive *logical)
{
	size_t at = directive;

	*logical = (logical_directive){0};
	for (;;)
	{
		size_t line = raw->items[at].span.line;
		size_t line_after = raw_line_after(raw, at);
		size_t content_after = line_after;
		int continued = line_after > at ? continuation_status(source_text,
			source_length, &raw->items[line_after - 1]) : 0;
		size_t i;

		if (line_after > at)
			logical->definition_end = raw->items[line_after - 1].span.end;
		if (continued < 0)
		{
			pigen_source_span span = raw_span(source,
				&raw->items[line_after - 1]);
			free(logical->indices);
			*logical = (logical_directive){0};
			return fail(preprocessor, span,
				"macro continuation backslash must immediately precede newline");
		}
		if (continued) content_after--;
		for (i = at; i < content_after; i++)
			append_logical_index(logical, i);
		if (!continued || line_after >= raw->count ||
			raw->items[line_after].kind == PIGEN_TOKEN_EOF ||
			raw->items[line_after].span.line != line + 1)
		{
			logical->after = line_after;
			return 1;
		}
		at = line_after;
	}
}

static int fail_logical(preprocessor *preprocessor,
	logical_directive *logical, pigen_source_span span, const char *message)
{
	free(logical->indices);
	*logical = (logical_directive){0};
	return fail(preprocessor, span, message);
}

static int conditional_active(const preprocessor *preprocessor)
{
	return !preprocessor->conditional_depth ||
		preprocessor->conditionals[preprocessor->conditional_depth - 1]
			.branch_active;
}

static void push_conditional(preprocessor *preprocessor,
	conditional_frame frame)
{
	if (preprocessor->conditional_depth == preprocessor->conditional_capacity)
	{
		preprocessor->conditional_capacity =
			preprocessor->conditional_capacity ?
			preprocessor->conditional_capacity * 2 : 16;
		preprocessor->conditionals = pigen_resize(preprocessor->conditionals,
			preprocessor->conditional_capacity *
				sizeof(*preprocessor->conditionals));
	}
	preprocessor->conditionals[preprocessor->conditional_depth++] = frame;
}

static int handle_conditional(preprocessor *preprocessor,
	pigen_source_id source, const pigen_tokens *raw, size_t directive,
	conditional_kind kind, size_t *after)
{
	pigen_source_span directive_span = {source,
		raw->items[directive].span.start,
		raw->items[directive + 1].span.end};
	conditional_frame *frame;

	*after = directive + 2;
	if (kind == CONDITIONAL_IFDEF || kind == CONDITIONAL_IFNDEF ||
		kind == CONDITIONAL_ELSIF)
	{
		size_t name_at = directive + 2;
		pigen_source_span name;
		int defined;
		if (name_at >= raw->count ||
			raw->items[name_at].kind != PIGEN_TOKEN_IDENTIFIER)
			return fail(preprocessor, directive_span,
				"conditional macro directive requires a macro name");
		name = raw_span(source, &raw->items[name_at]);
		directive_span.end = name.end;
		*after = name_at + 1;
		defined = find_macro(preprocessor->result, name).index !=
			PIGEN_INVALID_ID;
		if (kind == CONDITIONAL_IFDEF || kind == CONDITIONAL_IFNDEF)
		{
			int selected = kind == CONDITIONAL_IFDEF ? defined : !defined;
			int parent_active = conditional_active(preprocessor);
			push_conditional(preprocessor, (conditional_frame){directive_span,
				parent_active, selected, parent_active && selected, 0});
			return 1;
		}
		if (!preprocessor->conditional_depth)
			return fail(preprocessor, directive_span,
				"`elsif has no matching conditional opener");
		frame = &preprocessor->conditionals[
			preprocessor->conditional_depth - 1];
		if (frame->saw_else)
			return fail(preprocessor, directive_span,
				"`elsif cannot follow `else");
		frame->branch_active = frame->parent_active &&
			!frame->branch_taken && defined;
		frame->branch_taken = frame->branch_taken || defined;
		return 1;
	}
	if (!preprocessor->conditional_depth)
		return fail(preprocessor, directive_span,
			kind == CONDITIONAL_ELSE ?
			"`else has no matching conditional opener" :
			"`endif has no matching conditional opener");
	frame = &preprocessor->conditionals[preprocessor->conditional_depth - 1];
	if (kind == CONDITIONAL_ELSE)
	{
		if (frame->saw_else)
			return fail(preprocessor, directive_span,
				"conditional has more than one `else");
		frame->branch_active = frame->parent_active && !frame->branch_taken;
		frame->branch_taken = 1;
		frame->saw_else = 1;
	}
	else
		preprocessor->conditional_depth--;
	return 1;
}

static int define_macro(preprocessor *preprocessor, pigen_source_id source,
	const char *source_text, const pigen_tokens *raw, size_t directive,
	size_t *after)
{
	const pigen_source_file *file = pigen_source_get(
		preprocessor->result->sources, source);
	logical_directive logical;
	size_t name;
	size_t replacement_at;
	size_t at;
	pigen_macro_definition macro = {0};

	if (!file || !collect_logical_directive(preprocessor, source, source_text,
		file->length, raw, directive, &logical))
		return 0;
	if (logical.count < 3)
		return fail_logical(preprocessor, &logical,
			raw_span(source, &raw->items[directive]),
			"`define requires a macro name");
	name = logical.indices[2];
	if (raw->items[name].kind != PIGEN_TOKEN_IDENTIFIER ||
		raw->items[name].span.line != raw->items[directive].span.line)
		return fail_logical(preprocessor, &logical,
			raw_span(source, &raw->items[directive]),
			"`define requires a macro name");
	macro.name = raw_span(source, &raw->items[name]);
	macro.first_parameter = preprocessor->result->macro_parameter_count;
	replacement_at = 3;
	if (replacement_at < logical.count &&
		raw_token_is(source_text,
			&raw->items[logical.indices[replacement_at]], "(") &&
		raw->items[name].span.end ==
			raw->items[logical.indices[replacement_at]].span.start)
	{
		macro.function_like = 1;
		at = replacement_at + 1;
		if (at < logical.count && raw_token_is(source_text,
			&raw->items[logical.indices[at]], ")"))
			replacement_at = at + 1;
		else
		{
			for (;;)
			{
				size_t i;
				size_t parameter;
				pigen_source_span parameter_name;
				if (at >= logical.count ||
					raw->items[logical.indices[at]].kind !=
						PIGEN_TOKEN_IDENTIFIER)
					return fail_logical(preprocessor, &logical,
						raw_span(source, &raw->items[name]),
						"macro parameter list requires a name");
				parameter = logical.indices[at];
				parameter_name = raw_span(source, &raw->items[parameter]);
				for (i = macro.first_parameter;
					i < preprocessor->result->macro_parameter_count; i++)
					if (span_equal_text(preprocessor->result->sources,
						preprocessor->result->macro_parameters[i].name,
						parameter_name))
						return fail_logical(preprocessor, &logical,
							parameter_name,
							"duplicate macro parameter name");
				append_macro_parameter(preprocessor->result,
					(pigen_macro_parameter){parameter_name,
						add_source_origin(preprocessor->result,
							parameter_name)});
				macro.parameter_count++;
				at++;
				if (at < logical.count && raw_token_is(source_text,
					&raw->items[logical.indices[at]], ")"))
				{
					replacement_at = at + 1;
					break;
				}
				if (at >= logical.count || !raw_token_is(source_text,
					&raw->items[logical.indices[at]], ","))
					return fail_logical(preprocessor, &logical,
						raw_span(source, &raw->items[name]),
						"unterminated macro parameter list");
				at++;
			}
		}
	}
	macro.definition = (pigen_source_span){source,
		raw->items[directive].span.start, logical.definition_end};
	macro.first_replacement = preprocessor->result->replacement_token_count;
	macro.replacement_count = logical.count - replacement_at;
	macro.active = 1;
	for (; replacement_at < logical.count; replacement_at++)
	{
		size_t replacement = logical.indices[replacement_at];
		pigen_expanded_token token = {raw->items[replacement].kind,
			add_source_origin(preprocessor->result,
				raw_span(source, &raw->items[replacement]))};
		append_replacement_token(preprocessor->result, token);
	}
	add_macro(preprocessor->result, macro);
	*after = logical.after;
	free(logical.indices);
	return 1;
}

static int undefine_macro(preprocessor *preprocessor, pigen_source_id source,
	const pigen_tokens *raw, size_t directive, size_t *after)
{
	size_t name = directive + 2;
	pigen_source_span name_span;
	pigen_macro_id macro;

	if (name >= raw->count || raw->items[name].kind != PIGEN_TOKEN_IDENTIFIER ||
		raw->items[name].span.line != raw->items[directive].span.line)
		return fail(preprocessor, raw_span(source, &raw->items[directive]),
			"`undef requires a macro name");
	name_span = raw_span(source, &raw->items[name]);
	macro = find_macro(preprocessor->result, name_span);
	if (macro.index != PIGEN_INVALID_ID)
		preprocessor->result->macros[macro.index].active = 0;
	*after = name + 1;
	while (*after < raw->count &&
		raw->items[*after].kind != PIGEN_TOKEN_EOF &&
		raw->items[*after].span.line == raw->items[directive].span.line)
		(*after)++;
	return 1;
}

static int source_on_stack(const preprocessor *preprocessor,
	pigen_source_id source)
{
	size_t i;
	for (i = 0; i < preprocessor->include_depth; i++)
		if (preprocessor->include_stack[i].index == source.index) return 1;
	return 0;
}

static void push_source(preprocessor *preprocessor, pigen_source_id source)
{
	if (preprocessor->include_depth == preprocessor->include_capacity)
	{
		preprocessor->include_capacity = preprocessor->include_capacity ?
			preprocessor->include_capacity * 2 : 16;
		preprocessor->include_stack = pigen_resize(preprocessor->include_stack,
			preprocessor->include_capacity *
			sizeof(*preprocessor->include_stack));
	}
	preprocessor->include_stack[preprocessor->include_depth++] = source;
}

static int preprocess_source(preprocessor *preprocessor,
	pigen_source_id source, int emit_eof);

static int decode_include_path(preprocessor *preprocessor,
	const pigen_expanded_token *tokens, size_t count, pigen_source_span span,
	char **path, size_t *path_length)
{
	pigen_expanded_source *result = preprocessor->result;
	size_t total = 0;
	size_t i;

	*path = NULL;
	*path_length = 0;
	if (count == 1 && tokens[0].kind == PIGEN_TOKEN_STRING)
	{
		const char *text = pigen_expanded_token_text(result, &tokens[0],
			&total);
		if (!text || total < 2 || text[0] != '"' || text[total - 1] != '"')
			return fail(preprocessor, span,
				"`include expansion did not produce a valid path");
		*path_length = total - 2;
		*path = pigen_resize(NULL, *path_length + 1);
		memcpy(*path, text + 1, *path_length);
		(*path)[*path_length] = '\0';
		return 1;
	}
	if (count < 3 || !token_text_is(result, &tokens[0], "<") ||
		!token_text_is(result, &tokens[count - 1], ">"))
		return fail(preprocessor, span,
			"`include expansion must produce one quoted or angle-bracket path");
	for (i = 1; i + 1 < count; i++)
	{
		size_t length;
		if (!pigen_expanded_token_text(result, &tokens[i], &length) ||
			total > SIZE_MAX - length)
			return fail(preprocessor, span, "invalid expanded include path");
		total += length;
	}
	if (!total)
		return fail(preprocessor, span, "`include path cannot be empty");
	if (total == SIZE_MAX)
		return fail(preprocessor, span, "expanded include path is too long");
	*path = pigen_resize(NULL, total + 1);
	*path_length = total;
	total = 0;
	for (i = 1; i + 1 < count; i++)
	{
		size_t length;
		const char *text = pigen_expanded_token_text(result, &tokens[i],
			&length);
		memcpy(*path + total, text, length);
		total += length;
	}
	(*path)[total] = '\0';
	return 1;
}

static int include_source(preprocessor *preprocessor, pigen_source_id source,
	const pigen_tokens *raw, size_t directive, size_t *after)
{
	size_t path_at = directive + 2;
	size_t line_after = raw_line_after(raw, directive);
	size_t input_count;
	pigen_expanded_token *input;
	pigen_expanded_token *expanded = NULL;
	size_t expanded_count = 0;
	size_t expanded_capacity = 0;
	expansion_output output = {&expanded, &expanded_count, &expanded_capacity};
	pigen_source_span operand_span;
	char *path;
	size_t path_length;
	pigen_source_id included = INVALID_ID(pigen_source_id);
	const char *loader_error = NULL;
	size_t i;

	if (path_at >= line_after)
		return fail(preprocessor, raw_span(source, &raw->items[directive]),
			"`include requires a path expression");
	operand_span = (pigen_source_span){source, raw->items[path_at].span.start,
		raw->items[line_after - 1].span.end};
	input_count = line_after - path_at;
	input = pigen_resize(NULL, input_count * sizeof(*input));
	for (i = 0; i < input_count; i++)
		input[i] = (pigen_expanded_token){raw->items[path_at + i].kind,
			add_source_origin(preprocessor->result,
				raw_span(source, &raw->items[path_at + i]))};
	if (!expand_tokens(preprocessor, &output, input, input_count))
	{
		free(input);
		free(expanded);
		return 0;
	}
	free(input);
	if (!decode_include_path(preprocessor, expanded, expanded_count,
		operand_span, &path, &path_length))
	{
		free(expanded);
		return 0;
	}
	free(expanded);
	if (!preprocessor->provider || !preprocessor->provider->load)
	{
		free(path);
		return fail(preprocessor, operand_span,
			"`include requires a source provider");
	}
	if (!preprocessor->provider->load(preprocessor->provider->context, source,
		path, path_length, &included, &loader_error))
	{
		free(path);
		return fail(preprocessor, operand_span,
			loader_error ? loader_error : "cannot load included source");
	}
	free(path);
	if (!pigen_source_get(preprocessor->result->sources, included))
		return fail(preprocessor, operand_span,
			"source provider returned an unknown SourceId");
	if (!record_include(preprocessor, (pigen_source_include){
		(pigen_source_span){source, raw->items[directive].span.start,
			operand_span.end}, operand_span, included}))
		return 0;
	if (source_on_stack(preprocessor, included))
		return fail(preprocessor, operand_span,
			"recursive source inclusion");
	if (!preprocess_source(preprocessor, included, 0)) return 0;
	*after = line_after;
	return 1;
}

static int preprocess_source(preprocessor *preprocessor,
	pigen_source_id source, int emit_eof)
{
	const pigen_source_file *file = pigen_source_get(
		preprocessor->result->sources, source);
	const char *source_text;
	pigen_tokens raw = {0};
	size_t i;
	int ok = 1;

	if (!file) return 0;
	source_text = file->text;
	push_source(preprocessor, source);
	if (!source_tokens(preprocessor, source, file, &raw))
	{
		preprocessor->include_depth--;
		return 0;
	}
	for (i = 0; i < raw.count; )
	{
		conditional_kind conditional = raw_conditional_kind(source_text,
			&raw, i);
		if (conditional != CONDITIONAL_NONE)
		{
			if (!handle_conditional(preprocessor, source, &raw,
				i, conditional, &i))
			{
				ok = 0;
				break;
			}
			continue;
		}
		if (!conditional_active(preprocessor))
		{
			if (raw_directive_is(source_text, &raw, i, "define"))
				i = continued_directive_after(source_text, file->length,
					&raw, i);
			else if (raw_directive_is(source_text, &raw, i, "include") ||
				raw_directive_is(source_text, &raw, i, "undef"))
				i = raw_line_after(&raw, i);
			else
				i++;
			continue;
		}
		if (raw_directive_is(source_text, &raw, i, "include"))
		{
			if (!include_source(preprocessor, source, &raw, i, &i))
			{
				ok = 0;
				break;
			}
			continue;
		}
		if (raw_directive_is(source_text, &raw, i, "define"))
		{
			if (!define_macro(preprocessor, source, source_text, &raw, i, &i))
			{
				ok = 0;
				break;
			}
			continue;
		}
		if (raw_directive_is(source_text, &raw, i, "undef"))
		{
			if (!undefine_macro(preprocessor, source, &raw, i, &i))
			{
				ok = 0;
				break;
			}
			continue;
		}
		{
			size_t segment_end = i;
			size_t segment_count;
			pigen_expanded_token *segment;
			expansion_output output = primary_output(preprocessor->result);
			size_t at;

			while (segment_end < raw.count &&
				(emit_eof || raw.items[segment_end].kind != PIGEN_TOKEN_EOF) &&
				raw_conditional_kind(source_text, &raw, segment_end) ==
					CONDITIONAL_NONE &&
				!raw_directive_is(source_text, &raw, segment_end, "include") &&
				!raw_directive_is(source_text, &raw, segment_end, "define") &&
				!raw_directive_is(source_text, &raw, segment_end, "undef"))
				segment_end++;
			if (segment_end == i)
			{
				i++;
				continue;
			}
			segment_count = segment_end - i;
			segment = pigen_resize(NULL, segment_count * sizeof(*segment));
			for (at = 0; at < segment_count; at++)
				segment[at] = (pigen_expanded_token){raw.items[i + at].kind,
					add_source_origin(preprocessor->result,
						raw_span(source, &raw.items[i + at]))};
			if (!expand_tokens(preprocessor, &output, segment, segment_count))
			{
				free(segment);
				ok = 0;
				break;
			}
			free(segment);
			i = segment_end;
		}
	}
	preprocessor->include_depth--;
	return ok;
}

int pigen_preprocess(const pigen_source_manager *sources,
	pigen_source_id source, const pigen_source_provider *provider,
	pigen_preprocess_result *result, pigen_preprocess_error *error)
{
	preprocessor preprocessor = {0};
	int ok;

	memset(result, 0, sizeof(*result));
	if (error) *error = (pigen_preprocess_error){invalid_span(), NULL};
	if (!pigen_source_get(sources, source)) return 0;
	result->written.sources = sources;
	result->written.root_source = source;
	result->expanded.sources = sources;
	result->expanded.root_source = source;
	preprocessor.result = &result->expanded;
	preprocessor.written = &result->written;
	preprocessor.error = error;
	preprocessor.provider = provider;
	ok = preprocess_source(&preprocessor, source, 1);
	if (ok && preprocessor.conditional_depth)
		ok = fail(&preprocessor, preprocessor.conditionals[
			preprocessor.conditional_depth - 1].opening,
			"unterminated conditional compilation block");
	free(preprocessor.expansion_stack);
	free(preprocessor.include_stack);
	free(preprocessor.conditionals);
	if (ok) return 1;
	pigen_free_preprocess_result(result);
	return 0;
}

const pigen_expanded_token *pigen_expanded_token_get(
	const pigen_expanded_source *source, pigen_token_id token)
{
	if (!source || token.index == PIGEN_INVALID_ID ||
		token.index >= source->token_count) return NULL;
	return &source->tokens[token.index];
}

const pigen_token_origin *pigen_origin_get(
	const pigen_expanded_source *source, pigen_origin_id origin)
{
	if (!source || origin.index == PIGEN_INVALID_ID ||
		origin.index >= source->origin_count) return NULL;
	return &source->origins[origin.index];
}

pigen_source_span pigen_origin_spelling_span(
	const pigen_expanded_source *source, pigen_origin_id origin)
{
	const pigen_token_origin *known;
	size_t remaining = source ? source->origin_count : 0;

	while (remaining-- && (known = pigen_origin_get(source, origin)))
	{
		if (known->kind == PIGEN_ORIGIN_SOURCE) return known->as.source;
		origin = known->kind == PIGEN_ORIGIN_MACRO_REPLACEMENT ?
			known->as.macro_replacement.replacement :
			known->as.macro_argument.argument;
	}
	return invalid_span();
}

pigen_source_span pigen_origin_expansion_span(
	const pigen_expanded_source *source, pigen_origin_id origin)
{
	const pigen_token_origin *known;
	size_t remaining = source ? source->origin_count : 0;

	while (remaining-- && (known = pigen_origin_get(source, origin)))
	{
		if (known->kind == PIGEN_ORIGIN_SOURCE) return known->as.source;
		origin = known->kind == PIGEN_ORIGIN_MACRO_REPLACEMENT ?
			known->as.macro_replacement.invocation :
			known->as.macro_argument.invocation;
	}
	return invalid_span();
}

const char *pigen_expanded_token_text(
	const pigen_expanded_source *source,
	const pigen_expanded_token *token, size_t *length)
{
	pigen_source_span spelling;
	if (length) *length = 0;
	if (!source || !token) return NULL;
	spelling = pigen_origin_spelling_span(source, token->origin);
	return pigen_source_span_text(source->sources, spelling, length);
}

void pigen_free_preprocess_result(pigen_preprocess_result *result)
{
	size_t i;
	pigen_expanded_source *expanded;
	if (!result) return;
	for (i = 0; i < result->written.file_count; i++)
		free(result->written.files[i].tokens);
	free(result->written.files);
	free(result->written.includes);
	expanded = &result->expanded;
	free(expanded->tokens);
	free(expanded->origins);
	free(expanded->macros);
	free(expanded->macro_parameters);
	free(expanded->replacement_tokens);
	*result = (pigen_preprocess_result){0};
}
