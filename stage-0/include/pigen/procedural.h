#ifndef PIGEN_PROCEDURAL_H
#define PIGEN_PROCEDURAL_H

#include <stddef.h>
#include "pigen/model.h"

typedef struct { pigen_span span; const char *start; const char *end; char *guard; char *domain; } pigen_procedural_statement;
typedef struct { const char *start; const char *end; char *guard; char *domain; } pigen_conditional_transfer;
typedef struct {
	pigen_procedural_statement *items;
	size_t count;
	size_t capacity;
	pigen_conditional_transfer *conditional_transfers;
	size_t conditional_transfer_count;
	size_t conditional_transfer_capacity;
} pigen_procedural_ast;

void pigen_parse_procedural_ast(const char *source, const char *end, pigen_procedural_ast *ast);
const char *pigen_procedural_guard_for(const pigen_procedural_ast *ast, const char *position);
const pigen_procedural_statement *pigen_procedural_statement_for(const pigen_procedural_ast *ast, const char *position);
const char *pigen_procedural_domain_for(const pigen_procedural_ast *ast, const char *position);
void pigen_free_procedural_ast(pigen_procedural_ast *ast);

#endif
