/* Signal declaration parsing and primitive-instance emission. */
#include <string.h>
#include <stdlib.h>

#include "pigen/declarations.h"
#include "pigen/util.h"

static const char *reset_connection = "1'b0";

void pigen_set_reset_connection(int has_reset)
{
	reset_connection = has_reset ? "reset" : "1'b0";
}

const char *pigen_reset_connection(void)
{
	return reset_connection;
}

static int last_identifier(const char *start, const char *end, const char **name, size_t *name_length)
{
	const char *cursor = end;
	
	while (cursor > start)
	{
		while (cursor > start && !pigen_is_identifier_char((unsigned char)cursor[-1]))
			cursor--;
		
		end = cursor;
		
		while (cursor > start && pigen_is_identifier_char((unsigned char)cursor[-1]))
			cursor--;
		
		if (cursor < end)
		{
			*name = cursor;
			*name_length = (size_t)(end - cursor);
			return 1;
		}
	}
	
	return 0;
}

/* Accept a primitive after an ANSI port direction, if one is present. */
char pigen_declaration_transfer_type(const char *start, const char *end, const char **keyword, const char **after_keyword)
{
	const char *cursor = pigen_skip_spaces(start, end);
	const char *word_start;
	char transfer_type;
	
	if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "input", 5) &&
		(cursor + 5 == end || !pigen_is_identifier_char(cursor[5])))
		cursor = pigen_skip_spaces(cursor + 5, end);
	else if ((size_t)(end - cursor) >= 6 && !memcmp(cursor, "output", 6) &&
		(cursor + 6 == end || !pigen_is_identifier_char(cursor[6])))
		cursor = pigen_skip_spaces(cursor + 6, end);
	else if ((size_t)(end - cursor) >= 5 && !memcmp(cursor, "inout", 5) &&
		(cursor + 5 == end || !pigen_is_identifier_char(cursor[5])))
		cursor = pigen_skip_spaces(cursor + 5, end);
	
	word_start = cursor;
	while (cursor < end && pigen_is_identifier_char((unsigned char)*cursor))
		cursor++;
	
	transfer_type = pigen_transfer_type_for_keyword(word_start, (size_t)(cursor - word_start));
	if (transfer_type)
	{
		*keyword = word_start;
		*after_keyword = cursor;
		return transfer_type;
	}

	return 0;
}

int pigen_transfer_type_has_storage(char transfer_type)
{
	const pigen_transfer_type_descriptor *descriptor = pigen_transfer_type_descriptor_get(transfer_type);

	return descriptor && descriptor->is_storage;
}

static int is_static_transfer_type(char transfer_type)
{
	return transfer_type == 'w' || transfer_type == 'r' || transfer_type == 'l';
}

void pigen_append_control_name(pigen_string *output, const char *name, size_t name_length, const char *suffix);

void pigen_emit_signal_condition(pigen_string *output, pigen_primitive *primitive, const char *suffix)
{
	if (is_static_transfer_type(primitive->transfer_type))
	{
		if (!strcmp(suffix, "valid") || primitive->transfer_type != 'w')
			pigen_append(output, "1'b1");
		else
			pigen_append(output, "1'b0");
	}
	else
		pigen_append_control_name(output, primitive->name, strlen(primitive->name),
			!strcmp(suffix, "ready") ? "out_ready" : suffix);
}

