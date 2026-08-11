/* Lowering for top-level pipeline and fabric language blocks. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/blocks.h"
#include "pigen/lexer.h"
#include "pigen/util.h"

typedef struct {
	const char *source;
	pigen_tokens tokens;
	size_t at;
} block_parser;

typedef struct { char *name; char *value; } block_parameter;

typedef struct {
	char *name;
	char *upper;
	char *lower;
	char *base;
	int is_signed;
} pipeline_signal;

typedef struct {
	char *name;
	pipeline_signal signal;
	int is_typed;
} pipeline_item;

typedef struct {
	pipeline_signal signal;
	char *initializer;
} pipeline_declaration;

typedef struct {
	char *name;
	pipeline_signal *inputs;
	size_t input_count;
	pipeline_signal *outputs;
	size_t output_count;
	pipeline_declaration *declarations;
	size_t declaration_count;
	char *body;
	int force_skid;
	int suppress_skid;
} pipeline_stage;

typedef struct {
	char *name;
	block_parameter *parameters;
	size_t parameter_count;
	pipeline_stage *stages;
	size_t stage_count;
	int skid_step;
} pipeline_block;

typedef struct { size_t start; size_t end; } source_exclusion;

typedef struct { char *instance; char *port; char *handle; } fabric_source;
typedef struct { char *instance; char *port; } fabric_destination;

typedef struct {
	fabric_source source;
	fabric_destination destination;
	char *recognized_source;
	int tier;
	int direct;
	size_t line;
	unsigned long long path_word;
	unsigned long long delivered_word;
	int hops;
} fabric_connection;

typedef struct {
	int direction; /* 0 = destination, 1 = source (matching lexical order). */
	fabric_source source;
	fabric_destination destination;
} fabric_endpoint;

typedef struct {
	int kind; /* 0 = unset/dummy, 1 = router, 2 = endpoint */
	int target;
	int port;
} fabric_attachment;

typedef struct { fabric_attachment ports[3]; } fabric_router;

typedef struct {
	fabric_router *routers;
	size_t router_count;
	fabric_endpoint *endpoints;
	size_t endpoint_count;
	int *endpoint_router;
	int *endpoint_port;
	int path_width;
} fabric_topology;

typedef struct {
	char *name;
	block_parameter *parameters;
	size_t parameter_count;
	fabric_connection *connections;
	size_t connection_count;
	int router_depth;
	int endpoint_depth;
} fabric_block;

static const pigen_token *current(block_parser *parser)
{
	return &parser->tokens.items[parser->at];
}

static int token_is(block_parser *parser, size_t at, const char *text)
{
	return at < parser->tokens.count &&
		pigen_token_is(parser->source, &parser->tokens.items[at], text);
}

static void position_at(block_parser *parser, size_t at)
{
	pigen_set_diagnostic_position(parser->source + parser->tokens.items[at].span.start);
}

static void expect(block_parser *parser, const char *text, const char *message)
{
	position_at(parser, parser->at);
	if (!token_is(parser, parser->at, text))
		pigen_fail(message);
	parser->at++;
}

static char *token_copy(block_parser *parser, size_t at)
{
	pigen_span span = parser->tokens.items[at].span;
	return pigen_copy_range(parser->source + span.start, span.end - span.start);
}

static int valid_identifier_token(block_parser *parser, size_t at)
{
	pigen_span span;
	unsigned char first;
	if (at >= parser->tokens.count) return 0;
	span = parser->tokens.items[at].span;
	if (span.start == span.end) return 0;
	first = (unsigned char)parser->source[span.start];
	return isalpha(first) || first == '_';
}

static char *trimmed_copy(const char *source, size_t start, size_t end)
{
	const char *first = pigen_skip_spaces(source + start, source + end);
	const char *last = pigen_trim_end(first, source + end);
	return pigen_copy_range(first, (size_t)(last - first));
}

static char *token_range_copy(block_parser *parser, size_t first, size_t after)
{
	if (first == after)
		return pigen_copy_range("", 0);
	return trimmed_copy(parser->source, parser->tokens.items[first].span.start,
		parser->tokens.items[after - 1].span.end);
}

static char *parse_identifier(block_parser *parser, const char *message)
{
	char *result;
	position_at(parser, parser->at);
	if (!valid_identifier_token(parser, parser->at))
		pigen_fail(message);
	result = token_copy(parser, parser->at);
	parser->at++;
	return result;
}

static void append_parameter(block_parameter **items, size_t *count, block_parameter item)
{
	*items = pigen_resize(*items, (*count + 1) * sizeof(**items));
	(*items)[(*count)++] = item;
}

static int named_parameter(block_parameter *items, size_t count, const char *name)
{
	for (size_t index = 0; index < count; index++)
		if (!strcmp(items[index].name, name))
			return 1;
	return 0;
}

static void parse_parameters(block_parser *parser, block_parameter **items, size_t *count)
{
	expect(parser, "#", "expected parameter list or `begin`");
	expect(parser, "(", "expected `(` after `#`");
	while (!token_is(parser, parser->at, ")"))
	{
		block_parameter parameter;
		size_t value_start;
		size_t value_end;
		int depth = 0;
		expect(parser, "parameter", "expected `parameter integer NAME = DEFAULT`");
		expect(parser, "integer", "block parameters must use `parameter integer`");
		parameter.name = parse_identifier(parser, "expected parameter name");
		if (named_parameter(*items, *count, parameter.name))
			pigen_fail("duplicate block parameter");
		expect(parser, "=", "expected `=` in block parameter");
		value_start = parser->at;
		while (current(parser)->kind != PIGEN_TOKEN_EOF)
		{
			if (!depth && (token_is(parser, parser->at, ",") || token_is(parser, parser->at, ")")))
				break;
			if (token_is(parser, parser->at, "(") || token_is(parser, parser->at, "[") || token_is(parser, parser->at, "{")) depth++;
			if (token_is(parser, parser->at, ")") || token_is(parser, parser->at, "]") || token_is(parser, parser->at, "}")) depth--;
			parser->at++;
		}
		value_end = parser->at;
		if (value_start == value_end)
			pigen_fail("block parameter default must not be empty");
		parameter.value = token_range_copy(parser, value_start, value_end);
		append_parameter(items, count, parameter);
		if (token_is(parser, parser->at, ",")) parser->at++;
	}
	expect(parser, ")", "unterminated block parameter list");
}

static void append_item(pipeline_item **items, size_t *count, pipeline_item item)
{
	for (size_t index = 0; index < *count; index++)
		if (!strcmp((*items)[index].name, item.name))
			pigen_fail("duplicate tuple value");
	*items = pigen_resize(*items, (*count + 1) * sizeof(**items));
	(*items)[(*count)++] = item;
}

static pipeline_signal parse_signal_tokens(block_parser *parser, size_t first, size_t after)
{
	pipeline_signal signal = {0};
	size_t at = first;
	size_t upper_start;
	size_t colon;
	size_t lower_start;
	size_t close;

	position_at(parser, first);
	signal.base = pigen_copy_range("logic", 5);
	if (at < after && (token_is(parser, at, "logic") || token_is(parser, at, "wire")))
	{
		free(signal.base);
		signal.base = token_copy(parser, at++);
	}
	if (at < after && (token_is(parser, at, "signed") || token_is(parser, at, "unsigned")))
	{
		signal.is_signed = token_is(parser, at, "signed");
		at++;
	}
	if (at >= after || !token_is(parser, at, "["))
		pigen_fail("expected a packed declaration such as `logic signed [W-1:0] sample`");
	upper_start = ++at;
	colon = after;
	close = after;
	for (; at < after; at++)
	{
		if (token_is(parser, at, ":") && colon == after) colon = at;
		if (token_is(parser, at, "]")) { close = at; break; }
	}
	if (colon == after || close == after || colon == upper_start || colon + 1 == close)
		pigen_fail("packed declaration requires an upper and lower bound");
	lower_start = colon + 1;
	if (close + 2 != after || !valid_identifier_token(parser, close + 1))
		pigen_fail("packed declaration requires exactly one signal name");
	signal.upper = token_range_copy(parser, upper_start, colon);
	signal.lower = token_range_copy(parser, lower_start, close);
	signal.name = token_copy(parser, close + 1);
	return signal;
}

static void parse_tuple(block_parser *parser, pipeline_item **items, size_t *count)
{
	size_t item_start;
	expect(parser, "{", "expected `{` to begin tuple");
	while (!token_is(parser, parser->at, "}"))
	{
		size_t item_end;
		int depth = 0;
		pipeline_item item = {0};
		item_start = parser->at;
		while (current(parser)->kind != PIGEN_TOKEN_EOF)
		{
			if (!depth && (token_is(parser, parser->at, ",") || token_is(parser, parser->at, "}"))) break;
			if (token_is(parser, parser->at, "[") || token_is(parser, parser->at, "(") || token_is(parser, parser->at, "{")) depth++;
			else if (token_is(parser, parser->at, "]") || token_is(parser, parser->at, ")") || token_is(parser, parser->at, "}")) depth--;
			parser->at++;
		}
		item_end = parser->at;
		if (item_start == item_end)
			pigen_fail("pipeline tuples must not contain empty values");
		if (item_end == item_start + 1 && valid_identifier_token(parser, item_start))
			item.name = token_copy(parser, item_start);
		else
		{
			item.signal = parse_signal_tokens(parser, item_start, item_end);
			item.name = pigen_copy_range(item.signal.name, strlen(item.signal.name));
			item.is_typed = 1;
		}
		append_item(items, count, item);
		if (token_is(parser, parser->at, ",")) parser->at++;
	}
	expect(parser, "}", "unterminated pipeline tuple");
	if (!*count)
		pigen_fail("stage input and yielded tuples must not be empty");
}

