/*
 * Pigen draft-0 frontend.
 *
 * This is intentionally a small SystemVerilog elaborator, not a compiler.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/model.h"
#include "pigen/assignments.h"
#include "pigen/blocks.h"
#include "pigen/declarations.h"
#include "pigen/fsm.h"
#include "pigen/pipeline.h"
#include "pigen/lexer.h"
#include "pigen/procedural.h"
#include "pigen/transfer.h"
#include "pigen/util.h"

#define fail pigen_fail

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
		fail("cannot size input");
	
	rewind(file);
	contents = pigen_resize(NULL, (size_t)file_length + 1);

	if (fread(contents, 1, (size_t)file_length, file) != (size_t)file_length)
		fail("cannot read input");

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

	if (input_length >= suffix_length && !memcmp(input + input_length - suffix_length, suffix, suffix_length))
		pigen_append_range(&path, input, input_length - suffix_length);
	else
		pigen_append(&path, input);

	pigen_append(&path, ".sv");
	return path.data;
}

static void usage(void)
{
	fputs("usage: pigen INPUT.pigen [-o OUTPUT.sv] [--diagram PATH | --no-diagram]\n", stderr);
	exit(2);
}

static char *diagram_output_path(const char *output_path, const char *fabric_name,
	size_t diagram_count)
{
	pigen_string path = {0};
	pigen_append(&path, output_path);
	if (diagram_count > 1)
	{
		pigen_append(&path, ".");
		pigen_append(&path, fabric_name);
	}
	pigen_append(&path, ".svg");
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

/* An ordinary sequential storage write whose RHS is a transport value.  This
 * is the escape hatch for inferred memories: Pigen owns the source handshake,
 * while the user retains the original SystemVerilog array destination. */
static int extract_manual_transport_write(const char *start, const char *end, pigen_primitives *primitives,
	const char **prefix_end, const char **destination, size_t *destination_length,
	const char **source, size_t *source_length)
{
	const char *operator = NULL;
	const char *cursor;
	const char *left_end;
	const char *source_end;
	for (cursor = start; cursor + 1 < end; cursor++)
		if (cursor[0] == '<' && cursor[1] == '=') operator = cursor;
	if (!operator) return 0;
	left_end = pigen_trim_end(start, operator);
	for (cursor = left_end; cursor > start && isspace((unsigned char)cursor[-1]); cursor--) ;
	left_end = cursor;
	while (cursor > start && !isspace((unsigned char)cursor[-1])) cursor--;
	if (cursor == left_end) return 0;
	*source = pigen_skip_spaces(operator + 2, end);
	source_end = pigen_trim_end(*source, end);
	for (cursor = *source; cursor < source_end && pigen_is_identifier_char((unsigned char)*cursor); cursor++) ;
	if (cursor != source_end || !pigen_find_primitive(primitives, *source, (size_t)(source_end - *source))) return 0;
	*prefix_end = cursor = left_end;
	*destination = left_end;
	while (*destination > start && !isspace((unsigned char)(*destination)[-1])) (*destination)--;
	*prefix_end = *destination;
	*destination_length = (size_t)(left_end - *destination);
	*source_length = (size_t)(source_end - *source);
	return 1;
}