static const char *fifo_payload_start(const char *start, const char *end, const char **depth, size_t *depth_length, const char **payload_end)
{
	const char *original_start;
	const char *close;
	const char *after_first;
	const char *second_close;
	const char *first_contents;
	size_t first_contents_length;
	int first_is_payload = 0;
	const char *cursor;

	start = pigen_skip_spaces(start, end);
	original_start = start;
	/* A packed payload may carry a SystemVerilog signedness modifier. */
	if (((size_t)(end - start) >= 6 && !memcmp(start, "signed", 6) &&
		(start + 6 == end || !pigen_is_identifier_char(start[6]))) ||
		((size_t)(end - start) >= 8 && !memcmp(start, "unsigned", 8) &&
		(start + 8 == end || !pigen_is_identifier_char(start[8]))))
	{
		start = pigen_skip_spaces(start + (start[0] == 's' ? 6 : 8), end);
		if (start == end || *start != '[')
			pigen_fail("signed fifo payload requires a packed range, such as `fifo signed [7:0][4] queue`");
	}
	/* A FIFO payload may be a packed range or a named SV type. */
	if (start != end && *start != '[')
	{
		const char *type_end = start;
		const char *depth_open;
		while (type_end < end && pigen_is_identifier_char((unsigned char)*type_end)) type_end++;
		if (type_end == start) pigen_fail("fifo requires payload then depth, such as `fifo[7:0][4] queue`");
		depth_open = pigen_skip_spaces(type_end, end);
		if (depth_open == end || *depth_open != '[')
			pigen_fail("fifo payload requires a following depth, such as `fifo packet_t[4] queue`");
		second_close = depth_open + 1;
		while (second_close < end && *second_close != ']') second_close++;
		if (second_close == end) pigen_fail("unterminated fifo depth");
		*depth = pigen_skip_spaces(depth_open + 1, second_close);
		*depth_length = (size_t)(pigen_trim_end(*depth, second_close) - *depth);
		if (!*depth_length) pigen_fail("fifo depth must not be empty");
		*payload_end = type_end;
		return original_start;
	}
	if (start == end)
		pigen_fail("fifo requires payload then depth, such as `fifo[7:0][4] queue`");

	close = start + 1;
	while (close < end && *close != ']')
		close++;

	if (close == end)
		pigen_fail("unterminated fifo depth");

	first_contents = pigen_skip_spaces(start + 1, close);
	first_contents_length = (size_t)(pigen_trim_end(first_contents, close) - first_contents);

	if (first_contents_length == 0)
		pigen_fail("fifo depth must not be empty");

	/* The first packed dimension is payload; the final bracket group is depth. */
	for (cursor = first_contents; cursor < close; cursor++)
	{
		if (*cursor == ':')
		{
			first_is_payload = 1;
			break;
		}
	}

	after_first = pigen_skip_spaces(close + 1, end);

	if (!first_is_payload)
		pigen_fail("fifo requires payload before depth, such as `fifo[7:0][4] queue`");

	if (after_first == end || *after_first != '[')
		pigen_fail("fifo payload width requires a following depth, such as `fifo[7:0][4] queue`");

	second_close = after_first + 1;
	while (second_close < end && *second_close != ']')
		second_close++;

	if (second_close == end)
		pigen_fail("unterminated fifo depth");

	*depth = pigen_skip_spaces(after_first + 1, second_close);
	*depth_length = (size_t)(pigen_trim_end(*depth, second_close) - *depth);

	if (*depth_length == 0)
		pigen_fail("fifo depth must not be empty");

	*payload_end = close + 1;
	return original_start;
}

static const char *payload_start(const char *start, const char *end)
{
	start = pigen_skip_spaces(start, end);
	
	if ((size_t)(end - start) >= 5 && !memcmp(start, "logic", 5) &&
		(start + 5 == end || !pigen_is_identifier_char(start[5])))
		pigen_fail("transfer types replace logic here; write `buf [WIDTH:0] name`");
	
	return start;
}

void pigen_append_control_name(pigen_string *output, const char *name, size_t name_length, const char *suffix)
{
	pigen_append_range(output, name, name_length);
	pigen_append(output, "__pigen_");
	pigen_append(output, suffix);
}

/* Module boundaries use normal SV-looking handshake names. */
static void append_port_control_name(pigen_string *output, const char *name, size_t name_length, const char *suffix)
{
	pigen_append_range(output, name, name_length);
	pigen_append(output, "_");
	pigen_append(output, suffix);
}

static void append_data_type(pigen_string *output, const char *start, const char *end)
{
	const char *trimmed = pigen_skip_spaces(start, end);
	if (trimmed < end && *trimmed == '[')
		pigen_append(output, "logic ");
	else if (((size_t)(end - trimmed) >= 6 && !memcmp(trimmed, "signed", 6) &&
		(trimmed + 6 == end || !pigen_is_identifier_char(trimmed[6]))) ||
		((size_t)(end - trimmed) >= 8 && !memcmp(trimmed, "unsigned", 8) &&
		(trimmed + 8 == end || !pigen_is_identifier_char(trimmed[8]))))
		pigen_append(output, "logic ");
	pigen_append_range(output, start, (size_t)(end - start));
}

