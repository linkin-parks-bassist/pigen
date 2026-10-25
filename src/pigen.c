/*
 * Pigen structured compiler frontend.
 *
 * Compiles one .pigen source through the structured pipeline
 * (preprocess, lex, parse, semantic, RTL lowering, naming,
 * output model, SV emission) via pigen_compile_source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/compile.h"
#include "pigen/source.h"
#include "pigen/util.h"

static char *read_file(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	long file_length = 0;
	char *contents;

	if (!file)
	{
		perror(path);
		exit(1);
	}

	if (fseek(file, 0, SEEK_END) || (file_length = ftell(file)) < 0)
	{
		fprintf(stderr, "pigen: cannot size input\n");
		exit(1);
	}

	rewind(file);
	contents = pigen_resize(NULL, (size_t)file_length + 1);

	if (fread(contents, 1, (size_t)file_length, file) != (size_t)file_length)
	{
		fprintf(stderr, "pigen: cannot read input\n");
		exit(1);
	}

	fclose(file);
	contents[file_length] = 0;
	*length = (size_t)file_length;
	return contents;
}

static char *default_output_path(const char *input)
{
	const char *suffix = ".pigen";
	size_t input_length = strlen(input);
	size_t suffix_length = strlen(suffix);
	pigen_string path = {0};

	if (input_length >= suffix_length &&
		!memcmp(input + input_length - suffix_length, suffix, suffix_length))
		pigen_append_range(&path, input, input_length - suffix_length);
	else
		pigen_append(&path, input);

	pigen_append(&path, ".sv");
	return path.data;
}

static void usage(void)
{
	fputs("usage: pigen INPUT.pigen [-o OUTPUT.sv]\n", stderr);
	exit(2);
}

static int write_file(const char *path, const char *data, size_t length)
{
	FILE *file = fopen(path, "wb");
	int failed;
	if (!file)
	{
		perror(path);
		return 0;
	}
	failed = fwrite(data, 1, length, file) != length;
	if (fclose(file)) failed = 1;
	if (failed)
	{
		perror(path);
		return 0;
	}
	return 1;
}

static void report_error(const pigen_source_manager *sources,
	const char *path, const pigen_compile_error *error)
{
	const pigen_source_file *file;
	const char *line_start;
	size_t line_length;
	size_t pad;
	pigen_source_location location;

	if (error->message && error->span.start != error->span.end &&
		pigen_source_span_valid(sources, error->span))
	{
		location = pigen_source_locate(sources, error->span);
		file = pigen_source_get(sources, error->span.source);
		line_start = file->text + file->line_starts[location.line - 1];
		line_length = strcspn(line_start, "\n");
		pad = location.column - 1;
		fprintf(stderr, "%s:%zu:%zu: %s\n", path, location.line,
			location.column, error->message);
		fprintf(stderr, "    %.*s\n", (int)line_length, line_start);
		fprintf(stderr, "    %*s^\n", (int)pad, "");
		return;
	}
	fprintf(stderr, "pigen: %s\n",
		error->message ? error->message : "compile failed");
}

int main(int argc, char **argv)
{
	const char *input_path = NULL;
	const char *output_path = NULL;
	char *owned_output_path = NULL;
	char *source;
	size_t source_length;

	if (argc < 2) usage();
	for (int argument = 1; argument < argc; argument++)
	{
		if (!strcmp(argv[argument], "-o") || !strcmp(argv[argument], "--output"))
		{
			if (output_path || ++argument == argc) usage();
			output_path = argv[argument];
		}
		else if (argv[argument][0] == '-' || input_path)
			usage();
		else
			input_path = argv[argument];
	}
	if (!input_path) usage();

	if (!output_path)
	{
		owned_output_path = default_output_path(input_path);
		output_path = owned_output_path;
	}

	source = read_file(input_path, &source_length);

	pigen_source_manager sources = {0};
	pigen_source_add(&sources, input_path, source, source_length);

	pigen_compile_result result = {0};
	pigen_compile_error error = {0};
	int ret = pigen_compile_source(&sources, input_path,
		source, source_length, NULL, &result, &error);
	free(source);

	if (ret != 0)
	{
		/* Loud failure: diagnostic with native source span to stderr,
		 * nonzero exit, no output file written. */
		report_error(&sources, input_path, &error);
		pigen_free_sources(&sources);
		pigen_free_compile_result(&result);
		free(owned_output_path);
		return 1;
	}

	/* Silent success: write the output file, stdout empty, exit 0,
	 * no "wrote" notice. */
	if (!write_file(output_path, result.systemverilog, result.length))
	{
		pigen_free_sources(&sources);
		pigen_free_compile_result(&result);
		free(owned_output_path);
		return 1;
	}

	pigen_free_sources(&sources);
	pigen_free_compile_result(&result);
	free(owned_output_path);
	return 0;
}
