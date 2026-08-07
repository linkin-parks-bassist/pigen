/* Source-spanned lexer shared by all later frontend passes. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/lexer.h"
#include "pigen/util.h"

static void add_token(pigen_tokens *tokens, pigen_token_kind kind, size_t start,
			      size_t end, size_t line, size_t column)
{
	if (tokens->count == tokens->capacity)
	{
		tokens->capacity = tokens->capacity ? tokens->capacity * 2 : 128;
		tokens->items = pigen_resize(tokens->items, tokens->capacity * sizeof(*tokens->items));
	}
	tokens->items[tokens->count++] = (pigen_token){ kind, { start, end, line, column } };
}

static void advance(const char *source, size_t *cursor, size_t *line, size_t *column)
{
	if (source[*cursor] == '\n')
	{
		(*line)++;
		*column = 1;
	}
	else
		(*column)++;
	(*cursor)++;
}

void pigen_lex_source(const char *source, size_t length, pigen_tokens *tokens)
{
	size_t cursor = 0;
	size_t line = 1;
	size_t column = 1;

	while (cursor < length)
	{
		size_t start = cursor;
		size_t start_line = line;
		size_t start_column = column;
		pigen_token_kind kind;

		if (isspace((unsigned char)source[cursor]))
		{
			advance(source, &cursor, &line, &column);
			continue;
		}

		if (cursor + 1 < length && source[cursor] == '/' && source[cursor + 1] == '/')
		{
			while (cursor < length && source[cursor] != '\n')
				advance(source, &cursor, &line, &column);
			continue;
		}

		if (cursor + 1 < length && source[cursor] == '/' && source[cursor + 1] == '*')
		{
			advance(source, &cursor, &line, &column);
			advance(source, &cursor, &line, &column);
			while (cursor + 1 < length && !(source[cursor] == '*' && source[cursor + 1] == '/'))
				advance(source, &cursor, &line, &column);
			if (cursor + 1 == length)
				pigen_fail("unterminated block comment");
			advance(source, &cursor, &line, &column);
			advance(source, &cursor, &line, &column);
			continue;
		}

		if (pigen_is_identifier_char((unsigned char)source[cursor]))
		{
			kind = PIGEN_TOKEN_IDENTIFIER;
			while (cursor < length && pigen_is_identifier_char((unsigned char)source[cursor]))
				advance(source, &cursor, &line, &column);
		}
		else if (isdigit((unsigned char)source[cursor]))
		{
			kind = PIGEN_TOKEN_NUMBER;
			while (cursor < length && (pigen_is_identifier_char((unsigned char)source[cursor]) || source[cursor] == '\''))
				advance(source, &cursor, &line, &column);
		}
		else if (source[cursor] == '"')
		{
			kind = PIGEN_TOKEN_STRING;
			advance(source, &cursor, &line, &column);
			while (cursor < length && source[cursor] != '"')
			{
				if (source[cursor] == '\\' && cursor + 1 < length)
					advance(source, &cursor, &line, &column);
				advance(source, &cursor, &line, &column);
			}
			if (cursor == length)
				pigen_fail("unterminated string literal");
			advance(source, &cursor, &line, &column);
		}
		else
		{
			kind = PIGEN_TOKEN_SYMBOL;
			advance(source, &cursor, &line, &column);
			if (cursor < length && ((source[start] == '<' && source[cursor] == '=') ||
				(source[start] == '=' && source[cursor] == '=') ||
				(source[start] == '!' && source[cursor] == '=') ||
				(source[start] == '-' && source[cursor] == '>') ||
				(source[start] == ':' && source[cursor] == ':')))
				advance(source, &cursor, &line, &column);
		}

		add_token(tokens, kind, start, cursor, start_line, start_column);
	}

	add_token(tokens, PIGEN_TOKEN_EOF, length, length, line, column);
}

void pigen_free_tokens(pigen_tokens *tokens)
{
	free(tokens->items);
	*tokens = (pigen_tokens){0};
}

int pigen_token_is(const char *source, const pigen_token *token, const char *text)
{
	size_t length = strlen(text);
	return token->span.end - token->span.start == length &&
		!memcmp(source + token->span.start, text, length);
}