static int is_unpacked_reg_array(char transfer_type, const char *after_keyword, const char *end)
{
	const char *cursor;
	if (transfer_type != 'r' && transfer_type != 'l') return 0;
	cursor = pigen_skip_spaces(after_keyword, end);
	if (((size_t)(end - cursor) >= 6 && !memcmp(cursor, "signed", 6) &&
		(cursor + 6 == end || !pigen_is_identifier_char(cursor[6]))) ||
		((size_t)(end - cursor) >= 8 && !memcmp(cursor, "unsigned", 8) &&
		(cursor + 8 == end || !pigen_is_identifier_char(cursor[8]))))
		cursor = pigen_skip_spaces(cursor + (cursor[0] == 's' ? 6 : 8), end);
	if (cursor < end && *cursor == '[')
	{
		for (cursor++; cursor < end && *cursor != ']'; cursor++) ;
		if (cursor == end) return 0;
		cursor = pigen_skip_spaces(cursor + 1, end);
	}
	while (cursor < end && pigen_is_identifier_char((unsigned char)*cursor)) cursor++;
	cursor = pigen_skip_spaces(cursor, end);
	return cursor < end && *cursor == '[';
}

/* Ordinary SV permits a comma-separated list of scalar wire/register names.
 * They are not signals, but Pigen must record each one so a later transfer
 * can identify its destination. */
static int emit_plain_declaration_list(pigen_string *output, const char *start,
	const char *after_keyword, const char *end, pigen_primitives *primitives, char transfer_type)
{
	const char *cursor;
	const char *part_start = after_keyword;
	int parens = 0, brackets = 0, braces = 0;
	int has_comma = 0;

	if (transfer_type != 'w' && transfer_type != 'r' && transfer_type != 'l') return 0;
	for (cursor = after_keyword; cursor < end; cursor++)
	{
		if (*cursor == '(') parens++;
		else if (*cursor == ')') parens--;
		else if (*cursor == '[') brackets++;
		else if (*cursor == ']') brackets--;
		else if (*cursor == '{') braces++;
		else if (*cursor == '}') braces--;
		else if (!parens && !brackets && !braces && *cursor == '=')
			return 0;
		else if (!parens && !brackets && !braces && *cursor == ',')
			has_comma = 1;
	}
	if (!has_comma) return 0;

	for (cursor = after_keyword; ; cursor++)
	{
		if (cursor == end || *cursor == ',')
		{
			const char *name;
			size_t name_length;
			if (!last_identifier(part_start, cursor, &name, &name_length))
				pigen_fail("declaration list requires an identifier after each comma");
			pigen_add_primitive(primitives, name, name_length, transfer_type, 1);
			if (cursor == end) break;
			part_start = cursor + 1;
		}
	}
	pigen_append_range(output, start, (size_t)(end - start));
	pigen_append(output, ";");
	return 1;
}

/* A signal list is merely concise spelling for several declarations with
 * the same data type. Lower each member through the normal single-name
 * path, so each receives its own endpoint and primitive instance. */
static int emit_signal_declaration_list(pigen_string *output, const char *keyword,
	const char *after_keyword, const char *end, pigen_primitives *primitives, char transfer_type)
{
	const char *cursor;
	const char *first_comma = NULL;
	const char *part_start;
	const char *first_name;
	const char *payload_end;
	size_t first_name_length;
	size_t keyword_length = 0;
	int parens = 0, brackets = 0, braces = 0;

	if (!pigen_transfer_type_has_storage(transfer_type)) return 0;
	while (pigen_is_identifier_char((unsigned char)keyword[keyword_length])) keyword_length++;
	for (cursor = after_keyword; cursor < end; cursor++)
	{
		if (*cursor == '(') parens++;
		else if (*cursor == ')') parens--;
		else if (*cursor == '[') brackets++;
		else if (*cursor == ']') brackets--;
		else if (*cursor == '{') braces++;
		else if (*cursor == '}') braces--;
		else if (!parens && !brackets && !braces && *cursor == ',')
		{
			first_comma = cursor;
			break;
		}
	}
	if (!first_comma || !last_identifier(after_keyword, first_comma, &first_name, &first_name_length))
		return 0;
	payload_end = pigen_trim_end(after_keyword, first_name);

	for (part_start = after_keyword, cursor = first_comma; ; )
	{
		const char *part_end = cursor;
		const char *name;
		size_t name_length;
		pigen_string declaration = {0};
		if (!last_identifier(part_start, part_end, &name, &name_length))
			pigen_fail("signal declaration list requires an identifier after each comma");
		pigen_append_range(&declaration, keyword, keyword_length);
		pigen_append(&declaration, " ");
		pigen_append_range(&declaration, after_keyword, (size_t)(payload_end - after_keyword));
		pigen_append(&declaration, " ");
		pigen_append_range(&declaration, name, name_length);
		pigen_emit_internal_declaration(output, declaration.data,
			declaration.data + declaration.length, primitives);
		free(declaration.data);
		if (part_end == end) break;
		part_start = part_end + 1;
		cursor = end;
		parens = brackets = braces = 0;
		for (const char *scan = part_start; scan < end; scan++)
		{
			if (*scan == '(') parens++;
			else if (*scan == ')') parens--;
			else if (*scan == '[') brackets++;
			else if (*scan == ']') brackets--;
			else if (*scan == '{') braces++;
			else if (*scan == '}') braces--;
			else if (!parens && !brackets && !braces && *scan == ',')
			{
				cursor = scan;
				break;
			}
		}
	}
	return 1;
}