static int signal_declaration_at(block_parser *parser, size_t at, size_t end,
	pipeline_declaration *declaration, size_t *after)
{
	size_t first = at;
	size_t close;
	size_t name;
	size_t semi;
	size_t initializer = 0;
	if (at < end && (token_is(parser, at, "logic") || token_is(parser, at, "wire"))) at++;
	if (at < end && (token_is(parser, at, "signed") || token_is(parser, at, "unsigned"))) at++;
	if (at >= end || !token_is(parser, at, "[")) return 0;
	for (close = at + 1; close < end && !token_is(parser, close, "]"); close++) ;
	if (close == end || close + 1 == end || !valid_identifier_token(parser, close + 1)) return 0;
	name = close + 1;
	semi = name + 1;
	if (semi < end && token_is(parser, semi, "="))
	{
		initializer = ++semi;
		while (semi < end && !token_is(parser, semi, ";")) semi++;
	}
	if (semi >= end || !token_is(parser, semi, ";")) return 0;
	declaration->signal = parse_signal_tokens(parser, first, name + 1);
	declaration->initializer = initializer ? token_range_copy(parser, initializer, semi) : NULL;
	*after = semi + 1;
	return 1;
}

static void add_exclusion(source_exclusion **items, size_t *count, size_t start, size_t end)
{
	*items = pigen_resize(*items, (*count + 1) * sizeof(**items));
	(*items)[(*count)++] = (source_exclusion){start, end};
}

static int declaration_named(pipeline_declaration *items, size_t count, const char *name)
{
	for (size_t index = 0; index < count; index++)
		if (!strcmp(items[index].signal.name, name)) return (int)index + 1;
	return 0;
}

static pipeline_signal copy_signal_with_name(const pipeline_signal *source, const char *name)
{
	pipeline_signal result;
	result.name = pigen_copy_range(name, strlen(name));
	result.upper = pigen_copy_range(source->upper, strlen(source->upper));
	result.lower = pigen_copy_range(source->lower, strlen(source->lower));
	result.base = pigen_copy_range(source->base, strlen(source->base));
	result.is_signed = source->is_signed;
	return result;
}

static int signal_named(pipeline_signal *items, size_t count, const char *name)
{
	for (size_t index = 0; index < count; index++)
		if (!strcmp(items[index].name, name)) return (int)index + 1;
	return 0;
}

static char *signal_width(const pipeline_signal *signal)
{
	char *end;
	long upper;
	pigen_string width = {0};
	if (!strcmp(signal->lower, "0"))
	{
		upper = strtol(signal->upper, &end, 10);
		if (*signal->upper && !*end)
		{
			pigen_append_format(&width, "%ld", upper + 1);
			return width.data;
		}
		{
			size_t length = strlen(signal->upper);
			while (length && isspace((unsigned char)signal->upper[length - 1])) length--;
			if (length >= 2 && signal->upper[length - 1] == '1')
			{
				size_t minus = length - 2;
				while (minus && isspace((unsigned char)signal->upper[minus])) minus--;
				if (signal->upper[minus] == '-')
				{
					const char *finish = signal->upper + minus;
					finish = pigen_trim_end(signal->upper, finish);
					return pigen_copy_range(signal->upper, (size_t)(finish - signal->upper));
				}
			}
		}
	}
	pigen_append_format(&width, "(%s) - (%s) + 1", signal->upper, signal->lower);
	return width.data;
}

static char *compact_text(const char *text)
{
	pigen_string result = {0};
	for (; *text; text++)
		if (!isspace((unsigned char)*text)) pigen_append_range(&result, text, 1);
	return result.data ? result.data : pigen_copy_range("", 0);
}

static void resolve_stage(pipeline_stage *stage, pipeline_item *input_items, size_t input_count,
	pipeline_item *output_items, size_t output_count, const pipeline_stage *previous)
{
	stage->inputs = pigen_resize(NULL, input_count * sizeof(*stage->inputs));
	stage->input_count = input_count;
	for (size_t index = 0; index < input_count; index++)
	{
		int declaration = declaration_named(stage->declarations, stage->declaration_count, input_items[index].name);
		if (input_items[index].is_typed)
		{
			if (declaration) pigen_fail("a value typed in the stage header must not be redeclared in the body");
			stage->inputs[index] = copy_signal_with_name(&input_items[index].signal, input_items[index].name);
		}
		else if (declaration)
		{
			if (stage->declarations[declaration - 1].initializer)
				pigen_fail("stage input cannot have an initializer");
			stage->inputs[index] = copy_signal_with_name(&stage->declarations[declaration - 1].signal, input_items[index].name);
		}
		else if (previous && index < previous->output_count)
			stage->inputs[index] = copy_signal_with_name(&previous->outputs[index], input_items[index].name);
		else
			pigen_fail("bare input in the first stage requires a declaration in the stage body");
	}
	stage->outputs = pigen_resize(NULL, output_count * sizeof(*stage->outputs));
	stage->output_count = output_count;
	for (size_t index = 0; index < output_count; index++)
	{
		int declaration = declaration_named(stage->declarations, stage->declaration_count, output_items[index].name);
		int input = signal_named(stage->inputs, stage->input_count, output_items[index].name);
		if (output_items[index].is_typed)
		{
			if (declaration) pigen_fail("a value typed in the stage header must not be redeclared in the body");
			stage->outputs[index] = copy_signal_with_name(&output_items[index].signal, output_items[index].name);
			if (!input)
			{
				stage->declarations = pigen_resize(stage->declarations,
					(stage->declaration_count + 1) * sizeof(*stage->declarations));
				stage->declarations[stage->declaration_count++] = (pipeline_declaration){
					copy_signal_with_name(&output_items[index].signal, output_items[index].name), NULL};
			}
		}
		else if (input)
			stage->outputs[index] = copy_signal_with_name(&stage->inputs[input - 1], output_items[index].name);
		else if (declaration)
			stage->outputs[index] = copy_signal_with_name(&stage->declarations[declaration - 1].signal, output_items[index].name);
		else
			pigen_fail("bare yielded value requires a declaration or matching input");
	}
	/* Inputs are unpacked from packet_in and must not be emitted twice. */
	for (size_t index = 0; index < stage->declaration_count; )
	{
		if (signal_named(stage->inputs, stage->input_count, stage->declarations[index].signal.name))
		{
			memmove(&stage->declarations[index], &stage->declarations[index + 1],
				(stage->declaration_count - index - 1) * sizeof(*stage->declarations));
			stage->declaration_count--;
		}
		else index++;
	}
	if (previous)
	{
		if (previous->output_count != stage->input_count)
			pigen_fail("pipeline stage tuple arity does not match the previous yield");
		for (size_t index = 0; index < stage->input_count; index++)
		{
			char *produced_width = signal_width(&previous->outputs[index]);
			char *consumed_width = signal_width(&stage->inputs[index]);
			char *produced = compact_text(produced_width);
			char *consumed = compact_text(consumed_width);
			if (strcmp(produced, consumed))
				pigen_fail("pipeline stage tuple width does not match the previous yield");
			free(produced_width); free(consumed_width); free(produced); free(consumed);
		}
	}
}

static char *body_without_exclusions(const char *source, size_t start, size_t end,
	source_exclusion *exclusions, size_t count)
{
	pigen_string body = {0};
	size_t cursor = start;
	for (size_t index = 0; index < count; index++)
	{
		if (exclusions[index].start > cursor)
			pigen_append_range(&body, source + cursor, exclusions[index].start - cursor);
		for (size_t at = exclusions[index].start; at < exclusions[index].end; at++)
			if (source[at] == '\n') pigen_append_range(&body, "\n", 1);
		cursor = exclusions[index].end;
	}
	if (cursor < end) pigen_append_range(&body, source + cursor, end - cursor);
	return body.data ? body.data : pigen_copy_range("", 0);
}

