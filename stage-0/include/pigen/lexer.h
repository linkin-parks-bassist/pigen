#ifndef PIGEN_LEXER_H
#define PIGEN_LEXER_H

#include "pigen/model.h"

typedef enum {
	PIGEN_TOKEN_EOF,
	PIGEN_TOKEN_IDENTIFIER,
	PIGEN_TOKEN_NUMBER,
	PIGEN_TOKEN_STRING,
	PIGEN_TOKEN_SYMBOL
} pigen_token_kind;

typedef struct {
	pigen_token_kind kind;
	pigen_span span;
} pigen_token;

typedef struct {
	pigen_token *items;
	size_t count;
	size_t capacity;
} pigen_tokens;

/* Lexes Pigen-owned source once while preserving all source positions. */
void pigen_lex_source(const char *source, size_t length, pigen_tokens *tokens);
void pigen_free_tokens(pigen_tokens *tokens);
int pigen_token_is(const char *source, const pigen_token *token, const char *text);

#endif