void pigen_emit_internal_declaration(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives)
{
	const char *keyword;
	const char *after_keyword;
	const char *name;
	const char *cursor;
	const char *payload_end;
	const char *fifo_payload_end = NULL;
	const char *primitive_module;
	const pigen_transfer_type_descriptor *descriptor;
	const char *fifo_depth = NULL;
	size_t fifo_depth_length = 0;
	size_t name_length;
	char transfer_type = pigen_declaration_transfer_type(start, end, &keyword, &after_keyword);

	if (emit_plain_declaration_list(output, start, after_keyword, end, primitives, transfer_type))
		return;
	if (emit_signal_declaration_list(output, keyword, after_keyword, end, primitives, transfer_type))
		return;

	/* Plain unpacked reg/logic arrays are ordinary SV memories, not signal
	 * declarations.  Preserve them so a port write can infer a synchronous RAM. */
	if (is_unpacked_reg_array(transfer_type, after_keyword, end))
	{
		pigen_append_range(output, start, (size_t)(end - start));
		pigen_append(output, ";");
		return;
	}
	
	if (transfer_type == 'f')
		after_keyword = fifo_payload_start(after_keyword, end, &fifo_depth, &fifo_depth_length, &fifo_payload_end);

	after_keyword = payload_start(after_keyword, end);
	end = pigen_trim_end(after_keyword, end);
	
	if (!transfer_type || !last_identifier(after_keyword, end, &name, &name_length))
	{
		pigen_append_range(output, start, (size_t)(end - start));
		pigen_append(output, ";");
		return;
	}

	{
		int parens = 0, brackets = 0, braces = 0;
		for (cursor = after_keyword; cursor < end; cursor++)
		{
			if (*cursor == '(') parens++;
			else if (*cursor == ')') parens--;
			else if (*cursor == '[') brackets++;
			else if (*cursor == ']') brackets--;
			else if (*cursor == '{') braces++;
			else if (*cursor == '}') braces--;
			else if (!parens && !brackets && !braces && (*cursor == ',' || *cursor == '='))
				pigen_fail("draft 0 primitive declarations allow one uninitialized name");
		}
	}

	payload_end = fifo_payload_end ? fifo_payload_end : pigen_trim_end(after_keyword, name);
	pigen_append(output, "\t");
	append_data_type(output, after_keyword, payload_end);
	pigen_append(output, " ");
	pigen_append_range(output, name, name_length);
	pigen_append(output, ";\n\n");

	if (pigen_transfer_type_has_storage(transfer_type))
	{
		/* A port payload is written directly by its source always_ff block.
		 * Keeping it out of a wrapper primitive lets synchronous RAM reads infer
		 * without a write-enable on the payload register. */
		if (transfer_type == 'h')
		{
			pigen_append(output, "\tlogic ");
			pigen_append_control_name(output, name, name_length, "valid");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "out_ready");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "clear");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "discard");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "force_valid");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "force_invalid");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, name, name_length, "force_after_transfer");
			pigen_append(output, ";\n\tassign ");
			pigen_append_control_name(output, name, name_length, "out_ready");
			pigen_append(output, " = 1'b1;\n\n");
			pigen_add_primitive(primitives, name, name_length, transfer_type, 1);
			pigen_set_port_metadata(primitives, name, name_length, after_keyword,
				(size_t)(payload_end - after_keyword), NULL, 0, 0);
			return;
		}
		descriptor = pigen_transfer_type_descriptor_get(transfer_type);
		primitive_module = descriptor->primitive_module;
		pigen_append(output, "\ttypedef ");
		append_data_type(output, after_keyword, payload_end);
		pigen_append(output, " ");
		pigen_append_control_name(output, name, name_length, "payload_t");
		pigen_append(output, ";\n");
		pigen_append(output, "\tlogic ");
		pigen_append_control_name(output, name, name_length, "in_valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "in_ready");
		pigen_append(output, ";\n\t");
		append_data_type(output, after_keyword, payload_end);
		pigen_append(output, " ");
		pigen_append_control_name(output, name, name_length, "packet_in");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "out_ready");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "clear");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "discard");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "force_valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "force_invalid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, name, name_length, "force_after_transfer");
		pigen_append(output, ";\n\n\t");
		if (transfer_type == 'i')
		{
			/* An ingress has the standard destination endpoint, but no storage:
			 * its `in_ready` is driven by the consuming pipeline stage. */
			pigen_add_primitive(primitives, name, name_length, transfer_type, 1);
			pigen_set_port_metadata(primitives, name, name_length, after_keyword,
				(size_t)(payload_end - after_keyword), NULL, 0, 0);
			return;
		}
		pigen_append(output, primitive_module);
		pigen_append(output, " #(\n\t\t.PAYLOAD_T(");
		append_data_type(output, after_keyword, payload_end);
		pigen_append(output, ")");

		if (transfer_type == 'f')
		{
			pigen_append(output, ",\n\t\t.DEPTH(");
			pigen_append_range(output, fifo_depth, fifo_depth_length);
			pigen_append(output, ")");
		}

		pigen_append(output, "\n\t)\n\t");
		pigen_append_control_name(output, name, name_length, "buffer");
		pigen_append(output, "\n\t(\n\t\t.clk(clk),\n\t\t.reset(");
		pigen_append(output, reset_connection);
		pigen_append(output, "),\n\t\t.clear(");
		pigen_append_control_name(output, name, name_length, "clear");
		pigen_append(output, "),\n\t\t.discard(");
		pigen_append_control_name(output, name, name_length, "discard");
		pigen_append(output, "),\n\t\t.force_valid(");
		pigen_append_control_name(output, name, name_length, "force_valid");
		pigen_append(output, "),\n\t\t.force_invalid(");
		pigen_append_control_name(output, name, name_length, "force_invalid");
		pigen_append(output, "),\n\t\t.force_after_transfer(");
		pigen_append_control_name(output, name, name_length, "force_after_transfer");
		pigen_append(output, "),\n\t\t.in_valid(");
		pigen_append_control_name(output, name, name_length, "in_valid");
		pigen_append(output, "),\n\t\t.in_ready(");
		pigen_append_control_name(output, name, name_length, "in_ready");
		pigen_append(output, "),\n\t\t.packet_in(");
		pigen_append_control_name(output, name, name_length, "packet_in");
		pigen_append(output, "),\n\t\t.out_valid(");
		pigen_append_control_name(output, name, name_length, "valid");
		pigen_append(output, "),\n\t\t.out_ready(");
		pigen_append_control_name(output, name, name_length, "out_ready");
		pigen_append(output, "),\n\t\t.packet_out(");
		pigen_append_range(output, name, name_length);
		pigen_append(output, ")\n\t);\n\n");

		pigen_add_primitive(primitives, name, name_length, transfer_type, 1);
		pigen_set_port_metadata(primitives, name, name_length, after_keyword,
			(size_t)(payload_end - after_keyword), transfer_type == 'f' ? fifo_depth : NULL,
			transfer_type == 'f' ? fifo_depth_length : 0, 0);
		return;
	}

	pigen_add_primitive(primitives, name, name_length, transfer_type, 1);
	pigen_set_port_metadata(primitives, name, name_length, after_keyword,
		(size_t)(payload_end - after_keyword), NULL, 0, 0);
}