static pipeline_stage parse_stage(block_parser *parser, const pipeline_stage *previous)
{
	pipeline_stage stage = {0};
	pipeline_item *input_items = NULL, *output_items = NULL;
	size_t input_count = 0, output_count = 0;
	size_t body_start, body_end;
	size_t scan;
	int depth = 0;
	source_exclusion *exclusions = NULL;
	size_t exclusion_count = 0;

	expect(parser, "stage", "expected `stage` or `endpipeline`");
	stage.name = parse_identifier(parser, "expected stage name");
	parse_tuple(parser, &input_items, &input_count);
	expect(parser, "yields", "expected `yields` after stage input tuple");
	parse_tuple(parser, &output_items, &output_count);
	expect(parser, "begin", "expected `begin` after stage yielded tuple");
	body_start = parser->tokens.items[parser->at - 1].span.end;
	for (scan = parser->at; scan < parser->tokens.count &&
		parser->tokens.items[scan].kind != PIGEN_TOKEN_EOF; scan++)
	{
		if (token_is(parser, scan, "endstage")) break;
	}
	if (scan >= parser->tokens.count || !token_is(parser, scan, "endstage"))
		pigen_fail("unterminated pipeline stage");
	body_end = parser->tokens.items[scan].span.start;
	for (size_t at = parser->at; at < scan; )
	{
		pipeline_declaration declaration = {0};
		size_t after;
		if (!depth && (token_is(parser, at, "skid") || token_is(parser, at, "no_skid")))
		{
			int force = token_is(parser, at, "skid");
			size_t directive_start = parser->tokens.items[at].span.start;
			size_t directive_end = parser->tokens.items[at].span.end;
			if (at + 1 < scan && token_is(parser, at + 1, ";")) { directive_end = parser->tokens.items[at + 1].span.end; at++; }
			if (force) stage.force_skid = 1; else stage.suppress_skid = 1;
			add_exclusion(&exclusions, &exclusion_count, directive_start, directive_end);
			at++;
			continue;
		}
		if (!depth && signal_declaration_at(parser, at, scan, &declaration, &after))
		{
			if (declaration_named(stage.declarations, stage.declaration_count, declaration.signal.name))
				pigen_fail("duplicate local declaration in pipeline stage");
			stage.declarations = pigen_resize(stage.declarations,
				(stage.declaration_count + 1) * sizeof(*stage.declarations));
			stage.declarations[stage.declaration_count++] = declaration;
			add_exclusion(&exclusions, &exclusion_count, parser->tokens.items[at].span.start,
				parser->tokens.items[after - 1].span.end);
			at = after;
			continue;
		}
		if (token_is(parser, at, "begin")) depth++;
		else if (token_is(parser, at, "end") && depth) depth--;
		at++;
	}
	if (stage.force_skid && stage.suppress_skid)
		pigen_fail("stage cannot contain both `skid` and `no_skid`");
	stage.body = body_without_exclusions(parser->source, body_start, body_end, exclusions, exclusion_count);
	parser->at = scan + 1;
	resolve_stage(&stage, input_items, input_count, output_items, output_count, previous);
	free(exclusions);
	return stage;
}

static pipeline_block parse_pipeline(block_parser *parser, size_t *source_end)
{
	pipeline_block pipeline = {0};
	int has_skid_step = 0;
	pipeline.skid_step = 4;
	expect(parser, "pipeline", "expected `pipeline`");
	pipeline.name = parse_identifier(parser, "expected pipeline name");
	if (token_is(parser, parser->at, "#"))
		parse_parameters(parser, &pipeline.parameters, &pipeline.parameter_count);
	expect(parser, "begin", "expected `begin` after pipeline declaration");
	while (!token_is(parser, parser->at, "endpipeline"))
	{
		if (token_is(parser, parser->at, "option"))
		{
			char *name;
			char *value;
			char *end;
			long parsed;
			size_t value_start;
			parser->at++;
			name = parse_identifier(parser, "expected pipeline option name");
			expect(parser, "=", "expected `=` in pipeline option");
			value_start = parser->at;
			while (!token_is(parser, parser->at, ";") && current(parser)->kind != PIGEN_TOKEN_EOF) parser->at++;
			value = token_range_copy(parser, value_start, parser->at);
			expect(parser, ";", "pipeline option must end with semicolon");
			if (strcmp(name, "skid_step")) pigen_fail("unknown pipeline option");
			if (has_skid_step) pigen_fail("duplicate pipeline option `skid_step`");
			parsed = strtol(value, &end, 10);
			if (!*value || *end || parsed < 0 || parsed > 0x7fffffffL)
				pigen_fail("pipeline `skid_step` must be a non-negative integer");
			pipeline.skid_step = (int)parsed;
			has_skid_step = 1;
			free(name); free(value);
			continue;
		}
		pipeline_stage stage = parse_stage(parser,
			pipeline.stage_count ? &pipeline.stages[pipeline.stage_count - 1] : NULL);
		for (size_t index = 0; index < pipeline.stage_count; index++)
			if (!strcmp(pipeline.stages[index].name, stage.name)) pigen_fail("duplicate pipeline stage");
		pipeline.stages = pigen_resize(pipeline.stages,
			(pipeline.stage_count + 1) * sizeof(*pipeline.stages));
		pipeline.stages[pipeline.stage_count++] = stage;
	}
	if (!pipeline.stage_count) pigen_fail("pipeline requires at least one stage");
	*source_end = current(parser)->span.end;
	parser->at++;
	return pipeline;
}

static int source_equal(const fabric_source *left, const fabric_source *right)
{
	return !strcmp(left->instance, right->instance) && !strcmp(left->port, right->port) &&
		!strcmp(left->handle, right->handle);
}

static int destination_equal(const fabric_destination *left, const fabric_destination *right)
{
	return !strcmp(left->instance, right->instance) && !strcmp(left->port, right->port);
}

static fabric_source parse_fabric_source(block_parser *parser)
{
	fabric_source source;
	source.instance = parse_identifier(parser, "fabric source requires `instance.port.handle`");
	expect(parser, ".", "fabric source requires `instance.port.handle`");
	source.port = parse_identifier(parser, "fabric source requires `instance.port.handle`");
	expect(parser, ".", "fabric source requires `instance.port.handle`");
	source.handle = parse_identifier(parser, "fabric source requires `instance.port.handle`");
	return source;
}

static fabric_destination parse_fabric_destination(block_parser *parser, char **recognized)
{
	fabric_destination destination;
	destination.instance = parse_identifier(parser, "fabric destination requires `instance.port`");
	expect(parser, ".", "fabric destination requires `instance.port`");
	destination.port = parse_identifier(parser, "fabric destination requires `instance.port`");
	*recognized = NULL;
	if (token_is(parser, parser->at, "."))
	{
		parser->at++;
		*recognized = parse_identifier(parser, "expected recognized source name");
	}
	return destination;
}

static fabric_connection parse_fabric_connection(block_parser *parser)
{
	fabric_connection connection = {0};
	connection.line = current(parser)->span.line;
	connection.source = parse_fabric_source(parser);
	if (token_is(parser, parser->at, ">"))
	{
		connection.direct = 1;
		parser->at++;
	}
	else
	{
		int dashes = 0;
		while (token_is(parser, parser->at, "-")) { dashes++; parser->at++; }
		if (token_is(parser, parser->at, "->")) { dashes++; parser->at++; }
		else if (token_is(parser, parser->at, ">")) parser->at++;
		else pigen_fail("fabric connection requires `>` or one or more `-` before `>`");
		if (!dashes) pigen_fail("fabric routed connection requires an arrow such as `->`");
		connection.tier = dashes;
	}
	connection.destination = parse_fabric_destination(parser, &connection.recognized_source);
	expect(parser, ";", "fabric connection must end with semicolon");
	return connection;
}

static int decimal_value(const char *text, int *result)
{
	char *end;
	long value = strtol(text, &end, 10);
	if (!*text || *end || value < 1 || value > 0x7fffffffL) return 0;
	*result = (int)value;
	return 1;
}

static void validate_fabric(const fabric_block *fabric)
{
	int has_payload_width = 0;
	if (!fabric->connection_count) pigen_fail("fabric requires at least one connection");
	for (size_t index = 0; index < fabric->parameter_count; index++)
		if (!strcmp(fabric->parameters[index].name, "PAYLOAD_W")) has_payload_width = 1;
	if (!has_payload_width) pigen_fail("fabric requires `parameter integer PAYLOAD_W = ...`");
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		for (size_t previous = 0; previous < index; previous++)
		{
			fabric_connection *other = &fabric->connections[previous];
			if (source_equal(&connection->source, &other->source))
				pigen_fail("fabric source handle is bound more than once");
			if (connection->recognized_source && other->recognized_source &&
				destination_equal(&connection->destination, &other->destination) &&
				!strcmp(connection->recognized_source, other->recognized_source))
				pigen_fail("fabric recognized source is bound more than once");
			if (destination_equal(&connection->destination, &other->destination) &&
				(connection->direct || other->direct))
				pigen_fail("direct fabric destination cannot appear in another connection");
		}
	}
}

static fabric_block parse_fabric(block_parser *parser, size_t *source_end)
{
	fabric_block fabric = {0};
	int has_router_depth = 0, has_endpoint_depth = 0, has_objective = 0;
	fabric.router_depth = 2;
	fabric.endpoint_depth = 2;
	expect(parser, "fabric", "expected `fabric`");
	fabric.name = parse_identifier(parser, "expected fabric name");
	if (token_is(parser, parser->at, "#"))
		parse_parameters(parser, &fabric.parameters, &fabric.parameter_count);
	expect(parser, "begin", "expected `begin` after fabric declaration");
	while (!token_is(parser, parser->at, "endfabric"))
	{
		if (token_is(parser, parser->at, "option"))
		{
			char *name;
			char *value;
			size_t value_start;
			size_t value_end;
			int parsed;
			parser->at++;
			name = parse_identifier(parser, "expected fabric option name");
			expect(parser, "=", "expected `=` in fabric option");
			value_start = parser->at;
			while (!token_is(parser, parser->at, ";") && current(parser)->kind != PIGEN_TOKEN_EOF) parser->at++;
			value_end = parser->at;
			expect(parser, ";", "fabric option must end with semicolon");
			if (value_start == value_end) pigen_fail("fabric option value must not be empty");
			value = token_range_copy(parser, value_start, value_end);
			if (!strcmp(name, "router_buffer_depth"))
			{
				if (has_router_depth) pigen_fail("duplicate fabric option `router_buffer_depth`");
				if (!decimal_value(value, &parsed) || parsed != 2) pigen_fail("V0 fixes `router_buffer_depth` at 2 entries");
				fabric.router_depth = parsed;
				has_router_depth = 1;
			}
			else if (!strcmp(name, "endpoint_fifo_depth"))
			{
				if (has_endpoint_depth) pigen_fail("duplicate fabric option `endpoint_fifo_depth`");
				if (!decimal_value(value, &parsed) || parsed != 2) pigen_fail("V0 fixes `endpoint_fifo_depth` at 2 entries");
				fabric.endpoint_depth = parsed;
				has_endpoint_depth = 1;
			}
			else if (!strcmp(name, "objective"))
			{
				if (has_objective) pigen_fail("duplicate fabric option `objective`");
				has_objective = 1;
			}
			else pigen_fail("unknown fabric option");
			free(name); free(value);
			continue;
		}
		fabric.connections = pigen_resize(fabric.connections,
			(fabric.connection_count + 1) * sizeof(*fabric.connections));
		fabric.connections[fabric.connection_count++] = parse_fabric_connection(parser);
	}
	validate_fabric(&fabric);
	*source_end = current(parser)->span.end;
	parser->at++;
	return fabric;
}

