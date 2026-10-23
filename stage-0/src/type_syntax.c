/* Shared structural type syntax for declarations and casts. */
#include <stdlib.h>
#include <string.h>

#include "pigen/expression.h"
#include "pigen/lexer.h"
#include "pigen/type_syntax.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static const pigen_expanded_token *token_at(const pigen_expanded_source *source,
	size_t at)
{
	return pigen_expanded_token_get(source,
		(pigen_token_id){(uint32_t)at});
}

static int token_is(const pigen_expanded_source *source, size_t at,
	const char *text)
{
	const pigen_expanded_token *token = token_at(source, at);
	const char *known;
	size_t length;

	if (!token) return 0;
	known = pigen_expanded_token_text(source, token, &length);
	return known && length == strlen(text) && !memcmp(known, text, length);
}

static int fail(const pigen_expanded_source *source, size_t at,
	pigen_syntax_error *error, const char *message)
{
	const pigen_expanded_token *token = token_at(source, at);

	if (error)
	{
		error->origin = token ? token->origin : INVALID_ID(pigen_origin_id);
		error->span = token ? pigen_origin_expansion_span(source, token->origin) :
			(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
		error->message = message;
	}
	return 0;
}

static size_t matching_bracket(const pigen_expanded_source *source,
	size_t open, size_t limit)
{
	size_t depth = 0;
	size_t at;

	for (at = open; at < limit; at++)
	{
		if (token_is(source, at, "[")) depth++;
		else if (token_is(source, at, "]") && !--depth) return at;
	}
	return limit;
}

static size_t top_level_colon(const pigen_expanded_source *source,
	size_t first, size_t after)
{
	size_t depth = 0;
	size_t at;

	for (at = first; at < after; at++)
	{
		if (token_is(source, at, "(") || token_is(source, at, "[") ||
			token_is(source, at, "{")) depth++;
		else if (token_is(source, at, ")") || token_is(source, at, "]") ||
			token_is(source, at, "}")) depth--;
		else if (!depth && token_is(source, at, ":")) return at;
	}
	return after;
}

static int append_type(pigen_syntax_type_arena *arena,
	pigen_syntax_type type, const pigen_syntax_type_argument *arguments,
	size_t argument_count, pigen_syntax_type_id *result)
{
	if (!arena || !result || arena->node_count >= PIGEN_INVALID_ID ||
		(argument_count && !arguments) ||
		argument_count > SIZE_MAX - arena->argument_count)
		return 0;
	if (arena->node_count == arena->node_capacity)
	{
		arena->node_capacity = arena->node_capacity ?
			arena->node_capacity * 2 : 16;
		arena->nodes = pigen_resize(arena->nodes,
			arena->node_capacity * sizeof(*arena->nodes));
	}
	if (arena->argument_count + argument_count > arena->argument_capacity)
	{
		size_t capacity = arena->argument_capacity ?
			arena->argument_capacity * 2 : 16;
		while (capacity < arena->argument_count + argument_count) capacity *= 2;
		arena->arguments = pigen_resize(arena->arguments,
			capacity * sizeof(*arena->arguments));
		arena->argument_capacity = capacity;
	}
	type.first_argument = arena->argument_count;
	type.argument_count = argument_count;
	if (argument_count)
		memcpy(arena->arguments + arena->argument_count, arguments,
			argument_count * sizeof(*arguments));
	arena->argument_count += argument_count;
	*result = (pigen_syntax_type_id){(uint32_t)arena->node_count};
	arena->nodes[arena->node_count++] = type;
	return 1;
}

int pigen_parse_type_prefix(const pigen_expanded_source *source,
	size_t first, size_t limit, pigen_syntax_expr_arena *expressions,
	pigen_syntax_type_arena *types, pigen_syntax_type_id *type,
	size_t *after, pigen_syntax_error *error)
{
	pigen_syntax_type parsed = {0};
	pigen_syntax_type_argument *arguments = NULL;
	size_t argument_count = 0;
	size_t argument_capacity = 0;
	size_t original_expression_count;
	size_t original_child_count;
	size_t at = first;
	int ok = 0;

	if (!source || !expressions || !types || !type || !after || first > limit ||
		limit > source->token_count)
		return 0;
	original_expression_count = expressions->node_count;
	original_child_count = expressions->child_count;
	parsed.base = INVALID_ID(pigen_token_id);
	parsed.signedness = PIGEN_SYNTAX_SIGN_IMPLICIT;
	if (token_is(source, at, "signed"))
	{
		parsed.signedness = PIGEN_SYNTAX_SIGN_SIGNED;
		at++;
	}
	else if (token_is(source, at, "unsigned"))
	{
		parsed.signedness = PIGEN_SYNTAX_SIGN_UNSIGNED;
		at++;
	}
	if (at < limit && token_at(source, at)->kind == PIGEN_TOKEN_IDENTIFIER &&
		!token_is(source, at, "signed") && !token_is(source, at, "unsigned"))
		parsed.base = (pigen_token_id){(uint32_t)at++};
	if (token_is(source, at, "signed") || token_is(source, at, "unsigned"))
	{
		if (parsed.signedness != PIGEN_SYNTAX_SIGN_IMPLICIT)
			goto done;
		parsed.signedness = token_is(source, at, "signed") ?
			PIGEN_SYNTAX_SIGN_SIGNED : PIGEN_SYNTAX_SIGN_UNSIGNED;
		at++;
	}
	while (at < limit && token_is(source, at, "["))
	{
		pigen_syntax_type_argument argument = {0};
		size_t close = matching_bracket(source, at, limit);
		size_t colon;

		if (close == limit)
		{
			fail(source, at, error, "unterminated type argument");
			goto done;
		}
		colon = top_level_colon(source, at + 1, close);
		if (at + 1 == close || colon == at + 1 || colon + 1 == close)
		{
			fail(source, at, error, "type argument requires an expression");
			goto done;
		}
		argument.location = pigen_syntax_location_from_extent(source, at,
			close + 1);
		if (colon == close)
		{
			argument.kind = PIGEN_SYNTAX_TYPE_COUNT;
			if (!pigen_parse_expression(source, at + 1, close, expressions,
				types, &argument.as.count, error)) goto done;
		}
		else
		{
			argument.kind = PIGEN_SYNTAX_TYPE_RANGE;
			if (!pigen_parse_expression(source, at + 1, colon, expressions,
				types, &argument.as.range.left, error) ||
				!pigen_parse_expression(source, colon + 1, close, expressions,
					types, &argument.as.range.right, error)) goto done;
		}
		if (argument_count == argument_capacity)
		{
			argument_capacity = argument_capacity ? argument_capacity * 2 : 4;
			arguments = pigen_resize(arguments,
				argument_capacity * sizeof(*arguments));
		}
		arguments[argument_count++] = argument;
		at = close + 1;
	}
	if (parsed.base.index == PIGEN_INVALID_ID &&
		parsed.signedness == PIGEN_SYNTAX_SIGN_IMPLICIT && !argument_count &&
		first != limit)
	{
		fail(source, first, error, "type requires a name");
		goto done;
	}
	parsed.location = pigen_syntax_location_from_extent(source, first, at);
	if (!append_type(types, parsed, arguments, argument_count, type)) goto done;
	*after = at;
	ok = 1;
done:
	if (!ok)
	{
		expressions->node_count = original_expression_count;
		expressions->child_count = original_child_count;
	}
	free(arguments);
	return ok;
}

pigen_syntax_type_ownership pigen_syntax_type_declaration_ownership(
	const pigen_expanded_source *source, const pigen_syntax_type_arena *arena,
	pigen_syntax_type_id type, pigen_syntax_type_name_query structured_name,
	void *structured_name_context)
{
	const pigen_syntax_type *known = pigen_syntax_type_get(arena, type);
	const pigen_syntax_type_argument *arguments;
	size_t i;

	if (!source || !known) return PIGEN_SYNTAX_TYPE_UNOWNED;
	arguments = pigen_syntax_type_arguments(arena, known->first_argument,
		known->argument_count);
	if (known->argument_count && !arguments)
		return PIGEN_SYNTAX_TYPE_UNOWNED;
	for (i = 0; i < known->argument_count; i++)
		if (arguments[i].kind == PIGEN_SYNTAX_TYPE_COUNT)
			return PIGEN_SYNTAX_TYPE_PIGEN;
	if (known->base.index != PIGEN_INVALID_ID && structured_name &&
		structured_name(structured_name_context, known->base))
		return PIGEN_SYNTAX_TYPE_STRUCTURED;
	if (known->base.index != PIGEN_INVALID_ID &&
		(token_is(source, known->base.index, "logic") ||
			token_is(source, known->base.index, "bit")))
		return PIGEN_SYNTAX_TYPE_SYSTEMVERILOG;
	if (known->base.index == PIGEN_INVALID_ID &&
		(known->signedness != PIGEN_SYNTAX_SIGN_IMPLICIT ||
			known->argument_count))
		return PIGEN_SYNTAX_TYPE_SYSTEMVERILOG;
	return PIGEN_SYNTAX_TYPE_UNOWNED;
}

const pigen_syntax_type *pigen_syntax_type_get(
	const pigen_syntax_type_arena *arena, pigen_syntax_type_id type)
{
	if (!arena || type.index == PIGEN_INVALID_ID || type.index >= arena->node_count)
		return NULL;
	return &arena->nodes[type.index];
}

const pigen_syntax_type_argument *pigen_syntax_type_arguments(
	const pigen_syntax_type_arena *arena, size_t first, size_t count)
{
	if (!arena || first > arena->argument_count ||
		count > arena->argument_count - first)
		return NULL;
	return arena->arguments + first;
}

void pigen_free_syntax_type_arena(pigen_syntax_type_arena *arena)
{
	if (!arena) return;
	free(arena->nodes);
	free(arena->arguments);
	*arena = (pigen_syntax_type_arena){0};
}