static int is_port_direction(const char *start, const char *end, const char *direction)
{
	size_t length = strlen(direction);
	
	return (size_t)(end - start) >= length && !memcmp(start, direction, length) &&
		(start + length == end || !pigen_is_identifier_char(start[length]));
}

static void emit_port_item(pigen_string *output, const char *start, const char *end, int trailing_comma, pigen_primitives *primitives)
{
	const char *keyword;
	const char *after_keyword;
	const char *name;
	const char *trimmed_start = pigen_skip_spaces(start, end);
	size_t name_length;
	char transfer_type = pigen_declaration_transfer_type(trimmed_start, end, &keyword, &after_keyword);
	int is_input = is_port_direction(trimmed_start, end, "input");
	int is_output = is_port_direction(trimmed_start, end, "output");
	const char *fifo_depth;
	const char *fifo_payload_end = NULL;
	const char *payload_end;
	size_t fifo_depth_length;
	
	pigen_append(output, "\t\t");

	if (!transfer_type)
	{
		pigen_append_range(output, trimmed_start, (size_t)(end - trimmed_start));
		pigen_append(output, trailing_comma ? ",\n" : "\n");
		return;
	}

	/* `logic` stays an ordinary SV boundary signal, but has reg signal semantics internally. */
	if (transfer_type == 'l')
	{
		if (!last_identifier(after_keyword, end, &name, &name_length))
			pigen_fail("logic port needs a name");

		pigen_append_range(output, trimmed_start, (size_t)(end - trimmed_start));
		pigen_append(output, trailing_comma ? ",\n" : "\n");
		pigen_add_primitive(primitives, name, name_length, transfer_type, 0);
		return;
	}

	if (transfer_type == 'f')
		after_keyword = fifo_payload_start(after_keyword, end, &fifo_depth, &fifo_depth_length, &fifo_payload_end);

	after_keyword = payload_start(after_keyword, end);
	end = pigen_trim_end(after_keyword, end);

	if (!last_identifier(after_keyword, end, &name, &name_length))
		pigen_fail("primitive port needs a name");

	if (!is_input && !is_output)
		pigen_fail("primitive port needs input or output direction");

	payload_end = fifo_payload_end ? fifo_payload_end : pigen_trim_end(after_keyword, name);
	pigen_append(output, is_input ? "input  " : "output ");
	append_data_type(output, after_keyword, payload_end);
	pigen_append(output, " ");
	pigen_append_range(output, name, name_length);
	pigen_append(output, ",\n\t\t");
	pigen_append(output, is_input ? "input  logic " : "output logic ");
	append_port_control_name(output, name, name_length, "valid");
	pigen_append(output, ",\n\t\t");
	pigen_append(output, is_input ? "output logic " : "input  logic ");
	append_port_control_name(output, name, name_length, "ready");
	pigen_append(output, trailing_comma ? ",\n" : "\n");

	pigen_add_primitive(primitives, name, name_length, transfer_type, 0);
	pigen_set_port_metadata(primitives, name, name_length, after_keyword, (size_t)(payload_end - after_keyword), transfer_type == 'f' ? fifo_depth : NULL, transfer_type == 'f' ? fifo_depth_length : 0, is_output);
}

