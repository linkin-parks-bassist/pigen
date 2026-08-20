/* Immutable source ownership and provenance for the structured frontend. */
#include <stdlib.h>
#include <string.h>

#include "pigen/source.h"
#include "pigen/util.h"

pigen_source_id pigen_source_add(pigen_source_manager *manager,
	const char *path, const char *text, size_t length)
{
	pigen_source_file *file;
	pigen_source_id result;
	size_t cursor;
	size_t line = 1;

	if (manager->count == PIGEN_INVALID_ID)
		pigen_fail("too many source files");
	if (manager->count == manager->capacity)
	{
		manager->capacity = manager->capacity ? manager->capacity * 2 : 8;
		manager->files = pigen_resize(manager->files,
			manager->capacity * sizeof(*manager->files));
	}

	result.index = (uint32_t)manager->count;
	file = &manager->files[manager->count++];
	file->path = pigen_copy_range(path, strlen(path));
	file->text = pigen_copy_range(text, length);
	file->length = length;
	for (cursor = 0; cursor < length; cursor++)
		if (text[cursor] == '\n') line++;
	file->line_starts = pigen_resize(NULL,
		line * sizeof(*file->line_starts));
	file->line_count = line;
	file->line_starts[0] = 0;
	line = 1;
	for (cursor = 0; cursor < length; cursor++)
		if (text[cursor] == '\n') file->line_starts[line++] = cursor + 1;
	return result;
}

const pigen_source_file *pigen_source_get(const pigen_source_manager *manager,
	pigen_source_id source)
{
	if (source.index == PIGEN_INVALID_ID || source.index >= manager->count)
		return NULL;
	return &manager->files[source.index];
}

int pigen_source_span_valid(const pigen_source_manager *manager,
	pigen_source_span span)
{
	const pigen_source_file *file = pigen_source_get(manager, span.source);
	return file && span.start <= span.end && span.end <= file->length;
}

pigen_source_location pigen_source_locate(const pigen_source_manager *manager,
	pigen_source_span span)
{
	const pigen_source_file *file;
	size_t first = 0;
	size_t after;

	if (!pigen_source_span_valid(manager, span))
		return (pigen_source_location){0, 0};
	file = pigen_source_get(manager, span.source);
	after = file->line_count;
	while (first + 1 < after)
	{
		size_t middle = first + (after - first) / 2;
		if (file->line_starts[middle] <= span.start)
			first = middle;
		else
			after = middle;
	}
	return (pigen_source_location){first + 1,
		span.start - file->line_starts[first] + 1};
}

const char *pigen_source_span_text(const pigen_source_manager *manager,
	pigen_source_span span, size_t *length)
{
	const pigen_source_file *file;

	if (!pigen_source_span_valid(manager, span))
		return NULL;
	file = pigen_source_get(manager, span.source);
	if (length)
		*length = span.end - span.start;
	return file->text + span.start;
}

void pigen_free_sources(pigen_source_manager *manager)
{
	size_t i;

	for (i = 0; i < manager->count; i++)
	{
		free(manager->files[i].path);
		free(manager->files[i].text);
		free(manager->files[i].line_starts);
	}
	free(manager->files);
	*manager = (pigen_source_manager){0};
}
