#ifndef PIGEN_SOURCE_H
#define PIGEN_SOURCE_H

#include <stddef.h>

#include "pigen/ids.h"

typedef struct {
	pigen_source_id source;
	size_t start;
	size_t end;
} pigen_source_span;

typedef struct {
	size_t line;
	size_t column;
} pigen_source_location;

typedef struct {
	char *path;
	char *text;
	size_t length;
	size_t *line_starts;
	size_t line_count;
} pigen_source_file;

typedef struct {
	pigen_source_file *files;
	size_t count;
	size_t capacity;
} pigen_source_manager;

/* The manager owns immutable copies of PATH and TEXT after this call. */
pigen_source_id pigen_source_add(pigen_source_manager *manager,
	const char *path, const char *text, size_t length);
const pigen_source_file *pigen_source_get(const pigen_source_manager *manager,
	pigen_source_id source);
int pigen_source_span_valid(const pigen_source_manager *manager,
	pigen_source_span span);
pigen_source_location pigen_source_locate(const pigen_source_manager *manager,
	pigen_source_span span);
const char *pigen_source_span_text(const pigen_source_manager *manager,
	pigen_source_span span, size_t *length);
void pigen_free_sources(pigen_source_manager *manager);

#endif