static int endpoint_compare(const void *left_, const void *right_)
{
	const fabric_endpoint *left = left_;
	const fabric_endpoint *right = right_;
	int comparison;
	if (left->direction != right->direction) return left->direction - right->direction;
	if (!left->direction)
	{
		comparison = strcmp(left->destination.instance, right->destination.instance);
		return comparison ? comparison : strcmp(left->destination.port, right->destination.port);
	}
	comparison = strcmp(left->source.instance, right->source.instance);
	if (comparison) return comparison;
	comparison = strcmp(left->source.port, right->source.port);
	return comparison ? comparison : strcmp(left->source.handle, right->source.handle);
}

static int endpoint_equal(const fabric_endpoint *left, const fabric_endpoint *right)
{
	if (left->direction != right->direction) return 0;
	return left->direction ? source_equal(&left->source, &right->source) :
		destination_equal(&left->destination, &right->destination);
}

static void add_endpoint(fabric_topology *topology, fabric_endpoint endpoint)
{
	for (size_t index = 0; index < topology->endpoint_count; index++)
		if (endpoint_equal(&topology->endpoints[index], &endpoint)) return;
	topology->endpoints = pigen_resize(topology->endpoints,
		(topology->endpoint_count + 1) * sizeof(*topology->endpoints));
	topology->endpoints[topology->endpoint_count++] = endpoint;
}

static int endpoint_index(const fabric_topology *topology, const fabric_endpoint *endpoint)
{
	for (size_t index = 0; index < topology->endpoint_count; index++)
		if (endpoint_equal(&topology->endpoints[index], endpoint)) return (int)index;
	pigen_fail("internal fabric endpoint lookup failed");
	return -1;
}

typedef struct { int kind; int index; } topology_node;

static topology_node make_topology_parent(fabric_topology *topology, topology_node left, topology_node right)
{
	int router;
	if (!left.kind) return right;
	if (!right.kind) return left;
	router = (int)topology->router_count++;
	topology->routers = pigen_resize(topology->routers,
		topology->router_count * sizeof(*topology->routers));
	topology->routers[router] = (fabric_router){0};
	topology_node children[2] = {left, right};
	for (int child = 0; child < 2; child++)
	{
		int port = child + 1;
		if (children[child].kind == 1)
		{
			topology->routers[router].ports[port] = (fabric_attachment){1, children[child].index, 0};
			topology->routers[children[child].index].ports[0] = (fabric_attachment){1, router, port};
		}
		else
		{
			topology->routers[router].ports[port] = (fabric_attachment){2, children[child].index, 0};
			topology->endpoint_router[children[child].index] = router;
			topology->endpoint_port[children[child].index] = port;
		}
	}
	return (topology_node){1, router};
}

static int attachment_matches_node(const fabric_attachment *attachment, int router_count, int node)
{
	if (attachment->kind == 1) return attachment->target == node;
	if (attachment->kind == 2) return router_count + attachment->target == node;
	return 0;
}

static int port_for_node(const fabric_topology *topology, int router, int node)
{
	for (int port = 0; port < 3; port++)
		if (attachment_matches_node(&topology->routers[router].ports[port],
			(int)topology->router_count, node)) return port;
	pigen_fail("internal fabric route traverses non-neighbor nodes");
	return -1;
}

static unsigned long long rotate_right_word(unsigned long long value, int width, int count)
{
	unsigned long long mask;
	if (width <= 0) return 0;
	mask = width == 64 ? ~0ULL : (1ULL << width) - 1;
	for (int index = 0; index < count % width; index++)
		value = ((value >> 1) | ((value & 1) << (width - 1))) & mask;
	return value;
}

static int trace_forward_endpoint(const fabric_topology *topology, int endpoint,
	unsigned long long path_word)
{
	int router = topology->endpoint_router[endpoint];
	int in_port = topology->endpoint_port[endpoint];
	for (size_t hop = 0; hop <= topology->router_count; hop++)
	{
		int bit = (int)(path_word & 1ULL);
		int out_port = bit ? (in_port + 2) % 3 : (in_port + 1) % 3;
		fabric_attachment attachment = topology->routers[router].ports[out_port];
		path_word = rotate_right_word(path_word, topology->path_width, 1);
		if (attachment.kind == 2) return attachment.target;
		if (attachment.kind != 1) pigen_fail("generated fabric route reached a dummy port");
		router = attachment.target;
		in_port = attachment.port;
	}
	pigen_fail("generated fabric route did not terminate");
	return -1;
}

static unsigned long long rotate_left_word(unsigned long long value, int width)
{
	unsigned long long mask;
	if (width <= 0) return 0;
	mask = width == 64 ? ~0ULL : (1ULL << width) - 1;
	return ((value << 1) | (value >> (width - 1))) & mask;
}

static int trace_reverse_endpoint(const fabric_topology *topology, int endpoint,
	unsigned long long delivered_word)
{
	int router = topology->endpoint_router[endpoint];
	int out_port = topology->endpoint_port[endpoint];
	for (size_t hop = 0; hop <= topology->router_count; hop++)
	{
		int bit = (int)((delivered_word >> (topology->path_width - 1)) & 1ULL);
		int in_port = bit ? (out_port + 1) % 3 : (out_port + 2) % 3;
		fabric_attachment attachment = topology->routers[router].ports[in_port];
		delivered_word = rotate_left_word(delivered_word, topology->path_width);
		if (attachment.kind == 2) return attachment.target;
		if (attachment.kind != 1) pigen_fail("generated reverse fabric route reached a dummy port");
		router = attachment.target;
		out_port = attachment.port;
	}
	pigen_fail("generated reverse fabric route did not terminate");
	return -1;
}

static void route_connection(fabric_topology *topology, fabric_connection *connection)
{
	fabric_endpoint source = {.direction = 1, .source = connection->source};
	fabric_endpoint destination = {.direction = 0, .destination = connection->destination};
	int source_index = endpoint_index(topology, &source);
	int destination_index_ = endpoint_index(topology, &destination);
	int node_count = (int)(topology->router_count + topology->endpoint_count);
	int start = (int)topology->router_count + source_index;
	int finish = (int)topology->router_count + destination_index_;
	int *previous = pigen_resize(NULL, (size_t)node_count * sizeof(*previous));
	int *queue = pigen_resize(NULL, (size_t)node_count * sizeof(*queue));
	int *path = pigen_resize(NULL, (size_t)node_count * sizeof(*path));
	int head = 0, tail = 0, path_count = 0;
	for (int index = 0; index < node_count; index++) previous[index] = -2;
	previous[start] = -1; queue[tail++] = start;
	while (head < tail)
	{
		int node = queue[head++];
		if (node == finish) break;
		if (node < (int)topology->router_count)
		{
			for (int port = 0; port < 3; port++)
			{
				fabric_attachment *attachment = &topology->routers[node].ports[port];
				int next;
				if (!attachment->kind) continue;
				next = attachment->kind == 1 ? attachment->target :
					(int)topology->router_count + attachment->target;
				if (previous[next] == -2) { previous[next] = node; queue[tail++] = next; }
			}
		}
		else
		{
			int endpoint = node - (int)topology->router_count;
			int next = topology->endpoint_router[endpoint];
			if (previous[next] == -2) { previous[next] = node; queue[tail++] = next; }
		}
	}
	if (previous[finish] == -2) pigen_fail("fabric topology did not connect route endpoints");
	for (int node = finish; node != -1; node = previous[node]) path[path_count++] = node;
	for (int left = 0, right = path_count - 1; left < right; left++, right--)
	{
		int temporary = path[left]; path[left] = path[right]; path[right] = temporary;
	}
	connection->path_word = 0;
	connection->hops = 0;
	for (int index = 1; index + 1 < path_count; index++)
	{
		int router = path[index];
		int in_port = port_for_node(topology, router, path[index - 1]);
		int out_port = port_for_node(topology, router, path[index + 1]);
		int bit = 0;
		if (out_port == (in_port + 1) % 3) bit = 0;
		else if (out_port == (in_port + 2) % 3) bit = 1;
		else pigen_fail("fabric route attempted a U-turn");
		if (connection->hops >= 64) pigen_fail("fabric route exceeds the 64-bit path limit");
		connection->path_word |= (unsigned long long)bit << connection->hops++;
	}
	free(previous); free(queue); free(path);
}