int main(int argc, char **argv)
{
	const char *input_path = NULL;
	const char *output_path = NULL;
	const char *requested_diagram_path = NULL;
	int no_diagram = 0;
	char *owned_output_path = NULL;
	char *source;
	size_t source_length;
	size_t cursor;
	size_t statement_start;
	int previous_statement_was_generated = 0;
	pigen_string output = {0};
	pigen_string block_output = {0};
	pigen_fabric_diagrams diagrams = {0};
	pigen_primitives primitives = {0};
	pigen_assignments assignments = {0};
	pigen_width_checks width_checks = {0};
	pigen_clears clears = {0};
	pigen_procedural_ast procedural_ast = {0};
	pigen_pipelines *pipeline_models = NULL;
	pigen_tokens tokens = {0};
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
			if (requested_diagram_path || no_diagram || ++argument == argc) usage();
			requested_diagram_path = argv[argument];
		}
		else if (!strncmp(argv[argument], "--diagram=", 10))
		{
			if (requested_diagram_path || no_diagram || !argv[argument][10]) usage();
			requested_diagram_path = argv[argument] + 10;
		}
		else if (!strcmp(argv[argument], "--no-diagram"))
		{
			if (requested_diagram_path || no_diagram) usage();
			no_diagram = 1;
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
	pigen_set_diagnostic_context(input_path, source);
	{
		char *lowered_source = pigen_lower_blocks(source, source_length, &block_output, &diagrams);
		free(source);
		source = lowered_source;
		pigen_set_diagnostic_context(input_path, source);
	}
	if ((requested_diagram_path || no_diagram) && !diagrams.count)
		fail("diagram options require at least one fabric block");
	if (requested_diagram_path && diagrams.count != 1)
		fail("--diagram requires exactly one fabric block; default paths support multiple fabrics");
	{
		char *lowered_source = pigen_lower_fsms(source, source_length, &source_length);
		free(source);
		source = lowered_source;
		pigen_set_diagnostic_context(input_path, source);
	}
	{
		char *lowered_source = pigen_lower_transfer_blocks(source, source_length,
			&source_length);
		free(source);
		source = lowered_source;
		pigen_set_diagnostic_context(input_path, source);
	}
	{
		char *prepared_source = pigen_prepare_pipeline_models(source, source_length,
			&source_length, &pipeline_models);
		free(source);
		source = prepared_source;
		pigen_set_diagnostic_context(input_path, source);
		pigen_parse_procedural_ast(source, source + source_length, &procedural_ast);
		{
			char *finished_source = pigen_finish_pipeline_models(source, source_length,
				&procedural_ast, pipeline_models, &source_length);
			free(source);
			source = finished_source;
			pigen_set_diagnostic_context(input_path, source);
			pigen_free_procedural_ast(&procedural_ast);
			procedural_ast = (pigen_procedural_ast){0};
			pigen_parse_procedural_ast(source, source + source_length, &procedural_ast);
		}
	}
	pigen_lex_source(source, source_length, &tokens);
	statement_start = 0;

	/*
	 * Preserve ordinary SystemVerilog and rewrite only primitive declarations.
	 * ANSI ports are expanded as individual top-level comma-separated items.
	 */
	for (cursor = 0; cursor <= source_length; )
	{
		if (cursor + 28 <= source_length && !memcmp(source + cursor, "/*__PIGEN_PIPELINE_BEGIN__*/", 28))
		{
			const char *marker_end = strstr(source + cursor + 28, "/*__PIGEN_PIPELINE_END__*/");
			if (!marker_end) fail("unterminated internal pipeline fragment");
			pigen_append_range(&output, source + statement_start,
				(size_t)(marker_end - (source + statement_start)));
			cursor = (size_t)(marker_end - source) + 26;
			statement_start = cursor;
			continue;
		}
		const char *opaque = pigen_skip_opaque(source + cursor, source + source_length);

		if (opaque)
		{
			cursor = (size_t)(opaque - source);
			continue;
		}

		if (cursor == source_length || source[cursor] == ';')
		{
			const char *start = source + statement_start;
			pigen_set_diagnostic_position(start);
			const char *end = source + cursor;
			const char *head = pigen_skip_trivia(start, end);
			const char *keyword;
			const char *after_keyword;
			const char *prefix_end;
			const char *destination;
			const char *expression;
			pigen_transfer transfer;
			const char *guard;
			const pigen_procedural_statement *procedural_statement;
			const char *action_start;
			size_t destination_length;
			size_t expression_length;
			size_t guard_length;
			int clear_action_kind;
			char kind = pigen_declaration_kind(head, pigen_trim_end(head, end), &keyword, &after_keyword);
			procedural_statement = end > start ?
				pigen_procedural_statement_for(&procedural_ast, end - 1) : NULL;
			action_start = procedural_statement ? procedural_statement->start : start;

			if (kind)
			{
				pigen_append_range(&output, start, (size_t)(head - start));
				pigen_emit_internal_declaration(&output, head, end, &primitives);
				previous_statement_was_generated = 1;
			}
			else if ((size_t)(end - head) >= 6 && !memcmp(head, "module", 6) && !pigen_is_identifier_char(head[6]))
			{
				const char *open = strchr(head, '(');
				const char *close;
				int depth = 0;

				if (!open)
					fail("module header without a port list");

				for (close = open; close < end; close++)
				{
					if (*close == '(')
						depth++;
					else if (*close == ')' && --depth == 0)
						break;
				}

				if (close == end)
					fail("unterminated module port list");
				{
					const char *scan;
					int has_reset = 0;
					for (scan = open + 1; scan + 5 <= close; scan++)
						if (!memcmp(scan, "reset", 5) &&
							(scan == open + 1 || !pigen_is_identifier_char((unsigned char)scan[-1])) &&
							(scan + 5 == close || !pigen_is_identifier_char((unsigned char)scan[5])))
							has_reset = 1;
					pigen_set_reset_connection(has_reset);
				}

				pigen_append_range(&output, head, (size_t)(open - head));
				pigen_append(&output, "\n\t(\n");
				pigen_emit_ports(&output, open + 1, close, &primitives);
				pigen_append(&output, "\t);\n");
				pigen_emit_port_adapters(&output, &primitives);
				previous_statement_was_generated = 0;
			}
			else if (pigen_extract_clear_action(start, end, &primitives, &prefix_end, &destination, &destination_length, &clear_action_kind))
			{
				if (!pigen_procedural_statement_for(&procedural_ast, prefix_end))
					fail("transport clear actions are allowed only in clocked always blocks");
				const char *parsed_guard = pigen_procedural_guard_for(&procedural_ast, prefix_end);
				const char *domain = pigen_procedural_domain_for(&procedural_ast, prefix_end);
				pigen_string rewritten_guard = {0};

				pigen_emit_rewritten_expression(&rewritten_guard, parsed_guard, parsed_guard + strlen(parsed_guard), &primitives);
				pigen_emit_rewritten_expression(&output, start, prefix_end, &primitives);
				pigen_append(&output, ";");
				if (clear_action_kind != 3)
					pigen_add_clear(&clears, destination, destination_length,
						rewritten_guard.data ? rewritten_guard.data : "",
						rewritten_guard.data ? strlen(rewritten_guard.data) : 0, domain,
						strlen(domain), clear_action_kind, (size_t)(prefix_end - source));
				free(rewritten_guard.data);
				previous_statement_was_generated = 0;
			}
			else if (pigen_extract_transport_transfer(action_start, end, &primitives, &transfer))
			{
				size_t group = assignments.next_group++;
				if (transfer.requires_exact_width)
					pigen_add_width_check(&width_checks, transfer.width_lhs,
						strlen(transfer.width_lhs), transfer.width_rhs,
						transfer.width_rhs_length, group);
				prefix_end = transfer.prefix_end;
				if (!pigen_procedural_statement_for(&procedural_ast, prefix_end))
					fail("transport assignments are allowed only in clocked always blocks");
				const char *parsed_guard = pigen_procedural_guard_for(&procedural_ast, prefix_end);
				const char *domain = pigen_procedural_domain_for(&procedural_ast, prefix_end);
				pigen_string rewritten_guard = {0};
				int emitted_register_member = 0;
				int has_immediate_member = 0;
				int grouped_immediate = transfer.count > 1;

				pigen_emit_rewritten_expression(&rewritten_guard, parsed_guard, parsed_guard + strlen(parsed_guard), &primitives);
				guard = rewritten_guard.data ? rewritten_guard.data : "";
				guard_length = strlen(guard);
				pigen_emit_rewritten_expression(&output, start, prefix_end, &primitives);

				for (size_t transfer_index = 0; transfer_index < transfer.count; transfer_index++)
				{
					if (transfer.items[transfer_index].destination_kind == 'r' ||
						transfer.items[transfer_index].destination_kind == 'l' ||
						transfer.items[transfer_index].destination_kind == 'm')
						has_immediate_member = 1;
					else
						grouped_immediate = 0;
				}
				if (!has_immediate_member) pigen_append(&output, ";");
				if (grouped_immediate)
				{
					pigen_append(&output, "if (");
					pigen_emit_transfer_accept_condition(&output, &transfer, guard, &primitives);
					pigen_append(&output, ")\n\t\tbegin\n\t\t\t");
					pigen_append(&output, transfer.width_lhs);
					pigen_append(&output, " <= ");
					pigen_emit_rewritten_expression(&output, transfer.width_rhs,
						transfer.width_rhs + transfer.width_rhs_length, &primitives);
					pigen_append(&output, ";\n\t\tend");
					emitted_register_member = 1;
				}

				for (size_t transfer_index = 0; transfer_index < transfer.count; transfer_index++)
				{
					pigen_transfer_item *item = &transfer.items[transfer_index];
					if (!grouped_immediate && (item->destination_kind == 'r' || item->destination_kind == 'l' || item->destination_kind == 'm'))
					{
						if (emitted_register_member)
							pigen_append(&output, "\n\t\t");
						pigen_append(&output, "if (");
						pigen_emit_transfer_accept_condition(&output, &transfer, guard, &primitives);
						pigen_append(&output, ")\n\t\tbegin\n\t\t\t");
						pigen_append_range(&output, item->destination, item->destination_length);
						pigen_append(&output, " <= ");
						pigen_emit_rewritten_expression(&output, item->expression, item->expression + item->expression_length, &primitives);
						pigen_append(&output, ";\n\t\tend");
						emitted_register_member = 1;
					}
					pigen_add_assignment_in_group(&assignments, item->destination, item->destination_length,
						item->expression, item->expression_length, guard, guard_length, domain, strlen(domain), item->destination_kind, group, (size_t)(prefix_end - source));
				}
				pigen_free_transfer(&transfer);
				free(rewritten_guard.data);
				previous_statement_was_generated = 0;
			}
			else if (extract_manual_transport_write(start, end, &primitives, &prefix_end, &destination,
				&destination_length, &expression, &expression_length))
			{
				const char *parsed_guard;
				const char *domain;
				pigen_string rewritten_guard = {0};
				if (!pigen_procedural_statement_for(&procedural_ast, prefix_end))
					fail("manual transport writes are allowed only in clocked always blocks");
				parsed_guard = pigen_procedural_guard_for(&procedural_ast, prefix_end);
				domain = pigen_procedural_domain_for(&procedural_ast, prefix_end);
				pigen_emit_rewritten_expression(&rewritten_guard, parsed_guard,
					parsed_guard + strlen(parsed_guard), &primitives);
				pigen_emit_rewritten_expression(&output, start, prefix_end, &primitives);
				pigen_append(&output, "if (");
				pigen_emit_expression_validity(&output, expression, &primitives);
				pigen_append(&output, ")\n\t\tbegin\n\t\t\t");
				pigen_append_range(&output, destination, destination_length);
				pigen_append(&output, " <= ");
				pigen_append_range(&output, expression, expression_length);
				pigen_append(&output, ";\n\t\tend");
				pigen_add_assignment(&assignments, destination, destination_length, expression, expression_length,
					rewritten_guard.data ? rewritten_guard.data : "",
					rewritten_guard.data ? strlen(rewritten_guard.data) : 0,
					domain, strlen(domain), 'm', (size_t)(prefix_end - source));
				free(rewritten_guard.data);
				previous_statement_was_generated = 0;
			}
			else
			{
				const char *endmodule = strstr(head, "endmodule");

				if (previous_statement_was_generated)
					start = pigen_skip_spaces(start, end);

				if (endmodule && endmodule < end)
				{
					size_t conditional_index;
					for (conditional_index = 0; conditional_index < procedural_ast.conditional_transfer_count; conditional_index++)
					{
						pigen_conditional_transfer *transfer = &procedural_ast.conditional_transfers[conditional_index];
						pigen_transfer conditional;
						pigen_string rewritten_transfer_guard = {0};
						if (!pigen_extract_transport_transfer(transfer->start, transfer->end, &primitives, &conditional) ||
							pigen_skip_spaces(transfer->start, transfer->end) != conditional.prefix_end)
							fail("conditional transfer requires declared transport values");
						pigen_emit_rewritten_expression(&rewritten_transfer_guard, transfer->guard,
							transfer->guard + strlen(transfer->guard), &primitives);
						{
							size_t group = assignments.next_group++;
							if (conditional.requires_exact_width)
								pigen_add_width_check(&width_checks, conditional.width_lhs,
									strlen(conditional.width_lhs), conditional.width_rhs,
									conditional.width_rhs_length, group);
							for (size_t member = 0; member < conditional.count; member++)
								pigen_add_assignment_in_group(&assignments, conditional.items[member].destination, conditional.items[member].destination_length,
									conditional.items[member].expression, conditional.items[member].expression_length,
									rewritten_transfer_guard.data ? rewritten_transfer_guard.data : "",
									rewritten_transfer_guard.data ? strlen(rewritten_transfer_guard.data) : 0,
									transfer->domain, strlen(transfer->domain),
									conditional.items[member].destination_kind == 'r' ? 'R' : conditional.items[member].destination_kind == 'l' ? 'L' : conditional.items[member].destination_kind == 'm' ? 'M' : conditional.items[member].destination_kind, group, (size_t)(transfer->start - source));
						}
						pigen_free_transfer(&conditional);
						free(rewritten_transfer_guard.data);
					}
					pigen_append_range(&output, start, (size_t)(endmodule - start));
					pigen_validate_assignments(&assignments, &primitives);
					pigen_validate_clears(&clears, &assignments, &primitives);
					pigen_emit_width_checks(&output, &width_checks, &primitives);
					pigen_emit_assignment_routes(&output, &assignments, &primitives);
					pigen_emit_clear_routes(&output, &clears, &assignments, &primitives);
					pigen_append_range(&output, endmodule, (size_t)(end - endmodule));
				}
				else
					pigen_emit_rewritten_expression(&output, start, end, &primitives);

				if (cursor < source_length)
					pigen_append(&output, ";");
				previous_statement_was_generated = 0;
			}

			statement_start = cursor + 1;
		}

		if (cursor == source_length)
			break;

		cursor++;
	}
	pigen_append(&output, block_output.data ? block_output.data : "");

	if (!write_file(output_path, output.data, output.length)) return 1;

	fprintf(stderr, "pigen: wrote %s\n", output_path);
	if (!no_diagram)
		for (size_t index = 0; index < diagrams.count; index++)
		{
			char *owned_diagram_path = NULL;
			const char *path = requested_diagram_path;
			if (!path)
			{
				owned_diagram_path = diagram_output_path(output_path,
					diagrams.items[index].name, diagrams.count);
				path = owned_diagram_path;
			}
			if (!write_file(path, diagrams.items[index].svg,
				strlen(diagrams.items[index].svg))) return 1;
			fprintf(stderr, "pigen: wrote %s\n", path);
			free(owned_diagram_path);
		}
	pigen_free_procedural_ast(&procedural_ast);
	pigen_free_tokens(&tokens);
	pigen_free_pipeline_models(pipeline_models);
	pigen_free_fabric_diagrams(&diagrams);
	free(owned_output_path);
	free(block_output.data);
	free(source);
	free(output.data);
	return 0;
}
