#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/source.h"

int main(void)
{
	pigen_source_manager sources = {0};
	char mutable_text[] = "module first;\n  buf [7:0] value;\nendmodule\n";
	pigen_source_id first = pigen_source_add(&sources, "first.pigen",
		mutable_text, strlen(mutable_text));
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
	text = pigen_source_span_text(&sources, value, &length);
	assert(text && length == 5 && !memcmp(text, "value", length));
	location = pigen_source_locate(&sources, value);
	assert(location.line == 2 && location.column == 13);

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
