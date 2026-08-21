/* Explicit atomic transfer-block syntax lowering. */
#include <stdlib.h>

#include "pigen/lexer.h"
#include "pigen/transfer.h"
#include "pigen/util.h"

typedef struct {
	const char *left;
	size_t left_length;
	const char *right;
	size_t right_length;
} transfer_member;

/* Transfer blocks are atomic groups.  A member may itself use a co-slice;
 * flatten that destination into the group's outer co-slice so the ordinary
 * transfer parser still sees each signal independently. */
static void append_flattened_destination(pigen_string *output, const char *text, size_t length)
{
	const char *start = pigen_skip_spaces(text, text + length);
	const char *end = pigen_trim_end(start, text + length);
	const char *cursor;
	int depth = 0;

	if (start < end && *start == '{' && end[-1] == '}')
	{
		for (cursor = start; cursor < end; cursor++)
		{
			if (*cursor == '{') depth++;
			else if (*cursor == '}' && --depth == 0) break;
		}
		if (cursor == end - 1)
		{
			pigen_append_range(output, start + 1, (size_t)(end - start - 2));
			return;
		}
	}
	pigen_append_range(output, text, length);
}

static int token_is(const char *source, const pigen_tokens *tokens, size_t at,
	const char *text)
{
	return at < tokens->count && pigen_token_is(source, &tokens->items[at], text);
}

static void add_member(transfer_member **members, size_t *count, size_t *capacity,
	const char *source, const pigen_tokens *tokens, size_t first, size_t semicolon)
{
	size_t at;
	size_t operator_at = (size_t)-1;
	int parentheses = 0, brackets = 0, braces = 0;
	const char *left;
	const char *left_end;
	const char *right;
	const char *right_end;

	if (first == semicolon)
	{
		pigen_set_diagnostic_position(source + tokens->items[semicolon].span.start);
		pigen_fail("empty member in transfer block");
	}
	for (at = first; at < semicolon; at++)
	{
		if (token_is(source, tokens, at, "(")) parentheses++;
		else if (token_is(source, tokens, at, ")")) parentheses--;
		else if (token_is(source, tokens, at, "[")) brackets++;
		else if (token_is(source, tokens, at, "]")) brackets--;
		else if (token_is(source, tokens, at, "{")) braces++;
		else if (token_is(source, tokens, at, "}")) braces--;
		else if (!parentheses && !brackets && !braces &&
			token_is(source, tokens, at, "<="))
		{
			if (operator_at != (size_t)-1)
			{
				pigen_set_diagnostic_position(source + tokens->items[at].span.start);
				pigen_fail("transfer block member requires exactly one top-level <=");
			}
			operator_at = at;
		}
	}
	if (operator_at == (size_t)-1)
	{
		pigen_set_diagnostic_position(source + tokens->items[first].span.start);
		pigen_fail("transfer block members must be assignments using <=");
	}
	if (operator_at == first || operator_at + 1 == semicolon)
	{
		pigen_set_diagnostic_position(source + tokens->items[operator_at].span.start);
		pigen_fail("transfer block member requires a destination and source");
	}

	left = source + tokens->items[first].span.start;
	left_end = source + tokens->items[operator_at].span.start;
	right = source + tokens->items[operator_at + 1].span.start;
	right_end = source + tokens->items[semicolon].span.start;
	left = pigen_skip_spaces(left, left_end);
	left_end = pigen_trim_end(left, left_end);
	right = pigen_skip_spaces(right, right_end);
	right_end = pigen_trim_end(right, right_end);
	if (*count == *capacity)
	{
		*capacity = *capacity ? *capacity * 2 : 4;
		*members = pigen_resize(*members, *capacity * sizeof(**members));
	}
	(*members)[(*count)++] = (transfer_member){left,
		(size_t)(left_end - left), right, (size_t)(right_end - right)};
}

static size_t lower_one(pigen_string *output, const char *source,
	const pigen_tokens *tokens, size_t transfer_at)
{
	size_t at = transfer_at + 2;
	size_t member_first = at;
	size_t member_count = 0, member_capacity = 0;
	transfer_member *members = NULL;
	int parentheses = 0, brackets = 0, braces = 0;

	for (;; at++)
	{
		if (at >= tokens->count || tokens->items[at].kind == PIGEN_TOKEN_EOF)
		{
			pigen_set_diagnostic_position(source + tokens->items[transfer_at].span.start);
			pigen_fail("unterminated transfer block");
		}
		if (!parentheses && !brackets && !braces && token_is(source, tokens, at, "end"))
			break;
		if (token_is(source, tokens, at, "(")) parentheses++;
		else if (token_is(source, tokens, at, ")")) parentheses--;
		else if (token_is(source, tokens, at, "[")) brackets++;
		else if (token_is(source, tokens, at, "]")) brackets--;
		else if (token_is(source, tokens, at, "{")) braces++;
		else if (token_is(source, tokens, at, "}")) braces--;
		else if (!parentheses && !brackets && !braces && token_is(source, tokens, at, ";"))
		{
			add_member(&members, &member_count, &member_capacity, source, tokens,
				member_first, at);
			member_first = at + 1;
		}
	}
	if (member_first != at)
	{
		pigen_set_diagnostic_position(source + tokens->items[member_first].span.start);
		pigen_fail("transfer block member requires a terminating semicolon");
	}
	if (!member_count)
	{
		pigen_set_diagnostic_position(source + tokens->items[transfer_at].span.start);
		pigen_fail("transfer block requires at least one member");
	}

	pigen_append(output, "{");
	for (size_t i = 0; i < member_count; i++)
	{
		if (i) pigen_append(output, ", ");
		append_flattened_destination(output, members[i].left, members[i].left_length);
	}
	pigen_append(output, "} <= {");
	for (size_t i = 0; i < member_count; i++)
	{
		if (i) pigen_append(output, ", ");
		pigen_append_range(output, members[i].right, members[i].right_length);
	}
	pigen_append(output, "};");
	free(members);
	return at;
}

char *pigen_lower_transfer_blocks(const char *source, size_t length,
	size_t *output_length)
{
	pigen_tokens tokens = {0};
	pigen_string output = {0};
	size_t copied = 0;
	size_t at;

	pigen_lex_source(source, length, &tokens);
	for (at = 0; at + 1 < tokens.count; at++)
	{
		size_t end_at;
		if (!token_is(source, &tokens, at, "transfer") ||
			!token_is(source, &tokens, at + 1, "begin"))
			continue;
		pigen_append_range(&output, source + copied,
			tokens.items[at].span.start - copied);
		end_at = lower_one(&output, source, &tokens, at);
		copied = tokens.items[end_at].span.end;
		at = end_at;
	}
	pigen_append_range(&output, source + copied, length - copied);
	pigen_free_tokens(&tokens);
	*output_length = output.length;
	return output.data ? output.data : pigen_copy_range(source, length);
}
