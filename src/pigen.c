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

#include "pigen/blocks.h"
#include "pigen/compile.h"
#include "pigen/specialize.h"
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
	fputs("usage: pigen INPUT.pigen [-o OUTPUT.sv] [--diagram PATH | --no-diagram]\n",
		stderr);
	exit(2);
}

static int source_has_content(const char *text, size_t length)
{
	size_t i;
	for (i = 0; i < length; i++)
		if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r' &&
			text[i] != '\n')
			return 1;
	return 0;
}

static char *diagram_path(const char *output, const char *fabric, int several)
{
	pigen_string path = {0};
	if (several)
		pigen_append_format(&path, "%s.%s.svg", output, fabric);
	else
		pigen_append_format(&path, "%s.svg", output);
	return path.data;
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
	const char *diagram_option = NULL;
	char *owned_output_path = NULL;
	char *source;
	size_t source_length;
	int no_diagram = 0;

	if (argc < 2) usage();
	for (int argument = 1; argument < argc; argument++)
	{
		if (!strcmp(argv[argument], "-o") || !strcmp(argv[argument], "--output"))
		{
			if (output_path || ++argument == argc) usage();
			output_path = argv[argument];
		}
		else if (!strcmp(argv[argument], "--diagram"))
		{
			if (diagram_option || ++argument == argc) usage();
			diagram_option = argv[argument];
		}
		else if (!strcmp(argv[argument], "--no-diagram"))
			no_diagram = 1;
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

	pigen_string generated = {0};
	pigen_fabric_diagrams diagrams = {0};
	char *cleaned = pigen_lower_blocks(source, source_length, &generated,
		no_diagram ? NULL : &diagrams);
	{
		const char *specialize_error = NULL;
		size_t specialize_error_offset = 0;
		char *specialized = pigen_specialize_fixed_point(cleaned, strlen(cleaned),
			256, 64u * 1024u * 1024u, &specialize_error, &specialize_error_offset);
		if (!specialized)
		{
			size_t line = 1, column = 1;
			for (size_t i = 0; i < specialize_error_offset && cleaned[i]; i++)
				if (cleaned[i] == '\n') { line++; column = 1; }
				else column++;
			fprintf(stderr, "%s:%zu:%zu: %s\n", input_path, line, column,
				specialize_error ? specialize_error : "specialization failed");
			free(cleaned);
			free(source);
			free(owned_output_path);
			return 1;
		}
		free(cleaned);
		cleaned = specialized;
	}
	int has_content = source_has_content(cleaned, strlen(cleaned));

	pigen_source_manager sources = {0};
	pigen_source_add(&sources, input_path, source, source_length);

	pigen_compile_result result = {0};
	pigen_compile_error error = {0};
	int ret = has_content ? pigen_compile_source(&sources, input_path,
		cleaned, strlen(cleaned), NULL, &result, &error) : 0;
	/* Fabric blocks may leave comment-only text behind; there is nothing
	 * ordinary to lower when naming finds no modules. */
	if (ret != 0 && generated.length && error.message &&
		strstr(error.message, "RTL naming failed"))
	{
		ret = 0;
		error = (pigen_compile_error){0};
	}
	if (ret == 0 && generated.length)
	{
		size_t base = result.length;
		char *combined = pigen_resize(result.systemverilog,
			base + generated.length + 2);
		if (base && combined[base - 1] != '\n')
			combined[base++] = '\n';
		memcpy(combined + base, generated.data, generated.length);
		base += generated.length;
		combined[base] = 0;
		result.systemverilog = combined;
		result.length = base;
	}
	free(cleaned);
	free(generated.data);
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

	if (!no_diagram && diagrams.count)
	{
		if (diagram_option && diagrams.count > 1)
		{
			fprintf(stderr,
				"pigen: --diagram requires exactly one fabric block\n");
			pigen_free_fabric_diagrams(&diagrams);
			pigen_free_sources(&sources);
			pigen_free_compile_result(&result);
			free(owned_output_path);
			return 1;
		}
		for (size_t i = 0; i < diagrams.count; i++)
		{
			char *owned = diagram_option ? NULL :
				diagram_path(output_path, diagrams.items[i].name,
					diagrams.count > 1);
			const char *path = diagram_option ? diagram_option : owned;
			if (!path || !write_file(path, diagrams.items[i].svg,
				strlen(diagrams.items[i].svg)))
			{
				free(owned);
				pigen_free_fabric_diagrams(&diagrams);
				pigen_free_sources(&sources);
				pigen_free_compile_result(&result);
				free(owned_output_path);
				return 1;
			}
			free(owned);
		}
	}
	pigen_free_fabric_diagrams(&diagrams);

	pigen_free_sources(&sources);
	pigen_free_compile_result(&result);
	free(owned_output_path);
	return 0;
}