static fabric_topology build_fabric_topology(fabric_block *fabric)
{
	fabric_topology topology = {0};
	size_t leaf_count = 1;
	topology_node *level;
	size_t level_count;
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		if (connection->direct) continue;
		add_endpoint(&topology, (fabric_endpoint){.direction = 1, .source = connection->source});
		add_endpoint(&topology, (fabric_endpoint){.direction = 0, .destination = connection->destination});
	}
	if (!topology.endpoint_count) return topology;
	qsort(topology.endpoints, topology.endpoint_count, sizeof(*topology.endpoints), endpoint_compare);
	topology.endpoint_router = pigen_resize(NULL, topology.endpoint_count * sizeof(*topology.endpoint_router));
	topology.endpoint_port = pigen_resize(NULL, topology.endpoint_count * sizeof(*topology.endpoint_port));
	for (size_t index = 0; index < topology.endpoint_count; index++) topology.endpoint_router[index] = topology.endpoint_port[index] = -1;
	while (leaf_count < topology.endpoint_count) leaf_count *= 2;
	level = pigen_resize(NULL, leaf_count * sizeof(*level)); level_count = leaf_count;
	for (size_t index = 0; index < leaf_count; index++)
		level[index] = index < topology.endpoint_count ? (topology_node){2, (int)index} : (topology_node){0, 0};
	while (level_count > 1)
	{
		for (size_t index = 0; index < level_count; index += 2)
			level[index / 2] = make_topology_parent(&topology, level[index], level[index + 1]);
		level_count /= 2;
	}
	if (level[0].kind != 1) pigen_fail("routed fabric requires at least two endpoints");
	topology.routers[level[0].index].ports[0] = (fabric_attachment){0, 0, 0};
	free(level);
	for (size_t index = 0; index < fabric->connection_count; index++)
		if (!fabric->connections[index].direct)
		{
			route_connection(&topology, &fabric->connections[index]);
			if (fabric->connections[index].hops > topology.path_width)
				topology.path_width = fabric->connections[index].hops;
		}
	for (size_t index = 0; index < fabric->connection_count; index++)
		if (!fabric->connections[index].direct)
			fabric->connections[index].delivered_word = rotate_right_word(
				fabric->connections[index].path_word, topology.path_width,
				fabric->connections[index].hops);
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		fabric_endpoint source = {.direction = 1, .source = connection->source};
		fabric_endpoint destination = {.direction = 0, .destination = connection->destination};
		int source_index;
		int destination_index_;
		if (connection->direct) continue;
		source_index = endpoint_index(&topology, &source);
		destination_index_ = endpoint_index(&topology, &destination);
		if (trace_forward_endpoint(&topology, source_index, connection->path_word) != destination_index_)
			pigen_fail("generated fabric route did not reach its destination");
		if (trace_reverse_endpoint(&topology, destination_index_, connection->delivered_word) != source_index)
			pigen_fail("generated fabric route is not reversible");
		for (size_t previous = 0; previous < index; previous++)
			if (!fabric->connections[previous].direct &&
				destination_equal(&fabric->connections[previous].destination, &connection->destination) &&
				fabric->connections[previous].delivered_word == connection->delivered_word)
				pigen_fail("generated fabric routes collide at a destination");
	}
	return topology;
}

#include "fabric_svg.inc"

static void append_fabric_diagram(pigen_fabric_diagrams *diagrams,
	const fabric_block *fabric, const fabric_topology *topology)
{
	pigen_string svg = {0};
	if (!diagrams) return;
	render_fabric_diagram(&svg, fabric, topology);
	diagrams->items = pigen_resize(diagrams->items,
		(diagrams->count + 1) * sizeof(*diagrams->items));
	diagrams->items[diagrams->count].name = pigen_copy_range(fabric->name, strlen(fabric->name));
	diagrams->items[diagrams->count].svg = svg.data;
	diagrams->count++;
}

static void append_source_text(pigen_string *output, const fabric_source *source, const char *suffix)
{
	pigen_append_format(output, "%s__%s__%s__%s", source->instance, source->port, source->handle, suffix);
}

static void append_destination_text(pigen_string *output, const fabric_destination *destination, const char *suffix)
{
	pigen_append_format(output, "%s__%s__%s", destination->instance, destination->port, suffix);
}

static int destination_is_routed(const fabric_block *fabric, const fabric_destination *destination)
{
	for (size_t index = 0; index < fabric->connection_count; index++)
		if (!fabric->connections[index].direct &&
			destination_equal(&fabric->connections[index].destination, destination)) return 1;
	return 0;
}

static int destination_first_at(const fabric_block *fabric, size_t connection)
{
	for (size_t index = 0; index < connection; index++)
		if (destination_equal(&fabric->connections[index].destination,
			&fabric->connections[connection].destination)) return 0;
	return 1;
}

static void append_fabric_parameter_block(pigen_string *output, const fabric_block *fabric,
	const fabric_topology *topology)
{
	pigen_append(output, " #(\n");
	for (size_t index = 0; index < fabric->parameter_count; index++)
		pigen_append_format(output, "\tparameter integer %s = %s,\n",
			fabric->parameters[index].name, fabric->parameters[index].value);
	if (topology->router_count)
		pigen_append_format(output, "\tparameter integer PATH_W = %d\n", topology->path_width);
	else
	{
		/* Remove the final comma when a direct-only fabric has no PATH_W. */
		if (output->length >= 2 && output->data[output->length - 2] == ',')
		{
			memmove(output->data + output->length - 2, output->data + output->length - 1, 2);
			output->length--;
		}
	}
	pigen_append(output, ")");
}

static void render_fabric_skid(pigen_string *output, const fabric_block *fabric)
{
	pigen_append_format(output,
		"module %s__fabric_skid #(parameter integer PACKET_W = 1)\n"
		"\t(input logic clk, input logic reset, input logic enable, input logic in_valid, output logic in_ready, "
		"input logic [PACKET_W-1:0] packet_in, output logic out_valid, input logic out_ready, output logic [PACKET_W-1:0] packet_out);\n"
		"\tlogic [1:0] count; logic [PACKET_W-1:0] packet0, packet1;\n"
		"\twire push = in_valid & in_ready; wire pop = out_valid & out_ready;\n"
		"\tassign in_ready = enable & (count != 2); assign out_valid = enable & (count != 0); assign packet_out = packet0;\n"
		"\talways_ff @(posedge clk) begin\n\t\tif (reset) begin count <= 0; packet0 <= '0; packet1 <= '0; end\n"
		"\t\telse if (enable) case ({push, pop})\n"
		"\t\t\t2'b10: begin if (count == 0) packet0 <= packet_in; else packet1 <= packet_in; count <= count + 1'b1; end\n"
		"\t\t\t2'b01: begin if (count == 2) packet0 <= packet1; count <= count - 1'b1; end\n"
		"\t\t\t2'b11: begin if (count == 2) begin packet0 <= packet1; packet1 <= packet_in; end else packet0 <= packet_in; end\n"
		"\t\t\tdefault: begin end\n\t\tendcase\n\tend\nendmodule\n\n", fabric->name);
}

