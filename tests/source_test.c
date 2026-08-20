#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/source.h"

static pigen_source_location scan_location(const char *text, size_t offset)
{
	pigen_source_location location = {1, 1};
	size_t i;
	for (i = 0; i < offset; i++)
	{
		if (text[i] == '\n')
			location = (pigen_source_location){location.line + 1, 1};
		else
			location.column++;
	}
	return location;
}

int main(void)
{
	pigen_source_manager sources = {0};
	char mutable_text[] = "module first;\n  buf [7:0] value;\nendmodule\n";
	const char empty_text[] = "";
	pigen_source_id first = pigen_source_add(&sources, "first.pigen",
		mutable_text, strlen(mutable_text));
	pigen_source_id empty = pigen_source_add(&sources, "empty.pigen",
		empty_text, 0);
	pigen_source_span value = {first, 26, 31};
	pigen_source_location location;
	const pigen_source_file *file;
	const char *text;
	size_t length;
	size_t i;

	mutable_text[26] = 'X';
	for (i = 0; i < 32; i++)
	{
		char path[32];
		(void)snprintf(path, sizeof(path), "extra-%zu.pigen", i);
		(void)pigen_source_add(&sources, path, "module m; endmodule\n", 20);
	}

	file = pigen_source_get(&sources, first);
	assert(file);
	assert(!strcmp(file->path, "first.pigen"));
	assert(file->text[26] == 'v');
	assert(file->line_count == 4);
	assert(file->line_starts[0] == 0);
	assert(file->line_starts[1] == 14);
	assert(file->line_starts[2] == 33);
	assert(file->line_starts[3] == file->length);
	text = pigen_source_span_text(&sources, value, &length);
	assert(text && length == 5 && !memcmp(text, "value", length));
	location = pigen_source_locate(&sources, value);
	assert(location.line == 2 && location.column == 13);
	location = pigen_source_locate(&sources,
		(pigen_source_span){first, 14, 14});
	assert(location.line == 2 && location.column == 1);
	location = pigen_source_locate(&sources,
		(pigen_source_span){first, file->length, file->length});
	assert(location.line == 4 && location.column == 1);
	location = pigen_source_locate(&sources,
		(pigen_source_span){empty, 0, 0});
	assert(location.line == 1 && location.column == 1);
	for (i = 0; i <= file->length; i++)
	{
		pigen_source_location expected = scan_location(file->text, i);
		location = pigen_source_locate(&sources,
			(pigen_source_span){first, i, i});
		assert(location.line == expected.line);
		assert(location.column == expected.column);
	}

	assert(!pigen_source_span_valid(&sources,
		(pigen_source_span){first, 30, 25}));
	assert(!pigen_source_span_valid(&sources,
		(pigen_source_span){first, 0, file->length + 1}));
	assert(!pigen_source_span_valid(&sources,
		(pigen_source_span){{PIGEN_INVALID_ID}, 0, 0}));
	assert(pigen_source_locate(&sources,
		(pigen_source_span){{PIGEN_INVALID_ID}, 0, 0}).line == 0);

	pigen_free_sources(&sources);
	assert(!sources.files && !sources.count && !sources.capacity);
	puts("PASS: source identities and spans retain immutable provenance");
	return 0;
}