void pigen_emit_ports(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives)
{
	const char *item = start;
	const char *cursor;
	int depth = 0;
	
	for (cursor = start; ; cursor++)
	{
		int is_split = cursor == end || (cursor < end && *cursor == ',' && depth == 0);
		
		if (is_split)
		{
			emit_port_item(output, item, cursor, cursor != end, primitives);
			item = cursor + 1;
		}

		if (cursor == end)
			break;

		if (*cursor == '(' || *cursor == '[' || *cursor == '{')
			depth++;
		else if (*cursor == ')' || *cursor == ']' || *cursor == '}')
			depth--;
	}
}

void pigen_emit_port_adapters(pigen_string *output, pigen_primitives *primitives)
{
	size_t i;

	for (i = 0; i < primitives->count; i++)
	{
		pigen_primitive *primitive = &primitives->items[i];
		const pigen_transfer_type_descriptor *descriptor;
		const char *module_name;
		size_t name_length;

		name_length = strlen(primitive->name);

		if (primitive->transfer_type == 'l')
			continue;

		if (!pigen_transfer_type_has_storage(primitive->transfer_type))
		{
			/* Static signal controls are constants, not private nets. */
			if (primitive->is_output)
			{
				pigen_append(output, "\tassign ");
				append_port_control_name(output, primitive->name, name_length, "valid");
				pigen_append(output, " = 1'b1;\n\n");
			}
			else
			{
				pigen_append(output, "\tassign ");
				append_port_control_name(output, primitive->name, name_length, "ready");
				pigen_append(output, primitive->transfer_type == 'r' ? " = 1'b1;\n\n" : " = 1'b0;\n\n");
			}

			continue;
		}

		if (!primitive->is_output)
		{
			pigen_append(output, "\tlogic ");
			pigen_append_control_name(output, primitive->name, name_length, "valid");
			pigen_append(output, ";\n\tlogic ");
			pigen_append_control_name(output, primitive->name, name_length, "out_ready");
			pigen_append(output, ";\n\n\tassign ");
			pigen_append_control_name(output, primitive->name, name_length, "valid");
			pigen_append(output, " = ");
			append_port_control_name(output, primitive->name, name_length, "valid");
			pigen_append(output, ";\n\tassign ");
			append_port_control_name(output, primitive->name, name_length, "ready");
			pigen_append(output, " = ");
			pigen_append_control_name(output, primitive->name, name_length, "out_ready");
			pigen_append(output, ";\n\n");
			continue;
		}

		descriptor = pigen_transfer_type_descriptor_get(primitive->transfer_type);
		module_name = descriptor->primitive_module;

		pigen_append(output, "\ttypedef ");
		append_data_type(output, primitive->data_type, primitive->data_type + strlen(primitive->data_type));
		pigen_append(output, " ");
		pigen_append_control_name(output, primitive->name, name_length, "payload_t");
		pigen_append(output, ";\n");
		pigen_append(output, "\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "in_valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "in_ready");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "out_ready");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "clear");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "discard");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "force_valid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "force_invalid");
		pigen_append(output, ";\n\tlogic ");
		pigen_append_control_name(output, primitive->name, name_length, "force_after_transfer");
		pigen_append(output, ";\n\t");
		append_data_type(output, primitive->data_type, primitive->data_type + strlen(primitive->data_type));
		pigen_append(output, " ");
		pigen_append_control_name(output, primitive->name, name_length, "packet_in");
		pigen_append(output, ";\n\n\t");
		pigen_append(output, module_name);
		pigen_append(output, " #(\n\t\t.PAYLOAD_T(");
		append_data_type(output, primitive->data_type, primitive->data_type + strlen(primitive->data_type));
		pigen_append(output, ")");

		if (primitive->transfer_type == 'f')
		{
			pigen_append(output, ",\n\t\t.DEPTH(");
			pigen_append(output, primitive->fifo_depth);
			pigen_append(output, ")");
		}

		pigen_append(output, "\n\t)\n\t");
		pigen_append_control_name(output, primitive->name, name_length, "buffer");
		pigen_append(output, "\n\t(\n\t\t.clk(clk),\n\t\t.reset(");
		pigen_append(output, reset_connection);
		pigen_append(output, "),\n\t\t.clear(");
		pigen_append_control_name(output, primitive->name, name_length, "clear");
		pigen_append(output, "),\n\t\t.discard(");
		pigen_append_control_name(output, primitive->name, name_length, "discard");
		pigen_append(output, "),\n\t\t.force_valid(");
		pigen_append_control_name(output, primitive->name, name_length, "force_valid");
		pigen_append(output, "),\n\t\t.force_invalid(");
		pigen_append_control_name(output, primitive->name, name_length, "force_invalid");
		pigen_append(output, "),\n\t\t.force_after_transfer(");
		pigen_append_control_name(output, primitive->name, name_length, "force_after_transfer");
		pigen_append(output, "),\n\t\t.in_valid(");
		pigen_append_control_name(output, primitive->name, name_length, "in_valid");
		pigen_append(output, "),\n\t\t.in_ready(");
		pigen_append_control_name(output, primitive->name, name_length, "in_ready");
		pigen_append(output, "),\n\t\t.packet_in(");
		pigen_append_control_name(output, primitive->name, name_length, "packet_in");
		pigen_append(output, "),\n\t\t.out_valid(");
		pigen_append_control_name(output, primitive->name, name_length, "valid");
		pigen_append(output, "),\n\t\t.out_ready(");
		pigen_append_control_name(output, primitive->name, name_length, "out_ready");
		pigen_append(output, "),\n\t\t.packet_out(");
		pigen_append(output, primitive->name);
		pigen_append(output, ")\n\t);\n\n");
		pigen_append(output, "\tassign ");
		append_port_control_name(output, primitive->name, name_length, "valid");
		pigen_append(output, " = ");
		pigen_append_control_name(output, primitive->name, name_length, "valid");
		pigen_append(output, ";\n\tassign ");
		pigen_append_control_name(output, primitive->name, name_length, "out_ready");
		pigen_append(output, " = ");
		append_port_control_name(output, primitive->name, name_length, "ready");
		pigen_append(output, ";\n\n");

		primitive->is_internal = 1;
	}
}