static void render_fabric_router(pigen_string *output, const fabric_block *fabric)
{
	pigen_append_format(output,
		"module %s__fabric_router #(parameter integer PAYLOAD_W = 1, parameter integer PATH_W = 1, parameter integer PACKET_W = PAYLOAD_W + PATH_W)\n\t(\n",
		fabric->name);
	pigen_append(output, "\t\tinput logic clk, input logic reset, input logic enable,\n");
	for (int port = 0; port < 3; port++)
	{
		pigen_append_format(output,
			"\t\tinput logic p%d_in_valid, output logic p%d_in_ready, input logic [PACKET_W-1:0] p%d_in_packet,\n"
			"\t\toutput logic p%d_out_valid, input logic p%d_out_ready, output logic [PACKET_W-1:0] p%d_out_packet%s\n",
			port, port, port, port, port, port, port == 2 ? "" : ",");
	}
	pigen_append(output, "\t);\n\tfunction automatic [PACKET_W-1:0] forward_packet(input logic [PACKET_W-1:0] packet);\n"
		"\t\tlogic [PATH_W-1:0] path; begin path = packet[PACKET_W-1:PAYLOAD_W]; path = path >> 1; "
		"path[PATH_W-1] = packet[PAYLOAD_W]; forward_packet = {path, packet[PAYLOAD_W-1:0]}; end\n\tendfunction\n");
	for (int port = 0; port < 3; port++)
		pigen_append_format(output, "\tlogic [1:0] p%d_count; logic [PACKET_W-1:0] p%d_packet0, p%d_packet1; logic [1:0] grant%d; logic grant%d_valid, rr%d;\n",
			port, port, port, port, port, port);
	for (int port = 0; port < 3; port++)
		pigen_append_format(output, "\twire p%d_remove; wire p%d_push = p%d_in_valid & p%d_in_ready; assign p%d_in_ready = enable & (p%d_count != 2);\n",
			port, port, port, port, port, port);
	for (int input = 0; input < 3; input++)
		for (int target = 0; target < 3; target++) if (input != target)
			pigen_append_format(output, "\twire p%d_to_p%d = (p%d_count != 0) & %sp%d_packet0[PAYLOAD_W];\n",
				input, target, input, target == (input + 1) % 3 ? "~" : "", input);
	for (int target = 0; target < 3; target++)
	{
		int first = (target + 2) % 3, second = (target + 1) % 3;
		pigen_append_format(output,
			"\talways @* begin grant%d_valid = 1'b0; grant%d = 0; p%d_out_valid = 1'b0; p%d_out_packet = '0;\n"
			"\t\tif (p%d_to_p%d & p%d_to_p%d) begin grant%d_valid = 1'b1; grant%d = rr%d ? %d : %d; end\n"
			"\t\telse if (p%d_to_p%d) begin grant%d_valid = 1'b1; grant%d = %d; end\n"
			"\t\telse if (p%d_to_p%d) begin grant%d_valid = 1'b1; grant%d = %d; end\n"
			"\t\tif (enable & grant%d_valid) begin p%d_out_valid = 1'b1; case (grant%d) 0: p%d_out_packet = forward_packet(p0_packet0); 1: p%d_out_packet = forward_packet(p1_packet0); default: p%d_out_packet = forward_packet(p2_packet0); endcase end\n\tend\n",
			target, target, target, target, first, target, second, target, target, target, target, second, first,
			first, target, target, target, first, second, target, target, target, second,
			target, target, target, target, target, target);
	}
	for (int input = 0; input < 3; input++)
	{
		pigen_append_format(output, "\tassign p%d_remove = ", input);
		for (int target = 0; target < 3; target++)
			pigen_append_format(output, "%s(p%d_out_valid & p%d_out_ready & (grant%d == %d))",
				target ? " | " : "", target, target, target, input);
		pigen_append(output, ";\n");
	}
	for (int target = 0; target < 3; target++)
		pigen_append_format(output, "\twire p%d_fire = p%d_out_valid & p%d_out_ready;\n", target, target, target);
	pigen_append(output, "\talways_ff @(posedge clk) begin\n\t\tif (reset) begin\n");
	for (int port = 0; port < 3; port++)
		pigen_append_format(output, "\t\t\tp%d_count <= 0; p%d_packet0 <= '0; p%d_packet1 <= '0; rr%d <= 1'b0;\n", port, port, port, port);
	pigen_append(output, "\t\tend else if (enable) begin\n");
	for (int port = 0; port < 3; port++)
		pigen_append_format(output,
			"\t\t\tcase ({p%d_push, p%d_remove})\n"
			"\t\t\t\t2'b10: begin if (p%d_count == 0) p%d_packet0 <= p%d_in_packet; else p%d_packet1 <= p%d_in_packet; p%d_count <= p%d_count + 1'b1; end\n"
			"\t\t\t\t2'b01: begin if (p%d_count == 2) p%d_packet0 <= p%d_packet1; p%d_count <= p%d_count - 1'b1; end\n"
			"\t\t\t\t2'b11: begin if (p%d_count == 2) begin p%d_packet0 <= p%d_packet1; p%d_packet1 <= p%d_in_packet; end else p%d_packet0 <= p%d_in_packet; end\n"
			"\t\t\t\tdefault: begin end\n\t\t\tendcase\n",
			port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port, port);
	for (int target = 0; target < 3; target++)
		pigen_append_format(output, "\t\t\tif (p%d_fire) rr%d <= (grant%d == %d);\n",
			target, target, target, (target + 2) % 3);
	pigen_append(output, "\t\tend\n\tend\nendmodule\n\n");
}

static void render_fabric(pigen_string *output, fabric_block *fabric,
	pigen_fabric_diagrams *diagrams)
{
	fabric_topology topology = build_fabric_topology(fabric);
	append_fabric_diagram(diagrams, fabric, &topology);
	pigen_append_format(output, "\n// Pigen fabric block `%s`.\n", fabric->name);
	pigen_append_format(output, "// route manifest: payload=PAYLOAD_W path_width=%d\n", topology.path_width);
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		pigen_append_format(output, "//   %s.%s.%s ", connection->source.instance,
			connection->source.port, connection->source.handle);
		if (connection->direct) pigen_append(output, "> ");
		else
		{
			for (int dash = 0; dash < connection->tier; dash++) pigen_append(output, "-");
			pigen_append(output, "> ");
		}
		pigen_append_format(output, "%s.%s", connection->destination.instance,
			connection->destination.port);
		if (connection->recognized_source)
			pigen_append_format(output, ".%s", connection->recognized_source);
		if (connection->direct) pigen_append(output, " direct\n");
		else pigen_append_format(output, " hops=%d path=%llu delivered=%llu\n",
			connection->hops, connection->path_word, connection->delivered_word);
	}
	render_fabric_skid(output, fabric);
	if (topology.router_count) render_fabric_router(output, fabric);
	pigen_append_format(output, "module %s", fabric->name);
	append_fabric_parameter_block(output, fabric, &topology);
	pigen_append(output, "\n\t(\n\t\tinput logic clk,\n\t\tinput logic reset,\n\t\tinput logic enable");
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_source *source = &fabric->connections[index].source;
		pigen_append(output, ",\n\t\tinput logic "); append_source_text(output, source, "valid");
		pigen_append(output, ",\n\t\toutput logic "); append_source_text(output, source, "ready");
		pigen_append(output, ",\n\t\tinput logic [PAYLOAD_W-1:0] "); append_source_text(output, source, "payload");
	}
	for (size_t index = 0; index < fabric->connection_count; index++) if (destination_first_at(fabric, index))
	{
		fabric_destination *destination = &fabric->connections[index].destination;
		pigen_append(output, ",\n\t\toutput logic "); append_destination_text(output, destination, "valid");
		pigen_append(output, ",\n\t\tinput logic "); append_destination_text(output, destination, "ready");
		pigen_append(output, ",\n\t\toutput logic [PAYLOAD_W-1:0] "); append_destination_text(output, destination, "payload");
		if (destination_is_routed(fabric, destination))
		{
			pigen_append(output, ",\n\t\toutput logic [PATH_W-1:0] "); append_destination_text(output, destination, "path");
		}
	}
	pigen_append(output, "\n\t);\n");
	if (topology.router_count)
	{
		pigen_append(output, "\tlocalparam integer PACKET_W = PATH_W + PAYLOAD_W;\n");
		for (size_t index = 0; index < fabric->connection_count; index++) if (!fabric->connections[index].direct)
		{
			fabric_connection *connection = &fabric->connections[index];
			pigen_append(output, "\tlocalparam logic [PATH_W-1:0] ROUTE__");
			pigen_append_format(output, "%s__%s__%s = %llu;\n", connection->source.instance,
				connection->source.port, connection->source.handle, connection->path_word);
			if (connection->recognized_source)
				pigen_append_format(output, "\tlocalparam logic [PATH_W-1:0] %s__%s__SOURCE__%s = %llu;\n",
					connection->destination.instance, connection->destination.port,
					connection->recognized_source, connection->delivered_word);
		}
	}
	/* Endpoint skid queues. */
	for (size_t index = 0; index < fabric->connection_count; index++)
	{
		fabric_connection *connection = &fabric->connections[index];
		const char *width = connection->direct ? "PAYLOAD_W" : "PACKET_W";
		pigen_append(output, "\twire ["); pigen_append(output, width); pigen_append(output, "-1:0] "); append_source_text(output, &connection->source, "packet_in"); pigen_append(output, ";\n\twire ");
		append_source_text(output, &connection->source, "packet_valid"); pigen_append(output, ", "); append_source_text(output, &connection->source, "packet_ready");
		pigen_append(output, ";\n\twire ["); pigen_append(output, width); pigen_append(output, "-1:0] "); append_source_text(output, &connection->source, "packet"); pigen_append(output, ";\n\tassign "); append_source_text(output, &connection->source, "packet_in"); pigen_append(output, " = ");
		if (connection->direct) append_source_text(output, &connection->source, "payload");
		else { pigen_append(output, "{ROUTE__"); pigen_append_format(output, "%s__%s__%s, ", connection->source.instance, connection->source.port, connection->source.handle); append_source_text(output, &connection->source, "payload"); pigen_append(output, "}"); }
		pigen_append(output, ";\n\t"); pigen_append_format(output, "%s__fabric_skid #(.PACKET_W(%s)) u_tx__%s__%s__%s (", fabric->name, width, connection->source.instance, connection->source.port, connection->source.handle);
		pigen_append(output, ".clk(clk), .reset(reset), .enable(enable), .in_valid("); append_source_text(output, &connection->source, "valid"); pigen_append(output, "), .in_ready("); append_source_text(output, &connection->source, "ready"); pigen_append(output, "), .packet_in("); append_source_text(output, &connection->source, "packet_in"); pigen_append(output, "), .out_valid("); append_source_text(output, &connection->source, "packet_valid"); pigen_append(output, "), .out_ready("); append_source_text(output, &connection->source, "packet_ready"); pigen_append(output, "), .packet_out("); append_source_text(output, &connection->source, "packet"); pigen_append(output, "));\n");
	}
	for (size_t index = 0; index < fabric->connection_count; index++) if (destination_first_at(fabric, index))
	{
		fabric_destination *destination = &fabric->connections[index].destination;
		int routed = destination_is_routed(fabric, destination);
		const char *width = routed ? "PACKET_W" : "PAYLOAD_W";
		pigen_append(output, "\twire "); append_destination_text(output, destination, "packet_valid"); pigen_append(output, ", "); append_destination_text(output, destination, "packet_ready"); pigen_append(output, ";\n\twire ["); pigen_append(output, width); pigen_append(output, "-1:0] "); append_destination_text(output, destination, "packet"); pigen_append(output, ", "); append_destination_text(output, destination, "packet_out"); pigen_append(output, ";\n\t");
		pigen_append_format(output, "%s__fabric_skid #(.PACKET_W(%s)) u_rx__%s__%s (", fabric->name, width, destination->instance, destination->port);
		pigen_append(output, ".clk(clk), .reset(reset), .enable(enable), .in_valid("); append_destination_text(output, destination, "packet_valid"); pigen_append(output, "), .in_ready("); append_destination_text(output, destination, "packet_ready"); pigen_append(output, "), .packet_in("); append_destination_text(output, destination, "packet"); pigen_append(output, "), .out_valid("); append_destination_text(output, destination, "valid"); pigen_append(output, "), .out_ready("); append_destination_text(output, destination, "ready"); pigen_append(output, "), .packet_out("); append_destination_text(output, destination, "packet_out"); pigen_append(output, "));\n\tassign "); append_destination_text(output, destination, "payload"); pigen_append(output, " = "); append_destination_text(output, destination, "packet_out");
		if (routed) pigen_append(output, "[PAYLOAD_W-1:0]");
		pigen_append(output, ";\n");
		if (routed) { pigen_append(output, "\tassign "); append_destination_text(output, destination, "path"); pigen_append(output, " = "); append_destination_text(output, destination, "packet_out"); pigen_append(output, "[PACKET_W-1:PAYLOAD_W];\n"); }
	}
	/* Router instances and physical links. */
	for (size_t router = 0; router < topology.router_count; router++) for (int port = 0; port < 3; port++)
		pigen_append_format(output, "\twire r%zu_p%d_in_valid, r%zu_p%d_in_ready; wire [PACKET_W-1:0] r%zu_p%d_in_packet; wire r%zu_p%d_out_valid, r%zu_p%d_out_ready; wire [PACKET_W-1:0] r%zu_p%d_out_packet;\n", router, port, router, port, router, port, router, port, router, port, router, port);
	for (size_t router = 0; router < topology.router_count; router++)
	{
		pigen_append_format(output, "\t%s__fabric_router #(.PAYLOAD_W(PAYLOAD_W), .PATH_W(PATH_W)) u_r%zu (.clk(clk), .reset(reset), .enable(enable)", fabric->name, router);
		for (int port = 0; port < 3; port++) pigen_append_format(output, ", .p%d_in_valid(r%zu_p%d_in_valid), .p%d_in_ready(r%zu_p%d_in_ready), .p%d_in_packet(r%zu_p%d_in_packet), .p%d_out_valid(r%zu_p%d_out_valid), .p%d_out_ready(r%zu_p%d_out_ready), .p%d_out_packet(r%zu_p%d_out_packet)", port, router, port, port, router, port, port, router, port, port, router, port, port, router, port, port, router, port);
		pigen_append(output, ");\n");
	}
	for (size_t router = 0; router < topology.router_count; router++) for (int port = 0; port < 3; port++)
	{
		fabric_attachment *attachment = &topology.routers[router].ports[port];
		if (!attachment->kind)
			pigen_append_format(output, "\tassign r%zu_p%d_in_valid = 1'b0; assign r%zu_p%d_in_packet = '0; assign r%zu_p%d_out_ready = 1'b1;\n", router, port, router, port, router, port);
		else if (attachment->kind == 1 && ((int)router < attachment->target || ((int)router == attachment->target && port < attachment->port)))
		{
			pigen_append_format(output, "\tassign r%d_p%d_in_valid = r%zu_p%d_out_valid; assign r%d_p%d_in_packet = r%zu_p%d_out_packet; assign r%zu_p%d_out_ready = r%d_p%d_in_ready;\n", attachment->target, attachment->port, router, port, attachment->target, attachment->port, router, port, router, port, attachment->target, attachment->port);
			pigen_append_format(output, "\tassign r%zu_p%d_in_valid = r%d_p%d_out_valid; assign r%zu_p%d_in_packet = r%d_p%d_out_packet; assign r%d_p%d_out_ready = r%zu_p%d_in_ready;\n", router, port, attachment->target, attachment->port, router, port, attachment->target, attachment->port, attachment->target, attachment->port, router, port);
		}
		else if (attachment->kind == 2)
		{
			fabric_endpoint *endpoint = &topology.endpoints[attachment->target];
			if (endpoint->direction)
			{
				pigen_append_format(output, "\tassign r%zu_p%d_in_valid = ", router, port); append_source_text(output, &endpoint->source, "packet_valid"); pigen_append_format(output, "; assign r%zu_p%d_in_packet = ", router, port); append_source_text(output, &endpoint->source, "packet"); pigen_append(output, "; assign "); append_source_text(output, &endpoint->source, "packet_ready"); pigen_append_format(output, " = r%zu_p%d_in_ready; assign r%zu_p%d_out_ready = 1'b1;\n", router, port, router, port);
			}
			else
			{
				pigen_append(output, "\tassign "); append_destination_text(output, &endpoint->destination, "packet_valid"); pigen_append_format(output, " = r%zu_p%d_out_valid; assign ", router, port); append_destination_text(output, &endpoint->destination, "packet"); pigen_append_format(output, " = r%zu_p%d_out_packet; assign r%zu_p%d_out_ready = ", router, port, router, port); append_destination_text(output, &endpoint->destination, "packet_ready"); pigen_append_format(output, "; assign r%zu_p%d_in_valid = 1'b0; assign r%zu_p%d_in_packet = '0;\n", router, port, router, port);
			}
		}
	}
	for (size_t index = 0; index < fabric->connection_count; index++) if (fabric->connections[index].direct)
	{
		fabric_connection *connection = &fabric->connections[index];
		pigen_append(output, "\tassign "); append_destination_text(output, &connection->destination, "packet_valid"); pigen_append(output, " = "); append_source_text(output, &connection->source, "packet_valid"); pigen_append(output, "; assign "); append_destination_text(output, &connection->destination, "packet"); pigen_append(output, " = "); append_source_text(output, &connection->source, "packet"); pigen_append(output, "; assign "); append_source_text(output, &connection->source, "packet_ready"); pigen_append(output, " = "); append_destination_text(output, &connection->destination, "packet_ready"); pigen_append(output, ";\n");
	}
	pigen_append(output, "endmodule\n\n");
	free(topology.routers);
	free(topology.endpoints);
	free(topology.endpoint_router);
	free(topology.endpoint_port);
}

