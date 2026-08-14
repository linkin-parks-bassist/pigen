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
	pigen_source_location location = {1, 1};
	size_t cursor;

	if (!pigen_source_span_valid(manager, span))
		return (pigen_source_location){0, 0};
	file = pigen_source_get(manager, span.source);
	for (cursor = 0; cursor < span.start; cursor++)
	{
		if (file->text[cursor] == '\n')
		{
			location.line++;
			location.column = 1;
		}
		else
			location.column++;
	}
	return location;
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
	}
	free(manager->files);
	*manager = (pigen_source_manager){0};
}