static void append_parameter_block(pigen_string *output, const pipeline_block *pipeline)
{
	if (!pipeline->parameter_count) return;
	pigen_append(output, " #(\n");
	for (size_t index = 0; index < pipeline->parameter_count; index++)
		pigen_append_format(output, "\tparameter integer %s = %s%s\n",
			pipeline->parameters[index].name, pipeline->parameters[index].value,
			index + 1 == pipeline->parameter_count ? "" : ",");
	pigen_append(output, ")");
}

static void append_width_sum(pigen_string *output, pipeline_signal *signals, size_t count)
{
	for (size_t index = 0; index < count; index++)
	{
		char *width = signal_width(&signals[index]);
		if (index) pigen_append(output, " + ");
		pigen_append_format(output, "(%s)", width);
		free(width);
	}
}

static void append_port_header(pigen_string *output, pipeline_signal *inputs, size_t input_count,
	pipeline_signal *outputs, size_t output_count)
{
	pigen_append(output, "\n\t(\n\t\tinput logic clk,\n\t\tinput logic reset,\n\t\tinput logic enable,\n"
		"\t\tinput logic in_valid,\n\t\toutput logic in_ready,\n\t\toutput logic out_valid,\n"
		"\t\tinput logic out_ready,\n\t\tinput logic [");
	append_width_sum(output, inputs, input_count);
	pigen_append(output, "-1:0] packet_in,\n\t\toutput logic [");
	append_width_sum(output, outputs, output_count);
	pigen_append(output, "-1:0] packet_out\n\t);\n");
}

static void render_pipeline_skid(pigen_string *output, const pipeline_block *pipeline)
{
	pigen_append_format(output,
		"module %s__skid #(parameter integer PACKET_WIDTH = 1)\n"
		"\t(input logic clk, input logic reset, input logic enable, input logic in_valid, output logic in_ready, "
		"output logic out_valid, input logic out_ready, input logic [PACKET_WIDTH-1:0] packet_in, output logic [PACKET_WIDTH-1:0] packet_out);\n"
		"\tlogic [PACKET_WIDTH-1:0] packet_skid; logic skid;\n"
		"\tassign in_ready = ~(out_valid & skid);\n"
		"\twire take_in = in_ready & in_valid; wire take_out = out_valid & out_ready;\n"
		"\talways_ff @(posedge clk) begin\n\t\tif (reset) begin skid <= 1'b0; out_valid <= 1'b0; packet_out <= '0; end\n"
		"\t\telse if (enable) case ({take_in, take_out})\n"
		"\t\t\t2'b00: begin end\n"
		"\t\t\t2'b01: if (skid) begin packet_out <= packet_skid; skid <= 1'b0; end else out_valid <= 1'b0;\n"
		"\t\t\t2'b10: if (out_valid) begin packet_skid <= packet_in; skid <= 1'b1; end else begin packet_out <= packet_in; out_valid <= 1'b1; end\n"
		"\t\t\t2'b11: if (skid) begin packet_out <= packet_skid; packet_skid <= packet_in; end else packet_out <= packet_in;\n"
		"\t\tendcase\n\tend\nendmodule\n\n", pipeline->name);
}

static void append_signal_type(pigen_string *output, const pipeline_signal *signal)
{
	pigen_append_format(output, "%s%s [%s:%s]", signal->base,
		signal->is_signed ? " signed" : "", signal->upper, signal->lower);
}

static void render_pipeline_stage(pigen_string *output, const pipeline_block *pipeline,
	const pipeline_stage *stage)
{
	pigen_append_format(output, "module %s__stage_%s", pipeline->name, stage->name);
	append_parameter_block(output, pipeline);
	append_port_header(output, stage->inputs, stage->input_count, stage->outputs, stage->output_count);
	for (size_t index = 0; index < stage->input_count; index++)
	{
		pigen_string offset = {0};
		char *width = signal_width(&stage->inputs[index]);
		append_signal_type(output, &stage->inputs[index]);
		pigen_append_format(output, " %s;\n", stage->inputs[index].name);
		if (index + 1 < stage->input_count)
			append_width_sum(&offset, &stage->inputs[index + 1], stage->input_count - index - 1);
		else pigen_append(&offset, "0");
		pigen_append_format(output, "\tassign %s = packet_in[%s +: (%s)];\n",
			stage->inputs[index].name, offset.data, width);
		free(offset.data); free(width);
	}
	for (size_t index = 0; index < stage->declaration_count; index++)
	{
		pipeline_declaration *declaration = &stage->declarations[index];
		pigen_append(output, "\t"); append_signal_type(output, &declaration->signal);
		pigen_append_format(output, " %s", declaration->signal.name);
		if (!strcmp(declaration->signal.base, "wire") && declaration->initializer)
			pigen_append_format(output, " = %s", declaration->initializer);
		pigen_append(output, ";\n");
	}
	pigen_append(output, "\tlogic ["); append_width_sum(output, stage->outputs, stage->output_count);
	pigen_append(output, "-1:0] packet_comb;\n\tassign in_ready = ~out_valid | out_ready;\n"
		"\twire take_in = in_ready & in_valid; wire take_out = out_valid & out_ready;\n\talways_comb begin\n");
	for (size_t index = 0; index < stage->declaration_count; index++)
		if (stage->declarations[index].initializer && strcmp(stage->declarations[index].signal.base, "wire"))
			pigen_append_format(output, "\t\t%s = %s;\n", stage->declarations[index].signal.name,
				stage->declarations[index].initializer);
	pigen_append(output, stage->body);
	pigen_append(output, "\n\t\tpacket_comb = {");
	for (size_t index = 0; index < stage->output_count; index++)
		pigen_append_format(output, "%s%s", index ? ", " : "", stage->outputs[index].name);
	pigen_append(output, "};\n\tend\n\talways_ff @(posedge clk) begin\n"
		"\t\tif (reset) begin out_valid <= 1'b0; packet_out <= '0; end\n"
		"\t\telse if (enable) begin\n\t\t\tif (take_in) begin packet_out <= packet_comb; out_valid <= 1'b1; end\n"
		"\t\t\telse if (take_out) out_valid <= 1'b0;\n\t\tend\n\tend\nendmodule\n\n");
}

static int stage_has_skid(const pipeline_block *pipeline, size_t stage)
{
	int periodic = pipeline->skid_step > 0 && ((int)stage + 1) % pipeline->skid_step == 0;
	return (periodic && !pipeline->stages[stage].suppress_skid) || pipeline->stages[stage].force_skid;
}

static void render_pipeline_top(pigen_string *output, const pipeline_block *pipeline)
{
	size_t component_count = pipeline->stage_count;
	for (size_t index = 0; index < pipeline->stage_count; index++) if (stage_has_skid(pipeline, index)) component_count++;
	pigen_append_format(output, "module %s", pipeline->name);
	append_parameter_block(output, pipeline);
	append_port_header(output, pipeline->stages[0].inputs, pipeline->stages[0].input_count,
		pipeline->stages[pipeline->stage_count - 1].outputs, pipeline->stages[pipeline->stage_count - 1].output_count);
	size_t component = 0;
	for (size_t stage = 0; stage < pipeline->stage_count; stage++)
	{
		pigen_append_format(output, "\twire c%zu_valid, c%zu_ready; wire [", component, component);
		append_width_sum(output, pipeline->stages[stage].outputs, pipeline->stages[stage].output_count);
		pigen_append_format(output, "-1:0] c%zu_packet;\n", component++);
		if (stage_has_skid(pipeline, stage))
		{
			pigen_append_format(output, "\twire c%zu_valid, c%zu_ready; wire [", component, component);
			append_width_sum(output, pipeline->stages[stage].outputs, pipeline->stages[stage].output_count);
			pigen_append_format(output, "-1:0] c%zu_packet;\n", component++);
		}
	}
	component = 0;
	for (size_t stage = 0; stage < pipeline->stage_count; stage++)
	{
		pipeline_stage *item = &pipeline->stages[stage];
		pigen_append_format(output, "\t%s__stage_%s", pipeline->name, item->name);
		if (pipeline->parameter_count)
		{
			pigen_append(output, " #(");
			for (size_t index = 0; index < pipeline->parameter_count; index++)
				pigen_append_format(output, "%s.%s(%s)", index ? ", " : "",
					pipeline->parameters[index].name, pipeline->parameters[index].name);
			pigen_append(output, ")");
		}
		pigen_append_format(output, " u_%s (.clk(clk), .reset(reset), .enable(enable), ", item->name);
		if (component)
			pigen_append_format(output, ".in_valid(c%zu_valid), .in_ready(c%zu_ready), .packet_in(c%zu_packet), ",
				component - 1, component - 1, component - 1);
		else
			pigen_append(output, ".in_valid(in_valid), .in_ready(in_ready), .packet_in(packet_in), ");
		pigen_append_format(output, ".out_valid(c%zu_valid), .out_ready(%s", component,
			component + 1 == component_count ? "out_ready" : "");
		if (component + 1 != component_count) pigen_append_format(output, "c%zu_ready", component);
		pigen_append_format(output, "), .packet_out(c%zu_packet));\n", component);
		component++;
		if (stage_has_skid(pipeline, stage))
		{
			pigen_append_format(output, "\t%s__skid #(.PACKET_WIDTH(", pipeline->name);
			append_width_sum(output, item->outputs, item->output_count);
			pigen_append_format(output, ")) u_skid_%zu (.clk(clk), .reset(reset), .enable(enable), .in_valid(c%zu_valid), .in_ready(c%zu_ready), .out_valid(c%zu_valid), .out_ready(",
				stage + 1, component - 1, component - 1, component);
			if (component + 1 == component_count) pigen_append(output, "out_ready");
			else pigen_append_format(output, "c%zu_ready", component);
			pigen_append_format(output, "), .packet_in(c%zu_packet), .packet_out(c%zu_packet));\n",
				component - 1, component);
			component++;
		}
	}
	pigen_append_format(output, "\tassign out_valid = c%zu_valid; assign packet_out = c%zu_packet;\nendmodule\n\n",
		component_count - 1, component_count - 1);
}

static void render_pipeline(pigen_string *output, const pipeline_block *pipeline)
{
	pigen_append(output, "\n// Pigen pipeline block.\n");
	render_pipeline_skid(output, pipeline);
	for (size_t index = 0; index < pipeline->stage_count; index++)
		render_pipeline_stage(output, pipeline, &pipeline->stages[index]);
	render_pipeline_top(output, pipeline);
}

static int ordinary_unit_end(block_parser *parser, const char **closing)
{
	static const struct { const char *opening; const char *closing; } units[] = {
		{"module", "endmodule"}, {"interface", "endinterface"},
		{"package", "endpackage"}, {"program", "endprogram"},
		{"class", "endclass"}, {"checker", "endchecker"}
	};
	for (size_t index = 0; index < sizeof(units) / sizeof(units[0]); index++)
		if (token_is(parser, parser->at, units[index].opening))
		{
			*closing = units[index].closing;
			return 1;
		}
	return 0;
}

char *pigen_lower_blocks(const char *source, size_t length, pigen_string *generated,
	pigen_fabric_diagrams *diagrams)
{
	block_parser parser = {source, {0}, 0};
	char *cleaned = pigen_copy_range(source, length);
	pigen_lex_source(source, length, &parser.tokens);
	while (current(&parser)->kind != PIGEN_TOKEN_EOF)
	{
		const char *closing;
		if (ordinary_unit_end(&parser, &closing))
		{
			while (current(&parser)->kind != PIGEN_TOKEN_EOF &&
				!token_is(&parser, parser.at, closing)) parser.at++;
			if (current(&parser)->kind == PIGEN_TOKEN_EOF)
				pigen_fail("unterminated SystemVerilog design unit");
			parser.at++;
			continue;
		}
		if (token_is(&parser, parser.at, "pipeline"))
		{
			size_t start = current(&parser)->span.start;
			size_t end;
			pipeline_block pipeline = parse_pipeline(&parser, &end);
			for (size_t index = start; index < end; index++) if (cleaned[index] != '\n') cleaned[index] = ' ';
			render_pipeline(generated, &pipeline);
			continue;
		}
		if (token_is(&parser, parser.at, "fabric"))
		{
			size_t start = current(&parser)->span.start;
			size_t end;
			fabric_block fabric = parse_fabric(&parser, &end);
			for (size_t index = start; index < end; index++) if (cleaned[index] != '\n') cleaned[index] = ' ';
			render_fabric(generated, &fabric, diagrams);
			continue;
		}
		parser.at++;
	}
	pigen_free_tokens(&parser.tokens);
	return cleaned;
}

void pigen_free_fabric_diagrams(pigen_fabric_diagrams *diagrams)
{
	if (!diagrams) return;
	for (size_t index = 0; index < diagrams->count; index++)
	{
		free(diagrams->items[index].name);
		free(diagrams->items[index].svg);
	}
	free(diagrams->items);
	*diagrams = (pigen_fabric_diagrams){0};
}
